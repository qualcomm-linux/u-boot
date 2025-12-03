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
#include <linux/errno.h>
#include <linker_lists.h>
#include <stdlib.h>
#include <tee/optee.h>
#include <time.h>
#include "qcom_fixup_handlers.h"

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

/* U-Boot only supports USB high-speed mode on Qualcomm platforms with DWC3
 * USB controllers. Rather than requiring source level DT changes, we fix up
 * DT here. This improves compatibility with upstream DT and simplifies the
 * porting process for new devices.
 */
static int fixup_qcom_dwc3(struct device_node *root, struct device_node *glue_np, bool flat)
{
	struct device_node *dwc3, *ssphy_np;
	int ret, len, hsphy_idx = 1;
	const __be32 *phandles;
	const char *second_phy_name;

	debug("Fixing up %s\n", glue_np->name);

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

	debug("Checking USB configuration for %s\n", glue_np->name);

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
		      glue_np->name);
		return 0;
	}

	/* No driver available - apply the fixup */
	debug("Applying USB high-speed fixup to %s\n", glue_np->name);

	/* Tell the glue driver to configure the wrapper for high-speed only operation */
	ret = of_write_prop(glue_np, "qcom,select-utmi-as-pipe-clk", 0, NULL);
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

/* Remove all references to the rpmhpd device */
static void fixup_power_domains(struct device_node *root)
{
	struct device_node *pd = NULL, *np = NULL;
	struct property *prop;
	const __be32 *val;

	/* All Qualcomm platforms name the rpm(h)pd "power-controller" */
	for_each_of_allnodes_from(root, pd) {
		if (pd->name && !strcmp("power-controller", pd->name))
			break;
	}

	/* Sanity check that this is indeed a power domain controller */
	if (!of_find_property(pd, "#power-domain-cells", NULL)) {
		log_err("Found power-controller but it doesn't have #power-domain-cells\n");
		return;
	}

	/* Remove all references to the power domain controller */
	for_each_of_allnodes_from(root, np) {
		if (!(prop = of_find_property(np, "power-domains", NULL)))
			continue;

		val = prop->value;
		if (val[0] == cpu_to_fdt32(pd->phandle))
			of_remove_property(np, prop);
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
		printf(#func " took %lluus\n", timer_get_us() - start); \
	} while (0)

static int qcom_of_fixup_nodes(void * __maybe_unused ctx, struct event *event)
{
	struct device_node *root = event->data.of_live_built.root;

	time_call(fixup_usb_nodes, root);
	time_call(fixup_power_domains, root);

	if (IS_ENABLED(CONFIG_OPTEE) && is_optee_smc_api())
		time_call(add_optee_node, root);

	return 0;
}

EVENT_SPY_FULL(EVT_OF_LIVE_BUILT, qcom_of_fixup_nodes);

/**
 * soc_specific_fixups() - Apply SoC-specific device tree fixups
 * @fdt: Pointer to the device tree
 *
 * This function applies SoC-specific fixups based on the device tree
 * compatible string. Each SoC can have its own fixup logic.
 */
static void soc_specific_fixups(struct fdt_header *fdt)
{
	int ret;

	/* QCS615-specific fixup: Disable MMC node */
	if (fdt_node_check_compatible(fdt, 0, "qcom,qcs615") == 0) {
		int path_offset;
		char prop_val[] = "disabled";

		path_offset = fdt_path_offset(fdt, "/soc@0/mmc@7c4000");
		if (path_offset >= 0) {
			ret = fixup_dt_node(fdt, path_offset, "status",
					    (void *)prop_val, SET_PROP_STRING);
			if (ret)
				log_err("Failed to disable MMC node for QCS615: %d\n", ret);
		}
	}
}

int ft_board_setup(void __maybe_unused *blob, struct bd_info __maybe_unused *bd)
{
	struct fdt_header *fdt = blob;

	/* Apply SoC-specific fixups */
	soc_specific_fixups(fdt);

	/* Call all common fixup handlers */
	boardinfo_fixup_handler(fdt);
	ddrinfo_fixup_handler(fdt);
	subsetparts_fixup_handler(fdt);

	return 0;
}

int fixup_dt_node(void *fdt_ptr, int node_offset,
		  const char *property_name,
		  void *property_value,
		  enum fdt_fixup_type type)
{
	int ret;

	if ((!fdt_ptr || node_offset < 0) ||
	    (!property_value && type != ADD_SUBNODE))
		return -1;

	switch (type) {
	case APPEND_PROP_U32:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3
				  + sizeof(u32)));
		ret = fdt_appendprop_u32(fdt_ptr, node_offset,
					 property_name,
					 *(u32 *)property_value);
		break;
	case APPEND_PROP_U64:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3
				  + sizeof(u64)));
		ret = fdt_appendprop_u64(fdt_ptr, node_offset,
					 property_name,
					 *(u64 *)property_value);
		break;
	case SET_PROP_U32:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3
				  + sizeof(u32)));
		ret = fdt_setprop_u32(fdt_ptr, node_offset,
				      property_name,
				      *(u32 *)property_value);
		break;
	case SET_PROP_U64:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3
				  + sizeof(u64)));
		ret = fdt_setprop_u64(fdt_ptr, node_offset,
				      property_name,
				      *(u64 *)property_value);
		break;
	case SET_PROP_STRING:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3
				  + strlen((char *)property_value)));
		ret = fdt_setprop_string(fdt_ptr, node_offset,
					 property_name,
					 (char *)property_value);
		break;
	case ADD_SUBNODE:
		fdt_set_totalsize(fdt_ptr,
				  (fdt_totalsize(fdt_ptr)
				  + sizeof(struct fdt_property)
				  + strlen(property_name) + 3));
		ret = fdt_add_subnode(fdt_ptr, node_offset,
				      property_name);
		break;
	default:
		ret = -1;
	}

	return ret;
}
