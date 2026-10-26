// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023, Intel Corporation.
 *
 * Portions based on U-Boot's dwc_eth_qos.c.
 */

/*
 * This driver supports the Synopsys Designware Ethernet XGMAC (10G Ethernet
 * MAC) IP block. The IP supports multiple options for bus type, clocking/
 * reset structure, and feature list.
 *
 * The driver is written such that generic core logic is kept separate from
 * configuration-specific logic. Code that interacts with configuration-
 * specific resources is split out into separate functions to avoid polluting
 * common code. If/when this driver is enhanced to support multiple
 * configurations, the core code should be adapted to call all configuration-
 * specific functions through function pointers, with the definition of those
 * function pointers being supplied by struct udevice_id xgmac_ids[]'s .data
 * field.
 *
 * This configuration uses an AXI master/DMA bus, an AHB slave/register bus,
 * contains the DMA, MTL, and MAC sub-blocks, and supports a single RGMII PHY.
 * This configuration also has SW control over all clock and reset signals to
 * the HW block.
 */

#define LOG_CATEGORY UCLASS_ETH

#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <eth_phy.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <miiphy.h>
#include <net.h>
#include <console.h>
#include <linux/kernel.h>	/* upper_32_bits, lower_32_bits */
#include <env.h>
#include <netdev.h>
#include <phy.h>
#include <reset.h>
#include <vsprintf.h>
#include <wait_bit.h>
#include <asm/cache.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/mdio.h>
#include "dwc_eth_xgmac.h"

static void *xgmac_alloc_descs(struct xgmac_priv *xgmac, unsigned int num)
{
	return memalign(ARCH_DMA_MINALIGN, num * xgmac->desc_size);
}

static void xgmac_free_descs(void *descs)
{
	free(descs);
}

static struct xgmac_desc *xgmac_get_desc(struct xgmac_priv *xgmac,
					 unsigned int num, bool rx)
{
	return (rx ? xgmac->rx_descs : xgmac->tx_descs) +
	       (num * xgmac->desc_size);
}

static ulong xgmac_rx_tail_addr(struct xgmac_priv *xgmac, unsigned int next_idx);
static unsigned int xgmac_rx_stall_idx;
static ulong xgmac_rx_stall_since;
static bool xgmac_rx_upstream(void);
static void xgmac_rx_tail_track(struct xgmac_priv *xgmac);
static void xgmac_rx_stall_check(struct xgmac_priv *xgmac);
static void xgmac_rx_chan_restart(struct xgmac_priv *xgmac);
static int xgmac_free_pkt(struct udevice *dev, uchar *packet, int length);
/*
 * D300: 64 was d246's raise from 8, and d246 and d247 both ended by exhausting
 * it, not by the link failing - 64 recoveries times 63 frames is ~6 MB and the
 * tftp server then times out waiting for an ACK the driver can no longer send.
 * The wedge is proven recoverable (thousands of frames across 64 resets), so
 * the budget is not protecting anything; it only decides how big a transfer
 * can be. 512 covers a ~48 MB transfer. "setenv eth_rxrestart 0" still
 * disables the recovery outright.
 */
/*
 * D321: one recovery per ~96 KB of receive traffic, so this is a cap on
 * transfer size, not a safety net against a runaway loop - and 512 caps it at
 * about 48 MB per network command. The 8 MiB run used 90. Raised to 4096, worth
 * ~384 MB, which is past anything a bootloader loads; the budget still exists so
 * that a genuinely broken link cannot spin forever.
 */
#define XGMAC_RX_RESTART_MAX	4096
static int xgmac_datapath_start(struct udevice *dev);
static struct udevice *xgmac_dev;
static unsigned int xgmac_rx_restarts;
/*
 * D288: xgmac_rx_frames counts from xgmac_start(), but a recovery re-inits the
 * datapath without resetting it, so the stall line could not show how far the
 * datapath got this time. Both d245 stalls came exactly 63 delivered frames
 * (64 received, one left stuck) after a start; this makes that period explicit
 * in the log instead of subtraction across two dumps.
 */
static u32 xgmac_rx_frames_at_start;
/* D309: byte and full-size-frame equivalents, same per-datapath convention. */
static u64 xgmac_rx_bytes_at_start;
static u32 xgmac_rx_full_frames_at_start;
/*
 * T-66/T-78: the rescue path's state. d252-d254 deferred the channel restart
 * to xgmac_free_pkt() and left the rescued frame in its ring buffer; D325
 * showed that hands the frame to the stack while the engine is still wedged,
 * so the ACK the stack immediately sends can time out. From d255 the frame is
 * copied out, the channel is restarted at once, and the copy is delivered on
 * the next xgmac_recv() call - i.e. on a healthy engine.
 */
static u32 xgmac_rx_rescued;
static int xgmac_rescue_len;
static u8 xgmac_rescue_buf[XGMAC_MAX_PACKET_SIZE] __aligned(ARCH_DMA_MINALIGN);
static bool xgmac_recovering;

void xgmac_inval_desc_generic(void *desc)
{
	unsigned long start;
	unsigned long end;

	if (!desc) {
		pr_err("%s invalid input buffer\n", __func__);
		return;
	}

	start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	end = ALIGN(start + sizeof(struct xgmac_desc),
		    ARCH_DMA_MINALIGN);

	invalidate_dcache_range(start, end);
}

void xgmac_flush_desc_generic(void *desc)
{
	unsigned long start;
	unsigned long end;

	if (!desc) {
		pr_err("%s invalid input buffer\n", __func__);
		return;
	}

	start = (unsigned long)desc & ~(ARCH_DMA_MINALIGN - 1);
	end = ALIGN(start + sizeof(struct xgmac_desc),
		    ARCH_DMA_MINALIGN);

	flush_dcache_range(start, end);
}

void xgmac_inval_buffer_generic(void *buf, size_t size)
{
	unsigned long start;
	unsigned long end;

	if (!buf) {
		pr_err("%s invalid input buffer\n", __func__);
		return;
	}

	start = (unsigned long)buf & ~(ARCH_DMA_MINALIGN - 1);
	end = ALIGN((unsigned long)buf + size,
		    ARCH_DMA_MINALIGN);

	invalidate_dcache_range(start, end);
}

void xgmac_flush_buffer_generic(void *buf, size_t size)
{
	unsigned long start;
	unsigned long end;

	if (!buf) {
		pr_err("%s invalid input buffer\n", __func__);
		return;
	}

	start = (unsigned long)buf & ~(ARCH_DMA_MINALIGN - 1);
	end = ALIGN((unsigned long)buf + size,
		    ARCH_DMA_MINALIGN);

	flush_dcache_range(start, end);
}

static int xgmac_mdio_wait_idle(struct xgmac_priv *xgmac)
{
	return wait_for_bit_le32(&xgmac->mac_regs->mdio_data,
				 XGMAC_MAC_MDIO_ADDRESS_SBUSY, false,
				 XGMAC_TIMEOUT_100MS, true);
}

/*
 * A keypress breaks the MDIO poll, and every read after it fails with -EINTR
 * (-4) rather than -ETIMEDOUT (D101). Printing one line per failed read buries
 * the console: pass 58 produced ten identical aborts inside a single link wait,
 * and the operator was pressing keys in the first place because the ARP retry
 * had already held the console for 25 s. Report the first abort of a command and
 * stay quiet for the rest of it. Cleared in xgmac_start(). D231.
 */
static bool xgmac_mdio_aborted;

static int xgmac_mdio_fail(struct xgmac_priv *xgmac, const char *what, int ret)
{
	if (ret == -EINTR) {
		if (!xgmac_mdio_aborted) {
			xgmac_mdio_aborted = true;
			pr_err("%s MDIO aborted by a keypress (-EINTR) at '%s'; further aborts in this command are silent\n",
			       xgmac->dev->name, what);
		}

		return ret;
	}

	pr_err("%s MDIO %s: %d\n", xgmac->dev->name, what, ret);

	return ret;
}

static int xgmac_mdio_read(struct mii_dev *bus, int mdio_addr, int mdio_devad,
			   int mdio_reg)
{
	struct xgmac_priv *xgmac = bus->priv;
	u32 val;
	u32 hw_addr;
	int ret;

	debug("%s(dev=%p, addr=0x%x, reg=%d):\n", __func__, xgmac->dev, mdio_addr,
	      mdio_reg);

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "not idle at entry", ret);

	if (mdio_devad == MDIO_DEVAD_NONE) {
		val = BIT(mdio_addr);
		writel(val, &xgmac->mac_regs->mdio_clause_22_port);

		hw_addr = (mdio_addr << XGMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
			   (mdio_reg & XGMAC_MAC_MDIO_REG_ADDR_C22P_MASK);
	} else {
		writel(0, &xgmac->mac_regs->mdio_clause_22_port);

		hw_addr = (mdio_devad << XGMAC_MAC_MDIO_ADDRESS_DA_SHIFT) |
			   (mdio_addr << XGMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
			   (mdio_reg & XGMAC_MAC_MDIO_ADDRESS_RA_MASK);
	}

	val = xgmac->config->config_mac_mdio <<
	      XGMAC_MAC_MDIO_ADDRESS_CR_SHIFT;

	val |= XGMAC_MDIO_SINGLE_CMD_ADDR_CMD_READ |
	       XGMAC_MAC_MDIO_ADDRESS_SBUSY;

	/*
	 * SADDR (BIT(18)) is "Skip ADDRess packet", NOT "send address" - the
	 * name reads backwards. When it is SET the MAC omits the Clause-45
	 * address frame, so the PHY never receives RA and answers out of
	 * whatever its internal address register still holds (RA=0 after
	 * reset). Upstream forces this bit unconditionally, which makes every
	 * C45 read return MMD register 0 regardless of the register asked for.
	 *
	 * Confirmed on Amboseli hardware, DA=1/PA=1 (docs/PASS2_RESULT.md
	 * pass 7): RA=3 with SADDR set returns 0x2040 (PMA/PMD Control 1,
	 * i.e. RA=0); the identical request with SADDR clear returns 0xc898,
	 * matching golden Linux's PHYID2. Requesting RA=2 with SADDR set
	 * returns the previous transaction's RA=3 answer, not PHYID1 - the
	 * address register is simply never conveyed to the PHY.
	 *
	 * Upstream's own EQoS driver treats the equivalent bit correctly: it
	 * calls it SKAP (Skip Address Packet) and only ever *preserves* it
	 * (dwc_eth_qos.c: "val &= EQOS_MAC_MDIO_ADDRESS_SKAP"), never sets it.
	 *
	 * Clause 22 has no address frame for this bit to skip, and a C22 read
	 * returns a sane BMCR (0x1040) with it set, so the C22 path is left
	 * byte-identical to upstream rather than changed on a path we have no
	 * failing evidence for.
	 */
	if (mdio_devad == MDIO_DEVAD_NONE)
		val |= XGMAC_MAC_MDIO_ADDRESS_SADDR;

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "not idle at entry", ret);

	writel(hw_addr, &xgmac->mac_regs->mdio_address);
	writel(val, &xgmac->mac_regs->mdio_data);

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "read didn't complete", ret);

	val = readl(&xgmac->mac_regs->mdio_data);
	val &= XGMAC_MAC_MDIO_DATA_GD_MASK;

	debug("%s: val=0x%x\n", __func__, val);

	return val;
}

static int xgmac_mdio_write(struct mii_dev *bus, int mdio_addr, int mdio_devad,
			    int mdio_reg, u16 mdio_val)
{
	struct xgmac_priv *xgmac = bus->priv;
	u32 val;
	u32 hw_addr;
	int ret;

	debug("%s(dev=%p, addr=0x%x, reg=%d, val=0x%x):\n", __func__, xgmac->dev,
	      mdio_addr, mdio_reg, mdio_val);

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "not idle at entry", ret);

	if (mdio_devad == MDIO_DEVAD_NONE) {
		val = BIT(mdio_addr);
		writel(val, &xgmac->mac_regs->mdio_clause_22_port);

		hw_addr = (mdio_addr << XGMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
			   (mdio_reg & XGMAC_MAC_MDIO_REG_ADDR_C22P_MASK);
	} else {
		writel(0, &xgmac->mac_regs->mdio_clause_22_port);

		hw_addr = (mdio_devad << XGMAC_MAC_MDIO_ADDRESS_DA_SHIFT) |
			   (mdio_addr << XGMAC_MAC_MDIO_ADDRESS_PA_SHIFT) |
			   (mdio_reg & XGMAC_MAC_MDIO_ADDRESS_RA_MASK);
	}

	val = (xgmac->config->config_mac_mdio <<
	       XGMAC_MAC_MDIO_ADDRESS_CR_SHIFT);

	val |= mdio_val | XGMAC_MDIO_SINGLE_CMD_ADDR_CMD_WRITE |
		XGMAC_MAC_MDIO_ADDRESS_SBUSY;

	/* See the SADDR comment in xgmac_mdio_read(): set => address frame
	 * skipped. A C45 write needs the address frame just as a read does.
	 */
	if (mdio_devad == MDIO_DEVAD_NONE)
		val |= XGMAC_MAC_MDIO_ADDRESS_SADDR;

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "not idle at entry", ret);

	writel(hw_addr, &xgmac->mac_regs->mdio_address);
	writel(val, &xgmac->mac_regs->mdio_data);

	ret = xgmac_mdio_wait_idle(xgmac);
	if (ret)
		return xgmac_mdio_fail(xgmac, "write didn't complete", ret);

	return 0;
}

static int xgmac_set_full_duplex(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	debug("%s(dev=%p):\n", __func__, dev);

	clrbits_le32(&xgmac->mac_regs->mac_extended_conf, XGMAC_MAC_EXT_CONF_HD);

	return 0;
}

static int xgmac_set_half_duplex(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	debug("%s(dev=%p):\n", __func__, dev);

	setbits_le32(&xgmac->mac_regs->mac_extended_conf, XGMAC_MAC_EXT_CONF_HD);

	/* WAR: Flush TX queue when switching to half-duplex */
	setbits_le32(&xgmac->mtl_chan->txq0_operation_mode,
		     XGMAC_MTL_TXQ0_OPERATION_MODE_FTQ);

	return 0;
}

static int xgmac_set_gmii_speed(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 val;

	debug("%s(dev=%p):\n", __func__, dev);

	val = XGMAC_MAC_CONF_SS_1G_GMII << XGMAC_MAC_CONF_SS_SHIFT;
	writel(val, &xgmac->mac_regs->tx_configuration);

	return 0;
}

static int xgmac_set_mii_speed_100(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 val;

	debug("%s(dev=%p):\n", __func__, dev);

	val = XGMAC_MAC_CONF_SS_100M_MII << XGMAC_MAC_CONF_SS_SHIFT;
	writel(val, &xgmac->mac_regs->tx_configuration);

	return 0;
}

static int xgmac_set_mii_speed_10(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 val;

	debug("%s(dev=%p):\n", __func__, dev);

	val = XGMAC_MAC_CONF_SS_2_10M_MII << XGMAC_MAC_CONF_SS_SHIFT;
	writel(val, &xgmac->mac_regs->tx_configuration);

	return 0;
}

static int xgmac_adjust_link(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;
	bool en_calibration;

	debug("%s(dev=%p):\n", __func__, dev);

	if (xgmac->phy->duplex)
		ret = xgmac_set_full_duplex(dev);
	else
		ret = xgmac_set_half_duplex(dev);
	if (ret < 0) {
		pr_err("%s xgmac_set_*_duplex() failed: %d\n", dev->name, ret);
		return ret;
	}

	switch (xgmac->phy->speed) {
	case SPEED_1000:
		en_calibration = true;
		ret = xgmac_set_gmii_speed(dev);
		break;
	case SPEED_100:
		en_calibration = true;
		ret = xgmac_set_mii_speed_100(dev);
		break;
	case SPEED_10:
		en_calibration = false;
		ret = xgmac_set_mii_speed_10(dev);
		break;
	default:
		pr_err("%s invalid speed %d\n", dev->name, xgmac->phy->speed);
		return -EINVAL;
	}
	if (ret < 0) {
		pr_err("%s xgmac_set_*mii_speed*() failed: %d\n", dev->name, ret);
		return ret;
	}

	if (en_calibration) {
		ret = xgmac->config->ops->xgmac_calibrate_pads(dev);
		if (ret < 0) {
			pr_err("%s xgmac_calibrate_pads() failed: %d\n",
			       dev->name, ret);

			return ret;
		}
	} else {
		ret = xgmac->config->ops->xgmac_disable_calibration(dev);
		if (ret < 0) {
			pr_err("%s xgmac_disable_calibration() failed: %d\n",
			       dev->name, ret);

			return ret;
		}
	}

	return 0;
}

static int xgmac_write_hwaddr(struct udevice *dev)
{
	struct eth_pdata *plat = dev_get_plat(dev);
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 val;

	/*
	 * This function may be called before start() or after stop(). At that
	 * time, on at least some configurations of the XGMAC HW, all clocks to
	 * the XGMAC HW block will be stopped, and a reset signal applied. If
	 * any register access is attempted in this state, bus timeouts or CPU
	 * hangs may occur. This check prevents that.
	 *
	 * A simple solution to this problem would be to not implement
	 * write_hwaddr(), since start() always writes the MAC address into HW
	 * anyway. However, it is desirable to implement write_hwaddr() to
	 * support the case of SW that runs subsequent to U-Boot which expects
	 * the MAC address to already be programmed into the XGMAC registers,
	 * which must happen irrespective of whether the U-Boot user (or
	 * scripts) actually made use of the XGMAC device, and hence
	 * irrespective of whether start() was ever called.
	 *
	 */
	if (!xgmac->config->reg_access_always_ok && !xgmac->reg_access_ok)
		return 0;

	/* Update the MAC address */
	val = (plat->enetaddr[5] << 8) |
		(plat->enetaddr[4]);
	writel(val, &xgmac->mac_regs->address0_high);
	val = (plat->enetaddr[3] << 24) |
		(plat->enetaddr[2] << 16) |
		(plat->enetaddr[1] << 8) |
		(plat->enetaddr[0]);
	writel(val, &xgmac->mac_regs->address0_low);
	return 0;
}

static int xgmac_read_rom_hwaddr(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_plat(dev);
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	ret = xgmac->config->ops->xgmac_get_enetaddr(dev);
	if (ret < 0)
		return ret;

	return !is_valid_ethaddr(pdata->enetaddr);
}

static int xgmac_get_phy_addr(struct xgmac_priv *priv, struct udevice *dev)
{
	struct ofnode_phandle_args phandle_args;
	int reg;

	if (dev_read_phandle_with_args(dev, "phy-handle", NULL, 0, 0,
				       &phandle_args)) {
		debug("Failed to find phy-handle");
		return -ENODEV;
	}

	priv->phy_of_node = phandle_args.node;

	reg = ofnode_read_u32_default(phandle_args.node, "reg", 0);

	return reg;
}

/*
 * Select between the upstream channel-register offsets and the SoC's relocated
 * ones, at start() time rather than probe() time.
 *
 * NB: The relocated bases are OFF unless eth_chan_base is set, and that is
 * deliberate. probe() runs during boot enumeration, before anyone can type a
 * command, and on this SoC an access to a window the XPU does not allow does
 * not return an error - it hangs the board hard enough to need a physical power
 * cycle. The relocated dma_chan_base 0x8500 sits above 0x22306fff, i.e. past
 * everything this port has ever successfully touched, and just above the
 * 0x22307000-0x22308000 TZ-only hole. So it gets the same treatment as
 * eth_wrapper and eth_serdes: the board always reaches a prompt, and the
 * operator decides when to take the risk.
 *
 *   setenv eth_chan_base 1
 */
/*
 * Diagnostic gates default to ON in this build and are opted *out* of, not in.
 * Passes 30, 31 and 32 were all lost to a mistyped variable name - eth_chat_base,
 * then regdump and txprobe without their eth_ prefix - and a silently skipped
 * diagnostic is indistinguishable in the log from a diagnostic that ran and found
 * nothing. "setenv <name> 0" turns one off.
 */
