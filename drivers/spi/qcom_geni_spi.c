// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm GENI QUPv3 SPI controller driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <log.h>
#include <dm.h>
#include <dm/device.h>
#include <dm/read.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <cpu_func.h>
#include <spi.h>
#include <clk.h>
#include <time.h>
#include <soc/qcom/geni-se.h>
#include <soc/qcom/qup-fw-load.h>

/* SPI-protocol-specific SE registers, on top of the generic ones in geni-se.h */
#define SE_SPI_CPHA			0x224
#define SE_SPI_LOOPBACK		0x22c
#define SE_SPI_CPOL			0x230
#define SE_SPI_DEMUX_OUTPUT_INV	0x24c
#define SE_SPI_DEMUX_SEL		0x250
#define SE_SPI_TRANS_CFG		0x25c
#define SE_SPI_DELAYS_COUNTERS		0x278
#define SE_SPI_WORD_LEN		0x268
#define SE_SPI_TX_TRANS_LEN		0x26c
#define SE_SPI_RX_TRANS_LEN		0x270

#define CPHA				BIT(0)

#define LOOPBACK_ENABLE			0x1
#define LOOPBACK_MSK			GENMASK(1, 0)

#define CPOL				BIT(2)

/* SE_SPI_TRANS_CFG */
#define CS_TOGGLE			BIT(1)

#define WORD_LEN_MSK			GENMASK(9, 0)
#define SPI_WORD_LEN_BITS		8
#define MIN_WORD_LEN			4

#define SPI_TX_ONLY			1
#define SPI_RX_ONLY			2
#define SPI_TX_RX			7
#define FRAGMENTATION			BIT(2)

#define SPI_ERR	(M_CMD_OVERRUN_EN | M_ILLEGAL_CMD_EN | M_CMD_FAILURE_EN | \
		  M_RX_FIFO_RD_ERR_EN | M_RX_FIFO_WR_ERR_EN | \
		  M_TX_FIFO_RD_ERR_EN | M_TX_FIFO_WR_ERR_EN)

#define SPI_XFER_TIMEOUT_MS		250

/* SPI-NOR reads/writes are page/sector sized; skip DMA setup below this */
#define SPI_DMA_MIN_XFER_BYTES		64

struct qcom_geni_spi_priv {
	phys_addr_t wrapper;
	phys_addr_t base;
	struct clk se;
	u32 tx_wm;
	u32 tx_fifo_depth;
	u32 bpw;
	u32 bytes_per_fifo_word;
	u32 oversampling;
	bool fifo_capable;
};

/* Bytes packed into each 32-bit FIFO word, based on word length */
static unsigned int qcom_geni_spi_bytes_per_fifo_word(unsigned int bpw)
{
	if (bpw <= 8)
		return 4;
	else if (bpw <= 16)
		return 2;
	return 1;
}

/* Scale the wait budget with transfer size instead of a flat constant */
static ulong qcom_geni_spi_xfer_timeout_ms(struct qcom_geni_spi_priv *priv,
					   unsigned int len)
{
	ulong rate = clk_get_rate(&priv->se);
	ulong ms;

	if (IS_ERR_VALUE(rate) || !rate)
		return SPI_XFER_TIMEOUT_MS + len / 1000;

	/* len is in bytes; add generous margin for controller/DMA overhead */
	ms = DIV_ROUND_UP((u64)len * 8 * 1000, rate) * 4;

	return max_t(ulong, ms, SPI_XFER_TIMEOUT_MS);
}

#define NUM_PACKING_VECTORS		4
#define PACKING_START_SHIFT		5
#define PACKING_DIR_SHIFT		4
#define PACKING_LEN_SHIFT		1
#define PACKING_STOP_BIT		BIT(0)
#define PACKING_VECTOR_SHIFT		10

