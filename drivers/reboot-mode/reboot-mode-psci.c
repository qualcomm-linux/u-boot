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
				    uint count)
{
	u32 reset_type = magic[0];
	u32 cookie = 0;

	if (count < 1 || count > REBOOT_MODE_MAX_MAGIC)
		return -EINVAL;

	/* psci_system_reset2() takes a 32-bit cookie; reject cookie_hi != 0 */
	if (count == REBOOT_MODE_MAX_MAGIC && magic[1]) {
		dev_err(dev, "64-bit reset cookie is not supported\n");
		return -EINVAL;
	}

	/* The cookie is the last cell */
	if (count >= 2)
		cookie = magic[count - 1];

	if (psci_features(ARM_PSCI_1_1_FN64_SYSTEM_RESET2) !=
	    ARM_PSCI_RET_SUCCESS) {
		dev_err(dev, "SYSTEM_RESET2 is not supported by firmware\n");
		return -EPROTONOSUPPORT;
	}

	/* Does not return */
	psci_system_reset2(reset_type, cookie);
}

static const struct reboot_mode_ops psci_reboot_mode_ops = {
	.trigger = psci_reboot_mode_trigger,
};

U_BOOT_DRIVER(psci_reboot_mode) = {
	.name = "reboot-mode-psci",
	.id = UCLASS_REBOOT_MODE,
	.ops = &psci_reboot_mode_ops,
};
