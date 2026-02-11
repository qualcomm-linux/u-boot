/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Type and enum definitions for the Chip Info driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum chip_info_result {
	CHIPINFO_SUCCESS = 0,
	CHIPINFO_ERROR = -1,
	CHIPINFO_ERROR_INVALID_PARAMETER = -2,
	CHIPINFO_ERROR_INSUFFICIENT_MEMORY = -3,
	CHIPINFO_ERROR_NOT_FOUND = -4,
	CHIPINFO_ERROR_INTERNAL = -5,
	CHIPINFO_ERROR_NOT_ALLOWED = -6,
	CHIPINFO_ERROR_NOT_SUPPORTED = -7,
	CHIPINFO_ERROR_NOT_INITIALIZED = -8,
	CHIPINFO_ERROR_OUT_OF_RANGE_PARAMETER = -9,
	CHIPINFO_ERROR_INVALID_ADDRESS = -10,
	CHIPINFO_ERROR_INSUFFICIENT_BUFFER_LENGTH = -11,
};

/**
 * Supported parts by the chipinfo_get_disabled_features API.
 * New parts should be appended to this enum (i.e. not inserted)
 * to preserve backwards compatibility.
 *
 * For targets that have multiple instances of a specific part,
 * a new part with a _N suffix should be added. On these targets,
 * part enums without the _N suffix are for the 0th instance,
 * e.g. CHIPINFO_PART_DISPLAY is for MDSS_0 on Makena.
 */
enum chip_info_Part_type {
	CHIPINFO_PART_UNKNOWN = 0,
	CHIPINFO_PART_GPU = 1,
	CHIPINFO_PART_VIDEO = 2,
	CHIPINFO_PART_CAMERA = 3,
	CHIPINFO_PART_DISPLAY = 4,
	CHIPINFO_PART_AUDIO = 5,
	CHIPINFO_PART_MODEM = 6,
	CHIPINFO_PART_WLAN = 7,
	CHIPINFO_PART_COMP = 8, // For both CDSP and NSP
	CHIPINFO_PART_SENSORS = 9,
	CHIPINFO_PART_NPU = 10,
	CHIPINFO_PART_SPSS = 11,
	CHIPINFO_PART_NAV = 12,
	CHIPINFO_PART_COMPUTE_1 = 13,
	CHIPINFO_PART_DISPLAY_1 = 14,
	CHIPINFO_PART_NSP = 15,
	CHIPINFO_PART_EVA = 16,
	CHIPINFO_PART_PCIE = 17,
	CHIPINFO_PART_CPU = 18,
	CHIPINFO_PART_DDR = 19,

	CHIPINFO_NUM_PARTS,
	CHIPINFO_PART_32BITS = 0x7FFFFFFF
};

/**
 * SKU_IDs logical mapping.
 * See ChipInfo_GetSKU for more details.
 */
enum chip_info_skuid_type {
	CHIPINFO_SKU_UNKNOWN = 0,

	CHIPINFO_SKU_AA = 0x01,
	CHIPINFO_SKU_AB = 0x02,
	CHIPINFO_SKU_AC = 0x03,
	CHIPINFO_SKU_AD = 0x04,
	CHIPINFO_SKU_AE = 0x05,
	CHIPINFO_SKU_AF = 0x06,
	// Reserved for future use

	CHIPINFO_SKU_Y0 = 0xf1,
	CHIPINFO_SKU_Y1 = 0xf2,
	CHIPINFO_SKU_Y2 = 0xf3,
	CHIPINFO_SKU_Y3 = 0xf4,
	CHIPINFO_SKU_Y4 = 0xf5,
	CHIPINFO_SKU_Y5 = 0xf6,
	CHIPINFO_SKU_Y6 = 0xf7,
	CHIPINFO_SKU_Y7 = 0xf8,
	// Reserved for future use

};
