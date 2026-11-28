// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */

#define LOG_CATEGORY UCLASS_IOMMU

#include <dm.h>
#include <iommu.h>
#include <malloc.h>
#include <phys2bus.h>
#include <asm/io.h>

#if (CONFIG_IS_ENABLED(OF_CONTROL) && !CONFIG_IS_ENABLED(OF_PLATDATA))

#if CONFIG_IS_ENABLED(PCI)
/**
 * iommu_map_is_legacy() - Check for a 1-cell 'iommu-map' on a 2-cell IOMMU
 *
 * @map: Contents of the 'iommu-map' property
 * @count: Length of @map in cells
 *
 * Some device trees describe each entry with a single 'iommu-base' cell
 * even though the IOMMU expects two. Such maps do not follow the binding
 * but are too common to reject, and can be told apart because every entry
 * points at the same IOMMU and maps exactly one RID.
 *
 * Return: true if @map has to be parsed as 4-cell entries
 */
static bool iommu_map_is_legacy(const u32 *map, int count)
{
	u32 phandle = map[1];
	int i;

	if (count % 4)
		return false;

	for (i = 0; i < count; i += 4) {
		if (map[i + 1] != phandle || map[i + 3] != 1)
			return false;
	}

	return true;
}

static int dev_pci_iommu_enable(struct udevice *dev)
{
	struct udevice *parent = dev->parent;
	struct udevice *dev_iommu;
	const struct iommu_ops *ops;
	bool legacy_map = false;
	ofnode iommu_node;
	u32 *iommu_map;
	u32 iommu_map_mask, length, phandle, rid, rid_base, iommu_base;
	u32 cells;
	int i, count, len, ret;

	while (parent) {
		len = dev_read_size(parent, "iommu-map");
		if (len > 0)
			break;
		parent = parent->parent;
	}

	if (len <= 0)
		return 0;

	iommu_map = malloc(len);
	if (!iommu_map)
		return -ENOMEM;

	count = len / sizeof(u32);
	ret = dev_read_u32_array(parent, "iommu-map", iommu_map, count);
	if (ret < 0) {
		free(iommu_map);
		return 0;
	}

	iommu_map_mask = dev_read_u32_default(parent, "iommu-map-mask", ~0);
	rid = (dm_pci_get_bdf(dev) >> 8) & iommu_map_mask;

	/*
	 * An entry is <rid-base, &iommu, iommu-base ..., length>, where
	 * 'iommu-base' is '#iommu-cells' of the target IOMMU wide, so an
	 * entry spans 3 + '#iommu-cells' cells.
	 */
	i = 0;
	while (i < count) {
		if (count - i < 2)
			goto bad_len;

		rid_base = iommu_map[i];
		phandle = iommu_map[i + 1];

		iommu_node = ofnode_get_by_phandle(phandle);
		if (!ofnode_valid(iommu_node)) {
			log_err("%s: Bad phandle %u in 'iommu-map' of '%s'\n",
				__func__, phandle, parent->name);
			free(iommu_map);
			return -ENODEV;
		}

		cells = legacy_map ? 1 :
			ofnode_read_u32_default(iommu_node, "#iommu-cells", 1);
		if (cells > OF_MAX_PHANDLE_ARGS) {
			log_err("%s: Bad '#iommu-cells' on '%s': %u\n",
				__func__, ofnode_get_name(iommu_node), cells);
			free(iommu_map);
			return -EINVAL;
		}

		if (i == 0 && cells == 2 &&
		    iommu_map_is_legacy(iommu_map, count)) {
			log_warning("%s: '%s' has 1-cell 'iommu-map' entries targeting 2-cell '#iommu-cells', treating as 1-cell output\n",
				    __func__, parent->name);
			legacy_map = true;
			cells = 1;
		}

		if (count - i < 3 + cells)
			goto bad_len;

		iommu_base = iommu_map[i + 2];
		i += 3 + cells;
		length = iommu_map[i - 1];

		if (rid < rid_base || rid - rid_base >= length)
			continue;

		if (length > 1 && cells > 1) {
			log_err("%s: '%s' cannot handle %u-ID range with %u-cell output specifier\n",
				__func__, parent->name, length, cells);
			free(iommu_map);
			return -EINVAL;
		}

		ret = uclass_get_device_by_phandle_id(UCLASS_IOMMU, phandle,
						      &dev_iommu);
		if (ret) {
			debug("%s: uclass_get_device_by_ofnode failed: %d\n",
			      __func__, ret);
			free(iommu_map);
			return ret;
		}
		dev->iommu = dev_iommu;
		dev->pci_iommu_sid = iommu_base + (rid - rid_base);

		ops = device_get_ops(dev->iommu);
		if (ops && ops->connect) {
			ret = ops->connect(dev);
			if (ret) {
				log_err("%s: Failed to connect '%s' to IOMMU '%s': %d\n",
					__func__, dev->name, dev->iommu->name, ret);
				free(iommu_map);
				return ret;
			}
		}
		break;
	}

	free(iommu_map);
	return 0;

bad_len:
	log_err("%s: Truncated 'iommu-map' on '%s': %d cells\n",
		__func__, parent->name, count);
	free(iommu_map);
	return -EINVAL;
}
#endif

int dev_iommu_enable(struct udevice *dev)
{
	struct ofnode_phandle_args args;
	struct udevice *dev_iommu;
	const struct iommu_ops *ops;
	int i, count, ret = 0;

	if (dev_has_ofnode(dev)) {
		count = dev_count_phandle_with_args(dev, "iommus",
						    "#iommu-cells", 0);
		for (i = 0; i < count; i++) {
			ret = dev_read_phandle_with_args(dev, "iommus",
							 "#iommu-cells", 0, i,
							 &args);
			if (ret) {
				log_err("%s: Failed to parse 'iommus' property for '%s': %d\n",
					__func__, dev->name, ret);
				return ret;
			}

			ret = uclass_get_device_by_ofnode(UCLASS_IOMMU, args.node,
							  &dev_iommu);
			if (ret) {
				log_err("%s: Failed to find IOMMU device for '%s': %d\n",
					__func__, dev->name, ret);
				return ret;
			}
			dev->iommu = dev_iommu;

			if (dev->parent && dev->parent->iommu == dev_iommu)
				continue;

			ops = device_get_ops(dev->iommu);
			if (ops && ops->connect) {
				ret = ops->connect(dev);
				if (ret) {
					log_err("%s: Failed to connect '%s' to IOMMU '%s': %d\n",
						__func__, dev->name, dev->iommu->name, ret);
					return ret;
				}
			}
		}
	} else {
		count = -1;
	}

#if CONFIG_IS_ENABLED(PCI)
	if (count < 0 && device_is_on_pci_bus(dev))
		return dev_pci_iommu_enable(dev);
#endif

	return 0;
}
#endif

dma_addr_t dev_iommu_dma_map(struct udevice *dev, void *addr, size_t size)
{
	const struct iommu_ops *ops;

	if (dev->iommu) {
		ops = device_get_ops(dev->iommu);
		if (ops && ops->map)
			return ops->map(dev->iommu, addr, size);
	}

	return dev_phys_to_bus(dev, virt_to_phys(addr));
}

void dev_iommu_dma_unmap(struct udevice *dev, dma_addr_t addr, size_t size)
{
	const struct iommu_ops *ops;

	if (dev->iommu) {
		ops = device_get_ops(dev->iommu);
		if (ops && ops->unmap)
			ops->unmap(dev->iommu, addr, size);
	}
}

UCLASS_DRIVER(iommu) = {
	.id		= UCLASS_IOMMU,
	.name		= "iommu",
};
