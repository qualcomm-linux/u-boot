/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

OUTPUT_FORMAT("default")
ENTRY(CONFIG_PLATFORM_ELFENTRY)

PHDRS {
	ptype PT_LOAD FLAGS(0x7);
}

SECTIONS {
	. = CONFIG_PLATFORM_ELFENTRY;
	.data : {
		*(.data*)
		. = ALIGN(0x1000);
	} :ptype
}
