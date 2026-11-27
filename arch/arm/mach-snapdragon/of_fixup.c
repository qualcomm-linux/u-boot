// SPDX-License-Identifier: GPL-2.0+
/*
 * OF_LIVE devicetree fixup.
 *
 * This file implements runtime fixups for Qualcomm DT to improve
 * compatibility with U-Boot. This includes adjusting the USB nodes
 * to only use USB high-speed if SSPHY driver is not available.
 *
 * We use OF_LIVE for this rather than early FDT fixup for a couple
 * of reasons: it has a much nicer API, is most likely more efficient,
 * and our changes are only applied to U-Boot. This allows us to use a
 * DT designed for Linux, run U-Boot with a modified version, and then
 * boot Linux with the original FDT.
 *
 * Copyright (c) 2024 Linaro Ltd.
 *   Author: Casey Connolly <casey.connolly@linaro.org>
 */

#define pr_fmt(fmt) "of_fixup: " fmt

#include <dt-bindings/input/linux-event-codes.h>
#include <dm/of_access.h>
#include <dm/of.h>
#include <dm/device.h>
#include <dm/lists.h>
#include <event.h>
#include <fdt_support.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linker_lists.h>
#include <log.h>
#include <stdlib.h>
#include <tee/optee.h>
#include <time.h>
#include <soc/qcom/smem.h>
#include <soc/qcom/socinfo.h>

/**
 * find_ssphy_node() - Find the super-speed PHY node referenced by DWC3
 * @dwc3: DWC3 device node
 *
 * Returns: Pointer to SS-PHY node if found, NULL otherwise
 */
static struct device_node *find_ssphy_node(struct device_node *dwc3)
{
	const __be32 *phandles;
	const char *phy_name;
	int len, i, ret;

	phandles = of_get_property(dwc3, "phys", &len);
	if (!phandles)
		return NULL;

	len /= sizeof(*phandles);

	/* Iterate through PHY phandles to find the SS-PHY */
	for (i = 0; i < len; i++) {
		ret = of_property_read_string_index(dwc3, "phy-names", i, &phy_name);
		if (ret)
			continue;

		/* Check if this is the super-speed PHY */
		if (!strncmp("usb3-phy", phy_name, strlen("usb3-phy")) ||
		    !strncmp("usb3_phy", phy_name, strlen("usb3_phy"))) {
			return of_find_node_by_phandle(NULL, be32_to_cpu(phandles[i]));
		}
	}

	return NULL;
}

/**
 * has_driver_for_node() - Check if any PHY driver can bind to this node
 * @np: Device node to check
 *
 * Returns: true if a PHY driver with matching compatible string exists, false otherwise
 */
static bool has_driver_for_node(struct device_node *np)
{
	struct driver *driver = ll_entry_start(struct driver, driver);
	const int n_ents = ll_entry_count(struct driver, driver);
	const char *compat_list, *compat;
	int compat_length, i;
	struct driver *entry;

	if (!np)
		return false;

	/* Get compatible strings from the node */
	compat_list = of_get_property(np, "compatible", &compat_length);
	if (!compat_list)
		return false;

	/* Check each compatible string against PHY drivers only */
	for (i = 0; i < compat_length; i += strlen(compat) + 1) {
		compat = compat_list + i;

		/* Iterate through all registered drivers */
		for (entry = driver; entry != driver + n_ents; entry++) {
			const struct udevice_id *of_match = entry->of_match;

			/* Skip non-PHY drivers to improve performance */
			if (entry->id != UCLASS_PHY)
				continue;

			if (!of_match)
				continue;

			while (of_match->compatible) {
				if (!strcmp(of_match->compatible, compat)) {
					debug("Found PHY driver '%s' for SS-PHY compatible '%s'\n",
					      entry->name, compat);
					return true;
				}
				of_match++;
			}
		}
	}

	return false;
}

