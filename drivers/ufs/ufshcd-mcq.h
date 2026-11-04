/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ufshcd-mcq.h - UFSHCI 4.0 Multi-Circular Queue (MCQ) support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __UFSHCD_MCQ_H
#define __UFSHCD_MCQ_H

struct ufs_hba;

/* Minimum ring depth needed to let the SQ/CQ pointers advance */
#define UFSHCD_MCQ_DEPTH	2

bool ufshcd_mcq_capable(struct ufs_hba *hba);
int ufshcd_mcq_alloc(struct ufs_hba *hba);
int ufshcd_mcq_make_operational(struct ufs_hba *hba);
int ufshcd_mcq_send_command(struct ufs_hba *hba, unsigned int task_tag);
int ufshcd_mcq_get_tr_ocs(struct ufs_hba *hba);

#endif /* __UFSHCD_MCQ_H */
