// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <hang.h>
#include <cpu_func.h>
#include <event.h>
#include <init.h>
#include <image.h>
#include <linux/string.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <soc/qcom/smem.h>
#include <atf_common.h>
#include <linux/err.h>
#include <dm/device-internal.h>
#include <part.h>
#include <blk.h>
#include <dm/uclass.h>
#include <mach/qclib.h>
#include <mach/spl.h>

DECLARE_GLOBAL_DATA_PTR;

#define QCCONFIG			"qc_config"

/**
 * qcom_spl_soc_qclib_override() - Populate ipq5210-specific interface table
 *				    entries.
 * @table:	Pointer to the QCLIB interface table being built.
 * @fit:	Pointer to the FIT image blob.
 * @images_node: FIT "images" node offset.
 *
 * Appends the "qc_config" entry and a placeholder "qcsdi" entry, filled in by QCLIB itself.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_soc_qclib_override(struct interface_table *table,
				const void *fit, int images_node)
{
	int ret;
	int entry_idx;
	int qcconfig_node;

	qcconfig_node = fdt_subnode_offset(fit, images_node, "qcconfig_1");
	if (qcconfig_node < 0) {
		pr_err("Failed to find qcconfig_1 node in FIT\n");
		return -ENOENT;
	}

	entry_idx = table->num_entries;
	memcpy(table->if_table_entries[entry_idx].entry_name,
	       QCCONFIG, strlen(QCCONFIG));

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, qcconfig_node,
					       &table->if_table_entries[entry_idx].address);
	if (ret) {
		pr_err("Failed to get qcconfig_1 entry point (%d)\n", ret);
		return ret;
	}
	table->if_table_entries[entry_idx].attributes = 0;
	table->num_entries = entry_idx + 1;

	entry_idx++;
	memcpy(table->if_table_entries[entry_idx].entry_name,
	       QCSDI, strlen(QCSDI));
	table->if_table_entries[entry_idx].address = 0;
	table->if_table_entries[entry_idx].attributes = 0;
	table->num_entries = entry_idx + 1;

	return 0;
}

/**
 * qcom_spl_soc_post_qclib_routine() - Cache the QCSDI address after QCLIB
 *					returns.
 * @if_tbl:	Pointer to the QCLIB interface table, populated by QCLIB.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int qcom_spl_soc_post_qclib_routine(struct interface_table *if_tbl)
{
	int ret;
	struct interface_table_entry qcsdi_entry;

	ret = qcom_spl_get_iftbl_entry_by_name(if_tbl, QCSDI, &qcsdi_entry);
	if (ret) {
		pr_err("Failed to get QCSDI entry from interface table (%d)\n", ret);
		return ret;
	}

	qclib_set_qcsdi_address(qcsdi_entry.address);
	pr_info("QCSDI address: 0x%llx\n", qcsdi_entry.address);

	return 0;
}
