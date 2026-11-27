// SPDX-License-Identifier: GPL-2.0
/*
 * Polled GPI (GSI DMA) backend for the GENI I2C driver.
 *
 * Based on Linux drivers/dma/qcom/gpi.c and drivers/i2c/busses/i2c-qcom-geni.c
 *
 * The serial engine is driven through a GPII: one event ring plus two
 * transfer rings (0 = TX/OUT, 1 = RX/IN). Interrupts are never enabled in
 * the GIC, command and transfer completions are polled instead.
 */

#include <cpu_func.h>
#include <dm/device.h>
#include <dm/device_compat.h>
#include <dm/ofnode.h>
#include <dm/read.h>
#include <i2c.h>
#include <log.h>
#include <malloc.h>
#include <time.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/log2.h>
#include <soc/qcom/geni-se.h>
#include <soc/qcom/qup-fw-load.h>

#include "geni_i2c_gpi.h"

/* Always printed: real failures */
#define GPIERR(fmt, ...) \
	printf("GPI: %s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

/* Per-TRE/event chatter, build with -DGPI_VERBOSE=1 to get it */
#ifndef GPI_VERBOSE
#define GPI_VERBOSE	0
#endif
#define GPITRACE(fmt, ...) \
	do { \
		if (GPI_VERBOSE) \
			printf("GPITRACE: %s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__); \
	} while (0)

/* Register offsets from the EE base (gpi-top - ee_offset), n = GPII id */
#define GPII_CH_CNTXT(n, k)	(0x20000 + 0x4000 * (n) + 0x80 * (k))
#define GPII_EV_CNTXT(n)	(0x21000 + 0x4000 * (n))
#define GPII_CH_DB(n, k)	(0x22000 + 0x4000 * (n) + 0x8 * (k))
#define GPII_EV_DB(n)		(0x22100 + 0x4000 * (n))
#define GPII_CH_CMD(n)		(0x23008 + 0x4000 * (n))
#define GPII_EV_CMD(n)		(0x23010 + 0x4000 * (n))
#define GPII_TYPE_IRQ(n)	(0x23080 + 0x4000 * (n))
#define GPII_TYPE_IRQ_MSK(n)	(0x23088 + 0x4000 * (n))
#define GPII_SRC_CH_IRQ(n)	(0x23090 + 0x4000 * (n))
#define GPII_SRC_EV_IRQ(n)	(0x23094 + 0x4000 * (n))
#define GPII_SRC_CH_IRQ_MSK(n)	(0x23098 + 0x4000 * (n))
#define GPII_SRC_EV_IRQ_MSK(n)	(0x2309c + 0x4000 * (n))
#define GPII_SRC_CH_IRQ_CLR(n)	(0x230a0 + 0x4000 * (n))
#define GPII_SRC_EV_IRQ_CLR(n)	(0x230a4 + 0x4000 * (n))
#define GPII_IEOB_IRQ_MSK(n)	(0x230b8 + 0x4000 * (n))
#define GPII_IEOB_IRQ_CLR(n)	(0x230c0 + 0x4000 * (n))
#define GPII_GLOB_IRQ_STTS(n)	(0x23100 + 0x4000 * (n))
#define GPII_GLOB_IRQ_EN(n)	(0x23108 + 0x4000 * (n))
#define GPII_GLOB_IRQ_CLR(n)	(0x23110 + 0x4000 * (n))
#define GPII_GPII_IRQ_STTS(n)	(0x23118 + 0x4000 * (n))
#define GPII_GPII_IRQ_EN(n)	(0x23120 + 0x4000 * (n))
#define GPII_GPII_IRQ_CLR(n)	(0x23128 + 0x4000 * (n))
#define GPII_INTSET(n)		(0x23180 + 0x4000 * (n))
#define GPII_MSI_BASE_LSB(n)	(0x23188 + 0x4000 * (n))
#define GPII_MSI_BASE_MSB(n)	(0x2318c + 0x4000 * (n))
#define GPII_ERROR_LOG(n)	(0x23200 + 0x4000 * (n))
#define GPII_SCRATCH_0(n)	(0x23400 + 0x4000 * (n))
#define GPII_SCRATCH_1(n)	(0x23404 + 0x4000 * (n))
#define GPII_CH_QOS(n, k)	(0x2005c + 0x4000 * (n) + 0x80 * (k))
#define GPII_CH_SCRATCH(n, k, i) (0x20060 + 0x4000 * (n) + 0x80 * (k) + 4 * (i))

/* Offsets inside a ring context */
#define CNTXT_0_CONFIG		0x00
#define CNTXT_1_R_LENGTH	0x04
#define CNTXT_2_BASE_LSB	0x08
#define CNTXT_3_BASE_MSB	0x0c
#define CNTXT_4_RP_LSB		0x10
#define CNTXT_5_RP_MSB		0x14
#define CNTXT_8_INT_MOD		0x20
#define CNTXT_10_MSI_LSB	0x28
#define CNTXT_11_MSI_MSB	0x2c
#define CNTXT_12_RP_UPD_LSB	0x30
#define CNTXT_13_RP_UPD_MSB	0x34

#define CNTXT_0_CHSTATE(v)	(((v) >> 20) & 0xf)

#define GPI_CHTYPE_DIR_IN	0
#define GPI_CHTYPE_DIR_OUT	1
#define GPI_CHTYPE_PROTO_GPI	2
#define GPI_CHTYPE_GPI_EV	2
#define GPI_INTTYPE_IRQ		1

#define CH_CMD_ALLOCATE		0
#define CH_CMD_START		1
#define CH_CMD_STOP		2
#define CH_CMD_RESET		9
#define CH_CMD_DE_ALLOC		10
#define EV_CMD_ALLOCATE		0
#define EV_CMD_DE_ALLOC		10

#define CH_STATE_NOT_ALLOC	0
#define CH_STATE_ALLOCATED	1
#define CH_STATE_STARTED	2
#define CH_STATE_STOPPED	3
#define CH_STATE_STOP_IN_PROC	4
#define STATE_IGNORE		0xffffffff

#define TYPE_IRQ_CH_CTRL	BIT(0)
#define TYPE_IRQ_EV_CTRL	BIT(1)
#define TYPE_IRQ_GLOB		BIT(2)
#define TYPE_IRQ_IEOB		BIT(3)
#define TYPE_IRQ_GENERAL	BIT(6)
#define TYPE_IRQ_ALL		(TYPE_IRQ_CH_CTRL | TYPE_IRQ_EV_CTRL | TYPE_IRQ_GLOB | \
				 TYPE_IRQ_IEOB | TYPE_IRQ_GENERAL)

#define GPI_NUM_CHAN		2
#define GPI_TX_CHAN		0
#define GPI_RX_CHAN		1
#define GPI_CHAN_TRES		64
#define GPI_EV_FACTOR		2
#define GPI_EL_SIZE		16
#define GPI_MAX_GPII		16

#define GPI_CMD_TIMEOUT_MS	250
#define GPI_XFER_TIMEOUT_MS	200

/* TRE */
#define TRE_TYPE_DMA		0x10
#define TRE_TYPE_GO		0x20
#define TRE_TYPE_CONFIG0	0x22
#define TRE_TYPE_SHIFT		16
#define TRE_FLAG_CHAIN		BIT(0)
#define TRE_FLAG_IEOT		BIT(9)
#define TRE_FLAG_LINK		BIT(11)

#define TRE_I2C_C0_TX_PACK	BIT(24)
#define TRE_I2C_C0_RX_PACK	BIT(25)
#define TRE_I2C_GO_ADDR_SHIFT	8
#define TRE_I2C_GO_STRETCH	BIT(26)

#define I2C_GO_WRITE		0x1
#define I2C_GO_READ		0x2

/* Event ring element */
#define EV_TYPE_XFER_COMPLETE	0x22
#define EV_TYPE_IMMEDIATE_DATA	0x30
#define EV_TYPE_QUP_NOTIF	0x31
#define EV_TYPE_STALE		0xff
#define EV_CODE_SUCCESS	1
#define EV_CODE_EOT		2
#define EV_CODE_EOB		4
#define EV_CODE_UNEXP_ERR	16

#define SE_DMA_TX_LEN_IN	0xc54

/* SE_GSI_EVENT_EN fields */
#define DMA_TX_EVENT_EN		BIT(0)
#define DMA_RX_EVENT_EN		BIT(1)
#define GENI_M_EVENT_EN		BIT(2)
#define GENI_S_EVENT_EN		BIT(3)

struct gpi_ring {
	void *base;
	u64 phys;
	u32 len;	/* in bytes, power of 2, ring is aligned to it */
	u32 wp;		/* byte offsets from base */
	u32 rp;
};

struct geni_gpi {
	struct udevice *dev;
	phys_addr_t se_base;
	void __iomem *ee;
	u32 gpii_mask;
	u32 gpii;
	u32 seid;
	u32 proto;
	bool up;
	struct gpi_ring ev;
	struct gpi_ring ch[GPI_NUM_CHAN];
};

static inline u32 gpi_rd(struct geni_gpi *g, u32 off)
{
	return readl(g->ee + off);
}

static inline void gpi_wr(struct geni_gpi *g, u32 off, u32 val)
{
	writel(val, g->ee + off);
}

static inline u32 ch_state(struct geni_gpi *g, u32 chid)
{
	return CNTXT_0_CHSTATE(gpi_rd(g, GPII_CH_CNTXT(g->gpii, chid) + CNTXT_0_CONFIG));
}

static inline u32 ev_state(struct geni_gpi *g)
{
	return CNTXT_0_CHSTATE(gpi_rd(g, GPII_EV_CNTXT(g->gpii) + CNTXT_0_CONFIG));
}

static void gpi_dump(struct geni_gpi *g, const char *tag)
{
	u32 n = g->gpii;

	GPIERR("%s: gpii=%u ch0=%u ch1=%u ev=%u type_irq=0x%x glob=0x%x gpii_irq=0x%x err=0x%x\n",
		 tag, n, ch_state(g, 0), ch_state(g, 1), ev_state(g),
		 gpi_rd(g, GPII_TYPE_IRQ(n)), gpi_rd(g, GPII_GLOB_IRQ_STTS(n)),
		 gpi_rd(g, GPII_GPII_IRQ_STTS(n)), gpi_rd(g, GPII_ERROR_LOG(n)));
	GPIERR("%s: ev rp=0x%x (sw rp=0x%x wp=0x%x) ch0 rp=0x%x (sw wp=0x%x) ch1 rp=0x%x (sw wp=0x%x)\n",
		 tag, gpi_rd(g, GPII_EV_CNTXT(n) + CNTXT_4_RP_LSB), g->ev.rp, g->ev.wp,
		 gpi_rd(g, GPII_CH_CNTXT(n, 0) + CNTXT_4_RP_LSB), g->ch[0].wp,
		 gpi_rd(g, GPII_CH_CNTXT(n, 1) + CNTXT_4_RP_LSB), g->ch[1].wp);
}

/* ---------------------------------------------------------------- */
/* Commands                                                         */
/* ---------------------------------------------------------------- */

static void gpi_clear_cmd_irqs(struct geni_gpi *g)
{
	u32 n = g->gpii;

	gpi_wr(g, GPII_SRC_CH_IRQ_CLR(n), gpi_rd(g, GPII_SRC_CH_IRQ(n)));
	gpi_wr(g, GPII_SRC_EV_IRQ_CLR(n), gpi_rd(g, GPII_SRC_EV_IRQ(n)));
}

/*
 * Send a channel (@is_ev == false) or event ring command and poll for its
 * completion. @want is the state the ring must end up in, or STATE_IGNORE.
 */
static int gpi_send_cmd(struct geni_gpi *g, bool is_ev, u32 opcode, u32 chid, u32 want)
{
	u32 n = g->gpii;
	u32 ctx = is_ev ? GPII_EV_CNTXT(n) : GPII_CH_CNTXT(n, chid);
	u32 type_bit = is_ev ? TYPE_IRQ_EV_CTRL : TYPE_IRQ_CH_CTRL;
	ulong start;
	u32 state = 0;
	bool done = false;

	gpi_clear_cmd_irqs(g);

	gpi_wr(g, is_ev ? GPII_EV_CMD(n) : GPII_CH_CMD(n), (opcode << 24) | chid);

	start = get_timer(0);
	while (get_timer(start) < GPI_CMD_TIMEOUT_MS) {
		/* latch the completion before the state, or the state may be stale */
		bool irq = gpi_rd(g, GPII_TYPE_IRQ(n)) & type_bit;

		state = CNTXT_0_CHSTATE(gpi_rd(g, ctx + CNTXT_0_CONFIG));

		if (irq) {
			/* STOP goes through a transient state first */
			if (state != CH_STATE_STOP_IN_PROC) {
				done = true;
				break;
			}
		} else if (want != STATE_IGNORE && state == want) {
			done = true;
			break;
		}
		udelay(10);
	}

	gpi_clear_cmd_irqs(g);

	if (!done) {
		GPIERR("%s cmd 0x%x chid %u timed out, state=%u\n",
			 is_ev ? "EV" : "CH", opcode, chid, state);
		gpi_dump(g, "cmd-timeout");
		return -ETIMEDOUT;
	}

	if (want != STATE_IGNORE && state != want) {
		GPIERR("%s cmd 0x%x chid %u: state=%u, wanted %u\n",
			 is_ev ? "EV" : "CH", opcode, chid, state, want);
		gpi_dump(g, "cmd-state");
		return -EIO;
	}

	return 0;
}

/* ---------------------------------------------------------------- */
/* Rings                                                            */
/* ---------------------------------------------------------------- */

static int gpi_alloc_ring(struct gpi_ring *r, u32 nelem)
{
	u32 len = roundup_pow_of_two(nelem * GPI_EL_SIZE);

	/* HW wants the ring aligned to its (power of 2) length */
	r->base = memalign(len, len);
	if (!r->base)
		return -ENOMEM;

	memset(r->base, 0, len);
	flush_dcache_range((ulong)r->base, (ulong)r->base + len);

	r->phys = virt_to_phys(r->base);
	r->len = len;
	r->wp = 0;
	r->rp = 0;

	return 0;
}

static void gpi_free_ring(struct gpi_ring *r)
{
	free(r->base);
	memset(r, 0, sizeof(*r));
}

static void gpi_queue_tre(struct gpi_ring *r, u32 d0, u32 d1, u32 d2, u32 d3)
{
	u32 *tre = r->base + r->wp;

	tre[0] = d0;
	tre[1] = d1;
	tre[2] = d2;
	tre[3] = d3;
	GPITRACE("TRE@+0x%x: %08x %08x %08x %08x\n", r->wp, d0, d1, d2, d3);

	r->wp = (r->wp + GPI_EL_SIZE) & (r->len - 1);
}

static void gpi_ring_ch_db(struct geni_gpi *g, u32 chid)
{
	struct gpi_ring *r = &g->ch[chid];

	flush_dcache_range((ulong)r->base, (ulong)r->base + r->len);
	gpi_wr(g, GPII_CH_DB(g->gpii, chid), lower_32_bits(r->phys + r->wp));
}

static void gpi_ring_ev_db(struct geni_gpi *g)
{
	gpi_wr(g, GPII_EV_DB(g->gpii), lower_32_bits(g->ev.phys + g->ev.wp));
}

/* ---------------------------------------------------------------- */
/* GPII bring up / tear down                                        */
/* ---------------------------------------------------------------- */

static void gpi_config_interrupts(struct geni_gpi *g)
{
	u32 n = g->gpii;

	gpi_wr(g, GPII_TYPE_IRQ_MSK(n), TYPE_IRQ_ALL);
	gpi_wr(g, GPII_IEOB_IRQ_MSK(n), BIT(0));
	gpi_wr(g, GPII_SRC_CH_IRQ_MSK(n), GENMASK(1, 0));
	gpi_wr(g, GPII_SRC_EV_IRQ_MSK(n), BIT(0));
	gpi_wr(g, GPII_GLOB_IRQ_EN(n), GENMASK(3, 0));
	gpi_wr(g, GPII_GPII_IRQ_EN(n), GENMASK(3, 0));
	gpi_wr(g, GPII_MSI_BASE_LSB(n), 0);
	gpi_wr(g, GPII_MSI_BASE_MSB(n), 0);
	gpi_wr(g, GPII_SCRATCH_0(n), 0);
	gpi_wr(g, GPII_SCRATCH_1(n), 0);
	gpi_wr(g, GPII_INTSET(n), 1);
	gpi_wr(g, GPII_ERROR_LOG(n), 0);
}

static void gpi_disable_interrupts(struct geni_gpi *g)
{
	u32 n = g->gpii;

	gpi_wr(g, GPII_TYPE_IRQ_MSK(n), 0);
	gpi_wr(g, GPII_IEOB_IRQ_MSK(n), 0);
	gpi_wr(g, GPII_SRC_CH_IRQ_MSK(n), 0);
	gpi_wr(g, GPII_SRC_EV_IRQ_MSK(n), 0);
	gpi_wr(g, GPII_GLOB_IRQ_EN(n), 0);
	gpi_wr(g, GPII_GPII_IRQ_EN(n), 0);
	gpi_wr(g, GPII_IEOB_IRQ_CLR(n), BIT(0));
	gpi_wr(g, GPII_GLOB_IRQ_CLR(n), GENMASK(31, 0));
	gpi_wr(g, GPII_GPII_IRQ_CLR(n), GENMASK(31, 0));
}

static int gpi_alloc_ev_ring(struct geni_gpi *g)
{
	struct gpi_ring *r = &g->ev;
	u32 base = GPII_EV_CNTXT(g->gpii);
	int ret;

	ret = gpi_send_cmd(g, true, EV_CMD_ALLOCATE, 0, CH_STATE_ALLOCATED);
	if (ret)
		return ret;

	gpi_wr(g, base + CNTXT_0_CONFIG,
	       (GPI_EL_SIZE << 24) | (GPI_INTTYPE_IRQ << 16) | GPI_CHTYPE_GPI_EV);
	gpi_wr(g, base + CNTXT_1_R_LENGTH, r->len);
	gpi_wr(g, base + CNTXT_2_BASE_LSB, lower_32_bits(r->phys));
	gpi_wr(g, base + CNTXT_3_BASE_MSB, upper_32_bits(r->phys));
	gpi_wr(g, GPII_EV_DB(g->gpii) + 4, upper_32_bits(r->phys));
	gpi_wr(g, base + CNTXT_8_INT_MOD, 0);
	gpi_wr(g, base + CNTXT_10_MSI_LSB, 0);
	gpi_wr(g, base + CNTXT_11_MSI_MSB, 0);
	gpi_wr(g, base + CNTXT_12_RP_UPD_LSB, 0);
	gpi_wr(g, base + CNTXT_13_RP_UPD_MSB, 0);

	/* all but one element are given to the HW */
	r->wp = r->len - GPI_EL_SIZE;
	r->rp = 0;
	gpi_ring_ev_db(g);

	return 0;
}

static void gpi_program_chan(struct geni_gpi *g, u32 chid)
{
	struct gpi_ring *r = &g->ch[chid];
	u32 n = g->gpii;
	u32 base = GPII_CH_CNTXT(n, chid);
	u32 dir = chid == GPI_TX_CHAN ? GPI_CHTYPE_DIR_OUT : GPI_CHTYPE_DIR_IN;

	gpi_wr(g, base + CNTXT_0_CONFIG,
	       (GPI_EL_SIZE << 24) | (0 << 14) | (dir << 3) | GPI_CHTYPE_PROTO_GPI);
	gpi_wr(g, base + CNTXT_1_R_LENGTH, r->len);
	gpi_wr(g, base + CNTXT_2_BASE_LSB, lower_32_bits(r->phys));
	gpi_wr(g, base + CNTXT_3_BASE_MSB, upper_32_bits(r->phys));
	gpi_wr(g, GPII_CH_DB(n, chid) + 4, upper_32_bits(r->phys));
	/* pair channel [20:16] | protocol [7:4] | SE id [2:0] */
	gpi_wr(g, GPII_CH_SCRATCH(n, chid, 0),
	       ((!chid) << 16) | (g->proto << 4) | g->seid);
	gpi_wr(g, GPII_CH_SCRATCH(n, chid, 1), 0);
	gpi_wr(g, GPII_CH_SCRATCH(n, chid, 2), 0);
	gpi_wr(g, GPII_CH_SCRATCH(n, chid, 3), 0);
	gpi_wr(g, GPII_CH_QOS(n, chid), 1);

	r->wp = 0;
	r->rp = 0;
}

static int gpi_alloc_chan(struct geni_gpi *g, u32 chid)
{
	int ret;

	ret = gpi_send_cmd(g, false, CH_CMD_ALLOCATE, chid, CH_STATE_ALLOCATED);
	if (ret)
		return ret;

	gpi_program_chan(g, chid);

	return 0;
}

/*
 * Get both channels back to an empty, started state after a failed transfer
 * (a NACK leaves a half consumed TX ring behind), without giving up the GPII.
 * Same sequence as the Linux terminate_all(): STOP, RESET, reprogram, START.
 */
static int gpi_recover(struct geni_gpi *g)
{
	struct gpi_ring *ev = &g->ev;
	u32 rp_off;
	int i, ret;

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		u32 state = ch_state(g, i);

		if (state == CH_STATE_STARTED || state == CH_STATE_STOP_IN_PROC) {
			ret = gpi_send_cmd(g, false, CH_CMD_STOP, i, CH_STATE_STOPPED);
			if (ret)
				return ret;
		}
		ret = gpi_send_cmd(g, false, CH_CMD_RESET, i, CH_STATE_ALLOCATED);
		if (ret)
			return ret;

		memset(g->ch[i].base, 0, g->ch[i].len);
		flush_dcache_range((ulong)g->ch[i].base, (ulong)g->ch[i].base + g->ch[i].len);
		gpi_program_chan(g, i);
	}

	/* drop whatever the failed transfer left in the event ring */
	rp_off = gpi_rd(g, GPII_EV_CNTXT(g->gpii) + CNTXT_4_RP_LSB) - lower_32_bits(ev->phys);
	if (rp_off >= ev->len || (rp_off & (GPI_EL_SIZE - 1)))
		return -EIO;
	ev->rp = rp_off;
	ev->wp = (rp_off - GPI_EL_SIZE) & (ev->len - 1);
	gpi_ring_ev_db(g);
	gpi_wr(g, GPII_IEOB_IRQ_CLR(g->gpii), BIT(0));
	gpi_wr(g, GPII_GLOB_IRQ_CLR(g->gpii), gpi_rd(g, GPII_GLOB_IRQ_STTS(g->gpii)));

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		ret = gpi_send_cmd(g, false, CH_CMD_START, i, CH_STATE_STARTED);
		if (ret)
			return ret;
	}

	return 0;
}

