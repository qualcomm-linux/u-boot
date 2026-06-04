// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm IPQ5210 pinctrl
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <dm.h>

#include "pinctrl-qcom.h"

#define MAX_PIN_NAME_LEN 32
static char pin_name[MAX_PIN_NAME_LEN] __section(".data");

enum ipq5210_functions {
	msm_mux_atest_char_start,
	msm_mux_atest_char_status0,
	msm_mux_atest_char_status1,
	msm_mux_atest_char_status2,
	msm_mux_atest_char_status3,
	msm_mux_atest_tic_en,
	msm_mux_audio_pri,
	msm_mux_audio_pri_mclk_out0,
	msm_mux_audio_pri_mclk_in0,
	msm_mux_audio_pri_mclk_out1,
	msm_mux_audio_pri_mclk_in1,
	msm_mux_audio_pri_mclk_out2,
	msm_mux_audio_pri_mclk_in2,
	msm_mux_audio_pri_mclk_out3,
	msm_mux_audio_pri_mclk_in3,
	msm_mux_audio_sec,
	msm_mux_audio_sec_mclk_out0,
	msm_mux_audio_sec_mclk_in0,
	msm_mux_audio_sec_mclk_out1,
	msm_mux_audio_sec_mclk_in1,
	msm_mux_audio_sec_mclk_out2,
	msm_mux_audio_sec_mclk_in2,
	msm_mux_audio_sec_mclk_out3,
	msm_mux_audio_sec_mclk_in3,
	msm_mux_core_voltage_0,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_cri_trng2,
	msm_mux_cri_trng3,
	msm_mux_dbg_out_clk,
	msm_mux_dg_out,
	msm_mux_gcc_plltest_bypassnl,
	msm_mux_gcc_plltest_resetn,
	msm_mux_gcc_tlmm,
	msm_mux_gpio,
	msm_mux_led0,
	msm_mux_led1,
	msm_mux_led2,
	msm_mux_mdc_mst,
	msm_mux_mdc_slv0,
	msm_mux_mdc_slv1,
	msm_mux_mdc_slv2,
	msm_mux_mdio_mst,
	msm_mux_mdio_slv0,
	msm_mux_mdio_slv1,
	msm_mux_mdio_slv2,
	msm_mux_mux_tod_out,
	msm_mux_pcie0_clk_req_n,
	msm_mux_pcie0_wake,
	msm_mux_pcie1_clk_req_n,
	msm_mux_pcie1_wake,
	msm_mux_pll_test,
	msm_mux_pon_active_led,
	msm_mux_pon_mux_sel,
	msm_mux_pon_rx,
	msm_mux_pon_rx_los,
	msm_mux_pon_tx,
	msm_mux_pon_tx_burst,
	msm_mux_pon_tx_dis,
	msm_mux_pon_tx_fault,
	msm_mux_pon_tx_sd,
	msm_mux_gpn_rx_los,
	msm_mux_gpn_tx_burst,
	msm_mux_gpn_tx_dis,
	msm_mux_gpn_tx_fault,
	msm_mux_gpn_tx_sd,
	msm_mux_pps,
	msm_mux_pwm0,
	msm_mux_pwm1,
	msm_mux_pwm2,
	msm_mux_pwm3,
	msm_mux_qdss_cti_trig_in_a0,
	msm_mux_qdss_cti_trig_in_a1,
	msm_mux_qdss_cti_trig_in_b0,
	msm_mux_qdss_cti_trig_in_b1,
	msm_mux_qdss_cti_trig_out_a0,
	msm_mux_qdss_cti_trig_out_a1,
	msm_mux_qdss_cti_trig_out_b0,
	msm_mux_qdss_cti_trig_out_b1,
	msm_mux_qdss_traceclk_a,
	msm_mux_qdss_tracectl_a,
	msm_mux_qdss_tracedata_a,
	msm_mux_qrng_rosc0,
	msm_mux_qrng_rosc1,
	msm_mux_qrng_rosc2,
	msm_mux_qspi_data,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs_n,
	msm_mux_qup_se0,
	msm_mux_qup_se1,
	msm_mux_qup_se2,
	msm_mux_qup_se3,
	msm_mux_qup_se4,
	msm_mux_qup_se5,
	msm_mux_qup_se5_l1,
	msm_mux_resout,
	msm_mux_rx_los0,
	msm_mux_rx_los1,
	msm_mux_rx_los2,
	msm_mux_sdc_clk,
	msm_mux_sdc_cmd,
	msm_mux_sdc_data,
	msm_mux_tsens_max,
	msm_mux__,
};

