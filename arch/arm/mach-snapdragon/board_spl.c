// SPDX-License-Identifier: GPL-2.0+
/*
 * Common SPL code for Qualcomm Snapdragon boards.
 *
 * Copyright (c) 2026 Michael Srba <Michael.Srba@seznam.cz>
 */

#include <asm/sections.h>
#include <blk.h>
#include <dm/uclass.h>
#include <hang.h>
#include <init.h>
#include <linux/kernel.h>
#include <mach/qclib.h>
#include <mach/spl.h>
#include <memalign.h>
#include <part.h>
#include <spl.h>
#include <soc/qcom/smem.h>

DECLARE_GLOBAL_DATA_PTR;

/* in SPL, we always use internal DT */
int __weak board_fdt_blob_setup(void **fdtp)
{
	return -EEXIST;
}

__weak void reset_cpu(void)
{
	/* This should currently not get called in non-error paths, so just hang */
	printf("reset_cpu called, going to hang()\n");
	hang();
}

void __weak qcom_spl_soc_shrm_reset(void)
{
}

const struct qcom_cdt_desc * __weak qcom_spl_soc_cdt_desc(void)
{
	return NULL;
}

void __weak qcom_spl_soc_rpm_reset(void)
{
}

/**
 * qcom_spl_get_boot_blk_desc() - Resolve the boot media's block device.
 * @descp:	Set to the resolved block device on success.
 *
 * Maps the already-detected boot device (spl_boot_device() - a direct
 * bootcfg register read, not a scan) to the uclass/devnum pair that
 * owns it, mirroring the same mapping common/spl/spl_mmc.c and
 * common/spl/spl_ufs.c use to pick a block device for a given
 * BOOT_DEVICE_* value. This makes the CDT partition lookup generic
 * across whatever boot medium the board actually used, instead of
 * assuming eMMC.
 *
 * Return: 0 on success, or a negative error code if the boot device is
 * unsupported or its block device can't be found.
 */
static int qcom_spl_get_boot_blk_desc(struct blk_desc **descp)
{
	enum uclass_id uclass_id;
	int devnum;

	switch (spl_boot_device()) {
	case BOOT_DEVICE_MMC1:
		uclass_id = UCLASS_MMC;
		devnum = 0;
		break;
	case BOOT_DEVICE_MMC2:
	case BOOT_DEVICE_MMC2_2:
		uclass_id = UCLASS_MMC;
		devnum = 1;
		break;
	case BOOT_DEVICE_UFS:
		uclass_id = UCLASS_SCSI;
		devnum = 0;
		break;
	default:
		return -ENOSYS;
	}

	*descp = blk_get_devnum_by_uclass_id(uclass_id, devnum);
	if (!*descp)
		return -ENODEV;

	return 0;
}

/**
 * qcom_spl_cdt_load() - Load the CDT from the boot medium into IMEM.
 *
 * On the open boot flow, SPL - not SBL1 - owns storage access at this
 * stage, so SPL (not QCLIB) must read the CDT off the boot medium and
 * stage it in IMEM at the address described by qcom_spl_soc_cdt_desc(),
 * then hand it to QCLIB via the interface table (added by the board's
 * qcom_spl_soc_qclib_override(), only if this load succeeded).
 *
 * Generic across SoCs and boot media: qcom_spl_get_boot_blk_desc()
 * resolves the block device for whichever flash spl_boot_device()
 * reports (a single register read, not a device scan). The CDT GPT
 * partition is then located with a direct lookup -
 * part_get_info_by_name() first, falling back to
 * part_get_info_by_type_guid() if a fallback GUID was provided -
 * rather than manually iterating every partition number. On eMMC,
 * where the CDT may be provisioned on a boot hardware partition rather
 * than the default user-data area, the configured hwpart is selected
 * first and restored afterwards; this is a no-op on interfaces without
 * hardware partitions (UFS, ...).
 *
 * This is entirely optional and non-fatal to the caller: if no board
 * descriptor is provided, the boot device isn't supported, its block
 * device isn't found, the CDT partition isn't found by either name or
 * GUID, or the read fails, this returns a negative error code and
 * QCLIB simply falls back to its own default platform info.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_cdt_load(void)
{
	const struct qcom_cdt_desc *desc_cfg;
	struct blk_desc *desc;
	struct disk_partition info;
	lbaint_t blocks;
	bool is_mmc;
	int ret;

	desc_cfg = qcom_spl_soc_cdt_desc();
	if (!desc_cfg)
		return -ENOSYS;

	ret = qcom_spl_get_boot_blk_desc(&desc);
	if (ret) {
		pr_debug("CDT: boot device not supported for CDT staging (%d)\n",
			 ret);
		return ret;
	}

	is_mmc = desc->uclass_id == UCLASS_MMC;
	if (is_mmc) {
	    ret = blk_dselect_hwpart(desc, desc_cfg->mmc_hwpart);
		if (ret)
			return ret;
	}
	part_init(desc);

	ret = part_get_info_by_name(desc, desc_cfg->part_name, &info);
	if (ret < 0 && desc_cfg->part_type_guid &&
	    CONFIG_IS_ENABLED(PARTITION_TYPE_GUID))
		ret = part_get_info_by_type_guid(desc, desc_cfg->part_type_guid,
						 &info);
	if (ret < 0) {
		pr_err("CDT: partition not found (%d)\n", ret);
		goto restore_hwpart;
	}

	blocks = DIV_ROUND_UP(desc_cfg->size, info.blksz);
	if (blocks > info.size)
		blocks = info.size;

	{
		ALLOC_CACHE_ALIGN_BUFFER(u8, blk_buf, blocks * info.blksz);

		if (blk_dread(desc, info.start, blocks, blk_buf) != blocks) {
			pr_err("CDT: failed to read partition '%s'\n",
			       desc_cfg->part_name);
			ret = -EIO;
			goto restore_hwpart;
		}

		memcpy((void *)desc_cfg->addr, blk_buf,
			       min_t(u32, desc_cfg->size, blocks * info.blksz));
	}

	ret = 0;

restore_hwpart:
	/* Restore the default (user data) hardware partition, if eMMC. */
	if (is_mmc) {
	    int rc = blk_dselect_hwpart(desc, 0);
	    if (rc) {
	        if (!ret)
	            ret = rc;
	        return ret;
	    }
	}
	part_init(desc);

	return ret;
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
int __weak spl_mmc_boot_partition(const u32 boot_device)
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

