// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm FIT Multi-DTB Selection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Automatic DTB selection from FIT images based on hardware detection via SMEM.
 * Loads qclinux_fit.img from dtb partition, detects hardware parameters,
 * and selects the best matching DTB configuration.
 */

#include <dm.h>
#include <efi_loader.h>
#include <efi_api.h>
#include <image.h>
#include <smem.h>
#include <malloc.h>
#include <linux/libfdt.h>
#include <linux/list.h>
#include <linux/sizes.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <soc/qcom/socinfo.h>
#include <log.h>
#include <part.h>
#include <blk.h>
#include <env.h>
#include <lmb.h>
#include "qcom_fit_multidtb.h"
#include "qcom-priv.h"

/* LMB allocation macro matching board.c */
#define lmb_alloc(size, addr) lmb_alloc_mem(LMB_MEM_ALLOC_ANY, SZ_2M, addr, size, LMB_NONE)

/* FIT image paths */
#define FIT_IMAGES_PATH        "/images"
#define FIT_CONFIGURATIONS_PATH "/configurations"

/* Metadata DTB node names */
#define META_NODE_OEM          "oem"
#define META_NODE_SOC          "soc"
#define META_NODE_BOARD        "board"
#define META_NODE_SOC_SKU      "soc-sku"
#define META_NODE_BOARD_SUBTYPE_PERIPHERAL "board-subtype-peripheral-subtype"
#define META_NODE_BOARD_SUBTYPE_STORAGE    "board-subtype-storage-type"
#define META_NODE_BOARD_SUBTYPE_MEMORY     "board-subtype-memory-size"
#define META_NODE_SOFTSKU      "softsku"

/* Property names */
#define PROP_OEM_ID            "oem-id"
#define PROP_MSM_ID            "msm-id"
#define PROP_BOARD_ID          "board-id"
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

	memcpy(node->name, name, name_len);
	node->name[name_len] = '\0';

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
		if (strcmp(node->name, name) == 0)
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
 * qcom_get_ddr_size_type() - Get DDR size type from SMEM RAM partitions
 * @ddr_type: Pointer to store DDR type
 *
 * This function reads RAM partition information from SMEM and calculates
 * the total DDR size, then maps it to a DDR type constant (0-10).
 *
 * Return: 0 on success, negative on failure
 */