/* Configure how the SE packs/unpacks "bpw"-bit words into 32-bit FIFO entries */
static void qcom_geni_spi_config_packing(struct qcom_geni_spi_priv *priv, int bpw,
					 bool msb_to_lsb)
{
	u32 cfg0, cfg1, cfg[NUM_PACKING_VECTORS] = {0};
	int len, temp_bpw = bpw;
	int idx_start = msb_to_lsb ? bpw - 1 : 0;
	int idx = idx_start;
	int idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	int i, iter, pack_words;
	unsigned int ceil_bpw;

	if (bpw <= 8)
		pack_words = 4;
	else if (bpw <= 16)
		pack_words = 2;
	else
		pack_words = 1;

	ceil_bpw = (bpw & (BITS_PER_BYTE - 1)) ?
		((bpw & ~(BITS_PER_BYTE - 1)) + BITS_PER_BYTE) : bpw;

	iter = (ceil_bpw * pack_words) >> 3;
	if (iter <= 0 || iter > NUM_PACKING_VECTORS)
		return;

	for (i = 0; i < iter; i++) {
		len = min_t(int, temp_bpw, BITS_PER_BYTE) - 1;
		cfg[i] = idx << PACKING_START_SHIFT;
		cfg[i] |= msb_to_lsb << PACKING_DIR_SHIFT;
		cfg[i] |= len << PACKING_LEN_SHIFT;

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= PACKING_STOP_BIT;
	cfg0 = cfg[0] | (cfg[1] << PACKING_VECTOR_SHIFT);
	cfg1 = cfg[2] | (cfg[3] << PACKING_VECTOR_SHIFT);

	writel(cfg0, priv->base + SE_GENI_TX_PACKING_CFG0);
	writel(cfg1, priv->base + SE_GENI_TX_PACKING_CFG1);
	writel(cfg0, priv->base + SE_GENI_RX_PACKING_CFG0);
	writel(cfg1, priv->base + SE_GENI_RX_PACKING_CFG1);

	writel(bpw / 16, priv->base + SE_GENI_BYTE_GRAN);
}

static int qcom_geni_spi_fifo_xfer(struct qcom_geni_spi_priv *priv, const u8 *tx,
				   u8 *rx, unsigned int len, ulong timeout_ms)
{
	ulong start = get_timer(0);
	unsigned int tx_cur = 0, rx_cur = 0;

	while (get_timer(start) < timeout_ms) {
		u32 status = readl(priv->base + SE_GENI_M_IRQ_STATUS);
		unsigned int i;

		if (status & SPI_ERR) {
			writel(status, priv->base + SE_GENI_M_IRQ_CLEAR);
			if (tx)
				writel(0, priv->base + SE_GENI_TX_WATERMARK_REG);
			return -EIO;
		}

		if (tx && (status & M_TX_FIFO_WATERMARK_EN)) {
			for (i = 0; i < priv->tx_wm && tx_cur < len; i++) {
				u32 word = 0;
				unsigned int p;

				for (p = 0; p < priv->bytes_per_fifo_word && tx_cur < len; p++)
					word |= tx[tx_cur++] << (p * 8);

				writel(word, priv->base + SE_GENI_TX_FIFOn);
			}

			if (tx_cur == len)
				writel(0, priv->base + SE_GENI_TX_WATERMARK_REG);
		}

		if (status & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN)) {
			u32 rxstatus = readl(priv->base + SE_GENI_RX_FIFO_STATUS);
			u32 rxcnt = rxstatus & RX_FIFO_WC_MSK;

			if (rx) {
				for (i = 0; rx_cur < len && i < rxcnt; i++) {
					u32 word = readl(priv->base + SE_GENI_RX_FIFOn);
					unsigned int p;

					for (p = 0; p < priv->bytes_per_fifo_word &&
					     rx_cur < len; p++) {
						rx[rx_cur++] = word & 0xff;
						word >>= 8;
					}
				}
			} else {
				for (i = 0; i < rxcnt; i++)
					readl(priv->base + SE_GENI_RX_FIFOn);
			}
		}

		writel(status, priv->base + SE_GENI_M_IRQ_CLEAR);

		if (status & M_CMD_DONE_EN) {
			/* Drain any residual RX words after CMD_DONE */
			if (rx && rx_cur < len) {
				u32 rxstatus = readl(priv->base + SE_GENI_RX_FIFO_STATUS);
				u32 rxcnt = rxstatus & RX_FIFO_WC_MSK;
				unsigned int i;

				for (i = 0; rx_cur < len && i < rxcnt; i++) {
					u32 word = readl(priv->base + SE_GENI_RX_FIFOn);
					unsigned int p;

					for (p = 0; p < priv->bytes_per_fifo_word &&
					     rx_cur < len; p++) {
						rx[rx_cur++] = word & 0xff;
						word >>= 8;
					}
				}
			}
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static int qcom_geni_spi_abort(struct udevice *dev, struct qcom_geni_spi_priv *priv)
{
	ulong start = get_timer(0);
	u32 status;

	writel(M_GENI_CMD_ABORT, priv->base + SE_GENI_M_CMD_CTRL_REG);

	do {
		status = readl(priv->base + SE_GENI_M_IRQ_STATUS);
		if (get_timer(start) > SPI_XFER_TIMEOUT_MS)
			return -ETIMEDOUT;
	} while (!(status & M_CMD_ABORT_EN));

	writel(status, priv->base + SE_GENI_M_IRQ_CLEAR);

	/* Reset TX/RX DMA FSMs so the next transfer starts clean */
	start = get_timer(0);
	writel(1, priv->base + SE_DMA_TX_FSM_RST);
	do {
		status = readl(priv->base + SE_DMA_TX_IRQ_STAT);
		if (get_timer(start) > SPI_XFER_TIMEOUT_MS) {
			dev_err(dev, "DMA TX RESET failed\n");
			break;
		}
	} while (!(status & TX_RESET_DONE));
	writel(status, priv->base + SE_DMA_TX_IRQ_CLR);

	start = get_timer(0);
	writel(1, priv->base + SE_DMA_RX_FSM_RST);
	do {
		status = readl(priv->base + SE_DMA_RX_IRQ_STAT);
		if (get_timer(start) > SPI_XFER_TIMEOUT_MS) {
			dev_err(dev, "DMA RX RESET failed\n");
			break;
		}
	} while (!(status & RX_RESET_DONE));
	writel(status, priv->base + SE_DMA_RX_IRQ_CLR);

	return 0;
}

/* SE-DMA: the SE moves data directly to/from a physical buffer */
static void qcom_geni_spi_dma_tx_start(struct qcom_geni_spi_priv *priv, const u8 *tx,
				       unsigned int len)
{
	phys_addr_t buf = (phys_addr_t)(uintptr_t)tx;

	flush_dcache_range(ALIGN_DOWN((ulong)tx, ARCH_DMA_MINALIGN),
			   ALIGN((ulong)tx + len, ARCH_DMA_MINALIGN));

	writel(lower_32_bits(buf), priv->base + SE_DMA_TX_PTR_L);
	writel(upper_32_bits(buf), priv->base + SE_DMA_TX_PTR_H);
	writel(GENI_SE_DMA_EOT_BUF, priv->base + SE_DMA_TX_ATTR);
	writel(len, priv->base + SE_DMA_TX_LEN);
}

static void qcom_geni_spi_dma_rx_start(struct qcom_geni_spi_priv *priv, u8 *rx,
				       unsigned int len)
{
	phys_addr_t buf = (phys_addr_t)(uintptr_t)rx;

	/* Discard stale dirty cache lines before the DMA write lands */
	invalidate_dcache_range(ALIGN_DOWN((ulong)rx, ARCH_DMA_MINALIGN),
				ALIGN((ulong)rx + len, ARCH_DMA_MINALIGN));

	writel(lower_32_bits(buf), priv->base + SE_DMA_RX_PTR_L);
	writel(upper_32_bits(buf), priv->base + SE_DMA_RX_PTR_H);
	writel(0, priv->base + SE_DMA_RX_ATTR);
	writel(len, priv->base + SE_DMA_RX_LEN);
}

static int qcom_geni_spi_dma_wait_tx(struct qcom_geni_spi_priv *priv, ulong timeout_ms)
{
	ulong start = get_timer(0);
	u32 status;

	while (get_timer(start) < timeout_ms) {
		status = readl(priv->base + SE_DMA_TX_IRQ_STAT);
		if (!status) {
			udelay(1);
			continue;
		}

		writel(status, priv->base + SE_DMA_TX_IRQ_CLR);

		if (status & TX_SBE)
			return -EIO;
		if (status & TX_DMA_DONE)
			return 0;
	}

	return -ETIMEDOUT;
}

static int qcom_geni_spi_dma_wait_rx(struct qcom_geni_spi_priv *priv, u8 *rx,
				     unsigned int len, ulong timeout_ms)
{
	ulong start = get_timer(0);
	u32 status;

	while (get_timer(start) < timeout_ms) {
		status = readl(priv->base + SE_DMA_RX_IRQ_STAT);
		if (!status) {
			udelay(1);
			continue;
		}

		writel(status, priv->base + SE_DMA_RX_IRQ_CLR);

		if (status & RX_SBE)
			return -EIO;
		if (status & RX_DMA_DONE) {
			/* Force the AXI write to retire before trusting the buffer */
			readl(priv->base + SE_DMA_RX_LEN);
			invalidate_dcache_range(ALIGN_DOWN((ulong)rx, ARCH_DMA_MINALIGN),
						ALIGN((ulong)rx + len, ARCH_DMA_MINALIGN));
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static int qcom_geni_spi_dma_xfer_wait(struct qcom_geni_spi_priv *priv, const u8 *tx,
				       u8 *rx, unsigned int len, ulong timeout_ms)
{
	int ret;

	if (tx) {
		ret = qcom_geni_spi_dma_wait_tx(priv, timeout_ms);
		if (ret)
			return ret;
	}

	if (rx) {
		ret = qcom_geni_spi_dma_wait_rx(priv, rx, len, timeout_ms);
		if (ret)
			return ret;
	}

	return 0;
}

/* TX/RX_TRANS_LEN and DMA_TX/RX_LEN are 24-bit HW fields; split large transfers */
#define SPI_GENI_MAX_XFER_BYTES		0xFFFFFF

/* RX DMA requires a cache-line-aligned start and length to invalidate safely */
static bool qcom_geni_spi_rx_needs_bounce(const void *rx, unsigned int len)
{
	return !IS_ALIGNED((ulong)rx, ARCH_DMA_MINALIGN) ||
	       !IS_ALIGNED(len, ARCH_DMA_MINALIGN);
}

static int qcom_geni_spi_xfer_chunk(struct udevice *dev, struct qcom_geni_spi_priv *priv,
				    unsigned int len, const void *dout, void *din,
				    bool use_dma, bool last_chunk, ulong timeout_ms)
{
	u32 cmd, m_param = 0;
	int ret;

	writel(dout ? len : 0, priv->base + SE_SPI_TX_TRANS_LEN);
	writel(din ? len : 0, priv->base + SE_SPI_RX_TRANS_LEN);

	/* SPI_TX_ONLY | SPI_RX_ONLY is not a valid opcode, use SPI_TX_RX instead */
	if (dout && din)
		cmd = SPI_TX_RX;
	else if (din)
		cmd = SPI_RX_ONLY;
	else if (dout)
		cmd = SPI_TX_ONLY;
	else
		cmd = 0;

	if (!last_chunk)
		m_param |= FRAGMENTATION;

	if (use_dma) {
		setbits_le32(priv->base + SE_GENI_DMA_MODE_EN, GENI_DMA_MODE_EN);

		/* DMA: issue M_CMD0 first, then arm DMA descriptors */
		writel((cmd << M_OPCODE_SHFT) | (m_param & M_PARAMS_MSK),
		       priv->base + SE_GENI_M_CMD0);
		if (din)
			qcom_geni_spi_dma_rx_start(priv, din, len);
		if (dout)
			qcom_geni_spi_dma_tx_start(priv, dout, len);

		ret = qcom_geni_spi_dma_xfer_wait(priv, dout, din, len, timeout_ms);

		/* Clear M_CMD_DONE so status doesn't accumulate across transfers */
		writel(readl(priv->base + SE_GENI_M_IRQ_STATUS),
		       priv->base + SE_GENI_M_IRQ_CLEAR);
	} else {
		clrbits_le32(priv->base + SE_GENI_DMA_MODE_EN, GENI_DMA_MODE_EN);

		/*
		 * Set watermarks before M_CMD0. Only set TX_WATERMARK when
		 * we have TX data, since SPI_RX_ONLY doesn't use the TX FIFO.
		 */
		if (dout)
			writel(1, priv->base + SE_GENI_TX_WATERMARK_REG);
		/* RX_WATERMARK=0: SE fires RX_FIFO_LAST at end of transfer */
		if (din)
			writel(0, priv->base + SE_GENI_RX_WATERMARK_REG);

		writel((cmd << M_OPCODE_SHFT) | (m_param & M_PARAMS_MSK),
		       priv->base + SE_GENI_M_CMD0);

		ret = qcom_geni_spi_fifo_xfer(priv, dout, din, len, timeout_ms);
	}

	return ret;
}

static int qcom_geni_spi_xfer_once(struct udevice *dev, unsigned int len,
				   const void *dout, void *din, bool xfer_end)
{
	struct udevice *bus = dev_get_parent(dev);
	struct qcom_geni_spi_priv *priv = dev_get_priv(bus);
	ulong timeout_ms = qcom_geni_spi_xfer_timeout_ms(priv, len);
	bool use_dma = !priv->fifo_capable || len >= SPI_DMA_MIN_XFER_BYTES;
	u8 edge_buf[2][ARCH_DMA_MINALIGN] __aligned(ARCH_DMA_MINALIGN);
	unsigned int head_len = 0, tail_len = 0, mid_len = len, done = 0;
	int ret = 0;

	if (din && use_dma && qcom_geni_spi_rx_needs_bounce(din, len)) {
		head_len = min_t(unsigned int,
				 ALIGN((ulong)din, ARCH_DMA_MINALIGN) - (ulong)din,
				 len);
		tail_len = (len - head_len) % ARCH_DMA_MINALIGN;
		mid_len = len - head_len - tail_len;
	}

	if (head_len) {
		ret = qcom_geni_spi_xfer_chunk(dev, priv, head_len, dout, edge_buf[0],
					       use_dma, xfer_end && !mid_len && !tail_len,
					       timeout_ms);
		if (!ret)
			memcpy(din, edge_buf[0], head_len);
		done += head_len;
	}

	if (!ret && mid_len) {
		ret = qcom_geni_spi_xfer_chunk(dev, priv, mid_len,
					       dout ? (const u8 *)dout + done : NULL,
					       din ? (u8 *)din + done : NULL,
					       use_dma, xfer_end && !tail_len, timeout_ms);
		done += mid_len;
	}

	if (!ret && tail_len) {
		ret = qcom_geni_spi_xfer_chunk(dev, priv, tail_len,
					       dout ? (const u8 *)dout + done : NULL,
					       edge_buf[1], use_dma, xfer_end, timeout_ms);
		if (!ret)
			memcpy((u8 *)din + done, edge_buf[1], tail_len);
	}

	if (ret) {
		int abort_ret = qcom_geni_spi_abort(dev, priv);

		if (abort_ret)
			dev_err(dev, "abort failed: %d\n", abort_ret);
	}

	return ret;
}

static int qcom_geni_spi_xfer(struct udevice *dev, unsigned int bitlen,
			      const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev_get_parent(dev);
	struct qcom_geni_spi_priv *priv = dev_get_priv(bus);
	unsigned int len = DIV_ROUND_UP(bitlen, 8);
	unsigned int done = 0;
	int ret;

	if (flags & SPI_XFER_BEGIN)
		writel(0xffffffff, priv->base + SE_GENI_M_IRQ_CLEAR);

	while (done < len) {
		unsigned int chunk = min_t(unsigned int, len - done,
					    SPI_GENI_MAX_XFER_BYTES);
		bool xfer_end = (flags & SPI_XFER_END) && (done + chunk == len);

		ret = qcom_geni_spi_xfer_once(dev,
					      chunk,
					  dout ? (const u8 *)dout + done : NULL,
					  din ? (u8 *)din + done : NULL,
					  xfer_end);
		if (ret)
			return ret;

		done += chunk;
	}

	return 0;
}

static int qcom_geni_spi_set_speed(struct udevice *bus, uint speed)
{
	struct qcom_geni_spi_priv *priv = dev_get_priv(bus);
	ulong parent_rate;
	u32 div;

	if (!speed)
		return -EINVAL;

	parent_rate = clk_get_rate(&priv->se);
	if (IS_ERR_VALUE(parent_rate) || !parent_rate)
		div = priv->oversampling;
	else
		div = DIV_ROUND_UP(parent_rate, priv->oversampling * speed);

	div = clamp_t(u32, div, 1, CLK_DIV_MSK >> CLK_DIV_SHFT);

	writel(0, priv->base + SE_GENI_CLK_SEL);
	writel((div << CLK_DIV_SHFT) | SER_CLK_EN, priv->base + GENI_SER_M_CLK_CFG);

	return 0;
}

static int qcom_geni_spi_set_mode(struct udevice *bus, uint mode)
{
	struct qcom_geni_spi_priv *priv = dev_get_priv(bus);
	u32 val;

	val = readl(priv->base + SE_SPI_LOOPBACK);
	val &= ~LOOPBACK_MSK;
	if (mode & SPI_LOOP)
		val |= LOOPBACK_ENABLE;
	writel(val, priv->base + SE_SPI_LOOPBACK);

	val = readl(priv->base + SE_SPI_CPHA);
	if (mode & SPI_CPHA)
		val |= CPHA;
	else
		val &= ~CPHA;
	writel(val, priv->base + SE_SPI_CPHA);

	val = readl(priv->base + SE_SPI_CPOL);
	if (mode & SPI_CPOL)
		val |= CPOL;
	else
		val &= ~CPOL;
	writel(val, priv->base + SE_SPI_CPOL);

	return 0;
}

static int qcom_geni_spi_claim_bus(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	struct qcom_geni_spi_priv *priv = dev_get_priv(bus);
	struct dm_spi_slave_plat *slave_plat = dev_get_parent_plat(dev);
	unsigned int cs = slave_plat->cs[0];
	unsigned int bpw = slave_plat->wordlen ? slave_plat->wordlen : SPI_WORD_LEN_BITS;

	priv->bpw = bpw;
	priv->bytes_per_fifo_word = qcom_geni_spi_bytes_per_fifo_word(bpw);

	writel((bpw - MIN_WORD_LEN) & WORD_LEN_MSK, priv->base + SE_SPI_WORD_LEN);
	writel(cs, priv->base + SE_SPI_DEMUX_SEL);
	writel(slave_plat->mode & SPI_CS_HIGH ? BIT(cs) : 0,
	       priv->base + SE_SPI_DEMUX_OUTPUT_INV);

	qcom_geni_spi_config_packing(priv, bpw, true);

	writel(0xffffffff, priv->base + SE_GENI_M_IRQ_CLEAR);

	return 0;
}

static int qcom_geni_spi_release_bus(struct udevice *dev)
{
	return 0;
}

static u32 qcom_geni_spi_get_tx_fifo_depth(struct qcom_geni_spi_priv *priv)
{
	u32 val, hw_version, depth_mask;

	hw_version = readl(priv->wrapper + QUP_HW_VER_REG);
	depth_mask = geni_se_fifo_depth_mask(hw_version, TX_FIFO_DEPTH_MSK_256_BYTES,
					     TX_FIFO_DEPTH_MSK);

	val = readl(priv->base + SE_HW_PARAM_0);

	return (val & depth_mask) >> TX_FIFO_DEPTH_SHFT;
}

/* QUP v1.0 undersamples the SPI clock and needs 2x the requested bit rate */
static u32 qcom_geni_spi_get_oversampling(struct qcom_geni_spi_priv *priv)
{
	u32 hw_version = readl(priv->wrapper + QUP_HW_VER_REG);
	u32 hw_major = GENI_SE_VERSION_MAJOR(hw_version);
	u32 hw_minor = GENI_SE_VERSION_MINOR(hw_version);

	if (hw_major == 1 && hw_minor == 0)
		return 2;

	return 1;
}

static void qcom_geni_spi_hw_init(struct qcom_geni_spi_priv *priv)
{
	u32 val;

	writel(0xffffffff, priv->base + SE_GENI_M_IRQ_CLEAR);

	val = readl(priv->base + GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	writel(val, priv->base + GENI_CGC_CTRL);

	writel(DEFAULT_IO_OUTPUT_CTRL_MSK, priv->base + GENI_OUTPUT_CTRL);
	writel(FORCE_DEFAULT, priv->base + GENI_FORCE_DEFAULT_REG);

	val = readl(priv->base + SE_IRQ_EN);
	val |= GENI_M_IRQ_EN;
	writel(val, priv->base + SE_IRQ_EN);

	writel(priv->tx_wm, priv->base + SE_GENI_TX_WATERMARK_REG);

	val = readl(priv->base + SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN | M_CMD_DONE_EN | SPI_ERR;
	writel(val, priv->base + SE_GENI_M_IRQ_EN);

	writel(0xffffffff, priv->base + SE_DMA_TX_IRQ_CLR);
	writel(0xffffffff, priv->base + SE_DMA_RX_IRQ_CLR);
	writel(TX_DMA_DONE | TX_SBE, priv->base + SE_DMA_TX_IRQ_EN_SET);
	writel(RX_DMA_DONE | RX_SBE, priv->base + SE_DMA_RX_IRQ_EN_SET);

	/* We always control CS manually, don't let the SE auto-toggle it */
	val = readl(priv->base + SE_SPI_TRANS_CFG);
	val &= ~CS_TOGGLE;
	writel(val, priv->base + SE_SPI_TRANS_CFG);
}

static int qcom_geni_spi_probe(struct udevice *dev)
{
	struct qcom_geni_spi_priv *priv = dev_get_priv(dev);
	u32 proto;
	int ret;

	priv->wrapper = dev_read_addr(dev->parent);
	if (priv->wrapper == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ret = clk_get_by_name(dev, "se", &priv->se);
	if (ret) {
		dev_err(dev, "clk_get_by_name(se) failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->se);
	if (ret) {
		dev_err(dev, "clk_enable(se) failed: %d\n", ret);
		return ret;
	}

	proto = readl(priv->base + GENI_FW_REVISION_RO);
	proto &= FW_REV_PROTOCOL_MSK;
	proto >>= FW_REV_PROTOCOL_SHFT;

	if (proto == GENI_SE_INVALID_PROTO) {
		dev_dbg(dev, "firmware not loaded, loading now\n");
		ret = qcom_geni_load_firmware(priv->base, dev);
		if (ret) {
			dev_err(dev, "firmware load failed: %d\n", ret);
			clk_disable(&priv->se);
			return ret;
		}
		proto = readl(priv->base + GENI_FW_REVISION_RO);
		proto &= FW_REV_PROTOCOL_MSK;
		proto >>= FW_REV_PROTOCOL_SHFT;
		dev_info(dev, "firmware loaded, proto=0x%x\n", proto);
	} else {
		dev_info(dev, "firmware already loaded, proto=0x%x\n", proto);
	}

	if (proto != GENI_SE_SPI) {
		dev_err(dev, "Invalid proto %d\n", proto);
		clk_disable(&priv->se);
		return -ENXIO;
	}

	/*
	 * SE-DMA is an inherent capability of the GENI SE core (mirrors
	 * upstream Linux's spi-geni-qcom.c, which never gates SE-DMA by DT
	 * or hardware version). Only FIFO availability needs checking here.
	 */
	priv->fifo_capable = !(readl(priv->base + GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE);

	priv->tx_fifo_depth = qcom_geni_spi_get_tx_fifo_depth(priv);
	if (!priv->tx_fifo_depth) {
		dev_err(dev, "Invalid TX FIFO depth\n");
		clk_disable(&priv->se);
		return -ENXIO;
	}
	priv->tx_wm = priv->tx_fifo_depth - 1;
	priv->oversampling = qcom_geni_spi_get_oversampling(priv);

	qcom_geni_spi_hw_init(priv);

	return 0;
}

static const struct dm_spi_ops qcom_geni_spi_ops = {
	.claim_bus	= qcom_geni_spi_claim_bus,
	.release_bus	= qcom_geni_spi_release_bus,
	.xfer		= qcom_geni_spi_xfer,
	.set_speed	= qcom_geni_spi_set_speed,
	.set_mode	= qcom_geni_spi_set_mode,
	/*
	 * cs_info is not needed, since we require all chip selects to be
	 * in the device tree explicitly
	 */
};

static const struct udevice_id qcom_geni_spi_ids[] = {
	{ .compatible = "qcom,geni-spi" },
	{ }
};

U_BOOT_DRIVER(qcom_geni_spi) = {
	.name	= "qcom_geni_spi",
	.id	= UCLASS_SPI,
	.of_match = qcom_geni_spi_ids,
	.probe	= qcom_geni_spi_probe,
	.priv_auto = sizeof(struct qcom_geni_spi_priv),
	.ops	= &qcom_geni_spi_ops,
};
