// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm FIT Multi-DTB Selection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Automatic DTB selection from FIT images based on hardware detection via SMEM.
 * Loads qclinux_fit.img from dtb partition, detects hardware parameters,
 * and selects the best matching DTB configuration.
 */

#include <blk.h>
#include <dm.h>
#include <env.h>
#include <image.h>
#include <lmb.h>
#include <log.h>
#include <malloc.h>
#include <part.h>
#include <asm/io.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <soc/qcom/smem.h>
#include <soc/qcom/socinfo.h>

#include "qcom_fit_multidtb.h"

/* GPT partition name containing the FIT image */
#define QCOM_FIT_PARTNAME	"dtb_a"

#define TCSR_SOC_HW_VERSION        0x01fc8000
#define TCSR_MAJOR_VERSION_MASK    0x0000ff00
#define TCSR_MAJOR_VERSION_SHIFT   8
#define TCSR_MINOR_VERSION_MASK    0x000000ff
#define TCSR_MINOR_VERSION_SHIFT   0

#define lmb_alloc(size, addr) lmb_alloc_mem(LMB_MEM_ALLOC_ANY, SZ_2M, addr, size, LMB_NONE)

/* Maximum values to match (SOC needs 2) */
#define MAX_MATCH_VALUES 2

/* Metadata DTB node names */
#define META_NODE_OEM          "oem"
#define META_NODE_SOC          "soc"
#define META_NODE_SOCVER       "socver"
#define META_NODE_BOARD        "board"
#define META_NODE_BOARDREV     "boardrev"
#define META_NODE_SOC_SKU      "soc-sku"
#define META_NODE_BOARD_SUBTYPE_PERIPHERAL "board-subtype-peripheral-subtype"
#define META_NODE_BOARD_SUBTYPE_STORAGE    "board-subtype-storage-type"
#define META_NODE_BOARD_SUBTYPE_DDR_SIZE   "board-subtype-memory-size"
#define META_NODE_SOFTSKU      "softsku"

/* Property names */
#define PROP_OEM_ID            "oem-id"
#define PROP_MSM_ID            "msm-id"
#define PROP_SOCVER_ID         "socver-id"
#define PROP_BOARD_ID          "board-id"
#define PROP_BOARDREV_ID       "boardrev-id"
#define PROP_BOARD_SUBTYPE     "board-subtype"
#define PROP_SOFTSKU_ID        "softsku-id"
#define PROP_COMPATIBLE        "compatible"
#define PROP_FDT               "fdt"
#define PROP_DATA              "data"

/**
 * add_to_bucket() - Add a node name to the bucket list
 * @name: Node name to add
 * @name_len: Length of the name
 * @bucket_head: Head of the bucket list
 *
 * Return: 0 on success, negative error code on failure
 */
static int add_to_bucket(const char *name, int name_len, struct list_head *bucket_head)
{
	struct bucket_node *node;

	node = malloc(sizeof(*node));
	if (!node)
		return -ENOMEM;

	node->name = malloc(name_len + 1);
	if (!node->name) {
		free(node);
		return -ENOMEM;
	}

	strlcpy(node->name, name, name_len + 1);

	list_add_tail(&node->list, bucket_head);

	return 0;
}

/**
 * search_in_bucket() - Check if a name exists in the bucket list
 * @name: Name to search for
 * @bucket_head: Head of the bucket list
 *
 * Return: true if found, false otherwise
 */
static bool search_in_bucket(const char *name, struct list_head *bucket_head)
{
	struct bucket_node *node;

	list_for_each_entry(node, bucket_head, list) {
		if (!strcmp(node->name, name))
			return true;
	}

	return false;
}

/**
 * free_bucket_list() - Free all nodes in the bucket list
 * @bucket_head: Head of the bucket list
 */
static void free_bucket_list(struct list_head *bucket_head)
{
	struct bucket_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, bucket_head, list) {
		list_del(&node->list);
		free(node->name);
		free(node);
	}
}