/* Best effort release of whatever state a GPII is in */
static void gpi_release(struct geni_gpi *g)
{
	int i;

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		u32 state = ch_state(g, i);

		if (state == CH_STATE_NOT_ALLOC)
			continue;
		if (state == CH_STATE_STARTED || state == CH_STATE_STOP_IN_PROC)
			gpi_send_cmd(g, false, CH_CMD_STOP, i, CH_STATE_STOPPED);
		if (ch_state(g, i) == CH_STATE_STOPPED)
			gpi_send_cmd(g, false, CH_CMD_RESET, i, CH_STATE_ALLOCATED);
		gpi_send_cmd(g, false, CH_CMD_DE_ALLOC, i, STATE_IGNORE);
	}

	if (ev_state(g) != CH_STATE_NOT_ALLOC)
		gpi_send_cmd(g, true, EV_CMD_DE_ALLOC, 0, STATE_IGNORE);
}

static void gpi_shutdown(struct geni_gpi *g)
{
	int i;

	if (!g->up)
		return;

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		if (ch_state(g, i) == CH_STATE_STARTED)
			gpi_send_cmd(g, false, CH_CMD_STOP, i, CH_STATE_STOPPED);
		if (ch_state(g, i) == CH_STATE_STOPPED)
			gpi_send_cmd(g, false, CH_CMD_RESET, i, CH_STATE_ALLOCATED);
		gpi_send_cmd(g, false, CH_CMD_DE_ALLOC, i, STATE_IGNORE);
	}
	gpi_send_cmd(g, true, EV_CMD_DE_ALLOC, 0, STATE_IGNORE);
	gpi_disable_interrupts(g);

	for (i = 0; i < GPI_NUM_CHAN; i++)
		gpi_free_ring(&g->ch[i]);
	gpi_free_ring(&g->ev);

	g->up = false;
}

