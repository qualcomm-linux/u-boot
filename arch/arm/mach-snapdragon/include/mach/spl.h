/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __SPL_H__
#define __SPL_H__

void qcom_spl_malloc_init_f(void);
int qcom_spl_loader_pre_ddr(u8 boot_device);
int qclib_post_process_from_spl(void);
void qcom_spl_error_handler(void *arg);

#endif /* __SPL_H__ */