static int qcom_get_ddr_size_type(u32 *ddr_type)
{
	struct usable_ram_partition_table *rpt;
	struct ram_partition_entry *rpe;
	u64 total_ddr_size = 0;
	int part;

	rpt = qcom_get_ram_partitions();
	if (!rpt) {
		log_err("Failed to get RAM partition table\n");
		return -ENODEV;
	}

	rpe = &rpt->ram_part_entry[0];
	for (part = 0; part < rpt->num_partitions; part++, rpe++) {
		if (rpe->partition_category == RAM_PARTITION_SDRAM &&
		    rpe->partition_type == RAM_PARTITION_SYS_MEMORY) {
			total_ddr_size += rpe->available_length;
			log_debug("RAM partition %d: start=0x%llx size=0x%llx\n",
				  part, rpe->start_address, rpe->available_length);
		}
	}

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
 * qcom_get_storage_type() - Detect storage type (UFS/EMMC/NAND)
 *
 * Reads the boot device type from the shared IMEM cookie structure populated
 * by the bootloader. The shared region is located at the top 4KB of IMEM space.
 * Validates magic number and version before reading the boot device type.
 *
 * Requires CONFIG_QCOM_IMEM_SIZE to be set in platform defconfig. If not set,
 * returns default UFS type.
 *
 * Return: mem_card_type enum value (UFS/EMMC/NAND), or UFS as fallback
 */
static enum mem_card_type qcom_get_storage_type(void)
{
	struct boot_shared_imem_cookie_type *cookie;
	uintptr_t shared_imem_base;

	/* Check if IMEM size is configured */
	if (CONFIG_QCOM_IMEM_SIZE == 0) {
		log_warning("QCOM_IMEM_SIZE not configured, using default UFS\n");
		return UFS;
	}

	/* Calculate shared IMEM base using platform-specific IMEM size */
	shared_imem_base = SCL_IMEM_BASE + CONFIG_QCOM_IMEM_SIZE - SHARED_IMEM_SIZE;
	cookie = (struct boot_shared_imem_cookie_type *)shared_imem_base;

	log_debug("Shared IMEM base: 0x%lx (IMEM size: 0x%x)\n",
		  shared_imem_base, CONFIG_QCOM_IMEM_SIZE);

	/* Validate magic number */
	if (cookie->shared_imem_magic != BOOT_SHARED_IMEM_MAGIC_NUM) {
		log_warning("Invalid shared IMEM magic: 0x%x (expected 0x%x)\n",
			    cookie->shared_imem_magic, BOOT_SHARED_IMEM_MAGIC_NUM);
		return UFS; /* Fallback */
	}

	/* Validate version */
	if (cookie->shared_imem_version < BOOT_SHARED_IMEM_VERSION_NUM) {
		log_warning("Invalid shared IMEM version: %u (expected >= %u)\n",
			    cookie->shared_imem_version, BOOT_SHARED_IMEM_VERSION_NUM);
		return UFS; /* Fallback */
	}

	log_info("Shared IMEM: magic=0x%x, version=%u, boot_device_type=%u\n",
		 cookie->shared_imem_magic, cookie->shared_imem_version,
		 cookie->boot_device_type);

	/* Map boot device type to MemCardType */
	switch (cookie->boot_device_type) {
	case UFS_FLASH:
		log_info("Boot device from shared IMEM: UFS\n");
		return UFS;
	case MMC_FLASH:
	case SDC_FLASH:
		log_info("Boot device from shared IMEM: eMMC\n");
		return EMMC;
	case NAND_FLASH:
		log_info("Boot device from shared IMEM: NAND\n");
		return NAND;
	default:
		log_warning("Unknown shared IMEM boot device: %u\n",
			    cookie->boot_device_type);
		return UFS; /* Safe default */
	}
}

/**
 * qcom_detect_hardware_params() - Detect all hardware parameters from SMEM
 * @params: Pointer to hardware parameters structure
 *
 * This function reads hardware information from SMEM and populates the
 * qcom_hw_params structure with all necessary data for DTB selection.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_detect_hardware_params(struct qcom_hw_params *params)
{
	struct socinfo *soc_info;
	int ret;
	u32 raw_version;

	memset(params, 0, sizeof(*params));

	soc_info = qcom_get_socinfo();
	if (!soc_info) {
		log_err("Failed to get SOC info from SMEM\n");
		return -ENODEV;
	}

	params->chip_id = le32_to_cpu(soc_info->id) & 0xFFFF;

	raw_version = le32_to_cpu(soc_info->plat_ver);
	params->chip_version = (SOCINFO_MAJOR(raw_version) << 4) | SOCINFO_MINOR(raw_version);

	params->platform = le32_to_cpu(soc_info->hw_plat);
	params->subtype = le32_to_cpu(soc_info->hw_plat_subtype);

	if (le32_to_cpu(soc_info->fmt) >= 17)
		params->oem_variant_id = le32_to_cpu(soc_info->oem_variant);
	else
		params->oem_variant_id = 0;

	if (le32_to_cpu(soc_info->fmt) >= 9)
		params->foundry_id = le32_to_cpu(soc_info->foundry_id);
	else
		params->foundry_id = 0;

	ret = qcom_get_ddr_size_type(&params->ddr_size_type);
	if (ret) {
		log_warning("Failed to get DDR size, defaulting to 0\n");
		params->ddr_size_type = 0;
	}

	params->storage_type = qcom_get_storage_type();

	log_info("Hardware Parameters:\n");
	log_info("  Chip ID: 0x%x\n", params->chip_id);
	log_info("  Chip Version: 0x%x\n", params->chip_version);
	log_info("  Platform: 0x%x\n", params->platform);
	log_info("  Subtype: 0x%x\n", params->subtype);
	log_info("  OEM Variant ID: 0x%x\n", params->oem_variant_id);
	log_info("  DDR Size Type: %u\n", params->ddr_size_type);
	log_info("  Storage Type: %u\n", params->storage_type);
	log_info("  Foundry ID: 0x%x\n", params->foundry_id);

	return 0;
}

/**
 * process_oem_node() - Process OEM node from metadata DTB
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @oem_variant_id: OEM variant ID to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_oem_node(void *metadata, int root_offset,
			    u32 oem_variant_id, struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_oem_id;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset, META_NODE_OEM);
	if (node_offset < 0) {
		log_debug("OEM node not found\n");
		return node_offset;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_OEM_ID, &len);
		if (!prop || len <= 0)
			continue;

		dtb_oem_id = fdt32_to_cpu(*prop);

		if (dtb_oem_id == oem_variant_id) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched OEM: %s (id=0x%x)\n",
					 node_name, oem_variant_id);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	/* If no match, add "qcom" as default */
	log_info("No OEM match, using default 'qcom'\n");
	return add_to_bucket("qcom", 4, bucket_head);
}