static bool xgmac_env_off(const char *name)
{
	const char *s = env_get(name);

	return s && (!strcmp(s, "0") || !strcmp(s, "off"));
}

/*
 * The opt-in twin. The diagnostics were opt-out while the datapath was under
 * investigation (D139) because a mistyped variable name looked exactly like a
 * probe that ran and found nothing, and three passes were lost that way. D163
 * closed the bring-up, so they invert back: quiet by default, one setenv away.
 */
static bool xgmac_env_on(const char *name)
{
	const char *s = env_get(name);

	return s && strcmp(s, "0") && strcmp(s, "off");
}

static void xgmac_select_chan_regs(struct xgmac_priv *xgmac)
{
	/*
	 * Relocated by default: 0x8000/0x8500 are this SoC's real channel bases
	 * and are proven writable (D66, four readbacks for four). The upstream
	 * 0x1100/0x3100 defaults are simply wrong here, so make the wrong value
	 * the one that has to be asked for.
	 */
	xgmac->chan_relocated = xgmac->config->dma_chan_base &&
				!xgmac_env_off("eth_chan_base");

	if (xgmac->chan_relocated) {
		xgmac->mtl_chan = (void *)(xgmac->regs +
					   xgmac->config->mtl_chan_base);
		xgmac->dma_chan = (void *)(xgmac->regs +
					   xgmac->config->dma_chan_base);
		debug("%s: relocated channel regs: mtl +0x%x, dma +0x%x\n",
		      xgmac->dev->name, xgmac->config->mtl_chan_base,
		      xgmac->config->dma_chan_base);
	} else {
		xgmac->mtl_chan = (void *)(xgmac->regs +
					   XGMAC_MTL_CHAN_BASE_DEFAULT);
		xgmac->dma_chan = (void *)(xgmac->regs +
					   XGMAC_DMA_CHAN_BASE_DEFAULT);
	}
}

/*
 * Which MMD is holding the link down?
 *
 * gen10g_startup() ANDs phydev->mmds with MDIO_DEVS_LINK - MMDs 1 (PMA/PMD),
 * 2 (WIS), 3 (PCS), 4 (PHY XS), 5 (DTE XS) and 7 (AN) - and clears the link if
 * *any* present one fails to report MDIO_STAT1_LSTATUS. It never says which,
 * which reduces "No link" to a guess between the media side and the system
 * side. Print the whole set instead.
 *
 * MDIO_STAT1 latches low: a read moves the current state into the register, so
 * the first read can report a stale down. Read twice, print both, and judge on
 * the second - the same discipline gen10g_startup() itself uses.
 */
static void xgmac_dump_c45_link(struct phy_device *phydev)
{
	static const char * const mmd_name[] = {
		[MDIO_MMD_PMAPMD]	= "PMA/PMD",
		[MDIO_MMD_WIS]		= "WIS",
		[MDIO_MMD_PCS]		= "PCS",
		[MDIO_MMD_PHYXS]	= "PHY XS",
		[MDIO_MMD_DTEXS]	= "DTE XS",
		[MDIO_MMD_AN]		= "AN",
	};
	u32 mask = phydev->mmds & MDIO_DEVS_LINK;
	int devad;

	if (!phydev->is_c45)
		return;

	printf("  mmds 0x%08x, link-reporting set 0x%08x\n",
	       phydev->mmds, mask);

	for (devad = 0; devad < 32; devad++) {
		const char *name = NULL;
		int first, second;

		if (!(mask & BIT(devad)))
			continue;

		if (devad < ARRAY_SIZE(mmd_name) && mmd_name[devad])
			name = mmd_name[devad];

		first = phy_read(phydev, devad, MDIO_STAT1);
		second = phy_read(phydev, devad, MDIO_STAT1);

		/*
		 * An interrupted poll returns -EINTR for every remaining MMD, and
		 * each one would print an all-ones STAT1 and a bogus "link DOWN
		 * <-- this one". Stop rather than print a column of lies. D231.
		 */
		if (first == -EINTR || second == -EINTR) {
			printf("  MDIO aborted by a keypress - link state unknown\n");
			return;
		}

		printf("  MMD %2d %-8s STAT1 0x%04x -> 0x%04x  %s\n",
		       devad, name ? name : "?", first, second,
		       (second >= 0 && (second & MDIO_STAT1_LSTATUS)) ?
		       "LINK UP" : "link DOWN  <-- this one");
	}
}

/*
 * Bring the PHY up, and give a link that is still coming up time to appear.
 *
 * gen10g_startup() takes one snapshot: it reads MDIO_STAT1 (twice, for the
 * latch-low semantics) on every present MMD and clears the link if any of them
 * is down. It does not poll. So a link in the middle of establishing reads as a
 * hard down and start() fails, and the operator sees a dead interface that
 * works on the next command.
 *
 * That is not hypothetical - it is D103. Two identical dhcp runs in one boot,
 * forty seconds apart, read the same PHY's MMD 1 and MMD 7 as down and then as
 * up. The SoC-side delay this driver's glue inserts covers the SerDes PLL, not
 * the external PHY's own autonegotiation, which takes hundreds of milliseconds
 * to seconds.
 *
 * So retry. phy_startup() on a C45 PHY is a handful of MDIO reads with no side
 * effects, which makes it safe to call repeatedly, and the first call almost
 * always succeeds - the loop costs nothing when the link is already up.
 *
 * Six seconds, not three: pass 24 timed out at three with a cold PHY and then
 * read the same PHY as up on the next command. The loop exits the moment the
 * link appears, so a generous budget costs a working link nothing.
 *
 *   setenv eth_link_wait <ms>   override the budget (0 disables the retry)
 */
/*
 * gen10g_startup() requires *every* MMD in phydev->mmds & MDIO_DEVS_LINK to
 * report link, MMD 3 (PCS) included. For a multi-rate PHY that is wrong below
 * 10G: MMD 3 is the 10GBASE-R PCS, and it is legitimately down when the media
 * side settled on 1000BASE-T. Pass 25 proved it - the SoC's own XPCS reached
 * receive-link-up with the fault bit clear at 1000 Mbps while MMD 3 stayed at
 * 0x0002 - so MMD 3 was never a symptom of anything on the SoC side. See D113.
 *
 * Re-evaluate the link over a narrower set: PMA/PMD (1), which is the media
 * side, and AN (7), and not the PCS. Override with, say,
 *   setenv eth_c45_link_mask 8a    (upstream's set, PCS included)
 *   setenv eth_c45_link_mask 2     (PMA/PMD alone)
 * MDIO_STAT1 link status is latch-low, so every MMD is read twice and the
 * second read is the live one - the same discipline gen10g_startup() uses.
 */
#define XGMAC_C45_LINK_MASK_DEFAULT	(MDIO_DEVS_LINK & ~MDIO_DEVS_PCS)

static void xgmac_c45_relax_link(struct udevice *dev, struct phy_device *phydev)
{
	const char *s = env_get("eth_c45_link_mask");
	u32 want = XGMAC_C45_LINK_MASK_DEFAULT;
	int devad, reg;
	u32 mask;

	if (!phydev->is_c45 || phydev->link)
		return;

	if (s)
		want = simple_strtoul(s, NULL, 16);

	mask = phydev->mmds & want;
	if (!mask)
		return;

	phydev->link = 1;

	for (devad = 0; mask; devad++, mask >>= 1) {
		if (!(mask & 1))
			continue;

		phy_read(phydev, devad, MDIO_STAT1);
		reg = phy_read(phydev, devad, MDIO_STAT1);
		if (reg < 0 || !(reg & MDIO_STAT1_LSTATUS))
			phydev->link = 0;
	}

	if (phydev->link)
		debug("%s link up on the reduced c45 set 0x%02x (the PCS MMD is not required below 10G)\n",
		      dev->name, phydev->mmds & want);
}

#define XGMAC_LINK_WAIT_MS_DEFAULT	6000
#define XGMAC_LINK_WAIT_POLL_MS		100

/*
 * Energy Efficient Ethernet (802.3az) on the copper side is negotiated between
 * our PHY and the link partner, and once both advertise it either end may drop
 * into LPI whenever it has nothing to send. Coming out of LPI costs the
 * receiver a resynchronisation, and a U-Boot ping is exactly the pathological
 * case: one broadcast after tens of seconds of silence, with no TCP to retry it
 * and (per ping.c's "XXX always send arp request") an ARP that must succeed
 * before any payload can leave at all. Pass 58 and pass 59 bracket this - with
 * EEE left on at the host the AN-complete failures ran 0/3, with EEE disabled by
 * hand at the host they ran 10/10 across 30 s idles.
 *
 * A host-side setting is not a fix; nobody shipping this board can require it.
 * So clear our own EEE advertisement before autonegotiation: 802.3az makes EEE
 * usable only if BOTH ends advertise it, so a cleared 7.60 keeps the link out of
 * LPI no matter how the partner is configured.
 *
 * Read-only unless it has to write, and it prints what it found either way:
 * 7.60 is what we advertise, 7.61 is what the partner advertised last time
 * around, and 3.20 is what the PHY is capable of. Those three are the evidence
 * for whether EEE was ever actually negotiated on this bench, which nothing in
 * this log has yet measured directly.
 *
 * "setenv eth_eee 1" leaves the advertisement alone, for an A/B in one boot.
 * D243.
 */
static void xgmac_phy_disable_eee(struct udevice *dev, struct phy_device *phydev)
{
	int able, adv, lpable, ctrl, ret;

	able = phy_read(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_ABLE);
	adv = phy_read(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
	lpable = phy_read(phydev, MDIO_MMD_AN, MDIO_AN_EEE_LPABLE);

	debug("  EEE: PCS able 3.20 0x%04x, our adv 7.60 0x%04x, partner 7.61 0x%04x\n",
	      able & 0xffff, adv & 0xffff, lpable & 0xffff);

	if (adv < 0)
		return;

	if (env_get_yesno("eth_eee") == 1) {
		debug("  EEE: left enabled by eth_eee=1\n");

		return;
	}

	if (!(adv & 0xffff)) {
		debug("  EEE: already not advertised\n");

		return;
	}

	ret = phy_write(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0);
	if (ret < 0) {
		pr_err("%s: could not clear EEE advertisement: %d\n",
		       dev->name, ret);

		return;
	}

	/*
	 * 7.60 is only sampled while autonegotiation runs, so a link that is
	 * already up kept whatever it negotiated. Restart AN so the cleared
	 * advertisement takes effect; xgmac_phy_startup_wait() below is already
	 * built to wait out a re-negotiation.
	 */
	ctrl = phy_read(phydev, MDIO_MMD_AN, MDIO_CTRL1);
	if (ctrl >= 0) {
		ret = phy_write(phydev, MDIO_MMD_AN, MDIO_CTRL1,
				ctrl | MDIO_AN_CTRL1_RESTART);
		if (ret < 0)
			pr_err("%s: cleared EEE advertisement but could not restart AN: %d\n",
			       dev->name, ret);
	}

	adv = phy_read(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV);
	debug("  EEE: disabled, 7.60 now 0x%04x, AN restarted\n", adv & 0xffff);
}

static int xgmac_phy_startup_wait(struct udevice *dev, struct phy_device *phydev)
{
	const char *s = env_get("eth_link_wait");
	int budget = XGMAC_LINK_WAIT_MS_DEFAULT;
	int waited = 0;
	int ret;

	if (s)
		budget = simple_strtol(s, NULL, 10);

	ret = phy_startup(phydev);
	if (ret < 0)
		return ret;

	xgmac_c45_relax_link(dev, phydev);

	if (phydev->link || budget <= 0)
		return ret;

	debug("%s waiting up to %d ms for link\n", dev->name, budget);

	while (waited < budget) {
		mdelay(XGMAC_LINK_WAIT_POLL_MS);
		waited += XGMAC_LINK_WAIT_POLL_MS;

		/*
		 * Breakable on purpose, and this is the one place a keypress is
		 * the right answer: the operator can cut the wait short. Unlike the MDIO poll's ctrlc() (D101) an abort here
		 * cannot corrupt a measurement, because the link state is
		 * re-read from scratch on the next attempt.
		 */
		if (ctrlc()) {
			puts("Aborted\n");
			break;
		}

		ret = phy_startup(phydev);
		if (ret < 0)
			return ret;

		xgmac_c45_relax_link(dev, phydev);

		if (phydev->link) {
			debug("%s link up after %d ms\n", dev->name, waited);
			return 0;
		}
	}

	return 0;
}

/*
 * There is no way to read these back from the U-Boot prompt: by the time the
 * shell is reachable again xgmac_stop() has run, the clocks are gated and
 * every md.l on the block returns zero. So dump the whole datapath from
 * inside the driver, while it is live, gated on "setenv eth_regdump 1".
 */
/*
 * Frames handed up by xgmac_recv() since the last xgmac_start(). The pre-stop
 * descriptor snapshot cannot answer "did the ARP reply ever reach the driver?" -
 * by then the ring has been recycled and overwritten. These count it live.
 */
static u32 xgmac_rx_frames;
static u32 xgmac_rx_arp_frames;
/*
 * D309: with ring 16 the stall arrived on the datapath's 71st received frame,
 * not its 64th, and the tftp block count per datapath was unchanged - the extra
 * frames were background traffic on a noisier segment. So the trigger does not
 * count frames. Bytes and full-size frames are the two things that stayed
 * constant across both runs, and neither was being measured. Both are counted
 * from xgmac_recv() and reported in the stall line, per datapath.
 *
 * "Full" is >= 1500 payload bytes: a 1468-byte tftp block is 1514 on the wire,
 * and nothing in the background noise seen so far comes close (60, 666, 686).
 */
#define XGMAC_RX_FULL_FRAME_MIN	1500
static u64 xgmac_rx_bytes;
static u32 xgmac_rx_full_frames;

/*
 * Names for the MMC (MAC Management Counters) offsets the DWC XGMAC databook
 * defines, as absolute offsets from mac_regs (the MMC block base is 0x800 and
 * the databook numbers it from there). Only the counters this port has actually
 * seen move are listed; anything else still prints as a raw offset.
 *
 * The map was verified against a known-good run rather than assumed: a ping
 * that sent exactly two 64-byte frames, one broadcast ARP request and one
 * unicast ICMP echo, produced txoct_gb=128 txpkt_gb=2 txbcast_g=1 tx64=2
 * txuni_gb=1 txbcast_gb=1 txoct_g=128 txpkt_g=2 - eight independent counters
 * all agreeing with the frames on the wire, and pass 53 reproduced it on a
 * three-frame run (192/3/2/1/3).
 *
 * The RX half is deliberately NOT named. The same databook layout that fits the
 * TX counters fails its own consistency check on RX: pass 53's passing run
 * delivered two frames to the stack, one of them the ARP reply it was waiting
 * for, while the counter the layout calls "RX packets good+bad" read zero. The
 * unnamed offset 0x9c8 read exactly 2, and matched the delivered count in pass
 * 50 as well. So the RX block does not sit where the TX block's base implies,
 * and printing these offsets under borrowed names produced a dump that stated
 * the opposite of the truth (D217). They print as raw offsets until something
 * pins them down. See D206 for what the wrong names cost.
 */
static const struct {
	u32 off;
	const char *name;
} xgmac_mmc_names[] = {
	{ XGMAC_MMC_TXOCT_GB,		"txoct_gb" },
	{ XGMAC_MMC_TXPKT_GB,		"txpkt_gb" },
	{ XGMAC_MMC_TXBCAST_G,		"txbcast_g" },
	{ XGMAC_MMC_TXMCAST_G,		"txmcast_g" },
	{ XGMAC_MMC_TX64,		"tx64" },
	{ XGMAC_MMC_TXUNI_GB,		"txuni_gb" },
	{ XGMAC_MMC_TXMCAST_GB,		"txmcast_gb" },
	{ XGMAC_MMC_TXBCAST_GB,		"txbcast_gb" },
	{ XGMAC_MMC_TXUNDERFLOW,	"txunderflow" },
	{ XGMAC_MMC_TXOCT_G,		"txoct_g" },
	{ XGMAC_MMC_TXPKT_G,		"txpkt_g" },
	{ XGMAC_MMC_TXPAUSE,		"txpause" },
};

static const char *xgmac_mmc_name(u32 off)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xgmac_mmc_names); i++)
		if (xgmac_mmc_names[i].off == off)
			return xgmac_mmc_names[i].name;

	return NULL;
}

