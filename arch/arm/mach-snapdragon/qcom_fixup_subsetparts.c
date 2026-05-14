// SPDX-License-Identifier: GPL-2.0+
/* SUBSET Parts Fixup: A tool for fixing up subset parts in a system
 *
 * Copyright (c) 2017,2019, 2020 The Linux Foundation. All rights reserved.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <dm.h>
#include <fdt_support.h>
#include <smem.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/libfdt.h>
#include <soc/qcom/socinfo.h>
#include "chipinfo_def.h"
#include "qcom_fixup_handlers.h"
#include "qcom-priv.h"

static const unsigned char *const part_names[] = {
	[CHIPINFO_PART_GPU] = "gpu",
	[CHIPINFO_PART_VIDEO] = "video",
	[CHIPINFO_PART_CAMERA] = "camera",
	[CHIPINFO_PART_DISPLAY] = "display",
	[CHIPINFO_PART_AUDIO] = "audio",
	[CHIPINFO_PART_MODEM] = "modem",
	[CHIPINFO_PART_WLAN] = "wlan",
	[CHIPINFO_PART_COMP] = "compute",
	[CHIPINFO_PART_SENSORS] = "sensors",
	[CHIPINFO_PART_NPU] = "npu",
	[CHIPINFO_PART_SPSS] = "spss",
	[CHIPINFO_PART_NAV] = "nav",
	[CHIPINFO_PART_COMPUTE_1] = "compute1",
	[CHIPINFO_PART_DISPLAY_1] = "display1",
	[CHIPINFO_PART_NSP] = "nsp",
	[CHIPINFO_PART_EVA] = "eva",
	[CHIPINFO_PART_PCIE] = "pcie",
};

static int chipinfo_get_disabled_cpus(u32 *value_ptr);
static int read_cpu_subset_parts(u32 *value);
static int chipinfo_get_disabled_features(u32 chip_info_type_idx,
					  u32 *disabled_feature_ptr);
static int read_mm_subset_parts(u32 *value_ptr);
static void read_and_export_parts_disabled_features(void *fdt_ptr,
						    int node_offset);
static int add_platform_info_node(void *fdt_ptr);

/**
 * chipinfo_get_disabled_cpus() - Retrieves the disabled CPUs from the SOC info.
 * @value_ptr: A pointer to a u32 variable to store the disabled CPUs.
 *
 * This function reads the SOC info from the SMEM and extracts the disabled
 * CPUs.
 *
 * Return: 0 on success, negative on failure.
 */
static int chipinfo_get_disabled_cpus(u32 *value_ptr)
{
	u32 *subset_ptr;
	struct socinfo *soc_info_ptr;

	soc_info_ptr = qcom_get_socinfo();
	if (!soc_info_ptr)
		return log_msg_ret("Error: Failed to get socinfo\n", -1);

	if (soc_info_ptr->num_clusters == 0x0) {
		*value_ptr = 0x0;
	} else {
		subset_ptr = (u32 *)(soc_info_ptr
			     + soc_info_ptr->ncluster_array_offset);
		*value_ptr = (u32)(subset_ptr[0]);
	}

	return 0;
}

/**
 * read_cpu_subset_parts() - Retrieves the disabled CPUs from the SOC info.
 * @value: A pointer to a u32 variable to store the disabled CPUs.
 *
 * This function reads the SOC info from the SMEM and extracts the disabled
 * CPUs.
 *
 * Return: 0 on success, negative on failure.
 */
static int read_cpu_subset_parts(u32 *value)
{
	int ret;

	ret = chipinfo_get_disabled_cpus(value);
	if (ret)
		log_err("Failed to get subset[0] CPU. %d\n", ret);

	return ret;
}

/**
 * chipinfo_get_disabled_features() - Retrieves the disabled features from the SOC info.
 * @chip_info_type_idx: The index of the chip info type to retrieve the
 * disabled features for.
 * @disabled_feature_ptr: A pointer to a u32 variable to store the disabled features.
 *
 * This function reads the SOC info from the SMEM and extracts the disabled
 * features.
 *
 * Return: CHIPINFO_SUCCESS on success, CHIPINFO_ERROR_INVALID_PARAMETER on
 * failure.
 */
static int chipinfo_get_disabled_features(u32 chip_info_type_idx,
					  u32 *disabled_feature_ptr)
{
	u32 *subset_ptr;
	struct socinfo *soc_info_ptr;

	if (chip_info_type_idx >= CHIPINFO_NUM_PARTS)
		return CHIPINFO_ERROR_INVALID_PARAMETER;

	soc_info_ptr = qcom_get_socinfo();
	if (!soc_info_ptr)
		return log_msg_ret("Error: Failed to get socinfo\n", -1);

	subset_ptr = (u32 *)(soc_info_ptr
		     + soc_info_ptr->nsubset_parts_array_offset);
	*disabled_feature_ptr = (u32)(subset_ptr[chip_info_type_idx]);

	return CHIPINFO_SUCCESS;
}

/**
 * read_mm_subset_parts() - Retrieves the disabled MM subset parts from the SOC info.
 * @value_ptr: A pointer to a u32 variable to store the disabled MM
 * subset parts.
 *
 * This function reads the SOC info from the SMEM and extracts the disabled MM
 * subset parts.
 *
 * Return: 0 on success, negative on failure.
 */
