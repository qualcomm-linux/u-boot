// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm ETHQOS glue for the Synopsys DWC XGMAC on Amboseli.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Target: emac0, USXGMII at 10G, on QMP5 V1.
 *
 * emac0 was chosen because it is the port actually enabled on the lab board:
 * reading the live device tree over adb shows emac0 status=okay with
 * phy-mode=usxgmii, while emac2 (the SFP+ port) is status=disabled and
 * only present on QMP5 V3/V4. Register layout is identical across instances -
 * in MAC space emac1 is +0x80000 and emac2 is +0x100000 from emac0
 * (0x22300000 / 0x22380000 / 0x22400000, vendor kernel DT, Ethernet, line 434,621,809).
 * The MAC core itself is drivers/net/dwc_eth_xgmac.c; this file is only the
 * SoC glue, modelled on dwc_eth_xgmac_socfpga.c.
 *
 * NB: PASS 2 - RESOURCES PLUS THE IO MACRO. Clocks, the GDSC, the PHY supply and
 * the PHY reset come up so MDIO is reachable, and xgmac_start_resets() now also
 * programs the IO macro before the DMA soft reset. See the note below - it
 * reverses
 * what pass 1 concluded about that window.
 *
 * NB: THE RGMII IO MACRO IS PROGRAMMED FROM HERE - AND THAT IS A REVERSAL.
 * emac0's second reg window, "rgmii" at 0x2237F000, is listed as XPU-locked:
 *   amboseli-le-2-0_amss_standard_oem/Amboseli_boot/boot_images/ssg/SecPkg/
 *   Library/ACConfigDataLib/amboseli/amboseliAccessControl.xml
 *     <PRTn start="0x2237f000" end="0x22380000"
 *           rdomains="HYP,TZ" wdomains="HYP,TZ">
 *       SECACCESS-3476 Pinnacles : RGMII IO MACRO CSR need to be given TZ
 *       permissions
 * On the strength of that, pass 1 concluded U-Boot could never touch it and
 * that an SCM/SMC route into TZ would be required.
 *
 * That conclusion was never tested, and the kernel contradicts it: the Linux
 * driver reaches the same window with a plain ioremap and plain accessors -
 *   dwmac-qcom-ethqos.c:1943  devm_platform_ioremap_resource_byname(pdev, "rgmii")
 *   dwmac-qcom-ethqos.c:485   readl(ethqos->rgmii_base + offset)
 *   dwmac-qcom-ethqos.c:491   writel(value, ethqos->rgmii_base + offset)
 * with no SCM call anywhere in the driver, and Ethernet works under Linux on
 * this silicon (926 Mbit/s iperf3, zero error counters). So non-secure EL1 does
 * have plain MMIO access at Linux runtime. TZ may or may not reprogram the XPU
 * before U-Boot runs; that is the open question this code answers empirically.
 *
 * So the wrapper access below READS BEFORE IT WRITES and bails out on an
 * all-ones read, and the trace prints immediately before the first access. If
 * the window really is locked at U-Boot time, the serial log pinpoints it.
 *
 * It is also deliberately reached only from xgmac_start_resets(), which runs on
 * the first network command - never during boot. A fault here cannot stop the
 * board from booting.
 *
 * Why this matters more than "the datapath won't pass traffic": the wrapper
 * holds RGMII_CONFIG_FUNC_CLK_EN and the USXGMII GMII clock mux, so without it
 * the MAC has no tx/rx clock and DMA_MODE.SWR can never self-clear. That is the
 * XGMAC_DMA_MODE_SWR stuck: -110 seen through ten hardware passes.
 *
 * NB: There is also a TZ-only 4K hole INSIDE the MAC window, at +0x7000
 * (0x22307000-0x22308000, rd=HYP,MSA,TME_FW,TZ). Never sweep or dump it.
 *
 * Hardware values are cited to file:line throughout, per the conventions in
 * clock-sdx95.c and pinctrl-sdx95.c.
 */

#include <blk.h>
#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <env.h>
#include <errno.h>
#include <eth_phy.h>
#include <log.h>
#include <malloc.h>
#include <miiphy.h>
#include <mmc.h>
#include <net.h>
#include <part.h>
#include <phy.h>
#include <power-domain.h>
#include <reset.h>
#include <string.h>
#include <vsprintf.h>
#include <asm/cache.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <power/regulator.h>

#include "dwc_eth_xgmac.h"
#include "serdes_fe11lm_v1_3_seq.h"
#include "wrapper_usxgmii_tbl.h"
#include "pcs_xpcs_qcom_amboseli.h"

/* SerDes reference clock enable, TCSR_ETH_0_CLKREF_EN. */
#define AMBOSELI_TCSR_ETH_0_CLKREF_EN	0x01fdefc4
#define AMBOSELI_TCSR_CLKREF_EN_BIT	BIT(0)

#ifndef SPEED_5000
#define SPEED_5000 5000
#endif

/* RGMII IO macro / EMAC wrapper register offsets, from emac0 reg index 1 ("rgmii", 0x2237F000). */
#define RGMII_IO_MACRO_CONFIG			0x000
#define SDCC_HC_REG_DLL_CONFIG			0x004
#define SDCC_TEST_CTL				0x008
#define SDCC_HC_REG_DDR_CONFIG			0x00C
#define SDCC_HC_REG_DLL_CONFIG2			0x010
#define SDCC_USR_CTL				0x018
#define RGMII_IO_MACRO_CONFIG2			0x01C
#define RGMII_IO_MACRO_SCRATCH_2		0x044
#define RGMII_IO_MACRO_BYPASS			0x16C
#define SDC4_STATUS				0x014
#define SDC4_STATUS_DLL_LOCK			BIT(7)
#define SDC4_STATUS_DDR_STATUS_DLL_LOCK		BIT(0)
#define EMAC_WRAPPER_SGMII_PHY_CNTRL0		0x170
#define EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4	0x174
#define EMAC_WRAPPER_USXGMII_DIV_CONFIG		0x1CC
#define EMAC_WRAPPER_USXGMII_MUX_SEL		0x1D0

/* Fields ethqos_set_func_clk_en() touches directly (dwmac-qcom-ethqos.c:691-696). */
#define RGMII_CONFIG_FUNC_CLK_EN			BIT(30)
#define SGMII_PHY_CNTRL1_SGMII_TX_TO_RX_LOOPBACK_EN	BIT(3)
#define SGMII_PHY_CNTRL1_USXGMII_GMII_MASTER_CLK_MUX_SEL BIT(4)

/* Linux writes them too in this mode - ethqos_configure_echo() only skips DLL registers when mode == MAC_MODE_RGMII (:1598-1600), which is not our case. */

static const char *const amboseli_wspd_name[AMBOSELI_WSPD_COUNT] = {
	"10G", "5G", "2.5G", "1G", "100M", "10M",
};

static bool eth_debug_on(void)
{
	const char *s = env_get("eth_debug");

	return s && strcmp(s, "0") && strcmp(s, "off");
}

#define eth_trace(fmt, ...) do { \
	if (eth_debug_on()) \
		debug("emac0: " fmt, ##__VA_ARGS__); \
} while (0)

/* Second tier, off by default: the per-register wrapper trace. */
static bool eth_verbose_on(void)
{
	const char *s = env_get("eth_verbose");

	return s && strcmp(s, "0") && strcmp(s, "off");
}

static bool eth_regdump_on(void)
{
	const char *s = env_get("eth_regdump");

	return s && strcmp(s, "0") && strcmp(s, "off");
}