static void xgmac_dump_datapath(struct udevice *dev, const char *tag)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_desc *desc;
	const char *name;
	u32 v;
	int i, j;

	if (!xgmac_env_on("eth_regdump"))
		return;

	printf("== %s datapath dump (%s) ==\n", dev->name, tag);
	printf("  bases: mac 0x%08lx mtl_chan 0x%08lx dma_chan 0x%08lx relocated=%d\n",
	       (ulong)xgmac->mac_regs, (ulong)xgmac->mtl_chan,
	       (ulong)xgmac->dma_chan, xgmac->chan_relocated);

	/*
	 * hw_feature0..4 and us_tic_counter are read-only with non-zero POR
	 * values, so they are the anchor: if these read back sanely the MAC
	 * window is being decoded properly and every other read here can be
	 * believed. Pass 30 had MAC_Tx_Config reading a value nothing in the
	 * driver ever writes while MAC_Rx_Config, one word further on, read
	 * exactly right - which one of those is real cannot be told without
	 * an anchor.
	 */
	printf("  HW_Feature0..4    0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
	       readl(&xgmac->mac_regs->hw_feature0),
	       readl(&xgmac->mac_regs->hw_feature1),
	       readl(&xgmac->mac_regs->hw_feature2),
	       readl(&xgmac->mac_regs->hw_feature3),
	       readl(&xgmac->mac_regs->hw_feature4));
	printf("  US_Tic_Counter    0x%08x  MAC_Ext_Conf 0x%08x\n",
	       readl(&xgmac->mac_regs->us_tic_counter),
	       readl(&xgmac->mac_regs->mac_extended_conf));

	v = readl(&xgmac->mac_regs->tx_configuration);
	printf("  MAC_Tx_Config     0x%08x  TE=%d SS=%d  (@0x%08lx, reread 0x%08x)\n",
	       v, !!(v & XGMAC_MAC_CONF_TE),
	       (v >> XGMAC_MAC_CONF_SS_SHIFT) & 0x7,
	       (ulong)&xgmac->mac_regs->tx_configuration,
	       readl(&xgmac->mac_regs->tx_configuration));
	v = readl(&xgmac->mac_regs->rx_configuration);
	printf("  MAC_Rx_Config     0x%08x  RE=%d\n",
	       v, !!(v & XGMAC_MAC_CONF_RE));
	{
		u32 lpi = readl(&xgmac->mac_regs->lpi_control_status);

		printf("  MAC_LPI_Ctrl_Sts   0x%08x  RLPIST=%d TLPIST=%d LPIEN=%d PLS=%d LPITXA=%d LPIATE=%d\n",
		       lpi, !!(lpi & XGMAC_MAC_LPI_CS_RLPIST),
		       !!(lpi & XGMAC_MAC_LPI_CS_TLPIST),
		       !!(lpi & XGMAC_MAC_LPI_CS_LPIEN),
		       !!(lpi & XGMAC_MAC_LPI_CS_PLS),
		       !!(lpi & XGMAC_MAC_LPI_CS_LPITXA),
		       !!(lpi & XGMAC_MAC_LPI_CS_LPIATE));
		printf("  MAC_LPI_Timers     0x%08x  LPI_Auto_Entry 0x%08x\n",
		       readl(&xgmac->mac_regs->lpi_timers_control),
		       readl(&xgmac->mac_regs->lpi_auto_entry_timer));
	}

	printf("  MAC_Packet_Filter 0x%08x\n",
	       readl(&xgmac->mac_regs->mac_packet_filter));
	v = readl(&xgmac->mac_regs->rxq_ctrl0);
	printf("  MAC_RxQ_Ctrl0     0x%08x  RXQ0EN=%d\n", v, v & 0x3);
	printf("  MAC_Addr0         0x%04x%08x\n",
	       readl(&xgmac->mac_regs->address0_high) & 0xffff,
	       readl(&xgmac->mac_regs->address0_low));

	v = readl(&xgmac->mtl_chan->txq0_operation_mode);
	printf("  MTL_TxQ0_Op_Mode  0x%08x  TXQEN=%d TSF=%d\n",
	       v, (v >> 2) & 0x3, !!(v & BIT(1)));
	printf("  MTL_TxQ0_Debug    0x%08x\n",
	       readl(&xgmac->mtl_chan->txq0_debug));
	printf("  MTL_RxQ0_Op_Mode  0x%08x\n",
	       readl(&xgmac->mtl_chan->rxq0_operation_mode));
	v = readl(&xgmac->mtl_chan->rxq0_debug);
	printf("  MTL_RxQ0_Debug    0x%08x  PRXQ=%u RXQSTS=%u RRCSTS=%u\n", v,
	       (u32)((v >> XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
		     XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK),
	       (u32)((v >> XGMAC_MTL_RXQ0_DEBUG_RXQSTS_SHIFT) &
		     XGMAC_MTL_RXQ0_DEBUG_RXQSTS_MASK),
	       (v >> 1) & 0x3);

	printf("  DMA_CH0_Control   0x%08x\n",
	       readl(&xgmac->dma_chan->ch0_control));
	v = readl(&xgmac->dma_chan->ch0_tx_control);
	printf("  DMA_CH0_Tx_Ctrl   0x%08x  ST=%d\n",
	       v, !!(v & XGMAC_DMA_CH0_TX_CONTROL_ST));
	v = readl(&xgmac->dma_chan->ch0_rx_control);
	printf("  DMA_CH0_Rx_Ctrl   0x%08x  SR=%d\n",
	       v, !!(v & XGMAC_DMA_CH0_RX_CONTROL_SR));
	v = readl(&xgmac->dma_chan->ch0_status);
	printf("  DMA_CH0_Status    0x%08x  TI=%d RI=%d RBU=%d TBU=%d FBE=%d TPS=%d RPS=%d\n",
	       v,
	       !!(v & XGMAC_DMA_CH0_STATUS_TI),
	       !!(v & XGMAC_DMA_CH0_STATUS_RI),
	       !!(v & XGMAC_DMA_CH0_STATUS_RBU),
	       !!(v & XGMAC_DMA_CH0_STATUS_TBU),
	       !!(v & XGMAC_DMA_CH0_STATUS_FBE),
	       !!(v & XGMAC_DMA_CH0_STATUS_TPS),
	       !!(v & XGMAC_DMA_CH0_STATUS_RPS));
	printf("  DMA_CH0_TxTail    0x%08x  RxTail 0x%08x\n",
	       readl(&xgmac->dma_chan->ch0_txdesc_tail_pointer),
	       readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer));

	/*
	 * D179: where the DMA itself thinks it is. The struct stops at
	 * ch0_status (0x60), but 0x44/0x4c/0x54/0x5c inside unused_38[] are
	 * Current_App_TxDesc/RxDesc/TxBuffer/RxBuffer on this core, and they are
	 * the only direct evidence of hardware ring position.
	 *
	 * Pass 42 needs exactly this: RxTail read 0x87684830 in both the
	 * post-start and pre-stop dumps - never advanced, while TxTail did move -
	 * yet RXD4-6 were written back with RXD0-3 showing OWN=1. Either the
	 * recycle in xgmac_free_pkt() ran without its tail write taking effect,
	 * or hardware and software disagree about the current index and
	 * xgmac_recv() is polling a descriptor the DMA is not filling. Cur_RxDesc
	 * separates those two, and software's own rx_desc_idx is printed beside
	 * it so the comparison needs no arithmetic.
	 */
	printf("  DMA_CH0_Cur_TxDesc 0x%08x  Cur_RxDesc 0x%08x\n",
	       readl((u8 *)xgmac->dma_chan + 0x44),
	       readl((u8 *)xgmac->dma_chan + 0x4c));
	printf("  DMA_CH0_Cur_TxBuf  0x%08x  Cur_RxBuf  0x%08x\n",
	       readl((u8 *)xgmac->dma_chan + 0x54),
	       readl((u8 *)xgmac->dma_chan + 0x5c));
	/*
	 * D275: the global DMA block and the MTL read side.
	 *
	 * Everything printed above is per-channel and therefore inside
	 * DMA_MODE.SWR's reset domain, which is exactly the part d242 proved
	 * innocent: six full SWR re-inits in a row and not one further frame
	 * delivered. These are the registers outside that domain, plus the two
	 * fields that say what the engines are *doing* rather than how they are
	 * configured.
	 *
	 * The 0x3008/0x300c/0x3010 offsets are the Synopsys XGMAC
	 * DMA_Interrupt_Status / DMA_Debug_Status0 / DMA_Debug_Status1 slots.
	 * That placement is an inference - no dwxgmac2.h is available for this
	 * core - so they print raw and undecoded. Their value here is the diff
	 * between a healthy post-start dump and a wedged one; a field that
	 * differs is the RX state machine telling us where it parked, whatever
	 * the databook calls it. The addresses are inside the safe
	 * 0x22300000..0x22306fff MAC window.
	 *
	 * mtl_chan + 0x50 is MTL_RxQ0_Missed_Packet_Overflow_Cnt one slot past
	 * rxq0_debug in the relocated block. If frames are being dropped for
	 * want of room rather than held, this is where it shows.
	 */
	/*
	 * D282: [3008] read 0x00020000 in d243's wedge dump and 0 in all three
	 * later ones. If that bit is DMA_Interrupt_Status.MACIS then the MAC
	 * had an interrupt pending at the instant of the wedge and not
	 * afterwards, which is worth naming. MAC_Interrupt_Status/Enable sit at
	 * 0x0b0/0x0b4, inside the unused_0ac[] gap of xgmac_mac_regs and inside
	 * the safe MAC window.
	 */
	printf("  MAC_Int_Status    0x%08x  MAC_Int_Enable 0x%08x\n",
	       readl((u8 *)xgmac->mac_regs + 0xb0),
	       readl((u8 *)xgmac->mac_regs + 0xb4));
	printf("  DMA_Mode          0x%08x  DMA_SysBus_Mode 0x%08x\n",
	       readl(&xgmac->dma_regs->mode),
	       readl(&xgmac->dma_regs->sysbus_mode));
	printf("  DMA_glob raw      [3008]=0x%08x [300c]=0x%08x [3010]=0x%08x (int_sts/dbg0/dbg1, inferred)\n",
	       readl((u8 *)xgmac->dma_regs + 0x08),
	       readl((u8 *)xgmac->dma_regs + 0x0c),
	       readl((u8 *)xgmac->dma_regs + 0x10));
	printf("  MTL_Operation_Mode 0x%08x  MTL_rxq0 raw [4c]=0x%08x [50]=0x%08x (missed/ovf, inferred)\n",
	       readl(&xgmac->mtl_regs->mtl_operation_mode),
	       readl((u8 *)xgmac->mtl_chan + 0x4c),
	       readl((u8 *)xgmac->mtl_chan + 0x50));
	printf("  MAC_RxFlowCtrl    0x%08x  MAC_Q0_TxFlowCtrl 0x%08x\n",
	       readl(&xgmac->mac_regs->rx_flow_ctrl),
	       readl(&xgmac->mac_regs->q0_tx_flow_ctrl));
	printf("  rx frames delivered %u, of which ARP %u\n",
	       xgmac_rx_frames, xgmac_rx_arp_frames);
	printf("  rx this datapath: %u frames, %u full (>=%u), %llu bytes, %u rescued\n",
	       xgmac_rx_frames - xgmac_rx_frames_at_start,
	       xgmac_rx_full_frames - xgmac_rx_full_frames_at_start,
	       XGMAC_RX_FULL_FRAME_MIN,
	       (unsigned long long)(xgmac_rx_bytes - xgmac_rx_bytes_at_start),
	       xgmac_rx_rescued);
	printf("  sw idx: tx_desc_idx %d rx_desc_idx %d  desc_per_cacheline %u desc_size %u rx_stride %u ring %u/%u\n",
	       xgmac->tx_desc_idx, xgmac->rx_desc_idx,
	       xgmac->desc_per_cacheline, xgmac->desc_size,
	       xgmac->rx_stride, xgmac->desc_tx, xgmac->desc_rx);

	/*
	 * MMC - the MAC's own management counters, 0x0800..0x0aff, inside the
	 * documented-safe 0x22300000..0x22306fff window. Pass 48 showed a
	 * failing run with PRXQ=0 and zero frames delivered, i.e. nothing
	 * reached even the MTL queue, which no descriptor-level probe can
	 * explain: descriptors only matter once a frame is inside the MAC. The
	 * MMC counters sit upstream of that and split the remaining
	 * possibilities - frames arriving and being discarded for CRC, length or
	 * filtering will show up here, while a receive path that is genuinely
	 * dark leaves every counter at zero. Printed sparsely (nonzero only)
	 * because most of the block is always zero, and reset in xgmac_start()
	 * so the numbers are per-run rather than cumulative.
	 */
	printf("  MMC nonzero (0x800..0xaff):");
	for (i = 0x800; i < 0xb00; i += 4) {
		v = readl((u8 *)xgmac->mac_regs + i);
		if (!v)
			continue;
		name = xgmac_mmc_name(i);
		if (name)
			printf(" %s=%u", name, v);
		else
			printf(" [%03x]=%u", i, v);
	}
	printf("\n");

	/*
	 * The RX offsets that have been seen to move, raw and unnamed per D217.
	 * Printed even when zero, because zero is the informative reading in a
	 * run where frames were delivered to the stack.
	 */
	printf("  MMC RX raw  [900]=%u [928]=%u [9ac]=%u [9b4]=%u [9c8]=%u [a2c]=%u\n",
	       readl((u8 *)xgmac->mac_regs + 0x900),
	       readl((u8 *)xgmac->mac_regs + 0x928),
	       readl((u8 *)xgmac->mac_regs + 0x9ac),
	       readl((u8 *)xgmac->mac_regs + 0x9b4),
	       readl((u8 *)xgmac->mac_regs + 0x9c8),
	       readl((u8 *)xgmac->mac_regs + 0xa2c));

	if (xgmac->config->ops->xgmac_pcs_dump)
		xgmac->config->ops->xgmac_pcs_dump(dev);

	for (i = 0; i < xgmac->desc_tx; i++) {
		desc = xgmac_get_desc(xgmac, i, false);
		xgmac->config->ops->xgmac_inval_desc(desc);
		printf("  TXD%d %08x %08x %08x %08x  OWN=%d\n", i,
		       desc->des0, desc->des1, desc->des2, desc->des3,
		       !!(desc->des3 & XGMAC_DESC3_OWN));
	}

	for (i = 0; i < xgmac->desc_rx; i++) {
		void *buf = xgmac->rx_dma_buf + i * xgmac->rx_stride;

		desc = xgmac_get_desc(xgmac, i, true);
		xgmac->config->ops->xgmac_inval_desc(desc);
		/*
		 * D281: whether this buffer straddles a 4 KB page, printed
		 * per descriptor so the correlation with the wedge index is
		 * visible in the log instead of needing arithmetic on the
		 * addresses afterwards.
		 */
		printf("  RXD%d %08x %08x %08x %08x  OWN=%d  buf 0x%08x %s\n", i,
		       desc->des0, desc->des1, desc->des2, desc->des3,
		       !!(desc->des3 & XGMAC_DESC3_OWN), (u32)(ulong)buf,
		       (((ulong)buf >> 12) !=
			(((ulong)buf + XGMAC_MAX_PACKET_SIZE - 1) >> 12)) ?
				"CROSSES-4K" : "page-ok");

		/*
		 * des0 is the buffer address only until writeback clobbers it
		 * with status, so it cannot be used to find the buffer of a
		 * descriptor the DMA has already filled - exactly the ones
		 * worth looking at. The address is derived from rx_dma_buf
		 * instead, which is stable for the life of the ring, and the
		 * first 32 bytes are dumped here rather than left to a hand
		 * typed md.b: the ring moves between boots, and a stale
		 * address reads untouched DRAM and prints a convincing page of
		 * zeroes. 32 bytes covers dst/src MAC, ethertype and the start
		 * of the payload, which is all that is needed to tell an ARP
		 * reply from background multicast.
		 */
		/*
		 * Deliberately NOT gated on OWN. Pass 44 dumped nothing at all:
		 * xgmac_free_pkt() had recycled every descriptor back to OWN=1
		 * before the pre-stop dump ran, so an OWN=0 filter hides
		 * precisely the buffers whose contents we came to read. Recycle
		 * rewrites the descriptor, never the buffer - the received frame
		 * is still sitting in DRAM - so dump all eight unconditionally
		 * and let the caller match them against rx_desc_idx.
		 */
		if (!xgmac_env_on("eth_rxdump"))
			continue;

		xgmac->config->ops->xgmac_inval_buffer(buf, 32);
		printf("       ");
		for (j = 0; j < 32; j++)
			printf("%02x%s", ((u8 *)buf)[j], j == 15 ? "  " : " ");
		printf("\n");
	}
}

/*
 * D269: SWR resets MAC_Tx_Configuration, and the speed select lives *above* the
 * shared datapath body - xgmac_adjust_link() -> xgmac_set_gmii_speed() runs in
 * the PHY half of xgmac_start(). d240 measured the consequence: every
 * post-recovery dump read "MAC_Tx_Config 0x00000001 TE=1 SS=0", i.e. the MAC
 * left configured for 10G XGMII on a 1000 Mbps line, after which not one frame
 * was ever received again. Re-apply SS from the negotiated speed here so
 * recovery and start leave the same register value.
 */
static void xgmac_restore_mac_speed(struct xgmac_priv *xgmac)
{
	u32 ss;

	if (!xgmac->phy)
		return;

	switch (xgmac->phy->speed) {
	case SPEED_1000:
		ss = XGMAC_MAC_CONF_SS_1G_GMII;
		break;
	case SPEED_100:
		ss = XGMAC_MAC_CONF_SS_100M_MII;
		break;
	case SPEED_10:
		ss = XGMAC_MAC_CONF_SS_2_10M_MII;
		break;
	default:
		ss = XGMAC_MAC_CONF_SS_10G_XGMII;
		break;
	}

	clrsetbits_le32(&xgmac->mac_regs->tx_configuration,
			0x7 << XGMAC_MAC_CONF_SS_SHIFT,
			ss << XGMAC_MAC_CONF_SS_SHIFT);
}

/*
 * D265: everything from "Configure MTL" down, split out of xgmac_start() so
 * the RX stall recovery can re-run it after a DMA_MODE.SWR. SWR is the only
 * reset measured to empty MTL (D191), and it clears every DMA/MTL/MAC
 * register this function programs, so recovery and start share one body.
 */
