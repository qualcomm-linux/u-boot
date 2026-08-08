/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c), Vaisala Oyj
 */

#ifndef REBOOT_MODE_REBOOT_MODE_H__
#define REBOOT_MODE_REBOOT_MODE_H__

#include <asm/types.h>
#include <dm/device.h>

/*
 * Maximum number of 32-bit magic cells a reboot mode may carry.
 */
#define REBOOT_MODE_MAX_MAGIC	3

struct reboot_mode_mode {
	const char *mode_name;
	u32 magic[REBOOT_MODE_MAX_MAGIC];
	u8 count;
};

struct reboot_mode_uclass_platdata {
	struct reboot_mode_mode *modes;
	u8 count;
	const char *env_variable;
};

struct reboot_mode_ops {
	/**
	 * get() - get the current reboot mode value
	 *
	 * Returns the current value from the reboot mode backing store.
	 *
	 * @dev:	Device to read from
	 * @rebootmode:	Address to save the current reboot mode value
	 */
	int (*get)(struct udevice *dev, u32 *rebootmode);

	/**
	 * set() - set a reboot mode value
	 *
	 * Sets the value in the reboot mode backing store.
	 *
	 * @dev:	Device to read from
	 * @rebootmode:	New reboot mode value to store
	 */
	int (*set)(struct udevice *dev, u32 rebootmode);

	/**
	 * trigger() - reset the system into the given mode now
	 *
	 * Unlike get()/set(), which persist a value for the next boot to
	 * consume, trigger() performs the reset immediately. On success it
	 * does not return. Drivers with a backing store (nvmem, gpio, rtc)
	 * leave this NULL.
	 *
	 * @dev:	Device to trigger
	 * @magic:	Array of @count 32-bit magic cells describing the mode
	 * @count:	Number of valid cells in @magic (1 to
	 *		REBOOT_MODE_MAX_MAGIC)
	 * Return: does not return on success; -ve on error
	 */
	int (*trigger)(struct udevice *dev, const u32 *magic, int count);
};

/* Access the operations for a reboot mode device */
#define reboot_mode_get_ops(dev) ((struct reboot_mode_ops *)(dev)->driver->ops)

/**
 * dm_reboot_mode_update() - Update the reboot mode env variable.
 *
 * @dev:	Device to read from
 * Return: 0 if OK, -ve on error
 */
int dm_reboot_mode_update(struct udevice *dev);

/**
 * reboot_mode_request() - reset the system into a named mode
 *
 * Search every UCLASS_REBOOT_MODE device for a mode named @name and, if the
 * owning device supports triggering, reset into it. On success this does not
 * return.
 *
 * @name:	Mode name (without the device tree "mode-" prefix)
 * Return: does not return on success; -ENOENT if no device declares a
 *	   triggerable mode called @name; other -ve on error
 */
int reboot_mode_request(const char *name);

/**
 * reboot_mode_list() - print all triggerable reboot modes
 *
 * Enumerate every UCLASS_REBOOT_MODE device and print the name of each mode
 * that can be triggered (i.e. whose device implements the trigger op). Modes
 * that only exist to be read from a backing store on boot are not listed.
 *
 * Return: 0 if OK, -ve on error
 */
int reboot_mode_list(void);

#endif /* REBOOT_MODE_REBOOT_MODE_H__ */