/* Env-tunable microsecond value, so a timing question costs a boot, not a build. */
static u32 eth_env_us(const char *name, u32 dflt)
{
	const char *s = env_get(name);

	return s ? (u32)simple_strtoul(s, NULL, 10) : dflt;
}

/* snps,reset-delays-us = <pre assert post> - echo-eth-qmp5.dtsi:61 */
#define AMBOSELI_PHY_RESET_DELAY_CELLS	3

struct xgmac_amboseli_priv {
	struct clk_bulk clks;
	struct power_domain pd;
	bool pd_valid;
	/* emac0 reg index 1, "rgmii" - the IO macro / EMAC wrapper. */
	void __iomem *rgmii;
	void __iomem *serdes;
	/* "qcom-xpcs-handle" phandle target, pcs@22314000 - the Synopsys XPCS. */
	void __iomem *pcs;
	/* Set by the pcs_link_up op and consumed by fix_mac_speed, which runs later and would otherwise only have gen10g_startup()'s hardcoded SPEED_10000 to go on. */
	int xpcs_speed;
};

/* echo-eth-qmp5.dtsi:57-65 gives. */
static int xgmac_amboseli_reset_phy(struct udevice *dev)
{
	struct gpio_desc reset_gpio;
	u32 delays[AMBOSELI_PHY_RESET_DELAY_CELLS] = { 0, 10000, 10000 };
	int ret;

	ret = gpio_request_by_name(dev, "snps,reset-gpio", 0, &reset_gpio,
				   GPIOD_IS_OUT);
	if (ret) {
		/* No reset line described - not an error, the PHY may be strapped. */
		dev_dbg(dev, "no snps,reset-gpio (%d), skipping PHY reset\n", ret);
		return 0;
	}

	ret = dev_read_u32_array(dev, "snps,reset-delays-us", delays,
				 AMBOSELI_PHY_RESET_DELAY_CELLS);
	if (ret)
		dev_dbg(dev, "no snps,reset-delays-us, using defaults\n");

	if (delays[0])
		udelay(delays[0]);

	/* GPIOD_IS_OUT plus GPIO_ACTIVE_LOW in the DT means dm_gpio_set_value(1) drives the line low, i.e. */
	dm_gpio_set_value(&reset_gpio, 1);
	udelay(delays[1]);
	dm_gpio_set_value(&reset_gpio, 0);
	udelay(delays[2]);

	return 0;
}

static int xgmac_amboseli_enable_phy_supply(struct udevice *dev)
{
	struct udevice *reg;
	int ret;

	/* That is why DM_REGULATOR_FIXED is sufficient and no SPMI/RPMh support is needed for Ethernet on this SoC. */
	eth_trace("PHY supply lookup begin\n");
	ret = device_get_supply_regulator(dev, "vreg_emac_phy-supply", &reg);
	eth_trace("PHY supply lookup done ret=%d reg=%p\n", ret, reg);
	if (ret) {
		dev_dbg(dev, "no vreg_emac_phy-supply (%d), assuming always-on\n",
			ret);
		return 0;
	}

	/* regulator_common_set_enable() (drivers/power/regulator/regulator_common.c:78) returns -EALREADY when the rail is already on; it still bumps enable_count. */
	eth_trace("regulator enable call begin name=%s\n", reg->name);
	ret = regulator_set_enable(reg, true);
	eth_trace("regulator enable call done ret=%d\n", ret);
	if (ret == -EALREADY || ret == -ENOSYS || ret == -EACCES) {
		eth_trace("PHY supply already on (%d)\n", ret);
		return 0;
	}
	if (ret) {
		dev_err(dev, "failed to enable PHY supply: %d\n", ret);
		return ret;
	}

	eth_trace("PHY supply enabled\n");

	return 0;
}

/* Map the SerDes register window by following emac0's "phys" phandle to serdes0 (arch/arm/dts/sdx95-qmp5.dts: phy@1d80000, 0x1010 bytes). */
static void __iomem *xgmac_amboseli_map_phandle(struct udevice *dev,
						const char *prop,
						const char *label)
{
	ofnode node;
	fdt_addr_t addr;
	fdt_size_t size;
	void __iomem *base;

	node = ofnode_parse_phandle(dev_ofnode(dev), prop, 0);
	if (!ofnode_valid(node)) {
		dev_warn(dev, "no \"%s\" phandle; %s will not be configured\n",
			 prop, label);
		return NULL;
	}

	addr = ofnode_get_addr_size_index(node, 0, &size);
	if (addr == FDT_ADDR_T_NONE || !size) {
		dev_warn(dev, "%s node has no usable reg; not mapping\n", label);
		return NULL;
	}

	base = map_physmem(addr, size, MAP_NOCACHE);
	if (!base) {
		dev_warn(dev, "failed to map %s window at 0x%llx\n", label,
			 (unsigned long long)addr);
		return NULL;
	}

	eth_trace("%s window mapped at %p (0x%llx, %llu bytes)\n", label, base,
		  (unsigned long long)addr, (unsigned long long)size);
	return base;
}

static int xgmac_probe_resources_amboseli(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv;
	int ret;

	eth_trace("glue probe enter\n");
	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return -ENOMEM;
	xgmac->syscon_phy = priv;
	eth_trace("private state allocated\n");

	/* Only mapped here; it is not touched until xgmac_start_resets_amboseli(), which the core calls on the first network command. */
	eth_trace("mapping rgmii window\n");
	priv->rgmii = dev_remap_addr_name(dev, "rgmii");
	eth_trace("rgmii window mapped=%p\n", priv->rgmii);
	if (!priv->rgmii)
		dev_warn(dev, "no \"rgmii\" reg window; IO macro will not be configured\n");
	else
		eth_trace("IO macro window mapped at %p\n", priv->rgmii);

	eth_trace("mapping SerDes phandle\n");
	priv->serdes = xgmac_amboseli_map_phandle(dev, "phys", "SerDes");
	eth_trace("SerDes mapped=%p\n", priv->serdes);

	/* The Synopsys XPCS. */
	eth_trace("mapping XPCS phandle\n");
	priv->pcs = xgmac_amboseli_map_phandle(dev, "qcom-xpcs-handle", "XPCS");
	eth_trace("XPCS mapped=%p\n", priv->pcs);

	/* Order matters. */
	eth_trace("power domain get begin\n");
	ret = power_domain_get(dev, &priv->pd);
	eth_trace("power domain get done ret=%d\n", ret);
	if (ret) {
		/* Amboseli's emac0 GDSC is GCC_EMAC0_GDSC (clock-sdx95.c, offset 0x61004). */
		dev_err(dev, "failed to get power domain: %d\n", ret);
		goto err_free;
	}
	priv->pd_valid = true;
	eth_trace("stage 1/6: power domain acquired\n");

	eth_trace("power domain on begin\n");
	ret = power_domain_on(&priv->pd);
	eth_trace("power domain on done ret=%d\n", ret);
	if (ret) {
		dev_err(dev, "failed to power on EMAC GDSC: %d\n", ret);
		goto err_free;
	}
	eth_trace("stage 2/6: EMAC GDSC on\n");

	/* All 13 branch clocks at once. */
	eth_trace("clock bulk get begin\n");
	ret = clk_get_bulk(dev, &priv->clks);
	eth_trace("clock bulk get done ret=%d count=%u\n", ret,
		  priv->clks.count);
	if (ret) {
		dev_err(dev, "failed to get clocks: %d\n", ret);
		goto err_free;
	}

	eth_trace("stage 3/6: %d clocks from DT\n", priv->clks.count);

	eth_trace("clock bulk enable begin\n");
	ret = clk_enable_bulk(&priv->clks);
	eth_trace("clock bulk enable done ret=%d\n", ret);
	if (ret) {
		dev_err(dev, "failed to enable clocks: %d\n", ret);
		goto err_free;
	}
	eth_trace("stage 4/6: %d clocks enabled\n", priv->clks.count);

	/* SerDes reference clock - lives in TCSR, not GCC. See the note above. */
	setbits_le32((void *)AMBOSELI_TCSR_ETH_0_CLKREF_EN,
		     AMBOSELI_TCSR_CLKREF_EN_BIT);

	eth_trace("PHY regulator enable begin\n");
	ret = xgmac_amboseli_enable_phy_supply(dev);
	eth_trace("PHY regulator enable done ret=%d\n", ret);
	if (ret)
		goto err_free;

	eth_trace("PHY reset begin\n");
	ret = xgmac_amboseli_reset_phy(dev);
	eth_trace("PHY reset done ret=%d\n", ret);
	if (ret)
		goto err_free;

	/* hw_feature0 (offset 0x11c) is a read-only capability register: on a powered, clocked MAC it is non-zero. */
	eth_trace("MAC feature read begin\n");
	{
		u32 hw_feature0 = readl(&xgmac->mac_regs->hw_feature0);
		u32 hw_feature1 = readl(&xgmac->mac_regs->hw_feature1);

		eth_trace("MAC feature read done hw0=0x%08x hw1=0x%08x\n",
			  hw_feature0, hw_feature1);
	}

	/* The core driver still has work to do after this returns (mdio_alloc/mdio_register). */
	eth_trace("stage 6/6: resources up (regs=0x%llx, phy-mode=%d) - core takes over\n",
		  (unsigned long long)xgmac->regs, dev_read_phy_mode(dev));

	return 0;

err_free:
	free(priv);
	xgmac->syscon_phy = NULL;
	return ret;
}