static int xgmac_datapath_start(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret, i;
	u32 val, tx_fifo_sz, rx_fifo_sz, tqs, rqs, pbl;
	ulong last_rx_desc;
	ulong desc_pad;
	struct xgmac_desc *tx_desc = NULL;
	struct xgmac_desc *rx_desc = NULL;

	/* D269: SWR wiped the speed select; put it back before TE goes on. */
	xgmac_restore_mac_speed(xgmac);

	/* Configure MTL */

	/* Enable Store and Forward mode for TX */
	/* Program Tx operating mode */
	setbits_le32(&xgmac->mtl_chan->txq0_operation_mode,
		     XGMAC_MTL_TXQ0_OPERATION_MODE_TSF |
		     (XGMAC_MTL_TXQ0_OPERATION_MODE_TXQEN_ENABLED <<
		      XGMAC_MTL_TXQ0_OPERATION_MODE_TXQEN_SHIFT));

	/* Transmit Queue weight */
	writel(0x10, &xgmac->mtl_chan->txq0_quantum_weight);

	/* Enable Store and Forward mode for RX, since no jumbo frame */
	setbits_le32(&xgmac->mtl_chan->rxq0_operation_mode,
		     XGMAC_MTL_RXQ0_OPERATION_MODE_RSF);

	/* Transmit/Receive queue fifo size; use all RAM for 1 queue */
	val = readl(&xgmac->mac_regs->hw_feature1);
	tx_fifo_sz = (val >> XGMAC_MAC_HW_FEATURE1_TXFIFOSIZE_SHIFT) &
		XGMAC_MAC_HW_FEATURE1_TXFIFOSIZE_MASK;
	rx_fifo_sz = (val >> XGMAC_MAC_HW_FEATURE1_RXFIFOSIZE_SHIFT) &
		XGMAC_MAC_HW_FEATURE1_RXFIFOSIZE_MASK;

	/*
	 * r/tx_fifo_sz is encoded as log2(n / 128). Undo that by shifting.
	 * r/tqs is encoded as (n / 256) - 1.
	 */
	tqs = (128 << tx_fifo_sz) / 256 - 1;
	rqs = (128 << rx_fifo_sz) / 256 - 1;

	clrsetbits_le32(&xgmac->mtl_chan->txq0_operation_mode,
			XGMAC_MTL_TXQ0_OPERATION_MODE_TQS_MASK <<
			XGMAC_MTL_TXQ0_OPERATION_MODE_TQS_SHIFT,
			tqs << XGMAC_MTL_TXQ0_OPERATION_MODE_TQS_SHIFT);
	clrsetbits_le32(&xgmac->mtl_chan->rxq0_operation_mode,
			XGMAC_MTL_RXQ0_OPERATION_MODE_RQS_MASK <<
			XGMAC_MTL_RXQ0_OPERATION_MODE_RQS_SHIFT,
			rqs << XGMAC_MTL_RXQ0_OPERATION_MODE_RQS_SHIFT);

	/* D373: EHFC is what makes the MTL assert pause on its own. */
	if (xgmac_env_on("eth_txpause"))
		setbits_le32(&xgmac->mtl_chan->rxq0_operation_mode,
			     XGMAC_MTL_RXQ0_OPERATION_MODE_EHFC);
	else
		clrbits_le32(&xgmac->mtl_chan->rxq0_operation_mode,
			     XGMAC_MTL_RXQ0_OPERATION_MODE_EHFC);

	/* Configure MAC */
	clrsetbits_le32(&xgmac->mac_regs->rxq_ctrl0,
			XGMAC_MAC_RXQ_CTRL0_RXQ0EN_MASK <<
			XGMAC_MAC_RXQ_CTRL0_RXQ0EN_SHIFT,
			xgmac->config->config_mac <<
			XGMAC_MAC_RXQ_CTRL0_RXQ0EN_SHIFT);

	/* Multicast and Broadcast Queue Enable */
	setbits_le32(&xgmac->mac_regs->rxq_ctrl1,
		     XGMAC_MAC_RXQ_CTRL1_MCBCQEN);

	/*
	 * Upstream enables promiscuous (PR) and receive-all (RA)
	 * unconditionally, which means every multicast frame on the segment is
	 * accepted into the MTL receive queue. On this board's lab segment that
	 * is a continuous stream - SSDP, LLDP, IPv6 multicast (pass 47's buffer
	 * dumps were nothing else) - and the driver drains far slower than it
	 * arrives: a 7-deep ring, recycled only in batches of four, parking on
	 * RBU. Pass 49 measured the result, PRXQ climbing to 58 while just
	 * twelve frames reached the stack in four seconds. A queue in that state
	 * drops new arrivals, and the ARP reply is a new arrival 37 us after our
	 * request.
	 *
	 * "setenv eth_promisc 0" turns both bits off, leaving ordinary address
	 * filtering: our own unicast (MAC_Addr0, which xgmac_write_hwaddr() has
	 * already programmed from dev->enetaddr) plus broadcast, since DB is not
	 * set. That is everything U-Boot's stack needs for ARP, DHCP and TFTP,
	 * and it discards the flood at the MAC instead of queueing it.
	 *
	 * Left on by default so this stays a test rather than a silent change of
	 * behaviour; if it proves out it should become the default, because
	 * receive-all is not something a bootloader needs.
	 */
	if (xgmac_env_off("eth_promisc")) {
		clrbits_le32(&xgmac->mac_regs->mac_packet_filter,
			     XGMAC_MAC_PACKET_FILTER_RA |
			     XGMAC_MAC_PACKET_FILTER_PR);
		debug("xgmac: promiscuous/receive-all OFF, filtering to own unicast + broadcast\n");
	} else {
		setbits_le32(&xgmac->mac_regs->mac_packet_filter,
			     XGMAC_MAC_PACKET_FILTER_RA |
			     XGMAC_MAC_PACKET_FILTER_PR);
	}

	/*
	 * D373: TX pause is opt-in now, and PT is no longer slammed to its
	 * mask.
	 *
	 * PT_MASK is GENMASK(15, 0), so the code below asked for 65535 pause
	 * quanta. A quantum is 512 bit times, which at 1 Gbit/s is 512 ns, so
	 * one pause frame told the link partner to stop transmitting for
	 * 33.55 ms. EHFC below lets the MTL assert that automatically, and
	 * RFD/RFA are never programmed, so the activation threshold stays at
	 * its power-on value - the most eager one available.
	 *
	 * A bootloader has no use for this. U-Boot's stack has one frame in
	 * flight at a time and CONFIG_SYS_RX_ETH_BUFFER is 4; there is no
	 * sustained receive load for pause to protect. What it can do is cost
	 * 33.55 ms per assertion, and a tftp run that measured 21.5 KiB/s came
	 * out at 66.7 ms per block - 1.99 pause quanta - with 89/89 rescues,
	 * zero TX timeouts and no retransmissions, i.e. a datapath working
	 * perfectly and merely being held off.
	 *
	 * Cleared rather than skipped: q0_tx_flow_ctrl survives an
	 * eth_halt()/eth_start() the same way the wrapper's FUNC_CLK_EN does,
	 * so leaving the write out would inherit the previous command's value.
	 * "setenv eth_txpause 1" restores upstream behaviour for an A/B in a
	 * single boot.
	 */
	if (xgmac_env_on("eth_txpause")) {
		setbits_le32(&xgmac->mac_regs->q0_tx_flow_ctrl,
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_MASK <<
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_SHIFT);
	} else {
		clrbits_le32(&xgmac->mac_regs->q0_tx_flow_ctrl,
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_MASK <<
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_SHIFT);
	}

	/* Assign priority for RX flow control */
	clrbits_le32(&xgmac->mac_regs->rxq_ctrl2,
		     XGMAC_MAC_RXQ_CTRL2_PSRQ0_MASK <<
		     XGMAC_MAC_RXQ_CTRL2_PSRQ0_SHIFT);

	/*
	 * D373: RFE stays on - honouring a pause from the far end is free,
	 * our transmit side is a handful of ACKs. TFE is what emits them.
	 */
	if (xgmac_env_on("eth_txpause")) {
		setbits_le32(&xgmac->mac_regs->q0_tx_flow_ctrl,
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_TFE);
		debug("xgmac: TX pause ENABLED, PT = max (33.55 ms/frame at 1G)\n");
	} else {
		clrbits_le32(&xgmac->mac_regs->q0_tx_flow_ctrl,
			     XGMAC_MAC_Q0_TX_FLOW_CTRL_TFE);
	}
	setbits_le32(&xgmac->mac_regs->rx_flow_ctrl,
		     XGMAC_MAC_RX_FLOW_CTRL_RFE);

	clrbits_le32(&xgmac->mac_regs->tx_configuration,
		     XGMAC_MAC_CONF_JD);

	clrbits_le32(&xgmac->mac_regs->rx_configuration,
		     XGMAC_MAC_CONF_JE |
		     XGMAC_MAC_CONF_GPSLCE |
		     XGMAC_MAC_CONF_WD);

	setbits_le32(&xgmac->mac_regs->rx_configuration,
		     XGMAC_MAC_CONF_ACS |
		     XGMAC_MAC_CONF_CST);

	ret = xgmac_write_hwaddr(dev);
	if (ret < 0) {
		pr_err("%s xgmac_write_hwaddr() failed: %d\n", dev->name, ret);
		return ret;
	}

	/* Configure DMA */
	clrsetbits_le32(&xgmac->dma_regs->sysbus_mode,
			XGMAC_DMA_SYSBUS_MODE_AAL,
			XGMAC_DMA_SYSBUS_MODE_EAME |
			XGMAC_DMA_SYSBUS_MODE_UNDEF);

	/* Enable OSP mode */
	setbits_le32(&xgmac->dma_chan->ch0_tx_control,
		     XGMAC_DMA_CH0_TX_CONTROL_OSP);

	/* RX buffer size. Must be a multiple of bus width */
	clrsetbits_le32(&xgmac->dma_chan->ch0_rx_control,
			XGMAC_DMA_CH0_RX_CONTROL_RBSZ_MASK <<
			XGMAC_DMA_CH0_RX_CONTROL_RBSZ_SHIFT,
			XGMAC_MAX_PACKET_SIZE <<
			XGMAC_DMA_CH0_RX_CONTROL_RBSZ_SHIFT);

	/*
	 * D272: DSL counts 16-byte units on this core, not axi_bus_width.
	 *
	 * d241 set desc_size to 64 and let upstream's formula pick
	 * DSL = (64 - 16) / axi_bus_width = (64 - 16) / 8 = 6. The hardware
	 * then reported Cur_RxDesc = rxlist + 112, i.e. it placed its second
	 * descriptor 112 bytes in: 16 + 6 * 16. So the skip unit it applied is
	 * 16 bytes, and DSL = 6 gave a 112-byte stride against the driver's
	 * 64-byte layout. The DMA read address rxlist+112, which lands in the
	 * zero padding of the driver's RXD1, saw OWN=0 and stopped - MTL filled
	 * to PRXQ=11 while every descriptor read OWN=1 in the driver's own
	 * layout. That is d241's ping failure, and it is mine.
	 *
	 * DSL = (64 - 16) / 16 = 3 gives the 64-byte stride the driver uses.
	 * The bug was invisible before d241 because desc_size 16 makes DSL 0
	 * under either unit. "setenv eth_dsl8" restores the axi_bus_width
	 * formula if this measurement is ever contradicted.
	 */
	if (xgmac_env_on("eth_dsl8"))
		desc_pad = (xgmac->desc_size - sizeof(struct xgmac_desc)) /
			   xgmac->config->axi_bus_width;
	else
		desc_pad = (xgmac->desc_size - sizeof(struct xgmac_desc)) /
			   XGMAC_DSL_UNIT;

	setbits_le32(&xgmac->dma_chan->ch0_control,
		     XGMAC_DMA_CH0_CONTROL_PBLX8 |
		     (desc_pad << XGMAC_DMA_CH0_CONTROL_DSL_SHIFT));

	if (xgmac_env_on("eth_regdump"))
		printf("xgmac: desc stride %u bytes: DSL=%lu x %u + %u, so the DMA must find RXD1 at 0x%08lx\n",
		       xgmac->desc_size, desc_pad,
		       xgmac_env_on("eth_dsl8") ?
				xgmac->config->axi_bus_width : XGMAC_DSL_UNIT,
		       (unsigned int)sizeof(struct xgmac_desc),
		       (ulong)xgmac_get_desc(xgmac, 1, true));

	/*
	 * Burst length must be < 1/2 FIFO size.
	 * FIFO size in tqs is encoded as (n / 256) - 1.
	 * Each burst is n * 8 (PBLX8) * 16 (AXI width) == 128 bytes.
	 * Half of n * 256 is n * 128, so pbl == tqs, modulo the -1.
	 */
	pbl = tqs + 1;
	if (pbl > 32)
		pbl = 32;

	clrsetbits_le32(&xgmac->dma_chan->ch0_tx_control,
			XGMAC_DMA_CH0_TX_CONTROL_TXPBL_MASK <<
			XGMAC_DMA_CH0_TX_CONTROL_TXPBL_SHIFT,
			pbl << XGMAC_DMA_CH0_TX_CONTROL_TXPBL_SHIFT);

	clrsetbits_le32(&xgmac->dma_chan->ch0_rx_control,
			XGMAC_DMA_CH0_RX_CONTROL_RXPBL_MASK <<
			XGMAC_DMA_CH0_RX_CONTROL_RXPBL_SHIFT,
			8 << XGMAC_DMA_CH0_RX_CONTROL_RXPBL_SHIFT);

	/*
	 * D285: the AXI master configuration, taken from the vendor DT rather
	 * than from upstream's "maximise everything".
	 *
	 * Upstream programs the OSR limit fields to their mask (31 outstanding
	 * reads and 31 outstanding writes) and enables the fixed burst lengths
	 * 4/8/16/32 with UNDEF clear, giving DMA_SysBus_Mode 0x1f1f081e. That
	 * is what every image up to d244 ran. The vendor DT for this exact
	 * SoC (echo-eth.dtsi, emac0_stmmac-axi-config) asks for something
	 * quite different:
	 *
	 *	snps,wr_osr_lmt = <15>;
	 *	snps,rd_osr_lmt = <15>;
	 *	snps,blen = <0 0 0 0 0 0 0>;
	 *	(no snps,fixed-burst)
	 *
	 * with the comment "If FB is disabled, the AXI master chooses a burst
	 * length of any value less than the maximum enabled burst length (all
	 * lesser burst length enables are redundant)". stmmac's
	 * dwxgmac2_dma_axi() turns the missing snps,fixed-burst into UNDEF=1
	 * and the all-zero blen into no BLEN bits at all, so Linux runs this
	 * core with 0x0f0f0801: half the outstanding requests we allow, and
	 * undefined burst lengths instead of four fixed ones.
	 *
	 * Why this is the suspect. The wedge is a write that stops part-way
	 * through a frame - 816 bytes into RXD2's buffer in d243, 432 bytes in
	 * d244 - with the descriptor still owned by the DMA, no FBE, no RBU,
	 * no RPS, and MTL's read controller stuck at RRCSTS=1 "reading packet
	 * data" for good. Nothing about the buffer explains it: it survives
	 * desc_size 16 and 64, desc_per_cacheline 4 and 1, a 1600-byte and a
	 * 2048-byte stride, and buffers that provably never cross a 4 KB page
	 * (D279, D281). What it is not indifferent to is timing. The counter
	 * resets in xgmac_start(), so "delivered 2" is two frames per net
	 * command, not two per boot - and the first two frames of any command
	 * are always software-paced (the ARP reply, then whatever the first
	 * transmit draws out), while the third is the first frame that can
	 * arrive while the DMA still has the previous frame's writes and two
	 * descriptor writebacks in flight. An outstanding-request limit the
	 * interconnect cannot actually honour would look exactly like this,
	 * and would hang the burst rather than error it, which is why SWR
	 * never recovers it (D277, D283).
	 *
	 * "setenv eth_axi31 1" restores the upstream/d244 value in one
	 * command, per D259.
	 */
	/*
	 * D288: two knobs to attribute d245's win, since the vendor value
	 * changed two things at once. "setenv eth_axiosr 1" puts the OSR limits
	 * back to 31 and keeps UNDEF; "setenv eth_axiblen 1" puts the fixed
	 * burst lengths back and keeps OSR 15. Whichever one brings back the
	 * two-frame wedge is the one that mattered, and that is what the merge
	 * commit needs to say.
	 */
	if (xgmac_env_on("eth_axi31"))
		val = (XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_MASK <<
		       XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT) |
		      (XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_MASK <<
		       XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_SHIFT) |
		      XGMAC_DMA_SYSBUS_MODE_EAME |
		      XGMAC_DMA_SYSBUS_MODE_BLEN32 |
		      XGMAC_DMA_SYSBUS_MODE_BLEN16 |
		      XGMAC_DMA_SYSBUS_MODE_BLEN8 |
		      XGMAC_DMA_SYSBUS_MODE_BLEN4;
	else
		val = (XGMAC_DMA_SYSBUS_MODE_OSR_LMT_VENDOR <<
		       XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT) |
		      (XGMAC_DMA_SYSBUS_MODE_OSR_LMT_VENDOR <<
		       XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_SHIFT) |
		      XGMAC_DMA_SYSBUS_MODE_EAME |
		      XGMAC_DMA_SYSBUS_MODE_UNDEF;

	if (xgmac_env_on("eth_axiosr")) {
		val &= ~((XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_MASK <<
			  XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT) |
			 (XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_MASK <<
			  XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_SHIFT));
		val |= (XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_MASK <<
			XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT) |
		       (XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_MASK <<
			XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_SHIFT);
	}

	if (xgmac_env_on("eth_axiblen")) {
		val &= ~XGMAC_DMA_SYSBUS_MODE_UNDEF;
		val |= XGMAC_DMA_SYSBUS_MODE_BLEN32 |
		       XGMAC_DMA_SYSBUS_MODE_BLEN16 |
		       XGMAC_DMA_SYSBUS_MODE_BLEN8 |
		       XGMAC_DMA_SYSBUS_MODE_BLEN4;
	}

	/*
	 * D281, the second half of the same suspicion: AAL. With AAL clear the
	 * DMA starts its bursts at whatever address the buffer happens to
	 * begin at; with AAL set it aligns beats to the bus width. If the
	 * page-contained buffers alone do not fix the wedge, this is the other
	 * half of the same question and can be tried without a reflash:
	 * "setenv eth_aal 1". Kept opt-in because upstream does not set it and
	 * one change per image is the rule (D259).
	 */
	if (xgmac_env_on("eth_aal"))
		val |= XGMAC_DMA_SYSBUS_MODE_AAL;

	writel(val, &xgmac->dma_regs->sysbus_mode);

	/* Set up descriptors */

	memset(xgmac->tx_descs, 0, xgmac->desc_size * XGMAC_DESCRIPTORS_MAX);
	memset(xgmac->rx_descs, 0, xgmac->desc_size * XGMAC_DESCRIPTORS_MAX);

	for (i = 0; i < xgmac->desc_tx; i++) {
		tx_desc = (struct xgmac_desc *)xgmac_get_desc(xgmac, i, false);

		xgmac->config->ops->xgmac_flush_desc(tx_desc);
	}

	for (i = 0; i < xgmac->desc_rx; i++) {
		rx_desc = (struct xgmac_desc *)xgmac_get_desc(xgmac, i, true);

		/*
		 * des0/des1 are the low/high halves of the 64-bit buffer
		 * address. des1 was previously left unwritten here and zeroed
		 * at the other three sites, which truncates any buffer above
		 * 4 GiB exactly as the ring address was truncated above.
		 */
		rx_desc->des0 = lower_32_bits((ulong)(xgmac->rx_dma_buf +
						      (i * xgmac->rx_stride)));
		rx_desc->des1 = upper_32_bits((ulong)(xgmac->rx_dma_buf +
						      (i * xgmac->rx_stride)));
		rx_desc->des3 = XGMAC_DESC3_OWN;
		/* Flush the cache to the memory */
		mb();
		xgmac->config->ops->xgmac_flush_desc(rx_desc);
		xgmac->config->ops->xgmac_inval_buffer(xgmac->rx_dma_buf +
						       (i * xgmac->rx_stride),
						       XGMAC_MAX_PACKET_SIZE);
	}

	/*
	 * The descriptor list address is a 64-bit register split across a
	 * _haddress (upper 32) and _address (lower 32) pair. Both halves must be
	 * programmed: hardcoding _haddress to 0 silently truncates any ring
	 * allocated above 4 GiB, and the resulting address is very likely still
	 * mapped, so the DMA engine polls unrelated memory forever instead of
	 * faulting.
	 *
	 * Measured on Amboseli (SDX95), which has DRAM at 0x80000000+2G and
	 * 0x880000000+6G: U-Boot relocates to the top of the high bank, so the
	 * ring landed at 0x8efdaa0c0 and the MAC was told 0xefdaa0c0 - 36 GiB
	 * away. des3's OWN bit was therefore never written back, ZERO frames
	 * moved in either direction, and no error was reported anywhere.
	 * Clamping ram_top below 4 GiB took RX from 0 frames to 11.
	 */
	writel(upper_32_bits((ulong)xgmac_get_desc(xgmac, 0, false)),
	       &xgmac->dma_chan->ch0_txdesc_list_haddress);
	writel(lower_32_bits((ulong)xgmac_get_desc(xgmac, 0, false)),
	       &xgmac->dma_chan->ch0_txdesc_list_address);
	writel(xgmac->desc_tx - 1,
	       &xgmac->dma_chan->ch0_txdesc_ring_length);
	writel(upper_32_bits((ulong)xgmac_get_desc(xgmac, 0, true)),
	       &xgmac->dma_chan->ch0_rxdesc_list_haddress);
	writel(lower_32_bits((ulong)xgmac_get_desc(xgmac, 0, true)),
	       &xgmac->dma_chan->ch0_rxdesc_list_address);
	writel(xgmac->desc_rx - 1,
	       &xgmac->dma_chan->ch0_rxdesc_ring_length);

	/*
	 * The channel block base comes from the vendor's dwxgmac_addrs, but the
	 * layout inside the block is inferred from the upstream structs. Read
	 * the descriptor registers back so a wrong intra-block offset is
	 * visible in the log instead of turning into a silent DMA hang.
	 */
	if (xgmac->chan_relocated && xgmac_env_on("eth_regdump")) {
		u32 rb_tx = readl(&xgmac->dma_chan->ch0_txdesc_list_address);
		u32 rb_rx = readl(&xgmac->dma_chan->ch0_rxdesc_list_address);
		u32 rb_tl = readl(&xgmac->dma_chan->ch0_txdesc_ring_length);
		u32 rb_rl = readl(&xgmac->dma_chan->ch0_rxdesc_ring_length);

		printf("xgmac: dma_chan +0x%x readback: txlist 0x%08x (want 0x%08lx) rxlist 0x%08x (want 0x%08lx) txlen 0x%x rxlen 0x%x (want 0x%x/0x%x)\n",
		       xgmac->config->dma_chan_base,
		       rb_tx, (ulong)xgmac->tx_descs,
		       rb_rx, (ulong)xgmac->rx_descs,
		       rb_tl, rb_rl,
		       xgmac->desc_tx - 1, xgmac->desc_rx - 1);
	}

	/* Enable everything */
	setbits_le32(&xgmac->dma_chan->ch0_tx_control,
		     XGMAC_DMA_CH0_TX_CONTROL_ST);
	setbits_le32(&xgmac->dma_chan->ch0_rx_control,
		     XGMAC_DMA_CH0_RX_CONTROL_SR);
	setbits_le32(&xgmac->mac_regs->tx_configuration,
		     XGMAC_MAC_CONF_TE);
	setbits_le32(&xgmac->mac_regs->rx_configuration,
		     XGMAC_MAC_CONF_RE);

	/*
	 * D260: take the MAC out of every LPI mode.
	 *
	 * The two failing pings of pass 62 and 63 are exactly the two where the
	 * link partner advertised EEE (`partner 7.61 0x0006`); every ping where
	 * it advertised 0x0000 passed, and pass 59's ten-for-ten was run with
	 * the host's EEE switched off. Clearing our own 7.60 advertisement, as
	 * xgmac_phy_disable_eee() does, stops EEE being *negotiated* but does
	 * nothing about the MAC's own LPI machinery: LPIEN/LPITXA/LPIATE let it
	 * enter LPI on its own, and RLPIST latches when the partner sends LPI.
	 * A MAC sitting in RX LPI state with a frame in MTL and every descriptor
	 * armed is exactly what the failing dump shows (PRXQ=1, RRCSTS=1,
	 * nothing written back). Write the register to zero so no LPI mode is
	 * armed, and print what it held. "setenv eth_lpi 1" leaves it alone.
	 */
	if (!xgmac_env_on("eth_lpi")) {
		u32 lpi = readl(&xgmac->mac_regs->lpi_control_status);

		if (lpi & (XGMAC_MAC_LPI_CS_LPIEN | XGMAC_MAC_LPI_CS_LPITXA |
			   XGMAC_MAC_LPI_CS_LPIATE | XGMAC_MAC_LPI_CS_RLPIST |
			   XGMAC_MAC_LPI_CS_TLPIST) ||
		    xgmac_env_on("eth_regdump"))
			printf("xgmac: MAC_LPI_Ctrl_Sts was 0x%08x (RLPIST=%d LPIEN=%d), clearing\n",
			       lpi, !!(lpi & XGMAC_MAC_LPI_CS_RLPIST),
			       !!(lpi & XGMAC_MAC_LPI_CS_LPIEN));

		writel(0, &xgmac->mac_regs->lpi_control_status);
	}

	/* MMC_Control.CNTRST, self-clearing: make the counters per-run. */
	if (xgmac_env_on("eth_regdump"))
		writel(XGMAC_MMC_CONTROL_CNTRST,
		       (u8 *)xgmac->mac_regs + XGMAC_MMC_CONTROL);

	/*
	 * "setenv eth_txprobe 1": a write/read-back ladder on
	 * MAC_Tx_Configuration. Every setbits_le32() on this register is a
	 * read-modify-write, so if the read half is broken the driver has been
	 * writing garbage back into it all along - including over the TE bit.
	 * Three patterns settle it: if the readbacks track the writes the
	 * register is fine and something later clobbers it; if they do not,
	 * offset 0x000 is not being decoded and every RMW on it is corrupt.
	 */
	if (xgmac_env_on("eth_txprobe")) {
		u32 want = (XGMAC_MAC_CONF_SS_1G_GMII <<
			    XGMAC_MAC_CONF_SS_SHIFT) | XGMAC_MAC_CONF_TE;
		u32 r0, r1, r2, r3;

		r0 = readl(&xgmac->mac_regs->tx_configuration);
		writel(want, &xgmac->mac_regs->tx_configuration);
		mb();
		r1 = readl(&xgmac->mac_regs->tx_configuration);
		writel(0, &xgmac->mac_regs->tx_configuration);
		mb();
		r2 = readl(&xgmac->mac_regs->tx_configuration);
		writel(want, &xgmac->mac_regs->tx_configuration);
		mb();
		r3 = readl(&xgmac->mac_regs->tx_configuration);

		printf("%s txprobe: as-found 0x%08x, wrote 0x%08x -> 0x%08x, wrote 0x00000000 -> 0x%08x, wrote 0x%08x -> 0x%08x\n",
		       dev->name, r0, want, r1, r2, want, r3);
	}

	/*
	 * "setenv eth_forcetx 1": stop trusting the read-modify-writes and
	 * plant 1G + TE with a single writel(). If TX starts working only with
	 * this set, the fault is in the RMW path, not in the MAC.
	 */
	if (env_get("eth_forcetx")) {
		u32 want = (XGMAC_MAC_CONF_SS_1G_GMII <<
			    XGMAC_MAC_CONF_SS_SHIFT) | XGMAC_MAC_CONF_TE;

		writel(want, &xgmac->mac_regs->tx_configuration);
		mb();
		printf("%s forcetx: MAC_Tx_Config <- 0x%08x, reads 0x%08x\n",
		       dev->name, want,
		       readl(&xgmac->mac_regs->tx_configuration));
	}

	/* TX tail pointer not written until we need to TX a packet */
	/*
	 * Point RX tail pointer at last descriptor. Ideally, we'd point at the
	 * first descriptor, implying all descriptors were available. However,
	 * that's not distinguishable from none of the descriptors being
	 * available.
	 */
	last_rx_desc = xgmac_rx_upstream() ?
		       (ulong)xgmac_get_desc(xgmac, xgmac->desc_rx - 1,
					     true) :
		       xgmac_rx_tail_addr(xgmac, xgmac->desc_rx);
	writel(last_rx_desc, &xgmac->dma_chan->ch0_rxdesc_tail_pointer);
	if (xgmac_env_on("eth_regdump"))
		printf("xgmac: RxTail <- 0x%08lx (%s convention), reads 0x%08x, Cur_RxDesc 0x%08x\n",
		       last_rx_desc,
		       xgmac_rx_upstream() ? "upstream &RXD[N-1]" :
					     "one past ring end",
		       readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer),
		       readl((u8 *)xgmac->dma_chan + 0x4c));

	/*
	 * Clear whatever latched during the resets so the status register means
	 * "since the engine started" and not "since power-on". W1C, so writing
	 * back what was read clears exactly the set bits.
	 */
	writel(readl(&xgmac->dma_chan->ch0_status),
	       &xgmac->dma_chan->ch0_status);

	xgmac->started = true;

	/*
	 * D275: during a recovery the interesting dump is the one taken before
	 * the SWR, not after it - CNTRST in this function has just zeroed the
	 * MMC counters and the re-arm has just rewritten the whole ring, so a
	 * post-SWR dump destroys the evidence it appears to be reporting. d242
	 * printed six of them and every one described a freshly initialised
	 * channel. "setenv eth_rxpostdump 1" brings them back.
	 */
	/* D288: everything below counts from here, recovery included. */
	xgmac_rx_frames_at_start = xgmac_rx_frames;
	/* D309: same convention for the byte and full-frame counters. */
	xgmac_rx_bytes_at_start = xgmac_rx_bytes;
	xgmac_rx_full_frames_at_start = xgmac_rx_full_frames;

	if (!xgmac_recovering || xgmac_env_on("eth_rxpostdump"))
		xgmac_dump_datapath(dev, "post-start");

	debug("%s: OK\n", __func__);
	return 0;
}

