// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <clk.h>
#include <dm.h>
#include <generic-phy.h>
#include <reset.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>

#define USB_PHY_UTMI_CTRL0		0x3c
#define SLEEPM				BIT(0)

#define USB_PHY_UTMI_CTRL5		0x50
#define POR				BIT(1)

#define USB_PHY_HS_PHY_CTRL_COMMON0	0x54
#define PHY_ENABLE			BIT(0)
#define SIDDQ_SEL			BIT(1)
#define SIDDQ				BIT(2)

#define USB_PHY_HS_PHY_CTRL2		0x64
#define USB2_SUSPEND_N			BIT(2)
#define USB2_SUSPEND_N_SEL		BIT(3)

#define USB_PHY_CFG0			0x94
#define UTMI_PHY_CMN_CTRL_OVERRIDE_EN	BIT(1)

#define USB_PHY_CFG1			0x154
#define PLL_EN				BIT(0)

#define USB_PHY_FSEL_SEL		0xb8
#define FSEL_SEL			BIT(0)

#define USB_PHY_XCFGI_39_32		0x16c
#define HSTX_PE				GENMASK(3, 2)

#define USB_PHY_XCFGI_71_64		0x17c
#define HSTX_SWING			GENMASK(3, 0)

#define USB_PHY_XCFGI_31_24		0x168
#define HSTX_SLEW			GENMASK(2, 0)

#define USB_PHY_XCFGI_7_0		0x15c
#define PLL_LOCK_TIME			GENMASK(1, 0)

struct m31_phy_tbl_entry {
	u32 off;
	u32 mask;
	u32 val;
};

#define M31_EUSB_PHY_INIT_CFG(o, m, v)	{ .off = o, .mask = m, .val = v }

static const struct m31_phy_tbl_entry m31_eusb2_setup_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG0, UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL5, POR, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL_COMMON0, PHY_ENABLE, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG1, PLL_EN, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_FSEL_SEL, FSEL_SEL, 1),
};

static const struct m31_phy_tbl_entry m31_eusb_phy_override_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_39_32, HSTX_PE, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_71_64, HSTX_SWING, 7),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_31_24, HSTX_SLEW, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_XCFGI_7_0, PLL_LOCK_TIME, 0),
};

static const struct m31_phy_tbl_entry m31_eusb_phy_reset_tbl[] = {
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N_SEL, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL0, SLEEPM, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL_COMMON0, SIDDQ_SEL, 1),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL_COMMON0, SIDDQ, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_UTMI_CTRL5, POR, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_HS_PHY_CTRL2, USB2_SUSPEND_N_SEL, 0),
	M31_EUSB_PHY_INIT_CFG(USB_PHY_CFG0, UTMI_PHY_CMN_CTRL_OVERRIDE_EN, 0),
};

struct m31_eusb2_phy_priv {
	void __iomem *base;
	struct clk ref_clk;
	struct clk ahb_clk;
	struct reset_ctl reset;
};

static int m31_eusb2_phy_write_seq(void __iomem *base,
				    const struct m31_phy_tbl_entry *tbl, int num)
{
	int i;
	u32 val;

	for (i = 0; i < num; i++, tbl++) {
		val = readl(base + tbl->off);
		val &= ~tbl->mask;
		val |= tbl->val << __ffs(tbl->mask);
		writel(val, base + tbl->off);
	}

	return 0;
}

static int m31_eusb2_phy_init(struct phy *phy)
{
	struct m31_eusb2_phy_priv *priv = dev_get_priv(phy->dev);
	int ret;

	ret = clk_enable(&priv->ref_clk);
	if (ret)
		return ret;

	ret = clk_enable(&priv->ahb_clk);
	if (ret)
		return ret;

	ret = reset_assert(&priv->reset);
	if (ret)
		return ret;

	udelay(10);

	ret = reset_deassert(&priv->reset);
	if (ret)
		return ret;

	m31_eusb2_phy_write_seq(priv->base, m31_eusb2_setup_tbl,
				 ARRAY_SIZE(m31_eusb2_setup_tbl));
	m31_eusb2_phy_write_seq(priv->base, m31_eusb_phy_override_tbl,
				 ARRAY_SIZE(m31_eusb_phy_override_tbl));
	m31_eusb2_phy_write_seq(priv->base, m31_eusb_phy_reset_tbl,
				 ARRAY_SIZE(m31_eusb_phy_reset_tbl));

	return 0;
}

static int m31_eusb2_phy_probe(struct udevice *dev)
{
	struct m31_eusb2_phy_priv *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_read_addr_name_ptr(dev, "eusb2_phy_base");
	if (!priv->base)
		return -EINVAL;

	ret = clk_get_by_name(dev, "ref", &priv->ref_clk);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "ahb", &priv->ahb_clk);
	if (ret)
		return ret;

	return reset_get_by_index(dev, 0, &priv->reset);
}

static struct phy_ops m31_eusb2_phy_ops = {
	.init = m31_eusb2_phy_init,
};

static const struct udevice_id m31_eusb2_phy_ids[] = {
	{ .compatible = "qcom,echo-m31-eusb2-phy" },
	{ }
};

U_BOOT_DRIVER(qcom_m31_eusb2_phy) = {
	.name		= "qcom-m31-eusb2-phy",
	.id		= UCLASS_PHY,
	.of_match	= m31_eusb2_phy_ids,
	.ops		= &m31_eusb2_phy_ops,
	.probe		= m31_eusb2_phy_probe,
	.priv_auto	= sizeof(struct m31_eusb2_phy_priv),
};
