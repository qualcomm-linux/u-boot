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
#include <asm/sections.h>
#include <soc/qcom/smem.h>
#include <atf_common.h>
#include <linux/err.h>
#include <dm/device-internal.h>
#include <part.h>
#include <blk.h>
#include <dm/uclass.h>
#include "qcom-priv.h"
#include <mach/spl.h>

#define QCOM_SPL_TCSR_REG_ADDR		0x195c100
#define QCOM_SPL_DLOAD_MASK		BIT(4)
#define QCOM_SPL_DLOAD_SHFT		0x4

#define QCOM_SPL_IS_DLOAD_BIT_SET	((readl(QCOM_SPL_TCSR_REG_ADDR) & \
					QCOM_SPL_DLOAD_MASK) >> \
					QCOM_SPL_DLOAD_SHFT)

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
	fit = spl_get_load_buffer(0, 0);

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
	 * Find "qcconfig_1" image node
	 */
	qcconfig_node = fdt_subnode_offset(fit, images_node, "qcconfig_1");
	if (qcconfig_node < 0) {
		pr_err("Failed to find qcconfig_1 node in FIT\n");
		return -ENOENT;
	}

	/*
	 * Find "qcom-lib-1" image node
	 */
	qclib_node = fdt_subnode_offset(fit, images_node, "qclib_1");
	if (qclib_node < 0) {
		pr_err("Failed to find qclib_1 node in FIT\n");
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
		pr_err("Failed to get qcconfig_1 entry point (%d)\n", ret);
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
		pr_err("Failed to get qclib_1 entry point (%d)\n", ret);
		return ret;
	}

	qclib_entry = (qcom_spl_jump_img_entry_t)entry_point;

	pr_info("Jumping to qclib_1 at 0x%llx\n", entry_point);
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