/**
 * qcom_get_ddr_size_type() - Get DDR size type from gd->ram_size
 * @ddr_type: Pointer to store DDR type
 *
 * Uses gd->ram_size which is already calculated by dram.c from SMEM/FDT
 * during early boot (same value U-Boot prints as "DRAM: X GiB").
 * Avoids redundant SMEM re-parsing.
 *
 * Return: 0 on success
 */
static int qcom_get_ddr_size_type(u32 *ddr_type)
{
	u64 total_ddr_size = gd->ram_size;

	log_info("Total DDR Size: 0x%llx (%llu MB)\n",
		 total_ddr_size, total_ddr_size / SZ_1M);

	*ddr_type = 0;
	if (total_ddr_size <= DDR_128MB)
		*ddr_type = DDRTYPE_128MB;
	else if (total_ddr_size <= DDR_256MB)
		*ddr_type = DDRTYPE_256MB;
	else if (total_ddr_size <= DDR_512MB)
		*ddr_type = DDRTYPE_512MB;
	else if (total_ddr_size <= DDR_1024MB)
		*ddr_type = DDRTYPE_1024MB;
	else if (total_ddr_size <= DDR_2048MB)
		*ddr_type = DDRTYPE_2048MB;
	else if (total_ddr_size <= DDR_3072MB)
		*ddr_type = DDRTYPE_3072MB;
	else if (total_ddr_size <= DDR_4096MB)
		*ddr_type = DDRTYPE_4096MB;

	log_debug("DDR Type: %u\n", *ddr_type);

	return 0;
}

/**
 * qcom_get_imem_address() - Look up shared IMEM cookie address for this SoC
 *
 * Reads the root node compatible string from the device tree and searches
 * the qcom_imem_table for a matching entry.
 *
 * Return: Physical address of shared IMEM cookie, or 0 if not found
 */
static uintptr_t qcom_get_imem_address(void)
{
	const struct qcom_imem_info *entry;
	const char *soc_compat;
	int len;

	soc_compat = fdt_getprop(gd->fdt_blob, 0, "compatible", &len);
	if (!soc_compat) {
		log_warning("Cannot read SoC compatible from DT\n");
		return 0;
	}

	for (entry = qcom_imem_table; entry->compatible; entry++) {
		if (fdt_stringlist_contains(soc_compat, len, entry->compatible))
			return entry->shared_imem_addr;
	}

	log_warning("SoC not found in IMEM table\n");
	return 0;
}

/**
 * qcom_get_storage_type() - Detect storage type (UFS/EMMC/NAND)
 *
 * Reads the boot device type from the shared IMEM cookie structure populated
 * by the bootloader. The shared IMEM address is looked up from a per-SoC
 * table (qcom_imem_table) using the device tree compatible string.
 *
 * Falls back to UFS if the SoC is not in the table or if the IMEM cookie
 * magic/version validation fails.
 *
 * Return: mem_card_type enum value (UFS/EMMC/NAND), or UFS as fallback
 */
static enum mem_card_type qcom_get_storage_type(void)
{
	struct boot_imem_cookie *imem;
	uintptr_t shared_imem_addr;

	shared_imem_addr = qcom_get_imem_address();
	if (!shared_imem_addr) {
		log_warning("SoC not in IMEM table, defaulting to UFS\n");
		return UFS;
	}

	imem = (struct boot_imem_cookie *)shared_imem_addr;

	if (imem->shared_imem_magic != BOOT_SHARED_IMEM_MAGIC_NUM) {
		log_warning("Invalid shared IMEM magic: 0x%x, defaulting to UFS\n",
			    imem->shared_imem_magic);
		return UFS;
	}

	if (imem->shared_imem_version < BOOT_SHARED_IMEM_VERSION_NUM) {
		log_warning("Invalid shared IMEM version: %u, defaulting to UFS\n",
			    imem->shared_imem_version);
		return UFS;
	}

	log_info("Shared IMEM: magic=0x%x, version=%u, boot_device_type=%u\n",
		 imem->shared_imem_magic, imem->shared_imem_version,
		 imem->boot_device_type);