static int gpi_setup(struct geni_gpi *g)
{
	int i, ret;

	ret = gpi_alloc_ring(&g->ev, GPI_CHAN_TRES << GPI_EV_FACTOR);
	if (ret)
		goto err;
	for (i = 0; i < GPI_NUM_CHAN; i++) {
		ret = gpi_alloc_ring(&g->ch[i], GPI_CHAN_TRES);
		if (ret)
			goto err;
	}
	GPITRACE("rings: ev=%llx/0x%x ch0=%llx/0x%x ch1=%llx/0x%x\n",
		 (unsigned long long)g->ev.phys, g->ev.len,
		 (unsigned long long)g->ch[0].phys, g->ch[0].len,
		 (unsigned long long)g->ch[1].phys, g->ch[1].len);

	gpi_config_interrupts(g);
	if (GPI_VERBOSE)
		gpi_dump(g, "pre-alloc");

	ret = gpi_alloc_ev_ring(g);
	if (ret) {
		GPIERR("event ring alloc failed: %d\n", ret);
		goto err_irq;
	}

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		ret = gpi_alloc_chan(g, i);
		if (ret) {
			GPIERR("channel %d alloc failed: %d\n", i, ret);
			goto err_release;
		}
	}

	for (i = 0; i < GPI_NUM_CHAN; i++) {
		ret = gpi_send_cmd(g, false, CH_CMD_START, i, CH_STATE_STARTED);
		if (ret) {
			GPIERR("channel %d start failed: %d\n", i, ret);
			goto err_release;
		}
	}

	g->up = true;
	if (GPI_VERBOSE)
		gpi_dump(g, "started");

	return 0;

