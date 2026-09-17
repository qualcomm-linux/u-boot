/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __QCLIB_H__
#define __QCLIB_H__

#include <linux/bitops.h>
#include <linux/types.h>

#define MAGIC_KEY			"QCLIB_CB"
#define MAX_ENTRIES			0xF
#define IF_TABLE_VERSION		0x1
#define QCSDI				"qcsdi"
#define QCLIB_LOG_BUFFER		"qclib_log_buffer"

/* interface_table.global_attributes bits */
#define QCLIB_GA_ENABLE_UART_LOGGING	BIT(0)

/**
 * struct interface_table_entry - Meta data for blobs in QCLIB interface
 * @entry_name:	Name of the data blob (e.g., "dcb_settings").
 * @address:	Address of the data blob.
 * @size:	Size of the data blob.
 * @attributes:	Attributes for the blob (e.g., save to storage).
 */
struct interface_table_entry {
	char entry_name[24];
	u64 address;
	u32 size;
	u32 attributes;
};

/**
 * struct interface_table - QCLIB Interface table header
 * @magic_key:		Magic key for validation ("QCLIB_CB").
 * @version:		Interface table version.
 * @num_entries:	Number of valid entries.
 * @max_entries:	Maximum allowable entries.
 * @global_attributes:	Flags for global attributes (e.g., SDI path).
 * @reserved1:		Reserved for future use.
 * @reserved2:		Reserved for future use.
 * @if_table_entries:	Array of interface table entries.
 */
struct interface_table {
	char magic_key[8];
	u32 version;
	u32 num_entries;
	u32 max_entries;
	u32 global_attributes;
	u32 reserved1;
	u32 reserved2;
	struct interface_table_entry if_table_entries[MAX_ENTRIES];
};

/* Exported by fit-handler.c */
int qcom_spl_get_fit_img_entry_point(void *fit, int node, u64 *entry_point);
int qcom_spl_get_iftbl_entry_by_name(struct interface_table *if_tbl,
				     char *name,
				     struct interface_table_entry *entry);

/* QCSDI address accessors, populated post-QCLIB, consumed by fit-handler.c */
u64 qclib_get_qcsdi_address(void);
void qclib_set_qcsdi_address(u64 address);

/* qclib_log_buffer address accessors, populated post-QCLIB */
u64 qclib_get_log_buffer_entry(void);
void qclib_set_log_buffer_entry(u64 address);

/*
 * __weak SoC extension points. A board's spl-<soc>.c may provide a strong
 * override for any of these; the defaults in qclib.c are all no-ops.
 */
bool qcom_spl_soc_check_dload_mode(void);
int qcom_spl_soc_pre_qclib_routine(void);
int qcom_spl_soc_qclib_override(struct interface_table *table, const void *fit,
				int images_node);
int qcom_spl_soc_post_qclib_routine(struct interface_table *if_tbl);

int qclib_populate_interface_table(struct interface_table *if_tbl,
				   const void *fit, int images_node);
int qcom_spl_invoke_qclib(void);

#endif /* __QCLIB_H__ */