	switch (imem->boot_device_type) {
	case UFS_FLASH:
		return UFS;
	case MMC_FLASH:
	case SDC_FLASH:
		return EMMC;
	case NAND_FLASH:
		return NAND;
	default:
		log_warning("Unknown shared IMEM boot device: %u, defaulting to UFS\n",
			    imem->boot_device_type);
		return UFS;
	}
}

static u32 qcom_read_tcsr_soc_version(void)
{
	u32 reg_val = readl(TCSR_SOC_HW_VERSION);
	u32 major = (reg_val & TCSR_MAJOR_VERSION_MASK) >> TCSR_MAJOR_VERSION_SHIFT;
	u32 minor = (reg_val & TCSR_MINOR_VERSION_MASK) >> TCSR_MINOR_VERSION_SHIFT;

	return (major << 4) | minor;
}

/**
 * qcom_detect_hardware_params() - Detect all hardware parameters from SMEM
 * @params: Pointer to hardware parameters structure
 *
 * Reads hardware information directly from SMEM using qcom_smem_get() and
 * populates the qcom_hw_params structure with all data needed for DTB selection.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_detect_hardware_params(struct qcom_hw_params *params)
{
	struct socinfo *soc_info;
	size_t size;
	int ret;
	u32 raw_version;

	memset(params, 0, sizeof(*params));

	soc_info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID, &size);
	if (IS_ERR_OR_NULL(soc_info)) {
		log_err("Failed to get SOC info from SMEM\n");
		return -ENODEV;
	}

	params->chip_id = le32_to_cpu(soc_info->id) & 0xffff;

	raw_version = le32_to_cpu(soc_info->plat_ver);
	params->board_version = (SOCINFO_MAJOR(raw_version) << 4) | SOCINFO_MINOR(raw_version);

	params->chip_version = qcom_read_tcsr_soc_version();

	params->platform = le32_to_cpu(soc_info->hw_plat);
	params->subtype = le32_to_cpu(soc_info->hw_plat_subtype);

	if (le32_to_cpu(soc_info->fmt) >= 17)
		params->oem_variant_id = le32_to_cpu(soc_info->oem_variant);

	if (le32_to_cpu(soc_info->fmt) >= 9)
		params->foundry_id = le32_to_cpu(soc_info->foundry_id);

	ret = qcom_get_ddr_size_type(&params->ddr_size_type);
	if (ret)
		log_warning("Failed to get DDR size, defaulting to 0\n");

	params->storage_type = qcom_get_storage_type();

	log_info("Hardware Parameters:\n");
	log_info("  Chip ID: 0x%x\n", params->chip_id);
	log_info("  Chip Version: 0x%x\n", params->chip_version);
	log_info("  Board Version: 0x%x\n", params->board_version);
	log_info("  Platform: 0x%x\n", params->platform);
	log_info("  Subtype: 0x%x\n", params->subtype);
	log_info("  OEM Variant ID: 0x%x\n", params->oem_variant_id);
	log_info("  DDR Size Type: %u\n", params->ddr_size_type);
	log_info("  Storage Type: %u\n", params->storage_type);
	log_info("  Foundry ID: 0x%x\n", params->foundry_id);

	return 0;
}

/**
 * log_match_values() - Log matched hardware parameter values
 * @log_type: Type label for logging (e.g., "SOC", "Board")
 * @subnode_name: Name of the matched node
 * @num_match_values: Number of values to log
 * @match_values: Array of matched values
 */
static void log_match_values(const char *log_type, const char *subnode_name,
			     int num_match_values, const u32 *match_values)
{
	int i;

	log_info("Matched %s: %s (", log_type, subnode_name);

	for (i = 0; i < num_match_values; i++) {
		if (i > 0)
			log_info(", ");
		log_info("val%d=0x%x", i + 1, match_values[i]);
	}

	log_info(")\n");
}

/**
 * process_node() - Generic metadata node processor
 * @type: Type of node to process
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @params: Hardware parameters
 * @bucket_head: Bucket list head
 *
 * Processes different types of nodes in the metadata DTB. Handles matching
 * hardware parameters against DTB properties, with support for bit masking/shifting
 * and fallback values.
 *
 * Return: 0 on success, -ENOENT if no match, other negative on error
 */
