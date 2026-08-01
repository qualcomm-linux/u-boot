// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <dm.h>

#include "pinctrl-qcom.h"

#define MAX_PIN_NAME_LEN 32
static char pin_name[MAX_PIN_NAME_LEN] __section(".data");

typedef unsigned int msm_pin_function[12];

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11)\
	{						\
		msm_mux_gpio, /* gpio mode */		\
		msm_mux_##f1,				\
		msm_mux_##f2,				\
		msm_mux_##f3,				\
		msm_mux_##f4,				\
		msm_mux_##f5,				\
		msm_mux_##f6,				\
		msm_mux_##f7,				\
		msm_mux_##f8,				\
		msm_mux_##f9,				\
		msm_mux_##f10,				\
		msm_mux_##f11				\
	}

#define SDC_QDSD_PINGROUP(pg_name, ctl, pull, drv)	\
	{						\
		.name = pg_name,			\
		.ctl_reg = ctl,				\
		.io_reg = 0,				\
		.pull_bit = pull,			\
		.drv_bit = drv,				\
		.oe_bit = -1,				\
		.in_bit = -1,				\
		.out_bit = -1,				\
	}

enum shikra_functions {
	msm_mux_gpio,
	msm_mux_agera_pll,
	msm_mux_atest_bbrx,
	msm_mux_atest_char,
	msm_mux_atest_gpsadc,
	msm_mux_atest_tsens,
	msm_mux_atest_usb,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c0,
	msm_mux_cci_i2c1,
	msm_mux_cci_timer,
	msm_mux_char_exec,
	msm_mux_cri_trng,
	msm_mux_dac_calib,
	msm_mux_dbg_out_clk,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi,
	msm_mux_dmic,
	msm_mux_emac_dll,
	msm_mux_emac_mcg,
	msm_mux_emac_phy,
	msm_mux_emac0_ptp_aux,
	msm_mux_emac0_ptp_pps,
	msm_mux_emac1_ptp_aux,
	msm_mux_emac1_ptp_pps,
	msm_mux_ext_mclk,
	msm_mux_gcc_gp,
	msm_mux_gsm0_tx,
	msm_mux_i2s0,
	msm_mux_i2s1,
	msm_mux_i2s2,
	msm_mux_i2s3,
	msm_mux_jitter_bist,
	msm_mux_m_voc,
	msm_mux_mdp_vsync_e,
	msm_mux_mdp_vsync_out0,
	msm_mux_mdp_vsync_out1,
	msm_mux_mdp_vsync_p,
	msm_mux_mdp_vsync_s,
	msm_mux_mpm_pwr,
	msm_mux_mss_lte,
	msm_mux_nav_gpio,
	msm_mux_pa_indicator_or,
	msm_mux_pbs_in,
	msm_mux_pbs_out,
	msm_mux_pcie0_clk_req_n,
	msm_mux_phase_flag,
	msm_mux_pll,
	msm_mux_prng_rosc,
	msm_mux_pwm,
	msm_mux_qdss_cti,
	msm_mux_qup0_se0,
	msm_mux_qup0_se1,
	msm_mux_qup0_se1_01,
	msm_mux_qup0_se1_23,
	msm_mux_qup0_se2,
	msm_mux_qup0_se3_01,
	msm_mux_qup0_se3_23,
	msm_mux_qup0_se4_01,
	msm_mux_qup0_se4_23,
	msm_mux_qup0_se5,
	msm_mux_qup0_se6,
	msm_mux_qup0_se7_01,
	msm_mux_qup0_se7_23,
	msm_mux_qup0_se8,
	msm_mux_qup0_se9,
	msm_mux_qup0_se9_01,
	msm_mux_qup0_se9_23,
	msm_mux_rgmii,
	msm_mux_sd_write_protect,
	msm_mux_sdc_cdc,
	msm_mux_sdc_tb_trig,
	msm_mux_ssbi_wtr,
	msm_mux_swr0_rx,
	msm_mux_swr0_tx,
	msm_mux_tgu_ch_trigout,
	msm_mux_tsc_async,
	msm_mux_tsense_pwm,
	msm_mux_uim1,
	msm_mux_uim2,
	msm_mux_unused_adsp,
	msm_mux_unused_gsm1,
	msm_mux_usb0_phy_ps,
	msm_mux_vfr,
	msm_mux_vsense_trigger_mirnat,
	msm_mux_wlan,
	msm_mux__,
};

