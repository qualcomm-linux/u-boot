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
#include <dt-bindings/clock/qcom,nord-gcc.h>
#include "clock-qcom.h"

static const struct gate_clk nord_gcc_clks[] = {
	GATE_CLK_POLLED(GCC_MMU_0_TCU_VOTE_CLK,      0x7d094, BIT(0),  0x7d094),
	GATE_CLK_POLLED(GCC_PCIE_A_AUX_CLK,          0x9d008, BIT(14), 0x49058),
	GATE_CLK_POLLED(GCC_PCIE_A_CFG_AHB_CLK,      0x9d008, BIT(13), 0x49054),
	GATE_CLK_POLLED(GCC_PCIE_A_PHY_AUX_CLK,      0x9d010, BIT(12), 0x4d01c),
	GATE_CLK_POLLED(GCC_PCIE_A_PHY_RCHNG_CLK,    0x9d008, BIT(16), 0x49078),
	GATE_CLK_POLLED(GCC_PCIE_A_SLV_AXI_CLK,      0x9d008, BIT(11), 0x4902c),
	GATE_CLK_POLLED(GCC_PCIE_A_SLV_Q2A_AXI_CLK,  0x9d008, BIT(10), 0x49024),
	GATE_CLK_POLLED(GCC_PCIE_B_AUX_CLK,          0x9d008, BIT(23), 0x4a058),
	GATE_CLK_POLLED(GCC_PCIE_B_CFG_AHB_CLK,      0x9d008, BIT(22), 0x4a054),
	GATE_CLK_POLLED(GCC_PCIE_B_PHY_AUX_CLK,      0x9d010, BIT(13), 0x4e01c),
	GATE_CLK_POLLED(GCC_PCIE_B_PHY_RCHNG_CLK,    0x9d008, BIT(25), 0x4a078),
	GATE_CLK_POLLED(GCC_PCIE_B_SLV_AXI_CLK,      0x9d008, BIT(20), 0x4a02c),
	GATE_CLK_POLLED(GCC_PCIE_B_SLV_Q2A_AXI_CLK,  0x9d008, BIT(19), 0x4a024),
	GATE_CLK_POLLED(GCC_PCIE_C_AUX_CLK,          0x9d010, BIT(0),  0x4b058),
	GATE_CLK_POLLED(GCC_PCIE_C_CFG_AHB_CLK,      0x9d008, BIT(31), 0x4b054),
	GATE_CLK_POLLED(GCC_PCIE_C_PHY_AUX_CLK,      0x9d010, BIT(14), 0x4f01c),
	GATE_CLK_POLLED(GCC_PCIE_C_PHY_RCHNG_CLK,    0x9d010, BIT(2),  0x4b078),
	GATE_CLK_POLLED(GCC_PCIE_C_SLV_AXI_CLK,      0x9d008, BIT(29), 0x4b02c),
	GATE_CLK_POLLED(GCC_PCIE_C_SLV_Q2A_AXI_CLK,  0x9d008, BIT(28), 0x4b024),
	GATE_CLK_POLLED(GCC_PCIE_D_AUX_CLK,          0x9d010, BIT(9),  0x4c058),
	GATE_CLK_POLLED(GCC_PCIE_D_CFG_AHB_CLK,      0x9d010, BIT(8),  0x4c054),
	GATE_CLK_POLLED(GCC_PCIE_D_PHY_AUX_CLK,      0x9d010, BIT(16), 0x5001c),
	GATE_CLK_POLLED(GCC_PCIE_D_PHY_RCHNG_CLK,    0x9d010, BIT(11), 0x4c078),
	GATE_CLK_POLLED(GCC_PCIE_D_SLV_AXI_CLK,      0x9d010, BIT(6),  0x4c02c),
	GATE_CLK_POLLED(GCC_PCIE_D_SLV_Q2A_AXI_CLK,  0x9d010, BIT(5),  0x4c024),
	GATE_CLK_POLLED(GCC_PCIE_NOC_CNOC_SF_QX_CLK, 0x9d010, BIT(24), 0x52040),
	GATE_CLK_POLLED(GCC_PCIE_NOC_M_CFG_CLK,      0x9d018, BIT(4),  0x52060),
	GATE_CLK_POLLED(GCC_PCIE_NOC_M_PDB_CLK,      0x9d018, BIT(8),  0x52084),
	GATE_CLK_POLLED(GCC_PCIE_NOC_PWRCTL_CLK,     0x9d018, BIT(7),  0x52080),
	GATE_CLK_POLLED(GCC_PCIE_NOC_QOSGEN_EXTREF_CLK, 0x9d010, BIT(19), 0x52074),
	GATE_CLK_POLLED(GCC_PCIE_NOC_SLAVE_AXI_CLK,  0x9d010, BIT(26), 0x52058),
	GATE_CLK_POLLED(GCC_PCIE_NOC_S_CFG_CLK,      0x9d018, BIT(5),  0x52064),
	GATE_CLK_POLLED(GCC_PCIE_NOC_S_PDB_CLK,       0x9d018, BIT(9),  0x5208c),
	GATE_CLK_POLLED(GCC_PCIE_NOC_TSCTR_CLK,      0x9d010, BIT(18), 0x52070),
	GATE_CLK_POLLED(GCC_PCIE_NOC_XO_CLK,         0x9d018, BIT(6),  0x52068),
	GATE_CLK_POLLED(GCC_PDM_AHB_CLK,             0x1a004, BIT(0),  0x1a004),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_CORE_2X_CLK, 0x9d000, BIT(24), 0x23020),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_CORE_CLK,    0x9d000, BIT(23), 0x2300c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_M_CLK,       0x9d000, BIT(22), 0x23004),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_QSPI_REF_CLK, 0x9d000, BIT(26), 0x23170),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_S0_CLK,      0x9d000, BIT(25), 0x2315c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP3_S_AHB_CLK,   0x9d010, BIT(15), 0x23008),
	GATE_CLK_POLLED(GCC_SMMU_PCIE_QTC_VOTE_CLK,  0x7d0b8, BIT(0),  0x7d0b8),

	GATE_CLK(GCC_BOOT_ROM_AHB_CLK,          0x1f004, BIT(0)),
	GATE_CLK(GCC_GP1_CLK,                   0x30000, BIT(0)),
	GATE_CLK(GCC_GP2_CLK,                   0x31000, BIT(0)),
	GATE_CLK(GCC_PCIE_A_DTI_QTC_CLK,        0x9d008, BIT(8)),
	GATE_CLK(GCC_PCIE_A_MSTR_AXI_CLK,       0x9d008, BIT(12)),
	GATE_CLK(GCC_PCIE_A_PIPE_CLK,           0x9d008, BIT(15)),
	GATE_CLK(GCC_PCIE_B_DTI_QTC_CLK,        0x9d008, BIT(17)),
	GATE_CLK(GCC_PCIE_B_MSTR_AXI_CLK,       0x9d008, BIT(21)),
	GATE_CLK(GCC_PCIE_B_PIPE_CLK,           0x9d008, BIT(24)),
	GATE_CLK(GCC_PCIE_C_DTI_QTC_CLK,        0x9d008, BIT(26)),
	GATE_CLK(GCC_PCIE_C_MSTR_AXI_CLK,       0x9d008, BIT(30)),
	GATE_CLK(GCC_PCIE_C_PIPE_CLK,           0x9d010, BIT(1)),
	GATE_CLK(GCC_PCIE_D_DTI_QTC_CLK,        0x9d010, BIT(3)),
	GATE_CLK(GCC_PCIE_D_MSTR_AXI_CLK,       0x9d010, BIT(7)),
	GATE_CLK(GCC_PCIE_D_PIPE_CLK,           0x9d010, BIT(10)),
	GATE_CLK(GCC_PCIE_NOC_ASYNC_BRIDGE_CLK, 0x9d018, BIT(18)),
	GATE_CLK(GCC_PCIE_NOC_MSTR_AXI_CLK,     0x9d010, BIT(25)),
	GATE_CLK(GCC_PCIE_NOC_REFGEN_CLK,       0x52078, BIT(0)),
	GATE_CLK(GCC_PCIE_NOC_SAFETY_CLK,       0x5207c, BIT(0)),
	GATE_CLK(GCC_PDM2_CLK,                  0x1a00c, BIT(0)),
	GATE_CLK(GCC_PDM_XO4_CLK,               0x1a008, BIT(0)),
};