static int fixup_qcom_dwc3(struct device_node *root, struct device_node *glue_np, bool flat)
{
	struct device_node *dwc3, *ssphy_np;
	int ret, len, hsphy_idx = 1;
	const __be32 *phandles;
	const char *second_phy_name;

	/* New DT flattens the glue and controller into a single node. */
	if (flat) {
		dwc3 = glue_np;
		debug("%s uses flat DT\n", glue_np->name);
	} else {
		/* Find the DWC3 node itself */
		dwc3 = of_find_compatible_node(glue_np, NULL, "snps,dwc3");
		if (!dwc3) {
			log_err("Failed to find dwc3 node\n");
			return -ENOENT;
		}
	}

	debug("Checking USB configuration for %s\n", dwc3->name);

	phandles = of_get_property(dwc3, "phys", &len);
	len /= sizeof(*phandles);
	if (len == 1) {
		debug("Only one phy, not a superspeed controller\n");
		return 0;
	}

	/* Figure out if the superspeed phy is present */
	ret = of_property_read_string_index(dwc3, "phy-names", 1, &second_phy_name);
	if (ret == -ENODATA) {
		debug("Only one phy, not a super-speed controller\n");
		return 0;
	} else if (ret) {
		log_err("Failed to read second phy name: %d\n", ret);
		return ret;
	}

	/* Find the super-speed PHY node and check if a driver is available */
	ssphy_np = find_ssphy_node(dwc3);
	if (ssphy_np && has_driver_for_node(ssphy_np)) {
		debug("Skipping USB fixup for %s (SS-PHY driver available)\n",
		      dwc3->name);
		return 0;
	}

	/* No driver available - apply the fixup */
	debug("Applying USB high-speed fixup to %s\n", dwc3->name);

	/* Tell the glue driver to configure the wrapper for high-speed only operation */
	ret = of_write_prop(dwc3, "qcom,select-utmi-as-pipe-clk", 0, NULL);
	if (ret) {
		log_err("Failed to add property 'qcom,select-utmi-as-pipe-clk': %d\n", ret);
		return ret;
	}

	/*
	 * Determine which phy is the superspeed phy by checking the name of the second phy
	 * since it is typically the superspeed one.
	 */
	if (!strncmp("usb3-phy", second_phy_name, strlen("usb3-phy")))
		hsphy_idx = 0;

	/* Overwrite the "phys" property to only contain the high-speed phy */
	ret = of_write_prop(dwc3, "phys", sizeof(*phandles), phandles + hsphy_idx);
	if (ret) {
		log_err("Failed to overwrite 'phys' property: %d\n", ret);
		return ret;
	}

	/* Overwrite "phy-names" to only contain a single entry */
	ret = of_write_prop(dwc3, "phy-names", strlen("usb2-phy") + 1, "usb2-phy");
	if (ret) {
		log_err("Failed to overwrite 'phy-names' property: %d\n", ret);
		return ret;
	}

	ret = of_write_prop(dwc3, "maximum-speed", strlen("high-speed") + 1, "high-speed");
	if (ret) {
		log_err("Failed to set 'maximum-speed' property: %d\n", ret);
		return ret;
	}

	return 0;
}

static void fixup_usb_nodes(struct device_node *root)
{
	struct device_node *glue_np = root, *tmp;
	int ret;
	bool flat;

	while (true) {
		flat = false;
		/* First check for the old DT format with glue node then the new flattened format */
		tmp = of_find_compatible_node(glue_np, NULL, "qcom,dwc3");
		if (!tmp) {
			tmp = of_find_compatible_node(glue_np, NULL, "qcom,snps-dwc3");
			flat = !!tmp;
		}
		if (!tmp)
			break;
		glue_np = tmp;

		if (!of_device_is_available(glue_np))
			continue;
		ret = fixup_qcom_dwc3(root, glue_np, flat);
		if (ret)
			log_warning("Failed to fixup node %s: %d\n", glue_np->name, ret);
	}
}

static void add_optee_node(struct device_node *root)
{
	struct device_node *fw = NULL, *optee = NULL;
	int ret;

	fw = of_find_node_by_path("/firmware");
	if (!fw) {
		log_err("Failed to find /firmware node\n");
		return;
	}

	ret = of_add_subnode(fw, "optee", strlen("optee") + 1, &optee);
	if (ret) {
		log_err("Failed to add 'optee' subnode: %d\n", ret);
		return;
	}

	ret = of_write_prop(optee, "compatible", strlen("linaro,optee-tz") + 1,
			    "linaro,optee-tz");
	if (ret) {
		log_err("Failed to add optee 'compatible' property: %d\n", ret);
		return;
	}

	ret = of_write_prop(optee, "method", strlen("smc") + 1, "smc");
	if (ret) {
		log_err("Failed to add optee 'method' property: %d\n", ret);
		return;
	}
}

