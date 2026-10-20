// SPDX-License-Identifier: GPL-2.0+
/*
 * Dynamic Linux kernel command-line construction for Qualcomm boards
 * that boot directly from U-Boot (no Qualcomm ABL stage in between).
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define LOG_CATEGORY LOGC_BOARD
#define pr_fmt(fmt) "QCOM: " fmt

#include <blk.h>
#include <bootflow.h>
#include <env.h>
#include <fdt_support.h>
#include <fdtdec.h>
#include <log.h>
#include <mmc.h>
#include <part.h>
#include <stdio.h>
#include <string.h>
#include <dm/read.h>
#include <linux/errno.h>
#include <linux/kernel.h>

/* mmc dev 0 is this board's single eMMC boot device */
#define QCOM_BOOTARGS_MMC_DEV	0
#define QCOM_BOOTARGS_ROOT_PART	"system"

/*
 * HLOS's own boot.scr already sets a "console", "androidboot.console",
 * "androidboot.hardware", etc. bootargs string (see
 * docs/hlos-boot-scr-fix-proposal.md) - this table only carries tokens
 * that are missing from that string and either (a) ABL-dynamic with no
 * other source on this U-Boot-first boot path, or (b) a build-time kernel
 * DTS convention robust against whichever FIT DTB/boot.scr combination is
 * flashed (see console_msg_format below). Every other static token
 * HLOS's boot.scr already supplies is intentionally left for boot.scr to
 * own; duplicating byte-identical values here would just be redundant.
 *
 * androidboot.bootdevice= and silent_boot.mode= are NOT in this table -
 * ABL computes both dynamically too (GetBootDevice() / GetRebootReason()
 * in UpdateCmdLine.c), but derived at runtime below instead of hardcoded,
 * since unlike baseband/fstab_suffix their value isn't invariant for this
 * single-SKU board; see qcom_bootdevice_cmdline_arg() and
 * qcom_silent_boot_mode_cmdline_arg().
 */
struct qcom_bootargs_static_arg {
	const char *arg;
	const char *val;
};

static const struct qcom_bootargs_static_arg qcom_bootargs_static[] = {
	/*
	 * Dynamic in ABL (BoardPlatformChipBaseBand() chip-info query /
	 * GetRootDeviceType() storage-type detection, UpdateCmdLine.c) but
	 * invariant for this board (single baseband, eMMC-only defconfig),
	 * so a static value here is an acceptable simplification - see
	 * docs/bootargs-duplicate-removal-plan.md for the full per-key
	 * ABL comparison. Not present anywhere in HLOS's own boot.scr, so
	 * without this table they would have no source at all.
	 */
	{ "androidboot.baseband",	"msm" },
	{ "androidboot.fstab_suffix",	"emmc" },
	/*
	 * NOT an ABL-dynamic token - console_msg_format is a static kernel
	 * command-line convention baked into the kernel's own DTS at build
	 * time in every flow (ABL-era kernel DTS, this FIT's kernel DTB,
	 * and U-Boot's own echo.dtsi control DTB all hardcode it the same
	 * way), never computed at ABL/U-Boot runtime. Set here as a safety
	 * net matching echo.dtsi's own convention regardless of whether the
	 * incoming FIT kernel DTB's /chosen/bootargs happens to carry it -
	 * cmdline_set_arg() is idempotent (replace-if-present, add-if-
	 * absent), so this is safe even if the DTB already supplied the
	 * same value.
	 */
	{ "console_msg_format",	"syslog" },
};

/* Working buffer must comfortably fit the base DTS string plus every
 * static/dynamic token above; computed total is well under 1KB today.
 */
#define QCOM_BOOTARGS_CMDLINE_SIZE	1024

static int qcom_root_cmdline_arg(char *out, size_t outlen)
{
	struct blk_desc *desc;
	struct disk_partition info;
	int part;

	desc = blk_get_dev("mmc", QCOM_BOOTARGS_MMC_DEV);
	if (!desc)
		return log_msg_ret("blk", -ENODEV);

	part = part_get_info_by_name(desc, QCOM_BOOTARGS_ROOT_PART, &info);
	if (part < 0)
		return log_msg_ret("part", part);

	snprintf(out, outlen, "/dev/mmcblk%dp%d", QCOM_BOOTARGS_MMC_DEV, part);

	return 0;
}

/*
 * ABL's GetBootDevice() (LinuxLoaderLib.c) reads a live BootDeviceBaseAddr
 * UEFI variable rather than hardcoding this address, because the same ABL
 * binary can run on boards whose MMC controller sits at a different SoC
 * address. U-Boot already knows this address via its own probed MMC
 * controller's "reg" property - read it from there instead of duplicating
 * the literal.
 */
static int qcom_bootdevice_cmdline_arg(char *out, size_t outlen)
{
	struct mmc *mmc;
	fdt_addr_t addr;

	mmc = find_mmc_device(QCOM_BOOTARGS_MMC_DEV);
	if (!mmc || !mmc->dev)
		return log_msg_ret("mmc", -ENODEV);

	addr = dev_read_addr(mmc->dev);
	if (addr == FDT_ADDR_T_NONE)
		return log_msg_ret("addr", -ENOENT);

	snprintf(out, outlen, "%llx.sdhci", (unsigned long long)addr);

	return 0;
}

/*
 * serial# may already be set by qcom_set_serialno() (arch/arm/mach-snapdragon/
 * board.c), which relays androidboot.serialno= out of a previous-stage
 * bootloader's /chosen/bootargs when qcom_boot_source == QCOM_BOOT_SOURCE_ANDROID.
 * When U-Boot is the first-stage bootloader (QCOM_BOOT_SOURCE_XBL) there is no
 * previous cmdline to relay, so fall back to the eMMC CID product serial
 * number, the same source used by board/qualcomm/dragonboard410c.
 */