#define MSM_PIN_FUNCTION(fname)				\
	[msm_mux_##fname] = {#fname, msm_mux_##fname}

static const struct pinctrl_function msm_pinctrl_functions[] = {
	MSM_PIN_FUNCTION(atest_char_start),
	MSM_PIN_FUNCTION(atest_char_status0),
	MSM_PIN_FUNCTION(atest_char_status1),
	MSM_PIN_FUNCTION(atest_char_status2),
	MSM_PIN_FUNCTION(atest_char_status3),
	MSM_PIN_FUNCTION(atest_tic_en),
	MSM_PIN_FUNCTION(audio_pri),
	MSM_PIN_FUNCTION(audio_pri_mclk_out0),
	MSM_PIN_FUNCTION(audio_pri_mclk_in0),
	MSM_PIN_FUNCTION(audio_pri_mclk_out1),
	MSM_PIN_FUNCTION(audio_pri_mclk_in1),
	MSM_PIN_FUNCTION(audio_pri_mclk_out2),
	MSM_PIN_FUNCTION(audio_pri_mclk_in2),
	MSM_PIN_FUNCTION(audio_pri_mclk_out3),
	MSM_PIN_FUNCTION(audio_pri_mclk_in3),
	MSM_PIN_FUNCTION(audio_sec),
	MSM_PIN_FUNCTION(audio_sec_mclk_out0),
	MSM_PIN_FUNCTION(audio_sec_mclk_in0),
	MSM_PIN_FUNCTION(audio_sec_mclk_out1),
	MSM_PIN_FUNCTION(audio_sec_mclk_in1),
	MSM_PIN_FUNCTION(audio_sec_mclk_out2),
	MSM_PIN_FUNCTION(audio_sec_mclk_in2),
	MSM_PIN_FUNCTION(audio_sec_mclk_out3),
	MSM_PIN_FUNCTION(audio_sec_mclk_in3),
	MSM_PIN_FUNCTION(core_voltage_0),
	MSM_PIN_FUNCTION(cri_trng0),
	MSM_PIN_FUNCTION(cri_trng1),
	MSM_PIN_FUNCTION(cri_trng2),
	MSM_PIN_FUNCTION(cri_trng3),
	MSM_PIN_FUNCTION(dbg_out_clk),
	MSM_PIN_FUNCTION(dg_out),
	MSM_PIN_FUNCTION(gcc_plltest_bypassnl),
	MSM_PIN_FUNCTION(gcc_plltest_resetn),
	MSM_PIN_FUNCTION(gcc_tlmm),
	MSM_PIN_FUNCTION(gpio),
	MSM_PIN_FUNCTION(led0),
	MSM_PIN_FUNCTION(led1),
	MSM_PIN_FUNCTION(led2),
	MSM_PIN_FUNCTION(mdc_mst),
	MSM_PIN_FUNCTION(mdc_slv0),
	MSM_PIN_FUNCTION(mdc_slv1),
	MSM_PIN_FUNCTION(mdc_slv2),
	MSM_PIN_FUNCTION(mdio_mst),
	MSM_PIN_FUNCTION(mdio_slv0),
	MSM_PIN_FUNCTION(mdio_slv1),
	MSM_PIN_FUNCTION(mdio_slv2),
	MSM_PIN_FUNCTION(mux_tod_out),
	MSM_PIN_FUNCTION(pcie0_clk_req_n),
	MSM_PIN_FUNCTION(pcie0_wake),
	MSM_PIN_FUNCTION(pcie1_clk_req_n),
	MSM_PIN_FUNCTION(pcie1_wake),
	MSM_PIN_FUNCTION(pll_test),
	MSM_PIN_FUNCTION(pon_active_led),
	MSM_PIN_FUNCTION(pon_mux_sel),
	MSM_PIN_FUNCTION(pon_rx),
	MSM_PIN_FUNCTION(pon_rx_los),
	MSM_PIN_FUNCTION(pon_tx),
	MSM_PIN_FUNCTION(pon_tx_burst),
	MSM_PIN_FUNCTION(pon_tx_dis),
	MSM_PIN_FUNCTION(pon_tx_fault),
	MSM_PIN_FUNCTION(pon_tx_sd),
	MSM_PIN_FUNCTION(gpn_rx_los),
	MSM_PIN_FUNCTION(gpn_tx_burst),
	MSM_PIN_FUNCTION(gpn_tx_dis),
	MSM_PIN_FUNCTION(gpn_tx_fault),
	MSM_PIN_FUNCTION(gpn_tx_sd),
	MSM_PIN_FUNCTION(pps),
	MSM_PIN_FUNCTION(pwm0),
	MSM_PIN_FUNCTION(pwm1),
	MSM_PIN_FUNCTION(pwm2),
	MSM_PIN_FUNCTION(pwm3),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_in_b1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_a1),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b0),
	MSM_PIN_FUNCTION(qdss_cti_trig_out_b1),
	MSM_PIN_FUNCTION(qdss_traceclk_a),
	MSM_PIN_FUNCTION(qdss_tracectl_a),
	MSM_PIN_FUNCTION(qdss_tracedata_a),
	MSM_PIN_FUNCTION(qrng_rosc0),
	MSM_PIN_FUNCTION(qrng_rosc1),
	MSM_PIN_FUNCTION(qrng_rosc2),
	MSM_PIN_FUNCTION(qspi_data),
	MSM_PIN_FUNCTION(qspi_clk),
	MSM_PIN_FUNCTION(qspi_cs_n),
	MSM_PIN_FUNCTION(qup_se0),
	MSM_PIN_FUNCTION(qup_se1),
	MSM_PIN_FUNCTION(qup_se2),
	MSM_PIN_FUNCTION(qup_se3),
	MSM_PIN_FUNCTION(qup_se4),
	MSM_PIN_FUNCTION(qup_se5),
	MSM_PIN_FUNCTION(qup_se5_l1),
	MSM_PIN_FUNCTION(resout),
	MSM_PIN_FUNCTION(rx_los0),
	MSM_PIN_FUNCTION(rx_los1),
	MSM_PIN_FUNCTION(rx_los2),
	MSM_PIN_FUNCTION(sdc_clk),
	MSM_PIN_FUNCTION(sdc_cmd),
	MSM_PIN_FUNCTION(sdc_data),
	MSM_PIN_FUNCTION(tsens_max),
};

