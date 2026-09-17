// SPDX-License-Identifier: GPL-2.0
#include <asm/system.h>
#include <spl.h>

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

enum pbl_shared_data_version {
	PSD_VERSION_1		= 0x00010000, // IPQ 5332, 9574
	PSD_VERSION_2		= 0x00020000, // IPQ 5210, 5424, 5610, 9650
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

static struct pbl_shared_data g_psd __section(".data");

void save_boot_params(ulong r0, ulong r1, ulong r2, ulong r3)
{
	unsigned long sctlr;
	struct pbl_shared_data *psd;

	sctlr = get_sctlr();
	set_sctlr(sctlr & ~(CR_M));	/* Disable MMU */

	psd = (struct pbl_shared_data *)r0;

	if (!psd || psd->num_of_entries < PBL_SHARED_DATA_PARAM_MAX ||
	     psd->version != PSD_VERSION_2)
		goto out;

	memcpy(&g_psd, psd, sizeof(g_psd));

out:
	save_boot_params_ret();
}

u32 __weak spl_boot_device(void)
{
	struct pbl_shared_data *psd = &g_psd;

#ifdef DEBUG
	for (int i  = 0; psd && i < psd->num_of_entries; i++) {
		printf("entry[0x%x] = %d 0x%08x %d\n", i,
		       psd->entry[i].param_id, psd->entry[i].value,
		       psd->entry[i].valid);
	}
#endif
	if (psd->version != PSD_VERSION_2) {
		pr_err("Unknown PBL shared data version\n");
		goto out;
	}

	if (psd->entry[PSD_ID_IS_EDL_MODE].valid &&
	    psd->entry[PSD_ID_IS_EDL_MODE].value) {
		printf("Selected boot device: DFU\n");
		return BOOT_DEVICE_DFU;
	}

	if (psd->entry[PSD_ID_BOOT_MEDIA_TYPE].valid) {
		switch (psd->entry[PSD_ID_BOOT_MEDIA_TYPE].value) {
		case PSD_MMC_FLASH:
			printf("Selected boot device: MMC\n");
			return BOOT_DEVICE_MMC1;
		case PSD_NOR_FLASH:
			printf("Selected boot device: NOR\n");
			return BOOT_DEVICE_NOR;
		case PSD_NAND_FLASH:
			printf("Selected boot device: NAND\n");
			return BOOT_DEVICE_NAND;
		case PSD_UFS_FLASH:
			printf("Selected boot device: UFS\n");
			return BOOT_DEVICE_UFS;
		}
	}

out:
	pr_err("No boot device configured\n");
	return BOOT_DEVICE_NONE;
}