/*
 * D301: the descriptor and buffer layout, chosen once per network command.
 *
 * Both knobs used to be read at probe and were therefore unreachable from the
 * prompt (see xgmac_probe_resources_core()). The allocations are sized for the
 * maximum, so switching layout here only changes how much of them is used and
 * how the DMA is told to step through them - which is exactly what the
 * experiment needs to vary.
 */
static void xgmac_select_layout(struct xgmac_priv *xgmac)
{
	unsigned int desc_step = sizeof(struct xgmac_desc);
	unsigned int desc_size, rx_stride, ring, dpc;

	/*
	 * D270: 64 bytes per descriptor is the default from d241 on.
	 * DMA_CH0_Control.DSL lets the hardware skip between descriptors, so
	 * one descriptor per 64-byte cacheline is reachable. A descriptor then
	 * cannot share a cacheline with three neighbours, so flushing one can
	 * never write a stale cached copy of another back over a DMA
	 * writeback, and desc_per_cacheline becomes 1, so xgmac_free_pkt()
	 * recycles every packet instead of every fourth.
	 *
	 * "setenv eth_desc64 0" restores upstream's packed 16-byte layout,
	 * which also makes DSL 0 and the ring 128 bytes instead of 512.
	 */
	if (desc_step < ARCH_DMA_MINALIGN && xgmac_env_off("eth_desc64"))
		desc_size = BIT(fls(desc_step) - 1);
	else
		desc_size = ALIGN(desc_step, (unsigned int)ARCH_DMA_MINALIGN);

	/*
	 * D281: page-contained RX buffers, refuted as the wedge but kept as a
	 * revert. "setenv eth_buf1600 1" packs the buffers at 1600 bytes.
	 */
	rx_stride = xgmac_env_on("eth_buf1600") ? XGMAC_MAX_PACKET_SIZE
						: XGMAC_RX_STRIDE_ALIGNED;

	/*
	 * T-68: the last discriminator for the 64-frame stall period. Every
	 * candidate that scales with bytes, alignment, stride or recycle
	 * timing is refuted (D303, D305, D306), which leaves two models that
	 * are numerically identical at the eight-entry ring we have always
	 * used: an absolute count of 64, or eight traversals of the ring.
	 *
	 *   "setenv eth_ring 4"   traversals -> N=31,  count -> N=63
	 *   "setenv eth_ring 8"   both       -> N=63   (today's behaviour)
	 *   "setenv eth_ring 16"  traversals -> N=127, count -> N=63
	 *
	 * Descriptor memory and the RX buffer arena are allocated for
	 * XGMAC_DESCRIPTORS_MAX in probe, so this only ever selects a prefix
	 * of what already exists - the D301 pattern, and the reason this is
	 * settable from the prompt at all.
	 *
	 * Two constraints on the value. It must divide into desc_per_cacheline
	 * evenly, because xgmac_free_pkt() refills a whole cacheline's worth of
	 * descriptors when the last index in the line is freed and a ring that
	 * ends mid-line would never trigger that batch. And it must be at
	 * least 4, because three of the eight ring slots are in flight during
	 * ordinary operation and anything smaller starves rather than
	 * measures. Out-of-range values fall back to the default with a
	 * complaint rather than silently measuring something else.
	 */
	ring = (unsigned int)env_get_ulong("eth_ring", 10,
					   XGMAC_DESCRIPTORS_RX);
	dpc = ARCH_DMA_MINALIGN / desc_size;
	if (ring < 4 || ring > XGMAC_DESCRIPTORS_MAX || (ring % dpc)) {
		printf("xgmac: eth_ring %u rejected (need 4..%u, a multiple of %u for desc_size %u) - using %u\n",
		       ring, XGMAC_DESCRIPTORS_MAX, dpc, desc_size,
		       XGMAC_DESCRIPTORS_RX);
		ring = XGMAC_DESCRIPTORS_RX;
	}

	if ((desc_size != xgmac->desc_size || rx_stride != xgmac->rx_stride ||
	     ring != xgmac->desc_rx) && xgmac_env_on("eth_regdump"))
		printf("xgmac: layout: desc_size %u -> %u, rx_stride %u -> %u, ring %u -> %u\n",
		       xgmac->desc_size, desc_size,
		       xgmac->rx_stride, rx_stride,
		       xgmac->desc_rx, ring);

	xgmac->desc_size = desc_size;
	xgmac->desc_per_cacheline = ARCH_DMA_MINALIGN / xgmac->desc_size;
	xgmac->rx_stride = rx_stride;
	xgmac->desc_tx = ring;
	xgmac->desc_rx = ring;
}

