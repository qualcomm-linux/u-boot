// SPDX-License-Identifier: GPL-2.0-only
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
#include <dt-bindings/clock/qcom,shikra-gcc.h>
#include "clock-qcom.h"

#define GCC_QUPV3_WRAP0_S0_CLK_CMD_RCGR	0x1f148
#define GCC_SDCC1_APPS_CLK_CMD_RCGR		0x38028
#define GCC_SDCC2_APPS_CLK_CMD_RCGR		0x1e00c
#define GCC_USB30_PRIM_MASTER_CLK_CMD_RCGR	0x1a01c
#define GCC_USB30_PRIM_MOCK_UTMI_CLK_CMD_RCGR	0x1a034
#define GCC_USB3_PRIM_PHY_AUX_CLK_CMD_RCGR	0x1a060

static const struct pll_vote_clk gpll6_vote_clk = {
	.status = 0x6024,
	.status_bit = BIT(31),
	.ena_vote = 0x79000,
	.vote_bit = BIT(6),
};

static const struct pll_vote_clk gpll7_vote_clk = {
	.status = 0x7024,
	.status_bit = BIT(31),
	.ena_vote = 0x79000,
	.vote_bit = BIT(7),
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, CFG_CLK_SRC_GPLL0_AUX2, 1, 384, 15625),
	F(14745600, CFG_CLK_SRC_GPLL0_AUX2, 1, 768, 15625),
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	F(29491200, CFG_CLK_SRC_GPLL0_AUX2, 1, 1536, 15625),
	F(32000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 8, 75),
	F(48000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 4, 25),
	F(64000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 16, 75),
	F(75000000, CFG_CLK_SRC_GPLL0_AUX2, 4, 0, 0),
	F(80000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 4, 15),
	F(96000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 8, 25),
	F(100000000, CFG_CLK_SRC_GPLL0_AUX2, 3, 0, 0),
	F(102400000, CFG_CLK_SRC_GPLL0_AUX2, 1, 128, 375),
	F(112000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 28, 75),
	F(117964800, CFG_CLK_SRC_GPLL0_AUX2, 1, 6144, 15625),
	F(120000000, CFG_CLK_SRC_GPLL0_AUX2, 2.5, 0, 0),
	F(128000000, CFG_CLK_SRC_GPLL6, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, CFG_CLK_SRC_CXO, 16, 3, 25),
	F(400000, CFG_CLK_SRC_CXO, 12, 1, 4),
	F(20000000, CFG_CLK_SRC_GPLL0_AUX2, 5, 1, 3),
	F(25000000, CFG_CLK_SRC_GPLL0_AUX2, 6, 1, 2),
	F(50000000, CFG_CLK_SRC_GPLL0_AUX2, 6, 0, 0),
	F(100000000, CFG_CLK_SRC_GPLL0_AUX2, 3, 0, 0),
	F(192000000, CFG_CLK_SRC_GPLL6, 2, 0, 0),
	F(384000000, CFG_CLK_SRC_GPLL6, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, CFG_CLK_SRC_CXO, 12, 1, 4),
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	F(25000000, CFG_CLK_SRC_GPLL0_AUX2, 12, 0, 0),
	F(50000000, CFG_CLK_SRC_GPLL0_AUX2, 6, 0, 0),
	F(100000000, CFG_CLK_SRC_GPLL0_AUX2, 3, 0, 0),
	F(202000000, CFG_CLK_SRC_GPLL7, 4, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, CFG_CLK_SRC_GPLL0_AUX2, 4.5, 0, 0),
	F(133333333, CFG_CLK_SRC_GPLL0, 4.5, 0, 0),
	F(200000000, CFG_CLK_SRC_GPLL0, 3, 0, 0),
	F(240000000, CFG_CLK_SRC_GPLL0, 2.5, 0, 0),
	{ }
};

