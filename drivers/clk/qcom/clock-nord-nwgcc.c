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
#include <dt-bindings/clock/qcom,nord-nwgcc.h>
#include "clock-qcom.h"

static const struct gate_clk nord_nwgcc_clks[] = {
	GATE_CLK_POLLED(NW_GCC_CAMERA_TRIG_CLK,        0x16034, BIT(0), 0x16034),
	GATE_CLK_POLLED(NW_GCC_DISP_0_TRIG_CLK,        0x1801c, BIT(0), 0x1801c),
	GATE_CLK_POLLED(NW_GCC_DISP_1_TRIG_CLK,        0x1901c, BIT(0), 0x1901c),
	GATE_CLK_POLLED(NW_GCC_EVA_TRIG_CLK,           0x1b028, BIT(0), 0x1b028),
	GATE_CLK_POLLED(NW_GCC_GPU_2_GPLL0_CLK_SRC,    0x76000, BIT(6), 0x24150),
	GATE_CLK_POLLED(NW_GCC_GPU_2_GPLL0_DIV_CLK_SRC,0x76000, BIT(7), 0x24158),
	GATE_CLK_POLLED(NW_GCC_GPU_2_HSCNOC_GFX_CLK,   0x2400c, BIT(0), 0x2400c),
	GATE_CLK_POLLED(NW_GCC_GPU_GPLL0_CLK_SRC,      0x76000, BIT(4), 0x23150),
	GATE_CLK_POLLED(NW_GCC_GPU_GPLL0_DIV_CLK_SRC,  0x76000, BIT(5), 0x23158),
	GATE_CLK_POLLED(NW_GCC_GPU_SMMU_VOTE_CLK,      0x86038, BIT(0), 0x86038),
	GATE_CLK_POLLED(NW_GCC_MMU_1_TCU_VOTE_CLK,     0x86040, BIT(0), 0x86040),

	GATE_CLK(NW_GCC_ACMU_MUX_CLK,          0x1f01c, BIT(0)),
	GATE_CLK(NW_GCC_CAMERA_HF_AXI_CLK,     0x16008, BIT(0)),
	GATE_CLK(NW_GCC_CAMERA_SF_AXI_CLK,     0x1601c, BIT(0)),
	GATE_CLK(NW_GCC_DISP_0_HF_AXI_CLK,     0x18008, BIT(0)),
	GATE_CLK(NW_GCC_DISP_1_HF_AXI_CLK,     0x19008, BIT(0)),
	GATE_CLK(NW_GCC_DPRX0_AXI_HF_CLK,      0x29004, BIT(0)),
	GATE_CLK(NW_GCC_DPRX1_AXI_HF_CLK,      0x2a004, BIT(0)),
	GATE_CLK(NW_GCC_EVA_AXI0C_CLK,         0x1b01c, BIT(0)),
	GATE_CLK(NW_GCC_EVA_AXI0_CLK,          0x1b008, BIT(0)),
	GATE_CLK(NW_GCC_FRQ_MEASURE_REF_CLK,   0x1f008, BIT(0)),
	GATE_CLK(NW_GCC_GP1_CLK,               0x20000, BIT(0)),
	GATE_CLK(NW_GCC_GP2_CLK,               0x21000, BIT(0)),
	GATE_CLK(NW_GCC_GPU_HSCNOC_GFX_CLK,    0x2300c, BIT(0)),
	GATE_CLK(NW_GCC_HSCNOC_GPU_2_AXI_CLK,  0x24160, BIT(0)),
	GATE_CLK(NW_GCC_HSCNOC_GPU_AXI_CLK,    0x23160, BIT(0)),
	GATE_CLK(NW_GCC_VIDEO_AXI0C_CLK,       0x1a01c, BIT(0)),
	GATE_CLK(NW_GCC_VIDEO_AXI0_CLK,        0x1a008, BIT(0)),
	GATE_CLK(NW_GCC_VIDEO_AXI1_CLK,        0x1a030, BIT(0)),
};

static int nord_nwgcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, nord_nwgcc_clks[clk->id].name);

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static const struct qcom_reset_map nord_nwgcc_resets[] = {
	[NW_GCC_CAMERA_BCR] = { 0x16000 },
	[NW_GCC_DISPLAY_0_BCR] = { 0x18000 },
	[NW_GCC_DISPLAY_1_BCR] = { 0x19000 },
	[NW_GCC_DPRX0_BCR] = { 0x29000 },
	[NW_GCC_DPRX1_BCR] = { 0x2a000 },
	[NW_GCC_EVA_BCR] = { 0x1b000 },
	[NW_GCC_GPU_2_BCR] = { 0x24000 },
	[NW_GCC_GPU_BCR] = { 0x23000 },
	[NW_GCC_VIDEO_BCR] = { 0x1a000 },
};

static struct msm_clk_data nord_nwgcc_data = {
	.resets = nord_nwgcc_resets,
	.num_resets = ARRAY_SIZE(nord_nwgcc_resets),
	.clks = nord_nwgcc_clks,
	.num_clks = ARRAY_SIZE(nord_nwgcc_clks),

	.enable = nord_nwgcc_enable,
};

static const struct udevice_id gcc_nord_nwgcc_of_match[] = {
	{
		.compatible = "qcom,nord-nwgcc",
		.data = (ulong)&nord_nwgcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_nord_nwgcc) = {
	.name		= "gcc_nord_nwgcc",
	.id		= UCLASS_NOP,
	.of_match	= gcc_nord_nwgcc_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
