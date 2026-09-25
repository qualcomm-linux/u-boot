/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

/* The boot ROM identifies XBL-sec by the segment type in p_flags. */
#define PF_SEGMENT_TYPE_XBL_SEC 5

/*
 * The XBL-sec segment address must match the address encoded by the supplied
 * XBL-sec image:
 *
 *   entry address  0x0c350000
 *   LOAD offset    0x00002000
 *   base address   0x0c34e000
 */
#define XBL_SEC_SEGMENT_ADDR 0x000000000c34e000

/* xbl_sec.elf is staged in the output directory by the board build rules. */
TARGET("binary")
INPUT("./xbl_sec.elf")

OUTPUT_FORMAT("default")

ENTRY(CONFIG_PLATFORM_ELFENTRY)

PHDRS {
	data PT_LOAD FLAGS(7);
	xbl_sec PT_LOAD FLAGS(5 | (PF_SEGMENT_TYPE_XBL_SEC << 24));
}

SECTIONS {
	. = XBL_SEC_SEGMENT_ADDR;
	.xbl_sec : {
		"./xbl_sec.elf"
	} :xbl_sec

	. = CONFIG_PLATFORM_ELFENTRY;
	.data : {
		*(.data*)
	} :data
}
