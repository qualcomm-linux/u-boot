/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PBL_H__
#define __PBL_H__

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

#endif /* __PBL_H__ */
