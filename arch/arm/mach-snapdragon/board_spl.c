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

#if IS_ENABLED(CONFIG_SPL_SMEM)
/**
 * qcom_spl_populate_smem() - Populate shared memory (SMEM) information.
 * @ctx:	Pointer to the global SPL context.
 *
 * This function initializes and populates various SMEM items with boot-related
 * information, such as flash type, try-mode status, and ATF enable status.
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_spl_populate_smem(void *ctx)
{
	int ret;
	size_t size;
	struct udevice *smem;
	u32 *fltype;

	ret = uclass_get_device(UCLASS_SMEM, 0, &smem);
	if (ret) {
		pr_err("Failed to find SMEM node (%d)\n", ret);
		return ret;
	}

	size = sizeof(u32);
	ret = smem_alloc(smem, -1, SMEM_BOOT_FLASH_TYPE, size);
	if (ret) {
		pr_err("Failed to alloc item: SMEM_BOOT_FLASH_TYPE (%d)\n", ret);
		return ret;
	}

	fltype = (u32 *)smem_get(smem, -1, SMEM_BOOT_FLASH_TYPE, &size);
	if (!fltype) {
		pr_err("Failed to get item: SMEM_BOOT_FLASH_TYPE\n");
		return -ENOENT;
	}

	if (IS_ENABLED(CONFIG_SPL_MMC)) {
		*fltype = SMEM_BOOT_MMC_FLASH;
		return 0;
	}

	pr_err("Boot medium not specified\n");

	return -ENOENT;
}
#endif /* IS_ENABLED(CONFIG_SPL_SMEM) */

#if CONFIG_IS_ENABLED(MMC)

#define QCOM_SPL_FIT_IMG_PARTITION	"0:BOOTLDR"

/**
 * spl_find_partition_info() - Find partition information by name
 * @uclass_id: Device class ID (UCLASS_MMC)
 * @device_num: Device number within the class
 * @part_name: Name of the partition to find
 * @info: Pointer to store partition information
 *
 * This function provides partition lookup logic for MMC.
 * Return: Partition number on success, negative error code on failure
 */
static int spl_find_partition_info(enum uclass_id uclass_id, int device_num,
				   const char *part_name,
				   struct disk_partition *info)
{
	int ret;
	struct blk_desc *desc;

	if (!part_name || !info) {
		printf("Invalid parameters for partition lookup\n");
		return -EINVAL;
	}

	/*
	 * Get block device descriptor
	 */
	desc = blk_get_devnum_by_uclass_id(uclass_id, device_num);
	if (!desc) {
		printf("Block device not found for class %d, device %d\n",
		       uclass_id, device_num);
		return -ENODEV;
	}

	/*
	 * Initialize partition table if needed
	 */
	if (desc->part_type == PART_TYPE_UNKNOWN) {
		printf("Initializing partition table\n");
		/*
		 * Prefer EFI/GPT
		 */
		desc->part_type = PART_TYPE_EFI;
	}

	/*
	 * Find partition by name
	 */
	ret = part_get_info_by_name(desc, part_name, info);
	if (ret < 0) {
		printf("Partition '%s' not found\n", part_name);
		return -ENOENT;
	}

	printf("Found partition '%s' at partition number %d\n", part_name, ret);
	return ret;
}

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
	int ret;
	struct disk_partition info;

	ret = spl_find_partition_info(UCLASS_MMC, 0, QCOM_SPL_FIT_IMG_PARTITION, &info);
#if IS_ENABLED(CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_USE_PARTITION)
	if (ret < 0) {
		printf("Using default MMC partition %d\n",
		       CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION);
		return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION;
	}
#endif
	return ret;
}

/**
 * spl_mmc_get_uboot_raw_sector() - Find the raw sector offset
 * @mmc:	Pointer to the MMC device
 * @raw_sect:	Sector
 *
 * Return: 0 if the image is at the starting of the partition without any offset.
 */
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
