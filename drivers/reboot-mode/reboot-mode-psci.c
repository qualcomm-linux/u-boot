// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <reboot-mode/reboot-mode.h>
#include <asm/psci.h>
#include <asm/system.h>
#include <linux/errno.h>

static int psci_reboot_mode_trigger(struct udevice *dev, const u32 *magic,
				    int count)
{
	u32 reset_type = magic[0];
	u64 cookie = 0;
	int i;

	if (count < 1 || count > REBOOT_MODE_MAX_MAGIC)
		return -EINVAL;

	/*
	 * U-Boot's psci_system_reset2() takes only a 32-bit cookie, whereas
	 * the binding allows a 64-bit cookie in the 3-cell form (magic[1] is
	 * cookie_hi, magic[2] is cookie_lo). Reject a non-zero high half
	 * rather than silently truncating it.
	 */
	if (count == REBOOT_MODE_MAX_MAGIC && magic[1]) {
		dev_err(dev, "64-bit reset cookie is not supported\n");
		return -EINVAL;
	}

	for (i = 1; i < count; i++)
		cookie = (cookie << 32) | magic[i];

	if (psci_features(ARM_PSCI_1_1_FN64_SYSTEM_RESET2) !=
	    ARM_PSCI_RET_SUCCESS) {
		dev_err(dev, "SYSTEM_RESET2 is not supported by firmware\n");
		return -EPROTONOSUPPORT;
	}

	/* Does not return on success. */
	psci_system_reset2(reset_type, (u32)cookie);

	return -EINPROGRESS;
}

static const struct reboot_mode_ops psci_reboot_mode_ops = {
	.trigger = psci_reboot_mode_trigger,
};

U_BOOT_DRIVER(psci_reboot_mode) = {
	.name = "reboot-mode-psci",
	.id = UCLASS_REBOOT_MODE,
	.ops = &psci_reboot_mode_ops,
};
