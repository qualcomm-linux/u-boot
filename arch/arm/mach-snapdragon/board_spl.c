// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2024 Linaro Ltd
 * Copyright (c) 2026 Michael Srba <Michael.Srba@seznam.cz>
 */

#include <asm/system.h>
#include <hang.h>
#include <malloc.h>
#include <spl.h>
#include <mach/pbl.h>

DECLARE_GLOBAL_DATA_PTR;

/* in SPL, we always use internal DT */
int board_fdt_blob_setup(void **fdtp)
{
	*fdtp = (void *)gd->fdt_blob;
	return 0;
}

/* in SPL, we don't have a real reset function */
__weak void reset_cpu(void)
{
	hang();
}

static struct pbl_shared_data g_psd __section(".data");

void save_boot_params(ulong r0, ulong r1, ulong r2, ulong r3)
{
	unsigned long sctlr;
	struct pbl_shared_data *psd;

	sctlr = get_sctlr();
	set_sctlr(sctlr & ~(CR_M));     /* Disable MMU */

	psd = (struct pbl_shared_data *)r0;

	if (!psd || psd->num_of_entries < PBL_SHARED_DATA_PARAM_MAX)
		goto out;

	memcpy(&g_psd, psd, sizeof(g_psd));

out:
	save_boot_params_ret();
}

u32 spl_boot_device(void)
{
	struct pbl_shared_data *psd = &g_psd;

#ifdef DEBUG
	for (int i  = 0; psd && i < psd->num_of_entries; i++) {
		printf("entry[0x%x] = %d 0x%08x %d\n", i,
		       psd->entry[i].param_id, psd->entry[i].value,
		       psd->entry[i].valid);
	}
#endif

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

	pr_err("No boot device configured\n");
	return BOOT_DEVICE_NONE;
}