static int process_node(enum node_process_type type,
		       void *metadata,
		       int root_offset,
		       struct qcom_hw_params *params,
		       struct list_head *bucket_head)
{
	const char *node_name, *prop_name, *log_type;
	const char *fallback;
	const char *subnode_name;
	int node_offset, subnode, len, name_len;
	int num_match_values, i;
	u32 match_values[MAX_MATCH_VALUES];
	u32 masks[MAX_MATCH_VALUES];
	int shifts[MAX_MATCH_VALUES];
	bool all_match;

	fallback = NULL;
	num_match_values = 1;
	memset(shifts, 0, sizeof(shifts));
	memset(masks, 0xff, sizeof(masks));

	switch (type) {
	case NODE_TYPE_OEM:
		node_name = META_NODE_OEM;
		prop_name = PROP_OEM_ID;
		match_values[0] = params->oem_variant_id;
		log_type = "OEM";
		fallback = "qcom";
		break;
	case NODE_TYPE_SOC:
		node_name = META_NODE_SOC;
		prop_name = PROP_MSM_ID;
		match_values[0] = params->chip_id;
		masks[0] = 0xffff;
		num_match_values = 1;
		log_type = "SOC";
		break;
	case NODE_TYPE_SOCVER:
		node_name = META_NODE_SOCVER;
		prop_name = PROP_SOCVER_ID;
		match_values[0] = params->chip_version;
		num_match_values = 1;
		log_type = "SOCVER";
		break;
	case NODE_TYPE_BOARD:
		node_name = META_NODE_BOARD;
		prop_name = PROP_BOARD_ID;
		match_values[0] = params->platform;
		log_type = "Board";
		break;
	case NODE_TYPE_BOARDREV:
		node_name = META_NODE_BOARDREV;
		prop_name = PROP_BOARDREV_ID;
		match_values[0] = params->board_version;
		num_match_values = 1;
		log_type = "BoardRev";
		break;
	case NODE_TYPE_PERIPHERAL:
		node_name = META_NODE_BOARD_SUBTYPE_PERIPHERAL;
		prop_name = PROP_BOARD_SUBTYPE;
		match_values[0] = params->subtype;
		log_type = "Peripheral Subtype";
		break;
	case NODE_TYPE_STORAGE:
		node_name = META_NODE_BOARD_SUBTYPE_STORAGE;
		prop_name = PROP_BOARD_SUBTYPE;
		match_values[0] = params->storage_type;
		masks[0] = 0x7000;
		shifts[0] = 12;
		log_type = "Storage";
		break;
	case NODE_TYPE_DDR_SIZE:
		node_name = META_NODE_BOARD_SUBTYPE_DDR_SIZE;
		prop_name = PROP_BOARD_SUBTYPE;
		match_values[0] = params->ddr_size_type;
		masks[0] = 0xf00;
		shifts[0] = 8;
		log_type = "DDR Size";
		break;
	case NODE_TYPE_SOFTSKU:
		node_name = META_NODE_SOFTSKU;
		prop_name = PROP_SOFTSKU_ID;
		match_values[0] = params->softsku_id;
		log_type = "SoftSKU";
		break;
	default:
		return -EINVAL;
	}

	node_offset = fdt_subnode_offset(metadata, root_offset, node_name);
	if (node_offset < 0) {
		log_debug("%s node not found\n", log_type);
		return node_offset;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		const u32 *prop = fdt_getprop(metadata, subnode, prop_name, &len);

		if (!prop || len < (int)(num_match_values * sizeof(u32)))
			continue;

		all_match = true;
		for (i = 0; i < num_match_values; i++) {
			u32 dtb_value = fdt32_to_cpu(prop[i]);

			dtb_value = (dtb_value & masks[i]) >> shifts[i];

			if (dtb_value != match_values[i]) {
				all_match = false;
				break;
			}
		}

		if (!all_match)
			continue;

		subnode_name = fdt_get_name(metadata, subnode, &name_len);
		if (subnode_name) {
			log_match_values(log_type, subnode_name, num_match_values,
					 match_values);
			return add_to_bucket(subnode_name, name_len, bucket_head);
		}
	}

