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
#include <dt-bindings/clock/qcom,echo-gcc.h>
#include "clock-qcom.h"

#define GCC_EEE_EMAC0_CLK_CMD_RCGR	0x710b8
#define GCC_EEE_EMAC1_CLK_CMD_RCGR	0x720b8
#define GCC_EEE_EMAC2_CLK_CMD_RCGR	0x160b8
#define GCC_EMAC0_LOCAL_PTP_CLK_CMD_RCGR	0x710d4
#define GCC_EMAC0_PHY_AUX_CLK_CMD_RCGR	0x71030
#define GCC_EMAC0_PTP_CLK_CMD_RCGR	0x71084
#define GCC_EMAC0_RGMII_CLK_CMD_RCGR	0x7106c
#define GCC_EMAC1_LOCAL_PTP_CLK_CMD_RCGR	0x720d4
#define GCC_EMAC1_PHY_AUX_CLK_CMD_RCGR	0x72030
#define GCC_EMAC1_PTP_CLK_CMD_RCGR	0x72084
#define GCC_EMAC1_RGMII_CLK_CMD_RCGR	0x7206c
#define GCC_EMAC2_LOCAL_PTP_CLK_CMD_RCGR	0x160d4
#define GCC_EMAC2_PHY_AUX_CLK_CMD_RCGR	0x16030
#define GCC_EMAC2_PTP_CLK_CMD_RCGR	0x16084
#define GCC_EMAC2_RGMII_CLK_CMD_RCGR	0x1606c
#define GCC_GP1_CLK_CMD_RCGR	0x47004
#define GCC_GP2_CLK_CMD_RCGR	0x48004
#define GCC_GP3_CLK_CMD_RCGR	0x49004
#define GCC_PCIE_1_AUX_CLK_CMD_RCGR	0x67050
#define GCC_PCIE_1_PHY_AUX_CLK_CMD_RCGR	0x5601c
#define GCC_PCIE_1_PHY_RCHNG_CLK_CMD_RCGR	0x6707c
#define GCC_PCIE_2_AUX_CLK_CMD_RCGR	0x68070
#define GCC_PCIE_2_PHY_AUX_CLK_CMD_RCGR	0x6e01c
#define GCC_PCIE_2_PHY_RCHNG_CLK_CMD_RCGR	0x68040
#define GCC_PCIE_3_AUX_CLK_CMD_RCGR	0x13070
#define GCC_PCIE_3_PHY_AUX_CLK_CMD_RCGR	0x1701c
#define GCC_PCIE_3_PHY_RCHNG_CLK_CMD_RCGR	0x13040
#define GCC_PCIE_AUX_CLK_CMD_RCGR	0x53078
#define GCC_PCIE_PHY_AUX_CLK_CMD_RCGR	0x5401c
#define GCC_PCIE_RCHNG_PHY_CLK_CMD_RCGR	0x53094
#define GCC_PDM2_CLK_CMD_RCGR	0x34010
#define GCC_PRIME_CORE_CLK_CMD_RCGR	0x1001c
#define GCC_QUPV3_WRAP0_S0_CLK_CMD_RCGR	0x6c014
#define GCC_QUPV3_WRAP0_S1_CLK_CMD_RCGR	0x6c150
#define GCC_QUPV3_WRAP0_S2_CLK_CMD_RCGR	0x6c28c
#define GCC_QUPV3_WRAP0_S3_CLK_CMD_RCGR	0x6c3c8
#define GCC_QUPV3_WRAP0_S4_CLK_CMD_RCGR	0x6c504
#define GCC_QUPV3_WRAP0_S5_CLK_CMD_RCGR	0x6c640
#define GCC_QUPV3_WRAP0_S6_CLK_CMD_RCGR	0x6c77c
#define GCC_QUPV3_WRAP0_S7_CLK_CMD_RCGR	0x6c8b8
#define GCC_QUPV3_WRAP0_S8_CLK_CMD_RCGR	0x6c9f4
#define GCC_QUPV3_WRAP0_S9_CLK_CMD_RCGR	0x6cb30
#define GCC_SDCC1_APPS_CLK_CMD_RCGR	0x6b018
#define GCC_SDCC1_ICE_CORE_CLK_CMD_RCGR	0x6b044
#define GCC_SDCC2_APPS_CLK_CMD_RCGR	0x6a01c
#define GCC_TLMM_125_CLK_CMD_RCGR	0x2e000
#define GCC_USB30_MASTER_CLK_CMD_RCGR	0x2703c
#define GCC_USB30_MOCK_UTMI_CLK_CMD_RCGR	0x27054
#define GCC_USB3_PHY_AUX_CLK_CMD_RCGR	0x2707c

