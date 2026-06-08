// SPDX-License-Identifier: GPL-2.0-only
/*
 * Clock drivers for Qualcomm shikra
 *
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

/* USB 3.0 Primary */
#define USB30_PRIM_MASTER_CLK_CMD_RCGR		0x1a01c
#define USB30_PRIM_MOCK_UTMI_CLK_CMD_RCGR	0x1a034
#define USB3_PRIM_PHY_AUX_CMD_RCGR		0x1a060

/* USB 2.0 */
#define USB20_MASTER_CLK_CMD_RCGR		0xb003c
#define USB20_MOCK_UTMI_CLK_CMD_RCGR		0xb0020

/* QUPV3 */
#define QUPV3_WRAP0_S0_CLK_CMD_RCGR		0x1f148
#define QUPV3_WRAP0_S1_CLK_CMD_RCGR		0x1f278
#define QUPV3_WRAP0_S2_CLK_CMD_RCGR		0x1f3a8
#define QUPV3_WRAP0_S3_CLK_CMD_RCGR		0x1f4d8
#define QUPV3_WRAP0_S4_CLK_CMD_RCGR		0x1f608
#define QUPV3_WRAP0_S5_CLK_CMD_RCGR		0x1f738
#define QUPV3_WRAP0_S6_CLK_CMD_RCGR		0x1f868
#define QUPV3_WRAP0_S7_CLK_CMD_RCGR		0x1f998
#define QUPV3_WRAP0_S8_CLK_CMD_RCGR		0x1fac8
#define QUPV3_WRAP0_S9_CLK_CMD_RCGR		0x1fbf8

/* SDCC1 */
#define SDCC1_APPS_CLK_CMD_RCGR			0x38028
#define SDCC1_ICE_CORE_CLK_CMD_RCGR		0x38010

/* SDCC2 */
#define SDCC2_APPS_CLK_CMD_RCGR			0x1e00c