	if (fallback) {
		log_info("No %s match, using fallback '%s'\n", log_type, fallback);
		return add_to_bucket(fallback, strlen(fallback), bucket_head);
	}

	log_debug("No %s match\n", log_type);

	return -ENOENT;
}

/**
 * qcom_build_bucket_list() - Build bucket list from metadata DTB
 * @metadata: Metadata DTB pointer
 * @params: Hardware parameters
 * @bucket_head: Bucket list head
 *
 * This function parses the metadata DTB and builds a list of matching
 * node names based on the detected hardware parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_build_bucket_list(void *metadata,
				  struct qcom_hw_params *params,
				  struct list_head *bucket_head)
{
	int root_offset;
	int ret;
	struct bucket_node *node;

	log_debug("Building bucket list from hardware parameters\n");

	root_offset = fdt_path_offset(metadata, "/");
	if (root_offset < 0) {
		log_err("Failed to find root node in metadata DTB\n");
		return root_offset;
	}

	ret = process_node(NODE_TYPE_OEM, metadata, root_offset,
			   params, bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	ret = process_node(NODE_TYPE_SOC, metadata, root_offset,
			   params, bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	ret = process_node(NODE_TYPE_SOCVER, metadata, root_offset,
			   params, bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	ret = process_node(NODE_TYPE_BOARD, metadata, root_offset,
			   params, bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	ret = process_node(NODE_TYPE_BOARDREV, metadata, root_offset,
			   params, bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	process_node(NODE_TYPE_PERIPHERAL, metadata, root_offset,
		     params, bucket_head);

	process_node(NODE_TYPE_STORAGE, metadata, root_offset,
		     params, bucket_head);

	process_node(NODE_TYPE_DDR_SIZE, metadata, root_offset,
		     params, bucket_head);

	process_node(NODE_TYPE_SOFTSKU, metadata, root_offset,
		     params, bucket_head);

	log_debug("Bucket list: ");
	list_for_each_entry(node, bucket_head, list)
		log_debug("%s ", node->name);
	log_debug("\n");

	return 0;
}

/**
 * qcom_load_raw_partition() - Load FIT image from a raw partition by name
 * @partname: GPT partition name to search for
 * @fitp: Pointer to store FIT image address
 * @fit_sizep: Pointer to store FIT image size
 *
 * Searches all block devices for a GPT partition with the given name and
 * reads the entire partition into a buffer. The caller is responsible for
 * validating the content (fdt_check_header, fit_check_format).
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_load_raw_partition(const char *partname, void **fitp,
				   size_t *fit_sizep)
{
	struct udevice *blk_dev;
	struct blk_desc *desc = NULL;
	struct disk_partition part_info;
	void *part_buf;
	size_t part_size;

	blk_foreach_probe(BLKF_BOTH, blk_dev) {
		desc = dev_get_uclass_plat(blk_dev);
		if (!desc)
			continue;
		if (part_get_info_by_name(desc, partname, &part_info) >= 0)
			break;
		desc = NULL;
	}

	if (!desc) {
		log_err("Partition '%s' not found on any block device\n", partname);
		return -ENOENT;
	}

	part_size = (size_t)(part_info.size * part_info.blksz);
	part_buf = malloc(part_size);
	if (!part_buf)
		return -ENOMEM;

	if (blk_dread(desc, part_info.start, part_info.size, part_buf) != part_info.size) {
		log_err("Failed to read partition '%s'\n", partname);
		free(part_buf);
		return -EIO;
	}

	*fitp = part_buf;
	*fit_sizep = part_size;

	log_info("Loaded raw partition '%s': %zu bytes\n", partname, part_size);
	return 0;
}

/**
 * qcom_extract_metadata_dtb() - Extract metadata DTB from FIT image
 * @fit: FIT image pointer
 * @metadata: Pointer to store metadata DTB address
 * @metadata_size: Pointer to store metadata DTB size
 *
 * The metadata DTB is the first image in the FIT (fdt-0).
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_extract_metadata_dtb(void *fit, void **metadata,
				     size_t *metadata_size)
{
	int images_node, first_image;
	const void *data;
	size_t size;
	int ret;

	images_node = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_node < 0) {
		log_err("Cannot find /images node in FIT\n");
		return images_node;
	}

	first_image = fdt_first_subnode(fit, images_node);
	if (first_image < 0) {
		log_err("Cannot find first image in FIT\n");
		return first_image;
	}

	ret = fit_image_get_data(fit, first_image, &data, &size);
	if (ret) {
		log_err("Failed to get metadata DTB data\n");
		return ret;
	}

	*metadata = malloc(size);
	if (!*metadata) {
		log_err("Failed to allocate memory for metadata DTB\n");
		return -ENOMEM;
	}

	memcpy(*metadata, data, size);
	*metadata_size = size;

	log_info("Extracted metadata DTB: %zu bytes\n", size);

	return 0;
}

/**
 * qcom_count_compatible_matches() - Count matching tokens in compatible string
 * @compatible: Compatible string from FIT configuration
 * @compat_len: Length of compatible string
 * @bucket_head: Bucket list head
 *
 * Parses the compatible string and counts how many tokens match entries
 * in the bucket list. The compatible string format is typically:
 * "vendor,device-variant-subtype" where tokens are separated by commas and dashes.
 *
 * Return: Number of matching tokens
 */