static int xgmac_remove_resources_amboseli(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv = xgmac->syscon_phy;

	if (!priv)
		return 0;

	clk_disable_bulk(&priv->clks);

	/* The consequence is that a second probe sees -EALREADY, which xgmac_amboseli_enable_phy_supply() treats as success. */
	free(priv);
	xgmac->syscon_phy = NULL;

	return 0;
}

/* partition_emmc_ext.xml declares it with filename="". */
#if CONFIG_IS_ENABLED(MMC) && CONFIG_IS_ENABLED(BLK)
static int amboseli_mac_from_art(u8 *enetaddr)
{
	struct disk_partition info;
	struct blk_desc *dev_desc;
	struct mmc *mmc;
	u8 *buf;
	int part;

	mmc = find_mmc_device(0);
	if (!mmc || mmc_init(mmc))
		return -ENODEV;

	dev_desc = blk_get_dev("mmc", 0);
	if (!dev_desc || dev_desc->type == DEV_TYPE_UNKNOWN)
		return -ENODEV;

	part = part_get_info_by_name(dev_desc, "0:ART", &info);
	if (part < 0) {
		eth_trace("no \"0:ART\" partition on mmc 0 (%d)\n", part);
		return -ENOENT;
	}

	/* blk_dread() works in whole blocks, and the MAC is the first 6 bytes of the partition. */
	buf = memalign(ARCH_DMA_MINALIGN, info.blksz);
	if (!buf)
		return -ENOMEM;

	if (blk_dread(dev_desc, info.start, 1, buf) != 1) {
		eth_trace("short read from \"0:ART\"\n");
		free(buf);
		return -EIO;
	}

	/* Validate BEFORE copying. */
	if (!is_valid_ethaddr(buf)) {
		eth_trace("\"0:ART\" holds no valid MAC (%pM) - partition is "
			  "probably still unpopulated (filename=\"\")\n", buf);
		free(buf);
		return -ENXIO;
	}

	memcpy(enetaddr, buf, ARP_HLEN);
	free(buf);

	return 0;
}
#else
static inline int amboseli_mac_from_art(u8 *enetaddr)
{
	return -ENOSYS;
}
#endif

/* - CONFIG_ETH_MAX_MAC is already 3, NOT 0 - the Ethernet branch fixed that, so common.c:2111 would request a sensible 18 bytes. */
static int xgmac_get_enetaddr_amboseli(struct udevice *dev)
{
	struct eth_pdata *pdata = dev_get_plat(dev);
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	u32 hi_addr, lo_addr;

	/* This is the LAST glue callback the core makes while bringing the device up: eth_post_probe() (net/eth-uclass.c) calls it through ->read_rom_hwaddr() after xgmac. */
	eth_trace("LOADED: %s ready - MDIO bus '%s'\n",
		  dev->name, xgmac->mii ? xgmac->mii->name : "(none)");

	/* Safe to read here: eth_post_probe() runs after xgmac_probe(), so mac_regs points into the live, clocked window that stage 5/6 already proved responsive. */
	hi_addr = readl(&xgmac->mac_regs->address0_high);
	lo_addr = readl(&xgmac->mac_regs->address0_low);

	/* Byte order is the XGMAC's, not the wire's: address0_low holds bytes 0-3 and address0_high holds bytes 4-5 in its low half. */
	pdata->enetaddr[0] = lo_addr & 0xff;
	pdata->enetaddr[1] = (lo_addr >> 8) & 0xff;
	pdata->enetaddr[2] = (lo_addr >> 16) & 0xff;
	pdata->enetaddr[3] = (lo_addr >> 24) & 0xff;
	pdata->enetaddr[4] = hi_addr & 0xff;
	pdata->enetaddr[5] = (hi_addr >> 8) & 0xff;

	if (!is_valid_ethaddr(pdata->enetaddr)) {
		/* Try 0:ART before giving up - see amboseli_mac_from_art() for why this is safe to attempt even though the partition currently ships empty. */
		if (!amboseli_mac_from_art(pdata->enetaddr)) {
			eth_trace("MAC address %pM from \"0:ART\"\n",
				  pdata->enetaddr);

			return 0;
		}

		/* Zero it rather than leaving the rejected bytes in place. */
		eth_trace("no MAC in MAC_ADDRESS0 (high=0x%08x low=0x%08x) - "
			  "core will assign a random address\n",
			  hi_addr, lo_addr);
		memset(pdata->enetaddr, 0, ARP_HLEN);

		return -ENXIO;
	}

	eth_trace("MAC address %pM from MAC_ADDRESS0 (high=0x%08x low=0x%08x)\n",
		  pdata->enetaddr, hi_addr, lo_addr);

	return 0;
}

/* 5 ms is far longer than a 115200-baud FIFO needs. */
/* T-24 made the diagnostics opt-in, which dropped this from udelay(5000) plus two blocking UART lines per register (~13 ms) to udelay(100). */
static void wrapper_mark(const char *what, u32 offset)
{
	if (eth_verbose_on())
		eth_trace("wrapper: >>> %s +0x%03x\n", what, offset);

	udelay(eth_env_us("eth_wrapper_delay", 5000));
}

/* Against that lead, note the table sets PDN in SDCC_HC_REG_DLL_CONFIG (0x2004642c, bit 29) and leaves DLL_EN clear. */
static void wrapper_dll_status(struct xgmac_amboseli_priv *priv, const char *when)
{
	u32 v;

	if (!priv || !priv->rgmii)
		return;

	v = readl(priv->rgmii + SDC4_STATUS);
	eth_trace("wrapper: SDC4_STATUS %s 0x%08x  DLL_LOCK=%d DDR_DLL_LOCK=%d\n",
		  when, v, !!(v & SDC4_STATUS_DLL_LOCK),
		  !!(v & SDC4_STATUS_DDR_STATUS_DLL_LOCK));
}

