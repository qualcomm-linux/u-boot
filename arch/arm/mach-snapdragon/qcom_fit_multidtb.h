/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm FIT Multi-DTB Selection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * This implements automatic DTB selection from FIT images based on hardware
 * detection via SMEM.
 */

#ifndef __QCOM_FIT_MULTIDTB_H__
#define __QCOM_FIT_MULTIDTB_H__

#include <linux/types.h>
#include <linux/list.h>

/**
 * struct bucket_node - Node in the bucket list
 * @list: List head for linking nodes
 * @name: Node name string (e.g., "qcom", "sa8775p-v2", "ride", "ufs", "8gb")
 *
 * The bucket list contains all matching node names from the metadata DTB.
 * These are used to match against FIT configuration compatible strings.
 */
struct bucket_node {
	struct list_head list;
	char *name;
};

/* Node processing types for metadata DTB parsing */
enum node_process_type {
	NODE_TYPE_OEM,
	NODE_TYPE_SOC,
	NODE_TYPE_SOCVER,
	NODE_TYPE_BOARD,
	NODE_TYPE_BOARDREV,
	NODE_TYPE_PERIPHERAL,
	NODE_TYPE_STORAGE,
	NODE_TYPE_DDR_SIZE,
	NODE_TYPE_SOFTSKU,
};

/* Function prototypes */

/**
 * qcom_fit_multidtb_setup() - Main entry point for FIT multi-DTB selection
 *
 * This function:
 * 1. Loads qclinux_fit.img from EFI partition
 * 2. Extracts metadata DTB
 * 3. Detects hardware parameters from SMEM
 * 4. Builds bucket list from metadata
 * 5. Finds matching FIT configuration
 * 6. Loads DTB and applies overlays
 * 7. Sets FDT for EFI
 *
 * Return: 0 on success, negative error code on failure
 */
int qcom_fit_multidtb_setup(void);

#endif /* __QCOM_FIT_MULTIDTB_H__ */
