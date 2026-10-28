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

#endif /* __QCOM_FIT_MULTIDTB_H__ */
