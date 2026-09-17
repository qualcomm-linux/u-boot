// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <atf_common.h>
#include <image.h>
#include <log.h>
#include <spl.h>
#include <linux/err.h>
#include <mach/qclib.h>
#include <mach/spl.h>

/**
 * qcom_spl_get_fit_img_entry_point() - Get entry point from FIT image node.
 * @fit:	 Pointer to the FIT image blob.
 * @node:	 Node ID within the FIT image.
 * @entry_point: Pointer to store the retrieved entry point.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_get_fit_img_entry_point(void *fit, int node,
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
int qcom_spl_get_iftbl_entry_by_name(struct interface_table *if_tbl,
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
	u64 qcsdi_address = qclib_get_qcsdi_address();

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
			 * This address was populated by qcom_spl_invoke_qclib()
			 */
			if (qcsdi_address == 0)
				pr_warn("QCSDI address not set, BL31 may not function correctly\n");

			node->ep_info->args.arg0 = qcsdi_address;
			pr_debug("Setting BL31 arg0 to QCSDI address: 0x%llx\n", qcsdi_address);
		}
	}

	return bl_params;
}
