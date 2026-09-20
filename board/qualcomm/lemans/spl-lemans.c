// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPL support for the Qualcomm Lemans EVK.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/sections.h>
#include <cpu_func.h>
#include <hang.h>
#include <init.h>
#include <spl.h>
#include <mach/spl.h>

#if defined(CONFIG_SPL_BUILD)

DECLARE_GLOBAL_DATA_PTR;

/**
 * board_init_f() - SPL entry point for Lemans EVK.
 * @dummy:	Unused.
 *
 * DDR is already initialised by XBL, so unlike the generic mach-snapdragon
 * board_init_f() this skips qcom_spl_loader_pre_ddr() and
 * qcom_spl_invoke_qclib() and proceeds straight to board_init_r().
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
 * spl_boot_device() - Determine the SPL boot device for Lemans EVK.
 *
 * XBL loads U-Boot SPL from the UFS "uefi_a" partition and leaves no valid PBL
 * shared data in r0, so the boot device is always UFS.
 *
 * Return: BOOT_DEVICE_UFS.
 */
u32 spl_boot_device(void)
{
	return BOOT_DEVICE_UFS;
}

#endif /* CONFIG_SPL_BUILD */