static int xgmac_start(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;
	int addr = -1;

	/* A previous command may have latched a keypress abort (D231). */
	xgmac_mdio_aborted = false;

	debug("%s(dev=%p):\n", __func__, dev);

	xgmac->tx_desc_idx = 0;
	xgmac->rx_desc_idx = 0;
	xgmac_rx_frames = 0;
	xgmac_rx_rescued = 0;
	xgmac_rescue_len = 0;
	xgmac_rx_arp_frames = 0;
	xgmac_rx_bytes = 0;
	xgmac_rx_full_frames = 0;

	xgmac_select_layout(xgmac);
	xgmac_select_chan_regs(xgmac);

	ret = xgmac->config->ops->xgmac_start_resets(dev);
	if (ret < 0) {
		pr_err("%s xgmac_start_resets() failed: %d\n", dev->name, ret);
		goto err;
	}

	xgmac->reg_access_ok = true;

	/*
	 * Match the working CE sequence: reset the DMA immediately after the
	 * EMAC resources are enabled, before MDIO/PHY setup.  On this USXGMII
	 * integration the reset also clears stale MTL state and establishes the
	 * state expected by the descriptor engine.
	 */
	{
		u32 before = readl(&xgmac->mtl_chan->rxq0_debug);
		u32 prxq = (before >> XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
			   XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK;

		if (prxq || xgmac_env_on("eth_regdump"))
			printf("xgmac: MTL_RxQ0_Debug before DMA reset 0x%08x (PRXQ=%u)\n",
			       before, prxq);
	}

	setbits_le32(&xgmac->dma_regs->mode, XGMAC_DMA_MODE_SWR);
	ret = wait_for_bit_le32(&xgmac->dma_regs->mode,
				XGMAC_DMA_MODE_SWR, false,
				xgmac->config->swr_wait, false);
	if (ret) {
		pr_err("%s XGMAC_DMA_MODE_SWR stuck: %d\n", dev->name, ret);
		goto err_stop_resets;
	}

	ret = xgmac->config->ops->xgmac_calibrate_pads(dev);
	if (ret < 0) {
		pr_err("%s xgmac_calibrate_pads() failed: %d\n", dev->name, ret);
		goto err_stop_resets;
	}

	/*
	 * if PHY was already connected and configured,
	 * don't need to reconnect/reconfigure again
	 */
	if (!xgmac->phy) {
		addr = xgmac_get_phy_addr(xgmac, dev);
		debug("%s: PHY DT address=%d, MDIO ID probe begin\n",
		      dev->name, addr);
		for (int probe_addr = 0; probe_addr < 2; probe_addr++) {
			int c22_id1 = xgmac->mii->read(xgmac->mii, probe_addr,
						       MDIO_DEVAD_NONE, MII_PHYSID1);
			int c22_id2 = xgmac->mii->read(xgmac->mii, probe_addr,
						       MDIO_DEVAD_NONE, MII_PHYSID2);
			int c45_id1 = xgmac->mii->read(xgmac->mii, probe_addr,
						       MDIO_MMD_PMAPMD, MII_PHYSID1);
			int c45_id2 = xgmac->mii->read(xgmac->mii, probe_addr,
						       MDIO_MMD_PMAPMD, MII_PHYSID2);

			debug("%s: MDIO addr=%d c22=%04x:%04x c45=%04x:%04x\n",
			      dev->name, probe_addr, c22_id1, c22_id2,
			      c45_id1, c45_id2);
		}
		xgmac->phy = phy_connect(xgmac->mii, addr, dev,
					 xgmac->config->interface(dev));
		if (!xgmac->phy) {
			pr_err("%s phy_connect() failed\n", dev->name);
			goto err_stop_resets;
		}

		if (xgmac->config->phy_force_c45) {
			ret = phy_force_clause45(xgmac->phy);
			if (ret) {
				pr_err("%s phy_force_clause45() failed: %d\n",
				       dev->name, ret);
				goto err_shutdown_phy;
			}
			debug("%s: PHY rebound as clause-45 (%s)\n",
			      dev->name, xgmac->phy->drv->name);
		}

		if (xgmac->max_speed) {
			ret = phy_set_supported(xgmac->phy, xgmac->max_speed);
			if (ret) {
				pr_err("%s phy_set_supported() failed: %d\n",
				       dev->name, ret);

				goto err_shutdown_phy;
			}
		}

		xgmac->phy->node = xgmac->phy_of_node;
		ret = phy_config(xgmac->phy);
		if (ret < 0) {
			pr_err("%s phy_config() failed: %d\n", dev->name, ret);
			goto err_shutdown_phy;
		}
	}

	xgmac_phy_disable_eee(dev, xgmac->phy);

	ret = xgmac_phy_startup_wait(dev, xgmac->phy);
	if (ret < 0) {
		pr_err("%s phy_startup() failed: %d\n", dev->name, ret);
		goto err_shutdown_phy;
	}

	if (!xgmac->phy->link) {
		/*
		 * ret is still 0 from phy_startup() here, and upstream falls
		 * through to "return ret" via err_shutdown_phy - so start()
		 * reports failure on the console and success to its caller.
		 * eth_init() then marks the interface ACTIVE and the net stack
		 * transmits into a MAC whose DMA was never started, which is
		 * how a plain "No link" turned into a hung board.
		 */
		pr_err("%s No link\n", dev->name);
		xgmac_dump_c45_link(xgmac->phy);
		ret = -ENETDOWN;
		goto err_shutdown_phy;
	}

	/*
	 * Correct phydev->speed before anything consumes it - see the
	 * xgmac_pcs_adjust_phy comment in the header and D115.
	 */
	if (xgmac->config->ops->xgmac_pcs_adjust_phy) {
		ret = xgmac->config->ops->xgmac_pcs_adjust_phy(dev, xgmac->phy);
		if (ret < 0) {
			pr_err("%s xgmac_pcs_adjust_phy() failed: %d\n",
			       dev->name, ret);
			goto err_shutdown_phy;
		}
	}

	if (xgmac->config->ops->xgmac_fix_mac_speed) {
		ret = xgmac->config->ops->xgmac_fix_mac_speed(dev,
							      xgmac->phy->speed);
		if (ret < 0) {
			pr_err("%s xgmac_fix_mac_speed() failed: %d\n",
			       dev->name, ret);
			goto err_shutdown_phy;
		}
	}

	ret = xgmac_adjust_link(dev);
	if (ret < 0) {
		pr_err("%s xgmac_adjust_link() failed: %d\n", dev->name, ret);
		goto err_shutdown_phy;
	}

	xgmac_rx_restarts = 0;
	xgmac_rx_stall_idx = 0;
	xgmac_rx_stall_since = 0;
	xgmac_dev = dev;

	ret = xgmac_datapath_start(dev);
	if (ret)
		goto err;

	return 0;

err_shutdown_phy:
	phy_shutdown(xgmac->phy);
err_stop_resets:
	xgmac->config->ops->xgmac_stop_resets(dev);
err:
	pr_err("%s FAILED: %d\n", dev->name, ret);
	return ret;
}

static void xgmac_stop(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	unsigned long start_time;
	u32 val;
	u32 trcsts;
	u32 txqsts;
	u32 prxq;
	u32 rxqsts;

	debug("%s(dev=%p):\n", __func__, dev);

	if (!xgmac->started)
		return;

	xgmac_dump_datapath(dev, "pre-stop");

	/*
	 * D370: what replaces the 356 per-event lines. One line per transfer,
	 * printed only when the defect actually fired, so a clean datapath
	 * stays silent. The counters were already maintained; only the
	 * reporting point moves. "setenv eth_rxverbose 1" restores the
	 * per-event detail for bisecting.
	 */
	if (xgmac_rx_rescued || xgmac_rx_restarts)
		printf("xgmac: %u writeback losses rescued, %u RX engine resets\n",
		       xgmac_rx_rescued, xgmac_rx_restarts);

	xgmac->started = false;
	xgmac->reg_access_ok = false;

	/* Disable TX DMA */
	clrbits_le32(&xgmac->dma_chan->ch0_tx_control,
		     XGMAC_DMA_CH0_TX_CONTROL_ST);

	/* Wait for TX all packets to drain out of MTL */
	start_time = get_timer(0);

	while (get_timer(start_time) < XGMAC_TIMEOUT_100MS) {
		val = readl(&xgmac->mtl_chan->txq0_debug);

		trcsts = (val >> XGMAC_MTL_TXQ0_DEBUG_TRCSTS_SHIFT) &
			  XGMAC_MTL_TXQ0_DEBUG_TRCSTS_MASK;

		txqsts = val & XGMAC_MTL_TXQ0_DEBUG_TXQSTS;

		if (trcsts != XGMAC_MTL_TXQ0_DEBUG_TRCSTS_READ_STATE && !txqsts)
			break;
	}

	/* D291: say so instead of burning the timeout in silence (T-38). */
	if (get_timer(start_time) >= XGMAC_TIMEOUT_100MS)
		printf("xgmac: stop: TX MTL did not drain in %u ms (TxQ0_Debug 0x%08x)\n",
		       XGMAC_TIMEOUT_100MS, readl(&xgmac->mtl_chan->txq0_debug));

	/* Turn off MAC TX and RX */
	clrbits_le32(&xgmac->mac_regs->tx_configuration,
		     XGMAC_MAC_CONF_RE);
	clrbits_le32(&xgmac->mac_regs->rx_configuration,
		     XGMAC_MAC_CONF_RE);

	/*
	 * Wait for all RX packets to drain out of MTL.
	 *
	 * D291: this loop cannot succeed if anything is left in the queue, so
	 * the only thing protecting it is the timeout. Draining MTL needs the
	 * RX DMA to move the frame into a descriptor, and the DMA takes the
	 * descriptor its own pointer is on, not one the driver picks. At stop
	 * time that pointer is usually sitting on descriptors the driver has
	 * received but not yet recycled - d246's ping ended with Cur_RxDesc on
	 * RXD7 and RXD0 already OWN=0 - so the very next fetch finds OWN clear,
	 * raises RBU and parks. A frame that arrives between the pre-stop dump
	 * and this loop therefore pins PRXQ for the whole timeout. On a quiet
	 * network PRXQ is 0 and the loop exits at once, which is why this only
	 * shows up on a busy one.
	 */
	start_time = get_timer(0);

	while (get_timer(start_time) < XGMAC_TIMEOUT_100MS) {
		val = readl(&xgmac->mtl_chan->rxq0_debug);

		prxq = (val >> XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
			XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK;

		rxqsts = (val >> XGMAC_MTL_RXQ0_DEBUG_RXQSTS_SHIFT) &
			  XGMAC_MTL_RXQ0_DEBUG_RXQSTS_MASK;

		if (!prxq && !rxqsts)
			break;
	}

	if (get_timer(start_time) >= XGMAC_TIMEOUT_100MS)
		printf("xgmac: stop: RX MTL did not drain in %u ms (RxQ0_Debug 0x%08x) - frame dropped\n",
		       XGMAC_TIMEOUT_100MS, readl(&xgmac->mtl_chan->rxq0_debug));

	/* Turn off RX DMA */
	clrbits_le32(&xgmac->dma_chan->ch0_rx_control,
		     XGMAC_DMA_CH0_RX_CONTROL_SR);

	if (xgmac->phy)
		phy_shutdown(xgmac->phy);

	xgmac->config->ops->xgmac_stop_resets(dev);

	debug("%s: OK\n", __func__);
}

static int xgmac_send(struct udevice *dev, void *packet, int length)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_desc *tx_desc;
	unsigned long start_time;
	unsigned int tx_idx;

	debug("%s(dev=%p, packet=%p, length=%d):\n", __func__, dev, packet,
	      length);

	memcpy(xgmac->tx_dma_buf, packet, length);
	xgmac->config->ops->xgmac_flush_buffer(xgmac->tx_dma_buf, length);

	tx_idx = xgmac->tx_desc_idx;
	tx_desc = xgmac_get_desc(xgmac, tx_idx, false);
	xgmac->tx_desc_idx++;
	xgmac->tx_desc_idx %= xgmac->desc_tx;

	/* des0/des1 = low/high halves of the buffer address; see xgmac_start(). */
	tx_desc->des0 = lower_32_bits((ulong)xgmac->tx_dma_buf);
	tx_desc->des1 = upper_32_bits((ulong)xgmac->tx_dma_buf);
	tx_desc->des2 = length;
	/*
	 * Make sure that if HW sees the _OWN write below, it will see all the
	 * writes to the rest of the descriptor too.
	 */
	mb();
	tx_desc->des3 = XGMAC_DESC3_OWN | XGMAC_DESC3_FD | XGMAC_DESC3_LD | length;
	xgmac->config->ops->xgmac_flush_desc(tx_desc);

	writel((ulong)xgmac_get_desc(xgmac, xgmac->tx_desc_idx, false),
	       &xgmac->dma_chan->ch0_txdesc_tail_pointer);
	if (xgmac_env_on("eth_txdebug"))
		printf("xgmac: TX submit desc=%u len=%d des3=0x%08x TE=%d TXST=%d status=0x%08x tail=0x%08x\n",
		       tx_idx, length, tx_desc->des3,
		       !!(readl(&xgmac->mac_regs->tx_configuration) &
			  XGMAC_MAC_CONF_TE),
		       !!(readl(&xgmac->dma_chan->ch0_tx_control) &
			  XGMAC_DMA_CH0_TX_CONTROL_ST),
		       readl(&xgmac->dma_chan->ch0_status),
		       readl(&xgmac->dma_chan->ch0_txdesc_tail_pointer));

	start_time = get_timer(0);

	while (get_timer(start_time) < XGMAC_TIMEOUT_100MS) {
		xgmac->config->ops->xgmac_inval_desc(tx_desc);
		if (!(readl(&tx_desc->des3) & XGMAC_DESC3_OWN)) {
			if (xgmac_env_on("eth_txdebug"))
				printf("xgmac: TX complete desc=%u status=0x%08x\n",
				       tx_idx, readl(&xgmac->dma_chan->ch0_status));
			return 0;
		}
	}
	/*
	 * D291: was debug() only, and with the old timeout it sat here for a
	 * hundred seconds first. A transmit that never completes is worth a
	 * line on the console.
	 */
	printf("xgmac: TX timeout after %u ms (des3 0x%08x, Cur_TxDesc 0x%08x, TxQ0_Debug 0x%08x)\n",
	       XGMAC_TIMEOUT_100MS, readl(&tx_desc->des3),
	       readl((u8 *)xgmac->dma_chan + 0x44),
	       readl(&xgmac->mtl_chan->txq0_debug));

	/*
	 * D297/D298: this descriptor is still OWN=1 and tx_desc_idx has already
	 * moved past it, so upstream's bare return leaks it. d247 showed what
	 * that costs: five timeouts in a row left TXD2/3/4 permanently OWN=1
	 * with tx_desc_idx at 5, and once eight have leaked the ring is full of
	 * descriptors the DMA owns and the driver can never transmit again.
	 *
	 * The frames themselves went out - MMC txpkt_gb climbed from 63 to 68
	 * across those five timeouts - so what failed is the descriptor
	 * writeback, the same failure as the RX wedge and at the same count.
	 * Treat it the same way: reset the channel, which re-arms both rings and
	 * puts tx_desc_idx back to 0. The frame is lost; tftp and ping retry.
	 * "setenv eth_txrecover 0" restores the leak.
	 */
	if (!xgmac_env_off("eth_txrecover") &&
	    xgmac_rx_restarts < XGMAC_RX_RESTART_MAX)
		xgmac_rx_chan_restart(xgmac);

	return -ETIMEDOUT;
}

/*
 * RBU - receive buffer unavailable - means the RX ring ran dry and the engine
 * parked itself. Nothing in the upstream driver ever clears it or re-arms the
 * tail pointer, so once it latches, every later frame is dropped in the MTL RX
 * queue instead of reaching a descriptor. Pass 33 caught exactly that state:
 * DMA_CH0_Status 0x00000084 (TBU | RBU) with MTL_RxQ0_Debug 0x005b0012 - which
 * is PRXQ, bits 30:16, so 0x5b = 91 *packets* queued with nowhere to go, not
 * 0x5b bytes as this comment originally said. A DHCP reply arriving in that
 * window is lost silently.
 *
 * Re-arming the tail here unblocks the DMA but does not empty the queue, and
 * nothing else does either: see the eth_rxflush note in xgmac_start() for why a
 * backlog outlives every eth_halt()/eth_start() in a boot.
 */
/*
 * D250: the RX tail pointer convention.
 *
 * Upstream programs the tail as the address of the LAST descriptor, &RXD[7].
 * The DMA fetches descriptors while Cur_RxDesc != RxTail, so &RXD[7] means
 * "you may use RXD0..RXD6" and RBU is raised the moment Cur_RxDesc reaches
 * &RXD[7]. That is exactly the wedged state d235 measured: RxTail ==
 * Cur_RxDesc == &RXD[7] with RXD2..RXD7 all still OWN=1 and 21 frames stuck
 * in MTL. Once the two are equal there is no tail value left to write that
 * differs from the one the register already holds, so xgmac_rx_rearm()'s write
 * cannot restart the engine - which is what the three-identical-pointers
 * diagnostic proved.
 *
 * Linux stmmac uses the other convention: rx_tail_addr = dma_rx_phy +
 * (nr_descs * sizeof(desc)), i.e. ONE PAST the end of the ring, and on refill
 * it moves the tail to the next descriptor still to be refilled. That keeps
 * the tail ahead of Cur_RxDesc while descriptors are free, makes all 8
 * descriptors usable instead of 7, and - the point here - always leaves a tail
 * value that differs from Cur_RxDesc, so a tail write is a real kick.
 *
 * "setenv eth_rxupstream 1" restores the upstream &RXD[N-1] convention for a
 * same-boot A/B.
 */
static ulong xgmac_rx_tail_addr(struct xgmac_priv *xgmac, unsigned int idx)
{
	return (ulong)xgmac_get_desc(xgmac, 0, true) + idx * xgmac->desc_size;
}

/*
 * D259: one switch that restores the upstream RX tail handling exactly - the
 * &RXD[N-1] tail in xgmac_start(), the &RXD[idx] write in xgmac_free_pkt()
 * every fourth packet, no tracking and no recovery. That is the code that
 * passed ten pings in pass 59, so "setenv eth_rxupstream 1" is always
 * available as a way back to a known-good datapath if anything here regresses.
 */
static bool xgmac_rx_upstream(void)
{
	return xgmac_env_on("eth_rxupstream");
}

/*
 * D256: keep the tail one descriptor BEHIND the one xgmac_recv() is waiting
 * for, and refresh it on every poll.
 *
 * The DMA fills descriptors while Cur_RxDesc != RxTail, so whatever the tail
 * points at is the one descriptor that never gets written. d236's ping 2 hung
 * because the tail excluded exactly the descriptor the driver was blocked on:
 * four background broadcasts landed in RXD0..RXD3 during xgmac_start(), the
 * driver delivered them, xgmac_free_pkt() recycled the batch and set the tail
 * to &RXD[4] - and then waited forever for RXD4, which the tail had just put
 * out of bounds. No RBU either, because the DMA still had RXD7/RXD0..3 to
 * work with, so the d236 recovery never even triggered.
 *
 * One behind the next expected index is the only value that is always correct:
 * it gives the DMA a 7-deep window that starts at the descriptor the driver
 * wants next, and it moves forward every time the driver consumes one - which
 * doubles as the tail-write kick the engine needs to leave a suspend. Writing
 * the tail is a register poke, not a descriptor flush, so unlike the re-arm it
 * does not have to be batched per cacheline. "setenv eth_rxtrack 0" disables
 * the tracking.
 */
static void xgmac_rx_tail_track(struct xgmac_priv *xgmac)
{
	ulong want;
	u32 have;

	/*
	 * D266: opt-in from d240 on. "One behind the next expected index" is
	 * the textbook window, but it collides with the descriptor fetch
	 * pointer: with rx_desc_idx 0 it computes &RXD[7], which is exactly
	 * where Cur_RxDesc parks after the 8-descriptor prefetch, so RBU
	 * relatches on every poll and the tracker and the RBU re-arm spend the
	 * whole command overwriting each other (measured, d239 tftp). The tail
	 * is not what protects un-recycled descriptors anyway - OWN=0 is - so
	 * the default is now a tail one past the ring end, which can never
	 * equal a descriptor address. "setenv eth_rxtrack 1" restores tracking.
	 */
	if (xgmac_rx_upstream() || !xgmac_env_on("eth_rxtrack"))
		return;

	want = xgmac_rx_tail_addr(xgmac, (xgmac->rx_desc_idx +
					  xgmac->desc_rx - 1) %
					 xgmac->desc_rx);
	have = readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer);

	if ((ulong)have == want)
		return;

	/* Never hand the DMA a tail it has already fetched up to. */
	if (want == (ulong)readl((u8 *)xgmac->dma_chan + 0x4c))
		return;

	writel(want, &xgmac->dma_chan->ch0_rxdesc_tail_pointer);

	if (xgmac_env_on("eth_rxdump"))
		printf("xgmac: RxTail tracked 0x%08x -> 0x%08lx for rx_desc_idx %u\n",
		       have, want, xgmac->rx_desc_idx);
}

/*
 * Hand every RX descriptor back to the DMA and put the software index back at
 * RXD0. Any frame the DMA had written back but xgmac_recv() had not yet
 * delivered is dropped - acceptable, because this only runs on a wedge where
 * the alternative is dropping every frame from here to the end of the command.
 */
static void xgmac_rx_ring_rearm(struct xgmac_priv *xgmac)
{
	unsigned int i;

	for (i = 0; i < xgmac->desc_rx; i++) {
		struct xgmac_desc *rx_desc = xgmac_get_desc(xgmac, i, true);

		rx_desc->des0 = lower_32_bits((ulong)(xgmac->rx_dma_buf +
						      (i * xgmac->rx_stride)));
		rx_desc->des1 = upper_32_bits((ulong)(xgmac->rx_dma_buf +
						      (i * xgmac->rx_stride)));
		rx_desc->des2 = 0;
		mb();
		rx_desc->des3 = XGMAC_DESC3_OWN;
		xgmac->config->ops->xgmac_flush_desc(rx_desc);
		xgmac->config->ops->xgmac_inval_buffer(xgmac->rx_dma_buf +
						      (i * xgmac->rx_stride),
						      XGMAC_MAX_PACKET_SIZE);
	}

	xgmac->rx_desc_idx = 0;
}

/*
 * D250: the real recovery D249 asked for. A tail write is the documented way
 * to un-suspend an RX DMA, but it only works if the tail moves; when the
 * engine has parked ON the tail there is nothing to move. Rewriting
 * DMA_CH0_RxDesc_List_Address forces Cur_RxDesc back to the base of the ring,
 * which is the strongest reset available without DMA_MODE.SWR - and unlike
 * SWR it keeps the MAC, the MTL queue and its backlog intact, so the frames
 * already queued should drain instead of being thrown away.
 *
 * Sequence: stop the channel (SR=0), re-arm the whole ring, rewrite the list
 * address and ring length, start the channel, then plant a tail one past the
 * end. Capped per xgmac_start() so a hardware fault cannot turn into an
 * infinite recovery loop. "setenv eth_rxrestart 0" disables it.
 *
 * D288: the cap was 8, and d245 hit it. With the AXI master configuration
 * fixed the transfer runs 63 frames between stalls instead of dying at two,
 * so eight recoveries is only ~500 frames - the d245 log ends on "SWR (#8)"
 * followed by nothing but tftp timeouts, because the budget ran out rather
 * than because the link died. 64 lets a real transfer finish and, more
 * usefully, shows whether the 63-frame period is constant over dozens of
 * events instead of two.
 */

/*
 * D265: recover a wedged RX engine with a real reset.
 *
 * d239 refuted the cheap version. Dropping SR, rewriting
 * DMA_CH0_RxDesc_List_Address to the ring base and raising SR again does not
 * move Cur_RxDesc off &RXD[7] and does not drain MTL: four restarts in a row
 * left PRXQ pinned at 1 with all eight descriptors armed and not one further
 * frame delivered. The only reset ever measured to empty MTL is DMA_MODE.SWR
 * (D191) - every command starts with PRXQ=0 because xgmac_start() does it - so
 * do exactly that here and re-run the shared datapath programming afterwards.
 * The in-flight frame is lost either way; tftp and ping both retry.
 * "setenv eth_rxreset 0" keeps the old list-address rewrite.
 */
