// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <efi_loader.h>
#include <fdt_support.h>
#include <log.h>
#include <linux/sizes.h>
#include <asm/global_data.h>
#include "qcom-priv.h"

DECLARE_GLOBAL_DATA_PTR;

/**
 * struct reserved_mem_region - Reserved memory region descriptor
 * @start: Physical start address of the region
 * @size: Size of the region in bytes
 * @name: Human-readable name for logging
 */
struct reserved_mem_region {
	u64 start;
	u64 size;
	const char *name;
};

/* QCS615 reserved memory regions */
static const struct reserved_mem_region qcs615_reserved[] = {
	{ 0x80000000, 0x600000,    "hyp" },
	{ 0x85D00000, 0x200000,    "boot" },
	{ 0x85F00000, 0x20000,     "aop" },
	{ 0x85F40000, 0x30000,     "xbl_dt" },
	{ 0x86200000, 0x100000,    "tz_stat" },
	{ 0x86300000, 0x1200000,   "tags" },
	{ 0x87500000, 0x500000,    "tz" },
	{ 0x87A00000, 0x1C00000,   "tzapps" },
	{ 0x8AB00000, 0xCC17000,   "pil" },
	{ 0xA0000000, 0x1600000,   "secure_dsp" },
};

/* SC7280/QCM6490 reserved memory regions */
static const struct reserved_mem_region sc7280_reserved[] = {
	{ 0x80600000, 0x100000,    "axon_dma" },
	{ 0x80894000, 0x40000,     "xbl_dt" },
	{ 0x84300000, 0x16B00000,  "pil_reserved" },
	{ 0xE1000000, 0x2400000,   "display" },
};

/**
 * add_soc_reserved_memory() - Add SoC-specific reserved memory to EFI map
 * @regions: Array of reserved memory regions
 * @count: Number of regions in the array
 * @soc_name: SoC name for logging
 *
 * This function iterates through the provided reserved memory regions and
 * adds them to the EFI memory map with type EFI_RESERVED_MEMORY_TYPE.
 */
static void add_soc_reserved_memory(const struct reserved_mem_region *regions,
				    int count, const char *soc_name)
{
	int i;

	log_debug("Adding %s reserved memory to EFI map (%d regions)\n",
		  soc_name, count);

	for (i = 0; i < count; i++) {
		efi_status_t ret;

		ret = efi_add_memory_map(regions[i].start,
					 regions[i].size,
					 EFI_RESERVED_MEMORY_TYPE);

		if (ret != EFI_SUCCESS) {
			log_err("%s: Failed to reserve %s (0x%llx-0x%llx): %lu\n",
				soc_name, regions[i].name,
				regions[i].start,
				regions[i].start + regions[i].size,
				ret & ~EFI_ERROR_MASK);
		} else {
			log_debug("%s: Reserved %s: 0x%llx-0x%llx (%llu KB)\n",
				  soc_name, regions[i].name,
				  regions[i].start,
				  regions[i].start + regions[i].size,
				  regions[i].size / 1024);
		}
	}
}

/**
 * qcom_add_reserved_memory_to_efi() - Add Qualcomm SoC reserved memory to EFI
 *
 * This function detects the SoC type from the device tree and adds the
 * appropriate reserved memory regions to the EFI memory map.
 *
 * Supported SoCs:
 * - QCS615 (Talos)
 * - QCM6490/SC7280 (Kodiak)
 */
void qcom_add_reserved_memory_to_efi(void)
{
	/* Detect SoC and add appropriate reserved memory */
	if (fdt_node_check_compatible(gd->fdt_blob, 0, "qcom,qcs615") == 0) {
		add_soc_reserved_memory(qcs615_reserved,
					ARRAY_SIZE(qcs615_reserved),
					"QCS615");
	} else if (fdt_node_check_compatible(gd->fdt_blob, 0, "qcom,qcm6490") == 0 ||
		   fdt_node_check_compatible(gd->fdt_blob, 0, "qcom,sc7280") == 0) {
		add_soc_reserved_memory(sc7280_reserved,
					ARRAY_SIZE(sc7280_reserved),
					"QCM6490/SC7280");
	} else {
		log_debug("No SoC-specific reserved memory to add\n");
	}
}
