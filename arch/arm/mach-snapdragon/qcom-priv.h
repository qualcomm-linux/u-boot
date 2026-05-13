// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

#include <stdbool.h>

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

/*
 * enum qcom_memmap_source - Track where we got the memory map from.
 * used for debugging and validation.
 */
enum qcom_memmap_source {
	QCOM_MEMMAP_SOURCE_INTERNAL_FDT = 1,
	QCOM_MEMMAP_SOURCE_EXTERNAL_FDT,
	QCOM_MEMMAP_SOURCE_SMEM,
};

/* Set by qcom_parse_memory() */
extern enum qcom_memmap_source qcom_memmap_source;

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
/*
 * Capsule Update GUIDs for FIT capsules
 * Each board has a unique GUID to prevent cross-board flashing
 */

/* QCS615 FIT Capsule GUID: 9fd379d2-670e-4bb3-86a1-40497e6e17b0 */
#define QCOM_QCS615_FIT_CAPSULE_GUID \
	EFI_GUID(0x9fd379d2, 0x670e, 0x4bb3, 0x86, 0xa1, \
		 0x40, 0x49, 0x7e, 0x6e, 0x17, 0xb0)

/* QCS6490 FIT Capsule GUID: 6f25bfd2-a165-468b-980f-ac51a0a45c52 */
#define QCOM_QCS6490_FIT_CAPSULE_GUID \
	EFI_GUID(0x6f25bfd2, 0xa165, 0x468b, 0x98, 0x0f, \
		 0xac, 0x51, 0xa0, 0xa4, 0x5c, 0x52)

/* Lemans FIT Capsule GUID: 78462415-6133-431c-9fae-48f2bafd5c71 */
#define QCOM_LEMANS_FIT_CAPSULE_GUID \
	EFI_GUID(0x78462415, 0x6133, 0x431c, 0x9f, 0xae, \
		 0x48, 0xf2, 0xba, 0xfd, 0x5c, 0x71)

/* Common name for FIT capsule (same for all boards) */
#define QCOM_FIT_CAPSULE_NAME u"QCOM_FIT_CAPSULE"

void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

int qcom_parse_memory(const void *fdt, bool fdt_is_internal);

#endif /* __QCOM_PRIV_H__ */
