// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Sean Anderson <sean.anderson@seco.com>
 */

#include <i2c_eeprom.h>
#include <linker_lists.h>
#include <misc.h>
#include <nvmem.h>
#include <rtc.h>
#include <dm/device_compat.h>
#include <dm/ofnode.h>
#include <dm/read.h>
#include <dm/uclass.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>

/* Maximum supported NVMEM cell size */
#define MAX_NVMEM_CELL_SIZE sizeof(u32)  /* 4 bytes */

/**
 * nvmem_cell_read_raw() - Read raw bytes from NVMEM cell without bit field extraction
 * @cell: NVMEM cell to read from
 * @buf: Buffer to store read data
 * @size: Size of buffer
 *
 * This is an internal helper that reads raw bytes from hardware without applying
 * bit field extraction. Used by both nvmem_cell_read() and nvmem_cell_write().
 * Caller must validate buffer size before calling this function.
 *
 * Return: Number of bytes read on success, negative error code on failure
 */
static int nvmem_cell_read_raw(struct nvmem_cell *cell, void *buf, size_t size)
{
	int ret;

	memset(buf, 0, size);

	switch (cell->nvmem->driver->id) {
	case UCLASS_I2C_EEPROM:
		ret = i2c_eeprom_read(cell->nvmem, cell->offset, buf, cell->size);
		break;
	case UCLASS_MISC:
		ret = misc_read(cell->nvmem, cell->offset, buf, cell->size);
		if (ret < 0)
			return ret;
		if (ret != cell->size)
			return -EIO;
		ret = 0;
		break;
	case UCLASS_RTC:
		ret = dm_rtc_read(cell->nvmem, cell->offset, buf, cell->size);
		break;
	default:
		return -ENOSYS;
	}

	if (ret)
		return ret;

	return cell->size;
}

int nvmem_cell_read(struct nvmem_cell *cell, void *buf, size_t size)
{
	int ret, bytes_needed;
	u32 value;

	dev_dbg(cell->nvmem, "%s: off=%u size=%zu\n", __func__, cell->offset, size);

	if (cell->nbits) {
		if (size != MAX_NVMEM_CELL_SIZE) {
			dev_dbg(cell->nvmem, "bit field requires buffer size %d, got %zu\n",
				MAX_NVMEM_CELL_SIZE, size);
			return -EINVAL;
		}

		bytes_needed = DIV_ROUND_UP(cell->nbits + cell->bit_offset, BITS_PER_BYTE);
		if (bytes_needed > cell->size || bytes_needed > MAX_NVMEM_CELL_SIZE) {
			dev_dbg(cell->nvmem, "bit field requires %d bytes, cell size %zu\n",
				bytes_needed, cell->size);
			return -EINVAL;
		}
	} else {
		if (size != cell->size) {
			dev_dbg(cell->nvmem, "buffer size %zu must match cell size %zu\n",
				size, cell->size);
			return -EINVAL;
		}
	}

	ret = nvmem_cell_read_raw(cell, buf, size);
	if (ret < 0)
		return ret;

	if (cell->nbits) {
		value = le32_to_cpu(*((__le32 *)buf));
		value >>= cell->bit_offset;
		value &= GENMASK(cell->nbits - 1, 0);
		*(u32 *)buf = value;
	}

	return 0;
}

