/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Qualcomm device tree fixup handlers
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_FIXUP_HANDLERS_H__
#define __QCOM_FIXUP_HANDLERS_H__

#include <linux/libfdt.h>

/**
 * hypervisor_fixup_handler() - Update the device tree with hypervisor boot
 * information.
 * @fdt_ptr: A pointer to the device tree header.
 */
void hypervisor_fixup_handler(struct fdt_header *fdt_ptr);

#endif /* __QCOM_FIXUP_HANDLERS_H__ */