/* Note this deliberately UNDOES two bits the table above just wrote: the table is the steady-state 1G configuration. */
static void xgmac_amboseli_set_func_clk_en(void __iomem *rgmii)
{
	setbits_le32(rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4,
		     SGMII_PHY_CNTRL1_SGMII_TX_TO_RX_LOOPBACK_EN);
	clrbits_le32(rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4,
		     SGMII_PHY_CNTRL1_USXGMII_GMII_MASTER_CLK_MUX_SEL);
	setbits_le32(rgmii + RGMII_IO_MACRO_CONFIG, RGMII_CONFIG_FUNC_CLK_EN);
}

/* dwc_eth_xgmac.c calls this at :559, immediately before the DMA_MODE.SWR wait at :567. */
/* Pass 17 settled that the IO macro is programmable from U-Boot and that FUNC_CLK_EN plus the SGMII tx->rx loopback is not enough on its own: XGMAC_DMA_MODE_SWR s. */
static int xgmac_amboseli_serdes_powerup(struct udevice *dev,
					 struct xgmac_amboseli_priv *priv)
{
#if AMBOSELI_SERDES_SEQ_AVAILABLE
	int i, written = 0;

	/* USXGMII maps to USX10G at every speed. */
	for (i = 0; i < ARRAY_SIZE(serdes_fe11lm_v1_3_seq); i++) {
		const struct serdes_reg_per_mode *r = &serdes_fe11lm_v1_3_seq[i];
		u32 val = r->value[SERDES_MODE_USX10G];

		if (val == SERDES_SKIP)
			continue;

		writel(val, priv->serdes + r->reg);
		written++;
	}

	/* Linux polls four ready bits at this point. */
	mdelay(10);

	eth_trace("serdes: USX10G sequence applied, %d of %d entries written\n",
		  written, (int)ARRAY_SIZE(serdes_fe11lm_v1_3_seq));
	if (eth_regdump_on()) {
		printf("emac0: SerDes programmed USX10G entries=%d/%d "
		       "[0110]=0x%02x [0174]=0x%02x [019c]=0x%02x "
		       "[0e00]=0x%02x [0e04]=0x%02x [0e08]=0x%02x\n",
		       written, (int)ARRAY_SIZE(serdes_fe11lm_v1_3_seq),
		       readl(priv->serdes + 0x0110) & 0xff,
		       readl(priv->serdes + 0x0174) & 0xff,
		       readl(priv->serdes + 0x019c) & 0xff,
		       readl(priv->serdes + 0x0e00) & 0xff,
		       readl(priv->serdes + 0x0e04) & 0xff,
		       readl(priv->serdes + 0x0e08) & 0xff);
	}
	return 0;
#else
	eth_trace("serdes: no sequence table in this build - see drivers/net/serdes_fe11lm_v1_3_seq.h\n");
	return -ENOSYS;
#endif
}

/* Unknown speeds fall back to 1G, the column the DMA reset is known to survive, rather than to whatever column index a bogus speed would compute to. */
static int amboseli_wrapper_col(int speed)
{
	const char *override = env_get("eth_speed");

	/* eth_speed sweeps columns by hand when the negotiated speed is in doubt: "setenv eth_speed 10000" (or 5000/2500/1000/100/10). */
	if (override)
		speed = simple_strtol(override, NULL, 10);

	switch (speed) {
	case SPEED_10:
		return AMBOSELI_WSPD_10;
	case SPEED_100:
		return AMBOSELI_WSPD_100;
	case SPEED_1000:
		return AMBOSELI_WSPD_1000;
	case SPEED_2500:
		return AMBOSELI_WSPD_2500;
	case SPEED_5000:
		return AMBOSELI_WSPD_5000;
	case SPEED_10000:
		return AMBOSELI_WSPD_10000;
	default:
		eth_trace("wrapper: no column for %d Mbps, using 1G\n", speed);
		return AMBOSELI_WSPD_1000;
	}
}

/* --------------------------------------------------------------------------- Pass 23 read the external PHY's MMDs directly and got: MMD 1 PMA/PMD LINK UP the med. */

static u32 xpcs_rd(struct xgmac_amboseli_priv *priv, u32 reg)
{
	return readl(priv->pcs + reg);
}

static void xpcs_wr(struct xgmac_amboseli_priv *priv, u32 reg, u32 val)
{
	writel(val, priv->pcs + reg);
}

static void xgmac_amboseli_xpcs_program_dump(struct xgmac_amboseli_priv *priv,
					     const char *tag)
{
	if (!eth_regdump_on() || !priv || !priv->pcs)
		return;

	printf("emac0: XPCS %s CTRL2=0x%08x DIG_CTRL1=0x%08x "
	       "KR_CTRL=0x%08x MMD_CTRL=0x%08x\n", tag,
	       xpcs_rd(priv, DW_SR_MII_PCS_CTRL2),
	       xpcs_rd(priv, DW_VR_MII_PCS_DIG_CTRL1),
	       xpcs_rd(priv, DW_VR_MII_PCS_KR_CTRL),
	       xpcs_rd(priv, DW_SR_MII_MMD_CTRL));
	printf("emac0: XPCS %s STS1=0x%08x STS2=0x%08x AN_INTR_STS=0x%08x\n",
	       tag, xpcs_rd(priv, DW_SR_MII_PCS_STS1),
	       xpcs_rd(priv, DW_SR_XS_PCS_STS2),
	       xpcs_rd(priv, DW_VR_MII_AN_INTR_STS));
}

/* qcom_xpcs_poll_reset() (:239) uses 32 retries of usleep_range(1000, 2000) and returns -ETIMEDOUT if the field is still set. */
static int xpcs_poll_reset(struct xgmac_amboseli_priv *priv, u32 reg, u32 field)
{
	unsigned int retries = AMBOSELI_XPCS_RESET_RETRIES;
	u32 val;

	do {
		mdelay(1);
		val = xpcs_rd(priv, reg);
	} while ((val & field) && --retries);

	return (val & field) ? -ETIMEDOUT : 0;
}

/* xpcs_soft_reset() (:255). */
static int xgmac_amboseli_xpcs_soft_reset(struct xgmac_amboseli_priv *priv)
{
	int ret;

	xpcs_wr(priv, DW_SR_MII_PCS_CTRL1,
		xpcs_rd(priv, DW_SR_MII_PCS_CTRL1) | SOFT_RST);

	ret = xpcs_poll_reset(priv, DW_SR_MII_PCS_CTRL1, SW_RST_BIT_STATUS);
	if (ret) {
		eth_trace("xpcs: SR_MII_PCS_CTRL1 soft reset stuck (0x%08x)\n",
			  xpcs_rd(priv, DW_SR_MII_PCS_CTRL1));
		return ret;
	}

	xpcs_wr(priv, DW_VR_MII_DIG_CTRL1,
		xpcs_rd(priv, DW_VR_MII_DIG_CTRL1) | SOFT_RST);

	ret = xpcs_poll_reset(priv, DW_VR_MII_DIG_CTRL1, SW_RST_BIT_STATUS);
	if (ret) {
		eth_trace("xpcs: VR_MII_DIG_CTRL1 soft reset stuck (0x%08x)\n",
			  xpcs_rd(priv, DW_VR_MII_DIG_CTRL1));
		return ret;
	}

	return 0;
}

