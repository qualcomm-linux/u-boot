// SPDX-License-Identifier: GPL-2.0+
/*
 * Common SPL code for Qualcomm Snapdragon boards.
 *
 * Copyright (c) 2026 Michael Srba <Michael.Srba@seznam.cz>
 */

#include <hang.h>
#include <spl.h>
#include <soc/qcom/smem.h>

/* in SPL, we always use internal DT */
int board_fdt_blob_setup(void **fdtp)
{
	return -EEXIST;
}

__weak void reset_cpu(void)
{
	/* This should currently not get called in non-error paths, so just hang */
	printf("reset_cpu called, going to hang()\n");
	hang();
}

u32 spl_boot_device(void)
{
	/* TODO: check boot reason to support UFS and sdcard */
	u32 boot_device = BOOT_DEVICE_DFU;

	return boot_device;
}

#if IS_ENABLED(CONFIG_SPL_SMEM)
/**
 * qcom_spl_populate_smem() - Populate shared memory (SMEM) information.
 * @ctx:	Pointer to the global SPL context.
 *
 * This function initializes and populates various SMEM items with boot-related
 * information, such as flash type.
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_spl_populate_smem(void *ctx)
{
	int ret;
	size_t size;
	struct udevice *smem;
	u32 *fltype;

	ret = qcom_smem_init();
	if (ret) {
		pr_err("Failed init SMEM (%d)\n", ret);
		return ret;
	}

	size = sizeof(u32);

	fltype = (u32 *)smem_get(-1, SMEM_BOOT_FLASH_TYPE, &size);
	if (!fltype) {
		pr_err("Failed to get item: SMEM_BOOT_FLASH_TYPE\n");
		return -ENOENT;
	}

	if (IS_ENABLED(CONFIG_SPL_MMC)) {
		*fltype = SMEM_BOOT_MMC_FLASH;
		wmb();
		return 0;
	}

	pr_err("Boot medium not specified\n");

	return -ENOENT;
}
#endif /* IS_ENABLED(CONFIG_SPL_SMEM) */
