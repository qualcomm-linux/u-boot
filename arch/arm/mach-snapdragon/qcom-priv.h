// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

#include <soc/qcom/socinfo.h>
#include "rampart.h"

/**
 * enum qcom_boot_source - Track where we got loaded from.
 * Used for capsule update logic.
 *
 * @QCOM_BOOT_SOURCE_ANDROID: chainloaded (typically from ABL)
 * @QCOM_BOOT_SOURCE_XBL: flashed to the XBL or UEFI partition
 */
enum qcom_boot_source {
	QCOM_BOOT_SOURCE_ANDROID = 1,
	QCOM_BOOT_SOURCE_XBL,
};

extern enum qcom_boot_source qcom_boot_source;

int board_serial_num(u32 *serial_num_ptr);

/**
 * qcom_get_smem_device() - Get cached SMEM device
 *
 * Return: Pointer to SMEM device on success, NULL on failure
 */
struct udevice *qcom_get_smem_device(void);

/**
 * qcom_get_socinfo() - Get cached socinfo from SMEM
 *
 * Return: Pointer to socinfo structure on success, NULL on failure
 */
struct socinfo *qcom_get_socinfo(void);

/**
 * qcom_get_ram_partitions() - Get cached RAM partition table from SMEM
 *
 * Return: Pointer to RAM partition table on success, NULL on failure
 */
struct usable_ram_partition_table *qcom_get_ram_partitions(void);

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

#endif /* __QCOM_PRIV_H__ */
