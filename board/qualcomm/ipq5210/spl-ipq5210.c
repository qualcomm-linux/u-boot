// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <hang.h>
#include <cpu_func.h>
#include <event.h>
#include <init.h>
#include <image.h>
#include <spl.h>
#include <spl_load.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sections.h>
#include <soc/qcom/smem.h>
#include <atf_common.h>
#include <linux/err.h>
#include <dm/device-internal.h>
#include <part.h>
#include <blk.h>
#include <dm/uclass.h>
#include <mach/pbl.h>
#include <mach/spl.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_SPL_BUILD)
/**
 * board_init_f() - Main entry point for SPL.
 * @dummy:	Dummy argument (unused).
 */
void board_init_f(ulong dummy)
{
	int ret;

	memset(__bss_start, 0, __bss_end - __bss_start); /* Clear BSS */

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	event_notify_null(EVT_LAST_STAGE_INIT);

	preloader_console_init();

	ret = qcom_spl_loader_pre_ddr(spl_boot_device());
	if (ret) {
		pr_debug("qcom_spl_loader_pre_ddr() failed (%d)\n", ret);
		goto fail;
	}

	ret = qclib_post_process_from_spl();
	if (ret) {
		pr_debug("qclib_post_process_from_spl() failed (%d)\n", ret);
		goto fail;
	}

	board_init_r(NULL, 0);

fail:
	if (ret)
		reset_cpu();
}
#endif /* CONFIG_SPL_BUILD */

int board_fit_config_name_match(const char *name)
{
	/*
	 * SPL loads the pre-HLOS images from bootldr FIT image
	 * as below
	 *
	 * In board_init_f() - Matches "pre-ddr" configuration node and
	 * load the images mentioned in its <loadables>
	 *
	 * In board_init_r() - Matches "post-ddr" configuration node and
	 * load the images mentioned in its <loadables>
	 *
	 */
	if (!(gd->flags & GD_FLG_SPL_INIT)) {
		if (!strcmp(name, "pre-ddr")) {
			printf("Selected FIT Config: %s\n", name);
			return 0;
		}
	} else {
		if (!strcmp(name, "post-ddr")) {
			printf("Selected FIT Config: %s\n", name);
			return 0;
		}
	}

	return -EINVAL;
}