err_release:
	gpi_release(g);
err_irq:
	gpi_disable_interrupts(g);
err:
	for (i = 0; i < GPI_NUM_CHAN; i++)
		gpi_free_ring(&g->ch[i]);
	gpi_free_ring(&g->ev);

	return ret;
}

/* Take the first GPII of the mask that nobody has touched */
static int gpi_pick_gpii(struct geni_gpi *g)
{
	int n, last = -1, pick = -1;

	for (n = 0; n < GPI_MAX_GPII; n++) {
		u32 c0, c1, ev;

		if (!(g->gpii_mask & BIT(n)))
			continue;

		g->gpii = n;
		last = n;
		c0 = ch_state(g, 0);
		c1 = ch_state(g, 1);
		ev = ev_state(g);
		GPITRACE("gpii %d: ch0=%u ch1=%u ev=%u\n", n, c0, c1, ev);

		/*
		 * Stop at the first free one: the mask advertises GPIIs the NS EE can't
		 * touch (reading GPII 5 resets the SoC on Amboseli), so don't scan further
		 * than needed.
		 */
		if (!c0 && !c1 && !ev) {
			pick = n;
			break;
		}
	}

	if (pick >= 0) {
		g->gpii = pick;
		return 0;
	}

	if (last < 0)
		return -ENODEV;

	/* All taken (e.g. left over from an earlier boot stage), reclaim the last one */
	g->gpii = last;
	GPITRACE("no free gpii, reclaiming gpii %d\n", last);
	gpi_release(g);

	return 0;
}