static ulong shikra_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	const struct freq_tbl *freq;

	switch (clk->id) {
	case GCC_QUPV3_WRAP0_S0_CLK:
		/*
		 * ftbl_gcc_qupv3_wrap0_s0_clk_src[]'s last entry is
		 * GPLL6-sourced; vote defensively even though this port's
		 * only requested rate (debug_uart_clock, 14745600) resolves
		 * to the GPLL0-sourced entry.
		 */
		clk_enable_gpll0(priv->base, &gpll6_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S0_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_SDCC1_APPS_CLK:
		clk_enable_gpll0(priv->base, &gpll6_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_sdcc1_apps_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_SDCC1_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_SDCC2_APPS_CLK:
		clk_enable_gpll0(priv->base, &gpll7_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_sdcc2_apps_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_SDCC2_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_USB30_PRIM_MASTER_CLK:
		freq = qcom_find_freq(ftbl_gcc_usb30_prim_master_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_USB30_PRIM_MASTER_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		clk_rcg_set_rate(priv->base, GCC_USB3_PRIM_PHY_AUX_CLK_CMD_RCGR,
				 0, CFG_CLK_SRC_CXO);
		return freq->freq;
	case GCC_USB30_PRIM_MOCK_UTMI_CLK:
		WARN(rate != 19200000,
		     "Unexpected rate for USB30_PRIM_MOCK_UTMI_CLK: %lu\n", rate);
		clk_rcg_set_rate(priv->base, GCC_USB30_PRIM_MOCK_UTMI_CLK_CMD_RCGR,
				 0, CFG_CLK_SRC_CXO);
		return rate;
	default:
		return 0;
	}
}

static const struct gate_clk shikra_clks[] = {
	GATE_CLK(GCC_AHB2PHY_CSI_CLK, 0x1d004, BIT(0)),
	GATE_CLK(GCC_AHB2PHY_USB_CLK, 0x1d008, BIT(0)),
	GATE_CLK_POLLED(GCC_BOOT_ROM_AHB_CLK, 0x7900c, BIT(1), 0x23004),
	GATE_CLK(GCC_CAMSS_AXI_CLK, 0x58044, BIT(0)),
	GATE_CLK(GCC_CAMSS_CAMNOC_ATB_CLK, 0x5804c, BIT(0)),
	GATE_CLK(GCC_CAMSS_CAMNOC_DRAGONLINK_ATB_CLK, 0x58060, BIT(0)),
	GATE_CLK(GCC_CAMSS_CAMNOC_NTS_XO_CLK, 0x58050, BIT(0)),
	GATE_CLK(GCC_CAMSS_CCI_0_CLK, 0x56018, BIT(0)),
	GATE_CLK(GCC_CAMSS_CPHY_0_CLK, 0x52088, BIT(0)),
	GATE_CLK(GCC_CAMSS_CPHY_1_CLK, 0x5208c, BIT(0)),
	GATE_CLK(GCC_CAMSS_CSI0PHYTIMER_CLK, 0x45018, BIT(0)),
	GATE_CLK(GCC_CAMSS_CSI1PHYTIMER_CLK, 0x45034, BIT(0)),
	GATE_CLK(GCC_CAMSS_MCLK0_CLK, 0x51018, BIT(0)),
	GATE_CLK(GCC_CAMSS_MCLK1_CLK, 0x51034, BIT(0)),
	GATE_CLK(GCC_CAMSS_MCLK2_CLK, 0x51050, BIT(0)),
	GATE_CLK(GCC_CAMSS_MCLK3_CLK, 0x5106c, BIT(0)),
	GATE_CLK(GCC_CAMSS_NRT_AXI_CLK, 0x58054, BIT(0)),
	GATE_CLK(GCC_CAMSS_OPE_AHB_CLK, 0x5503c, BIT(0)),
	GATE_CLK(GCC_CAMSS_OPE_CLK, 0x5501c, BIT(0)),
	GATE_CLK(GCC_CAMSS_RT_AXI_CLK, 0x5805c, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_0_CLK, 0x5201c, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_0_CPHY_RX_CLK, 0x5207c, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_0_CSID_CLK, 0x520ac, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_1_CLK, 0x5203c, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_1_CPHY_RX_CLK, 0x52080, BIT(0)),
	GATE_CLK(GCC_CAMSS_TFE_1_CSID_CLK, 0x520cc, BIT(0)),
	GATE_CLK(GCC_CAMSS_TOP_AHB_CLK, 0x58028, BIT(0)),
	GATE_CLK_POLLED(GCC_CAM_THROTTLE_NRT_CLK, 0x79004, BIT(16), 0x17070),
	GATE_CLK_POLLED(GCC_CAM_THROTTLE_RT_CLK, 0x79004, BIT(15), 0x1706c),
	GATE_CLK(GCC_CFG_NOC_USB2_PRIM_AXI_CLK, 0x111c4, BIT(0)),
	GATE_CLK(GCC_CFG_NOC_USB3_PRIM_AXI_CLK, 0x1a07c, BIT(0)),
	GATE_CLK(GCC_DDRSS_GPU_AXI_CLK, 0x71000, BIT(0)),
	GATE_CLK(GCC_DDRSS_MEMNOC_PCIE_SF_CLK, 0x29044, BIT(0)),
	GATE_CLK(GCC_DISP_GPLL0_DIV_CLK_SRC, 0x79004, BIT(11)),
	GATE_CLK(GCC_DISP_HF_AXI_CLK, 0x17020, BIT(0)),
	GATE_CLK_POLLED(GCC_DISP_THROTTLE_CORE_CLK, 0x79004, BIT(13), 0x17064),
	GATE_CLK(GCC_EMAC0_AHB_CLK, 0xad010, BIT(0)),
	GATE_CLK(GCC_EMAC0_AXI_CLK, 0xad014, BIT(0)),
	GATE_CLK(GCC_EMAC0_AXI_SYS_NOC_CLK, 0x109d4, BIT(0)),
	GATE_CLK(GCC_EMAC0_CC_SGMIIPHY_RX_CLK, 0xad044, BIT(0)),
	GATE_CLK(GCC_EMAC0_CC_SGMIIPHY_TX_CLK, 0xad03c, BIT(0)),
	GATE_CLK(GCC_EMAC0_PHY_AUX_CLK, 0xad018, BIT(0)),
	GATE_CLK(GCC_EMAC0_PTP_CLK, 0xad034, BIT(0)),
	GATE_CLK(GCC_EMAC0_RGMII_CLK, 0xad038, BIT(0)),
	GATE_CLK(GCC_EMAC1_AHB_CLK, 0xae010, BIT(0)),
	GATE_CLK(GCC_EMAC1_AXI_CLK, 0xae014, BIT(0)),
	GATE_CLK(GCC_EMAC1_AXI_SYS_NOC_CLK, 0x109f4, BIT(0)),
	GATE_CLK(GCC_EMAC1_CC_SGMIIPHY_RX_CLK, 0xae044, BIT(0)),
	GATE_CLK(GCC_EMAC1_CC_SGMIIPHY_TX_CLK, 0xae03c, BIT(0)),
	GATE_CLK(GCC_EMAC1_PHY_AUX_CLK, 0xae018, BIT(0)),
	GATE_CLK(GCC_EMAC1_PTP_CLK, 0xae034, BIT(0)),
	GATE_CLK(GCC_EMAC1_RGMII_CLK, 0xae038, BIT(0)),
	GATE_CLK(GCC_GP1_CLK, 0x4d000, BIT(0)),
	GATE_CLK(GCC_GP2_CLK, 0x4e000, BIT(0)),
	GATE_CLK(GCC_GP3_CLK, 0x4f000, BIT(0)),
	GATE_CLK(GCC_GPU_GPLL0_CLK_SRC, 0x7900c, BIT(18)),
	GATE_CLK(GCC_GPU_GPLL0_DIV_CLK_SRC, 0x7900c, BIT(19)),
	GATE_CLK(GCC_GPU_MEMNOC_GFX_CLK, 0x3600c, BIT(0)),
	GATE_CLK(GCC_GPU_SMMU_VOTE_CLK, 0x7d000, BIT(0)),
	GATE_CLK(GCC_GPU_SNOC_DVM_GFX_CLK, 0x36018, BIT(0)),
	GATE_CLK_POLLED(GCC_GPU_THROTTLE_CORE_CLK, 0x7900c, BIT(21), 0x36048),
	GATE_CLK(GCC_MMU_TCU_VOTE_CLK, 0x7d06c, BIT(0)),
	GATE_CLK_POLLED(GCC_PCIE_AUX_CLK, 0x79018, BIT(0), 0xaf044),
	GATE_CLK_POLLED(GCC_PCIE_CFG_AHB_CLK, 0x7900c, BIT(27), 0xaf010),
	GATE_CLK(GCC_PCIE_CLKREF_EN, 0xb8000, BIT(0)),
	GATE_CLK_POLLED(GCC_PCIE_MSTR_AXI_CLK, 0x7900c, BIT(30), 0xaf020),
	GATE_CLK_POLLED(GCC_PCIE_PIPE_CLK, 0x79018, BIT(2), 0xaf050),
	GATE_CLK_POLLED(GCC_PCIE_RCHNG_PHY_CLK, 0x7900c, BIT(31), 0xaf040),
	GATE_CLK_POLLED(GCC_PCIE_SLEEP_CLK, 0x79018, BIT(1), 0xaf04c),
	GATE_CLK_POLLED(GCC_PCIE_SLV_AXI_CLK, 0x7900c, BIT(29), 0xaf018),
	GATE_CLK_POLLED(GCC_PCIE_SLV_Q2A_AXI_CLK, 0x7900c, BIT(28), 0xaf014),
	GATE_CLK_POLLED(GCC_PCIE_TBU_CLK, 0x79018, BIT(6), 0xaf098),
	GATE_CLK_POLLED(GCC_PCIE_THROTTLE_CORE_CLK, 0x79018, BIT(5), 0xaf094),
	GATE_CLK_POLLED(GCC_PCIE_THROTTLE_XO_CLK, 0x79018, BIT(4), 0xaf090),
	GATE_CLK(GCC_PCIE_TILE_AXI_SYS_NOC_CLK, 0x10f2c, BIT(0)),
	GATE_CLK(GCC_PDM2_CLK, 0x2000c, BIT(0)),
	GATE_CLK(GCC_PDM_AHB_CLK, 0x20004, BIT(0)),
	GATE_CLK(GCC_PDM_XO4_CLK, 0x20008, BIT(0)),
	GATE_CLK(GCC_PWM0_XO512_CLK, 0x2002c, BIT(0)),
	GATE_CLK_POLLED(GCC_QMIP_CAMERA_NRT_AHB_CLK, 0x79004, BIT(9), 0x17014),
	GATE_CLK_POLLED(GCC_QMIP_CAMERA_RT_AHB_CLK, 0x79004, BIT(12), 0x17060),
	GATE_CLK_POLLED(GCC_QMIP_DISP_AHB_CLK, 0x79004, BIT(10), 0x17018),
	GATE_CLK_POLLED(GCC_QMIP_GPU_CFG_AHB_CLK, 0x7900c, BIT(20), 0x36040),
	GATE_CLK_POLLED(GCC_QMIP_PCIE_CFG_AHB_CLK, 0x79018, BIT(3), 0xaf08c),
	GATE_CLK_POLLED(GCC_QMIP_VIDEO_VCODEC_AHB_CLK, 0x79004, BIT(8), 0x17010),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_CORE_2X_CLK, 0x79004, BIT(21), 0x1f014),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_CORE_CLK, 0x79004, BIT(20), 0x1f00c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S0_CLK, 0x79004, BIT(22), 0x1f144),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S1_CLK, 0x79004, BIT(23), 0x1f274),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S2_CLK, 0x79004, BIT(24), 0x1f3a4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S3_CLK, 0x79004, BIT(25), 0x1f4d4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S4_CLK, 0x79004, BIT(26), 0x1f604),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S5_CLK, 0x79004, BIT(27), 0x1f734),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S6_CLK, 0x79004, BIT(28), 0x1f864),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S7_CLK, 0x79004, BIT(29), 0x1f994),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S8_CLK, 0x79004, BIT(30), 0x1fac4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S9_CLK, 0x79004, BIT(31), 0x1fbf4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP_0_M_AHB_CLK, 0x79004, BIT(18), 0x1f004),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP_0_S_AHB_CLK, 0x79004, BIT(19), 0x1f008),
	GATE_CLK(GCC_SDCC1_AHB_CLK, 0x38008, BIT(0)),
	GATE_CLK(GCC_SDCC1_APPS_CLK, 0x38004, BIT(0)),
	GATE_CLK(GCC_SDCC1_ICE_CORE_CLK, 0x3800c, BIT(0)),
	GATE_CLK(GCC_SDCC2_AHB_CLK, 0x1e008, BIT(0)),
	GATE_CLK(GCC_SDCC2_APPS_CLK, 0x1e004, BIT(0)),
	GATE_CLK(GCC_SYS_NOC_USB2_PRIM_AXI_CLK, 0x10a14, BIT(0)),
	GATE_CLK(GCC_SYS_NOC_USB3_PRIM_AXI_CLK, 0x1a078, BIT(0)),
	GATE_CLK(GCC_TSCSS_AHB_CLK, 0xac024, BIT(0)),
	GATE_CLK(GCC_TSCSS_CNTR_CLK, 0xac020, BIT(0)),
	GATE_CLK(GCC_TSCSS_ETU_CLK, 0xac01c, BIT(0)),
	GATE_CLK(GCC_UFS_CLKREF_EN, 0x8c000, BIT(0)),
	GATE_CLK(GCC_USB20_MASTER_CLK, 0xb0010, BIT(0)),
	GATE_CLK(GCC_USB20_MOCK_UTMI_CLK, 0xb001c, BIT(0)),
	GATE_CLK(GCC_USB20_SLEEP_CLK, 0xb0018, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_MASTER_CLK, 0x1a010, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_MOCK_UTMI_CLK, 0x1a018, BIT(0)),
	GATE_CLK(GCC_USB30_PRIM_SLEEP_CLK, 0x1a014, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_CLKREF_EN, 0x9f000, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_PHY_COM_AUX_CLK, 0x1a054, BIT(0)),
	GATE_CLK(GCC_USB3_PRIM_PHY_PIPE_CLK, 0x1a058, BIT(0)),
	GATE_CLK(GCC_VCODEC0_AXI_CLK, 0x6e008, BIT(0)),
	GATE_CLK(GCC_VENUS_AHB_CLK, 0x6e010, BIT(0)),
	GATE_CLK(GCC_VENUS_CTL_AXI_CLK, 0x6e004, BIT(0)),
	GATE_CLK(GCC_VIDEO_AXI0_CLK, 0x1701c, BIT(0)),
	GATE_CLK_POLLED(GCC_VIDEO_THROTTLE_CORE_CLK, 0x79004, BIT(14), 0x17068),
	GATE_CLK(GCC_VIDEO_VCODEC0_SYS_CLK, 0x6d044, BIT(0)),
	GATE_CLK(GCC_VIDEO_VENUS_CTL_CLK, 0x6d02c, BIT(0)),
};