static int read_mm_subset_parts(u32 *value_ptr)
{
	u32 idx;
	u32 subset_val;
	int ret;
	*value_ptr = 0;

	for (idx = 1; idx < CHIPINFO_NUM_PARTS; idx++) {
		subset_val = 0;
		ret = chipinfo_get_disabled_features(idx, &subset_val);
		if (ret) {
			log_err("Failed to get MM subset[%d] part. %d\n",
				idx, ret);
			continue;
		}
		*value_ptr |= ((subset_val & 0x01) << idx);
	}
	return ret;
}

/**
 * read_and_export_parts_disabled_features() - Reads and exports the disabled
 * features for each part.
 * @fdt_ptr: The firmware DT node to update.
 * @node_offset: The offset in the DT node where the features should be set.
 *
 * This function reads the SOC info from the SMEM and extracts the disabled
 * features for each part. It then exports these features to the firmware DT
 * node.
 */
static void read_and_export_parts_disabled_features(void *fdt_ptr, int node_offset)
{
	u32 mask, n_idx = 0, n_parts = 1;
	int offset_child;
	char str_buffer[24];
	int ret, part;

	for (part = CHIPINFO_PART_GPU; part <= CHIPINFO_PART_PCIE; part++) {
		mask = 0;
		ret = chipinfo_get_disabled_features(n_idx, &mask);
		if (ret) {
			log_err("Failed to get Part %s information. %d\n",
				part_names[part], ret);
			mask = UINT32_MAX;
			n_parts = UINT32_MAX;
		}
		snprintf(str_buffer, sizeof(str_buffer), "subset-%s",
			 part_names[part]);
		offset_child = fixup_dt_node(fdt_ptr, node_offset,
					     str_buffer, NULL, ADD_SUBNODE);
		if (offset_child < 0) {
			log_err("Error adding %s, Error: %d\n", str_buffer,
				offset_child);
			continue;
		}
		ret = fixup_dt_node(fdt_ptr, offset_child, "part-info",
				    (void *)&mask, SET_PROP_U32);
		if (ret)
			log_err("ERROR: Cannot update part-info prop\n");

		ret = fixup_dt_node(fdt_ptr, offset_child, "part-count",
				    (void *)&n_parts, SET_PROP_U32);
		if (ret)
			log_err("ERROR: Cannot update part-count prop\n");
	}
}

/**
 * add_platform_info_node() - Adds the platform info node to the firmware DT.
 * @fdt_ptr: The firmware DT to update.
 *
 * This function creates the platform info node in the firmware DT and sets its
 * properties.
 *
 * Return: The offset of the platform info node on success, negative on failure.
 */
static int add_platform_info_node(void *fdt_ptr)
{
	int offset;

	offset = fdt_path_offset(fdt_ptr, "/firmware/qcom,platform-parts-info");
	if (offset >= 0)
		return offset;
	offset = fdt_path_offset(fdt_ptr, "/firmware");
	if (offset < 0) {
		offset = fixup_dt_node(fdt_ptr, 0, "firmware", NULL, ADD_SUBNODE);
		if (offset < 0)
			return log_msg_ret("Error creating firmware node\n",
				offset);
	}
	offset = fixup_dt_node(fdt_ptr, offset, "qcom,platform-parts-info", NULL,
			       ADD_SUBNODE);
	if (offset < 0)
		log_err("Error adding qcom,platform-parts-info, Error: %d\n",
			offset);

	return offset;
}

/**
 * subsetparts_fixup_handler() - This function is the entry point for the
 * Subset Parts fixup handler.
 * @fdt_ptr: The firmware DT node to update.
 *
 * It reads the SOC info from the SMEM, extracts the disabled subset parts, and
 * exports them to the firmware DT node.
 */
void subsetparts_fixup_handler(struct fdt_header *fdt_ptr)
{
	u32 subset_parts_mm_value;
	u32 subset_parts_cpu_value;
	int offset;
	int ret;

	offset = add_platform_info_node(fdt_ptr);
	if (offset < 0) {
		log_err("Failed to add qcom,platform-parts-info node in %s\n",
			__func__);
		return;
	}
	ret = read_mm_subset_parts(&subset_parts_mm_value);
	if (ret) {
		log_err("No mm Subset parts found\n");
	} else {
		ret = fixup_dt_node(fdt_ptr, offset, "subset-parts",
				    (void *)&subset_parts_mm_value, SET_PROP_U32);
		if (ret)
			log_err("ERROR: Cannot update subset-parts prop\n");
	}
	ret = read_cpu_subset_parts(&subset_parts_cpu_value);
	if (ret) {
		log_err("No Subset parts for cpu ss found\n");
	} else {
		ret = fixup_dt_node(fdt_ptr, offset, "subset-cores",
				    (void *)&subset_parts_cpu_value, SET_PROP_U32);
		if (ret)
			log_err("ERROR: Cannot update subset-cores prop\n");
	}
	read_and_export_parts_disabled_features(fdt_ptr, offset);
}
