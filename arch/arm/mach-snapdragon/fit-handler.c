// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <atf_common.h>
#include <image.h>
#include <log.h>
#include <spl.h>
#include <linux/err.h>
#include <linux/libfdt.h>
#include <mach/qclib.h>
#include <mach/spl.h>

#define TOC_FDT				"toc_fdt"

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
 * bl2_plat_get_bl31_params_v2() - Retrieve and fix up Snapdragon BL31 params.
 * @bl32_entry: Entry point for BL32 (OP-TEE).
 * @bl33_entry: Entry point for BL33 (U-Boot/kernel).
 * @fdt_addr:   Address of the Device Tree Blob (FDT).
 *
 * Pass the QCSDI address to BL31 through arg0.
 * Boards can provide a fixup hook for additional handoff requirements.
 *
 * Return: Pointer to the populated BL31 parameters structure.
 */
struct bl_params *bl2_plat_get_bl31_params_v2(ulong bl32_entry,
					      ulong bl33_entry,
					      ulong fdt_addr)
{
	struct bl_params *bl_params;
	struct bl_params_node *node;
	u64 qcsdi_address = qclib_get_qcsdi_address();

	bl_params = bl2_plat_get_bl31_params_v2_default(bl32_entry, bl33_entry,
							fdt_addr);

	for_each_bl_params_node(bl_params, node) {
		if (node->image_id != ATF_BL31_IMAGE_ID)
			continue;

		if (qcsdi_address == 0)
			pr_warn("QCSDI address not set, BL31 may not function correctly\n");

		node->ep_info->args.arg0 = qcsdi_address;
		pr_debug("Setting BL31 arg0 to QCSDI address: 0x%llx\n",
			 qcsdi_address);
	}

	qcom_spl_board_fixup_bl31_params_v2(bl_params);

	return bl_params;
}

/**
 * qcom_spl_board_fixup_bl31_params_v2() - Apply optional board-specific fixups.
 * @bl_params: BL31 parameters populated by the Snapdragon implementation.
 *
 * Boards can override this weak hook to add platform-specific parameter
 * handoffs without replacing the common Snapdragon QCSDI fixup.
 */
__weak void qcom_spl_board_fixup_bl31_params_v2(struct bl_params *bl_params)
{
	(void)bl_params;
}

/**
 * qcom_spl_get_toc_fdt_address() - Get the optional TOC FDT load address.
 *
 * The generic FIT loader has already loaded the node by the time ATF
 * parameters are prepared. Look up its load address so it can be passed to
 * BL32 through arg0.
 *
 * Return: TOC FDT load address, or 0 if the optional node is unavailable.
 */
u64 qcom_spl_get_toc_fdt_address(void)
{
	u64 toc_fdt_address = 0;
	const void *fit = (const void *)CONFIG_SPL_LOAD_FIT_ADDRESS;
	int images_node;
	int toc_fdt_node;
	int ret;

	images_node = fdt_subnode_offset(fit, 0, "images");
	if (images_node < 0) {
		pr_err("Failed to find images node in FIT\n");
		return 0;
	}

	toc_fdt_node = fdt_subnode_offset(fit, images_node, TOC_FDT);
	if (toc_fdt_node < 0) {
		pr_debug("No '%s' node in FIT, BL32 will get arg0=0\n", TOC_FDT);
		return 0;
	}

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, toc_fdt_node,
					       &toc_fdt_address);
	if (ret)
		pr_warn("Failed to get '%s' load address (%d)\n", TOC_FDT, ret);
	else
		printf("TOC FDT address: 0x%lx\n",
		       (unsigned long)toc_fdt_address);

	return toc_fdt_address;
}
