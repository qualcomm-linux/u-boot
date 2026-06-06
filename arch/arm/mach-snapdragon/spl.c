// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <hang.h>
#include <cpu_func.h>
#include <init.h>
#include <image.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <smem.h>
#include <atf_common.h>
#include <linux/err.h>
#include <dm/device-internal.h>
#include <part.h>
#include <blk.h>
#include <dm/uclass.h>
#include "qcom-priv.h"

DECLARE_GLOBAL_DATA_PTR;

#define QCOM_SPL_TCSR_REG_ADDR		0x195c100
#define QCOM_SPL_DLOAD_MASK		BIT(4)
#define QCOM_SPL_DLOAD_SHFT		0x4

#define QCOM_SPL_IS_DLOAD_BIT_SET	((readl(QCOM_SPL_TCSR_REG_ADDR) & \
					QCOM_SPL_DLOAD_MASK) >> \
					QCOM_SPL_DLOAD_SHFT)

#define QCOM_SPL_FIT_IMG_PARTITION	"0:BOOTLDR"

#define MAGIC_KEY			"QCLIB_CB"
#define MAX_ENTRIES			0xF
#define IF_TABLE_VERSION		0x1
#define QCCONFIG			"qc_config"
#define QCSDI				"qcsdi"

/**
 * struct interface_table_entry - Meta data for blobs in QCLIB interface
 * @entry_name:	Name of the data blob (e.g., "dcb_settings").
 * @address:	Address of the data blob.
 * @size:	Size of the data blob.
 * @attributes:	Attributes for the blob (e.g., save to storage).
 */
struct interface_table_entry {
	char entry_name[24];
	u64 address;
	u32 size;
	u32 attributes;
};

/**
 * struct interface_table - QCLIB Interface table header
 * @magic_key:		Magic key for validation ("QCLIB_CB").
 * @version:		Interface table version.
 * @num_entries:	Number of valid entries.
 * @max_entries:	Maximum allowable entries.
 * @global_attributes:	Flags for global attributes (e.g., SDI path).
 * @reserved1:		Reserved for future use.
 * @reserved2:		Reserved for future use.
 * @if_table_entries:	Array of interface table entries.
 */
struct interface_table {
	char magic_key[8];
	u32 version;
	u32 num_entries;
	u32 max_entries;
	u32 global_attributes;
	u32 reserved1;
	u32 reserved2;
	struct interface_table_entry if_table_entries[MAX_ENTRIES];
};

/**
 * qcom_spl_jump_img_entry_t - Type definition for image entry point functions.
 * @arg1:	First argument passed to the entry point.
 * @arg2:	Second argument passed to the entry point.
 */
typedef void (*qcom_spl_jump_img_entry_t)(void *arg1, void *arg2);

/*
 * Global QCSDI address populated by qclib_post_process_from_spl
 * Placed in .data section to ensure it persists
 */
static u64 g_qcsdi_address __section(".data");

/**
 * lowlevel_init() - Early low-level initialization.
 *
 * This function performs very early hardware initialization,
 * specifically disabling the MMU if enabled by PBL.
 */
void lowlevel_init(void)
{
}

/**
 * qcom_spl_error_handler() - Centralized SPL error handler.
 * @arg:	Generic argument (unused).
 *
 * This function is invoked upon critical errors during the SPL boot process.
 */
void qcom_spl_error_handler(void *arg)
{
	pr_err("Entered the SPL Error Handler\n");
	hang();
}

