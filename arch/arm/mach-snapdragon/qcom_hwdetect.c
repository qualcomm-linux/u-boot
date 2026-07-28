// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm Hardware Detection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Detects hardware parameters from SMEM, TCSR registers, and IMEM
 * for use by DTB selection and other subsystems.
 */

#include <dm.h>
#include <log.h>
#include <qcom_hwinfo.h>
#include <asm/global_data.h>
#include <dm/uclass.h>
#include <linux/errno.h>
#include <linux/sizes.h>
#include <soc/qcom/smem.h>
#include <soc/qcom/socinfo.h>

#include "qcom_hwdetect.h"

DECLARE_GLOBAL_DATA_PTR;

/* TCSR SOC HW Version register field masks */
#define TCSR_MAJOR_VERSION_MASK    0x0000ff00
#define TCSR_MAJOR_VERSION_SHIFT   8
#define TCSR_MINOR_VERSION_MASK    0x000000ff
#define TCSR_MINOR_VERSION_SHIFT   0

/* DDR type enum */
enum ddr_type {
	DDRTYPE_256MB = 1,
	DDRTYPE_512MB,
	DDRTYPE_1024MB,
	DDRTYPE_2048MB,
	DDRTYPE_3072MB,
	DDRTYPE_4096MB,
	DDRTYPE_128MB,
};

/**
 * qcom_get_hwinfo_dev() - Look up the qcom,hwinfo device
 * @devp: Returns the qcom_hwinfo udevice
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_get_hwinfo_dev(struct udevice **devp)
{
	int ret;

	ret = uclass_get_device_by_driver(UCLASS_MISC, DM_DRIVER_GET(qcom_hwinfo),
					  devp);
	if (ret)
		log_warning("qcom,hwinfo device not found/probed: %d\n", ret);

	return ret;
}

/**
 * qcom_get_ddr_size_type() - Get DDR size type from gd->ram_size
 * @ddr_type: Pointer to store DDR type
 *
 * Uses gd->ram_size which is calculated by dram.c from SMEM/FDT
 * during early boot. Avoids redundant SMEM re-parsing.
 *
 * Return: 0 on success
 */
static int qcom_get_ddr_size_type(u32 *ddr_type)
{
	u64 total_ddr_size = gd->ram_size;

	log_debug("Total DDR Size: 0x%llx (%llu MB)\n",
		  total_ddr_size, total_ddr_size / SZ_1M);

	*ddr_type = 0;
	if (total_ddr_size <= SZ_128M)
		*ddr_type = DDRTYPE_128MB;
	else if (total_ddr_size <= SZ_256M)
		*ddr_type = DDRTYPE_256MB;
	else if (total_ddr_size <= SZ_512M)
		*ddr_type = DDRTYPE_512MB;
	else if (total_ddr_size <= SZ_1G)
		*ddr_type = DDRTYPE_1024MB;
	else if (total_ddr_size <= SZ_2G)
		*ddr_type = DDRTYPE_2048MB;
	else if (total_ddr_size <= (SZ_2G + SZ_1G))
		*ddr_type = DDRTYPE_3072MB;
	else if (total_ddr_size <= SZ_4G)
		*ddr_type = DDRTYPE_4096MB;

	log_debug("DDR Type: %u\n", *ddr_type);

	return 0;
}

int qcom_hwdetect_get_params(struct qcom_hw_params *params)
{
	struct udevice *hwinfo;
	struct socinfo *soc_info;
	size_t size;
	int ret;
	u32 raw_version, reg_val, major, minor;

	if (!params)
		return -EINVAL;

	memset(params, 0, sizeof(*params));

	ret = qcom_get_hwinfo_dev(&hwinfo);
	if (ret) {
		log_err("qcom,hwinfo device not available\n");
		return ret;
	}

	soc_info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID, &size);
	if (IS_ERR_OR_NULL(soc_info)) {
		log_err("Failed to get SOC info from SMEM\n");
		return -ENODEV;
	}

	params->chip_id = le32_to_cpu(soc_info->id) & 0xffff;

	raw_version = le32_to_cpu(soc_info->plat_ver);
	params->board_version = (SOCINFO_MAJOR(raw_version) << 4) | SOCINFO_MINOR(raw_version);

	ret = qcom_hwinfo_get_tcsr_hw_version(hwinfo, &reg_val);
	if (ret) {
		log_err("Failed to read TCSR HW version via qcom,hwinfo: %d\n", ret);
		return ret;
	}
	major = (reg_val & TCSR_MAJOR_VERSION_MASK) >> TCSR_MAJOR_VERSION_SHIFT;
	minor = (reg_val & TCSR_MINOR_VERSION_MASK) >> TCSR_MINOR_VERSION_SHIFT;
	params->chip_version = (major << 4) | minor;

	params->platform = le32_to_cpu(soc_info->hw_plat);
	params->subtype = le32_to_cpu(soc_info->hw_plat_subtype);

	if (le32_to_cpu(soc_info->fmt) >= 17)
		params->oem_variant_id = le32_to_cpu(soc_info->oem_variant);

	if (le32_to_cpu(soc_info->fmt) >= 9)
		params->foundry_id = le32_to_cpu(soc_info->foundry_id);

	ret = qcom_get_ddr_size_type(&params->ddr_size_type);
	if (ret)
		log_warning("Failed to get DDR size, defaulting to 0\n");

	ret = qcom_hwinfo_get_storage_type(hwinfo, &params->storage_type);
	if (ret) {
		log_warning("Failed to detect storage type via qcom,hwinfo, defaulting to UFS\n");
		params->storage_type = QCOM_HWINFO_STORAGE_UFS;
	}

	log_debug("Hardware Parameters:\n");
	log_debug("  Chip ID: 0x%x\n", params->chip_id);
	log_debug("  Chip Version: 0x%x\n", params->chip_version);
	log_debug("  Board Version: 0x%x\n", params->board_version);
	log_debug("  Platform: 0x%x\n", params->platform);
	log_debug("  Subtype: 0x%x\n", params->subtype);
	log_debug("  OEM Variant ID: 0x%x\n", params->oem_variant_id);
	log_debug("  DDR Size Type: %u\n", params->ddr_size_type);
	log_debug("  Storage Type: %u\n", params->storage_type);
	log_debug("  Foundry ID: 0x%x\n", params->foundry_id);

	return 0;
}