// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm SPMI SDAM NVMEM driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <dm.h>
#include <misc.h>
#include <dm/device_compat.h>
#include <dm/uclass.h>
#include <spmi/spmi.h>

#define PID_SHIFT 8
#define PID_MASK (0xFF << PID_SHIFT)
#define REG_MASK 0xFF
#define SDAM_SIZE 0x100

struct qcom_sdam_priv {
	u32 base;
	u32 size;
	u32 pmic_usid;
	struct udevice *spmi_dev;
};

/**
 * qcom_sdam_find_spmi_pmic() - Find SPMI controller and PMIC USID
 * @dev: SDAM device
 * @spmi_dev: Returns SPMI controller device
 * @pmic_usid: Returns PMIC USID for SPMI access
 *
 * Walks up the device tree to find the PMIC parent and SPMI controller.
 * Supports both direct SDAM under PMIC and virtual NVMEM under PON.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_sdam_find_spmi_pmic(struct udevice *dev,
				    struct udevice **spmi_dev,
				    u32 *pmic_usid)
{
	struct udevice *pmic_dev = dev->parent;
	int ret;

	if (!pmic_dev) {
		dev_err(dev, "No parent device found\n");
		return -ENODEV;
	}

	ret = dev_read_u32_index(pmic_dev, "reg", 0, pmic_usid);
	if (ret) {
		dev_err(dev, "Could not read PMIC USID: %d\n", ret);
		return ret;
	}

	*spmi_dev = pmic_dev->parent;
	if (!*spmi_dev || (*spmi_dev)->uclass->uc_drv->id != UCLASS_SPMI) {
		dev_err(dev, "Could not find SPMI controller\n");
		return -ENODEV;
	}

	dev_dbg(dev, "Found PMIC USID=%d, SPMI controller=%s\n",
		*pmic_usid, (*spmi_dev)->name);

	return 0;
}

/**
 * qcom_sdam_read() - Read data from SDAM/NVMEM region
 * @dev: MISC device (SDAM)
 * @offset: Offset within SDAM/NVMEM region
 * @buf: Buffer to read data into
 * @size: Number of bytes to read
 *
 * Uses the same SPMI register access pattern as pmic_qcom.c driver
 * for consistency and reliability. This function is called by the
 * NVMEM subsystem via misc_read().
 *
 * Return: number of bytes read on success, negative error code on failure
 */
static int qcom_sdam_read(struct udevice *dev, int offset,
			  void *buf, int size)
{
	struct qcom_sdam_priv *priv = dev_get_priv(dev);
	u8 *buffer = buf;
	int ret;

	if (offset + size > priv->size)
		return -EINVAL;

	for (size_t i = 0; i < size; i++) {
		u32 reg = priv->base + offset + i;

		ret = spmi_reg_read(priv->spmi_dev, priv->pmic_usid,
				    (reg & PID_MASK) >> PID_SHIFT,
				    reg & REG_MASK);
		if (ret < 0) {
			dev_err(dev, "SPMI read failed at 0x%x: %d\n", reg, ret);
			return ret;
		}
		buffer[i] = ret;

		dev_dbg(dev, "Read 0x%02x from 0x%x (PID=0x%02x REG=0x%02x)\n",
			buffer[i], reg, (reg & PID_MASK) >> PID_SHIFT, reg & REG_MASK);
	}

	return size;
}

/**
 * qcom_sdam_write() - Write data to SDAM/NVMEM region
 * @dev: MISC device (SDAM)
 * @offset: Offset within SDAM/NVMEM region
 * @buf: Buffer containing data to write
 * @size: Number of bytes to write
 *
 * Uses the same SPMI register access pattern as pmic_qcom.c driver
 * for consistency and reliability. This function is called by the
 * NVMEM subsystem via misc_write().
 *
 * Return: number of bytes written on success, negative error code on failure
 */
static int qcom_sdam_write(struct udevice *dev, int offset,
			   const void *buf, int size)
{
	struct qcom_sdam_priv *priv = dev_get_priv(dev);
	const u8 *buffer = buf;
	int ret;

	if (offset + size > priv->size)
		return -EINVAL;

	for (size_t i = 0; i < size; i++) {
		u32 reg = priv->base + offset + i;

		ret = spmi_reg_write(priv->spmi_dev, priv->pmic_usid,
				     (reg & PID_MASK) >> PID_SHIFT,
				     reg & REG_MASK,
				     buffer[i]);
		if (ret < 0) {
			dev_err(dev, "SPMI write failed at 0x%x: %d\n", reg, ret);
			return ret;
		}

		dev_dbg(dev, "Wrote 0x%02x to 0x%x (PID=0x%02x REG=0x%02x)\n",
			buffer[i], reg, (reg & PID_MASK) >> PID_SHIFT, reg & REG_MASK);
	}

	return size;
}

static const struct misc_ops qcom_sdam_ops = {
	.read = qcom_sdam_read,
	.write = qcom_sdam_write,
};

/**
 * qcom_sdam_probe() - Probe SDAM device and register as NVMEM provider
 * @dev: SDAM device
 *
 * Handles both real SDAM blocks and virtual NVMEM under PON blocks.
 * For virtual NVMEM, adds the parent PON base address to the offset.
 *
 * Return: 0 on success, negative error code on failure
 */
static int qcom_sdam_probe(struct udevice *dev)
{
	struct qcom_sdam_priv *priv = dev_get_priv(dev);
	fdt_addr_t base;
	int ret;

	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE) {
		dev_err(dev, "Could not read base address\n");
		return -EINVAL;
	}

	priv->base = base;
	priv->size = SDAM_SIZE;

	ret = qcom_sdam_find_spmi_pmic(dev, &priv->spmi_dev, &priv->pmic_usid);
	if (ret)
		return ret;

	dev_dbg(dev, "SDAM base=0x%x size=0x%x PMIC_USID=%d\n",
		priv->base, priv->size, priv->pmic_usid);

	return 0;
}

static const struct udevice_id qcom_sdam_ids[] = {
	{ .compatible = "qcom,spmi-sdam" },
	{ }
};

U_BOOT_DRIVER(qcom_spmi_sdam) = {
	.name = "qcom-spmi-sdam",
	.id = UCLASS_MISC,
	.of_match = qcom_sdam_ids,
	.probe = qcom_sdam_probe,
	.ops = &qcom_sdam_ops,
	.priv_auto = sizeof(struct qcom_sdam_priv),
};
