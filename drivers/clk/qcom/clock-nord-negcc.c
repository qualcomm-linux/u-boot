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
#include <linux/bug.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/qcom,nord-negcc.h>
#include "clock-qcom.h"

#define NE_GCC_QUPV3_WRAP2_S1_CLK_CMD_RCGR	0x382a8

/*
 * negcc-nord.c calls qcom_branch_set_force_mem_core() on
 * NE_GCC_UFS_PHY_ICE_CORE_CLK and NE_GCC_UFS_PHY_AXI_CLK. This U-Boot clock
 * framework has no FORCE_MEM_CORE_ON primitive (confirmed absent from
 * clock-qcom.h/.c) — flagged as a YELLOW residual risk, not worked around.
 */

static ulong nord_negcc_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (clk->id < priv->data->num_clks)
		debug("%s: %s, requested rate=%ld\n", __func__,
		      priv->data->clks[clk->id].name, rate);

	switch (clk->id) {
	case NE_GCC_QUPV3_WRAP2_S1_CLK:
		WARN(rate != 14745600,
		     "Unexpected rate for NE_GCC_QUPV3_WRAP2_S1_CLK: %lu\n", rate);
		clk_rcg_set_rate_mnd(priv->base, NE_GCC_QUPV3_WRAP2_S1_CLK_CMD_RCGR,
				     1, 384, 15625, CFG_CLK_SRC_GPLL0, 16);
		return rate;
	default:
		return 0;
	}
}

static const struct gate_clk nord_negcc_clks[] = {
	GATE_CLK_POLLED(NE_GCC_AGGRE_NOC_UFS_PHY_AXI_CLK,  0x330f4, BIT(0),  0x330f4),
	GATE_CLK_POLLED(NE_GCC_AGGRE_NOC_USB2_AXI_CLK,     0x31068, BIT(0),  0x31068),
	GATE_CLK_POLLED(NE_GCC_AGGRE_NOC_USB3_PRIM_AXI_CLK,0x2a098, BIT(0),  0x2a098),
	GATE_CLK_POLLED(NE_GCC_AGGRE_NOC_USB3_SEC_AXI_CLK, 0x2c098, BIT(0),  0x2c098),
	GATE_CLK_POLLED(NE_GCC_AHB2PHY_CLK,                0x30004, BIT(0),  0x30004),
	GATE_CLK_POLLED(NE_GCC_CNOC_USB2_AXI_CLK,          0x31064, BIT(0),  0x31064),
	GATE_CLK_POLLED(NE_GCC_CNOC_USB3_PRIM_AXI_CLK,     0x2a094, BIT(0),  0x2a094),
	GATE_CLK_POLLED(NE_GCC_CNOC_USB3_SEC_AXI_CLK,      0x2c094, BIT(0),  0x2c094),
	GATE_CLK_POLLED(NE_GCC_GPU_2_HSCNOC_GFX_CLK,       0x34014, BIT(0),  0x34014),
	GATE_CLK_POLLED(NE_GCC_GPU_2_SMMU_VOTE_CLK,        0x57028, BIT(0),  0x57028),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_CORE_2X_CLK,    0x57008, BIT(1),  0x38020),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_CORE_CLK,       0x57008, BIT(0),  0x3800c),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_M_AHB_CLK,      0x57000, BIT(30), 0x38004),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S0_CLK,         0x57008, BIT(2),  0x3815c),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S1_CLK,         0x57008, BIT(3),  0x38298),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S2_CLK,         0x57008, BIT(4),  0x383d4),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S3_CLK,         0x57008, BIT(5),  0x38510),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S4_CLK,         0x57008, BIT(6),  0x3864c),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S5_CLK,         0x57008, BIT(7),  0x38788),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S6_CLK,         0x57008, BIT(8),  0x388c4),
	GATE_CLK_POLLED(NE_GCC_QUPV3_WRAP2_S_AHB_CLK,      0x57000, BIT(31), 0x38008),
	GATE_CLK_POLLED(NE_GCC_UFS_PHY_AHB_CLK,            0x33028, BIT(0),  0x33028),
	GATE_CLK_POLLED(NE_GCC_UFS_PHY_AXI_CLK,            0x33018, BIT(0),  0x33018),
	GATE_CLK_POLLED(NE_GCC_UFS_PHY_ICE_CORE_CLK,       0x3307c, BIT(0),  0x3307c),
	GATE_CLK_POLLED(NE_GCC_UFS_PHY_PHY_AUX_CLK,        0x330bc, BIT(0),  0x330bc),
	GATE_CLK_POLLED(NE_GCC_UFS_PHY_UNIPRO_CORE_CLK,    0x3306c, BIT(0),  0x3306c),
	GATE_CLK_POLLED(NE_GCC_USB31_PRIM_ATB_CLK,         0x2a018, BIT(0),  0x2a018),
	GATE_CLK_POLLED(NE_GCC_USB31_PRIM_EUD_AHB_CLK,     0x2a02c, BIT(0),  0x2a02c),
	GATE_CLK_POLLED(NE_GCC_USB31_SEC_ATB_CLK,          0x2c018, BIT(0),  0x2c018),
	GATE_CLK_POLLED(NE_GCC_USB31_SEC_EUD_AHB_CLK,      0x2c02c, BIT(0),  0x2c02c),

	GATE_CLK(NE_GCC_FRQ_MEASURE_REF_CLK,      0x20008, BIT(0)),
	GATE_CLK(NE_GCC_GP1_CLK,                  0x21000, BIT(0)),
	GATE_CLK(NE_GCC_GP2_CLK,                  0x22000, BIT(0)),
	GATE_CLK(NE_GCC_GPU_2_GPLL0_CLK_SRC,      0x57000, BIT(0)),
	GATE_CLK(NE_GCC_GPU_2_GPLL0_DIV_CLK_SRC,  0x57000, BIT(0)),
	GATE_CLK(NE_GCC_SDCC4_APPS_CLK,           0x18004, BIT(0)),
	GATE_CLK(NE_GCC_SDCC4_AXI_CLK,            0x18014, BIT(0)),
	GATE_CLK(NE_GCC_UFS_PHY_RX_SYMBOL_0_CLK,  0x33030, BIT(0)),
	GATE_CLK(NE_GCC_UFS_PHY_RX_SYMBOL_1_CLK,  0x330d8, BIT(0)),
	GATE_CLK(NE_GCC_UFS_PHY_TX_SYMBOL_0_CLK,  0x3302c, BIT(0)),
	GATE_CLK(NE_GCC_USB20_MASTER_CLK,         0x31018, BIT(0)),
	GATE_CLK(NE_GCC_USB20_MOCK_UTMI_CLK,      0x3102c, BIT(0)),
	GATE_CLK(NE_GCC_USB20_SLEEP_CLK,          0x31028, BIT(0)),
	GATE_CLK(NE_GCC_USB31_PRIM_MASTER_CLK,    0x2a01c, BIT(0)),
	GATE_CLK(NE_GCC_USB31_PRIM_MOCK_UTMI_CLK, 0x2a034, BIT(0)),
	GATE_CLK(NE_GCC_USB31_PRIM_SLEEP_CLK,     0x2a030, BIT(0)),
	GATE_CLK(NE_GCC_USB31_SEC_MASTER_CLK,     0x2c01c, BIT(0)),
	GATE_CLK(NE_GCC_USB31_SEC_MOCK_UTMI_CLK,  0x2c034, BIT(0)),
	GATE_CLK(NE_GCC_USB31_SEC_SLEEP_CLK,      0x2c030, BIT(0)),
	GATE_CLK(NE_GCC_USB3_PRIM_PHY_AUX_CLK,    0x2a06c, BIT(0)),
	GATE_CLK(NE_GCC_USB3_PRIM_PHY_COM_AUX_CLK,0x2a070, BIT(0)),
	GATE_CLK(NE_GCC_USB3_PRIM_PHY_PIPE_CLK,   0x2a074, BIT(0)),
	GATE_CLK(NE_GCC_USB3_SEC_PHY_AUX_CLK,     0x2c06c, BIT(0)),
	GATE_CLK(NE_GCC_USB3_SEC_PHY_COM_AUX_CLK, 0x2c070, BIT(0)),
	GATE_CLK(NE_GCC_USB3_SEC_PHY_PIPE_CLK,    0x2c074, BIT(0)),
};