typedef unsigned int msm_pin_function[10];

#define PINGROUP(id, f1, f2, f3, f4, f5, f6, f7, f8, f9) \
	[id] = {        msm_mux_gpio, /* gpio mode */	\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9,			\
	}

static const msm_pin_function ipq5210_pin_functions[] = {
	PINGROUP(0, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(1, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(2, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(3, sdc_data, qspi_data, pwm2, _, _, _, _, _, _),
	PINGROUP(4, sdc_cmd, qspi_cs_n, _, _, _, _, _, _, _),
	PINGROUP(5, sdc_clk, qspi_clk, _, _, _, _, _, _, _),
	PINGROUP(6, qup_se0, led0, pwm1, _, cri_trng0, qdss_tracedata_a, _, _, _),
	PINGROUP(7, qup_se0, led1, pwm1, _, cri_trng1, qdss_tracedata_a, _, _, _),
	PINGROUP(8, qup_se0, pwm1, audio_pri_mclk_out2, audio_pri_mclk_in2, _, cri_trng2, qdss_tracedata_a, _, _),
	PINGROUP(9, qup_se0, led2, pwm1, _, cri_trng3, qdss_tracedata_a, _, _, _),
	PINGROUP(10, pon_rx_los, qup_se3, pwm0, _, _, qdss_tracedata_a, _, _, _),
	PINGROUP(11, pon_active_led, qup_se3, pwm0, _, _, qdss_tracedata_a, _, _, _),
	PINGROUP(12, pon_tx_dis, qup_se2, pwm0, audio_pri_mclk_out0, audio_pri_mclk_in0, _, qrng_rosc0, qdss_tracedata_a, _),
	PINGROUP(13, gpn_tx_dis, qup_se2, pwm0, audio_pri_mclk_out3, audio_pri_mclk_in3, _, qrng_rosc1, qdss_tracedata_a, _),
	PINGROUP(14, pon_tx_burst, qup_se0, _, qrng_rosc2, qdss_tracedata_a, _, _, _, _),
	PINGROUP(15, pon_tx, qup_se0, _, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(16, pon_tx_sd, audio_sec_mclk_out1, audio_sec_mclk_in1, qdss_cti_trig_out_b0, _, _, _, _, _),
	PINGROUP(17, pon_tx_fault, audio_sec_mclk_out0, audio_sec_mclk_in0, _, _, _, _, _, _),
	PINGROUP(18, pps, pll_test, _, _, _, _, _, _, _),
	PINGROUP(19, mux_tod_out, audio_pri_mclk_out1, audio_pri_mclk_in1, _, _, _, _, _, _),
	PINGROUP(20, qup_se2, mdc_slv1, tsens_max, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(21, qup_se2, mdio_slv1, qdss_tracedata_a, _, _, _, _, _, _),
	PINGROUP(22, core_voltage_0, qup_se3, pwm3, _, _, _, _, _, _),
	PINGROUP(23, led0, qup_se3, dbg_out_clk, qdss_traceclk_a, _, _, _, _, _),
	PINGROUP(24, _, _, _, _, _, _, _, _, _),
	PINGROUP(25, _, _, _, _, _, _, _, _, _),
	PINGROUP(26, mdc_mst, led2, _, qdss_tracectl_a, _, _, _, _, _),
	PINGROUP(27, mdio_mst, led1, _, _, _, _, _, _, _),
	PINGROUP(28, pcie1_clk_req_n, qup_se1, _, _, qdss_cti_trig_out_a0, _, _, _, _),
	PINGROUP(29, _, _, _, _, _, _, _, _, _),
	PINGROUP(30, pcie1_wake, qup_se1, _, _, qdss_cti_trig_in_a0, _, _, _, _),
	PINGROUP(31, pcie0_clk_req_n, mdc_slv0, _, qdss_cti_trig_out_a1, _, _, _, _, _),
	PINGROUP(32, _, _, _, _, _, _, _, _, _),
	PINGROUP(33, pcie0_wake, mdio_slv0, qdss_cti_trig_in_a1, _, _, _, _, _, _),
	PINGROUP(34, audio_pri, atest_char_status0, qdss_cti_trig_in_b0, _, _, _, _, _, _),
	PINGROUP(35, audio_pri, rx_los2, atest_char_status1, qdss_cti_trig_out_b1, _, _, _, _, _),
	PINGROUP(36, audio_pri, _, rx_los1, atest_char_status2, _, _, _, _, _),
	PINGROUP(37, audio_pri, rx_los0, atest_char_status3, _, qdss_cti_trig_in_b1, _, _, _, _),
	PINGROUP(38, qup_se1, led2, gcc_plltest_bypassnl, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(39, qup_se1, led1, led0, gcc_tlmm, qdss_tracedata_a, _, _, _, _),
	PINGROUP(40, qup_se4, rx_los2, audio_sec, gcc_plltest_resetn, qdss_tracedata_a, _, _, _, _),
	PINGROUP(41, qup_se4, rx_los1, audio_sec, qdss_tracedata_a, _, _, _, _, _),
	PINGROUP(42, qup_se4, rx_los0, audio_sec, atest_tic_en, _, _, _, _, _),
	PINGROUP(43, qup_se4, audio_sec, _, _, _, _, _, _, _),
	PINGROUP(44, resout, _, _, _, _, _, _, _, _),
	PINGROUP(45, pon_mux_sel, _, _, _, _, _, _, _, _),
	PINGROUP(46, dg_out, atest_char_start, _, _, _, _, _, _, _),
	PINGROUP(47, gpn_rx_los, mdc_slv2, qup_se5, _, _, _, _, _, _),
	PINGROUP(48, pon_rx, qup_se5, _, _, _, _, _, _, _),
	PINGROUP(49, gpn_tx_fault, mdio_slv2, qup_se5, audio_sec_mclk_out2, audio_sec_mclk_in2, _, _, _, _),
	PINGROUP(50, gpn_tx_sd, qup_se5, audio_sec_mclk_out3, audio_sec_mclk_in3, _, _, _, _, _),
	PINGROUP(51, gpn_tx_burst, qup_se5, _, _, _, _, _, _, _),
	PINGROUP(52, qup_se2, qup_se5, qup_se4, qup_se5_l1, _, _, _, _, _),
	PINGROUP(53, qup_se2, qup_se4, qup_se5_l1, _, _, _, _, _, _),
};

static const char *ipq5210_get_function_name(struct udevice *dev, uint selector)
{
	return msm_pinctrl_functions[selector].name;
}

static const char *ipq5210_get_pin_name(struct udevice *dev, uint selector)
{
	snprintf(pin_name, MAX_PIN_NAME_LEN, "gpio%u", selector);
	return pin_name;
}

static int ipq5210_get_function_mux(unsigned int pin, uint selector)
{
	unsigned int i;
	const msm_pin_function *func = ipq5210_pin_functions + pin;

	for (i = 0; i < 10; i++)
		if ((*func)[i] == selector)
			return i;

	pr_err("Can't find requested function for pin %u\n", pin);
	return -EINVAL;
}

static const struct msm_pinctrl_data ipq5210_data = {
	.pin_data = {
		.pin_count = 54,
		.special_pins_start = 54, /* There are no special pins */
	},
	.functions_count = ARRAY_SIZE(msm_pinctrl_functions),
	.get_function_name = ipq5210_get_function_name,
	.get_function_mux = ipq5210_get_function_mux,
	.get_pin_name = ipq5210_get_pin_name,
};

static const struct udevice_id msm_pinctrl_ids[] = {
	{ .compatible = "qcom,ipq5210-tlmm", .data = (ulong)&ipq5210_data },
	{ /* Sentinal */ }
};

U_BOOT_DRIVER(pinctrl_ipq5210) = {
	.name		= "pinctrl_ipq5210",
	.id		= UCLASS_NOP,
	.of_match	= msm_pinctrl_ids,
	.ops		= &msm_pinctrl_ops,
	.bind		= msm_pinctrl_bind,
	.flags = DM_FLAG_PRE_RELOC,
};