/**
 * process_soc_node() - Process SOC node from metadata DTB
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @chip_id: Chip ID to match
 * @chip_version: Chip version to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_soc_node(void *metadata, int root_offset,
			    u32 chip_id, u32 chip_version,
			    struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_chip_id, dtb_chip_version;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset, META_NODE_SOC);
	if (node_offset < 0) {
		log_debug("SOC node not found\n");
		return node_offset;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_MSM_ID, &len);
		if (!prop || len < 8) /* Need at least 2 u32 values */
			continue;

		dtb_chip_id = fdt32_to_cpu(prop[0]) & 0xFFFF;
		dtb_chip_version = fdt32_to_cpu(prop[1]);

		if (dtb_chip_id == chip_id && dtb_chip_version == chip_version) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched SOC: %s (id=0x%x, ver=0x%x)\n",
					 node_name, chip_id, chip_version);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	log_debug("No SOC match found\n");
	return -ENOENT;
}

/**
 * process_board_node() - Process board node from metadata DTB
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @platform: Platform ID to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_board_node(void *metadata, int root_offset,
			      u32 platform, struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_platform;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset, META_NODE_BOARD);
	if (node_offset < 0) {
		log_debug("Board node not found\n");
		return node_offset;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_BOARD_ID, &len);
		if (!prop || len <= 0)
			continue;

		dtb_platform = fdt32_to_cpu(*prop);

		if (dtb_platform == platform) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched Board: %s (id=0x%x)\n",
					 node_name, platform);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	log_debug("No board match found\n");
	return -ENOENT;
}

/**
 * process_board_subtype_storage_node() - Process storage type node
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @storage_type: Storage type to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_board_subtype_storage_node(void *metadata, int root_offset,
					      u32 storage_type,
					      struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_storage_type;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset,
					 META_NODE_BOARD_SUBTYPE_STORAGE);
	if (node_offset < 0) {
		log_debug("Board storage type node not found\n");
		return 0;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_BOARD_SUBTYPE, &len);
		if (!prop || len <= 0)
			continue;

		/* Extract storage type from board-subtype bits [14:12] */
		dtb_storage_type = (fdt32_to_cpu(*prop) & 0x7000) >> 12;

		if (dtb_storage_type == storage_type) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched Storage: %s (type=%u)\n",
					 node_name, storage_type);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	return 0;
}

/**
 * process_board_subtype_memory_node() - Process memory size node
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @ddr_size_type: DDR size type to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_board_subtype_memory_node(void *metadata, int root_offset,
					     u32 ddr_size_type,
					     struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_ddr_type;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset,
					 META_NODE_BOARD_SUBTYPE_MEMORY);
	if (node_offset < 0) {
		log_debug("Board memory size node not found\n");
		return 0;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_BOARD_SUBTYPE, &len);
		if (!prop || len <= 0)
			continue;

		/* Extract DDR type from board-subtype bits [11:8] */
		dtb_ddr_type = (fdt32_to_cpu(*prop) & 0xF00) >> 8;

		if (dtb_ddr_type == ddr_size_type) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched Memory: %s (type=%u)\n",
					 node_name, ddr_size_type);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	return 0;
}