static int nord_gcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, nord_gcc_clks[clk->id].name);

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static const struct qcom_reset_map nord_gcc_resets[] = {
	[GCC_PCIE_A_BCR] = { 0x49000 },
	[GCC_PCIE_A_LINK_DOWN_BCR] = { 0xb9000 },
	[GCC_PCIE_A_NOCSR_COM_PHY_BCR] = { 0xb900c },
	[GCC_PCIE_A_PHY_BCR] = { 0x4d000 },
	[GCC_PCIE_A_PHY_CFG_AHB_BCR] = { 0xb9014 },
	[GCC_PCIE_A_PHY_COM_BCR] = { 0xb9018 },
	[GCC_PCIE_A_PHY_NOCSR_COM_PHY_BCR] = { 0xb9010 },
	[GCC_PCIE_B_BCR] = { 0x4a000 },
	[GCC_PCIE_B_LINK_DOWN_BCR] = { 0xba000 },
	[GCC_PCIE_B_NOCSR_COM_PHY_BCR] = { 0xba008 },
	[GCC_PCIE_B_PHY_BCR] = { 0x4e000 },
	[GCC_PCIE_B_PHY_CFG_AHB_BCR] = { 0xba010 },
	[GCC_PCIE_B_PHY_COM_BCR] = { 0xba014 },
	[GCC_PCIE_B_PHY_NOCSR_COM_PHY_BCR] = { 0xba00c },
	[GCC_PCIE_C_BCR] = { 0x4b000 },
	[GCC_PCIE_C_LINK_DOWN_BCR] = { 0xbb07c },
	[GCC_PCIE_C_NOCSR_COM_PHY_BCR] = { 0xbb084 },
	[GCC_PCIE_C_PHY_BCR] = { 0x4f000 },
	[GCC_PCIE_C_PHY_CFG_AHB_BCR] = { 0xbb08c },
	[GCC_PCIE_C_PHY_COM_BCR] = { 0xbb090 },
	[GCC_PCIE_C_PHY_NOCSR_COM_PHY_BCR] = { 0xbb088 },
	[GCC_PCIE_D_BCR] = { 0x4c000 },
	[GCC_PCIE_D_LINK_DOWN_BCR] = { 0xbc000 },
	[GCC_PCIE_D_NOCSR_COM_PHY_BCR] = { 0xbc008 },
	[GCC_PCIE_D_PHY_BCR] = { 0x50000 },
	[GCC_PCIE_D_PHY_CFG_AHB_BCR] = { 0xbc010 },
	[GCC_PCIE_D_PHY_COM_BCR] = { 0xbc014 },
	[GCC_PCIE_D_PHY_NOCSR_COM_PHY_BCR] = { 0xbc00c },
	[GCC_PCIE_NOC_BCR] = { 0x52000 },
	[GCC_PDM_BCR] = { 0x1a000 },
	[GCC_QUPV3_WRAPPER_3_BCR] = { 0x23000 },
	[GCC_TCSR_PCIE_BCR] = { 0xb901c },
};