/* ---------------------------------------------------------------- */
/* SE side                                                          */
/* ---------------------------------------------------------------- */

static void geni_se_select_gpi_mode(phys_addr_t base)
{
	u32 val;

	writel(0, base + SE_GSI_EVENT_EN);
	writel(0xffffffff, base + SE_GENI_M_IRQ_CLEAR);
	writel(0xffffffff, base + SE_GENI_S_IRQ_CLEAR);
	writel(0xffffffff, base + SE_DMA_TX_IRQ_CLR);
	writel(0xffffffff, base + SE_DMA_RX_IRQ_CLR);
	writel(0xffffffff, base + SE_IRQ_EN);

	writel(0, base + SE_IRQ_EN);

	val = readl(base + SE_GENI_M_IRQ_EN);
	val &= ~(M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN |
		 M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);
	writel(val, base + SE_GENI_M_IRQ_EN);

	writel(GENI_DMA_MODE_EN, base + SE_GENI_DMA_MODE_EN);

	val = readl(base + SE_GSI_EVENT_EN);
	val |= DMA_RX_EVENT_EN | DMA_TX_EVENT_EN | GENI_M_EVENT_EN | GENI_S_EVENT_EN;
	writel(val, base + SE_GSI_EVENT_EN);
}

/* ---------------------------------------------------------------- */
/* Transfers                                                        */
/* ---------------------------------------------------------------- */