/**
 * process_board_subtype_peripheral_node() - Process peripheral subtype node
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @subtype: Peripheral subtype to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_board_subtype_peripheral_node(void *metadata, int root_offset,
						 u32 subtype,
						 struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_subtype;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset,
					 META_NODE_BOARD_SUBTYPE_PERIPHERAL);
	if (node_offset < 0) {
		log_debug("Board peripheral subtype node not found\n");
		return 0;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_BOARD_SUBTYPE, &len);
		if (!prop || len <= 0)
			continue;

		dtb_subtype = fdt32_to_cpu(*prop);

		if (dtb_subtype == subtype) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched Peripheral Subtype: %s (subtype=%u)\n",
					 node_name, subtype);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	return 0;
}

/**
 * process_softsku_node() - Process softsku node
 * @metadata: Metadata DTB pointer
 * @root_offset: Root node offset
 * @softsku_id: SoftSKU ID to match
 * @bucket_head: Bucket list head
 *
 * Return: 0 on success, negative on failure
 */
static int process_softsku_node(void *metadata, int root_offset,
				u32 softsku_id, struct list_head *bucket_head)
{
	int node_offset, subnode;
	const u32 *prop;
	int len;
	u32 dtb_softsku_id;
	const char *node_name;
	int name_len;

	node_offset = fdt_subnode_offset(metadata, root_offset, META_NODE_SOFTSKU);
	if (node_offset < 0) {
		log_debug("SoftSKU node not found\n");
		return 0;
	}

	fdt_for_each_subnode(subnode, metadata, node_offset) {
		prop = fdt_getprop(metadata, subnode, PROP_SOFTSKU_ID, &len);
		if (!prop || len <= 0)
			continue;

		dtb_softsku_id = fdt32_to_cpu(*prop);

		if (dtb_softsku_id == softsku_id) {
			node_name = fdt_get_name(metadata, subnode, &name_len);
			if (node_name) {
				log_info("Matched SoftSKU: %s (id=%u)\n",
					 node_name, softsku_id);
				return add_to_bucket(node_name, name_len, bucket_head);
			}
		}
	}

	return 0;
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

	/* Process OEM node */
	ret = process_oem_node(metadata, root_offset, params->oem_variant_id,
			       bucket_head);
	if (ret < 0 && ret != -ENOENT)
		return ret;

	/* Process SOC node (required) */
	ret = process_soc_node(metadata, root_offset, params->chip_id,
			       params->chip_version, bucket_head);
	if (ret < 0)
		return ret;

	/* Process board node (required) */
	ret = process_board_node(metadata, root_offset, params->platform,
				 bucket_head);
	if (ret < 0)
		return ret;

	/* Process peripheral subtype node (optional) */
	process_board_subtype_peripheral_node(metadata, root_offset,
					      params->subtype, bucket_head);

	/* Process storage type node (optional) */
	process_board_subtype_storage_node(metadata, root_offset,
					   params->storage_type, bucket_head);

	/* Process memory size node (optional) */
	process_board_subtype_memory_node(metadata, root_offset,
					  params->ddr_size_type, bucket_head);

	/* Process softsku node (optional) */
	process_softsku_node(metadata, root_offset, params->softsku_id,
			     bucket_head);

	/* Log bucket contents */
	log_debug("Bucket list: ");
	list_for_each_entry(node, bucket_head, list)
		log_debug("%s ", node->name);
	log_debug("\n");

	return 0;
}

/*
 * FIT Image Loading and Processing
 */