static const struct qcom_power_map nord_gcc_gdscs[] = {
	[GCC_PCIE_A_GDSC] = { 0x49004 },
	[GCC_PCIE_A_PHY_GDSC] = { 0x4d004 },
	[GCC_PCIE_B_GDSC] = { 0x4a004 },
	[GCC_PCIE_B_PHY_GDSC] = { 0x4e004 },
	[GCC_PCIE_C_GDSC] = { 0x4b004 },
	[GCC_PCIE_C_PHY_GDSC] = { 0x4f004 },
	[GCC_PCIE_D_GDSC] = { 0x4c004 },
	[GCC_PCIE_D_PHY_GDSC] = { 0x50004 },
	[GCC_PCIE_NOC_GDSC] = { 0x52004 },
};

static struct msm_clk_data nord_gcc_data = {
	.resets = nord_gcc_resets,
	.num_resets = ARRAY_SIZE(nord_gcc_resets),
	.clks = nord_gcc_clks,
	.num_clks = ARRAY_SIZE(nord_gcc_clks),

	.power_domains = nord_gcc_gdscs,
	.num_power_domains = ARRAY_SIZE(nord_gcc_gdscs),

	.enable = nord_gcc_enable,
};

static const struct udevice_id gcc_nord_of_match[] = {
	{
		.compatible = "qcom,nord-gcc",
		.data = (ulong)&nord_gcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_nord) = {
	.name		= "gcc_nord",
	.id		= UCLASS_NOP,
	.of_match	= gcc_nord_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
