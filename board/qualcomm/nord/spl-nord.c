// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPL support for Qualcomm Nord Ride.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/sections.h>
#include <asm/io.h>
#include <atf_common.h>
#include <cpu_func.h>
#include <hang.h>
#include <init.h>
#include <spl.h>
#include <mach/spl.h>

#if defined(CONFIG_SPL_BUILD)

DECLARE_GLOBAL_DATA_PTR;

#define NORD_BOOT_CONFIG_ADDR		0x360d4700
#define NORD_BOOT_CONFIG_FAST_BOOT_MASK	0x3e
#define NORD_BOOT_CONFIG_FAST_SHIFT	1

enum nord_boot_option {
	NORD_BOOT_UFS_THEN_USB_0 = 0x0,
	NORD_BOOT_UFS_THEN_USB_1 = 0x1,
	NORD_BOOT_PCIE_FLASHLESS = 0x2,
	NORD_BOOT_QSPI_THEN_PCIE = 0x3,
	NORD_BOOT_DSPI_THEN_PCIE = 0x4,
	NORD_BOOT_QSPI_PCIE_THEN_PCIE = 0x5,
	NORD_BOOT_DSPI_PCIE_THEN_PCIE = 0x6,
};

/**
 * board_init_f() - SPL entry point for Nord Ride.
 * @dummy: Unused.
 *
 * XBL has already initialised DDR before handing control to SPL. Nord
 * therefore follows the Lemans chainloaded-SPL flow and skips the generic
 * pre-DDR/QCLIB path.
 */
void board_init_f(ulong dummy)
{
	int ret;

	memset(__bss_start, 0, __bss_end - __bss_start);

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	preloader_console_init();

	board_init_r(NULL, 0);

	return;

fail:
	reset_cpu();
}

/**
 * spl_boot_device() - Determine the SPL boot device for Nord Ride.
 *
 * Nord is chainloaded by XBL, so PBL shared data in r0 is not
 * available here. Read the boot option selected by XBL from
 * BOOT_CONFIG instead.
 *
 * Return: The primary SPL boot device selected by BOOT_CONFIG.
 *
 * BOOT_CONFIG describes fallback paths as well, but spl_boot_device() can
 * return only one device. Return the primary device and reject boot options
 * that do not have a corresponding SPL boot-device type.
 */
u32 spl_boot_device(void)
{
	u32 boot_config;
	u32 boot_option;

	boot_config = readl((void *)NORD_BOOT_CONFIG_ADDR);
	boot_option = (boot_config & NORD_BOOT_CONFIG_FAST_BOOT_MASK) >>
		      NORD_BOOT_CONFIG_FAST_SHIFT;

	switch (boot_option) {
	case NORD_BOOT_UFS_THEN_USB_0:
	case NORD_BOOT_UFS_THEN_USB_1:
		printf("Selected boot device: UFS (USB fallback)\n");
		return BOOT_DEVICE_UFS;
	case NORD_BOOT_QSPI_THEN_PCIE:
	case NORD_BOOT_DSPI_THEN_PCIE:
	case NORD_BOOT_QSPI_PCIE_THEN_PCIE:
	case NORD_BOOT_DSPI_PCIE_THEN_PCIE:
		printf("Selected boot device: SPI (PCIe fallback)\n");
		return BOOT_DEVICE_SPI;
	case NORD_BOOT_PCIE_FLASHLESS:
		printf("Unsupported boot option: PCIe flashless\n");
		return BOOT_DEVICE_NONE;
	default:
		printf("Unsupported boot option: %u\n", boot_option);
		return BOOT_DEVICE_NONE;
	}
}

/**
 * qcom_spl_board_fixup_bl31_params_v2() - Apply Nord's BL31 parameter fixups.
 * @bl_params: BL31 parameters populated by the Snapdragon implementation.
 *
 * The Snapdragon implementation handles the QCSDI-to-BL31 handoff. Nord
 * additionally passes the optional TOC FDT address to BL32.
 */
void qcom_spl_board_fixup_bl31_params_v2(struct bl_params *bl_params)
{
	struct bl_params_node *node;
	u64 toc_fdt_address;

	toc_fdt_address = qcom_spl_get_toc_fdt_address();

	for_each_bl_params_node(bl_params, node) {
		if (node->image_id == ATF_BL32_IMAGE_ID) {
			if (toc_fdt_address == 0)
				pr_debug("TOC FDT address not set, BL32 will get arg0=0\n");

			node->ep_info->args.arg0 = toc_fdt_address;
			printf("TOC FDT address passed to BL32 (arg0): 0x%lx\n",
			       (unsigned long)node->ep_info->args.arg0);
		}
	}
}

#endif /* CONFIG_SPL_BUILD */
