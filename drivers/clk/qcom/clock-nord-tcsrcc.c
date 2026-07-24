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

static int nord_tcsrcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, nord_tcsrcc_clks[clk->id].name);

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static struct msm_clk_data nord_tcsrcc_data = {
	.clks = nord_tcsrcc_clks,
	.num_clks = ARRAY_SIZE(nord_tcsrcc_clks),

	.enable = nord_tcsrcc_enable,
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
	.id		= UCLASS_NOP,
	.of_match	= gcc_nord_tcsrcc_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