static int shikra_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, shikra_clks[clk->id].name);

	return qcom_gate_clk_en(priv, clk->id);
}

static const struct qcom_reset_map shikra_gcc_resets[] = {
	[GCC_CAMSS_OPE_BCR] = { 0x55000 },
	[GCC_CAMSS_TFE_BCR] = { 0x52000 },
	[GCC_CAMSS_TOP_BCR] = { 0x58000 },
	[GCC_EMAC0_BCR] = { 0xad000 },
	[GCC_EMAC1_BCR] = { 0xae000 },
	[GCC_GPU_BCR] = { 0x36000 },
	[GCC_MMSS_BCR] = { 0x17000 },
	[GCC_PCIE_BCR] = { 0xaf000 },
	[GCC_PCIE_PHY_BCR] = { 0xb1000 },
	[GCC_PDM_BCR] = { 0x20000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x1f000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x1c004 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_TSCSS_BCR] = { 0xac000 },
	[GCC_USB20_BCR] = { 0xb0000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3PHY_PHY_PRIM_SP0_BCR] = { 0x1b008 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x1b020 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
	[GCC_VCODEC0_BCR] = { 0x6d034 },
	[GCC_VENUS_BCR] = { 0x6d018 },
	[GCC_VIDEO_INTERFACE_BCR] = { 0x6e000 },
};

static const struct qcom_power_map shikra_gdscs[] = {
	[GCC_CAMSS_TOP_GDSC] = { 0x58004 },
	[GCC_EMAC0_GDSC] = { 0xad004 },
	[GCC_EMAC1_GDSC] = { 0xae004 },
	[GCC_PCIE_GDSC] = { 0xaf004 },
	[GCC_USB20_GDSC] = { 0xb0004 },
	[GCC_USB30_PRIM_GDSC] = { 0x1a004 },
	[GCC_VCODEC0_GDSC] = { 0x6d038 },
	[GCC_VENUS_GDSC] = { 0x6d01c },
};

static struct msm_clk_data shikra_gcc_data = {
	.resets = shikra_gcc_resets,
	.num_resets = ARRAY_SIZE(shikra_gcc_resets),
	.clks = shikra_clks,
	.num_clks = ARRAY_SIZE(shikra_clks),

	.power_domains = shikra_gdscs,
	.num_power_domains = ARRAY_SIZE(shikra_gdscs),

	.enable = shikra_enable,
	.set_rate = shikra_set_rate,
};

static const struct udevice_id gcc_shikra_of_match[] = {
	{
		.compatible = "qcom,shikra-gcc",
		.data = (ulong)&shikra_gcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_shikra) = {
	.name		= "gcc_shikra",
	.id		= UCLASS_NOP,
	.of_match	= gcc_shikra_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
