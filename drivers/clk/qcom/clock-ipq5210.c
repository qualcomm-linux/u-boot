// SPDX-License-Identifier: GPL-2.0
/*
 * Clock drivers for Qualcomm IPQ5210
 *
 * (C) Copyright 2024 Linaro Ltd.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <clk-uclass.h>
#include <dm.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/bug.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/qcom,ipq5210-gcc.h>
#include <dt-bindings/reset/qcom,ipq5210-gcc.h>
#include "clock-qcom.h"

static ulong ipq5210_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	/*
	 * Since GATE_CLK is deprecated and GATE_CLK_POLLED is being used
	 * use 'cbcr_reg' instead of 'reg'.
	 */
	switch (clk->id) {
	case GCC_QUPV3_WRAP_SE1_CLK:
		clk_rcg_set_rate_mnd(priv->base, priv->data->clks[clk->id].cbcr_reg,
				     0, 2, 217, CFG_CLK_SRC_GPLL0, 16);
		break;
	case GCC_SDCC1_AHB_CLK:
		break;
	case GCC_SDCC1_APPS_CLK:
		clk_rcg_set_rate_mnd(priv->base, priv->data->clks[clk->id].cbcr_reg,
				     0, 6, 25, CFG_CLK_SRC_GPLL0, 16);
		break;
	default:
		return -EINVAL;
	}

	return rate;
}

static const struct gate_clk ipq5210_clks[] = {
	GATE_CLK_POLLED(GCC_QUPV3_WRAP_SE1_CLK,	0x05020, BIT(0),	0x05004),
	GATE_CLK_POLLED(GCC_SDCC1_AHB_CLK,	0x3303c, BIT(0),	0x3303c),
	GATE_CLK_POLLED(GCC_SDCC1_APPS_CLK,	0x3302c, BIT(0),	0x33004),
	GATE_CLK_POLLED(GCC_IM_SLEEP_CLK,	0x34020, BIT(0),	0x34020),
};

static int ipq5210_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks <= clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return -ENOENT;
	}

	debug("%s: clk %s\n", __func__, ipq5210_clks[clk->id].name);

	return qcom_gate_clk_en(priv, clk->id);
}

static const struct qcom_reset_map ipq5210_gcc_resets[] = {
	[GCC_SDCC_BCR]			= {0x33000, 0},
	[GCC_USB0_PHY_BCR]		= {0x2c06c, 0},
	[GCC_USB3PHY_0_PHY_BCR]		= {0x2c070, 0},
	[GCC_QUSB2_0_PHY_BCR]		= {0x2c068, 0},
	[GCC_USB_BCR]			= {0x2c000, 0},
};

static struct msm_clk_data ipq5210_gcc_data = {
	.resets = ipq5210_gcc_resets,
	.num_resets = ARRAY_SIZE(ipq5210_gcc_resets),
	.clks = ipq5210_clks,
	.num_clks = ARRAY_SIZE(ipq5210_clks),
	.enable = ipq5210_enable,
	.set_rate = ipq5210_set_rate,
};

static const struct udevice_id gcc_ipq5210_of_match[] = {
	{
		.compatible = "qcom,ipq5210-gcc",
		.data = (ulong)&ipq5210_gcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_ipq5210) = {
	.name		= "gcc_ipq5210",
	.id		= UCLASS_NOP,
	.of_match	= gcc_ipq5210_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
