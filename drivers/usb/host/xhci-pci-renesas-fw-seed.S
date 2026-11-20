/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Renesas uPD720201/uPD720202 xHCI controller firmware, linked in from a
 * file supplied at build time via CONFIG_USB_XHCI_PCI_RENESAS_FW_FILE.
 *
 * Copyright (c) Qualcomm Technologies, Inc. All rights reserved.
 */

.section .rodata.renesas_xhci_fw.init,"a"
.balign 16
.global __renesas_xhci_fw_begin
__renesas_xhci_fw_begin:
.incbin RENESAS_XHCI_FW_SEED_FILE
.global __renesas_xhci_fw_end
__renesas_xhci_fw_end:
.balign 16