/**
 * qcom_load_fit_image() - Load FIT image from EFI partition
 * @fit: Pointer to store FIT image address
 * @fit_size: Pointer to store FIT image size
 *
 * This function loads qclinux_fit.img from the EFI partition using the
 * EFI Simple File System Protocol, matching the pattern from efi_fdt.c
 *
 * Return: EFI_SUCCESS on success, error code on failure
 */
static efi_status_t qcom_load_fit_image(void **fit, efi_uintn_t *fit_size)
{
	efi_status_t ret;
	efi_handle_t *volume_handles = NULL;
	efi_uintn_t count;
	struct efi_handler *handler;
	struct efi_simple_file_system_protocol *v;
	struct efi_file_handle *root = NULL;
	struct efi_file_handle *file = NULL;
	u16 fit_name[] = u"/qclinux_fit.img";
	u32 i;

	log_info("%s: Loading FIT image from EFI partition\n", __func__);

	/* Locate all file system volumes */
	ret = efi_locate_handle_buffer_int(BY_PROTOCOL,
					   &efi_simple_file_system_protocol_guid,
					   NULL, &count, &volume_handles);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to locate file system volumes: %lu\n", ret);
		return ret;
	}

	/* Try each volume */
	for (i = 0; i < count; i++) {
		ret = efi_search_protocol(volume_handles[i],
					  &efi_simple_file_system_protocol_guid,
					  &handler);
		if (ret != EFI_SUCCESS)
			continue;

		ret = efi_protocol_open(handler, (void **)&v, efi_root, NULL,
					EFI_OPEN_PROTOCOL_GET_PROTOCOL);
		if (ret != EFI_SUCCESS)
			continue;

		/* Open volume */
		ret = EFI_CALL(v->open_volume(v, &root));
		if (ret != EFI_SUCCESS)
			continue;

		/* Try to open FIT file */
		ret = EFI_CALL(root->open(root, &file, fit_name,
					  EFI_FILE_MODE_READ, 0));
		if (ret == EFI_SUCCESS) {
			log_info("%s: %ls found!\n", __func__, fit_name);
			break;
		}

		EFI_CALL(root->close(root));
		root = NULL;
	}

	if (!file) {
		log_err("FIT image not found on any volume\n");
		efi_free_pool(volume_handles);
		return EFI_NOT_FOUND;
	}

	/* Get file size */
	ret = efi_file_size(file, fit_size);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to get FIT file size: %lu\n", ret);
		goto out;
	}

	log_info("FIT image size: %lu bytes\n", *fit_size);

	/* Allocate buffer */
	ret = efi_allocate_pages(EFI_ALLOCATE_ANY_PAGES,
				 EFI_BOOT_SERVICES_DATA,
				 efi_size_in_pages(*fit_size),
				 (efi_physical_addr_t *)fit);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to allocate memory for FIT image: %lu\n", ret);
		goto out;
	}

	/* Read file */
	ret = EFI_CALL(file->read(file, fit_size, *fit));
	if (ret != EFI_SUCCESS) {
		log_err("Failed to read FIT image: %lu\n", ret);
		efi_free_pages((uintptr_t)*fit, efi_size_in_pages(*fit_size));
		*fit = NULL;
	}

