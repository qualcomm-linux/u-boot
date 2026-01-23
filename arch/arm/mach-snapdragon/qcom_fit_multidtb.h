/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Qualcomm FIT Multi-DTB Selection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * This implements automatic DTB selection from FIT images based on hardware
 * detection via SMEM.
 */

#ifndef __QCOM_FIT_MULTIDTB_H__
#define __QCOM_FIT_MULTIDTB_H__

#include <linux/types.h>
#include <linux/list.h>

/* DDR size thresholds (in bytes) */
#define MB             (1024 * 1024UL)
#define DDR_128MB      (128 * MB)
#define DDR_256MB      (256 * MB)
#define DDR_512MB      (512 * MB)
#define DDR_1024MB     (1024 * MB)
#define DDR_2048MB     (2048 * MB)
#define DDR_3072MB     (3072 * MB)
#define DDR_4096MB     (4096 * MB)

/* DDR type enum */
enum ddr_type {
	DDRTYPE_256MB = 1,
	DDRTYPE_512MB,		/* 2 */
	DDRTYPE_1024MB,		/* 3 */
	DDRTYPE_2048MB,		/* 4 */
	DDRTYPE_3072MB,		/* 5 */
	DDRTYPE_4096MB,		/* 6 */
	DDRTYPE_128MB,		/* 7 */
};

/* Storage type enum */
enum mem_card_type {
	UFS = 0,
	EMMC = 1,
	NAND = 2,
	STORAGE_UNKNOWN,
};

/* Boot device types from shared IMEM */
enum boot_media_type {
	NO_FLASH         = 0,
	NOR_FLASH        = 1,
	NAND_FLASH       = 2,
	ONENAND_FLASH    = 3,
	SDC_FLASH        = 4,
	MMC_FLASH        = 5,
	SPI_FLASH        = 6,
	PCIE_FLASHLESS   = 7,
	UFS_FLASH        = 8,
	RESERVED_0_FLASH = 9,
	RESERVED_1_FLASH = 10,
	USB_FLASHLESS    = 11
};

/* Shared IMEM constants */
#define BOOT_SHARED_IMEM_MAGIC_NUM    0xC1F8DB40
#define BOOT_SHARED_IMEM_VERSION_NUM  0x3
#define SCL_IMEM_BASE                 0x14680000
#define SHARED_IMEM_SIZE              0x1000      /* 4KB */

/* Boot shared IMEM cookie structure */
struct boot_shared_imem_cookie_type {
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

/* FDT configuration types */
#define FDT_TYPE_DTB   0
#define FDT_TYPE_DTBO  1

/* Maximum string lengths */
#define MAX_NODE_NAME_LEN  64
#define MAX_COMPATIBLE_LEN 256

/**
 * struct qcom_hw_params - Hardware parameters detected from SMEM
 * @chip_id: SoC chip ID (from socinfo->id)
 * @chip_version: SoC version (from socinfo->plat_ver)
 * @platform: Hardware platform ID (from socinfo->hw_plat)
 * @subtype: Hardware platform subtype (from socinfo->hw_plat_subtype)
 * @oem_variant_id: OEM variant ID (from socinfo->oem_variant)
 * @ddr_size_type: DDR size type (0-10, calculated from RAM partitions)
 * @storage_type: Storage type (UFS=1, EMMC=2, NAND=3)
 * @foundry_id: Foundry ID (from socinfo->foundry_id)
 * @softsku_id: Software SKU ID (if available)
 *
 * This structure holds all hardware parameters needed for DTB selection.
 */
struct qcom_hw_params {
	u32 chip_id;
	u32 chip_version;
	u32 platform;
	u32 subtype;
	u32 oem_variant_id;
	u32 ddr_size_type;
	u32 storage_type;
	u32 foundry_id;
	u32 softsku_id;
};

/**
 * struct bucket_node - Node in the bucket list
 * @list: List head for linking nodes
 * @name: Node name string (e.g., "qcom", "sa8775p-v2", "ride", "ufs", "8gb")
 *
 * The bucket list contains all matching node names from the metadata DTB.
 * These are used to match against FIT configuration compatible strings.
 */
struct bucket_node {
	struct list_head list;
	char *name;
};

/**
 * struct fdt_config_node - FDT configuration entry
 * @list: List head for linking nodes
 * @name: FDT image name (e.g., "fdt-base", "fdt-overlay-1")
 * @type: FDT type (FDT_TYPE_DTB or FDT_TYPE_DTBO)
 *
 * This structure represents an entry in the FIT configuration's "fdt" property.
 */
struct fdt_config_node {
	struct list_head list;
	char *name;
	u8 type;
};

/* Function prototypes */

/**
 * qcom_fit_multidtb_setup() - Main entry point for FIT multi-DTB selection
 *
 * This function:
 * 1. Loads qclinux_fit.img from EFI partition
 * 2. Extracts metadata DTB
 * 3. Detects hardware parameters from SMEM
 * 4. Builds bucket list from metadata
 * 5. Finds matching FIT configuration
 * 6. Loads DTB and applies overlays
 * 7. Installs FDT for EFI
 *
 * Return: 0 on success, negative error code on failure
 */
int qcom_fit_multidtb_setup(void);

#endif /* __QCOM_FIT_MULTIDTB_H__ */
