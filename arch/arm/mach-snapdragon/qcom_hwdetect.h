/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm Hardware Detection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_HWDETECT_H__
#define __QCOM_HWDETECT_H__

#include <linux/types.h>

/**
 * struct qcom_hw_params - Hardware parameters
 * @chip_id: SoC chip ID
 * @chip_version: SoC version from TCSR
 * @platform: Hardware platform ID
 * @board_version: Board version
 * @subtype: Hardware platform subtype
 * @oem_variant_id: OEM variant ID
 * @foundry_id: Foundry ID
 * @softsku_id: Software SKU ID
 * @ddr_size_type: DDR size type (0-7)
 * @storage_type: Storage type (UFS/EMMC/NAND)
 *
 * Contains all hardware parameters detected from SMEM, TCSR, and IMEM.
 */
struct qcom_hw_params {
	u32 chip_id;
	u32 chip_version;
	u32 platform;
	u32 board_version;
	u32 subtype;
	u32 oem_variant_id;
	u32 foundry_id;
	u32 softsku_id;
	u32 ddr_size_type;
	u32 storage_type;
};

/**
 * qcom_hwdetect_get_params() - Get hardware parameters
 * @params: Pointer to store hardware parameters
 *
 * Detects all hardware parameters from SMEM, TCSR, and IMEM.
 * Returns a complete structure ready for use by any subsystem.
 *
 * Return: 0 on success, negative error code on failure
 */
int qcom_hwdetect_get_params(struct qcom_hw_params *params);

#endif /* __QCOM_HWDETECT_H__ */