out:
	if (file)
		EFI_CALL(file->close(file));
	if (root)
		EFI_CALL(root->close(root));
	efi_free_pool(volume_handles);

	return ret;
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

	/* Get /images node */
	images_node = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_node < 0) {
		log_err("Cannot find /images node in FIT\n");
		return images_node;
	}

	/* Get first subnode (metadata DTB) */
	first_image = fdt_first_subnode(fit, images_node);
	if (first_image < 0) {
		log_err("Cannot find first image in FIT\n");
		return first_image;
	}

	/* Get image data */
	ret = fit_image_get_data(fit, first_image, &data, &size);
	if (ret) {
		log_err("Failed to get metadata DTB data\n");
		return ret;
	}

	/* Allocate and copy metadata DTB */
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
 * qcom_find_matching_config() - Find matching FIT configuration
 * @fit: FIT image pointer
 * @bucket_head: Bucket list head
 * @config_node: Pointer to store matching configuration node offset
 *
 * This function iterates through all FIT configurations and finds the one
 * with the most matching tokens in its compatible string.
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
	char *compat_copy;
	char *token;
	int match_count;

	/* Get /configurations node */
	configs_node = fdt_path_offset(fit, FIT_CONFIGURATIONS_PATH);
	if (configs_node < 0) {
		log_err("Cannot find /configurations node in FIT\n");
		return configs_node;
	}

	/* Iterate through all configurations */
	fdt_for_each_subnode(cfg, fit, configs_node) {
		cfg_name = fdt_get_name(fit, cfg, &name_len);
		compatible = fdt_getprop(fit, cfg, PROP_COMPATIBLE, &compat_len);

		if (!compatible || compat_len <= 0) {
			log_debug("Config %s has no compatible property\n", cfg_name);
			continue;
		}

		log_debug("Checking config: %s, compatible: %s\n",
			  cfg_name, compatible);

		/* Make a copy for tokenization */
		compat_copy = malloc(compat_len + 1);
		if (!compat_copy)
			continue;

		memcpy(compat_copy, compatible, compat_len);
		compat_copy[compat_len] = '\0';

		/* Count matching tokens */
		match_count = 0;

		/* First split by comma to get vendor prefix (e.g., "qcom") */
		token = strtok(compat_copy, ",");
		if (token && search_in_bucket(token, bucket_head))
			match_count++;

		/* Then split remaining parts by dash */
		token = strtok(NULL, "-");
		while (token) {
			if (search_in_bucket(token, bucket_head))
				match_count++;
			token = strtok(NULL, "-");
		}

		free(compat_copy);

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
	int fdt_node;
	const void *fdt_data;
	size_t fdt_size;
	void *base_dtb = NULL;
	size_t base_dtb_size = 0;
	phys_addr_t dtb_addr;
	int i, ret;
	int fixups_offset;

	/* Get /images node */
	images_node = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_node < 0)
		return images_node;

	/* Load base DTB (first fdt entry) */
	fdt_name = fdt_stringlist_get(fit, config_node, PROP_FDT, 0, &fdt_name_len);
	if (!fdt_name) {
		log_err("No fdt property in configuration\n");
		return -EINVAL;
	}

	printf("DTB: %s\n", fdt_name);

	fdt_node = fdt_subnode_offset(fit, images_node, fdt_name);
	if (fdt_node < 0) {
		log_err("Cannot find DTB node: %s\n", fdt_name);
		return fdt_node;
	}

	ret = fit_image_get_data(fit, fdt_node, &fdt_data, &fdt_size);
	if (ret) {
		log_err("Failed to get DTB data\n");
		return ret;
	}

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
		/* LMB memory doesn't need explicit freeing */
		return ret;
	}

	/* Apply overlays (remaining fdt entries) */
	for (i = 1; ; i++) {
		fdt_name = fdt_stringlist_get(fit, config_node, PROP_FDT, i,
					      &fdt_name_len);
		if (!fdt_name)
			break; /* No more overlays */

		log_info("Applying overlay: %s\n", fdt_name);

		fdt_node = fdt_subnode_offset(fit, images_node, fdt_name);
		if (fdt_node < 0) {
			log_err("Cannot find overlay node: %s\n", fdt_name);
			continue;
		}

		ret = fit_image_get_data(fit, fdt_node, &fdt_data, &fdt_size);
		if (ret) {
			log_err("Failed to get overlay data\n");
			continue;
		}

		/* Verify this is an overlay (has __fixups__ node) */
		fixups_offset = fdt_path_offset(fdt_data, "/__fixups__");
		if (fixups_offset == -FDT_ERR_NOTFOUND) {
			log_warning("%s is not a valid overlay (no __fixups__)\n", fdt_name);
			continue;
		}

		/* Apply overlay */
		ret = fdt_overlay_apply_verbose(base_dtb, (void *)fdt_data);
		if (ret) {
			log_err("Failed to apply overlay %s: %d\n", fdt_name, ret);
			/* Continue with other overlays */
		}
	}

	/* Pack final DTB */
	ret = fdt_pack(base_dtb);
	if (ret) {
		log_err("Failed to pack DTB: %d\n", ret);
		/* LMB memory doesn't need explicit freeing */
		return ret;
	}

	*final_dtb = base_dtb;
	*final_dtb_size = fdt_totalsize(base_dtb);

	log_info("Final DTB size: %zu bytes\n", *final_dtb_size);

	return 0;
}

