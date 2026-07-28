/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm HW-Info aggregator driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Reads boot-time hardware info (IMEM boot cookie, TCSR HW version) via
 * syscon regions, using offsets from the "qcom,hwinfo" DT node.
 */

#ifndef __QCOM_HWINFO_H__
#define __QCOM_HWINFO_H__

#include <linux/types.h>

struct udevice;

/*
 * Storage type values. Match enum mem_card_type in
 * arch/arm/mach-snapdragon/qcom_fit_multidtb.h.
 */
enum qcom_hwinfo_storage_type {
	QCOM_HWINFO_STORAGE_EMMC = 0,
	QCOM_HWINFO_STORAGE_UFS = 1,
	QCOM_HWINFO_STORAGE_NAND = 2,
	QCOM_HWINFO_STORAGE_SDCARD = 3,
	QCOM_HWINFO_STORAGE_UNKNOWN,
};

/* Boot shared IMEM cookie constants */
#define QCOM_HWINFO_IMEM_MAGIC_NUM    0xc1f8db40
#define QCOM_HWINFO_IMEM_VERSION_NUM  0x3

/* Boot device types from shared IMEM */
enum qcom_hwinfo_boot_media_type {
	QCOM_HWINFO_NO_FLASH         = 0,
	QCOM_HWINFO_NOR_FLASH        = 1,
	QCOM_HWINFO_NAND_FLASH       = 2,
	QCOM_HWINFO_ONENAND_FLASH    = 3,
	QCOM_HWINFO_SDC_FLASH        = 4,
	QCOM_HWINFO_MMC_FLASH        = 5,
	QCOM_HWINFO_SPI_FLASH        = 6,
	QCOM_HWINFO_PCIE_FLASHLESS   = 7,
	QCOM_HWINFO_UFS_FLASH        = 8,
	QCOM_HWINFO_RESERVED_0_FLASH = 9,
	QCOM_HWINFO_RESERVED_1_FLASH = 10,
	QCOM_HWINFO_USB_FLASHLESS    = 11,
};

/*
 * Boot shared IMEM cookie layout, populated by the bootloader.
 * Do NOT mark __packed - breaks natural alignment expected by firmware.
 */
struct qcom_hwinfo_imem_cookie {
	u32 shared_imem_magic;
	u32 shared_imem_version;
	u64 etb_buf_addr;
	u64 l2_cache_dump_buff_addr;
	u32 a64_pointer_padding;
	u32 uefi_ram_dump_magic;
	u32 ddr_training_cookie;
	u32 abnormal_reset_occurred;
	u32 reset_status_register;
	u32 rpm_sync_cookie;
	u32 debug_config;
	u64 boot_log_addr;
	u32 boot_log_size;
	u32 boot_fail_count;
	u32 sbl1_error_type;
	u32 uefi_image_magic;
	u32 boot_device_type;
	u64 boot_devtree_addr;
	u64 boot_devtree_size;
};

/**
 * qcom_hwinfo_get_imem_cookie() - Get a read-only pointer to the IMEM cookie
 * @dev: qcom_hwinfo device
 * @cookie: Returns a const pointer into the live IMEM region
 *
 * Validates magic/version before returning. On failure, @cookie is unset.
 *
 * Return: 0 on success, -ENODEV if imem regmap unavailable, -EINVAL on
 *	   magic/version mismatch
 */
int qcom_hwinfo_get_imem_cookie(struct udevice *dev,
				const struct qcom_hwinfo_imem_cookie **cookie);

/**
 * qcom_hwinfo_get_storage_type() - Detect boot storage type from IMEM cookie
 * @dev: qcom_hwinfo device
 * @storage_type: Returns detected storage type
 *
 * Wraps qcom_hwinfo_get_imem_cookie(); defaults to UFS on failure.
 *
 * Return: 0 always
 */
int qcom_hwinfo_get_storage_type(struct udevice *dev, u32 *storage_type);

/**
 * qcom_hwinfo_get_tcsr_hw_version() - Read the TCSR SOC_HW_VERSION register
 * @dev: qcom_hwinfo device
 * @hw_version: Returns the raw register value
 *
 * Return: 0 on success, negative error code on failure
 */
int qcom_hwinfo_get_tcsr_hw_version(struct udevice *dev, u32 *hw_version);

#endif /* __QCOM_HWINFO_H__ */