static const struct freq_tbl ftbl_gcc_usb30_prim_master_clk_src[] = {
	F(66666667, CFG_CLK_SRC_GPLL0_AUX2, 4.5, 0, 0),
	F(133333333, CFG_CLK_SRC_GPLL0, 4.5, 0, 0),
	F(200000000, CFG_CLK_SRC_GPLL0, 3, 0, 0),
	F(240000000, CFG_CLK_SRC_GPLL0, 2.5, 0, 0),
	{ }
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

static const struct freq_tbl ftbl_gcc_sdcc1_ice_core_clk_src[] = {
	F(75000000, CFG_CLK_SRC_GPLL0_AUX2, 4, 0, 0),
	F(100000000, CFG_CLK_SRC_GPLL0_AUX2, 3, 0, 0),
	F(150000000, CFG_CLK_SRC_GPLL0_AUX2, 2, 0, 0),
	F(200000000, CFG_CLK_SRC_GPLL0, 3, 0, 0),
	F(300000000, CFG_CLK_SRC_GPLL0_AUX2, 1, 0, 0),
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

static const struct freq_tbl ftbl_gcc_usb20_master_clk_src[] = {
	F(60000000, CFG_CLK_SRC_GPLL0_AUX2, 5, 0, 0),
	F(120000000, CFG_CLK_SRC_GPLL0, 5, 0, 0),
	{ }
};

static ulong shikra_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	const struct freq_tbl *freq;

	if (clk->id < priv->data->num_clks)
		debug("%s: %s, requested rate=%ld\n", __func__,
		      priv->data->clks[clk->id].name, rate);

	switch (clk->id) {
	case GCC_USB30_PRIM_MASTER_CLK:
		freq = qcom_find_freq(ftbl_gcc_usb30_prim_master_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, USB30_PRIM_MASTER_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_USB30_PRIM_MOCK_UTMI_CLK:
		clk_rcg_set_rate(priv->base, USB30_PRIM_MOCK_UTMI_CLK_CMD_RCGR, 1, 0);
		return 19200000;
	case GCC_USB3_PRIM_PHY_COM_AUX_CLK:
		clk_rcg_set_rate(priv->base, USB3_PRIM_PHY_AUX_CMD_RCGR, 1, 0);
		return 19200000;
	case GCC_USB20_MASTER_CLK:
		freq = qcom_find_freq(ftbl_gcc_usb20_master_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, USB20_MASTER_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_USB20_MOCK_UTMI_CLK:
		clk_rcg_set_rate(priv->base, USB20_MOCK_UTMI_CLK_CMD_RCGR, 1, 0);
		return 19200000;
	case GCC_QUPV3_WRAP0_S0_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S0_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S1_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S1_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S2_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S2_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S3_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S3_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S4_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S4_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S5_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S5_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S6_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S6_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S7_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S7_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S8_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S8_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_QUPV3_WRAP0_S9_CLK:
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap0_s0_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, QUPV3_WRAP0_S9_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_SDCC1_APPS_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc1_apps_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, SDCC1_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_SDCC1_ICE_CORE_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc1_ice_core_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, SDCC1_ICE_CORE_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	case GCC_SDCC2_APPS_CLK:
		freq = qcom_find_freq(ftbl_gcc_sdcc2_apps_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, SDCC2_APPS_CLK_CMD_RCGR,
				     freq->pre_div, freq->m, freq->n, freq->src, 8);
		return freq->freq;
	default:
		return rate;
	}
}

static const struct gate_clk shikra_clks[] = {
	GATE_CLK(GCC_AHB2PHY_CSI_CLK, 0x1d004, BIT(0)),
	GATE_CLK(GCC_AHB2PHY_USB_CLK, 0x1d008, BIT(0)),
	GATE_CLK(GCC_CFG_NOC_USB2_PRIM_AXI_CLK, 0x111c4, BIT(0)),
	GATE_CLK(GCC_CFG_NOC_USB3_PRIM_AXI_CLK, 0x1a07c, BIT(0)),
	GATE_CLK(GCC_QUPV3_WRAP0_S0_CLK, 0x79004, BIT(22)),
	GATE_CLK(GCC_QUPV3_WRAP0_S1_CLK, 0x79004, BIT(23)),
	GATE_CLK(GCC_QUPV3_WRAP0_S2_CLK, 0x79004, BIT(24)),
	GATE_CLK(GCC_QUPV3_WRAP0_S3_CLK, 0x79004, BIT(25)),
	GATE_CLK(GCC_QUPV3_WRAP0_S4_CLK, 0x79004, BIT(26)),
	GATE_CLK(GCC_QUPV3_WRAP0_S5_CLK, 0x79004, BIT(27)),
	GATE_CLK(GCC_QUPV3_WRAP0_S6_CLK, 0x79004, BIT(28)),
	GATE_CLK(GCC_QUPV3_WRAP0_S7_CLK, 0x79004, BIT(29)),
	GATE_CLK(GCC_QUPV3_WRAP0_S8_CLK, 0x79004, BIT(30)),
	GATE_CLK(GCC_QUPV3_WRAP0_S9_CLK, 0x79004, BIT(31)),
	GATE_CLK(GCC_SDCC1_AHB_CLK, 0x38008, BIT(0)),
	GATE_CLK(GCC_SDCC1_APPS_CLK, 0x38004, BIT(0)),
	GATE_CLK(GCC_SDCC1_ICE_CORE_CLK, 0x3800c, BIT(0)),
	GATE_CLK(GCC_SDCC2_AHB_CLK, 0x1e008, BIT(0)),
	GATE_CLK(GCC_SDCC2_APPS_CLK, 0x1e004, BIT(0)),
	GATE_CLK(GCC_SYS_NOC_USB2_PRIM_AXI_CLK, 0x10a14, BIT(0)),
	GATE_CLK(GCC_SYS_NOC_USB3_PRIM_AXI_CLK, 0x1a078, BIT(0)),
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
};

static int shikra_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks <= clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, shikra_clks[clk->id].name);

	switch (clk->id) {
	case GCC_SYS_NOC_USB3_PRIM_AXI_CLK:
		qcom_gate_clk_en(priv, GCC_USB30_PRIM_MASTER_CLK);
		fallthrough;
	case GCC_USB30_PRIM_MASTER_CLK:
		qcom_gate_clk_en(priv, GCC_USB3_PRIM_PHY_COM_AUX_CLK);
		break;
	case GCC_SYS_NOC_USB2_PRIM_AXI_CLK:
		qcom_gate_clk_en(priv, GCC_USB20_MASTER_CLK);
		fallthrough;
	case GCC_USB20_MASTER_CLK:
		qcom_gate_clk_en(priv, GCC_USB20_MOCK_UTMI_CLK);
		qcom_gate_clk_en(priv, GCC_USB20_SLEEP_CLK);
		break;
	}

	return qcom_gate_clk_en(priv, clk->id);
}

static const struct qcom_reset_map shikra_gcc_resets[] = {
	[GCC_QUPV3_WRAPPER_0_BCR] = { 0x1f000 },
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x1c000 },
	[GCC_SDCC1_BCR] = { 0x38000 },
	[GCC_SDCC2_BCR] = { 0x1e000 },
	[GCC_USB20_BCR] = { 0xb0000 },
	[GCC_USB30_PRIM_BCR] = { 0x1a000 },
	[GCC_USB3PHY_PHY_PRIM_SP0_BCR] = { 0x1b008 },
	[GCC_USB3_PHY_PRIM_SP0_BCR] = { 0x1b000 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x1d000 },
};

static const struct qcom_power_map shikra_gdscs[] = {
	[GCC_USB20_GDSC] = { 0xb0004 },
	[GCC_USB30_PRIM_GDSC] = { 0x1a004 },
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
