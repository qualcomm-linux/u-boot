/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
PHDRS {
	ptype PT_LOAD FLAGS(0x7);
}

ENTRY(_entry)

SECTIONS {
	. = CONFIG_PLATFORM_ELFENTRY;
	_entry = . ;
	data : {
		*(.data)
		. = ALIGN(4);
	} :ptype
}
