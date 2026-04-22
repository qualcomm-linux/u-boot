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

/* TrustZone SMC definitions */
#define TZ_SYSCALL_CREATE_SMC_ID(o, s, f) \
	((u32)((((o) & 0x3f) << 24) | (((s) & 0xff) << 8) | ((f) & 0xff)))

#define TZ_OWNER_SIP				2
#define TZ_SVC_BOOT				1
#define TZ_SVC_INFO				6
#define TZ_BOOT_CMD_KVM_MILESTONE		0x21
#define TZ_INFO_IS_SVC_AVAILABLE_CMD		0x01

#define TZ_CONFIGURE_MILESTONE_SERVICE_ID \
	TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP, TZ_SVC_BOOT, TZ_BOOT_CMD_KVM_MILESTONE)
#define TZ_CONFIGURE_MILESTONE_SERVICE_PARAM_ID		0x23

#define TZ_INFO_IS_SVC_AVAILABLE_ID \
	TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP, TZ_SVC_INFO, TZ_INFO_IS_SVC_AVAILABLE_CMD)
#define TZ_INFO_IS_SVC_AVAILABLE_ID_PARAM_ID		0x1

/* Hypervisor boot types */
#define QCOM_HYP_BOOT_TYPE_GUNYAH		0
#define QCOM_HYP_BOOT_TYPE_KVM			1

/* TCR_EL2 bit field definitions */
#define TCR_T0SZ_MASK				0x1FUL
#define TCR_PS_MASK				(0x7UL << 32)
#define TCR_PS_SHIFT				16
#define TCR_SH_ORGN_IRGN_MASK			0x3F00UL

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