/* qcom_xpcs_select_mode() (:950). */
static void xgmac_amboseli_xpcs_select_mode(struct xgmac_amboseli_priv *priv)
{
	u32 val;

	val = xpcs_rd(priv, DW_SR_MII_PCS_CTRL2);
	xpcs_wr(priv, DW_SR_MII_PCS_CTRL2, val & ~PCS_TYPE_SEL_10GBR);

	val = xpcs_rd(priv, DW_VR_MII_PCS_DIG_CTRL1);
	xpcs_wr(priv, DW_VR_MII_PCS_DIG_CTRL1, val | DW_USXGMII_EN);

	val = xpcs_rd(priv, DW_VR_MII_PCS_KR_CTRL);
	xpcs_wr(priv, DW_VR_MII_PCS_KR_CTRL, val & ~USXG_MODE_SEL);

	eth_trace("xpcs: USXGMII mode selected (CTRL2 0x%08x, DIG_CTRL1 0x%08x, KR_CTRL 0x%08x)\n",
		  xpcs_rd(priv, DW_SR_MII_PCS_CTRL2),
		  xpcs_rd(priv, DW_VR_MII_PCS_DIG_CTRL1),
		  xpcs_rd(priv, DW_VR_MII_PCS_KR_CTRL));
}

/* qcom_xpcs_loopback() (:632) - the DW_LBE RX-clock loopback. */
static void xgmac_amboseli_xpcs_loopback(struct xgmac_amboseli_priv *priv,
					 bool on)
{
	u32 val = xpcs_rd(priv, DW_SR_MII_MMD_CTRL);

	if (on)
		val |= DW_LBE;
	else
		val &= ~DW_LBE;

	xpcs_wr(priv, DW_SR_MII_MMD_CTRL, val);
}

/* qcom_xpcs_create() (:1316), minus the platform-device and phylink plumbing: check the device ID, soft-reset, select USXGMII, enable the RX-clock loopback. */
static int xgmac_amboseli_xpcs_init(struct udevice *dev,
				    struct xgmac_amboseli_priv *priv)
{
	u32 id;
	int ret;

	/* xpcs_get_id() (:1130) reads DEV_ID1 into the high half and DEV_ID2 into the low. */
	id = (xpcs_rd(priv, DW_SR_MII_PCS_DEV_ID1) << 16) |
	     (xpcs_rd(priv, DW_SR_MII_PCS_DEV_ID2) & 0xffff);

	eth_trace("xpcs: device ID 0x%08x (expect 0x%08x)\n", id,
		  SYNOPSYS_XPCS_ID);

	if (id == 0xffffffff || !id) {
		dev_err(dev, "XPCS window not answering (ID 0x%08x); skipping\n",
			id);
		return -ENODEV;
	}

	if ((id & SYNOPSYS_XPCS_MASK) != SYNOPSYS_XPCS_ID) {
		/* Not fatal on purpose. */
		eth_trace("xpcs: unexpected device ID, continuing anyway\n");
	}

	ret = xgmac_amboseli_xpcs_soft_reset(priv);
	if (ret)
		return ret;

	xgmac_amboseli_xpcs_select_mode(priv);

	/* The MAC needs an RX clock before the PHY can supply one, and the DMA soft reset that follows will not self-clear without it. */
	xgmac_amboseli_xpcs_loopback(priv, true);
	xgmac_amboseli_xpcs_program_dump(priv, "after-init");

	eth_trace("xpcs: init done (MMD_CTRL 0x%08x, PCS_STS1 0x%08x)\n",
		  xpcs_rd(priv, DW_SR_MII_MMD_CTRL),
		  xpcs_rd(priv, DW_SR_MII_PCS_STS1));

	return 0;
}

/* Map a speed in Mbps onto the SR_MII_MMD_CTRL USXGMII speed-select bits. */
static u32 xgmac_amboseli_xpcs_ss(int speed)
{
	switch (speed) {
	case SPEED_10000:
		return DW_USXGMII_10000;
	case SPEED_5000:
		return DW_USXGMII_5000;
	case SPEED_2500:
		return DW_GMII_2500;
	case SPEED_1000:
		return DW_GMII_1000;
	case SPEED_100:
		return DW_GMII_100;
	case SPEED_10:
	default:
		/* 10 Mbps is all three select bits clear. */
		return 0;
	}
}

/* qcom_xpcs_configure_an_params() (:668). */
static void xgmac_amboseli_xpcs_an_params(struct xgmac_amboseli_priv *priv,
					  bool an)
{
	u32 val;

	val = xpcs_rd(priv, DW_SR_MII_MMD_CTRL);
	xpcs_wr(priv, DW_SR_MII_MMD_CTRL, val & ~AN_CL37_EN);

	if (!an)
		return;

	val = xpcs_rd(priv, DW_VR_MII_AN_INTR_STS);
	xpcs_wr(priv, DW_VR_MII_AN_INTR_STS, val & ~DW_VR_MII_ANCMPLT_INTR);

	val = xpcs_rd(priv, DW_VR_MII_AN_CTRL);
	val |= DW_VR_MII_TX_CONFIG_PHY_SIDE << DW_VR_MII_AN_CTRL_TX_CONFIG_SHIFT;
	val |= DW_VR_MII_SGMII_LINK_STS;
	val &= ~DW_VR_MII_CTRL;
	val &= ~DW_VR_MII_AN_INTR_EN;
	xpcs_wr(priv, DW_VR_MII_AN_CTRL, val);
}

/* qcom_xpcs_wait_an_complete() (:706), polling branch: set AN_CL37_EN and wait up to an_timeout_ms for the AN-complete latch, then clear it. */
static int xgmac_amboseli_xpcs_wait_an(struct xgmac_amboseli_priv *priv)
{
	u32 sts = 0;
	int i;

	xpcs_wr(priv, DW_SR_MII_MMD_CTRL,
		xpcs_rd(priv, DW_SR_MII_MMD_CTRL) | AN_CL37_EN);

	for (i = 0; i < AMBOSELI_XPCS_AN_TIMEOUT_MS; i++) {
		sts = xpcs_rd(priv, DW_VR_MII_AN_INTR_STS);
		if (sts & DW_VR_MII_ANCMPLT_INTR)
			break;
		mdelay(1);
	}

	if (!(sts & DW_VR_MII_ANCMPLT_INTR)) {
		eth_trace("xpcs: autonegotiation did not complete in %d ms (AN_INTR_STS 0x%08x)\n",
			  AMBOSELI_XPCS_AN_TIMEOUT_MS, sts);

		/* Pass 26 hit exactly that: DIG_CTRL1 0x00002600 with bit 10 stuck, on the very same hardware where pass 25's AN-disabled run cleared it immediately. */
		xpcs_wr(priv, DW_SR_MII_MMD_CTRL,
			xpcs_rd(priv, DW_SR_MII_MMD_CTRL) & ~AN_CL37_EN);

		return -ETIMEDOUT;
	}

	eth_trace("xpcs: autonegotiation complete in %d ms (AN_INTR_STS 0x%08x)\n",
		  i, sts);

	xpcs_wr(priv, DW_VR_MII_AN_INTR_STS, sts & ~DW_VR_MII_ANCMPLT_INTR);

	return 0;
}