static int nord_negcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, nord_negcc_clks[clk->id].name);

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static const struct qcom_reset_map nord_negcc_resets[] = {
	[NE_GCC_GPU_2_BCR] = { 0x34000 },
	[NE_GCC_QUPV3_WRAPPER_2_BCR] = { 0x38000 },
	[NE_GCC_SDCC4_BCR] = { 0x18000 },
	[NE_GCC_UFS_PHY_BCR] = { 0x33000 },
	[NE_GCC_USB20_PRIM_BCR] = { 0x31000 },
	[NE_GCC_USB31_PRIM_BCR] = { 0x2a000 },
	[NE_GCC_USB31_SEC_BCR] = { 0x2c000 },
	[NE_GCC_USB3_DP_PHY_PRIM_BCR] = { 0x2b008 },
	[NE_GCC_USB3_DP_PHY_SEC_BCR] = { 0x2d008 },
	[NE_GCC_USB3_PHY_PRIM_BCR] = { 0x2b000 },
	[NE_GCC_USB3_PHY_SEC_BCR] = { 0x2d000 },
	[NE_GCC_USB3PHY_PHY_PRIM_BCR] = { 0x2b004 },
	[NE_GCC_USB3PHY_PHY_SEC_BCR] = { 0x2d004 },
	[NE_GCC_QUSB2PHY_PRIM_BCR] = { 0x2e000 },
};

static const struct qcom_power_map nord_negcc_gdscs[] = {
	[NE_GCC_UFS_MEM_PHY_GDSC] = { 0x32000 },
	[NE_GCC_UFS_PHY_GDSC] = { 0x33004 },
	[NE_GCC_USB20_PRIM_GDSC] = { 0x31004 },
	[NE_GCC_USB31_PRIM_GDSC] = { 0x2a004 },
	[NE_GCC_USB31_SEC_GDSC] = { 0x2c004 },
	[NE_GCC_USB3_PHY_GDSC] = { 0x2b00c },
	[NE_GCC_USB3_SEC_PHY_GDSC] = { 0x2d00c },
};

static struct msm_clk_data nord_negcc_data = {
	.resets = nord_negcc_resets,
	.num_resets = ARRAY_SIZE(nord_negcc_resets),
	.clks = nord_negcc_clks,
	.num_clks = ARRAY_SIZE(nord_negcc_clks),

	.power_domains = nord_negcc_gdscs,
	.num_power_domains = ARRAY_SIZE(nord_negcc_gdscs),

	.enable = nord_negcc_enable,
	.set_rate = nord_negcc_set_rate,
};

static const struct udevice_id gcc_nord_negcc_of_match[] = {
	{
		.compatible = "qcom,nord-negcc",
		.data = (ulong)&nord_negcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_nord_negcc) = {
	.name		= "gcc_nord_negcc",
	.id		= UCLASS_NOP,
	.of_match	= gcc_nord_negcc_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
