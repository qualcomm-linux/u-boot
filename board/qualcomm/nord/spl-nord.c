// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/sections.h>
#include <asm/io.h>
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
	NORD_BOOT_UFS = 0x0,
	NORD_BOOT_SPI = 0x3,
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
	case NORD_BOOT_UFS:
		printf("Selected boot device: UFS\n");
		return BOOT_DEVICE_UFS;
	case NORD_BOOT_SPI:
		printf("Selected boot device: SPI\n");
		return BOOT_DEVICE_SPI;
	default:
		printf("Unsupported boot option: %u\n", boot_option);
		return BOOT_DEVICE_NONE;
	}
}

#endif /* CONFIG_SPL_BUILD */