/* Poll the event ring until a completion shows up for @chid */
static int gpi_wait_event(struct geni_gpi *g, u32 chid, u32 timeout_ms)
{
	struct gpi_ring *ev = &g->ev;
	u32 n = g->gpii;
	ulong start = get_timer(0);
	int ret = -ETIMEDOUT;
	bool got = false;

	while (get_timer(start) < timeout_ms) {
		u32 rp_off = gpi_rd(g, GPII_EV_CNTXT(n) + CNTXT_4_RP_LSB) -
			     lower_32_bits(ev->phys);

		if (rp_off >= ev->len || (rp_off & (GPI_EL_SIZE - 1))) {
			GPIERR("bogus event ring RP 0x%x\n", rp_off);
			gpi_dump(g, "bad-rp");
			return -EIO;
		}

		if (rp_off != ev->rp) {
			invalidate_dcache_range((ulong)ev->base, (ulong)ev->base + ev->len);

			while (ev->rp != rp_off) {
				u32 *e = ev->base + ev->rp;
				u32 code = e[2] >> 24;
				u32 type = (e[3] >> 16) & 0xff;
				u32 ech = e[3] >> 24;

				GPITRACE("EV@+0x%x: %08x %08x %08x %08x (type 0x%x chid %u code %u len %u)\n",
					 ev->rp, e[0], e[1], e[2], e[3], type, ech, code, e[2] & 0xffffff);

				if (type == EV_TYPE_XFER_COMPLETE && ech == chid) {
					got = true;
					ret = code == EV_CODE_UNEXP_ERR ? -EREMOTEIO : 0;
				}

				/* give the element back to the HW */
				ev->wp = (ev->wp + GPI_EL_SIZE) & (ev->len - 1);
				ev->rp = (ev->rp + GPI_EL_SIZE) & (ev->len - 1);
			}

			gpi_ring_ev_db(g);
			gpi_wr(g, GPII_IEOB_IRQ_CLR(n), BIT(0));

			if (got)
				return ret;
		}

		if (gpi_rd(g, GPII_GLOB_IRQ_STTS(n))) {
			GPIERR("global error while waiting for chid %u\n", chid);
			gpi_dump(g, "glob-err");
			gpi_wr(g, GPII_GLOB_IRQ_CLR(n), gpi_rd(g, GPII_GLOB_IRQ_STTS(n)));
			return -EIO;
		}

		udelay(10);
	}

	GPIERR("timeout waiting for chid %u\n", chid);
	gpi_dump(g, "timeout");

	return ret;
}