#define time_call(func, ...) \
	do { \
		u64 start = timer_get_us(); \
		func(__VA_ARGS__); \
		debug(#func " took %lluus\n", timer_get_us() - start); \
	} while (0)

static int qcom_of_fixup_nodes(void * __maybe_unused ctx, struct event *event)
{
	struct device_node *root = event->data.of_live_built.root;

	time_call(fixup_usb_nodes, root);

	if (CONFIG_IS_ENABLED(OPTEE) && is_optee_smc_api())
		time_call(add_optee_node, root);

	return 0;
}

EVENT_SPY_FULL(EVT_OF_LIVE_BUILT, qcom_of_fixup_nodes);

#define QCOM_MAX_CPU_CLUSTERS	8

/**
 * qcom_get_disabled_cpus() - Read the per-cluster disabled CPU masks
 * @masks: Array to fill, one bitmask per CPU cluster (bit n = core n)
 * @max: Number of entries in @masks
 *
 * The boot firmware publishes, in the socinfo SMEM item (format >= 14),
 * an array with one u32 per CPU cluster where a set bit means the
 * corresponding core is fused off on this SKU.
 *
 * Return: number of clusters filled in, or negative error code
 */
static int qcom_get_disabled_cpus(u32 *masks, unsigned int max)
{
	struct socinfo *info;
	size_t size;
	u32 num, offset, i;
	const __le32 *array;

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID, &size);
	if (IS_ERR_OR_NULL(info))
		return -ENODEV;

	if (size < offsetof(struct socinfo, ncluster_array_offset) +
		   sizeof(info->ncluster_array_offset) ||
	    le32_to_cpu(info->fmt) < SOCINFO_VERSION(0, 14))
		return -ENOENT;

	num = le32_to_cpu(info->num_clusters);
	offset = le32_to_cpu(info->ncluster_array_offset);
	if (!num || !offset || offset > size ||
	    num > (size - offset) / sizeof(*array))
		return -ENOENT;

	if (num > max) {
		log_warning("%u CPU clusters in socinfo, checking first %u\n",
			    num, max);
		num = max;
	}

	array = (const __le32 *)((const u8 *)info + offset);
	for (i = 0; i < num; i++)
		masks[i] = le32_to_cpu(array[i]);

	return num;
}

/**
 * fdt_renumber_subnodes() - Close the gap left by a deleted numbered node
 * @blob: FDT blob
 * @parent: Path of the parent node
 * @prefix: Name prefix of the numbered subnodes ("cluster" or "core")
 * @from: Index of the deleted subnode
 *
 * The Linux cpu-map parser walks clusterN and coreN nodes in order and
 * stops at the first missing index, so rename prefix(from + 1)... down
 * by one.
 */
static void fdt_renumber_subnodes(void *blob, const char *parent,
				  const char *prefix, int from)
{
	char path[64], name[16];
	int node, i, ret;

	for (i = from + 1; ; i++) {
		snprintf(path, sizeof(path), "%s/%s%d", parent, prefix, i);
		node = fdt_path_offset(blob, path);
		if (node < 0)
			break;

		snprintf(name, sizeof(name), "%s%d", prefix, i - 1);
		ret = fdt_set_name(blob, node, name);
		if (ret) {
			log_err("Failed to rename %s: %s\n", path,
				fdt_strerror(ret));
			break;
		}
	}
}

/**
 * fdt_remove_cpu() - Remove a fused-off CPU from the kernel devicetree
 * @blob: FDT blob
 * @cluster: Cluster index in /cpus/cpu-map
 * @core: Core index within the cluster
 *
 * Removes the cpu-map coreN entry, the CPU node it points to, and the
 * CPU's PSCI power domain under /psci, which nothing else references.
 *
 * Return: 0 on success, negative error code on failure
 */