/**
 * qcom_spl_malloc_init_f() - Initialize malloc for SPL.
 *
 * This function initializes the malloc subsystem using the memory region
 */
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
 * qcom_spl_get_fit_img_entry_point() - Get entry point from FIT image node.
 * @fit:	 Pointer to the FIT image blob.
 * @node:	 Node ID within the FIT image.
 * @entry_point: Pointer to store the retrieved entry point.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_spl_get_fit_img_entry_point(void *fit, int node,
					    u64 *entry_point)
{
	int ret;

	if (!fit) {
		pr_err("FIT image blob is NULL\n");
		return -EINVAL;
	}
	if (node <= 0) {
		pr_err("Invalid FIT node ID %d\n", node);
		return -EINVAL;
	}
	if (!entry_point) {
		pr_err("Entry point pointer is NULL\n");
		return -EINVAL;
	}

	ret = fit_image_get_entry(fit, node, (ulong *)entry_point);
	if (ret) {
		pr_debug("No entry point for node %d, trying load address\n",
			 node);
		ret = fit_image_get_load(fit, node, (ulong *)entry_point);
		if (ret)
			pr_err("No load address for node %d (%d)\n", node, ret);
	}

	return ret;
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

/**
 * qcom_spl_get_iftbl_entry_by_name() - Get an interface table entry by name.
 * @if_tbl:	Pointer to the QCLIB interface table.
 * @name:	Name of the entry to find.
 * @entry:	Pointer to a buffer where the found entry will be copied.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_spl_get_iftbl_entry_by_name(struct interface_table *if_tbl,
					    char *name,
					    struct interface_table_entry *entry)
{
	uint uc_index;

	if (!if_tbl) {
		pr_err("Invalid interface table\n");
		return -EINVAL;
	}
	if (!name) {
		pr_err("Invalid name\n");
		return -EINVAL;
	}
	if (!entry) {
		pr_err("Invalid entry pointer\n");
		return -EINVAL;
	}

	for (uc_index = 0; uc_index < if_tbl->num_entries; uc_index++) {
		if (!strcmp(if_tbl->if_table_entries[uc_index].entry_name, name)) {
			memcpy(entry,
			       &if_tbl->if_table_entries[uc_index],
			       sizeof(struct interface_table_entry));
			return 0;
		}
	}
	pr_err("Interface table entry '%s' not found\n", name);

	return -ENOENT;
}

/**
 * qclib_post_process_from_spl() - Post-process QCLIB image from SPL FIT address
 *
 * This function performs the same operations as qclib_post_process() but
 * takes no arguments. It gets the FIT image from CONFIG_SPL_LOAD_FIT_ADDRESS
 * and finds the qcom-lib-1 node automatically.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qclib_post_process_from_spl(void)
{
	int ret;
	int entry_idx;
	int images_node;
	int qcconfig_node;
	int qclib_node;
	const void *fit;
	struct interface_table if_tbl;
	struct interface_table_entry qcsdi_entry;
	qcom_spl_jump_img_entry_t qclib_entry;
	u64 entry_point;

	/* Get FIT image from SPL load address */
	fit = (const void *)CONFIG_SPL_LOAD_FIT_ADDRESS;

	pr_debug("QCLIB post-processing from SPL: fit=%p\n", fit);

	/*
	 * Find "images" node in FIT (get it once and reuse)
	 */
	images_node = fdt_subnode_offset(fit, 0, "images");
	if (images_node < 0) {
		pr_err("Failed to find images node in FIT\n");
		return -ENOENT;
	}

	/*
	 * Find "qcom-config-1" image node
	 */
	qcconfig_node = fdt_subnode_offset(fit, images_node, "qcom-config-1");
	if (qcconfig_node < 0) {
		pr_err("Failed to find qcom-config-1 node in FIT\n");
		return -ENOENT;
	}

	/*
	 * Find "qcom-lib-1" image node
	 */
	qclib_node = fdt_subnode_offset(fit, images_node, "qcom-lib-1");
	if (qclib_node < 0) {
		pr_err("Failed to find qcom-lib-1 node in FIT\n");
		return -ENOENT;
	}

	/*
	 * Initialize the local interface table
	 */
	memset(&if_tbl, 0, sizeof(struct interface_table));
	memcpy(if_tbl.magic_key, MAGIC_KEY, strlen(MAGIC_KEY));

	if_tbl.version = IF_TABLE_VERSION;
	if_tbl.num_entries = 0;
	if_tbl.max_entries = MAX_ENTRIES;

	/*
	 * Add QCCONFIG entry to the interface table
	 */
	entry_idx = 0;
	memcpy(if_tbl.if_table_entries[entry_idx].entry_name,
	       QCCONFIG, strlen(QCCONFIG));

	ret = qcom_spl_get_fit_img_entry_point((void *)fit,
					       qcconfig_node,
					       &if_tbl.if_table_entries[entry_idx].address);
	if (ret) {
		pr_err("Failed to get qcom-config-1 entry point (%d)\n", ret);
		return ret;
	}
	if_tbl.if_table_entries[entry_idx].attributes = 0;
	if_tbl.num_entries = entry_idx + 1;

	/*
	 * Add QCSDI entry to the interface table
	 */
	entry_idx++;
	memcpy(if_tbl.if_table_entries[entry_idx].entry_name,
	       QCSDI, strlen(QCSDI));

	if_tbl.if_table_entries[entry_idx].address = 0;
	if_tbl.if_table_entries[entry_idx].attributes = 0;
	if_tbl.num_entries = entry_idx + 1;

	/*
	 * Get qcom-lib-1 entry point
	 */
	ret = qcom_spl_get_fit_img_entry_point((void *)fit,
					       qclib_node,
					       &entry_point);
	if (ret) {
		pr_err("Failed to get qcom-lib-1 entry point (%d)\n", ret);
		return ret;
	}

	qclib_entry = (qcom_spl_jump_img_entry_t)entry_point;

	pr_info("Jumping to qcom-lib-1 at 0x%llx\n", entry_point);
	qclib_entry(&if_tbl, NULL);

	/* Parse the interface table to extract QCSDI address */
	ret = qcom_spl_get_iftbl_entry_by_name(&if_tbl, QCSDI, &qcsdi_entry);
	if (ret) {
		pr_err("Failed to get QCSDI entry from interface table (%d)\n", ret);
		return ret;
	}

	g_qcsdi_address = qcsdi_entry.address;
	pr_info("QCSDI address: 0x%llx\n", g_qcsdi_address);

	return 0;
}