/* The AN word carries the speed the link actually came up at, which is worth more here than in Linux: this port's PHY is bound to the generic 10G driver. */
static int xgmac_amboseli_xpcs_negotiated_speed(struct xgmac_amboseli_priv *priv)
{
	u32 sts = xpcs_rd(priv, DW_VR_MII_AN_INTR_STS);
	int speed;
	u32 val;

	if (!(sts & DW_VR_MII_USXG_ANSGM_SP_LNKSTS)) {
		eth_trace("xpcs: AN word says link down (AN_INTR_STS 0x%08x)\n",
			  sts);
		return -ENETDOWN;
	}

	switch ((sts & DW_VR_MII_USXG_ANSGM_SP) >> DW_VR_MII_USXG_ANSGM_SP_SHIFT) {
	case DW_VR_MII_USXG_ANSGM_SP_10G:
		speed = SPEED_10000;
		break;
	case DW_VR_MII_USXG_ANSGM_SP_5G:
		speed = SPEED_5000;
		break;
	case DW_VR_MII_USXG_ANSGM_SP_2P5G:
		speed = SPEED_2500;
		break;
	case DW_VR_MII_USXG_ANSGM_SP_1000:
		speed = SPEED_1000;
		break;
	case DW_VR_MII_USXG_ANSGM_SP_100:
		speed = SPEED_100;
		break;
	case DW_VR_MII_USXG_ANSGM_SP_10:
		speed = SPEED_10;
		break;
	default:
		eth_trace("xpcs: invalid negotiated speed code (AN_INTR_STS 0x%08x)\n",
			  sts);
		return -EINVAL;
	}

	/* Program the negotiated rate back into the speed-select field. */
	val = xpcs_rd(priv, DW_SR_MII_MMD_CTRL) & ~DW_USXGMII_SS_MASK;
	xpcs_wr(priv, DW_SR_MII_MMD_CTRL,
		val | xgmac_amboseli_xpcs_ss(speed));

	eth_trace("xpcs: negotiated %d Mbps\n", speed);

	return speed;
}

/* Pass 25's run 1 enabled USXGMII Clause-37 AN and it never completed. */
static int xgmac_amboseli_xpcs_c37_hint(struct xgmac_amboseli_priv *priv)
{
	u32 sts = xpcs_rd(priv, DW_VR_MII_AN_INTR_STS);
	int speed;

	switch ((sts & DW_VR_MII_AN_STS_C37_ANSGM_SP) >>
		DW_VR_MII_AN_STS_C37_ANSGM_SP_SHIFT) {
	case DW_VR_MII_C37_ANSGM_SP_1000:
		speed = SPEED_1000;
		break;
	case DW_VR_MII_C37_ANSGM_SP_100:
		speed = SPEED_100;
		break;
	case DW_VR_MII_C37_ANSGM_SP_10:
		speed = SPEED_10;
		break;
	default:
		return 0;
	}

	eth_trace("xpcs: in-band hint %d Mbps %s duplex (AN_INTR_STS 0x%08x)\n",
		  speed,
		  sts & DW_VR_MII_AN_STS_C37_ANSGM_FD ? "full" : "half", sts);

	return speed;
}

/* Poll SR_MII_PCS_STS1 bit 2 (Receive Link Up). */
static void xgmac_amboseli_xpcs_wait_rlu(struct xgmac_amboseli_priv *priv)
{
	u32 sts = 0;
	int i;

	for (i = 0; i < AMBOSELI_XPCS_RLU_TIMEOUT_MS; i++) {
		sts = xpcs_rd(priv, DW_SR_MII_PCS_STS1);
		if (sts & DW_SR_XS_PCS_STS1_RLU)
			break;
		mdelay(1);
	}

	if (sts & DW_SR_XS_PCS_STS1_RLU)
		eth_trace("xpcs: RLU - receive link up in %d ms (PCS_STS1 0x%08x)\n",
			  i, sts);
	else
		eth_trace("xpcs: RLU never asserted in %d ms (PCS_STS1 0x%08x)\n",
			  AMBOSELI_XPCS_RLU_TIMEOUT_MS, sts);
}

/* Each one re-inits the whole chain, and each costs up to AMBOSELI_XPCS_AN_TIMEOUT_MS. */
/* Same shape as xgmac_env_off() in the core driver, which is not exported. */
static bool xgmac_env_off_local(const char *name)
{
	const char *s = env_get(name);

	return s && (!strcmp(s, "0") || !strcmp(s, "off"));
}

static int xgmac_amboseli_an_retries(void)
{
	const char *s = env_get("eth_an_retry");

	if (!s)
		return 3;

	return simple_strtoul(s, NULL, 10);
}

/* qcom_xpcs_link_up() (:1061) + qcom_xpcs_link_up_usxgmii() (:859), USXGMII only. */
static int xgmac_amboseli_xpcs_link_up(struct udevice *dev,
				       struct xgmac_amboseli_priv *priv,
				       int speed, bool an)
{
	int negotiated = speed;
	int ret;
	u32 val;

	/* Drop the RX-clock loopback: by now the external PHY has a media link (pass 23 confirmed MMD 1 up) and supplies the clock itself. */
	xgmac_amboseli_xpcs_loopback(priv, false);

	xgmac_amboseli_xpcs_select_mode(priv);

	xgmac_amboseli_xpcs_an_params(priv, an);

	/* Program the requested speed before AN, as the kernel does. */
	val = xpcs_rd(priv, DW_SR_MII_MMD_CTRL) & ~DW_USXGMII_SS_MASK;
	xpcs_wr(priv, DW_SR_MII_MMD_CTRL, val | xgmac_amboseli_xpcs_ss(speed));

	if (an) {
		int tries = xgmac_amboseli_an_retries() + 1;
		bool deep = !xgmac_env_off_local("eth_reinit");
		int try;

		/* AN state on its own has never predicted the outcome (a run has failed with AN complete in 99 ms and passed with it complete in 5 ms). */
		for (try = 0; try < tries; try++) {
			if (try) {
				eth_trace("xpcs: retrying autonegotiation (attempt %d of %d)%s\n",
					  try + 1, tries,
					  deep ? " after a full SerDes + XPCS re-init"
					       : "");

				/* Later measurement: restarting the negotiation on its own has now failed three times out of three in a single run (pass 55). */
				if (deep) {
					xgmac_amboseli_xpcs_init(dev, priv);
					xgmac_amboseli_serdes_powerup(dev, priv);
					xgmac_amboseli_xpcs_loopback(priv, false);
					xgmac_amboseli_xpcs_select_mode(priv);
				}

				xgmac_amboseli_xpcs_an_params(priv, an);
				val = xpcs_rd(priv, DW_SR_MII_MMD_CTRL) &
				      ~DW_USXGMII_SS_MASK;
				xpcs_wr(priv, DW_SR_MII_MMD_CTRL,
					val | xgmac_amboseli_xpcs_ss(speed));
			}

			ret = xgmac_amboseli_xpcs_wait_an(priv);
			if (!ret) {
				ret = xgmac_amboseli_xpcs_negotiated_speed(priv);
				break;
			}
		}

		if (ret > 0)
			negotiated = ret;
		else
			eth_trace("xpcs: no usable AN result after %d attempt(s) - keeping the forced %d Mbps\n",
				  tries, speed);
	}

	/* Post-AN reset sequence. */
	udelay(1);

	xgmac_amboseli_xpcs_wait_rlu(priv);

	xpcs_wr(priv, DW_VR_MII_PCS_DIG_CTRL1,
		xpcs_rd(priv, DW_VR_MII_PCS_DIG_CTRL1) | DW_USXGMII_RST);

	ret = xpcs_poll_reset(priv, DW_VR_MII_PCS_DIG_CTRL1, DW_USXGMII_RST);
	if (ret) {
		dev_err(dev, "XPCS USXGMII reset stuck (DIG_CTRL1 0x%08x)\n",
			xpcs_rd(priv, DW_VR_MII_PCS_DIG_CTRL1));
		return ret;
	}

	eth_trace("xpcs: link up sequence complete at %d Mbps\n", negotiated);
	xgmac_amboseli_xpcs_program_dump(priv, "after-link-up");

	return negotiated;
}