static int fdt_remove_cpu(void *blob, int cluster, int core)
{
	char cluster_path[48], core_path[64];
	const char *cpu_name;
	const fdt32_t *pds;
	u32 cpu_phandle, pd_phandle = 0;
	int core_node, cpu_node, pd_node, parent, len, ret;

	snprintf(cluster_path, sizeof(cluster_path),
		 "/cpus/cpu-map/cluster%d", cluster);
	snprintf(core_path, sizeof(core_path), "%s/core%d",
		 cluster_path, core);

	core_node = fdt_path_offset(blob, core_path);
	if (core_node < 0)
		return core_node;

	cpu_phandle = fdt_getprop_u32_default_node(blob, core_node, 0, "cpu", 0);
	if (!cpu_phandle) {
		/* e.g. SMT cores described with threadN subnodes */
		log_warning("%s has no cpu phandle, not removing\n", core_path);
		return -ENOENT;
	}

	cpu_node = fdt_node_offset_by_phandle(blob, cpu_phandle);
	if (cpu_node < 0)
		return cpu_node;
	cpu_name = fdt_get_name(blob, cpu_node, NULL);

	/* Only drop the PSCI domain if it is a per-CPU domain under /psci */
	pds = fdt_getprop(blob, cpu_node, "power-domains", &len);
	if (pds && len >= (int)sizeof(*pds)) {
		pd_node = fdt_node_offset_by_phandle(blob,
						     fdt32_to_cpu(pds[0]));
		parent = pd_node >= 0 ? fdt_parent_offset(blob, pd_node) : -1;
		if (parent >= 0 &&
		    !strcmp(fdt_get_name(blob, parent, NULL), "psci") &&
		    !strncmp(fdt_get_name(blob, pd_node, NULL),
			     "power-domain-cpu", strlen("power-domain-cpu")))
			pd_phandle = fdt32_to_cpu(pds[0]);
	}

	printf("   Removing fused-off CPU %s (cluster %d core %d)\n",
	       cpu_name, cluster, core);

	/* Offsets change after each deletion, so look each node up again */
	ret = fdt_del_node(blob, fdt_path_offset(blob, core_path));
	if (ret)
		return ret;

	ret = fdt_del_node(blob, fdt_node_offset_by_phandle(blob, cpu_phandle));
	if (ret)
		return ret;

	if (pd_phandle) {
		ret = fdt_del_node(blob,
				   fdt_node_offset_by_phandle(blob, pd_phandle));
		if (ret)
			return ret;
	}

	fdt_renumber_subnodes(blob, cluster_path, "core", core);

	return 0;
}

/**
 * qcom_fixup_disabled_cpus() - Drop fused-off CPUs from the kernel DT
 * @blob: Kernel FDT blob
 *
 * Some SKUs share a die with a full-featured SoC but have CPU cores fused
 * off, e.g. IQ10 SKUs with 17 of Nord's 18 cores enabled. The kernel DT
 * describes every core, so Linux tries to bring the missing ones online
 * and fails. Remove them based on the socinfo disabled-CPU masks.
 *
 * Socinfo cluster/core indices are matched against /cpus/cpu-map
 * clusterN/coreM, which only holds where the two numberings agree, so
 * this is restricted to platforms where that has been checked.
 */
static void qcom_fixup_disabled_cpus(void *blob)
{
	u32 masks[QCOM_MAX_CPU_CLUSTERS] = { 0 };
	char cluster_path[48];
	int num, cluster, core, node;

	if (fdt_node_check_compatible(blob, 0, "qcom,nord"))
		return;

	num = qcom_get_disabled_cpus(masks, ARRAY_SIZE(masks));
	if (num <= 0)
		return;

	/*
	 * Go from the highest index down so the renumbering done after
	 * each removal never shifts an index that is still to be visited.
	 */
	for (cluster = num - 1; cluster >= 0; cluster--) {
		if (!masks[cluster])
			continue;

		for (core = 31; core >= 0; core--) {
			if (!(masks[cluster] & BIT(core)))
				continue;

			if (cluster == 0 && core == 0) {
				log_warning("Boot CPU reported disabled, ignoring\n");
				continue;
			}

			if (fdt_remove_cpu(blob, cluster, core))
				log_warning("Failed to remove CPU cluster %d core %d\n",
					    cluster, core);
		}

		/* Drop the cluster entirely if no cores are left */
		snprintf(cluster_path, sizeof(cluster_path),
			 "/cpus/cpu-map/cluster%d", cluster);
		node = fdt_path_offset(blob, cluster_path);
		if (node >= 0 && fdt_first_subnode(blob, node) < 0 &&
		    !fdt_del_node(blob, node))
			fdt_renumber_subnodes(blob, "/cpus/cpu-map", "cluster",
					      cluster);
	}
}

