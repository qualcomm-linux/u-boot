/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Qualcomm TrustZone Peripheral Image Loader (PIL) SIP calls.
 *
 * Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_SCM_PIL_H__
#define __QCOM_SCM_PIL_H__

#include <linux/errno.h>
#include <linux/types.h>

/* GENI SE peripheral/subsys ID used by EDK2's GpiDrvLib for this SoC */
#define PAS_ID_GENI_SE		0x13

#if IS_ENABLED(CONFIG_QCOM_GENI_SE_PIL_UNLOCK)
int qcom_scm_pas_unlock(u32 peripheral);
#else
static inline int qcom_scm_pas_unlock(u32 peripheral)
{
	return -ENOSYS;
}
#endif

#endif /* __QCOM_SCM_PIL_H__ */
