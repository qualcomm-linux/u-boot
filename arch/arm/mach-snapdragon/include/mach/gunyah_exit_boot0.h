/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Early Gunyah Hypervisor Exit
 *
 * Exit Gunyah hypervisor and switch to EL2 during early boot. This must
 * happen before EL register configuration in start.S so that U-Boot can
 * run properly at EL2.
 */

#include <linux/arm-smccc.h>

/* TrustZone SMC IDs for hypervisor configuration */
#define TZ_EL2_SWITCH_SMC_ID			0x02000121
#define TZ_EL2_SWITCH_PARAM_ID			0x00000023
#define TZ_EL2_SWITCH_PARAM2_EXIT_GUNYAH	0x1

	/* Save FDT address before we modify x0 */
	mov	x9, x0

	/* Only perform hypervisor switch if we're at EL1 */
	mrs	x0, CurrentEL
	cmp	x0, #(1 << 2)
	bne	1f

	/* Switch to EL2 (exit Gunyah) */
	ldr	w0, =TZ_EL2_SWITCH_SMC_ID
	ldr	w1, =TZ_EL2_SWITCH_PARAM_ID
	mov	w2, wzr
	mov	w3, wzr
	ldr	w4, =TZ_EL2_SWITCH_PARAM2_EXIT_GUNYAH
	smc	#0

	/* Restore FDT address */
	mov	x0, x9
1:
	b	reset