int ft_board_setup(void *blob, struct bd_info __maybe_unused *bd)
{
	struct device_node *uboot_parent_np, *uboot_node_np;
	int kernel_parent, ret;
	const char *name;
	const void *prop;
	int proplen;

	qcom_fixup_disabled_cpus(blob);

	/* Only apply fixup for QCS615, QCS6490, and QCS8300 platforms */
	if (!of_device_is_compatible(gd->of_root, "qcom,talos-evk", NULL, NULL) &&
	    !of_device_is_compatible(gd->of_root, "qcom,qcs615", NULL, NULL) &&
	    !of_device_is_compatible(gd->of_root, "qcom,qcs6490-rb3gen2", NULL, NULL) &&
	    !of_device_is_compatible(gd->of_root, "qcom,qcm6490", NULL, NULL) &&
	    !of_device_is_compatible(gd->of_root, "qcom,qcs8300", NULL, NULL)) {
		return 0;
	}

	/* Get U-Boot's reserved-memory node */
	uboot_parent_np = of_find_node_by_path("/reserved-memory");
	if (!uboot_parent_np)
		return 0;

	/* Find kernel's reserved-memory node */
	kernel_parent = fdt_path_offset(blob, "/reserved-memory");
	if (kernel_parent < 0) {
		debug("No reserved-memory node in kernel DT, skipping fixup\n");
		return 0;
	}

	/* Iterate through U-Boot's reserved-memory subnodes */
	for (uboot_node_np = uboot_parent_np->child; uboot_node_np; uboot_node_np = uboot_node_np->sibling) {
		name = uboot_node_np->name;
		if (!name) {
			log_warning("Failed to get name for reserved-memory subnode\n");
			continue;
		}

		/* Check if this node already exists in kernel DT by name */
		if (fdt_subnode_offset(blob, kernel_parent, name) >= 0) {
			debug("Reserved region '%s' already exists in kernel DT, skipping\n", name);
			continue;
		}

		/* Get the reg property */
		prop = of_get_property(uboot_node_np, "reg", &proplen);
		if (!prop || proplen <= 0) {
			log_warning("Reserved region '%s' has no valid reg property, skipping\n", name);
			continue;
		}

		/* Add this node to kernel DT */
		int new_node = fdt_add_subnode(blob, kernel_parent, name);

		if (new_node < 0) {
			log_err("Failed to add reserved region '%s': %d\n", name, new_node);
			continue;
		}

		/* Copy the reg property */
		ret = fdt_setprop(blob, new_node, "reg", prop, proplen);
		if (ret < 0) {
			log_err("Failed to set reg property for '%s': %d\n", name, ret);
			continue;
		}

		/* Copy no-map property if it exists */
		if (of_get_property(uboot_node_np, "no-map", NULL)) {
			ret = fdt_setprop_empty(blob, new_node, "no-map");
			if (ret < 0)
				log_warning("Failed to set no-map for '%s': %d\n", name, ret);
		}

		/* Copy compatible property if it exists */
		prop = of_get_property(uboot_node_np, "compatible", &proplen);
		if (prop && proplen > 0) {
			ret = fdt_setprop(blob, new_node, "compatible", prop, proplen);
			if (ret < 0)
				log_warning("Failed to set compatible for '%s': %d\n", name, ret);
		}

		debug("Added reserved region '%s' to kernel DT\n", name);
	}

	return 0;
}
