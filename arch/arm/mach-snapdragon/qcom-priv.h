// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

#include <stdbool.h>

/**
 * enum qcom_boot_source - Track where we got loaded from.
 * Used for capsule update logic.
 *
 * @QCOM_BOOT_SOURCE_ANDROID: chainloaded (typically from ABL)
 * @QCOM_BOOT_SOURCE_XBL: flashed to the XBL or UEFI partition
 */
enum qcom_boot_source {
	QCOM_BOOT_SOURCE_ANDROID = 1,
	QCOM_BOOT_SOURCE_XBL,
};

extern enum qcom_boot_source qcom_boot_source;

/*
 * enum qcom_memmap_source - Track where we got the memory map from.
 * used for debugging and validation.
 */
enum qcom_memmap_source {
	QCOM_MEMMAP_SOURCE_INTERNAL_FDT = 1,
	QCOM_MEMMAP_SOURCE_EXTERNAL_FDT,
	QCOM_MEMMAP_SOURCE_SMEM,
};

/* Set by qcom_parse_memory() */
extern enum qcom_memmap_source qcom_memmap_source;

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

int qcom_parse_memory(const void *fdt, bool fdt_is_internal);
enum pbl_shared_data_param_id {
	PSD_ID_PBL_FW_VERSION			= 0x0,	/* PBL firmware version */
	PSD_ID_PBL_PATCH_VERSION		= 0x1,	/* Patch version */
	PSD_ID_RMB_MBOX_BASE_ADDR		= 0x2,	/* Not used */
	PSD_ID_CPU_BOOT_SPEED_HZ		= 0x3,	/* CPU boot speed (Hz) */
	PSD_ID_BOOT_MEDIA_TYPE			= 0x4,	/* Boot media type */
	PSD_ID_IS_EDL_MODE			= 0x5,	/* Emergency Download mode */
	PSD_ID_DEV_PROG_ELF_ENTRY_ADDR		= 0x6,	/* Not used */
	PSD_ID_XBL_CONFIG_ELF_ENTRY_ADDR	= 0x7,	/* Not used */
	PSD_ID_XBL_SC_EXT_ELF_ENTRY_ADDR	= 0x8,	/* Not used */
	PSD_ID_PBL_TIMESTAMPS_BUFFER_ADDR	= 0x9,	/* PBL logs address */
	PSD_ID_PBL_TIMESTAMPS_BUFFER_SIZE	= 0xa,	/* PBL log size */
	PSD_ID_PBL_DEBUG_SHARED_INFO_ADDR	= 0xb,	/* Debug info address */
	PSD_ID_PBL_DEBUG_SHARED_INFO_SIZE	= 0xc,	/* Debug info size */
	PSD_ID_TME_CPU_PBL_ROM_BYPASS_FUSE	= 0xd,	/* Secure boot status */
	PSD_ID_XBL_SC_DEBUG_LOG_ADDR		= 0xe,	/* XBL SC debug log address */
	PSD_ID_XBL_SC_DEBUG_LOG_SIZE		= 0xf,	/* XBL SC debug log size */
	PSD_ID_CURRENT_IMAGE_SET		= 0x10,	/* Booted image set */
	PSD_ID_MEDIA_DATA_INFO_ADDR		= 0x11,	/* Media info pointer */
	PSD_ID_MEDIA_DATA_INFO_SIZE		= 0x12,	/* Media info size */
	PBL_SHARED_DATA_PARAM_MAX,
	PBL_SHARED_DATA_PARAM_SIZE	= 0xffffffffu, /* to force 32 bits */
};

enum pbl_boot_flash_type {
	PSD_NO_FLASH		= 0,
	PSD_NOR_FLASH		= 1,
	PSD_NAND_FLASH		= 2,
	PSD_ONENAND_FLASH	= 3,
	PSD_SDC_FLASH		= 4,
	PSD_MMC_FLASH		= 5,
	PSD_SPI_FLASH		= 6,
	PSD_PCIE_FLASH		= 7,
	PSD_UFS_FLASH		= 8,
	PSD_RSVD_1_FLASH	= 9,
	PSD_USB_FLASH		= 10,
	PSD_SPI_NAND_FLASH	= 11,
	PSD_SPI_FLASH_GPT	= 12,
};

struct pbl_shared_data_entry {
	u32 param_id;
	ulong value;
	bool valid;
};

struct pbl_shared_data {
	u32 version;
	u32 num_of_entries;
	struct pbl_shared_data_entry entry[PBL_SHARED_DATA_PARAM_MAX];
};

#endif /* __QCOM_PRIV_H__ */
