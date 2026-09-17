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

#if CONFIG_IS_ENABLED(MMC)

#define QCOM_SPL_FIT_IMG_PARTITION	"0:BOOTLDR"

/**
 * spl_mmc_boot_mode() - Determine the boot mode for MMC
 * @mmc:	Pointer to the MMC device
 * @boot_device:	Boot device ID
 *
 * Return: MMCSD_MODE_RAW to use raw partition access
 */
u32 spl_mmc_boot_mode(struct mmc *mmc, const u32 boot_device)
{
	return MMCSD_MODE_RAW;
}

/**
 * spl_mmc_boot_partition() - Determine which partition to boot from
 * @boot_device:	Boot device ID
 *
 * Return: Partition number to boot from, or default partition on error
 */
int spl_mmc_boot_partition(const u32 boot_device)
{
	int p_no;
	struct blk_desc *desc;
	struct disk_partition info;

	desc = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	if (!desc) {
		pr_err("%s: Block device not found\n", __func__);
		return -ENODEV;
	}

	p_no = part_get_info_by_name(desc, QCOM_SPL_FIT_IMG_PARTITION, &info);
	if (p_no < 0) {
		pr_err("Partition " QCOM_SPL_FIT_IMG_PARTITION " not found\n");
		return -ENOENT;
	}

	pr_debug("Found " QCOM_SPL_FIT_IMG_PARTITION " at %d\n", p_no);

	if (p_no < 0) {
		printf("Using default MMC partition %d\n",
		       CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION);
		return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION;
	}

	return p_no;
}

unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc, ulong raw_sect)
{
	return 0;
}
#endif /* CONFIG_IS_ENABLED(MMC) */
