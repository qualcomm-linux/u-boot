// SPDX-License-Identifier: GPL-2.0-only
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
#include <fat.h>
#include <image.h>
#include <lmb.h>
#include <log.h>
#include <malloc.h>
#include <memalign.h>
#include <part.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include "qcom_fit_multidtb.h"
#include "qcom_hwdetect.h"

/* FIT image filename on FAT partition and raw partition name */
#define QCOM_FIT_FILENAME	"qclinux_fit.img"
#define QCOM_FIT_PARTNAME	"dtb_a"

#define lmb_alloc(size, addr) \
	lmb_alloc_mem(LMB_MEM_ALLOC_ANY, SZ_2M, addr, size, LMB_NONE)

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
 * struct overlay_entry - Gathered overlay FDT image info
 * @name: Overlay image name in the FIT
 * @data: Pointer to overlay FDT data within the FIT
 * @size: Size of the overlay FDT data
 */
struct overlay_entry {
	const char *name;
	const void *data;
	size_t size;
};

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
		masks[0] = 0xc000;
		shifts[0] = 14;
		log_type = "Storage";
		break;
	case NODE_TYPE_DDR_SIZE:
		node_name = META_NODE_BOARD_SUBTYPE_DDR_SIZE;
		prop_name = PROP_BOARD_SUBTYPE;
		match_values[0] = params->ddr_size_type;
		masks[0] = 0x1f00;
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
 * qcom_add_dtbo_tokens() - Split a comma-separated token string into the bucket list
 * @str: Comma-separated overlay token string
 * @bucket_head: Bucket list head
 *
 * Splits @str on commas, trims surrounding whitespace from each token, skips
 * empty tokens, and adds each remaining token to the bucket list.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_add_dtbo_tokens(const char *str, struct list_head *bucket_head)
{
	char *str_copy;
	char *str_ptr;
	char *token;
	int ret = 0;

	str_copy = strdup(str);
	if (!str_copy)
		return -ENOMEM;

	str_ptr = str_copy;
	while ((token = strsep(&str_ptr, ","))) {
		while (*token == ' ' || *token == '\t')
			token++;

		if (!*token)
			continue;

		log_info("Adding vendor DTBO token: %s\n", token);
		ret = add_to_bucket(token, strlen(token), bucket_head);
		if (ret) {
			log_err("Failed to add token '%s': %d\n", token, ret);
			break;
		}
	}

	free(str_copy);
	return ret;
}

/**
 * qcom_parse_vendor_dtbo() - Parse vendor_dtbo environment variable
 * @bucket_head: Bucket list to add overlay tokens to
 *
 * Reads the "vendor_dtbo" environment variable and parses its comma-separated
 * overlay identifiers, adding them to the bucket list for FIT configuration
 * matching. This influences which FIT configuration is selected.
 *
 * Return: 0 on success, negative error code on failure or if unset
 */
static int qcom_parse_vendor_dtbo(struct list_head *bucket_head)
{
	const char *vendor_dtbo;

	vendor_dtbo = env_get("vendor_dtbo");
	if (!vendor_dtbo || !*vendor_dtbo)
		return -ENOENT;

	log_info("vendor_dtbo: %s\n", vendor_dtbo);

	return qcom_add_dtbo_tokens(vendor_dtbo, bucket_head);
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

	ret = qcom_parse_vendor_dtbo(bucket_head);
	if (ret)
		log_debug("vendor_dtbo not applied: %d\n", ret);

	log_debug("Bucket list: ");
	list_for_each_entry(node, bucket_head, list)
		log_debug("%s ", node->name);
	log_debug("\n");

	return 0;
}

/**
 * qcom_fit_calc_size() - Compute the true total size of a FIT image
 * @fit: Pointer to a buffer containing at least the FIT's FDT structure
 * @buf_size: Size of the buffer pointed to by @fit
 *
 * External-data FITs store image payloads after the FDT structure via
 * "data-offset"/"data-size" properties, so fdt_totalsize() alone misses
 * them. Walk /images to find the true end of the file.
 *
 * Return: total FIT size in bytes, or 0 on error
 */
static size_t qcom_fit_calc_size(const void *fit, size_t buf_size)
{
	int images_node, node;
	size_t aligned_size, max_end, end;
	int offset, size;

	if (buf_size < sizeof(struct fdt_header) || fdt_check_header(fit)) {
		log_err("Invalid FDT header while probing FIT size\n");
		return 0;
	}

	aligned_size = (fdt_totalsize(fit) + 3) & ~3;
	max_end = 0;

	images_node = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_node < 0)
		return aligned_size;

	fdt_for_each_subnode(node, fit, images_node) {
		if (fit_image_get_data_offset(fit, node, &offset))
			continue;
		if (fit_image_get_data_size(fit, node, &size))
			continue;
		if (offset < 0 || size < 0)
			continue;

		end = (size_t)offset + (size_t)size;
		if (end > max_end)
			max_end = end;
	}

	return max_end ? aligned_size + max_end : aligned_size;
}