/*
 * Main Entry Point
 */

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
	efi_uintn_t fit_size = 0;
	void *metadata = NULL;
	size_t metadata_size = 0;
	struct qcom_hw_params hw_params;
	LIST_HEAD(bucket_list);
	int config_node;
	void *final_dtb = NULL;
	size_t final_dtb_size = 0;
	efi_status_t efi_ret;
	int ret;

	log_info("=== Qualcomm FIT Multi-DTB Selection ===\n");

	/* Step 1: Load FIT image from EFI partition */
	log_info("Step 1: Loading FIT image\n");
	efi_ret = qcom_load_fit_image(&fit, &fit_size);
	if (efi_ret != EFI_SUCCESS) {
		log_err("Failed to load FIT image\n");
		ret = -EIO;
		goto cleanup_fit;
	}

	/* Validate FIT header */
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

	/* Step 2: Extract metadata DTB */
	log_info("Step 2: Extracting metadata DTB\n");
	ret = qcom_extract_metadata_dtb(fit, &metadata, &metadata_size);
	if (ret) {
		log_err("Failed to extract metadata DTB\n");
		goto cleanup_metadata;
	}

	/* Step 3: Detect hardware parameters */
	log_info("Step 3: Detecting hardware parameters\n");
	ret = qcom_detect_hardware_params(&hw_params);
	if (ret) {
		log_err("Failed to detect hardware parameters\n");
		goto cleanup_metadata;
	}

	/* Step 4: Build bucket list */
	log_info("Step 4: Building bucket list\n");
	ret = qcom_build_bucket_list(metadata, &hw_params, &bucket_list);
	if (ret) {
		log_err("Failed to build bucket list\n");
		goto cleanup_bucket;
	}

	/* Step 5: Find matching configuration */
	log_info("Step 5: Finding matching configuration\n");
	ret = qcom_find_matching_config(fit, &bucket_list, &config_node);
	if (ret) {
		log_err("Failed to find matching configuration\n");
		goto cleanup_bucket;
	}

	/* Step 6: Load DTB and apply overlays */
	log_info("Step 6: Loading DTB and applying overlays\n");
	ret = qcom_load_dtb_with_overlays(fit, config_node, &final_dtb,
					  &final_dtb_size);
	if (ret) {
		log_err("Failed to load DTB with overlays\n");
		goto cleanup_dtb;
	}

	/* Step 7: Update fdt_addr to point to selected DTB */
	log_info("Step 7: Setting fdt_addr to selected DTB address\n");

	/* Validate DTB before setting pointer */
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
	final_dtb = NULL; /* Prevent cleanup */

	log_info("=== FIT Multi-DTB Selection Complete ===\n");

	ret = 0;
	goto cleanup_success;

cleanup_dtb:
	/* Only clean up final_dtb on ERROR (success keeps it for EFI boot) */
	if (ret && final_dtb) {
		/* LMB memory doesn't need explicit freeing - just clear pointer */
		final_dtb = NULL;
	}

cleanup_success:
cleanup_bucket:
	/* Always clean up bucket list (temporary data) */
	free_bucket_list(&bucket_list);

cleanup_metadata:
	/* Always clean up metadata DTB (temporary data) */
	if (metadata)
		free(metadata);

cleanup_fit:
	/* Always clean up FIT image (temporary data) */
	if (fit)
		efi_free_pages((uintptr_t)fit, efi_size_in_pages(fit_size));

	return ret;
}
