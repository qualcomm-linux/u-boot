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
#include <dt-bindings/clock/qcom,nord-segcc.h>
#include "clock-qcom.h"

#define SE_GCC_QUPV3_WRAP0_S4_CLK_CMD_RCGR	0x2665c

static ulong nord_segcc_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (clk->id < priv->data->num_clks)
		debug("%s: %s, requested rate=%ld\n", __func__,
		      priv->data->clks[clk->id].name, rate);

	switch (clk->id) {
	case SE_GCC_QUPV3_WRAP0_S4_CLK:
		WARN(rate != 14745600,
		     "Unexpected rate for SE_GCC_QUPV3_WRAP0_S4_CLK: %lu\n", rate);
		clk_rcg_set_rate_mnd(priv->base, SE_GCC_QUPV3_WRAP0_S4_CLK_CMD_RCGR,
				     1, 384, 15625, CFG_CLK_SRC_GPLL0, 16);
		return rate;
	default:
		return 0;
	}
}

static const struct gate_clk nord_segcc_clks[] = {
	GATE_CLK_POLLED(SE_GCC_EMAC0_AXI_CLK,           0x2401c, BIT(0),  0x2401c),
	GATE_CLK_POLLED(SE_GCC_EMAC1_AXI_CLK,           0x2501c, BIT(0),  0x2501c),
	GATE_CLK_POLLED(SE_GCC_MMU_2_TCU_VOTE_CLK,      0x57040, BIT(0),  0x57040),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_CORE_2X_CLK, 0x57000, BIT(15), 0x26020),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_CORE_CLK,    0x57000, BIT(14), 0x2600c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_M_AHB_CLK,   0x57000, BIT(12), 0x26004),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S0_CLK,      0x57000, BIT(16), 0x2615c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S1_CLK,      0x57000, BIT(17), 0x26298),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S2_CLK,      0x57000, BIT(18), 0x263d4),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S3_CLK,      0x57000, BIT(19), 0x26510),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S4_CLK,      0x57000, BIT(20), 0x2664c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S5_CLK,      0x57000, BIT(21), 0x26788),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S6_CLK,      0x57000, BIT(22), 0x268c4),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP0_S_AHB_CLK,   0x57000, BIT(13), 0x26008),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_CORE_2X_CLK, 0x57000, BIT(26), 0x27020),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_CORE_CLK,    0x57000, BIT(25), 0x2700c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_M_AHB_CLK,   0x57000, BIT(23), 0x27004),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S0_CLK,      0x57000, BIT(27), 0x2715c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S1_CLK,      0x57000, BIT(28), 0x27298),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S2_CLK,      0x57000, BIT(29), 0x273d4),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S3_CLK,      0x57000, BIT(30), 0x27510),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S4_CLK,      0x57000, BIT(31), 0x2764c),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S5_CLK,      0x57008, BIT(0),  0x27788),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S6_CLK,      0x57008, BIT(1),  0x278c4),
	GATE_CLK_POLLED(SE_GCC_QUPV3_WRAP1_S_AHB_CLK,   0x57000, BIT(24), 0x27008),

	GATE_CLK(SE_GCC_EEE_EMAC0_CLK,               0x240b4, BIT(0)),
	GATE_CLK(SE_GCC_EEE_EMAC1_CLK,               0x250b4, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_CC_SGMIIPHY_RX_CLK,    0x24064, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_CC_SGMIIPHY_TX_CLK,    0x2405c, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_PHY_AUX_CLK,           0x2402c, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_PTP_CLK,               0x24048, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_RGMII_CLK,             0x24058, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_RPCS_RX_CLK,           0x240a8, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_RPCS_TX_CLK,           0x240a4, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_XGXS_RX_CLK,           0x240b0, BIT(0)),
	GATE_CLK(SE_GCC_EMAC0_XGXS_TX_CLK,           0x240ac, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_CC_SGMIIPHY_RX_CLK,    0x25064, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_CC_SGMIIPHY_TX_CLK,    0x2505c, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_PHY_AUX_CLK,           0x2502c, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_PTP_CLK,               0x25048, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_RGMII_CLK,             0x25058, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_RPCS_RX_CLK,           0x250a8, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_RPCS_TX_CLK,           0x250a4, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_XGXS_RX_CLK,           0x250b0, BIT(0)),
	GATE_CLK(SE_GCC_EMAC1_XGXS_TX_CLK,           0x250ac, BIT(0)),
	GATE_CLK(SE_GCC_FRQ_MEASURE_REF_CLK,         0x18008, BIT(0)),
	GATE_CLK(SE_GCC_GP1_CLK,                     0x19000, BIT(0)),
	GATE_CLK(SE_GCC_GP2_CLK,                     0x1a000, BIT(0)),
};

static int nord_segcc_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, nord_segcc_clks[clk->id].name);

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static const struct qcom_reset_map nord_segcc_resets[] = {
	[SE_GCC_EMAC0_BCR] = { 0x24000 },
	[SE_GCC_EMAC1_BCR] = { 0x25000 },
	[SE_GCC_QUPV3_WRAPPER_0_BCR] = { 0x26000 },
	[SE_GCC_QUPV3_WRAPPER_1_BCR] = { 0x27000 },
};

static const struct qcom_power_map nord_segcc_gdscs[] = {
	[SE_GCC_EMAC0_GDSC] = { 0x24004 },
	[SE_GCC_EMAC1_GDSC] = { 0x25004 },
};

static struct msm_clk_data nord_segcc_data = {
	.resets = nord_segcc_resets,
	.num_resets = ARRAY_SIZE(nord_segcc_resets),
	.clks = nord_segcc_clks,
	.num_clks = ARRAY_SIZE(nord_segcc_clks),

	.power_domains = nord_segcc_gdscs,
	.num_power_domains = ARRAY_SIZE(nord_segcc_gdscs),

	.enable = nord_segcc_enable,
	.set_rate = nord_segcc_set_rate,
};

static const struct udevice_id gcc_nord_segcc_of_match[] = {
	{
		.compatible = "qcom,nord-segcc",
		.data = (ulong)&nord_segcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_nord_segcc) = {
	.name		= "gcc_nord_segcc",
	.id		= UCLASS_NOP,
	.of_match	= gcc_nord_segcc_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