static void xgmac_rx_chan_restart(struct xgmac_priv *xgmac)
{
	unsigned int prxq_before, prxq_after;
	ulong tail;
	int ret;

	prxq_before = (unsigned int)((readl(&xgmac->mtl_chan->rxq0_debug) >>
				     XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
				    XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK);

	xgmac_recovering = true;

	/*
	 * D275: the strong option, opt-in.
	 *
	 * d242 showed a full SWR plus a complete re-run of the shared datapath
	 * programming does not restore reception, while the identical
	 * programming works from cold every single time - so whatever is wedged
	 * sits outside the channel reset domain, in the MAC/XPCS/SerDes half of
	 * xgmac_start() that the recovery never touches. "setenv eth_rxfull 1"
	 * makes the recovery a full stop/start, which is exactly what a fresh
	 * ping command does. If that revives the transfer the wedge is up there;
	 * if it does not, no amount of MAC-side reinitialisation will fix this
	 * and the fault is in front of the MAC. Off by default because it
	 * re-runs PHY startup and can cost seconds (D259: risky changes ship
	 * with a one-command revert, not as the default).
	 */
	if (xgmac_dev && xgmac_env_on("eth_rxfull")) {
		/* Both dumps are wanted here, so drop the suppression first. */
		xgmac_recovering = false;
		xgmac_stop(xgmac_dev);
		ret = xgmac_start(xgmac_dev);
		xgmac_rx_restarts++;
		xgmac_rx_stall_idx = 0;
		xgmac_rx_stall_since = 0;
		prxq_after = (unsigned int)((readl(&xgmac->mtl_chan->rxq0_debug) >>
					    XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
					   XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK);
		printf("xgmac: RX full stop/start (#%u, ret %d): Cur_RxDesc 0x%08x, RxTail 0x%08x, PRXQ %u -> %u\n",
		       xgmac_rx_restarts, ret,
		       readl((u8 *)xgmac->dma_chan + 0x4c),
		       readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer),
		       prxq_before, prxq_after);
		if (ret)
			xgmac_rx_restarts = XGMAC_RX_RESTART_MAX;
		return;
	}

	if (xgmac_dev && !xgmac_env_off("eth_rxreset")) {
		clrbits_le32(&xgmac->mac_regs->rx_configuration,
			     XGMAC_MAC_CONF_RE);
		clrbits_le32(&xgmac->mac_regs->tx_configuration,
			     XGMAC_MAC_CONF_TE);
		clrbits_le32(&xgmac->dma_chan->ch0_rx_control,
			     XGMAC_DMA_CH0_RX_CONTROL_SR);
		clrbits_le32(&xgmac->dma_chan->ch0_tx_control,
			     XGMAC_DMA_CH0_TX_CONTROL_ST);

		setbits_le32(&xgmac->dma_regs->mode, XGMAC_DMA_MODE_SWR);
		ret = wait_for_bit_le32(&xgmac->dma_regs->mode,
					XGMAC_DMA_MODE_SWR, false,
					XGMAC_TIMEOUT_100MS, false);
		if (ret) {
			printf("xgmac: RX recovery: SWR stuck (%d), giving up\n",
			       ret);
			xgmac_rx_restarts = XGMAC_RX_RESTART_MAX;
			xgmac_recovering = false;
			return;
		}

		xgmac->tx_desc_idx = 0;
		xgmac->rx_desc_idx = 0;

		ret = xgmac_datapath_start(xgmac_dev);
		if (ret) {
			printf("xgmac: RX recovery: datapath re-init failed (%d)\n",
			       ret);
			xgmac_rx_restarts = XGMAC_RX_RESTART_MAX;
			xgmac_recovering = false;
			return;
		}

		tail = readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer);
	} else {
		clrbits_le32(&xgmac->dma_chan->ch0_rx_control,
			     XGMAC_DMA_CH0_RX_CONTROL_SR);

		xgmac_rx_ring_rearm(xgmac);

		/* Both halves, as in xgmac_start() - see the note there. */
		writel(upper_32_bits((ulong)xgmac_get_desc(xgmac, 0, true)),
		       &xgmac->dma_chan->ch0_rxdesc_list_haddress);
		writel(lower_32_bits((ulong)xgmac_get_desc(xgmac, 0, true)),
		       &xgmac->dma_chan->ch0_rxdesc_list_address);
		writel(xgmac->desc_rx - 1,
		       &xgmac->dma_chan->ch0_rxdesc_ring_length);

		setbits_le32(&xgmac->dma_chan->ch0_rx_control,
			     XGMAC_DMA_CH0_RX_CONTROL_SR);

		tail = xgmac_rx_tail_addr(xgmac, xgmac->desc_rx);
		writel(XGMAC_DMA_CH0_STATUS_RBU, &xgmac->dma_chan->ch0_status);
		writel(tail, &xgmac->dma_chan->ch0_rxdesc_tail_pointer);
	}

	xgmac_recovering = false;
	xgmac_rx_restarts++;
	xgmac_rx_stall_idx = 0;
	xgmac_rx_stall_since = 0;

	prxq_after = (unsigned int)((readl(&xgmac->mtl_chan->rxq0_debug) >>
				    XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
				   XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK);

	/*
	 * D370: this line and the rescue line below fire once per writeback
	 * loss - 178 times in a 16 MiB download - and each pair is ~168
	 * characters. At 115200 8N1 a character is 86.8 us of BLOCKING putc(),
	 * so the pair costs 14.6 ms and the set costs 2.60 s of a 10.0 s
	 * transfer: 26%. That is not an estimate from a wall clock, it is
	 * chars x 86.8 us against a serial driver that blocks the datapath.
	 *
	 * Both are kept verbatim behind "setenv eth_rxverbose 1", because they
	 * are the only per-event record of the D314 writeback defect and every
	 * bisect so far has depended on them. What ships is the one-line
	 * summary xgmac_stop() prints instead.
	 */
	if (xgmac_env_on("eth_rxverbose"))
		printf("xgmac: RX %s (#%u): Cur_RxDesc 0x%08x, RxTail 0x%08lx, PRXQ %u -> %u\n",
		       (xgmac_dev && !xgmac_env_off("eth_rxreset")) ?
				"engine reset via SWR" : "channel restarted",
		       xgmac_rx_restarts,
		       readl((u8 *)xgmac->dma_chan + 0x4c), tail,
		       prxq_before, prxq_after);
}

/*
 * D256: a wedge with RBU clear.
 *
 * d236's ping 2 made no progress with RBU=0, PRXQ=0 and the DMA happily parked
 * on descriptors the driver was not looking at, so every recovery gated on RBU
 * was dead. Watch rx_desc_idx instead: if it has not moved for
 * XGMAC_RX_STALL_MS while the driver is still polling, the tail tracking above
 * has already had its chance and the channel needs the stronger reset.
 */
/*
 * D300: 400 ms was chosen when it was unknown whether the missing writeback was
 * merely late. D294 settled that - the descriptor is re-read 10 ms after this
 * timer expires and OWN is still set, 34,000 frame times after the frame
 * landed - so every recovery spends ~390 ms of the 400 waiting for something
 * that is never coming. At 63 frames per recovery that is the dominant cost of
 * a transfer: d247's run burnt ~26 s of pure stall. 100 ms keeps a wide margin
 * over any real inter-frame gap on a link that is delivering back-to-back tftp
 * blocks, and "setenv eth_rxstallms 400" restores the old value if a quiet or
 * lossy link starts producing false positives.
 */
/*
 * D323: 20, measured. T-74 ran the 8 MiB transfer at 20 ms and the writeback was
 * still gone on all 89 stalls - the "merely late" verdict has now never once
 * appeared, across ~180 stalls at 100 ms and 89 at 20 ms. At 100 ms the detector
 * was 8.8 s of a 26.0 s transfer, 34%; at 20 ms it is 1.78 s of 11.95 s. The
 * safety argument for a long timer was never the duration anyway - it is the
 * PRXQ != 0 gate below (D273), which means the driver only ever acts when the
 * MAC is provably holding a frame it will not hand over. "setenv eth_rxstallms
 * 100" restores the previous value.
 */
/*
 * D372: 5 ms, measured. The cost of this timer is 89 x itself per 8 MiB and
 * nothing else - the wait is not a poll interval, it is how long the driver
 * takes to conclude a writeback that never arrives has not arrived, and
 * xgmac_recv() keeps polling throughout so a late frame is still delivered.
 * Sweeping it against the best of repeated runs (host interference can only
 * slow a transfer, so the fastest run is the board's capability) gave 2.5,
 * 3.4, 4.3 and 5.4 MiB/s at 20, 10, 5 and 1 ms, all within 2% of
 * 1.42 s + 89 x stallms against the 238 us/block residual of D371.
 *
 * The floor is between 1 and 5. At 1 ms xgmac_rx_restarts exceeded
 * xgmac_rx_rescued (89/87 and 90/87): the writeback landed late, OWN was
 * already clear so the rescue block was skipped, and the code fell through to
 * a channel restart anyway - a SWR on a valid descriptor, which discards the
 * frame and costs a retransmit. Equal counters mean no false positive, and 5
 * held equal across three runs while sitting 5x above the only late writeback
 * ever measured (D365, RXD3 des3 0x300705ea).
 *
 * "setenv eth_rxstallms 20" restores the previous value.
 */
#define XGMAC_RX_STALL_MS	5

static ulong xgmac_rx_stall_ms(void)
{
	return env_get_ulong("eth_rxstallms", 10, XGMAC_RX_STALL_MS);
}

/*
 * T-66: work out how long the stranded frame is, from the frame itself.
 *
 * D314 established that when the stall fires the frame's *data* is already in
 * the buffer and only the descriptor writeback is missing - d251 printed a
 * complete Ethernet+IPv4+UDP header out of the stalled descriptor's buffer on
 * both stalls of the same transfer. So the one thing the driver is missing is
 * the length field, and the frame carries it: 14 bytes of Ethernet header plus
 * the IPv4 total-length field. For the frames this actually strands - full-size
 * tftp data blocks - that arithmetic gives 14 + 1500 = 1514, which is bit for
 * bit the length the hardware writes into des3 on a successful close
 * (0x300705ea decodes as FD|LD|1514). The rescued descriptor is therefore
 * indistinguishable from a real one, not an approximation of one.
 *
 * Trusting a length out of DMA memory is exactly what D299 crashed on, so this
 * is deliberately paranoid: IPv4 only, header length in range, *and* the IPv4
 * header checksum has to verify before the total-length field is believed. A
 * 16-bit field that survives its own checksum is not a wild value. The result
 * is then bounded by the buffer the descriptor owns, the same clamp D299 added
 * to the normal path. Anything that fails a check returns an error and the
 * caller falls back to the plain reset - the rescue can decline, it can never
 * fabricate.
 *
 * ARP and other non-IP frames are not rescuable and do not need to be: they
 * carry no length field, they are never the frame a tftp transfer is waiting
 * for, and the reset path already handles them correctly.
 */
static int xgmac_rx_rescue_len(struct xgmac_priv *xgmac, u8 *sb)
{
	unsigned int ihl, total, len, i;
	u32 sum = 0;

	if (sb[12] != 0x08 || sb[13] != 0x00)
		return -EPROTONOSUPPORT;
	if ((sb[14] >> 4) != 4)
		return -EPROTONOSUPPORT;

	ihl = (sb[14] & 0xf) * 4;
	if (ihl < 20 || ihl > 40)
		return -EINVAL;

	for (i = 0; i < ihl; i += 2)
		sum += ((u32)sb[14 + i] << 8) | sb[14 + i + 1];
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	if (sum != 0xffff)
		return -EBADMSG;

	total = ((unsigned int)sb[16] << 8) | sb[17];
	if (total < ihl)
		return -EINVAL;

	len = 14 + total;
	if (len < 60 || len > XGMAC_MAX_PACKET_SIZE ||
	    len > xgmac->rx_stride)
		return -EMSGSIZE;

	return (int)len;
}

static void xgmac_rx_stall_check(struct xgmac_priv *xgmac)
{
	if (xgmac_rx_upstream() || xgmac_env_off("eth_rxrestart"))
		return;

	if (!xgmac_rx_stall_since || xgmac_rx_stall_idx != xgmac->rx_desc_idx) {
		xgmac_rx_stall_idx = xgmac->rx_desc_idx;
		xgmac_rx_stall_since = get_timer(0);
		return;
	}

	if (get_timer(xgmac_rx_stall_since) < xgmac_rx_stall_ms())
		return;

	if (xgmac_rx_restarts >= XGMAC_RX_RESTART_MAX)
		return;

	/*
	 * D273: no frame in MTL means nothing is wedged - the driver is simply
	 * waiting for the peer, which is the normal state of an ARP or ping
	 * poll. d241 fired a full SWR re-init 400 ms into a perfectly healthy
	 * ARP wait (PRXQ=0) and tore down the pending transmit with it.
	 * Recover only when the MAC is holding a packet it will not hand over.
	 */
	if (!((readl(&xgmac->mtl_chan->rxq0_debug) >>
	       XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
	      XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK)) {
		xgmac_rx_stall_since = get_timer(0);
		return;
	}

	/*
	 * D324: the per-event console traffic is now a measurable part of the
	 * transfer. This header is ~200 characters, the buffer hexdump another
	 * ~100, and at 115200 baud that is ~26 ms of blocking serial per event -
	 * against a 20 ms detector and a 1.78 ms round trip. Times 89 events on
	 * an 8 MiB file it is ~2.3 s of a 12 s transfer. Everything these lines
	 * established is now established: the buffer holds a valid frame (D316,
	 * four datapaths then 88 more) and the writeback is never late (D294,
	 * D323). So the diagnostic form moves behind "eth_rxdump 1" and the
	 * default is one concise line, printed below.
	 */
	if (xgmac_env_on("eth_rxdump"))
		printf("xgmac: RX stalled %lu ms at rx_desc_idx %u after %u frames (%u full, %llu bytes) this datapath (%u total) with RBU clear (Cur_RxDesc 0x%08x, RxTail 0x%08x, PRXQ=%u)\n",
		       get_timer(xgmac_rx_stall_since), xgmac->rx_desc_idx,
		       xgmac_rx_frames - xgmac_rx_frames_at_start,
		       xgmac_rx_full_frames - xgmac_rx_full_frames_at_start,
		       (unsigned long long)(xgmac_rx_bytes - xgmac_rx_bytes_at_start),
		       xgmac_rx_frames,
		       readl((u8 *)xgmac->dma_chan + 0x4c),
		       readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer),
		       (unsigned int)((readl(&xgmac->mtl_chan->rxq0_debug) >>
				       XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
				      XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK));

	/*
	 * D288: the two questions the d245 dumps could not answer.
	 *
	 * d245 wedged with Cur_RxBuf 1520 bytes into the stalled descriptor's
	 * buffer - 1518 bytes, a full-size frame with its CRC, rounded up to
	 * the next 16 - while the descriptor itself still read des3 0x80000000,
	 * OWN=1, no length and no FD/LD, after an explicit invalidate. So the
	 * frame's data write landed and the descriptor writeback did not. Two
	 * things follow that are worth measuring before the recovery erases
	 * them:
	 *
	 * 1. Is that really a frame? Print the head of the buffer. A valid
	 *    Ethernet header (our MAC in the destination, 0x0800 at offset 12)
	 *    proves the data path delivered the frame into the right buffer and
	 *    only the descriptor close is missing.
	 * 2. Is the writeback merely late? Re-read the descriptor after 10 ms,
	 *    which is three orders of magnitude longer than any burst. If OWN
	 *    clears on the second read the writeback is slow, not lost, and the
	 *    400 ms stall detector is misreading a stalled *poll*; if it stays
	 *    set the writeback is genuinely gone.
	 */
	{
		struct xgmac_desc *sd = xgmac_get_desc(xgmac, xgmac->rx_desc_idx,
						      true);
		u8 *sb = (u8 *)xgmac->rx_dma_buf +
			 xgmac->rx_desc_idx * xgmac->rx_stride;
		u32 des3_first, des3_again;
		int j;

		xgmac->config->ops->xgmac_inval_desc(sd);
		des3_first = sd->des3;

		/*
		 * 64, not 32: the printed dump only needs 32 bytes but
		 * xgmac_rx_rescue_len() checksums the whole IPv4 header, which
		 * ends at byte 34 for the minimum header and 54 for the
		 * largest one this accepts. Needed on both paths.
		 */
		xgmac->config->ops->xgmac_inval_buffer(sb, 64);

		if (xgmac_env_on("eth_rxdump")) {
			printf("xgmac: stalled RXD%u buffer head:",
			       xgmac->rx_desc_idx);
			for (j = 0; j < 32; j++)
				printf("%s%02x", (j % 16) ? " " : "\n  ", sb[j]);
			printf("\n");

			udelay(10000);
			xgmac->config->ops->xgmac_inval_desc(sd);
			des3_again = sd->des3;
			printf("xgmac: stalled RXD%u des3 0x%08x, again after 10 ms 0x%08x (%s)\n",
			       xgmac->rx_desc_idx, des3_first, des3_again,
			       (des3_again & XGMAC_DESC3_OWN) ?
				       "writeback lost" : "writeback merely late");
		} else {
			/*
			 * D323: the 10 ms confirmation delay costs 0.89 s over
			 * an 8 MiB transfer and has returned the same answer
			 * every time it has ever been asked. The first read is
			 * taken after the detector has already waited
			 * eth_rxstallms, which is itself 200 times the longest
			 * plausible writeback latency.
			 */
			des3_again = des3_first;
		}

		/*
		 * T-66. D302 measured a recovery at ~2.07 s, of which the stall
		 * detector is 0.10 s and the wire time is nothing: the ~1.97 s
		 * is the tftp server's retransmit timer, running because the
		 * frame we are about to reset over is thrown away. Close the
		 * descriptor by hand first and the frame goes up, the peer is
		 * ACKed, and the reset costs only its own 0.1 s.
		 *
		 * D325/T-78: order matters, and d252-d254 had it wrong.
		 * xgmac_rx_chan_restart() re-initialises the ring and zeroes
		 * rx_desc_idx, so those builds deferred it to
		 * xgmac_free_pkt() to keep the rescued descriptor alive. That
		 * delivers the frame to the stack with the engine still
		 * wedged, and the stack answers a tftp data block by sending
		 * an ACK straight away - twice in 5714 blocks that ACK hit a
		 * 100 ms TX timeout and cost a retransmit. Copying the frame
		 * out lets the restart happen here, before anything is
		 * delivered, so the ACK always goes out on a healthy engine.
		 *
		 * Off by default (D259). "setenv eth_rxrescue 1" enables it;
		 * "setenv eth_rxrescue 0" is the one-command revert to exactly
		 * d251 behaviour. This is a mitigation and must never be
		 * described as a fix: the periodic writeback loss of D314 is
		 * still there, still every 64th full-size frame, and the merge
		 * notes have to say so.
		 */
		/*
		 * D322: on by default from d253. The 8 MiB transfer measured
		 * 187 s -> 26.0 s, 7.2x, with 88 rescues all at exactly 1514
		 * bytes and not one declined; the path was separately shown to
		 * be inert when disabled (pass 81). Shipping a 7x mitigation
		 * off by default would be indefensible, and D259's requirement
		 * is a one-command revert, not a particular default:
		 * "setenv eth_rxrescue 0".
		 */
		if ((des3_again & XGMAC_DESC3_OWN) &&
		    !xgmac_env_off("eth_rxrescue")) {
			int rlen = xgmac_rescue_len ?
					-EBUSY :
					xgmac_rx_rescue_len(xgmac, sb);

			if (rlen > 0) {
				/*
				 * T-78. Take a copy and restart immediately.
				 * The 1514-byte memcpy is far cheaper than the
				 * 100 ms TX timeout plus tftp retransmit that
				 * D325 measured on the descriptor-forging
				 * version of this path, and no descriptor is
				 * forged at all any more: the frame's length
				 * comes from its own checksum-verified IPv4
				 * header, which is the only thing the lost
				 * writeback would have told us.
				 */
				xgmac->config->ops->xgmac_inval_buffer(sb,
								      rlen);
				memcpy(xgmac_rescue_buf, sb, rlen);
				xgmac_rescue_len = rlen;
				xgmac_rx_rescued++;
				/* D370: see xgmac_rx_chan_restart(). */
				if (xgmac_env_on("eth_rxverbose"))
					printf("xgmac: RXD%u writeback lost after %u frames, rescued %d bytes (#%u)\n",
					       xgmac->rx_desc_idx,
					       xgmac_rx_frames -
						       xgmac_rx_frames_at_start,
					       rlen, xgmac_rx_rescued);
				xgmac_rx_chan_restart(xgmac);
				xgmac_rx_stall_idx = 0;
				xgmac_rx_stall_since = 0;
				return;
			}

			printf("xgmac: RXD%u writeback lost after %u frames, not rescuable (%d) - taking the reset\n",
			       xgmac->rx_desc_idx,
			       xgmac_rx_frames - xgmac_rx_frames_at_start, rlen);

			/*
			 * D377: say what is actually in the buffer. -EPROTONOSUPPORT
			 * (-93) means only "sb[12..13] is not 0x0800, or the version
			 * nibble is not 4", and two very different faults produce it:
			 * a buffer the DMA never wrote, which reads back as the zeros
			 * left by init - the frame never landed at all, so this is not
			 * a lost writeback - or a real non-IPv4 frame that promiscuous
			 * plus receive-all let in (ARP, IPv6 multicast, LLDP), which
			 * the reset path already handles correctly and which is not a
			 * defect at all. Sixteen bytes separates them at a glance.
			 *
			 * sb was invalidated for 64 bytes by the caller above, so these
			 * are the bytes the device left in DRAM, not a stale line.
			 *
			 * eth_rxdump already prints 32 bytes here, but it also prints
			 * per frame elsewhere and needs a setenv that saveenv cannot
			 * persist (D355). At ~19 lines per transfer this is cheaper
			 * than the reset it annotates, so it stays unconditional.
			 */
			printf("xgmac:   buf %02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x %02x%02x %02x%02x (dst src type ver/ihl)\n",
			       sb[0], sb[1], sb[2], sb[3], sb[4], sb[5],
			       sb[6], sb[7], sb[8], sb[9], sb[10], sb[11],
			       sb[12], sb[13], sb[14], sb[15]);
		} else if ((des3_again & XGMAC_DESC3_OWN) &&
			   !xgmac_env_on("eth_rxdump")) {
			/* Rescue disabled: still say what happened, once. */
			printf("xgmac: RXD%u writeback lost after %u frames, rescue disabled\n",
			       xgmac->rx_desc_idx,
			       xgmac_rx_frames - xgmac_rx_frames_at_start);
		}
	}

	/*
	 * D275: dump before touching anything. Every ring, MMC and Cur_RxBuf
	 * reading in d242's log was taken after the SWR and after CNTRST, so
	 * six dumps described six freshly initialised channels and said nothing
	 * about the wedge. This is the only dump of the failing state itself.
	 */
	if (xgmac_dev)
		xgmac_dump_datapath(xgmac_dev, "wedged, pre-recovery");

	xgmac_rx_chan_restart(xgmac);
	xgmac_rx_stall_since = get_timer(0);
}

static void xgmac_rx_rearm(struct xgmac_priv *xgmac)
{
	u32 sts = readl(&xgmac->dma_chan->ch0_status);
	u32 cur, prxq, before;
	ulong tail;

	xgmac_rx_tail_track(xgmac);

	if (!(sts & XGMAC_DMA_CH0_STATUS_RBU)) {
		xgmac_rx_stall_check(xgmac);
		return;
	}

	/*
	 * D247/D250: under the upstream convention this wrote the tail pointer
	 * back to the address it already held, so it cleared RBU and moved
	 * nothing - d235 measured RxTail, the written value and Cur_RxDesc all
	 * equal to 0x87684830 three times running while PRXQ climbed 5 -> 21.
	 * Unless eth_rxupstream is set the tail is one past the ring end, so this write
	 * is a genuine kick; if the engine is parked on it anyway, fall through
	 * to the channel restart.
	 */
	tail = xgmac_rx_tail_addr(xgmac, xgmac->desc_rx);
	before = readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer);
	cur = readl((u8 *)xgmac->dma_chan + 0x4c);
	prxq = (u32)(readl(&xgmac->mtl_chan->rxq0_debug) >>
		XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT) &
	       XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK;

	if (xgmac_env_on("eth_regdump"))
		printf("xgmac: RBU re-arm: RxTail 0x%08x -> 0x%08lx, Cur_RxDesc 0x%08x, PRXQ=%u, rx_desc_idx %u\n",
		       before, tail, cur, prxq, xgmac->rx_desc_idx);

	/* W1C, and only the bits that are actually set. */
	writel(XGMAC_DMA_CH0_STATUS_RBU, &xgmac->dma_chan->ch0_status);
	writel(tail, &xgmac->dma_chan->ch0_rxdesc_tail_pointer);

	/*
	 * The tail write cannot help when the engine is already sitting on the
	 * address being written and frames are waiting in MTL. That is the
	 * deadlock, and it needs the list address rewritten, not a pointer
	 * nudged.
	 */
	if (xgmac_rx_upstream() || xgmac_env_off("eth_rxrestart"))
		return;

	if (cur != tail || !prxq)
		return;

	if (xgmac_rx_restarts >= XGMAC_RX_RESTART_MAX) {
		if (xgmac_env_on("eth_regdump"))
			printf("xgmac: RX wedged again, restart budget %u used up\n",
			       XGMAC_RX_RESTART_MAX);
		return;
	}

	xgmac_rx_chan_restart(xgmac);
}

static int xgmac_recv(struct udevice *dev, int flags, uchar **packetp)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_desc *rx_desc;
	int length;

	debug("%s(dev=%p, flags=0x%x):\n", __func__, dev, flags);

	xgmac_rx_rearm(xgmac);

	/*
	 * T-78. A frame rescued by xgmac_rx_stall_check() lives in
	 * xgmac_rescue_buf, not in the ring - the ring was reinitialised
	 * underneath it - so it is delivered from here and xgmac_free_pkt()
	 * has nothing to recycle for it.
	 */
	if (xgmac_rescue_len) {
		length = xgmac_rescue_len;
		xgmac_rescue_len = 0;
		*packetp = xgmac_rescue_buf;
		xgmac_rx_frames++;
		xgmac_rx_bytes += length;
		if (length >= XGMAC_RX_FULL_FRAME_MIN)
			xgmac_rx_full_frames++;
		return length;
	}

	rx_desc = xgmac_get_desc(xgmac, xgmac->rx_desc_idx, true);
	xgmac->config->ops->xgmac_inval_desc(rx_desc);
	if (rx_desc->des3 & XGMAC_DESC3_OWN) {
		debug("%s: RX packet not available\n", __func__);
		return -EAGAIN;
	}

	*packetp = xgmac->rx_dma_buf +
		   (xgmac->rx_desc_idx * xgmac->rx_stride);
	length = rx_desc->des3 & XGMAC_RDES3_PKT_LENGTH_MASK;
	debug("%s: *packetp=%p, length=%d\n", __func__, *packetp, length);

	/*
	 * D299: upstream trusts this length completely, and it is not
	 * trustworthy. The field is 14 bits, so it can say up to 16383, while
	 * the buffer this descriptor owns is rx_stride - 2048 - bytes inside a
	 * heap allocation of exactly XGMAC_DESCRIPTORS_MAX * rx_stride. A
	 * descriptor closed as a context descriptor (CTXT) does not carry a
	 * packet length in those bits at all.
	 *
	 * Handing an over-long length to xgmac_inval_buffer() is not a harmless
	 * over-read: invalidate_dcache_range() *discards* the cache lines it
	 * covers, so a length that runs off the end of the buffer arena throws
	 * away whatever dirty lines the CPU is holding for the malloc heap that
	 * sits immediately after it. The heap then reads back stale, and the
	 * next free() unlinks a chunk whose fd pointer is zero. That is exactly
	 * d247's crash: "Synchronous Abort, esr 0x96000047, far 0x18", a write
	 * to NULL + 0x18, in free() called from the hush parser, with the
	 * faulting chunk at 0x8768a4b0 - just past rx_dma_buf's 16 KB arena.
	 *
	 * So bound it. A descriptor that reports more than its own buffer can
	 * hold has not delivered a packet we can use; recycle it and tell the
	 * stack nothing arrived. "setenv eth_rxlenraw 1" restores upstream's
	 * unchecked behaviour, with the invalidate still clamped - the crash is
	 * not worth reproducing on purpose.
	 */
	if (length > (int)xgmac->rx_stride ||
	    (rx_desc->des3 & XGMAC_RDES3_CTXT)) {
		printf("xgmac: RXD%u des3 0x%08x reports length %d for a %u-byte buffer%s - dropping\n",
		       xgmac->rx_desc_idx, rx_desc->des3, length,
		       xgmac->rx_stride,
		       (rx_desc->des3 & XGMAC_RDES3_CTXT) ? " (CTXT)" : "");
		if (!xgmac_env_on("eth_rxlenraw")) {
			xgmac_free_pkt(dev, *packetp, 0);
			return -EAGAIN;
		}
		length = xgmac->rx_stride;
	}

	xgmac->config->ops->xgmac_inval_buffer(*packetp, length);

	/*
	 * The whole open question is whether the host's ARP reply reaches this
	 * function at all. Printing every frame would flood the console at line
	 * rate, so count them all and print only ARP (ethertype 0x0806) in full:
	 * that is a handful of frames per ping even on a busy segment, and 42
	 * bytes covers the entire ARP packet - operation, sender and target
	 * addresses - so a reply that arrives and is then rejected upstairs
	 * looks different from one that never arrives.
	 */
	/*
	 * Counted unconditionally. Pass 49 ran with eth_regdump but not
	 * eth_rxdump and reported "rx frames delivered 0" while the recycle log
	 * proved twelve frames had gone up - because the increment sat inside
	 * the eth_rxdump gate. Only the hexdump is expensive; a counter is not,
	 * and a counter that lies is worse than no counter.
	 */
	xgmac_rx_frames++;
	/* D309: the two quantities that survived the ring-16 run unchanged. */
	if (length > 0) {
		xgmac_rx_bytes += (u64)length;
		if (length >= XGMAC_RX_FULL_FRAME_MIN)
			xgmac_rx_full_frames++;
	}
	if (length >= 14 && *packetp && ((u8 *)*packetp)[12] == 0x08 &&
	    ((u8 *)*packetp)[13] == 0x06)
		xgmac_rx_arp_frames++;

	if (xgmac_env_on("eth_rxdump")) {
		u8 *p = *packetp;
		int n = length < 42 ? length : 42;
		int k;

		if (length >= 14 && p[12] == 0x08 && p[13] == 0x06) {
			printf("xgmac: rx ARP #%u (RXD%u, len %d):",
			       xgmac_rx_arp_frames, xgmac->rx_desc_idx, length);
			for (k = 0; k < n; k++)
				printf("%s%02x", (k % 16) ? " " : "\n       ", p[k]);
			printf("\n");
		}
	}

	return length;
}

