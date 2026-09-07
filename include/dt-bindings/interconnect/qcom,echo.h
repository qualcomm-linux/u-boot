/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_INTERCONNECT_QCOM_ECHO_H
#define __DT_BINDINGS_INTERCONNECT_QCOM_ECHO_H

#define MASTER_QUP_CORE_0				0
#define SLAVE_QUP_CORE_0				1

#define MASTER_SYS_TCU				0
#define MASTER_APPSS_PROC				1
#define MASTER_EMAC_GEM_NOC				2
#define MASTER_MSS_PROC				3
#define MASTER_COMPUTE_NOC				4
#define MASTER_ANOC_PCIE_GEM_NOC				5
#define MASTER_SNOC_SF_MEM_NOC				6
#define MASTER_GEM_NOC_CFG				7
#define MASTER_GIC				8
#define MASTER_IPA_PCIE				9
#define SLAVE_GEM_NOC_CNOC				10
#define SLAVE_LLCC				11
#define SLAVE_MEM_NOC_PCIE_SNOC				12
#define SLAVE_SERVICE_GEM_NOC				13

#define MASTER_LLCC				0
#define SLAVE_EBI1				1

#define MASTER_CDSP_PROC				0
#define SLAVE_CDSP_MEM_NOC				1

#define MASTER_PCIE_0				0
#define MASTER_PCIE_1				1
#define MASTER_PCIE_2				2
#define MASTER_PCIE_3				3
#define SLAVE_ANOC_PCIE_GEM_NOC				4

#define MASTER_AOSS				0
#define MASTER_AUDIO				1
#define MASTER_DCC				2
#define MASTER_I2C				3
#define MASTER_I3C				4
#define MASTER_PCIE_RSCC				5
#define MASTER_PRIME_PERIPH				6
#define MASTER_QPIC				7
#define MASTER_QUP_0				8
#define MASTER_TESIC				9
#define MASTER_TIC				10
#define MASTER_ANOC_SNOC				11
#define MASTER_GEM_NOC_CNOC				12
#define MASTER_GEM_NOC_PCIE_SNOC				13
#define MASTER_NSINOC				14
#define MASTER_SNOC_CFG				15
#define MASTER_PCIE_ANOC_CFG				16
#define MASTER_AOSS_AT				17
#define MASTER_CRYPTO				18
#define MASTER_IPA				19
#define MASTER_MSS_NAV				20
#define MASTER_MVMSS				21
#define MASTER_PRIME_PROC				22
#define MASTER_TME				23
#define MASTER_EMAC_0				24
#define MASTER_EMAC_1				25
#define MASTER_EMAC_2				26
#define MASTER_QDSS_DAP				27
#define MASTER_QDSS_ETR				28
#define MASTER_QDSS_ETR_1				29
#define MASTER_SDCC_1				30
#define MASTER_SDCC_2				31
#define MASTER_USB3_0				32
#define SLAVE_ETH0_CFG				33
#define SLAVE_ETH1_CFG				34
#define SLAVE_ETH2_CFG				35
#define SLAVE_AHB2PHY_ETH_0				36
#define SLAVE_AHB2PHY_ETH_1				37
#define SLAVE_AHB2PHY_ETH_2				38
#define SLAVE_AOSS				39
#define SLAVE_APPSS				40
#define SLAVE_AUDIO				41
#define SLAVE_BOOT_ROM				42
#define SLAVE_CLK_CTL				43
#define SLAVE_CMSR				44
#define SLAVE_CDSP_CFG				45
#define SLAVE_RBCPR_CX_CFG				46
#define SLAVE_RBCPR_MXA_CFG				47
#define SLAVE_RBCPR_MXC_CFG				48
#define SLAVE_CPR_NSPCX				49
#define SLAVE_CRYPTO_CFG				50
#define SLAVE_I2C				51
#define SLAVE_I3C				52
#define SLAVE_IMEM_CFG				53
#define SLAVE_IPA_CFG				54
#define SLAVE_IPC_ROUTER_CFG				55
#define SLAVE_CNOC_MSS				56
#define ICBDI_SLAVE_MVMSS_CFG				57
#define SLAVE_PCIE_0_CFG				58
#define SLAVE_PCIE_1_CFG				59
#define SLAVE_PCIE_2_CFG				60
#define SLAVE_PCIE_3_CFG				61
#define SLAVE_PCIE_RSC_CFG				62
#define SLAVE_PDM				63
#define SLAVE_PMU_WRAPPER_CFG				64
#define SLAVE_PRIME				65
#define SLAVE_QDSS_CFG				66
#define SLAVE_QPIC				67
#define SLAVE_QRNG				68
#define SLAVE_QUP_0				69
#define SLAVE_SDCC_1				70
#define SLAVE_SDCC_2				71
#define SLAVE_SPMI_VGI_COEX				72
#define SLAVE_TCSR				73
#define SLAVE_TESIC				74
#define SLAVE_TLMM				75
#define SLAVE_TME_CFG				76
#define SLAVE_USB3				77
#define SLAVE_USB3_PHY_CFG				78
#define SLAVE_VSENSE_CTRL_CFG				79
#define SLAVE_A1NOC_CFG				80
#define SLAVE_EMAC_ANOC				81
#define SLAVE_SNOC_GEM_NOC_SF				82
#define SLAVE_ANOC_THROTTLE_CFG				83
#define SLAVE_EMAC_THROTTLE_CFG				84
#define SLAVE_GIC_THROTTLE_CFG				85
#define SLAVE_IPA_THROTTLE_CFG				86
#define SLAVE_MCDMA_THROTTLE_CFG				87
#define SLAVE_MS_NAV_THROTTLE_CFG				88
#define SLAVE_NSP_QTB_CFG				89
#define SLAVE_NSP_THROTTLE_CFG				90
#define SLAVE_PCIE_THROTTLE_CFG				91
#define SLAVE_QM_CFG				92
#define SLAVE_QM_MPU_CFG				93
#define SLAVE_SNOC_THROTTLE_CFG				94
#define SLAVE_SNOC_CFG				95
#define SLAVE_PCIE_ANOC_CFG				96
#define SLAVE_BOOT_IMEM				97
#define SLAVE_IMEM				98
#define SLAVE_SERVICE_PCIE_ANOC				99
#define SLAVE_SERVICE_SNOC				100
#define SLAVE_PCIE_0				101
#define SLAVE_PCIE_1				102
#define SLAVE_PCIE_2				103
#define SLAVE_PCIE_3				104
#define SLAVE_QDSS_STM				105
#define SLAVE_TCU				106

#endif