int nvmem_cell_write(struct nvmem_cell *cell, const void *buf, size_t size)
{
	int ret, bytes_needed;
	u32 current, value, mask;

	dev_dbg(cell->nvmem, "%s: off=%u size=%zu\n", __func__, cell->offset, size);

	if (cell->nbits) {
		if (size != MAX_NVMEM_CELL_SIZE) {
			dev_dbg(cell->nvmem, "bit field requires buffer size %d, got %zu\n",
				MAX_NVMEM_CELL_SIZE, size);
			return -EINVAL;
		}

		bytes_needed = DIV_ROUND_UP(cell->nbits + cell->bit_offset, BITS_PER_BYTE);
		if (bytes_needed > cell->size || bytes_needed > MAX_NVMEM_CELL_SIZE) {
			dev_dbg(cell->nvmem, "bit field requires %d bytes, cell size %zu\n",
				bytes_needed, cell->size);
			return -EINVAL;
		}

		ret = nvmem_cell_read_raw(cell, &current, sizeof(current));
		if (ret < 0)
			return ret;

		current = le32_to_cpu(*((__le32 *)&current));
		value = *(const u32 *)buf;
		value &= GENMASK(cell->nbits - 1, 0);
		value <<= cell->bit_offset;

		mask = GENMASK(cell->nbits - 1, 0) << cell->bit_offset;

		current = (current & ~mask) | value;
		buf = &current;
	} else {
		if (size != cell->size) {
			dev_dbg(cell->nvmem, "buffer size %zu must match cell size %zu\n",
				size, cell->size);
			return -EINVAL;
		}
	}

	switch (cell->nvmem->driver->id) {
	case UCLASS_I2C_EEPROM:
		ret = i2c_eeprom_write(cell->nvmem, cell->offset, buf, cell->size);
		break;
	case UCLASS_MISC:
		ret = misc_write(cell->nvmem, cell->offset, buf, cell->size);
		if (ret < 0)
			return ret;
		if (ret != cell->size)
			return -EIO;
		ret = 0;
		break;
	case UCLASS_RTC:
		ret = dm_rtc_write(cell->nvmem, cell->offset, buf, cell->size);
		break;
	default:
		return -ENOSYS;
	}

	if (ret)
		return ret;

	return 0;
}

/**
 * nvmem_get_device() - Get an nvmem device for a cell
 * @node: ofnode of the nvmem device
 * @cell: Cell to look up
 *
 * Try to find a nvmem-compatible device by going through the nvmem interfaces.
 *
 * Return:
 * * 0 on success
 * * -ENODEV if we didn't find anything
 * * A negative error if there was a problem looking up the device
 */
static int nvmem_get_device(ofnode node, struct nvmem_cell *cell)
{
	int i, ret;
	enum uclass_id ids[] = {
		UCLASS_I2C_EEPROM,
		UCLASS_MISC,
		UCLASS_RTC,
	};

	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		ret = uclass_get_device_by_ofnode(ids[i], node, &cell->nvmem);
		if (!ret)
			return 0;
		if (ret != -ENODEV && ret != -EPFNOSUPPORT)
			return ret;
	}

	return -ENODEV;
}

int nvmem_cell_get_by_index(struct udevice *dev, int index,
			    struct nvmem_cell *cell)
{
	fdt_addr_t offset;
	fdt_size_t size = FDT_SIZE_T_NONE;
	int ret;
	struct ofnode_phandle_args args;
	ofnode par;

	dev_dbg(dev, "%s: index=%d\n", __func__, index);

	ret = dev_read_phandle_with_args(dev, "nvmem-cells", NULL, 0, index,
					 &args);
	if (ret)
		return ret;

	par = ofnode_get_parent(args.node);
	if (ofnode_device_is_compatible(par, "fixed-layout"))
		par = ofnode_get_parent(par);

	ret = nvmem_get_device(par, cell);
	if (ret)
		return ret;

	offset = ofnode_get_addr_size_index_notrans(args.node, 0, &size);
	if (offset == FDT_ADDR_T_NONE || size == FDT_SIZE_T_NONE) {
		dev_dbg(cell->nvmem, "missing address or size for %s\n",
			ofnode_get_name(args.node));
		return -EINVAL;
	}

	cell->offset = offset;
	cell->size = size;

	ret = ofnode_read_u32_index(args.node, "bits", 0, &cell->bit_offset);
	if (ret) {
		cell->bit_offset = 0;
		cell->nbits = 0;
	} else {
		ret = ofnode_read_u32_index(args.node, "bits", 1, &cell->nbits);
		if (ret)
			return -EINVAL;

		if (cell->bit_offset + cell->nbits > cell->size * 8)
			return -EINVAL;
	}

	return 0;
}

int nvmem_cell_get_by_name(struct udevice *dev, const char *name,
			   struct nvmem_cell *cell)
{
	int index;

	dev_dbg(dev, "%s, name=%s\n", __func__, name);

	index = dev_read_stringlist_search(dev, "nvmem-cell-names", name);
	if (index < 0)
		return index;

	return nvmem_cell_get_by_index(dev, index, cell);
}