static int qcom_count_compatible_matches(const char *compatible, int compat_len,
					 struct list_head *bucket_head)
{
	char *compat_copy;
	char *str_ptr;
	char *token;
	int match_count = 0;

	compat_copy = malloc(compat_len + 1);
	if (!compat_copy)
		return 0;

	memcpy(compat_copy, compatible, compat_len);
	compat_copy[compat_len] = '\0';

	str_ptr = compat_copy;

	/* First split by comma to get vendor prefix (e.g., "qcom") */
	token = strsep(&str_ptr, ",");
	if (token && search_in_bucket(token, bucket_head))
		match_count++;

	/* Then split remaining parts by dash */
	token = strsep(&str_ptr, "-");
	while (token) {
		if (search_in_bucket(token, bucket_head))
			match_count++;
		token = strsep(&str_ptr, "-");
	}

	free(compat_copy);
	return match_count;
}

/**
 * qcom_find_matching_config() - Find matching FIT configuration
 * @fit: FIT image pointer
 * @bucket_head: Bucket list head
 * @config_node: Pointer to store matching configuration node offset
 *
 * This function iterates through all FIT configurations and finds the one
 * with the most matching tokens in its compatible string against the bucket list.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_find_matching_config(void *fit, struct list_head *bucket_head,
				     int *config_node)
{
	int configs_node, cfg;
	const char *compatible;
	int compat_len;
	const char *cfg_name;
	int name_len;
	int best_match_count = 0;
	int best_config = -1;
	int match_count;

	configs_node = fdt_path_offset(fit, FIT_CONFS_PATH);
	if (configs_node < 0) {
		log_err("Cannot find /configurations node in FIT\n");
		return configs_node;
	}

	fdt_for_each_subnode(cfg, fit, configs_node) {
		cfg_name = fdt_get_name(fit, cfg, &name_len);
		compatible = fdt_getprop(fit, cfg, PROP_COMPATIBLE, &compat_len);

		if (!compatible || compat_len <= 0) {
			log_debug("Config %s has no compatible property\n", cfg_name);
			continue;
		}

		log_debug("Checking config: %s, compatible: %s\n",
			  cfg_name, compatible);

		match_count = qcom_count_compatible_matches(compatible, compat_len,
							    bucket_head);

		log_debug("Config %s: %d matches\n", cfg_name, match_count);

		if (match_count > best_match_count) {
			best_match_count = match_count;
			best_config = cfg;
		}
	}

	if (best_config < 0) {
		log_err("No matching configuration found\n");
		return -ENOENT;
	}

	cfg_name = fdt_get_name(fit, best_config, &name_len);
	compatible = fdt_getprop(fit, best_config, PROP_COMPATIBLE, &compat_len);
	log_info("Selected configuration: %s (compatible: %s, matches: %d)\n",
		 cfg_name, compatible, best_match_count);

	*config_node = best_config;
	return 0;
}

/**
 * qcom_get_fdt_image_data() - Get FDT image data from FIT
 * @fit: FIT image pointer
 * @images_node: Images node offset
 * @fdt_name: FDT image name to load
 * @fdt_datap: Pointer to store FDT data address
 * @fdt_sizep: Pointer to store FDT data size
 *
 * Helper function to load an FDT image from the FIT by name.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_get_fdt_image_data(void *fit, int images_node,
				   const char *fdt_name,
				   const void **fdt_datap, size_t *fdt_sizep)
{
	int fdt_node;
	int ret;

	fdt_node = fdt_subnode_offset(fit, images_node, fdt_name);
	if (fdt_node < 0) {
		log_err("Cannot find FDT node: %s\n", fdt_name);
		return fdt_node;
	}

	ret = fit_image_get_data(fit, fdt_node, fdt_datap, fdt_sizep);
	if (ret) {
		log_err("Failed to get FDT data for %s\n", fdt_name);
		return ret;
	}

	return 0;
}

/**
 * qcom_load_dtb_with_overlays() - Load DTB and apply overlays
 * @fit: FIT image pointer
 * @config_node: Configuration node offset
 * @final_dtb: Pointer to store final DTB address
 * @final_dtb_size: Pointer to store final DTB size
 *
 * This function loads the base DTB and applies all DTBOs specified in the
 * configuration's "fdt" property.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_load_dtb_with_overlays(void *fit, int config_node,
				       void **final_dtb,
				       size_t *final_dtb_size)
{
	int images_node;
	const char *fdt_name;
	int fdt_name_len;
	const void *fdt_data;
	size_t fdt_size;
	void *base_dtb = NULL;
	size_t base_dtb_size = 0;
	phys_addr_t dtb_addr;
	int i, ret;
	int fixups_offset;

	images_node = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_node < 0)
		return images_node;

	fdt_name = fdt_stringlist_get(fit, config_node, PROP_FDT, 0, &fdt_name_len);
	if (!fdt_name) {
		log_err("No fdt property in configuration\n");
		return -EINVAL;
	}

	log_info("DTB: %s\n", fdt_name);

	ret = qcom_get_fdt_image_data(fit, images_node, fdt_name,
				      &fdt_data, &fdt_size);
	if (ret)
		return ret;

	/* Allocate base DTB with extra space for overlays using LMB */
	base_dtb_size = fdt_size + (8 * 1024); /* Add 8KB for overlays */
	ret = lmb_alloc(base_dtb_size, &dtb_addr);
	if (ret) {
		log_err("Failed to allocate LMB memory for base DTB: %zu bytes\n", base_dtb_size);
		return -ENOMEM;
	}
	base_dtb = (void *)dtb_addr;

	memcpy(base_dtb, fdt_data, fdt_size);
	ret = fdt_open_into(base_dtb, base_dtb, base_dtb_size);
	if (ret) {
		log_err("Failed to open DTB: %d\n", ret);
		return ret;
	}

	/* Apply overlays (remaining fdt entries) */
	for (i = 1; ; i++) {
		fdt_name = fdt_stringlist_get(fit, config_node, PROP_FDT, i,
					      &fdt_name_len);
		if (!fdt_name)
			break;

		log_info("Applying overlay: %s\n", fdt_name);

		ret = qcom_get_fdt_image_data(fit, images_node, fdt_name,
					      &fdt_data, &fdt_size);
		if (ret)
			continue;

		fixups_offset = fdt_path_offset(fdt_data, "/__fixups__");
		if (fixups_offset == -FDT_ERR_NOTFOUND) {
			log_warning("%s is not a valid overlay (no __fixups__)\n", fdt_name);
			continue;
		}

		ret = fdt_overlay_apply_verbose(base_dtb, (void *)fdt_data);
		if (ret)
			log_err("Failed to apply overlay %s: %d\n", fdt_name, ret);
	}

	ret = fdt_pack(base_dtb);
	if (ret) {
		log_err("Failed to pack DTB: %d\n", ret);
		return ret;
	}

	*final_dtb = base_dtb;
	*final_dtb_size = fdt_totalsize(base_dtb);

	log_info("Final DTB size: %zu bytes\n", *final_dtb_size);

	return 0;
}

