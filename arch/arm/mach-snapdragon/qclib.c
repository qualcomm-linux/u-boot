// SPDX-License-Identifier: GPL-2.0
/*
 * QCLIB interface-table protocol: generic, SoC-agnostic orchestration of the
 * hand-off to the QCLIB firmware image, plus __weak extension points a
 * per-board spl-<soc>.c may override to populate SoC-specific table entries.
 */

#include <cpu_func.h>
#include <errno.h>
#include <fdt_support.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <mach/qclib.h>

static u64 g_qcsdi_address __section(".data");
static u64 g_log_buffer_entry __section(".data");

u64 qclib_get_qcsdi_address(void)
{
	return g_qcsdi_address;
}

void qclib_set_qcsdi_address(u64 addr)
{
	g_qcsdi_address = addr;
}

u64 qclib_get_log_buffer_entry(void)
{
	return g_log_buffer_entry;
}

void qclib_set_log_buffer_entry(u64 address)
{
	g_log_buffer_entry = address;
}

bool __weak qcom_spl_soc_check_dload_mode(void)
{
	return false;
}

int __weak qcom_spl_soc_pre_qclib_routine(void)
{
	return 0;
}

int __weak qcom_spl_soc_qclib_override(struct interface_table *table,
				       const void *fit, int images_node)
{
	return 0;
}

int __weak qcom_spl_soc_post_qclib_routine(struct interface_table *if_tbl)
{
	return 0;
}

int __weak qclib_populate_interface_table(struct interface_table *if_tbl,
					  const void *fit, int images_node)
{
	struct interface_table_entry *log_buffer_entry;

	memset(if_tbl, 0, sizeof(struct interface_table));
	memcpy(if_tbl->magic_key, MAGIC_KEY, strlen(MAGIC_KEY));
	if_tbl->version = IF_TABLE_VERSION;
	if_tbl->num_entries = 0;
	if_tbl->max_entries = MAX_ENTRIES;

	/*
	 * Reserve the "qclib_log_buffer" slot — common to every SoC.
	 * QCLIB fills in the address itself; SPL just allocates the entry.
	 */
	log_buffer_entry = &if_tbl->if_table_entries[if_tbl->num_entries];
	memcpy(log_buffer_entry->entry_name, QCLIB_LOG_BUFFER,
	       strlen(QCLIB_LOG_BUFFER));
	log_buffer_entry->address = 0;
	log_buffer_entry->size = 0;
	log_buffer_entry->attributes = 0;
	if_tbl->num_entries++;

	return qcom_spl_soc_qclib_override(if_tbl, fit, images_node);
}

static int qclib_find_fit_nodes(const void *fit, int *images_node,
				int *qclib_node)
{
	*images_node = fdt_subnode_offset(fit, 0, "images");
	if (*images_node < 0) {
		pr_err("Failed to find images node in FIT\n");
		return -ENOENT;
	}

	*qclib_node = fdt_subnode_offset(fit, *images_node, "qclib_1");
	if (*qclib_node < 0) {
		pr_err("Failed to find qclib_1 node in FIT\n");
		return -ENOENT;
	}

	return 0;
}

int qcom_spl_invoke_qclib(void)
{
	int ret;
	int images_node;
	int qclib_node;
	const void *fit;
	struct interface_table if_tbl;
	u64 entry_point;

	/* Get FIT image from SPL load address */
	fit = (const void *)CONFIG_SPL_LOAD_FIT_ADDRESS;

	pr_debug("QCLIB invoke: fit=%p\n", fit);

	ret = qclib_find_fit_nodes(fit, &images_node, &qclib_node);
	if (ret)
		return ret;

	ret = qcom_spl_soc_pre_qclib_routine();
	if (ret)
		return ret;

	ret = qclib_populate_interface_table(&if_tbl, fit, images_node);
	if (ret)
		return ret;

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, qclib_node, &entry_point);
	if (ret) {
		pr_err("Failed to get qcom-lib-1 entry point (%d)\n", ret);
		return ret;
	}

	pr_info("Jumping to qcom-lib-1 at 0x%llx\n", entry_point);

	/*
	 * QCLIB is a separately-linked firmware image, not an AAPCS64
	 * callee - a plain C function-pointer call risks the compiler
	 * caching a live value in a callee-saved register across the jump.
	 * Pin the args/target to fixed registers and clobber everything the
	 * ABI doesn't guarantee QCLIB will preserve.
	 */
	{
		register void *x0 asm("x0") = &if_tbl;
		register void *x1 asm("x1") = NULL;
		register u64 x8 asm("x8") = entry_point;

		asm volatile ("blr x8"
			      : "+r" (x0)
			      : "r" (x1), "r" (x8)
			      : "x2", "x3", "x4", "x5", "x6", "x7", "x9", "x10",
				"x11", "x12", "x13", "x14", "x15", "x16", "x17",
				"x19", "x20", "x21", "x22", "x23", "x24", "x25",
				"x26", "x27", "x28", "x30", "cc", "memory");
	}

	return qcom_spl_soc_post_qclib_routine(&if_tbl);
}
