// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <linux/types.h>
#include <clk-uclass.h>
#include <dm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/qcom,nord-tcsrcc.h>
#include "clock-qcom.h"

#define TCXO_RATE	38400000

static const struct gate_clk nord_tcsrcc_clks[] = {
	GATE_CLK(TCSR_DP_RX_0_CLKREF_EN,     0xa008, BIT(0)),
	GATE_CLK(TCSR_DP_RX_1_CLKREF_EN,     0xb008, BIT(0)),
	GATE_CLK(TCSR_DP_TX_0_CLKREF_EN,     0xc008, BIT(0)),
	GATE_CLK(TCSR_DP_TX_1_CLKREF_EN,     0xd008, BIT(0)),
	GATE_CLK(TCSR_DP_TX_2_CLKREF_EN,     0xe008, BIT(0)),
	GATE_CLK(TCSR_DP_TX_3_CLKREF_EN,     0xf008, BIT(0)),
	GATE_CLK(TCSR_PCIE_CLKREF_EN,        0x8,    BIT(0)),
	GATE_CLK(TCSR_UFS_CLKREF_EN,         0x3008, BIT(0)),
	GATE_CLK(TCSR_USB2_0_CLKREF_EN,      0x4008, BIT(0)),
	GATE_CLK(TCSR_USB2_1_CLKREF_EN,      0x5008, BIT(0)),
	GATE_CLK(TCSR_USB2_2_CLKREF_EN,      0x6008, BIT(0)),
	GATE_CLK(TCSR_USB3_0_CLKREF_EN,      0x8008, BIT(0)),
	GATE_CLK(TCSR_USB3_1_CLKREF_EN,      0x7008, BIT(0)),
	GATE_CLK(TCSR_UX_SGMII_0_CLKREF_EN,  0x1008, BIT(0)),
	GATE_CLK(TCSR_UX_SGMII_1_CLKREF_EN,  0x2008, BIT(0)),
};

static struct msm_clk_data nord_tcsrcc_data = {
	.clks = nord_tcsrcc_clks,
	.num_clks = ARRAY_SIZE(nord_tcsrcc_clks),
};

static int nord_tcsrcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	return qcom_gate_clk_en(priv, clk->id);
}

static ulong nord_tcsrcc_get_rate(struct clk *clk)
{
	return TCXO_RATE;
}

static int nord_tcsrcc_probe(struct udevice *dev)
{
	struct msm_clk_data *data = (struct msm_clk_data *)dev_get_driver_data(dev);
	struct msm_clk_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->data = data;

	return 0;
}

static struct clk_ops nord_tcsrcc_clk_ops = {
	.enable = nord_tcsrcc_enable,
	.get_rate = nord_tcsrcc_get_rate,
};

static const struct udevice_id gcc_nord_tcsrcc_of_match[] = {
	{
		.compatible = "qcom,nord-tcsrcc",
		.data = (ulong)&nord_tcsrcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_nord_tcsrcc) = {
	.name		= "gcc_nord_tcsrcc",
	.id		= UCLASS_CLK,
	.of_match	= gcc_nord_tcsrcc_of_match,
	.ops		= &nord_tcsrcc_clk_ops,
	.priv_auto	= sizeof(struct msm_clk_priv),
	.probe		= nord_tcsrcc_probe,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