static void gpi_queue_i2c_config(struct gpi_ring *tx, const struct geni_gpi_timing *t)
{
	gpi_queue_tre(tx, t->t_low_cnt | (t->t_high_cnt << 8) | (t->t_cycle_cnt << 16) |
		      TRE_I2C_C0_TX_PACK | TRE_I2C_C0_RX_PACK,
		      0, t->clk_div, (TRE_TYPE_CONFIG0 << TRE_TYPE_SHIFT) | TRE_FLAG_CHAIN);
}

static int gpi_xfer_one(struct geni_gpi *g, struct i2c_msg *msg, bool first, bool last,
			const struct geni_gpi_timing *t)
{
	struct gpi_ring *tx = &g->ch[GPI_TX_CHAN];
	struct gpi_ring *rx = &g->ch[GPI_RX_CHAN];
	bool rd = msg->flags & I2C_M_RD;
	u32 len = msg->len;
	u32 go0, buflen;
	u64 dma;
	u8 *buf;
	int ret;

	/*
	 * The engine can't do a zero-length transfer; i2c_probe_chip() uses
	 * one to detect a chip, so turn it into a one byte read.
	 */
	if (!len) {
		rd = true;
		len = 1;
	}

	buflen = ALIGN(len, ARCH_DMA_MINALIGN);
	buf = memalign(ARCH_DMA_MINALIGN, buflen);
	if (!buf)
		return -ENOMEM;

	memset(buf, 0, buflen);
	if (!rd)
		memcpy(buf, msg->buf, len);
	flush_dcache_range((ulong)buf, (ulong)buf + buflen);
	dma = virt_to_phys(buf);

	go0 = (rd ? I2C_GO_READ : I2C_GO_WRITE) | (msg->addr << TRE_I2C_GO_ADDR_SHIFT);
	if (!last)
		go0 |= TRE_I2C_GO_STRETCH;

	if (first)
		gpi_queue_i2c_config(tx, t);

	if (rd) {
		/* no TX DMA TRE for a read, it would put a write cycle on the bus */
		gpi_queue_tre(tx, go0, 0, len, (TRE_TYPE_GO << TRE_TYPE_SHIFT) | TRE_FLAG_LINK);
		gpi_queue_tre(rx, lower_32_bits(dma), upper_32_bits(dma), len,
			      (TRE_TYPE_DMA << TRE_TYPE_SHIFT) | TRE_FLAG_IEOT);
		/* RX first so the buffer is ready when the data starts to flow */
		gpi_ring_ch_db(g, GPI_RX_CHAN);
	} else {
		gpi_queue_tre(tx, go0, 0, len, (TRE_TYPE_GO << TRE_TYPE_SHIFT) | TRE_FLAG_CHAIN);
		gpi_queue_tre(tx, lower_32_bits(dma), upper_32_bits(dma), len,
			      (TRE_TYPE_DMA << TRE_TYPE_SHIFT) | TRE_FLAG_IEOT);
	}
	gpi_ring_ch_db(g, GPI_TX_CHAN);

	ret = gpi_wait_event(g, rd ? GPI_RX_CHAN : GPI_TX_CHAN, GPI_XFER_TIMEOUT_MS + len);

	if (!ret && rd && msg->len) {
		invalidate_dcache_range((ulong)buf, (ulong)buf + buflen);
		memcpy(msg->buf, buf, msg->len);
		GPITRACE("rx addr 0x%x len %u: %02x %02x %02x %02x\n", msg->addr, msg->len,
			 buf[0], len > 1 ? buf[1] : 0, len > 2 ? buf[2] : 0, len > 3 ? buf[3] : 0);
	}

	free(buf);

	return ret;
}

