// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <clk-uclass.h>
#include <dm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/bug.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/qcom,echo-tcsrcc.h>
#include "clock-qcom.h"

static const struct gate_clk echo_tcsr_clks[] = {
	GATE_CLK_POLLED(TCSR_ETH_0_CLKREF_EN, 0x9f78, BIT(0), 0x9f78),
	GATE_CLK_POLLED(TCSR_ETH_1_CLKREF_EN, 0x9f80, BIT(0), 0x9f80),
	GATE_CLK_POLLED(TCSR_ETH_2_CLKREF_EN, 0x9f88, BIT(0), 0x9f88),
	GATE_CLK_POLLED(TCSR_PCIE_1L_B_CLKREF_EN, 0x9f90, BIT(0), 0x9f90),
	GATE_CLK_POLLED(TCSR_PCIE_1L_CLKREF_EN, 0x9f98, BIT(0), 0x9f98),
	GATE_CLK_POLLED(TCSR_PCIE_2L_CLKREF_EN, 0x9fa8, BIT(0), 0x9fa8),
	GATE_CLK_POLLED(TCSR_PCIE_2L_SRIOV_CLKREF_EN, 0x9fa0, BIT(0), 0x9fa0),
	GATE_CLK_POLLED(TCSR_USB2_CLKREF_EN, 0x10, BIT(0), 0x10),
	GATE_CLK_POLLED(TCSR_USB3_CLKREF_EN, 0x0, BIT(0), 0x0),
};

static int echo_tcsr_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, echo_tcsr_clks[clk->id].name);

	return qcom_gate_clk_en(priv, clk->id);
}

static struct msm_clk_data echo_tcsr_data = {
	.clks = echo_tcsr_clks,
	.num_clks = ARRAY_SIZE(echo_tcsr_clks),

	.enable = echo_tcsr_enable,
};

static const struct udevice_id tcsrcc_echo_of_match[] = {
	{
		.compatible = "qcom,echo-tcsr",
		.data = (ulong)&echo_tcsr_data,
	},
	{ }
};

U_BOOT_DRIVER(tcsrcc_echo) = {
	.name		= "tcsrcc_echo",
	.id		= UCLASS_NOP,
	.of_match	= tcsrcc_echo_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