static const struct freq_tbl ftbl_gcc_eee_emac0_clk_src[] = {
	F(100000000, 1536, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_emac0_local_ptp_clk_src[] = {
	F(75000000, 1536, 4, 0, 0),
	F(250000000, 512, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, 0, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_emac0_rgmii_clk_src[] = {
	F(2500000, 1536, 10, 1, 12),
	F(5000000, 1536, 10, 1, 6),
	F(25000000, 1536, 12, 0, 0),
	F(50000000, 512, 10, 0, 0),
	F(250000000, 512, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_emac1_rgmii_clk_src[] = {
	F(2500000, 1536, 10, 1, 12),
	F(5000000, 1536, 10, 1, 6),
	F(25000000, 1536, 12, 0, 0),
	F(50000000, 512, 10, 0, 0),
	F(125000000, 512, 4, 0, 0),
	F(250000000, 512, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_gp1_clk_src[] = {
	F(19200000, 0, 1, 0, 0),
	F(50000000, 1536, 6, 0, 0),
	F(100000000, 256, 6, 0, 0),
	F(200000000, 256, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_pcie_1_phy_rchng_clk_src[] = {
	F(19200000, 0, 1, 0, 0),
	F(100000000, 1536, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_pdm2_clk_src[] = {
	F(60000000, 1536, 5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_prime_core_clk_src[] = {
	F(300000000, 1536, 1, 0, 0),
	F(533000000, 1024, 2, 0, 0),
	F(600000000, 256, 1, 0, 0),
	F(806400000, 1280, 1, 0, 0),
	F(933000000, 768, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, 1536, 1, 384, 15625),
	F(14745600, 1536, 1, 768, 15625),
	F(19200000, 0, 1, 0, 0),
	F(29491200, 1536, 1, 1536, 15625),
	F(32000000, 1536, 1, 8, 75),
	F(48000000, 1536, 1, 4, 25),
	F(64000000, 1536, 1, 16, 75),
	F(75000000, 1536, 4, 0, 0),
	F(80000000, 1536, 1, 4, 15),
	F(96000000, 1536, 1, 8, 25),
	F(100000000, 256, 6, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s1_clk_src[] = {
	F(7372800, 1536, 1, 384, 15625),
	F(14745600, 1536, 1, 768, 15625),
	F(19200000, 0, 1, 0, 0),
	F(29491200, 1536, 1, 1536, 15625),
	F(32000000, 1536, 1, 8, 75),
	F(48000000, 1536, 1, 4, 25),
	F(64000000, 1536, 1, 16, 75),
	F(80000000, 1536, 1, 4, 15),
	F(96000000, 1536, 1, 8, 25),
	F(128000000, 1536, 1, 32, 75),
	{ }
};

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s5_clk_src[] = {
	F(7372800, 1536, 1, 384, 15625),
	F(14745600, 1536, 1, 768, 15625),
	F(19200000, 0, 1, 0, 0),
	F(29491200, 1536, 1, 1536, 15625),
	F(32000000, 1536, 1, 8, 75),
	F(48000000, 1536, 1, 4, 25),
	F(64000000, 1536, 1, 16, 75),
	F(75000000, 1536, 4, 0, 0),
	F(80000000, 1536, 1, 4, 15),
	F(96000000, 1536, 1, 8, 25),
	F(120000000, 256, 5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_sdcc1_apps_clk_src[] = {
	F(144000, 0, 16, 3, 25),
	F(400000, 0, 12, 1, 4),
	F(19200000, 0, 1, 0, 0),
	F(20000000, 1536, 5, 1, 3),
	F(25000000, 1536, 12, 0, 0),
	F(37500000, 1536, 8, 0, 0),
	F(50000000, 1536, 6, 0, 0),
	F(100000000, 1536, 3, 0, 0),
	F(192000000, 512, 2, 0, 0),
	F(384000000, 512, 1, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(100000000, 1536, 3, 0, 0),
	F(150000000, 256, 4, 0, 0),
	F(300000000, 256, 2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_sdcc2_apps_clk_src[] = {
	F(400000, 0, 12, 1, 4),
	F(19200000, 0, 1, 0, 0),
	F(25000000, 1536, 12, 0, 0),
	F(37500000, 1536, 8, 0, 0),
	F(50000000, 1536, 6, 0, 0),
	F(100000000, 1536, 3, 0, 0),
	F(200000000, 256, 3, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_tlmm_125_clk_src[] = {
	F(125000000, 512, 4, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_usb30_master_clk_src[] = {
	F(200000000, 1536, 1.5, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gcc_usb3_phy_aux_clk_src[] = {
	F(1000000, 0, 1, 5, 96),
	F(19200000, 0, 1, 0, 0),
	{ }
};

static struct pll_vote_clk gpll3_vote_clk = {
	.status = 0x300c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(3),
};

static struct pll_vote_clk gpll4_vote_clk = {
	.status = 0x400c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(4),
};

static struct pll_vote_clk gpll5_vote_clk = {
	.status = 0x500c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(5),
};

static struct pll_vote_clk gpll6_vote_clk = {
	.status = 0x600c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(6),
};

static struct pll_vote_clk gpll7_vote_clk = {
	.status = 0x700c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(7),
};

static struct pll_vote_clk gpll8_vote_clk = {
	.status = 0x800c,
	.status_bit = BIT(31),
	.ena_vote = 0x7d020,
	.vote_bit = BIT(8),
};

static ulong echo_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	const struct freq_tbl *freq;

	if (clk->id < priv->data->num_clks)
		debug("%s: %s, requested rate=%ld\n",
		      __func__, priv->data->clks[clk->id].name, rate);

	switch (clk->id) {
	case GCC_EEE_EMAC0_CLK:
		freq = qcom_find_freq(ftbl_gcc_eee_emac0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EEE_EMAC0_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EEE_EMAC1_CLK:
		freq = qcom_find_freq(ftbl_gcc_eee_emac0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EEE_EMAC1_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EEE_EMAC2_CLK:
		freq = qcom_find_freq(ftbl_gcc_eee_emac0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EEE_EMAC2_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC0_LOCAL_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC0_LOCAL_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC0_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_EMAC0_PHY_AUX_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_EMAC0_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC0_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC0_RGMII_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_rgmii_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC0_RGMII_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC1_LOCAL_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC1_LOCAL_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC1_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_EMAC1_PHY_AUX_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_EMAC1_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC1_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC1_RGMII_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac1_rgmii_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC1_RGMII_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC2_LOCAL_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC2_LOCAL_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC2_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_EMAC2_PHY_AUX_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_EMAC2_PTP_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac0_local_ptp_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC2_PTP_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_EMAC2_RGMII_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_emac1_rgmii_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_EMAC2_RGMII_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_GP1_CLK:
		freq = qcom_find_freq(ftbl_gcc_gp1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_GP1_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_GP2_CLK:
		freq = qcom_find_freq(ftbl_gcc_gp1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_GP2_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_GP3_CLK:
		freq = qcom_find_freq(ftbl_gcc_gp1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_GP3_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_1_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_1_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_1_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_1_PHY_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_1_PHY_RCHNG_CLK:
		freq = qcom_find_freq(ftbl_gcc_pcie_1_phy_rchng_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PCIE_1_PHY_RCHNG_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_PCIE_2_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_2_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_2_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_2_PHY_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_2_PHY_RCHNG_CLK:
		freq = qcom_find_freq(ftbl_gcc_pcie_1_phy_rchng_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PCIE_2_PHY_RCHNG_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_PCIE_3_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_3_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_3_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_3_PHY_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_3_PHY_RCHNG_CLK:
		freq = qcom_find_freq(ftbl_gcc_pcie_1_phy_rchng_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PCIE_3_PHY_RCHNG_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_PCIE_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_PCIE_PHY_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_PCIE_RCHNG_PHY_CLK:
		freq = qcom_find_freq(ftbl_gcc_pcie_1_phy_rchng_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PCIE_RCHNG_PHY_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_PDM2_CLK:
		freq = qcom_find_freq(ftbl_gcc_pdm2_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PDM2_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_PRIME_CORE_CLK:
		clk_enable_gpll0(priv->base, &gpll3_vote_clk);
		clk_enable_gpll0(priv->base, &gpll5_vote_clk);
		clk_enable_gpll0(priv->base, &gpll7_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_prime_core_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_PRIME_CORE_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S0_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S0_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S1_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S1_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S2_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S2_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S3_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S3_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S4_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S4_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S5_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s5_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S5_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S6_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s5_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S6_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S7_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S7_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S8_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s1_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S8_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S9_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_QUPV3_WRAP0_S9_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_SDCC1_APPS_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc1_apps_clk_src, rate);
		/*
		 * GPLL6 never locks on this board (PLL_RESET_N=0 at reset,
		 * and this vote-only helper never programs PLL_MODE/L_VAL/
		 * ALPHA_VAL), so only vote/wait on it when the selected freq
		 * entry actually sources from it.
		 */
		if (freq->src == CFG_CLK_SRC_GPLL6_OUT_MAIN)
			clk_enable_gpll0(priv->base, &gpll6_vote_clk);
		clk_rcg_set_rate_mnd(priv->base, GCC_SDCC1_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_SDCC1_ICE_CORE_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc1_ice_core_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_SDCC1_ICE_CORE_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_SDCC2_APPS_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc2_apps_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_SDCC2_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_TLMM_125_CLK:
		clk_enable_gpll0(priv->base, &gpll4_vote_clk);
		freq = qcom_find_freq(ftbl_gcc_tlmm_125_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_TLMM_125_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_USB30_MASTER_CLK:
		freq = qcom_find_freq(ftbl_gcc_usb30_master_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_USB30_MASTER_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_USB30_MOCK_UTMI_CLK:
		freq = qcom_find_freq(ftbl_gcc_emac0_phy_aux_clk_src, rate);
		clk_rcg_set_rate(priv->base, GCC_USB30_MOCK_UTMI_CLK_CMD_RCGR, freq->pre_div, freq->src);
		return freq->freq;
	case GCC_USB3_PHY_AUX_CLK:
		freq = qcom_find_freq(ftbl_gcc_usb3_phy_aux_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, GCC_USB3_PHY_AUX_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	default:
		return 0;
	}
}

static const struct gate_clk echo_clks[] = {
	GATE_CLK_POLLED(GCC_BOOT_ROM_AHB_CLK, 0x7d000, BIT(26), 0x37004),
	GATE_CLK_POLLED(GCC_EEE_EMAC0_CLK, 0x710b4, BIT(0), 0x710b4),
	GATE_CLK_POLLED(GCC_EEE_EMAC1_CLK, 0x720b4, BIT(0), 0x720b4),
	GATE_CLK_POLLED(GCC_EEE_EMAC2_CLK, 0x160b4, BIT(0), 0x160b4),
	GATE_CLK_POLLED(GCC_EMAC0_AXI_CLK, 0x71018, BIT(0), 0x71018),
	GATE_CLK_POLLED(GCC_EMAC0_CC_SGMIIPHY_RX_CLK, 0x71064, BIT(0), 0x71064),
	GATE_CLK_POLLED(GCC_EMAC0_CC_SGMIIPHY_TX_CLK, 0x7105c, BIT(0), 0x7105c),
	GATE_CLK_POLLED(GCC_EMAC0_LOCAL_PTP_CLK, 0x710ec, BIT(0), 0x710ec),
	GATE_CLK_POLLED(GCC_EMAC0_PHY_AUX_CLK, 0x7102c, BIT(0), 0x7102c),
	GATE_CLK_POLLED(GCC_EMAC0_PTP_CLK, 0x71048, BIT(0), 0x71048),
	GATE_CLK_POLLED(GCC_EMAC0_RGMII_CLK, 0x71058, BIT(0), 0x71058),
	GATE_CLK_POLLED(GCC_EMAC0_RPCS_RX_CLK, 0x710a8, BIT(0), 0x710a8),
	GATE_CLK_POLLED(GCC_EMAC0_RPCS_TX_CLK, 0x710a4, BIT(0), 0x710a4),
	GATE_CLK_POLLED(GCC_EMAC0_SLV_AHB_CLK, 0x71028, BIT(0), 0x71028),
	GATE_CLK_POLLED(GCC_EMAC0_XGXS_RX_CLK, 0x710b0, BIT(0), 0x710b0),
	GATE_CLK_POLLED(GCC_EMAC0_XGXS_TX_CLK, 0x710ac, BIT(0), 0x710ac),
	GATE_CLK_POLLED(GCC_EMAC1_AXI_CLK, 0x72018, BIT(0), 0x72018),
	GATE_CLK_POLLED(GCC_EMAC1_CC_SGMIIPHY_RX_CLK, 0x72064, BIT(0), 0x72064),
	GATE_CLK_POLLED(GCC_EMAC1_CC_SGMIIPHY_TX_CLK, 0x7205c, BIT(0), 0x7205c),
	GATE_CLK_POLLED(GCC_EMAC1_LOCAL_PTP_CLK, 0x720ec, BIT(0), 0x720ec),
	GATE_CLK_POLLED(GCC_EMAC1_PHY_AUX_CLK, 0x7202c, BIT(0), 0x7202c),
	GATE_CLK_POLLED(GCC_EMAC1_PTP_CLK, 0x72048, BIT(0), 0x72048),
	GATE_CLK_POLLED(GCC_EMAC1_RGMII_CLK, 0x72058, BIT(0), 0x72058),
	GATE_CLK_POLLED(GCC_EMAC1_RPCS_RX_CLK, 0x720a8, BIT(0), 0x720a8),
	GATE_CLK_POLLED(GCC_EMAC1_RPCS_TX_CLK, 0x720a4, BIT(0), 0x720a4),
	GATE_CLK_POLLED(GCC_EMAC1_SLV_AHB_CLK, 0x72028, BIT(0), 0x72028),
	GATE_CLK_POLLED(GCC_EMAC1_XGXS_RX_CLK, 0x720b0, BIT(0), 0x720b0),
	GATE_CLK_POLLED(GCC_EMAC1_XGXS_TX_CLK, 0x720ac, BIT(0), 0x720ac),
	GATE_CLK_POLLED(GCC_EMAC2_AXI_CLK, 0x16018, BIT(0), 0x16018),
	GATE_CLK_POLLED(GCC_EMAC2_CC_SGMIIPHY_RX_CLK, 0x16064, BIT(0), 0x16064),
	GATE_CLK_POLLED(GCC_EMAC2_CC_SGMIIPHY_TX_CLK, 0x1605c, BIT(0), 0x1605c),
	GATE_CLK_POLLED(GCC_EMAC2_LOCAL_PTP_CLK, 0x160ec, BIT(0), 0x160ec),
	GATE_CLK_POLLED(GCC_EMAC2_PHY_AUX_CLK, 0x1602c, BIT(0), 0x1602c),
	GATE_CLK_POLLED(GCC_EMAC2_PTP_CLK, 0x16048, BIT(0), 0x16048),
	GATE_CLK_POLLED(GCC_EMAC2_RGMII_CLK, 0x16058, BIT(0), 0x16058),
	GATE_CLK_POLLED(GCC_EMAC2_RPCS_RX_CLK, 0x160a8, BIT(0), 0x160a8),
	GATE_CLK_POLLED(GCC_EMAC2_RPCS_TX_CLK, 0x160a4, BIT(0), 0x160a4),
	GATE_CLK_POLLED(GCC_EMAC2_SLV_AHB_CLK, 0x16028, BIT(0), 0x16028),
	GATE_CLK_POLLED(GCC_EMAC2_XGXS_RX_CLK, 0x160b0, BIT(0), 0x160b0),
	GATE_CLK_POLLED(GCC_EMAC2_XGXS_TX_CLK, 0x160ac, BIT(0), 0x160ac),
	GATE_CLK_POLLED(GCC_GP1_CLK, 0x47000, BIT(0), 0x47000),
	GATE_CLK_POLLED(GCC_GP2_CLK, 0x48000, BIT(0), 0x48000),
	GATE_CLK_POLLED(GCC_GP3_CLK, 0x49000, BIT(0), 0x49000),
	GATE_CLK_POLLED(GCC_I2C_SLAVE_AHB_CLK, 0x7d010, BIT(30), 0x19008),
	GATE_CLK_POLLED(GCC_I2C_SLAVE_XO_CLK, 0x7d010, BIT(29), 0x19004),
	GATE_CLK_POLLED(GCC_I3C_CORE_SLV_CFG_AHB_CLK, 0x7d010, BIT(31), 0x1a004),
	GATE_CLK_POLLED(GCC_MMU_TCU_VOTE_CLK, 0x95194, BIT(0), 0x95194),
	GATE_CLK_POLLED(GCC_MVMSS_NTS_CLK, 0x6f010, BIT(0), 0x6f010),
	GATE_CLK_POLLED(GCC_MVM_AHB_CLK, 0x6f008, BIT(0), 0x6f008),
	GATE_CLK_POLLED(GCC_MVM_MASTER_AXI_CLK, 0x6f004, BIT(0), 0x6f004),
	GATE_CLK_POLLED(GCC_PCIE20_PHY_AUX_CLK, 0x7d018, BIT(5), 0x53044),
	GATE_CLK_POLLED(GCC_PCIE_1_AUX_CLK, 0x7d008, BIT(22), 0x67040),
	GATE_CLK_POLLED(GCC_PCIE_1_CFG_AHB_CLK, 0x7d008, BIT(21), 0x6703c),
	GATE_CLK_POLLED(GCC_PCIE_1_MSTR_AXI_CLK, 0x7d008, BIT(20), 0x6702c),
	GATE_CLK_POLLED(GCC_PCIE_1_PHY_AUX_CLK, 0x7d010, BIT(26), 0x56018),
	GATE_CLK_POLLED(GCC_PCIE_1_PHY_RCHNG_CLK, 0x7d008, BIT(24), 0x67078),
	GATE_CLK_POLLED(GCC_PCIE_1_PIPE_CLK, 0x7d008, BIT(23), 0x67068),
	GATE_CLK_POLLED(GCC_PCIE_1_PIPE_DIV2_CLK, 0x7d010, BIT(3), 0x6709c),
	GATE_CLK_POLLED(GCC_PCIE_1_SLV_AXI_CLK, 0x7d008, BIT(19), 0x6701c),
	GATE_CLK_POLLED(GCC_PCIE_1_SLV_Q2A_AXI_CLK, 0x7d008, BIT(18), 0x67018),
	GATE_CLK_POLLED(GCC_PCIE_2_AUX_CLK, 0x7d008, BIT(29), 0x68060),
	GATE_CLK_POLLED(GCC_PCIE_2_CFG_AHB_CLK, 0x7d008, BIT(28), 0x6803c),
	GATE_CLK_POLLED(GCC_PCIE_2_MSTR_AXI_CLK, 0x7d000, BIT(8), 0x6802c),
	GATE_CLK_POLLED(GCC_PCIE_2_PHY_AUX_CLK, 0x7d010, BIT(27), 0x6e018),
	GATE_CLK_POLLED(GCC_PCIE_2_PHY_RCHNG_CLK, 0x7d008, BIT(31), 0x680ac),
	GATE_CLK_POLLED(GCC_PCIE_2_PIPE_CLK, 0x7d008, BIT(30), 0x68088),
	GATE_CLK_POLLED(GCC_PCIE_2_PIPE_DIV2_CLK, 0x7d010, BIT(4), 0x6809c),
	GATE_CLK_POLLED(GCC_PCIE_2_SLV_AXI_CLK, 0x7d008, BIT(26), 0x6801c),
	GATE_CLK_POLLED(GCC_PCIE_2_SLV_Q2A_AXI_CLK, 0x7d008, BIT(25), 0x68018),
	GATE_CLK_POLLED(GCC_PCIE_3_AUX_CLK, 0x7d010, BIT(14), 0x13060),
	GATE_CLK_POLLED(GCC_PCIE_3_CFG_AHB_CLK, 0x7d010, BIT(13), 0x1303c),
	GATE_CLK_POLLED(GCC_PCIE_3_MSTR_AXI_CLK, 0x7d010, BIT(12), 0x1302c),
	GATE_CLK_POLLED(GCC_PCIE_3_PHY_AUX_CLK, 0x7d010, BIT(28), 0x17018),
	GATE_CLK_POLLED(GCC_PCIE_3_PHY_RCHNG_CLK, 0x7d010, BIT(17), 0x130ac),
	GATE_CLK_POLLED(GCC_PCIE_3_PIPE_CLK, 0x7d010, BIT(15), 0x13088),
	GATE_CLK_POLLED(GCC_PCIE_3_PIPE_DIV2_CLK, 0x7d010, BIT(16), 0x1309c),
	GATE_CLK_POLLED(GCC_PCIE_3_SLV_AXI_CLK, 0x7d010, BIT(11), 0x1301c),
	GATE_CLK_POLLED(GCC_PCIE_3_SLV_Q2A_AXI_CLK, 0x7d010, BIT(10), 0x13018),
	GATE_CLK_POLLED(GCC_PCIE_AUX_CLK, 0x7d018, BIT(6), 0x53054),
	GATE_CLK_POLLED(GCC_PCIE_CFG_AHB_CLK, 0x7d018, BIT(3), 0x5303c),
	GATE_CLK_POLLED(GCC_PCIE_MSTR_AXI_CLK, 0x7d018, BIT(2), 0x5302c),
	GATE_CLK_POLLED(GCC_PCIE_PHY_AUX_CLK, 0x7d018, BIT(8), 0x54018),
	GATE_CLK_POLLED(GCC_PCIE_PIPE_CLK, 0x7d018, BIT(7), 0x53064),
	GATE_CLK_POLLED(GCC_PCIE_RCHNG_PHY_CLK, 0x7d018, BIT(4), 0x53040),
	GATE_CLK_POLLED(GCC_PCIE_SLV_AXI_CLK, 0x7d018, BIT(1), 0x5301c),
	GATE_CLK_POLLED(GCC_PCIE_SLV_Q2A_AXI_CLK, 0x7d018, BIT(0), 0x53018),
	GATE_CLK_POLLED(GCC_PDM2_CLK, 0x3400c, BIT(0), 0x3400c),
	GATE_CLK_POLLED(GCC_PDM_AHB_CLK, 0x34004, BIT(0), 0x34004),
	GATE_CLK_POLLED(GCC_PDM_XO4_CLK, 0x34008, BIT(0), 0x34008),
	GATE_CLK_POLLED(GCC_PRIME_CORE_CLK, 0x10014, BIT(0), 0x10014),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_CORE_2X_CLK, 0x7d000, BIT(15), 0x2d01c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_CORE_CLK, 0x7d000, BIT(14), 0x2d008),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S0_CLK, 0x7d000, BIT(16), 0x6c004),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S1_CLK, 0x7d000, BIT(17), 0x6c140),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S2_CLK, 0x7d000, BIT(18), 0x6c27c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S3_CLK, 0x7d000, BIT(19), 0x6c3b8),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S4_CLK, 0x7d000, BIT(20), 0x6c4f4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S5_CLK, 0x7d000, BIT(21), 0x6c630),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S6_CLK, 0x7d000, BIT(22), 0x6c76c),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S7_CLK, 0x7d000, BIT(23), 0x6c8a8),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S8_CLK, 0x7d010, BIT(7), 0x6c9e4),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP0_S9_CLK, 0x7d010, BIT(18), 0x6cb20),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP_0_M_AHB_CLK, 0x7d000, BIT(12), 0x2d000),
	GATE_CLK_POLLED(GCC_QUPV3_WRAP_0_S_AHB_CLK, 0x7d000, BIT(13), 0x2d004),
	GATE_CLK_POLLED(GCC_SDCC1_AHB_CLK, 0x6b004, BIT(0), 0x6b004),
	GATE_CLK_POLLED(GCC_SDCC1_APPS_CLK, 0x6b008, BIT(0), 0x6b008),
	GATE_CLK_POLLED(GCC_SDCC1_ICE_CORE_CLK, 0x6b034, BIT(0), 0x6b034),
	GATE_CLK_POLLED(GCC_SDCC2_AHB_CLK, 0x6a014, BIT(0), 0x6a014),
	GATE_CLK_POLLED(GCC_SDCC2_APPS_CLK, 0x6a004, BIT(0), 0x6a004),
	GATE_CLK_POLLED(GCC_SYS_NOC_I2C_SLAVE_CLK, 0x7d010, BIT(19), 0x1900c),
	GATE_CLK_POLLED(GCC_SYS_NOC_I3C_SLAVE_CLK, 0x7d010, BIT(20), 0x1a008),
	GATE_CLK_POLLED(GCC_SYS_NOC_MVMSS_CLK, 0x6f018, BIT(0), 0x6f018),
	GATE_CLK_POLLED(GCC_TLMM_125_CLK, 0x2e018, BIT(0), 0x2e018),
	GATE_CLK_POLLED(GCC_USB30_ATB_CLK, 0x27094, BIT(0), 0x27094),
	GATE_CLK_POLLED(GCC_USB30_MASTER_CLK, 0x2701c, BIT(0), 0x2701c),
	GATE_CLK_POLLED(GCC_USB30_MOCK_UTMI_CLK, 0x27038, BIT(0), 0x27038),
	GATE_CLK_POLLED(GCC_USB30_MSTR_AXI_CLK, 0x2702c, BIT(0), 0x2702c),
	GATE_CLK_POLLED(GCC_USB30_SLEEP_CLK, 0x27034, BIT(0), 0x27034),
	GATE_CLK_POLLED(GCC_USB30_SLV_AHB_CLK, 0x27030, BIT(0), 0x27030),
	GATE_CLK_POLLED(GCC_USB3_PHY_AUX_CLK, 0x27070, BIT(0), 0x27070),
	/*
	 * USB3 pipe clock is sourced from the QMP PHY's own pipe output, so its
	 * branch cannot report running until the PHY serdes has been started
	 * (later, in the PHY driver's power_on). Use non-polled GATE_CLK -- as
	 * every other qcom clk driver does for USB3_PHY_PIPE -- so enabling it
	 * during clk_enable_bulk() does not poll the CBCR and time out (-EBUSY).
	 */
	GATE_CLK(GCC_USB3_PHY_PIPE_CLK, 0x27074, BIT(0)),
	GATE_CLK_POLLED(GCC_USB_PHY_CFG_AHB2PHY_CLK, 0x29004, BIT(0), 0x29004),
};

static int echo_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, echo_clks[clk->id].name);

	switch (clk->id) {
	case GCC_QUPV3_WRAP0_S0_CLK ... GCC_QUPV3_WRAP0_S9_CLK:
		qcom_gate_clk_en(priv, GCC_QUPV3_WRAP_0_M_AHB_CLK);
		qcom_gate_clk_en(priv, GCC_QUPV3_WRAP_0_S_AHB_CLK);
		break;
	}

	return qcom_gate_clk_en(priv, clk->id);
}

static const struct qcom_reset_map echo_gcc_resets[] = {
	[GCC_EMAC0_BCR] = { 0x71000 },
	[GCC_EMAC1_BCR] = { 0x72000 },
	[GCC_EMAC2_BCR] = { 0x16000 },
	[GCC_I2C_BCR] = { 0x19000 },
	[GCC_I3C_CORE_BCR] = { 0x1a000 },
	[GCC_MVMSS_BCR] = { 0x6f000 },
	[GCC_PCIE_1_BCR] = { 0x67000 },
	[GCC_PCIE_1_LINK_DOWN_BCR] = { 0x9e700 },
	[GCC_PCIE_1_NOCSR_COM_PHY_BCR] = { 0x56120 },
	[GCC_PCIE_1_PHY_BCR] = { 0x56000 },
	[GCC_PCIE_2_BCR] = { 0x68000 },
	[GCC_PCIE_2_LINK_DOWN_BCR] = { 0x9f700 },
	[GCC_PCIE_2_NOCSR_COM_PHY_BCR] = { 0x6e130 },
	[GCC_PCIE_2_PHY_BCR] = { 0x6e000 },
	[GCC_PCIE_3_BCR] = { 0x13000 },
	[GCC_PCIE_3_LINK_DOWN_BCR] = { 0x9c004 },
	[GCC_PCIE_3_NOCSR_COM_PHY_BCR] = { 0x13300 },
	[GCC_PCIE_3_PHY_BCR] = { 0x17000 },
	[GCC_PCIE_BCR] = { 0x53000 },
	[GCC_PCIE_LINK_DOWN_BCR] = { 0x87000 },
	[GCC_PCIE_NOCSR_COM_PHY_BCR] = { 0x88008 },
	[GCC_PCIE_PHY_BCR] = { 0x54000 },
	[GCC_PCIE_PHY_CFG_AHB_BCR] = { 0x88000 },
	[GCC_PCIE_PHY_COM_BCR] = { 0x88004 },
	[GCC_PCIE_PHY_NOCSR_COM_PHY_BCR] = { 0x8800c },
	[GCC_PCIE_RSCC_BCR] = { 0x3d000 },
	[GCC_PDM_BCR] = { 0x34000 },
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x6c000 },
	[GCC_QUSB2PHY_BCR] = { 0x2a000 },
	[GCC_SDCC1_BCR] = { 0x6b000 },
	[GCC_SDCC2_BCR] = { 0x6a000 },
	[GCC_TCSR_PCIE_BCR] = { 0x84000 },
	[GCC_USB30_BCR] = { 0x27004 },
	[GCC_USB3PHY_PHY_BCR] = { 0x28004 },
	[GCC_USB3_PHY_BCR] = { 0x28000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x29000 },
};

static const struct qcom_power_map echo_gdscs[] = {
	[GCC_EMAC0_GDSC] = { 0x71004 },
	[GCC_EMAC1_GDSC] = { 0x72004 },
	[GCC_EMAC2_GDSC] = { 0x16004 },
	[GCC_PCIE_1_GDSC] = { 0x67004 },
	[GCC_PCIE_1_PHY_GDSC] = { 0x56004 },
	[GCC_PCIE_2_GDSC] = { 0x68004 },
	[GCC_PCIE_2_PHY_GDSC] = { 0x6e004 },
	[GCC_PCIE_3_GDSC] = { 0x13004 },
	[GCC_PCIE_3_PHY_GDSC] = { 0x17004 },
	[GCC_PCIE_GDSC] = { 0x53004 },
	[GCC_PCIE_PHY_GDSC] = { 0x54004 },
	[GCC_USB30_GDSC] = { 0x27008 },
	[GCC_USB3_PHY_GDSC] = { 0x28008 },
};

static struct msm_clk_data echo_gcc_data = {
	.resets = echo_gcc_resets,
	.num_resets = ARRAY_SIZE(echo_gcc_resets),
	.clks = echo_clks,
	.num_clks = ARRAY_SIZE(echo_clks),

	.power_domains = echo_gdscs,
	.num_power_domains = ARRAY_SIZE(echo_gdscs),

	.enable = echo_enable,
	.set_rate = echo_set_rate,
};

static const struct udevice_id gcc_echo_of_match[] = {
	{
		.compatible = "qcom,echo-gcc",
		.data = (ulong)&echo_gcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_echo) = {
	.name		= "gcc_echo",
	.id		= UCLASS_NOP,
	.of_match	= gcc_echo_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