#define MSM_PIN_FUNCTION(fname)                         \
	[msm_mux_##fname] = {#fname, msm_mux_##fname}

static const struct pinctrl_function msm_pinctrl_functions[] = {
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(agera_pll),
	MSM_PIN_FUNCTION(atest_bbrx),
	MSM_PIN_FUNCTION(atest_char),
	MSM_PIN_FUNCTION(atest_gpsadc),
	MSM_PIN_FUNCTION(atest_tsens),
	MSM_PIN_FUNCTION(atest_usb),
	MSM_PIN_FUNCTION(cam_mclk),
	MSM_PIN_FUNCTION(cci_async),
	MSM_PIN_FUNCTION(cci_i2c0),
	MSM_PIN_FUNCTION(cci_i2c1),
	MSM_PIN_FUNCTION(cci_timer),
	MSM_PIN_FUNCTION(char_exec),
	MSM_PIN_FUNCTION(cri_trng),
	MSM_PIN_FUNCTION(dac_calib),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(ddr_bist),
	MSM_PIN_FUNCTION(ddr_pxi),
	MSM_PIN_FUNCTION(dmic),
	MSM_PIN_FUNCTION(emac_dll),
	MSM_PIN_FUNCTION(emac_mcg),
	MSM_PIN_FUNCTION(emac_phy),
	MSM_PIN_FUNCTION(emac0_ptp_aux),
	MSM_PIN_FUNCTION(emac0_ptp_pps),
	MSM_PIN_FUNCTION(emac1_ptp_aux),
	MSM_PIN_FUNCTION(emac1_ptp_pps),
	MSM_PIN_FUNCTION(ext_mclk),
	MSM_PIN_FUNCTION(gcc_gp),
	MSM_PIN_FUNCTION(gsm0_tx),
	MSM_PIN_FUNCTION(i2s0),
	MSM_PIN_FUNCTION(i2s1),
	MSM_PIN_FUNCTION(i2s2),
	MSM_PIN_FUNCTION(i2s3),
	MSM_PIN_FUNCTION(jitter_bist),
	MSM_PIN_FUNCTION(m_voc),
	MSM_PIN_FUNCTION(mdp_vsync_e),
	MSM_PIN_FUNCTION(mdp_vsync_out0),
	MSM_PIN_FUNCTION(mdp_vsync_out1),
	MSM_PIN_FUNCTION(mdp_vsync_p),
	MSM_PIN_FUNCTION(mdp_vsync_s),
	MSM_PIN_FUNCTION(mpm_pwr),
	MSM_PIN_FUNCTION(mss_lte),
	MSM_PIN_FUNCTION(nav_gpio),
	MSM_PIN_FUNCTION(pa_indicator_or),
	MSM_PIN_FUNCTION(pbs_in),
	MSM_PIN_FUNCTION(pbs_out),
	MSM_PIN_FUNCTION(pcie0_clk_req_n),
	MSM_PIN_FUNCTION(phase_flag),
	MSM_PIN_FUNCTION(pll),
	MSM_PIN_FUNCTION(prng_rosc),
	MSM_PIN_FUNCTION(pwm),
	MSM_PIN_FUNCTION(qdss_cti),
	MSM_PIN_FUNCTION(qup0_se0),
	MSM_PIN_FUNCTION(qup0_se1),
	MSM_PIN_FUNCTION(qup0_se1_01),
	MSM_PIN_FUNCTION(qup0_se1_23),
	MSM_PIN_FUNCTION(qup0_se2),
	MSM_PIN_FUNCTION(qup0_se3_01),
	MSM_PIN_FUNCTION(qup0_se3_23),
	MSM_PIN_FUNCTION(qup0_se4_01),
	MSM_PIN_FUNCTION(qup0_se4_23),
	MSM_PIN_FUNCTION(qup0_se5),
	MSM_PIN_FUNCTION(qup0_se6),
	MSM_PIN_FUNCTION(qup0_se7_01),
	MSM_PIN_FUNCTION(qup0_se7_23),
	MSM_PIN_FUNCTION(qup0_se8),
	MSM_PIN_FUNCTION(qup0_se9),
	MSM_PIN_FUNCTION(qup0_se9_01),
	MSM_PIN_FUNCTION(qup0_se9_23),
	MSM_PIN_FUNCTION(rgmii),
	MSM_PIN_FUNCTION(sd_write_protect),
	MSM_PIN_FUNCTION(sdc_cdc),
	MSM_PIN_FUNCTION(sdc_tb_trig),
	MSM_PIN_FUNCTION(ssbi_wtr),
	MSM_PIN_FUNCTION(swr0_rx),
	MSM_PIN_FUNCTION(swr0_tx),
	MSM_PIN_FUNCTION(tgu_ch_trigout),
	MSM_PIN_FUNCTION(tsc_async),
	MSM_PIN_FUNCTION(tsense_pwm),
	MSM_PIN_FUNCTION(uim1),
	MSM_PIN_FUNCTION(uim2),
	MSM_PIN_FUNCTION(unused_adsp),
	MSM_PIN_FUNCTION(unused_gsm1),
	MSM_PIN_FUNCTION(usb0_phy_ps),
	MSM_PIN_FUNCTION(vfr),
	MSM_PIN_FUNCTION(vsense_trigger_mirnat),
	MSM_PIN_FUNCTION(wlan),
};

static const msm_pin_function shikra_pin_functions[] = {
	[0] = PINGROUP(0, qup0_se0, m_voc, _, phase_flag, _, _, _, _, _, _, _),
	[1] = PINGROUP(1, qup0_se0, mpm_pwr, ddr_bist, _, phase_flag, atest_tsens, _, _, _, _,
			_),
	[2] = PINGROUP(2, qup0_se0, ddr_bist, _, phase_flag, atest_tsens, _, _, _, _, _, _),
	[3] = PINGROUP(3, qup0_se0, ddr_bist, _, phase_flag, dac_calib, _, _, _, _, _, _),
	[4] = PINGROUP(4, qup0_se1_23, qup0_se1_01, ddr_bist, _, phase_flag, dac_calib, _, _, _,
			_, _),
	[5] = PINGROUP(5, qup0_se1_23, qup0_se1_01, _, phase_flag, dac_calib, _, _, _, _, _, _),
	[6] = PINGROUP(6, qup0_se2, cri_trng, _, phase_flag, dac_calib, _, _, _, _, _, _),
	[7] = PINGROUP(7, qup0_se2, cri_trng, _, phase_flag, dac_calib, _, _, _, _, _, _),
	[8] = PINGROUP(8, qup0_se2, _, phase_flag, dac_calib, _, _, _, _, _, _, _),
	[9] = PINGROUP(9, qup0_se2, _, phase_flag, dac_calib, _, _, _, _, _, _, _),
	[10] = PINGROUP(10, qup0_se3_01, qup0_se3_23, _, _, _, _, _, _, _, _, _),
	[11] = PINGROUP(11, qup0_se3_01, qup0_se3_23, _, phase_flag, _, _, _, _, _, _, _),
	[12] = PINGROUP(12, qup0_se4_01, qup0_se4_23, char_exec, _, _, _, _, _, _, _, _),
	[13] = PINGROUP(13, qup0_se4_01, qup0_se4_23, char_exec, _, _, _, _, _, _, _, _),
	[14] = PINGROUP(14, qup0_se5, pll, tgu_ch_trigout, dac_calib, wlan, _, _, _, _, _, _),
	[15] = PINGROUP(15, qup0_se5, tgu_ch_trigout, _, dac_calib, wlan, _, _, _, _, _, _),
	[16] = PINGROUP(16, qup0_se5, tgu_ch_trigout, _, phase_flag, dac_calib, _, _, _, _, _,
			_),
	[17] = PINGROUP(17, qup0_se5, tgu_ch_trigout, _, phase_flag, dac_calib, _, _, _, _, _,
			_),
	[18] = PINGROUP(18, qup0_se6, dac_calib, _, _, _, _, _, _, _, _, _),
	[19] = PINGROUP(19, qup0_se6, dac_calib, _, _, _, _, _, _, _, _, _),
	[20] = PINGROUP(20, qup0_se7_01, qup0_se7_23, cri_trng, _, _, _, _, _, _, _, _),
	[21] = PINGROUP(21, qup0_se7_01, qup0_se7_23, tsense_pwm, _, _, _, _, _, _, _, _),
	[22] = PINGROUP(22, qup0_se8, pll, agera_pll, pbs_out, _, _, _, _, _, _, _),
	[23] = PINGROUP(23, qup0_se8, agera_pll, pbs_out, _, _, _, _, _, _, _, _),
	[24] = PINGROUP(24, qup0_se8, pbs_out, _, _, _, _, _, _, _, _, _),
	[25] = PINGROUP(25, qup0_se8, _, _, _, _, _, _, _, _, _, _),
	[26] = PINGROUP(26, qup0_se9_23, qup0_se9_01, _, _, _, _, _, _, _, _, _),
	[27] = PINGROUP(27, qup0_se9_23, qup0_se9_01, prng_rosc, _, _, _, _, _, _, _, _),
	[28] = PINGROUP(28, qup0_se1, qup0_se6, emac_mcg, prng_rosc, _, phase_flag, qdss_cti, _,
			_, _, _),
	[29] = PINGROUP(29, qup0_se1, qup0_se6, emac_mcg, _, phase_flag, qdss_cti, _, _, _, _,
			_),
	[30] = PINGROUP(30, qup0_se2, qup0_se6, _, phase_flag, qdss_cti, _, _, _, _, _, _),
	[31] = PINGROUP(31, qup0_se2, qup0_se6, emac1_ptp_aux, emac1_ptp_pps, _, phase_flag,
			qdss_cti, _, _, _, _),
	[32] = PINGROUP(32, pwm, sdc_tb_trig, _, _, _, _, _, _, _, _, _),
	[33] = PINGROUP(33, emac1_ptp_aux, emac1_ptp_pps, sdc_tb_trig, _, _, _, _, _, _, _, _),
	[34] = PINGROUP(34, cam_mclk, _, _, _, _, _, _, _, _, _, _),
	[35] = PINGROUP(35, cam_mclk, unused_adsp, _, _, _, _, _, _, _, _, _),
	[36] = PINGROUP(36, cci_i2c0, _, _, _, _, _, _, _, _, _, _),
	[37] = PINGROUP(37, cci_i2c0, _, _, _, _, _, _, _, _, _, _),
	[38] = PINGROUP(38, cci_timer, _, _, _, _, _, _, _, _, _, _),
	[39] = PINGROUP(39, cci_async, _, _, _, _, _, _, _, _, _, _),
	[40] = PINGROUP(40, cci_timer, emac_mcg, pwm, _, _, _, _, _, _, _, _),
	[41] = PINGROUP(41, cci_i2c1, _, _, _, _, _, _, _, _, _, _),
	[42] = PINGROUP(42, cci_i2c1, _, _, _, _, _, _, _, _, _, _),
	[43] = PINGROUP(43, cci_timer, emac_mcg, pll, _, _, _, _, _, _, _, _),
	[44] = PINGROUP(44, emac_mcg, pll, _, _, _, _, _, _, _, _, _),
	[45] = PINGROUP(45, tsc_async, emac_mcg, pwm, gcc_gp, _, _, _, _, _, _, _),
	[46] = PINGROUP(46, tsc_async, emac_mcg, _, _, _, _, _, _, _, _, _),
	[47] = PINGROUP(47, cci_timer, emac_mcg, _, _, _, _, _, _, _, _, _),
	[48] = PINGROUP(48, _, qup0_se9, _, _, pbs_in, phase_flag, _, _, _, _, _),
	[49] = PINGROUP(49, _, qup0_se9, _, _, pbs_in, phase_flag, _, _, _, _, _),
	[50] = PINGROUP(50, _, qup0_se9, _, _, pbs_in, phase_flag, _, _, _, _, _),
	[51] = PINGROUP(51, _, qup0_se9, pbs_in, _, _, _, _, _, _, _, _),
	[52] = PINGROUP(52, _, _, _, _, _, _, _, _, _, _, _),
	[53] = PINGROUP(53, _, nav_gpio, gcc_gp, pwm, _, pbs_in, atest_usb, _, _, _, _),
	[54] = PINGROUP(54, _, pwm, _, pbs_in, phase_flag, atest_char, _, _, _, _, _),
	[55] = PINGROUP(55, _, pwm, _, pbs_in, phase_flag, atest_char, _, _, _, _, _),
	[56] = PINGROUP(56, _, pwm, _, pbs_in, phase_flag, atest_char, _, _, _, _, _),
	[57] = PINGROUP(57, _, pwm, _, pbs_in, phase_flag, atest_char, _, _, _, _, _),
	[58] = PINGROUP(58, _, nav_gpio, pwm, _, pbs_in, atest_bbrx, atest_usb,
			vsense_trigger_mirnat, emac_dll, _, _),
	[59] = PINGROUP(59, _, vfr, _, pbs_in, atest_bbrx, atest_usb, emac_dll, _, _, _, _),
	[60] = PINGROUP(60, _, emac1_ptp_aux, emac1_ptp_pps, emac0_ptp_aux, emac0_ptp_pps, _,
			pbs_in, atest_gpsadc, atest_usb, emac_dll, _),
	[61] = PINGROUP(61, _, pwm, gcc_gp, pa_indicator_or, dbg_out_clk, pbs_in, atest_usb,
			emac_dll, _, _, _),
	[62] = PINGROUP(62, _, pwm, _, pbs_in, phase_flag, atest_char, _, _, _, _, _),
	[63] = PINGROUP(63, _, nav_gpio, emac0_ptp_aux, emac0_ptp_pps, _, pbs_in, phase_flag,
			dac_calib, _, _, _),
	[64] = PINGROUP(64, _, unused_gsm1, dac_calib, _, _, _, _, _, _, _, _),
	[65] = PINGROUP(65, _, _, _, _, _, _, _, _, _, _, _),
	[66] = PINGROUP(66, _, dac_calib, _, _, _, _, _, _, _, _, _),
	[67] = PINGROUP(67, _, _, _, _, _, _, _, _, _, _, _),
	[68] = PINGROUP(68, _, ssbi_wtr, emac1_ptp_aux, emac1_ptp_pps, pwm, dac_calib, _, _, _,
			_, _),
	[69] = PINGROUP(69, _, ssbi_wtr, emac0_ptp_aux, emac0_ptp_pps, _, phase_flag, dac_calib,
			_, _, _, _),
	[70] = PINGROUP(70, _, ssbi_wtr, _, phase_flag, dac_calib, _, _, _, _, _, _),
	[71] = PINGROUP(71, _, ssbi_wtr, nav_gpio, _, phase_flag, _, _, _, _, _, _),
	[72] = PINGROUP(72, _, _, phase_flag, _, _, _, _, _, _, _, _),
	[73] = PINGROUP(73, _, _, _, _, _, _, _, _, _, _, _),
	[74] = PINGROUP(74, pll, _, pbs_in, phase_flag, _, _, _, _, _, _, _),
	[75] = PINGROUP(75, gsm0_tx, _, _, _, _, _, _, _, _, _, _),
	[76] = PINGROUP(76, pll, _, _, _, _, _, _, _, _, _, _),
	[77] = PINGROUP(77, uim2, pwm, _, _, _, _, _, _, _, _, _),
	[78] = PINGROUP(78, uim2, _, _, _, _, _, _, _, _, _, _),
	[79] = PINGROUP(79, uim2, pwm, _, _, _, _, _, _, _, _, _),
	[80] = PINGROUP(80, uim2, pwm, _, _, _, _, _, _, _, _, _),
	[81] = PINGROUP(81, uim1, _, _, _, _, _, _, _, _, _, _),
	[82] = PINGROUP(82, uim1, _, _, _, _, _, _, _, _, _, _),
	[83] = PINGROUP(83, uim1, _, _, _, _, _, _, _, _, _, _),
	[84] = PINGROUP(84, uim1, _, _, _, _, _, _, _, _, _, _),
	[85] = PINGROUP(85, emac0_ptp_aux, emac0_ptp_pps, _, _, _, _, _, _, _, _, _),
	[86] = PINGROUP(86, mdp_vsync_p, mdp_vsync_out0, mdp_vsync_out1, _, _, _, _, _, _, _, _),
	[87] = PINGROUP(87, _, pwm, _, _, _, _, _, _, _, _, _),
	[88] = PINGROUP(88, gcc_gp, _, dac_calib, _, _, _, _, _, _, _, _),
	[89] = PINGROUP(89, gcc_gp, _, dac_calib, _, _, _, _, _, _, _, _),
	[90] = PINGROUP(90, usb0_phy_ps, _, dac_calib, _, _, _, _, _, _, _, _),
	[91] = PINGROUP(91, nav_gpio, _, _, _, _, _, _, _, _, _, _),
	[92] = PINGROUP(92, nav_gpio, _, _, _, _, _, _, _, _, _, _),
	[93] = PINGROUP(93, _, _, _, _, _, _, _, _, _, _, _),
	[94] = PINGROUP(94, mdp_vsync_e, qdss_cti, qdss_cti, _, _, _, _, _, _, _, _),
	[95] = PINGROUP(95, nav_gpio, mdp_vsync_s, qdss_cti, qdss_cti, _, _, _, _, _, _, _),
	[96] = PINGROUP(96, dmic, cam_mclk, i2s1, jitter_bist, atest_gpsadc, atest_usb, _, _, _,
			_, _),
	[97] = PINGROUP(97, dmic, i2s1, dac_calib, _, _, _, _, _, _, _, _),
	[98] = PINGROUP(98, dmic, cam_mclk, i2s1, _, sdc_cdc, atest_usb, ddr_pxi, _, _, _, _),
	[99] = PINGROUP(99, dmic, i2s1, jitter_bist, sdc_cdc, atest_usb, ddr_pxi, _, _, _, _, _),
	[100] = PINGROUP(100, i2s2, nav_gpio, _, sdc_cdc, atest_usb, ddr_pxi, _, _, _, _, _),
	[101] = PINGROUP(101, i2s2, nav_gpio, _, sdc_cdc, atest_usb, ddr_pxi, _, _, _, _, _),
	[102] = PINGROUP(102, i2s2, pwm, _, phase_flag, _, _, _, _, _, _, _),
	[103] = PINGROUP(103, ext_mclk, i2s2, _, _, _, _, _, _, _, _, _),
	[104] = PINGROUP(104, ext_mclk, nav_gpio, _, _, _, _, _, _, _, _, _),
	[105] = PINGROUP(105, swr0_tx, i2s0, _, _, _, _, _, _, _, _, _),
	[106] = PINGROUP(106, swr0_tx, i2s0, _, _, _, _, _, _, _, _, _),
	[107] = PINGROUP(107, swr0_rx, i2s0, _, _, _, _, _, _, _, _, _),
	[108] = PINGROUP(108, swr0_rx, i2s0, _, _, _, _, _, _, _, _, _),
	[109] = PINGROUP(109, swr0_rx, i2s0, sd_write_protect, _, _, _, _, _, _, _, _),
	[110] = PINGROUP(110, ext_mclk, i2s0, _, gcc_gp, _, _, _, _, _, _, _),
	[111] = PINGROUP(111, i2s3, _, _, _, _, _, _, _, _, _, _),
	[112] = PINGROUP(112, i2s3, _, _, _, _, _, _, _, _, _, _),
	[113] = PINGROUP(113, i2s3, _, _, _, _, _, _, _, _, _, _),
	[114] = PINGROUP(114, ext_mclk, i2s3, _, _, _, _, _, _, _, _, _),
	[115] = PINGROUP(115, mss_lte, _, _, _, _, _, _, _, _, _, _),
	[116] = PINGROUP(116, mss_lte, _, dac_calib, _, _, _, _, _, _, _, _),
	[117] = PINGROUP(117, pcie0_clk_req_n, _, dac_calib, _, _, _, _, _, _, _, _),
	[118] = PINGROUP(118, _, dac_calib, _, _, _, _, _, _, _, _, _),
	[119] = PINGROUP(119, _, _, _, _, _, _, _, _, _, _, _),
	[120] = PINGROUP(120, emac_phy, _, _, _, _, _, _, _, _, _, _),
	[121] = PINGROUP(121, rgmii, _, _, _, _, _, _, _, _, _, _),
	[122] = PINGROUP(122, rgmii, _, _, _, _, _, _, _, _, _, _),
	[123] = PINGROUP(123, rgmii, _, _, _, _, _, _, _, _, _, _),
	[124] = PINGROUP(124, rgmii, _, _, _, _, _, _, _, _, _, _),
	[125] = PINGROUP(125, rgmii, _, _, _, _, _, _, _, _, _, _),
	[126] = PINGROUP(126, rgmii, _, _, _, _, _, _, _, _, _, _),
	[127] = PINGROUP(127, rgmii, _, _, _, _, _, _, _, _, _, _),
	[128] = PINGROUP(128, rgmii, _, _, _, _, _, _, _, _, _, _),
	[129] = PINGROUP(129, rgmii, _, _, _, _, _, _, _, _, _, _),
	[130] = PINGROUP(130, rgmii, _, _, _, _, _, _, _, _, _, _),
	[131] = PINGROUP(131, rgmii, _, _, _, _, _, _, _, _, _, _),
	[132] = PINGROUP(132, rgmii, _, _, _, _, _, _, _, _, _, _),
	[133] = PINGROUP(133, rgmii, _, _, _, _, _, _, _, _, _, _),
	[134] = PINGROUP(134, rgmii, _, _, _, _, _, _, _, _, _, _),
	[135] = PINGROUP(135, _, _, _, _, _, _, _, _, _, _, _),
	[136] = PINGROUP(136, emac_phy, _, _, _, _, _, _, _, _, _, _),
	[137] = PINGROUP(137, rgmii, _, _, _, _, _, _, _, _, _, _),
	[138] = PINGROUP(138, rgmii, _, _, _, _, _, _, _, _, _, _),
	[139] = PINGROUP(139, rgmii, _, _, _, _, _, _, _, _, _, _),
	[140] = PINGROUP(140, rgmii, _, _, _, _, _, _, _, _, _, _),
	[141] = PINGROUP(141, rgmii, _, _, _, _, _, _, _, _, _, _),
	[142] = PINGROUP(142, rgmii, _, _, _, _, _, _, _, _, _, _),
	[143] = PINGROUP(143, rgmii, _, _, _, _, _, _, _, _, _, _),
	[144] = PINGROUP(144, rgmii, _, _, _, _, _, _, _, _, _, _),
	[145] = PINGROUP(145, rgmii, _, _, _, _, _, _, _, _, _, _),
	[146] = PINGROUP(146, rgmii, _, _, _, _, _, _, _, _, _, _),
	[147] = PINGROUP(147, rgmii, _, _, _, _, _, _, _, _, _, _),
	[148] = PINGROUP(148, rgmii, _, _, _, _, _, _, _, _, _, _),
	[149] = PINGROUP(149, rgmii, _, _, _, _, _, _, _, _, _, _),
	[150] = PINGROUP(150, rgmii, _, _, _, _, _, _, _, _, _, _),
	[151] = PINGROUP(151, _, _, _, _, _, _, _, _, _, _, _),
	[152] = PINGROUP(152, _, _, _, _, _, _, _, _, _, _, _),
	[153] = PINGROUP(153, _, _, _, _, _, _, _, _, _, _, _),
	[154] = PINGROUP(154, _, _, _, _, _, _, _, _, _, _, _),
	[155] = PINGROUP(155, _, _, _, _, _, _, _, _, _, _, _),
	[156] = PINGROUP(156, _, _, _, _, _, _, _, _, _, _, _),
	[157] = PINGROUP(157, _, _, _, _, _, _, _, _, _, _, _),
	[158] = PINGROUP(158, _, _, _, _, _, _, _, _, _, _, _),
	[159] = PINGROUP(159, _, _, _, _, _, _, _, _, _, _, _),
	[160] = PINGROUP(160, _, _, _, _, _, _, _, _, _, _, _),
	[161] = PINGROUP(161, _, _, _, _, _, _, _, _, _, _, _),
	[162] = PINGROUP(162, _, _, _, _, _, _, _, _, _, _, _),
	[163] = PINGROUP(163, _, _, _, _, _, _, _, _, _, _, _),
	[164] = PINGROUP(164, _, _, _, _, _, _, _, _, _, _, _),
	[165] = PINGROUP(165, _, _, _, _, _, _, _, _, _, _, _),
};

static const struct msm_special_pin_data shikra_special_pins_data[] = {
	[0] = SDC_QDSD_PINGROUP("sdc1_rclk", 0xac004, 0, 0),
	[1] = SDC_QDSD_PINGROUP("sdc1_clk", 0xac000, 13, 6),
	[2] = SDC_QDSD_PINGROUP("sdc1_cmd", 0xac000, 11, 3),
	[3] = SDC_QDSD_PINGROUP("sdc1_data", 0xac000, 9, 0),
	[4] = SDC_QDSD_PINGROUP("sdc2_clk", 0xaa000, 14, 6),
	[5] = SDC_QDSD_PINGROUP("sdc2_cmd", 0xaa000, 11, 3),
	[6] = SDC_QDSD_PINGROUP("sdc2_data", 0xaa000, 9, 0),
};

static const char *shikra_get_function_name(struct udevice *dev, unsigned int selector)
{
	return msm_pinctrl_functions[selector].name;
}

static const char *shikra_get_pin_name(struct udevice *dev, unsigned int selector)
{
	if (selector >= 166 && selector <= 172)
		snprintf(pin_name, MAX_PIN_NAME_LEN,
			 shikra_special_pins_data[selector - 166].name);
	else
		snprintf(pin_name, MAX_PIN_NAME_LEN, "gpio%u", selector);

	return pin_name;
}

static int shikra_get_function_mux(__maybe_unused unsigned int pin, unsigned int selector)
{
	unsigned int i;
	const msm_pin_function *func = NULL;

	if (pin >= ARRAY_SIZE(shikra_pin_functions))
		return -EINVAL;

	func = shikra_pin_functions + pin;
	for (i = 0; i < 12; i++)
		if ((*func)[i] == selector)
			return i;

	pr_err("Can't find requested function for pin %u pin\n", pin);

	return -EINVAL;
}

static const struct msm_pinctrl_data shikra_data = {
	.pin_data = {
		.pin_count = 173,
		.special_pins_start = 166,
		.special_pins_data = shikra_special_pins_data,
	},
	.functions_count = ARRAY_SIZE(msm_pinctrl_functions),
	.get_function_name = shikra_get_function_name,
	.get_function_mux = shikra_get_function_mux,
	.get_pin_name = shikra_get_pin_name,
};

static const struct udevice_id msm_pinctrl_ids[] = {
	{ .compatible = "qcom,shikra-tlmm", .data = (ulong)&shikra_data },
	{ }
};

U_BOOT_DRIVER(pinctrl_shikra) = {
	.name		= "pinctrl_shikra",
	.id		= UCLASS_NOP,
	.of_match	= msm_pinctrl_ids,
	.ops		= &msm_pinctrl_ops,
	.bind		= msm_pinctrl_bind,
	.flags		= DM_FLAG_PRE_RELOC,
};