/**
 * qcom_find_fat_file() - Find a file on any FAT partition across all block devices
 * @filename: Filename to search for
 * @descp: Returns block descriptor of the partition containing the file
 * @part_info: Returns partition info of the found partition
 *
 * Iterates through all block devices and their partitions, mounting each as
 * FAT and checking if the file exists.
 *
 * Return: 0 on success, -ENOENT if not found
 */
static int qcom_find_fat_file(const char *filename, struct blk_desc **descp,
			      struct disk_partition *part_info)
{
	struct udevice *blk_dev;
	struct blk_desc *desc;
	loff_t file_size;
	int partnum;

	blk_foreach_probe(BLKF_BOTH, blk_dev) {
		desc = dev_get_uclass_plat(blk_dev);
		if (!desc)
			continue;

		for (partnum = 1; partnum <= MAX_SEARCH_PARTITIONS; partnum++) {
			if (part_get_info(desc, partnum, part_info))
				break;
			if (fat_set_blk_dev(desc, part_info))
				continue;
			if (!fat_size(filename, &file_size)) {
				*descp = desc;
				return 0;
			}
		}
	}

	return -ENOENT;
}

/**
 * qcom_load_raw_partition() - Load FIT image from a raw partition by name
 * @partname: GPT partition name to search for
 * @fitp: Pointer to store FIT image address
 * @fit_sizep: Pointer to store FIT image size
 *
 * Probes the FDT structure first to size the real FIT, instead of
 * malloc()'ing the whole (possibly oversized) partition. The caller is
 * responsible for validating the content (fdt_check_header, fit_check_format).
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_load_raw_partition(const char *partname, void **fitp,
				   size_t *fit_sizep)
{
	struct udevice *blk_dev;
	struct blk_desc *desc = NULL;
	struct disk_partition part_info;
	void *probe_buf;
	void *part_buf;
	size_t probe_size, probe_blocks, fit_size;
	lbaint_t fit_blocks;

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

	probe_blocks = min_t(size_t, DIV_ROUND_UP(SZ_8K, part_info.blksz),
			     part_info.size);
	probe_size = probe_blocks * part_info.blksz;

	probe_buf = malloc(probe_size);
	if (!probe_buf)
		return -ENOMEM;

	if (blk_dread(desc, part_info.start, probe_blocks, probe_buf) != probe_blocks) {
		log_err("Failed to probe-read partition '%s'\n", partname);
		free(probe_buf);
		return -EIO;
	}

	fit_size = qcom_fit_calc_size(probe_buf, probe_size);
	free(probe_buf);

	if (!fit_size) {
		log_err("Failed to determine FIT size for partition '%s'\n", partname);
		return -EINVAL;
	}

	if (fit_size > (size_t)(part_info.size * part_info.blksz))
		fit_size = (size_t)(part_info.size * part_info.blksz);

	fit_blocks = DIV_ROUND_UP(fit_size, part_info.blksz);
	fit_size = (size_t)fit_blocks * part_info.blksz;

	part_buf = malloc(fit_size);
	if (!part_buf)
		return -ENOMEM;

	if (blk_dread(desc, part_info.start, fit_blocks, part_buf) != fit_blocks) {
		log_err("Failed to read partition '%s'\n", partname);
		free(part_buf);
		return -EIO;
	}

	*fitp = part_buf;
	*fit_sizep = fit_size;

	log_info("Loaded raw partition '%s': %zu bytes\n", partname, fit_size);
	return 0;
}

/**
 * qcom_load_fit_image() - Load FIT image from FAT partition or raw partition
 * @filename: Filename to search for on FAT partitions
 * @partname: GPT partition name to use as fallback (raw image)
 * @fitp: Pointer to store FIT image address
 * @fit_sizep: Pointer to store FIT image size
 *
 * First tries to find @filename on any FAT partition across all block devices.
 * If not found, falls back to reading the raw partition named @partname.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_load_fit_image(const char *filename, const char *partname,
			       void **fitp, size_t *fit_sizep)
{
	struct blk_desc *desc;
	struct disk_partition part_info;
	loff_t file_size;
	void *fit_buf;
	int ret;

	log_info("%s: Loading FIT image\n", __func__);

	/* Try FAT first: search for filename on any FAT partition */
	ret = qcom_find_fat_file(filename, &desc, &part_info);
	if (!ret) {
		fat_size(filename, &file_size);

		fit_buf = malloc_cache_aligned(file_size);
		if (!fit_buf)
			return -ENOMEM;

		ret = file_fat_read(filename, fit_buf, file_size);
		if (ret >= 0) {
			log_info("Loaded '%s' from FAT partition: %lld bytes\n",
				 filename, file_size);
			*fitp = fit_buf;
			*fit_sizep = file_size;
			return 0;
		}

		log_debug("FAT read of '%s' failed (%d), trying raw partition\n",
			  filename, ret);
		free(fit_buf);
	}

	/* Fallback: try raw partition by name */
	return qcom_load_raw_partition(partname, fitp, fit_sizep);
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
					 struct list_head *bucket_head,
					 bool *full_match)
{
	char *compat_copy;
	char *str_ptr;
	char *token;
	int match_count = 0;

	*full_match = true;

	compat_copy = malloc(compat_len + 1);
	if (!compat_copy) {
		*full_match = false;
		return 0;
	}

	memcpy(compat_copy, compatible, compat_len);
	compat_copy[compat_len] = '\0';

	str_ptr = compat_copy;

	strsep(&str_ptr, ",");

	token = strsep(&str_ptr, "-");
	while (token) {
		match_count++;
		if (!search_in_bucket(token, bucket_head)) {
			*full_match = false;
			break;
		}
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
	bool full_match;

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
							    bucket_head, &full_match);

		log_debug("Config %s: %d tokens, full_match=%d\n", cfg_name,
			  match_count, full_match);

		if (!full_match)
			continue;

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
	struct overlay_entry *overlays = NULL;
	int overlay_count = 0;
	size_t overlays_size = 0;

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

	/* Gather overlay entries once: name, data pointer, and size */
	for (i = 1; ; i++) {
		const char *ov_name;
		const void *ov_data;
		size_t ov_size;
		int ov_name_len;
		struct overlay_entry *tmp;

		ov_name = fdt_stringlist_get(fit, config_node, PROP_FDT, i, &ov_name_len);
		if (!ov_name)
			break;

		if (qcom_get_fdt_image_data(fit, images_node, ov_name, &ov_data, &ov_size))
			continue;

		tmp = realloc(overlays, (overlay_count + 1) * sizeof(*overlays));
		if (!tmp) {
			ret = -ENOMEM;
			goto free_overlays;
		}
		overlays = tmp;
		overlays[overlay_count].name = ov_name;
		overlays[overlay_count].data = ov_data;
		overlays[overlay_count].size = ov_size;
		overlay_count++;
		overlays_size += ov_size;
	}

	/* Allocate base DTB with extra space for overlays using LMB */
	base_dtb_size = fdt_size + overlays_size + (2 * 1024);
	ret = lmb_alloc(base_dtb_size, &dtb_addr);
	if (ret) {
		log_err("Failed to allocate LMB memory for base DTB: %zu bytes\n", base_dtb_size);
		ret = -ENOMEM;
		goto free_overlays;
	}
	base_dtb = (void *)dtb_addr;

	memcpy(base_dtb, fdt_data, fdt_size);
	ret = fdt_open_into(base_dtb, base_dtb, base_dtb_size);
	if (ret) {
		log_err("Failed to open DTB: %d\n", ret);
		goto free_overlays;
	}

	/* Apply overlays using the already-gathered entries */
	for (i = 0; i < overlay_count; i++) {
		log_info("Applying overlay: %s (%zu bytes)\n",
			 overlays[i].name, overlays[i].size);

		fixups_offset = fdt_path_offset(overlays[i].data, "/__fixups__");
		if (fixups_offset == -FDT_ERR_NOTFOUND) {
			log_warning("%s is not a valid overlay (no __fixups__)\n",
				    overlays[i].name);
			continue;
		}

		ret = fdt_overlay_apply_verbose(base_dtb, (void *)overlays[i].data);
		if (ret)
			log_err("Failed to apply overlay %s: %d\n", overlays[i].name, ret);
	}

	ret = fdt_pack(base_dtb);
	if (ret) {
		log_err("Failed to pack DTB: %d\n", ret);
		goto free_overlays;
	}

	*final_dtb = base_dtb;
	*final_dtb_size = fdt_totalsize(base_dtb);

	log_info("Final DTB size: %zu bytes\n", *final_dtb_size);

	ret = 0;

free_overlays:
	free(overlays);
	return ret;
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
	ret = qcom_load_fit_image(QCOM_FIT_FILENAME, QCOM_FIT_PARTNAME,
				  &fit, &fit_size);
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
	ret = qcom_hwdetect_get_params(&hw_params);
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