int geni_gpi_xfer(struct geni_gpi *g, struct i2c_msg *msgs, int num,
		  const struct geni_gpi_timing *t)
{
	int i, ret = 0;

	if (!g->up) {
		ret = gpi_setup(g);
		if (ret)
			return ret;
	}

	for (i = 0; i < num; i++) {
		ret = gpi_xfer_one(g, &msgs[i], i == 0, i == num - 1, t);
		if (ret) {
			GPITRACE("msg %d/%d (addr 0x%x len %u %s) failed: %d\n", i, num,
				 msgs[i].addr, msgs[i].len,
				 msgs[i].flags & I2C_M_RD ? "rd" : "wr", ret);
			break;
		}
	}

	if (ret) {
		/* NACKs are routine (i2c probe), so only reset the channels, not the GPII */
		if (gpi_recover(g)) {
			GPIERR("channel recovery failed, rebuilding the GPII\n");
			gpi_shutdown(g);
		}
		return ret;
	}

	return 0;
}

void geni_gpi_remove(struct geni_gpi *g)
{
	if (!g)
		return;

	gpi_shutdown(g);
}

int geni_gpi_init(struct udevice *dev, phys_addr_t se_base, struct geni_gpi **gpip)
{
	struct ofnode_phandle_args args;
	struct geni_gpi *g;
	fdt_addr_t gpi_base;
	ofnode gpi_node;
	u32 ee_offset;
	int ret;

	ret = dev_read_phandle_with_args(dev, "dmas", "#dma-cells", 0, 0, &args);
	if (ret) {
		dev_err(dev, "no \"dmas\" for GPI mode: %d\n", ret);
		return ret;
	}
	if (args.args_count < 3) {
		dev_err(dev, "need 3 dma cells, got %d\n", args.args_count);
		return -EINVAL;
	}
	gpi_node = args.node;

	g = calloc(1, sizeof(*g));
	if (!g)
		return -ENOMEM;

	g->dev = dev;
	g->se_base = se_base;
	g->seid = args.args[1];
	g->proto = args.args[2];

	gpi_base = ofnode_get_addr(gpi_node);
	if (gpi_base == FDT_ADDR_T_NONE) {
		dev_err(dev, "GPI controller has no reg\n");
		ret = -EINVAL;
		goto err;
	}
	g->gpii_mask = ofnode_read_u32_default(gpi_node, "dma-channel-mask", 0);
	if (!g->gpii_mask) {
		dev_err(dev, "GPI controller has no dma-channel-mask\n");
		ret = -EINVAL;
		goto err;
	}

	/* same as the Linux match data: everything but sdm845 has the EE block 64K down */
	ee_offset = ofnode_device_is_compatible(gpi_node, "qcom,sdm845-gpi-dma") ? 0 : 0x10000;
	g->ee = (void __iomem *)(gpi_base - ee_offset);

	GPITRACE("gpi@0x%llx ee=0x%llx mask=0x%x seid=%u proto=%u\n",
		 (unsigned long long)gpi_base, (unsigned long long)(gpi_base - ee_offset),
		 g->gpii_mask, g->seid, g->proto);

	ret = gpi_pick_gpii(g);
	if (ret) {
		dev_err(dev, "no usable GPII\n");
		goto err;
	}
	GPITRACE("using gpii %u\n", g->gpii);

	geni_se_select_gpi_mode(se_base);

	ret = gpi_setup(g);
	if (ret) {
		dev_err(dev, "GPII setup failed: %d\n", ret);
		goto err;
	}

	*gpip = g;

	return 0;
err:
	free(g);

	return ret;
}
