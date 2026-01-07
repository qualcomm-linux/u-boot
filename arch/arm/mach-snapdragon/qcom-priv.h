// SPDX-License-Identifier: GPL-2.0

#ifndef __QCOM_PRIV_H__
#define __QCOM_PRIV_H__

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

#if IS_ENABLED(CONFIG_EFI_LOADER)
/**
 * qcom_add_reserved_memory_to_efi() - Add Qualcomm SoC reserved memory to EFI
 *
 * This function detects the SoC type and adds the appropriate reserved
 * memory regions to the EFI memory map. Called from efi_add_known_memory().
 */
void qcom_add_reserved_memory_to_efi(void);
#endif /* CONFIG_EFI_LOADER */

#if IS_ENABLED(CONFIG_EFI_HAVE_CAPSULE_SUPPORT)
void qcom_configure_capsule_updates(void);
#else
void qcom_configure_capsule_updates(void) {}
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

#endif /* __QCOM_PRIV_H__ */