static int xgmac_free_pkt(struct udevice *dev, uchar *packet, int length)
{
	ulong rx_tail;
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 idx, idx_mask = xgmac->desc_per_cacheline - 1;
	uchar *packet_expected;
	struct xgmac_desc *rx_desc;

	debug("%s(packet=%p, length=%d)\n", __func__, packet, length);

	/* T-78: rescued frames are not in the ring; nothing to give back. */
	if (packet == xgmac_rescue_buf)
		return 0;

	packet_expected = xgmac->rx_dma_buf +
			  (xgmac->rx_desc_idx * xgmac->rx_stride);
	if (packet != packet_expected) {
		debug("%s: Unexpected packet (expected %p)\n", __func__,
		      packet_expected);
		return -EINVAL;
	}

	xgmac->config->ops->xgmac_inval_buffer(packet, length);

	if ((xgmac->rx_desc_idx & idx_mask) == idx_mask) {
		for (idx = xgmac->rx_desc_idx - idx_mask;
		     idx <= xgmac->rx_desc_idx;
		     idx++) {
			rx_desc = xgmac_get_desc(xgmac, idx, true);
			rx_desc->des0 = 0;
			/* Flush the cache to the memory */
			mb();
			xgmac->config->ops->xgmac_flush_desc(rx_desc);
			xgmac->config->ops->xgmac_inval_buffer(packet, length);
			rx_desc->des0 = lower_32_bits((ulong)(xgmac->rx_dma_buf +
						      (idx * xgmac->rx_stride)));
			rx_desc->des1 = upper_32_bits((ulong)(xgmac->rx_dma_buf +
						      (idx * xgmac->rx_stride)));
			rx_desc->des2 = 0;
			/*
			 * Make sure that if HW sees the _OWN write below,
			 * it will see all the writes to the rest of the
			 * descriptor too.
			 */
			mb();
			rx_desc->des3 = XGMAC_DESC3_OWN;
			xgmac->config->ops->xgmac_flush_desc(rx_desc);
		}
		/*
		 * D256: no tail write here. &RXD[idx+1] excluded the very
		 * descriptor xgmac_recv() blocks on next and hung d236's second
		 * ping; upstream's &RXD[idx] happens to be right but only ever
		 * lands every 4th packet. xgmac_rx_tail_track() below does it
		 * for every packet, from the software index, which is the only
		 * thing that knows what the driver is waiting for.
		 */
		if (xgmac_rx_upstream())
			writel((ulong)rx_desc,
			       &xgmac->dma_chan->ch0_rxdesc_tail_pointer);
		rx_tail = readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer);
		/*
		 * D179: pass 42's RxTail never moved, which is only consistent
		 * with this line not running. Print it unconditionally under
		 * eth_regdump so "did the recycle happen" stops being an
		 * inference from descriptor OWN bits.
		 */
		/*
		 * D288: this used to ride on eth_regdump, which is also what
		 * turns on the register dumps - so asking for the dumps bought
		 * a ~70 character line at 115200 baud, about 6 ms of blocking
		 * serial, for every single received frame. A 1514-byte frame
		 * takes 12 us on the wire. Under tftp that print, not the
		 * hardware, sets the pace of the receive loop, and it perturbs
		 * exactly the timing the remaining stall depends on. Give it
		 * its own switch so the dumps can be had without it:
		 * "setenv eth_rxlog 1".
		 */
		if (xgmac_env_on("eth_rxlog"))
			printf("xgmac: recycled RXD%u..%u, RxTail <- 0x%08lx, reads 0x%08x\n",
			       xgmac->rx_desc_idx - idx_mask, xgmac->rx_desc_idx,
			       rx_tail,
			       readl(&xgmac->dma_chan->ch0_rxdesc_tail_pointer));
	}

	xgmac->rx_desc_idx++;
	xgmac->rx_desc_idx %= xgmac->desc_rx;

	xgmac_rx_tail_track(xgmac);

	return 0;
}

static int xgmac_probe_resources_core(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	unsigned int desc_step;
	int ret;

	debug("%s(dev=%p):\n", __func__, dev);

	/*
	 * D301: allocate for the largest layout, always, and let
	 * xgmac_select_layout() pick the one actually in use at every
	 * xgmac_start().
	 *
	 * This used to read eth_desc64 and eth_buf1600 right here, and the
	 * comments claimed setting them "before the first network command of
	 * the boot" would take effect. That was wrong, and d247 proved it:
	 * a run with eth_desc64 set to 0 at the prompt still reported
	 * "desc_size 64" and "DSL=3 x 16 + 16" in the dump, because U-Boot
	 * probes ethernet devices from eth_initialize() during board init,
	 * long before the autoboot prompt exists. With CONFIG_ENV_IS_NOWHERE
	 * there is no saveenv either, so from the prompt those two knobs were
	 * simply unreachable - the eth_desc64 experiment of T-63 never ran.
	 *
	 * Nothing else needs them at probe time: desc_size and rx_stride are
	 * used arithmetically everywhere else (xgmac_get_desc(), the ring
	 * arming, xgmac_recv(), xgmac_free_pkt()), and only these two
	 * allocations depend on their size. Size both for the maximum -
	 * 64 bytes per descriptor and a 2048-byte receive stride - and any
	 * smaller layout simply uses less of the same buffer.
	 */
	desc_step = sizeof(struct xgmac_desc);
	xgmac->desc_size = ALIGN(desc_step, (unsigned int)ARCH_DMA_MINALIGN);
	xgmac->desc_per_cacheline = ARCH_DMA_MINALIGN / xgmac->desc_size;
	xgmac->rx_stride = XGMAC_RX_STRIDE_ALIGNED;
	xgmac->desc_tx = XGMAC_DESCRIPTORS_TX;
	xgmac->desc_rx = XGMAC_DESCRIPTORS_RX;

	xgmac->tx_descs = xgmac_alloc_descs(xgmac, XGMAC_DESCRIPTORS_MAX);
	if (!xgmac->tx_descs) {
		debug("%s: xgmac_alloc_descs(tx) failed\n", __func__);
		ret = -ENOMEM;
		goto err;
	}

	xgmac->rx_descs = xgmac_alloc_descs(xgmac, XGMAC_DESCRIPTORS_MAX);
	if (!xgmac->rx_descs) {
		debug("%s: xgmac_alloc_descs(rx) failed\n", __func__);
		ret = -ENOMEM;
		goto err_free_tx_descs;
	}

	xgmac->tx_dma_buf = memalign(XGMAC_BUFFER_ALIGN, XGMAC_MAX_PACKET_SIZE);
	if (!xgmac->tx_dma_buf) {
		debug("%s: memalign(tx_dma_buf) failed\n", __func__);
		ret = -ENOMEM;
		goto err_free_descs;
	}
	debug("%s: tx_dma_buf=%p\n", __func__, xgmac->tx_dma_buf);

	xgmac->rx_dma_buf = memalign(XGMAC_RX_STRIDE_ALIGNED,
				     XGMAC_RX_BUFFER_SIZE);
	if (!xgmac->rx_dma_buf) {
		debug("%s: memalign(rx_dma_buf) failed\n", __func__);
		ret = -ENOMEM;
		goto err_free_tx_dma_buf;
	}
	debug("%s: rx_dma_buf=%p\n", __func__, xgmac->rx_dma_buf);

	xgmac->rx_pkt = malloc(XGMAC_MAX_PACKET_SIZE);
	if (!xgmac->rx_pkt) {
		debug("%s: malloc(rx_pkt) failed\n", __func__);
		ret = -ENOMEM;
		goto err_free_rx_dma_buf;
	}
	debug("%s: rx_pkt=%p\n", __func__, xgmac->rx_pkt);

	xgmac->config->ops->xgmac_inval_buffer(xgmac->rx_dma_buf,
			XGMAC_RX_BUFFER_SIZE);

	debug("%s: OK\n", __func__);
	return 0;

err_free_rx_dma_buf:
	free(xgmac->rx_dma_buf);
err_free_tx_dma_buf:
	free(xgmac->tx_dma_buf);
err_free_descs:
	xgmac_free_descs(xgmac->rx_descs);
err_free_tx_descs:
	xgmac_free_descs(xgmac->tx_descs);
err:

	debug("%s: returns %d\n", __func__, ret);
	return ret;
}

static int xgmac_remove_resources_core(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	debug("%s(dev=%p):\n", __func__, dev);

	free(xgmac->rx_pkt);
	free(xgmac->rx_dma_buf);
	free(xgmac->tx_dma_buf);
	xgmac_free_descs(xgmac->rx_descs);
	xgmac_free_descs(xgmac->tx_descs);

	debug("%s: OK\n", __func__);
	return 0;
}

/* board-specific Ethernet Interface initializations. */
__weak int board_interface_eth_init(struct udevice *dev,
				    phy_interface_t interface_type)
{
	return 0;
}

static int xgmac_probe(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	int ret;

	debug("eth_xgmac: probe enter %s\n", dev->name);
	debug("%s(dev=%p):\n", __func__, dev);

	xgmac->dev = dev;
	xgmac->config = (void *)dev_get_driver_data(dev);
	debug("eth_xgmac: config=%p\n", xgmac->config);

	xgmac->regs = dev_read_addr(dev);
	debug("eth_xgmac: mac regs=%p\n", (void *)xgmac->regs);
	if (xgmac->regs == FDT_ADDR_T_NONE) {
		pr_err("%s dev_read_addr() failed\n", dev->name);
		return -ENODEV;
	}
	xgmac->mac_regs = (void *)(xgmac->regs + XGMAC_MAC_REGS_BASE);
	xgmac->mtl_regs = (void *)(xgmac->regs + XGMAC_MTL_REGS_BASE);
	xgmac->dma_regs = (void *)(xgmac->regs + XGMAC_DMA_REGS_BASE);
	/* Defaults; xgmac_select_chan_regs() re-evaluates these at start(). */
	xgmac->mtl_chan = (void *)(xgmac->regs + XGMAC_MTL_CHAN_BASE_DEFAULT);
	xgmac->dma_chan = (void *)(xgmac->regs + XGMAC_DMA_CHAN_BASE_DEFAULT);

	xgmac->max_speed = dev_read_u32_default(dev, "max-speed", 0);

	debug("eth_xgmac: core resources begin\n");
	ret = xgmac_probe_resources_core(dev);
	debug("eth_xgmac: core resources done ret=%d\n", ret);
	if (ret < 0) {
		pr_err("%s xgmac_probe_resources_core() failed: %d\n",
		       dev->name, ret);

		return ret;
	}

	debug("eth_xgmac: glue resources begin\n");
	ret = xgmac->config->ops->xgmac_probe_resources(dev);
	debug("eth_xgmac: glue resources done ret=%d\n", ret);
	if (ret < 0) {
		pr_err("%s xgmac_probe_resources() failed: %d\n",
		       dev->name, ret);

		goto err_remove_resources_core;
	}

	debug("eth_xgmac: start clocks begin\n");
	ret = xgmac->config->ops->xgmac_start_clks(dev);
	debug("eth_xgmac: start clocks done ret=%d\n", ret);
	if (ret < 0) {
		pr_err("%s xgmac_start_clks() failed: %d\n", dev->name, ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_DM_ETH_PHY))
		xgmac->mii = eth_phy_get_mdio_bus(dev);
	debug("eth_xgmac: mdio lookup done mii=%p\n", xgmac->mii);

	if (!xgmac->mii) {
		xgmac->mii = mdio_alloc();
		if (!xgmac->mii) {
			pr_err("%s mdio_alloc() failed\n", dev->name);
			ret = -ENOMEM;
			goto err_stop_clks;
		}
		xgmac->mii->read = xgmac_mdio_read;
		xgmac->mii->write = xgmac_mdio_write;
		xgmac->mii->priv = xgmac;
		strcpy(xgmac->mii->name, dev->name);

		ret = mdio_register(xgmac->mii);
		debug("eth_xgmac: mdio register done ret=%d\n", ret);
		if (ret < 0) {
			pr_err("%s mdio_register() failed: %d\n",
			       dev->name, ret);

			goto err_free_mdio;
		}
	}

	if (IS_ENABLED(CONFIG_DM_ETH_PHY))
		eth_phy_set_mdio_bus(dev, xgmac->mii);

	debug("%s: OK\n", __func__);
	return 0;

err_free_mdio:
	mdio_free(xgmac->mii);
err_stop_clks:
	xgmac->config->ops->xgmac_stop_clks(dev);
err_remove_resources_core:
	xgmac_remove_resources_core(dev);

	debug("%s: returns %d\n", __func__, ret);
	return ret;
}

static int xgmac_remove(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);

	debug("%s(dev=%p):\n", __func__, dev);

	mdio_unregister(xgmac->mii);
	mdio_free(xgmac->mii);
	xgmac->config->ops->xgmac_stop_clks(dev);
	xgmac->config->ops->xgmac_remove_resources(dev);

	xgmac_remove_resources_core(dev);

	debug("%s: OK\n", __func__);
	return 0;
}

int xgmac_null_ops(struct udevice *dev)
{
	return 0;
}

static const struct eth_ops xgmac_ops = {
	.start = xgmac_start,
	.stop = xgmac_stop,
	.send = xgmac_send,
	.recv = xgmac_recv,
	.free_pkt = xgmac_free_pkt,
	.write_hwaddr = xgmac_write_hwaddr,
	.read_rom_hwaddr = xgmac_read_rom_hwaddr,
};

static const struct udevice_id xgmac_ids[] = {
#if IS_ENABLED(CONFIG_DWC_ETH_XGMAC_SOCFPGA)
	{
		.compatible = "intel,socfpga-dwxgmac",
		.data = (ulong)&xgmac_socfpga_config
	},
#endif
#if IS_ENABLED(CONFIG_DWC_ETH_XGMAC_AMBOSELI)
	{
		.compatible = "qcom,echo-ethqos",
		.data = (ulong)&xgmac_amboseli_config
	},
	{
		.compatible = "qcom,sdx95-ethqos",
		.data = (ulong)&xgmac_amboseli_config
	},
#endif
	{ }
};

U_BOOT_DRIVER(eth_xgmac) = {
	.name = "eth_xgmac",
	.id = UCLASS_ETH,
	.of_match = of_match_ptr(xgmac_ids),
	.probe = xgmac_probe,
	.remove = xgmac_remove,
	.ops = &xgmac_ops,
	.priv_auto = sizeof(struct xgmac_priv),
	.plat_auto = sizeof(struct eth_pdata),
};
