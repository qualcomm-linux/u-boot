// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __SPL_H__
#define __SPL_H__

#include <linux/types.h>

/**
 * struct qcom_cdt_desc - Board-specific CDT (Config/Chip Data Table) staging
 *			  parameters, provided by qcom_spl_soc_cdt_desc().
 * @part_name:		GPT partition name to search for (e.g. "cdt").
 * @part_type_guid:	Fallback GPT partition type GUID, checked if @part_name
 *			isn't found. May be NULL to skip the fallback.
 * @addr:		IMEM address to stage the CDT contents at.
 * @size:		Maximum number of bytes to stage at @addr.
 * @mmc_hwpart:		eMMC hardware partition the GPT partition lives on.
 *			Ignored for non-MMC boot media.
 */
struct qcom_cdt_desc {
	const char *part_name;
	const char *part_type_guid;
	u64 addr;
	u32 size;
	int mmc_hwpart;
};

void qcom_spl_malloc_init_f(void);
int qcom_spl_loader_pre_ddr(u8 boot_device);
void qcom_spl_error_handler(void *arg);

/*
 * __weak SoC/board extension point: a board's spl-<soc>.c may provide a
 * strong override to describe its CDT (Config/Chip Data Table) location;
 * the default returns NULL (no CDT support).
 */
const struct qcom_cdt_desc *qcom_spl_soc_cdt_desc(void);

/* Generic CDT loader, shared by all Snapdragon boards; driven by the
 * descriptor returned by qcom_spl_soc_cdt_desc().
 */
int qcom_spl_cdt_load(void);

#endif /* __SPL_H__ */