#if IS_ENABLED(CONFIG_SPL_SMEM)
/**
 * spl_board_prepare_for_boot() - Prepare board for boot
 *
 * This function is called by SPL before jumping to the next stage.
 * It populates SMEM during coldboot.
 */
void spl_board_prepare_for_boot(void)
{
	int ret;

	/*
	 * Populate SMEM in coldboot (Dload bit not set)
	 */
	if (!QCOM_SPL_IS_DLOAD_BIT_SET) {
		ret = qcom_spl_populate_smem(NULL);
		if (ret) {
			pr_err("Failed to populate SMEM (%d)\n", ret);
			qcom_spl_error_handler(NULL);
		}
	}
}
#endif /* IS_ENABLED(CONFIG_SPL_SMEM) */

/**
 * spl_get_load_buffer() - Allocate a cache-aligned buffer for image loading.
 * @offset:	Offset (unused, typically 0 for SPL).
 * @size:	Size of the buffer to allocate.
 *
 * Return: Pointer to the allocated buffer, or NULL on failure.
 */
struct legacy_img_hdr *spl_get_load_buffer(ssize_t offset, size_t size)
{
	return (void *)(CONFIG_SPL_LOAD_FIT_ADDRESS);
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
 * bl2_plat_get_bl31_params_v2() - Retrieve and fixup BL31 parameters.
 * @bl32_entry:	Entry point for BL32 (OP-TEE).
 * @bl33_entry:	Entry point for BL33 (U-Boot/kernel).
 * @fdt_addr:	Address of the Device Tree Blob (FDT).
 *
 * Return: Pointer to the populated BL31 parameters structure.
 */
struct bl_params *bl2_plat_get_bl31_params_v2(uintptr_t bl32_entry,
					      uintptr_t bl33_entry,
					      uintptr_t fdt_addr)
{
	struct bl_params *bl_params;
	struct bl_params_node *node;

	/*
	 * Populate the bl31 params with default values.
	 */
	bl_params = bl2_plat_get_bl31_params_v2_default(bl32_entry, bl33_entry,
							fdt_addr);

	/*
	 * Fixup the bl31 params based on platform requirements.
	 */
	for_each_bl_params_node(bl_params, node) {
		if (node->image_id == ATF_BL31_IMAGE_ID) {
			/*
			 * Pass QCSDI address to BL31 via arg0
			 * This address was populated by qclib_post_process()
			 */
			if (g_qcsdi_address == 0)
				pr_warn("QCSDI address not set, BL31 may not function correctly\n");

			node->ep_info->args.arg0 = g_qcsdi_address;
			pr_debug("Setting BL31 arg0 to QCSDI address: 0x%llx\n", g_qcsdi_address);
		}
	}

	return bl_params;
}

/**
 * qcom_spl_loader_pre_ddr() - SPL loader for pre-DDR stage.
 * @boot_device: Type of boot device.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int qcom_spl_loader_pre_ddr(u8 boot_device)
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

#if CONFIG_IS_ENABLED(MMC)
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

	/*
	 * Use common partition lookup function
	 */
	ret = spl_find_partition_info(UCLASS_MMC, 0, QCOM_SPL_FIT_IMG_PARTITION, &info);
	if (ret < 0) {
		printf("Using default MMC partition %d\n",
		       CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION);
		return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION;
	}

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

static struct pbl_shared_data g_psd __section(".data");

void save_boot_params(ulong r0, ulong r1, ulong r2, ulong r3)
{
	unsigned long sctlr;
	struct pbl_shared_data *psd;

	sctlr = get_sctlr();
	set_sctlr(sctlr & ~(CR_M));	/* Disable MMU */

	psd = (struct pbl_shared_data *)r0;

	if (!psd || psd->num_of_entries < PBL_SHARED_DATA_PARAM_MAX)
		goto out;

	memcpy(&g_psd, psd, sizeof(g_psd));

out:
	save_boot_params_ret();
}

/**
 * spl_boot_device() - Determine the boot device.
 *
 * Return: The mapped boot device type,
 *	   or BOOT_DEVICE_NONE if the device is invalid.
 */
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

#if defined(CONFIG_SPL_BUILD)
/**
 * board_init_f() - Main entry point for SPL.
 * @dummy:	Dummy argument (unused).
 */
void board_init_f(ulong dummy)
{
	int ret;

	memset(__bss_start, 0, __bss_end - __bss_start); /* Clear BSS */

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	preloader_console_init();

	ret = qcom_spl_loader_pre_ddr(spl_boot_device());
	if (ret) {
		pr_debug("qcom_spl_loader_pre_ddr() failed (%d)\n", ret);
		goto fail;
	}

	ret = qclib_post_process_from_spl();
	if (ret) {
		pr_debug("qclib_post_process_from_spl() failed (%d)\n", ret);
		goto fail;
	}

	board_init_r(NULL, 0);

fail:
	if (ret)
		qcom_spl_error_handler(NULL);
}
#endif /* CONFIG_SPL_BUILD */

int board_fit_config_name_match(const char *name)
{
	/*
	 * SPL loads the pre-HLOS images from bootldr FIT image
	 * as below
	 *
	 * In board_init_f() - Matches "pre-ddr" configuration node and
	 * load the images mentioned in its <loadables>
	 *
	 * In board_init_r() - Matches "post-ddr" configuration node and
	 * load the images mentioned in its <loadables>
	 *
	 */
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

int board_fdt_blob_setup(void **fdtp)
{
	return 0;
}

void reset_cpu(void)
{
	/*
	 * Empty placeholder for arch/arm/lib/reset.c:do_reset(),
	 * to avoid "undefined reference to `reset_cpu'"
	 */
}