static void qcom_serialno_cmdline_arg(char *out, size_t outlen)
{
	const char *serial = env_get("serial#");
	struct mmc *mmc;

	if (serial && *serial) {
		snprintf(out, outlen, "%s", serial);
		return;
	}

	*out = '\0';

	mmc = find_mmc_device(QCOM_BOOTARGS_MMC_DEV);
	if (!mmc || !mmc->has_init)
		return;

	snprintf(out, outlen, "%08x", mmc->cid[2] << 16 | mmc->cid[3] >> 16);
}

/*
 * Consumes the env var "reboot-mode", already populated at EVT_LAST_STAGE_INIT
 * by the existing nvmem-reboot-mode / qcom-spmi-sdam driver chain (see
 * arch/arm/dts/pmk8550.dtsi's "reboot-mode" node) from the PMIC's reboot
 * reason SDAM cell. This is the U-Boot equivalent of ABL's GetRebootReason()
 * RECOVERY_MODE check.
 */
static bool qcom_recovery_requested(void)
{
	const char *mode = env_get("reboot-mode");

	return mode && !strcmp(mode, "recovery");
}

/*
 * Maps the same env var "reboot-mode" (see qcom_recovery_requested() above)
 * to ABL's silent_boot.mode= values. ABL's GetRebootReason() treats
 * SILENT_MODE/NON_SILENT_MODE/FORCED_SILENT/FORCED_NON_SILENT as sibling
 * cases of the same boot-reason switch that also sets RECOVERY_MODE
 * (ShutdownServices.h). Defaults to "nonsilent" when reboot-mode is unset
 * or not one of the silent-boot modes, matching ABL's own default
 * (LinuxLoader.c's switch falls through to NON_SILENT_MODE when
 * BootReason == NORMAL_MODE).
 *
 * This chip's pmk8550.dtsi "reboot-mode" node currently only declares
 * mode-recovery/mode-bootloader, so "silent"/"forced-silent"/
 * "forced-nonsilent" can never actually be observed here yet - adding
 * the corresponding mode-* DT properties (with the correct PMIC-PON
 * magic values) is a separate follow-up. Until then this always
 * returns "nonsilent", identical to the previous hardcoded behaviour.
 */
static const char *qcom_silent_boot_mode_cmdline_arg(void)
{
	const char *mode = env_get("reboot-mode");

	if (!mode)
		return "nonsilent";
	if (!strcmp(mode, "silent"))
		return "silent";
	if (!strcmp(mode, "forced-silent"))
		return "forcedsilent";
	if (!strcmp(mode, "forced-nonsilent"))
		return "forcednonsilent";

	return "nonsilent";
}

/*
 * Thin wrapper around cmdline_set_arg() that logs if the working buffer
 * overflows (-E2BIG) instead of silently dropping the argument.
 */
static void qcom_cmdline_set_arg(char *cmdline, size_t cmdline_size,
				  const char *arg, const char *val)
{
	int ret;

	ret = cmdline_set_arg(cmdline, cmdline_size, cmdline, arg, val, NULL);
	if (ret < 0)
		log_warning("failed to set cmdline arg '%s': %d\n", arg, ret);
}

const char *board_fdt_chosen_bootargs(const struct fdt_property *fdt_ba)
{
	static char cmdline[QCOM_BOOTARGS_CMDLINE_SIZE];
	char root_arg[32] = "";
	char serial[32];
	char bootdevice[32] = "";
	const char *env_bootargs = env_get("bootargs");
	unsigned int i;

	/*
	 * Base is whatever the kernel DTS already baked into /chosen/bootargs
	 * (e.g. echo.dtsi's "console_msg_format=syslog ftrace_dump_on_oops"),
	 * since this board sets no CONFIG_BOOTARGS/env bootargs of its own.
	 * Fall back to the env var if present, matching the default __weak
	 * behaviour for boards that do rely on it.
	 */
	if (fdt_ba && fdt32_to_cpu(fdt_ba->len))
		snprintf(cmdline, sizeof(cmdline), "%s", fdt_ba->data);
	else
		snprintf(cmdline, sizeof(cmdline), "%s", env_bootargs ?: "");

	for (i = 0; i < ARRAY_SIZE(qcom_bootargs_static); i++)
		qcom_cmdline_set_arg(cmdline, sizeof(cmdline),
				      qcom_bootargs_static[i].arg,
				      qcom_bootargs_static[i].val);

	if (!qcom_root_cmdline_arg(root_arg, sizeof(root_arg))) {
		qcom_cmdline_set_arg(cmdline, sizeof(cmdline), "root", root_arg);
		qcom_cmdline_set_arg(cmdline, sizeof(cmdline), "rootwait",
				      BOOTFLOWCL_EMPTY);
	} else {
		log_warning("could not determine root= partition\n");
	}

	qcom_serialno_cmdline_arg(serial, sizeof(serial));
	if (serial[0])
		qcom_cmdline_set_arg(cmdline, sizeof(cmdline),
				      "androidboot.serialno", serial);

	if (!qcom_bootdevice_cmdline_arg(bootdevice, sizeof(bootdevice)))
		qcom_cmdline_set_arg(cmdline, sizeof(cmdline),
				      "androidboot.bootdevice", bootdevice);
	else
		log_warning("could not determine androidboot.bootdevice=\n");

	qcom_cmdline_set_arg(cmdline, sizeof(cmdline),
			      "androidboot.force_normal_boot",
			      qcom_recovery_requested() ? "0" : "1");

	qcom_cmdline_set_arg(cmdline, sizeof(cmdline), "silent_boot.mode",
			      qcom_silent_boot_mode_cmdline_arg());

	return cmdline;
}