/* Env gate, same shape as eth_wrapper / eth_serdes. */
static int xgmac_amboseli_xpcs_mode(void)
{
	const char *s = env_get("eth_xpcs");

	if (s && (!strcmp(s, "0") || !strcmp(s, "off")))
		return 0;
	/* s is NULL whenever eth_xpcs is unset, which since that pass is the default and therefore the common path. */
	if (s && !strcmp(s, "2"))
		return 2;
	return 1;
}

/* gen10g_startup() sets phydev->speed = SPEED_10000 unconditionally - its own comment says "For now just lie and say it's 10G all the time". */
static int xgmac_amboseli_adjust_phy_speed(struct udevice *dev,
					   struct xgmac_amboseli_priv *priv,
					   struct phy_device *phydev)
{
	if (!priv->xpcs_speed)
		return 0;

	if (phydev->speed != priv->xpcs_speed) {
		eth_trace("phy: driver reported %d Mbps, XPCS says %d - using the XPCS\n",
			  phydev->speed, priv->xpcs_speed);
		phydev->speed = priv->xpcs_speed;
	}

	/* USXGMII carries full duplex only. */
	phydev->duplex = DUPLEX_FULL;

	return 0;
}

/* The second dhcp, with the media link already up from the first, cleared the same reset in 1 ms and completed USXGMII autonegotiation in 5 ms. */
static int xgmac_amboseli_pcs_adjust_phy(struct udevice *dev,
					 struct phy_device *phydev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv = xgmac->syscon_phy;
	const char *s;
	int speed;

	if (!priv)
		return 0;

	if (!xgmac_amboseli_xpcs_mode() || !priv->pcs)
		return 0;

	/* Deciding it here rather than in the link-up sequence is what lets the wrapper be reprogrammed before the XPCS comes up. */
	speed = xgmac_amboseli_xpcs_c37_hint(priv);
	if (!speed)
		speed = SPEED_1000;

	s = env_get("eth_speed");
	if (s)
		speed = simple_strtol(s, NULL, 10);

	priv->xpcs_speed = speed;

	return xgmac_amboseli_adjust_phy_speed(dev, priv, phydev);
}

/* Write one speed column of the wrapper table. */
static void xgmac_amboseli_wrapper_write(struct xgmac_amboseli_priv *priv,
					 int col)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amboseli_wrapper_usxgmii); i++) {
		const struct amboseli_wrapper_reg *r =
			&amboseli_wrapper_usxgmii[i];
		u32 want = r->value[col];
		u32 val;

		wrapper_mark("write", r->offset);
		writel(want, priv->rgmii + r->offset);
		val = readl(priv->rgmii + r->offset);
		if (eth_verbose_on() || val != want)
			eth_trace("wrapper: +0x%03x <- 0x%08x, reads 0x%08x%s\n",
				  r->offset, want, val,
				  val == want ? "" : "  MISMATCH");
	}

	/* See wrapper_mark(): total-settle knob, separate from the per-register one. */
	udelay(eth_env_us("eth_wrapper_post_delay", 0));
	wrapper_dll_status(priv, "after table");
	if (eth_regdump_on())
		printf("emac0: wrapper after-table "
		       "[000]=0x%08x [170]=0x%08x [174]=0x%08x "
		       "[1cc]=0x%08x [1d0]=0x%08x\n",
		       readl(priv->rgmii + RGMII_IO_MACRO_CONFIG),
		       readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL0),
		       readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4),
		       readl(priv->rgmii + EMAC_WRAPPER_USXGMII_DIV_CONFIG),
		       readl(priv->rgmii + EMAC_WRAPPER_USXGMII_MUX_SEL));
}

static int xgmac_start_resets_amboseli(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv = xgmac->syscon_phy;
	const char *mode = env_get("eth_wrapper");
	const char *serdes;
	u32 val;
	int ret;

	/* Why it was off: CONFIG_BOOTCOMMAND is "bootflow scan; ..." (amboseli_mmc_defconfig:434), and bootflow scan walks the ethernet bootdev. */
	if (mode && (!strcmp(mode, "0") || !strcmp(mode, "off"))) {
		eth_trace("wrapper: skipped - eth_wrapper is %s\n", mode);
		return 0;
	}

	/* Default is the full sequence; "read" still stops after one read. */
	if (!mode)
		mode = "write";

	if (!priv || !priv->rgmii) {
		dev_err(dev, "IO macro window not mapped; cannot configure USXGMII\n");
		return 0;
	}

	/* If the XPU still blocks non-secure EL1 at U-Boot time, this is the access that proves it, and the marker is already on the wire when it happens. */
	wrapper_mark("read RGMII_IO_MACRO_CONFIG", RGMII_IO_MACRO_CONFIG);
	val = readl(priv->rgmii + RGMII_IO_MACRO_CONFIG);
	eth_trace("wrapper: RGMII_IO_MACRO_CONFIG = 0x%08x (POR 0x00c04d03)\n", val);

	if (val == 0xffffffff) {
		dev_err(dev, "IO macro reads all-ones - window not accessible, skipping USXGMII config\n");
		return 0;
	}

	if (!strcmp(mode, "read")) {
		eth_trace("wrapper: read-only mode, stopping here\n");
		return 0;
	}

	xgmac_amboseli_wrapper_write(priv, AMBOSELI_WSPD_1000);

	wrapper_mark("set_func_clk_en", EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4);
	xgmac_amboseli_set_func_clk_en(priv->rgmii);
	if (eth_regdump_on())
		printf("emac0: wrapper after-func-clk "
		       "[000]=0x%08x [174]=0x%08x\n",
		       readl(priv->rgmii + RGMII_IO_MACRO_CONFIG),
		       readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4));

	eth_trace("wrapper: 1G config + func_clk_en/loopback applied (0x174 = 0x%08x, expect 0x0000000e)\n",
		  readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4));

	/* Linux reaches the equivalent point from stmmac's hw_setup, which is also after the wrapper's probe-time ethqos_configure() and before init_dma_engine. */
	if (!xgmac_amboseli_xpcs_mode()) {
		eth_trace("xpcs: skipped - eth_xpcs is 0\n");
	} else if (!priv->pcs) {
		dev_err(dev, "XPCS window not mapped; cannot initialise it\n");
	} else {
		ret = xgmac_amboseli_xpcs_init(dev, priv);
		if (ret)
			dev_err(dev, "XPCS init failed: %d\n", ret);
	}

	/* It was opt-in for 38 passes because it is the step most likely to hang a board that is not ready for it; the USX10G sequence is now validated 87 entries for 87. */
	serdes = env_get("eth_serdes");
	if (serdes && (!strcmp(serdes, "0") || !strcmp(serdes, "off"))) {
		eth_trace("serdes: skipped - eth_serdes is %s\n", serdes);
		return 0;
	}

	if (!priv->serdes) {
		dev_err(dev, "SerDes window not mapped; cannot power it up\n");
		return 0;
	}

	xgmac_amboseli_serdes_powerup(dev, priv);

	return 0;
}

