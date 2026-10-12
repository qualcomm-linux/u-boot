// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __SPL_H__
#define __SPL_H__

#include <linux/types.h>

struct blk_desc;
struct bl_params;

void qcom_spl_malloc_init_f(void);
int qcom_spl_loader_pre_ddr(u8 boot_device);
void qcom_spl_error_handler(void *arg);
int qcom_spl_cdt_load(void);
int qcom_spl_get_boot_blk_desc(struct blk_desc **descp);
int qcom_spl_ddr_training_restore(void);
int qcom_spl_ddr_training_save(u64 address, u32 size);
u64 qcom_spl_get_toc_fdt_address(void);
void qcom_spl_board_fixup_bl31_params_v2(struct bl_params *bl_params);

#endif /* __SPL_H__ */