/**
 * qcom_fit_multidtb_setup() - Main entry point for FIT multi-DTB selection
 *
 * This is the main function that orchestrates the entire DTB selection process:
 * 1. Load qclinux_fit.img from EFI partition
 * 2. Extract metadata DTB
 * 3. Detect hardware parameters from SMEM
 * 4. Build bucket list from metadata
 * 5. Find matching FIT configuration
 * 6. Load DTB and apply overlays
 * 7. Install FDT for EFI
 *
 * Return: 0 on success, negative error code on failure
 */
int qcom_fit_multidtb_setup(void)
{
	void *fit = NULL;
	size_t fit_size = 0;
	void *metadata = NULL;
	size_t metadata_size = 0;
	struct qcom_hw_params hw_params;
	LIST_HEAD(bucket_list);
	int config_node;
	void *final_dtb = NULL;
	size_t final_dtb_size = 0;
	int ret;

	log_debug("=== FIT Multi-DTB Selection ===\n");

	log_debug("Loading FIT image\n");
	ret = qcom_load_raw_partition(QCOM_FIT_PARTNAME, &fit, &fit_size);
	if (ret) {
		log_err("Failed to load FIT image\n");
		goto cleanup_fit;
	}

	ret = fdt_check_header(fit);
	if (ret) {
		log_err("Invalid FIT header\n");
		ret = -EINVAL;
		goto cleanup_fit;
	}

	ret = fit_check_format(fit, IMAGE_SIZE_INVAL);
	if (ret) {
		log_err("Invalid FIT format\n");
		ret = -EINVAL;
		goto cleanup_fit;
	}

	log_debug("Extracting metadata DTB\n");
	ret = qcom_extract_metadata_dtb(fit, &metadata, &metadata_size);
	if (ret) {
		log_err("Failed to extract metadata DTB\n");
		goto cleanup_metadata;
	}

	log_debug("Detecting hardware parameters\n");
	ret = qcom_detect_hardware_params(&hw_params);
	if (ret) {
		log_err("Failed to detect hardware parameters\n");
		goto cleanup_metadata;
	}

	log_debug("Building bucket list\n");
	ret = qcom_build_bucket_list(metadata, &hw_params, &bucket_list);
	if (ret) {
		log_err("Failed to build bucket list\n");
		goto cleanup_bucket;
	}

	log_debug("Finding matching configuration\n");
	ret = qcom_find_matching_config(fit, &bucket_list, &config_node);
	if (ret) {
		log_err("Failed to find matching configuration\n");
		goto cleanup_bucket;
	}

	log_debug("Loading DTB and applying overlays\n");
	ret = qcom_load_dtb_with_overlays(fit, config_node, &final_dtb,
					  &final_dtb_size);
	if (ret) {
		log_err("Failed to load DTB with overlays\n");
		goto cleanup_dtb;
	}

	log_debug("Setting fdt_addr to selected DTB address\n");

	ret = fdt_check_header(final_dtb);
	if (ret) {
		log_err("Invalid final DTB header: %d\n", ret);
		ret = -EINVAL;
		goto cleanup_dtb;
	}

	/* Update fdt_addr environment variable to point to our DTB */
	env_set_hex("fdt_addr", (ulong)final_dtb);
	log_info("Updated fdt_addr=0x%lx, DTB size=%zu bytes\n", (ulong)final_dtb, final_dtb_size);
	log_info("EFI boot flow will use DTB directly from this address\n");

	/* Don't free final_dtb - LMB manages memory and EFI boot flow will use it */
	final_dtb = NULL;

	log_debug("=== FIT Multi-DTB Selection Complete ===\n");

	ret = 0;
	goto cleanup_success;

cleanup_dtb:
	if (ret && final_dtb)
		final_dtb = NULL;

cleanup_success:
cleanup_bucket:
	free_bucket_list(&bucket_list);

cleanup_metadata:
	if (metadata)
		free(metadata);

cleanup_fit:
	if (fit)
		free(fit);

	return ret;
}
