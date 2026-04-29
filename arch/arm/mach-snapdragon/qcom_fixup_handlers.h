/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * SUBSET Parts Fixup: A tool for fixing up subset parts in a system
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <linux/libfdt.h>

enum fdt_fixup_type {
	APPEND_PROP_U32 = 0,
	APPEND_PROP_U64 = 1,
	SET_PROP_U32 = 2,
	SET_PROP_U64 = 3,
	SET_PROP_STRING = 4,
	ADD_SUBNODE = 5,
};

/**
 * boardinfo_fixup_handler() - Board info fixup handler.
 * @fdt_ptr: Pointer to the device tree.
 *
 * This function is called to fix up the device tree blob (DTB) for the board.
 * It adds or modifies nodes, properties, and values as necessary to ensure
 * the DTB is in a valid state for the board.
 *
 * Return: None
 */
void boardinfo_fixup_handler(struct fdt_header *fdt_ptr);

/**
 * subsetparts_fixup_handler() - This function is the entry point for the
 * Subset Parts fixup handler.
 * @fdt_ptr: The firmware DT node to update.
 *
 * It reads the SOC info from the SMEM, extracts the disabled subset parts, and
 * exports them to the firmware DT node.
 *
 * Return: None
 */
void subsetparts_fixup_handler(struct fdt_header *fdt_ptr);

/**
 * ddrinfo_fixup_handler() - DDRInfo Fixup handler function
 * @fdt_ptr: Pointer to the device tree
 *
 * This function is responsible for updating the DDR information in
 * the device tree.
 *
 * Return: None
 */
void ddrinfo_fixup_handler(struct fdt_header *fdt_ptr);

/**
 * fixup_dt_node() - Exports a property to the firmware DT node.
 * @fdt_ptr: The firmware DT node to update.
 * @node_offset: The offset in the DT node where the property should be set.
 * @property_name: The name of the property to set.
 * @property_value: The value of the property to set.
 * @type: Fixup type
 * This function sets a property in the firmware DT node with the given name and
 * value.
 *
 * Return: 0 on success, negative on failure.
 */
int fixup_dt_node(void *fdt_ptr, int node_offset,
		  const char *property_name,
		  void *property_value,
		  enum fdt_fixup_type type);
