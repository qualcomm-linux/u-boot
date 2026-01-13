// SPDX-License-Identifier: GPL-2.0+
/*
 * Board Info FDT Fixup
 *
 * Copyright (c) 2015-2018, 2020-2021, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * This file contains code to fix up the device tree blob (DTB) for the board.
 * It is responsible for adding or modifying nodes, properties, and values as
 * necessary to ensure the DTB is in a valid state for the board.
 *
 * The fixups are applied in the following order:
 * 1. Add any necessary nodes or properties
 * 2. Modify existing nodes or properties
 * 3. Remove any unnecessary nodes or properties
 *
 * Each fixup is documented with a comment explaining its purpose and any
 * relevant details.
 */

#include <dm.h>
#include <fdt_support.h>
#include <smem.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/libfdt.h>
#include <soc/qcom/socinfo.h>
#include "chipinfo_def.h"
#include "qcom_fixup_handlers.h"
#include "qcom-priv.h"

static const char *const feature_code_names_external[] = {
	[CHIPINFO_SKU_UNKNOWN] = "Unknown",
	[CHIPINFO_SKU_AA] = "AA",
	[CHIPINFO_SKU_AB] = "AB",
	[CHIPINFO_SKU_AC] = "AC",
	[CHIPINFO_SKU_AD] = "AD",
	[CHIPINFO_SKU_AE] = "AE",
	[CHIPINFO_SKU_AF] = "AF",
};

static int add_serialnum_platinfo_prop(void *fdt_ptr, u32 node_offset);
static int add_sku_prop(void *fdt_ptr, u32 node_offset);
static int add_platforminfo_node(void *fdt_ptr);
static void add_platforminfo_properties(void *fdt_ptr);

/**
 * board_serial_num() - Retrieves the board serial number from the SMEM.
 * @serial_num_ptr: Pointer to a u32 variable to store the board
 * serial number.
 *
 * Return: 0 on success, negative error code on failure.
 */
int board_serial_num(u32 *serial_num_ptr)
{
	struct socinfo *soc_info_ptr;

	soc_info_ptr = qcom_get_socinfo();
	if (!soc_info_ptr)
		return log_msg_ret("Error: Failed to get socinfo\n", -1);

	*serial_num_ptr = soc_info_ptr->serial_num;

	return 0;
}

/**
 * add_serialnum_platinfo_prop() - Adds the serial number property to the platform information node.
 * @fdt_ptr: Pointer to the device tree.
 * @node_offset: Offset of the platform information node in the device tree.
 *
 * This function adds the serial number property to the platform information
 * node in the device tree. The serial number is retrieved from the SMEM using
 * the board_serial_num function.
 *
 * Return: 0 on success, negative on failure.
 */
static int add_serialnum_platinfo_prop(void *fdt_ptr, u32 node_offset)
{
	u32 serial_num;
	int ret;

	ret = board_serial_num(&serial_num);
	if (ret)
		return log_msg_ret("ERROR: Could not find serial number to populate\n",
				   ret);
	log_debug("Serial Number: 0x%x\n", serial_num);

	ret = fixup_dt_node(fdt_ptr, node_offset, "serial-number",
			    (void *)&serial_num, SET_PROP_U32);
	if (ret)
		log_err("ERROR: Cannot update Platform info node with SerialNumber - %d\n",
			ret);

	return ret;
}

static int add_sku_prop(void *fdt_ptr, u32 node_offset)
{
	u32 pcode;
	char feature_code[24];
	int ret;
	static const char feature_code_prop[] = "feature-code";
	struct socinfo *soc_info_ptr;

	soc_info_ptr = qcom_get_socinfo();
	if (!soc_info_ptr)
		return log_msg_ret("Error: Failed to get socinfo\n", -1);

	strlcpy(feature_code,
		feature_code_names_external[soc_info_ptr->feature_code],
		sizeof(feature_code));
	if (soc_info_ptr->pcode == 0)
		pcode = 0xFFFFFFFF;
	else
		pcode = soc_info_ptr->pcode;

	ret = fixup_dt_node(fdt_ptr, node_offset, "pcode", (void *)&pcode,
			    SET_PROP_U32);
	if (ret)
		return log_msg_ret("ERROR: Cannot update Platform info node with Pcode\n",
				   ret);
	ret = fixup_dt_node(fdt_ptr, node_offset, feature_code_prop,
			    (void *)feature_code, SET_PROP_STRING);
	if (ret)
		log_err("ERROR: Cannot update Platform info node with feature code - %d\n",
			ret);

	return ret;
}

/**
 * add_platforminfo_node() - Adds the platform info node to the device tree.
 * @fdt_ptr: The device tree to add the platform info node to.
 *
 * This function adds the platform info node to the device tree, including the
 * serial number, SKU information, and other relevant properties.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int add_platforminfo_node(void *fdt_ptr)
{
	int node_offset;

	node_offset = fdt_path_offset(fdt_ptr, "/firmware/qcom,platform-parts-info");
	if (node_offset >= 0)
		return node_offset;

	node_offset = fdt_path_offset(fdt_ptr, "/firmware");
	if (node_offset < 0) {
		node_offset = fixup_dt_node(fdt_ptr, 0, "firmware", NULL,
					    ADD_SUBNODE);
		if (node_offset < 0)
			return log_msg_ret("Error creating firmware node\n",
					   node_offset);
	}

	node_offset = fixup_dt_node(fdt_ptr, node_offset, "qcom,platform-parts-info",
				    NULL, ADD_SUBNODE);
	if (node_offset < 0) {
		log_err("Error adding qcom,platform-parts-info, Error: %d\n",
			node_offset);
		node_offset = fdt_path_offset(fdt_ptr,
					      "/firmware/qcom,platform-parts-info");
		if (node_offset < 0)
			log_err("Retry getting qcom,platform-parts-info offset, Error: %d\n",
				node_offset);
	}

	return node_offset;
}

/**
 * add_platforminfo_properties() - Adds platform information properties to the device tree.
 * @fdt_ptr: The device tree to update.
 *
 * This function populates the device tree with platform information properties,
 * including the serial number, SKU information, and platform parts information.
 *
 * Return: 0 on success, negative error code on failure.
 */
static void add_platforminfo_properties(void *fdt_ptr)
{
	int ret = 0;
	int node_offset;

	node_offset = add_platforminfo_node(fdt_ptr);
	if (node_offset < 0) {
		log_err("Failed to add qcom,platform-parts-info\n");
		return;
	}

	ret = add_serialnum_platinfo_prop(fdt_ptr, node_offset);
	if (ret)
		log_err("Failed to add Serial Number property\n");

	ret = add_sku_prop(fdt_ptr, node_offset);
	if (ret)
		log_err("Failed to add SKU properties\n");
}

/**
 * boardinfo_fixup_handler() - Board info fixup handler.
 * @fdt_ptr: Pointer to the device tree.
 *
 * This function is called to fix up the device tree blob (DTB) for the board.
 * It adds or modifies nodes, properties, and values as necessary to ensure the
 * DTB is in a valid state for the board.
 */
void boardinfo_fixup_handler(struct fdt_header *fdt_ptr)
{
	add_platforminfo_properties(fdt_ptr);
}