/* Enabling the loopback also *clears* USXGMII_GMII_MASTER_CLK_MUX_SEL (:686-688) and disabling it does not put that bit back; what restores it is the table write. */
static int xgmac_amboseli_fix_mac_speed(struct udevice *dev, int speed)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv = xgmac->syscon_phy;
	const char *mode = env_get("eth_wrapper");
	int col;

	/* Same gate as the pre-link pass: if that was skipped, skip this too. */
	if (mode && (!strcmp(mode, "0") || !strcmp(mode, "off") ||
		     !strcmp(mode, "read")))
		return 0;

	if (!priv || !priv->rgmii)
		return 0;

	/* gen10g_startup() hardcodes SPEED_10000 - "For now just lie and say it's 10G all the time". */
	if (priv->xpcs_speed && priv->xpcs_speed != speed) {
		eth_trace("wrapper: core says %d Mbps, XPCS negotiated %d - using the XPCS\n",
			  speed, priv->xpcs_speed);
		speed = priv->xpcs_speed;
	}

	col = amboseli_wrapper_col(speed);

	eth_trace("wrapper: link up at %d Mbps, reprogramming for %s\n",
		  speed, amboseli_wspd_name[col]);

	wrapper_mark("clear tx-to-rx loopback", EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4);
	clrbits_le32(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4,
		     SGMII_PHY_CNTRL1_SGMII_TX_TO_RX_LOOPBACK_EN);

	xgmac_amboseli_wrapper_write(priv, col);

	/* "setenv eth_loopback 1" keeps the wrapper's tx-to-rx loopback on through the data phase. */
	if (env_get("eth_loopback")) {
		setbits_le32(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4,
			     SGMII_PHY_CNTRL1_SGMII_TX_TO_RX_LOOPBACK_EN);
		eth_trace("wrapper: tx-to-rx loopback HELD ON for diagnosis (0x174 = 0x%08x)\n",
			  readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4));
	}

	eth_trace("wrapper: %s config applied (0x174 = 0x%08x, expect 0x00000016)\n",
		  amboseli_wspd_name[col],
		  readl(priv->rgmii + EMAC_WRAPPER_SGMII_PHY_CNTRL1_V4));

	/* The wrapper table drives the EMAC's clock mux and dividers for the negotiated rate. */
	if (xgmac_amboseli_xpcs_mode() && priv->pcs) {
		int negotiated = xgmac_amboseli_xpcs_link_up(dev, priv, speed,
					xgmac_amboseli_xpcs_mode() == 1);

		if (negotiated < 0)
			dev_err(dev, "XPCS link up failed: %d\n", negotiated);
		else if (negotiated != speed)
			eth_trace("wrapper: NB: XPCS came up at %d Mbps but the wrapper is programmed for %d - setenv eth_speed %d and retry\n",
				  negotiated, speed, negotiated);
	}

	return 0;
}

/* SR_XS_PCS_STS2's TX_FLT is the first register in this port that can say so directly. */
static void xgmac_amboseli_pcs_dump(struct udevice *dev)
{
	struct xgmac_priv *xgmac = dev_get_priv(dev);
	struct xgmac_amboseli_priv *priv = xgmac->syscon_phy;
	u32 sts1, sts2, dig_sts, an_sts, mmd_ctrl;

	if (!priv || !priv->pcs)
		return;

	sts1     = xpcs_rd(priv, DW_SR_MII_PCS_STS1);
	sts2     = xpcs_rd(priv, DW_SR_XS_PCS_STS2);
	dig_sts  = xpcs_rd(priv, DW_VR_XS_PCS_DIG_STS);
	an_sts   = xpcs_rd(priv, DW_VR_MII_AN_INTR_STS);
	mmd_ctrl = xpcs_rd(priv, DW_SR_MII_MMD_CTRL);

	printf("  XPCS STS1 0x%08x RLU=%d FLT=%d | STS2 0x%08x TX_FLT=%d RX_FLT=%d\n",
	       sts1, !!(sts1 & DW_SR_XS_PCS_STS1_RLU),
	       !!(sts1 & DW_SR_XS_PCS_STS1_FLT), sts2,
	       !!(sts2 & DW_SR_XS_PCS_STS2_TX_FLT),
	       !!(sts2 & DW_SR_XS_PCS_STS2_RX_FLT));
	printf("  XPCS DIG_STS 0x%08x (raw, PSEQ undecoded) | AN_INTR_STS 0x%08x | MMD_CTRL 0x%08x\n",
	       dig_sts, an_sts, mmd_ctrl);
	printf("  XPCS STS3 0x%08x LSTS 0x%08x MMD_STS 0x%08x ICG_ERRCNT1 0x%08x\n",
	       xpcs_rd(priv, DW_SR_MII_PCS_STS3),
	       xpcs_rd(priv, DW_SR_MII_PCS_LSTS),
	       xpcs_rd(priv, DW_SR_MII_MMD_STS),
	       xpcs_rd(priv, DW_VR_MII_PCS_ICG_ERRCNT1));

	/* COM status sits in 0x0100..0x01fc and the PCS block starts at 0x0e00 (the table's own SERDES_PCS_* offsets); the mapping is 0x1010 bytes. */
	if (priv->serdes) {
		u32 off, v;

		printf("  SerDes COM 0x100..0x1fc nonzero:");
		for (off = 0x100; off <= 0x1fc; off += 4) {
			v = readl(priv->serdes + off);
			if (v)
				printf(" [%03x]=%02x", off, v & 0xff);
		}
		printf("\n  SerDes PCS 0xe00..0x100c nonzero:");
		for (off = 0xe00; off <= 0x100c; off += 4) {
			v = readl(priv->serdes + off);
			if (v)
				printf(" [%03x]=%02x", off, v & 0xff);
		}
		printf("\n");
	}
}

static struct xgmac_ops xgmac_amboseli_ops = {
	.xgmac_inval_desc = xgmac_inval_desc_generic,
	.xgmac_flush_desc = xgmac_flush_desc_generic,
	.xgmac_inval_buffer = xgmac_inval_buffer_generic,
	.xgmac_flush_buffer = xgmac_flush_buffer_generic,
	.xgmac_probe_resources = xgmac_probe_resources_amboseli,
	.xgmac_remove_resources = xgmac_remove_resources_amboseli,
	.xgmac_stop_resets = xgmac_null_ops,
	.xgmac_start_resets = xgmac_start_resets_amboseli,
	.xgmac_stop_clks = xgmac_null_ops,
	.xgmac_start_clks = xgmac_null_ops,
	.xgmac_calibrate_pads = xgmac_null_ops,
	.xgmac_disable_calibration = xgmac_null_ops,
	.xgmac_get_enetaddr = xgmac_get_enetaddr_amboseli,
	.xgmac_fix_mac_speed = xgmac_amboseli_fix_mac_speed,
	.xgmac_pcs_adjust_phy = xgmac_amboseli_pcs_adjust_phy,
	.xgmac_pcs_dump = xgmac_amboseli_pcs_dump,
};

/* axi_bus_width: emac0 declares dma-coherent and a 64-bit AXI master in the kernel DT, so XGMAC_AXI_WIDTH_64 matches. */
struct xgmac_config __maybe_unused xgmac_amboseli_config = {
	.reg_access_always_ok = false,
	/* emac0_phy declares compatible = "ethernet-phy-ieee802.3-c45". */
	.phy_force_c45 = true,
	/* The global MTL/DMA registers stay at 0x1000/0x3000 - the 0x3000 DMA_MODE/SWR path is hardware-proven. */
	.mtl_chan_base = 0x8000,
	.dma_chan_base = 0x8500,
	.swr_wait = 50,
	.config_mac = XGMAC_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB,
	.config_mac_mdio = XGMAC_MAC_MDIO_ADDRESS_CR_350_400,
	.axi_bus_width = XGMAC_AXI_WIDTH_64,
	.interface = dev_read_phy_mode,
	.ops = &xgmac_amboseli_ops,
};