void qcom_spl_malloc_init_f(void)
{
	if (!CONFIG_IS_ENABLED(SYS_MALLOC_F))
		return;
	/*
	 * Set up by crt0.S
	 */
	assert(gd->malloc_base);
	gd->malloc_limit = CONFIG_VAL(SYS_MALLOC_F_LEN);
	gd->malloc_ptr = 0;

	mem_malloc_init(gd->malloc_base, gd->malloc_limit);
	gd->flags |= GD_FLG_FULL_MALLOC_INIT;
}

/**
 * spl_get_load_buffer() - Allocate a cache-aligned buffer for image loading.
 * @offset:	Offset (unused, typically 0 for SPL).
 * @size:	Size of the buffer to allocate.
 *
 * Return: Pointer to the allocated buffer, or NULL on failure.
 */
struct legacy_img_hdr *spl_get_load_buffer(ssize_t offset, size_t size)
{
#ifdef CONFIG_SPL_LOAD_FIT_ADDRESS
	return (void *)CONFIG_SPL_LOAD_FIT_ADDRESS;
#else
	return NULL;
#endif
}

/**
 * board_spl_fit_buffer_addr() - Get the address of the FIT image buffer.
 * @fit_size:	Size of the FIT image.
 * @sectors:	Number of sectors.
 * @bl_len:	Block length.
 *
 * Return: Address of the FIT image buffer.
 */
void *board_spl_fit_buffer_addr(ulong fit_size, int sectors, int bl_len)
{
	return spl_get_load_buffer(0, sectors * bl_len);
}

/**
 * qcom_spl_loader_pre_ddr() - SPL loader for pre-DDR stage.
 * @boot_device: Type of boot device.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_loader_pre_ddr(u8 boot_device)
{
	struct spl_image_loader *loader, *drv;
	struct spl_image_info spl_image = { 0 };
	struct spl_boot_device boot_dev = { .boot_device = boot_device, };
	int ret = -ENODEV, n_ents;

	drv = ll_entry_start(struct spl_image_loader, spl_image_loader);
	n_ents = ll_entry_count(struct spl_image_loader, spl_image_loader);

	for (loader = drv; loader && (loader != drv + n_ents); loader++) {
		if (boot_device != loader->boot_device)
			continue;

		ret = loader->load_image(&spl_image, &boot_dev);
		if (!ret)
			break;

		printf("%s: Error: %d\n", __func__, ret);
	}

	return ret;
}

/**
 * board_fit_config_name_match() - Select the FIT config for the current
 *				   boot stage.
 * @name:	Candidate FIT config name.
 *
 * Generic default: match "pre-ddr" before DRAM is up, "post-ddr" after.
 * A SoC with additional/different FIT configs should provide a strong
 * override.
 *
 * Return: 0 on match, -EINVAL otherwise.
 */
int __weak board_fit_config_name_match(const char *name)
{
	if (!(gd->flags & GD_FLG_SPL_INIT)) {
		if (!strcmp(name, "pre-ddr")) {
			printf("Selected FIT Config: %s\n", name);
			return 0;
		}
	} else {
		if (!strcmp(name, "post-ddr")) {
			printf("Selected FIT Config: %s\n", name);
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * board_init_f() - Generic SPL entry point.
 * @dummy:	Unused.
 *
 * Generic default: clear BSS, set up malloc, run early init, load the
 * pre-DDR image and invoke QCLIB when booting from PBL, then hand off to
 * board_init_r(). A SoC with additional bring-up steps should provide a
 * strong override.
 */
void __weak board_init_f(ulong dummy)
{
	int ret = 0;

	memset(__bss_start, 0, __bss_end - __bss_start); /* Clear BSS */

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	event_notify_null(EVT_LAST_STAGE_INIT);

	preloader_console_init();

	ret = qcom_spl_loader_pre_ddr(spl_boot_device());
	if (ret) {
		pr_debug("qcom_spl_loader_pre_ddr() failed (%d)\n", ret);
		goto fail;
	}

	ret = qcom_spl_invoke_qclib();
	if (ret) {
		pr_debug("qcom_spl_invoke_qclib() failed (%d)\n", ret);
		goto fail;
	}

	board_init_r(NULL, 0);

fail:
	if (ret)
		reset_cpu();
}
