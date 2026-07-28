// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm HW-Info aggregator driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Binds to a U-Boot-only DT node (compatible = "qcom,hwinfo") holding
 * phandles to the existing IMEM/TCSR syscon nodes plus per-SoC offsets.
 *
 * Example DT node (arch/arm/dts/<board>-u-boot.dtsi):
 *
 *	qcom_hwinfo: qcom-hwinfo {
 *		compatible = "qcom,hwinfo";
 *		imem = <&sram>;
 *		imem-boot-cookie-offset = <0x0>;
 *		tcsr = <&tcsr>;
 *		tcsr-hw-version-offset = <0x8000>;
 *	};
 */

#include <dm.h>
#include <log.h>
#include <regmap.h>
#include <syscon.h>
#include <dm/device_compat.h>
#include <linux/err.h>
#include <qcom_hwinfo.h>

struct qcom_hwinfo_priv {
	struct regmap *imem_regmap;
	u32 imem_cookie_off;
	struct regmap *tcsr_regmap;
	u32 tcsr_hwver_off;
};

int qcom_hwinfo_get_imem_cookie(struct udevice *dev,
				const struct qcom_hwinfo_imem_cookie **cookie)
{
	struct qcom_hwinfo_priv *priv = dev_get_priv(dev);
	const struct qcom_hwinfo_imem_cookie *c;
	void *base;

	if (!priv->imem_regmap)
		return -ENODEV;

	/* IMEM is plain mapped SRAM; safe to dereference directly. */
	base = regmap_get_range(priv->imem_regmap, 0);
	if (!base) {
		dev_err(dev, "Failed to get IMEM regmap range\n");
		return -ENODEV;
	}

	c = (const struct qcom_hwinfo_imem_cookie *)((u8 *)base + priv->imem_cookie_off);

	if (c->shared_imem_magic != QCOM_HWINFO_IMEM_MAGIC_NUM ||
	    c->shared_imem_version < QCOM_HWINFO_IMEM_VERSION_NUM) {
		dev_warn(dev, "Invalid IMEM cookie magic=0x%x version=%u\n",
			 c->shared_imem_magic, c->shared_imem_version);
		return -EINVAL;
	}

	*cookie = c;

	return 0;
}

int qcom_hwinfo_get_storage_type(struct udevice *dev, u32 *storage_type)
{
	const struct qcom_hwinfo_imem_cookie *cookie;
	int ret;

	ret = qcom_hwinfo_get_imem_cookie(dev, &cookie);
	if (ret) {
		dev_warn(dev, "Failed to get IMEM cookie: %d, defaulting to UFS\n", ret);
		*storage_type = QCOM_HWINFO_STORAGE_UFS;
		return 0;
	}

	switch (cookie->boot_device_type) {
	case QCOM_HWINFO_UFS_FLASH:
		*storage_type = QCOM_HWINFO_STORAGE_UFS;
		break;
	case QCOM_HWINFO_MMC_FLASH:
	case QCOM_HWINFO_SDC_FLASH:
		*storage_type = QCOM_HWINFO_STORAGE_EMMC;
		break;
	case QCOM_HWINFO_NAND_FLASH:
		*storage_type = QCOM_HWINFO_STORAGE_NAND;
		break;
	default:
		dev_warn(dev, "Unknown boot device type: %u, defaulting to UFS\n",
			 cookie->boot_device_type);
		*storage_type = QCOM_HWINFO_STORAGE_UFS;
		break;
	}

	return 0;
}

int qcom_hwinfo_get_tcsr_hw_version(struct udevice *dev, u32 *hw_version)
{
	struct qcom_hwinfo_priv *priv = dev_get_priv(dev);
	int ret;

	if (!priv->tcsr_regmap)
		return -ENODEV;

	ret = regmap_read(priv->tcsr_regmap, priv->tcsr_hwver_off, hw_version);
	if (ret) {
		dev_err(dev, "Failed to read TCSR HW version: %d\n", ret);
		return ret;
	}

	return 0;
}

static int qcom_hwinfo_of_to_plat(struct udevice *dev)
{
	struct qcom_hwinfo_priv *priv = dev_get_priv(dev);

	priv->imem_regmap = syscon_regmap_lookup_by_phandle(dev, "imem");
	if (IS_ERR(priv->imem_regmap)) {
		dev_warn(dev, "Unable to find 'imem' regmap (%ld)\n",
			 PTR_ERR(priv->imem_regmap));
		priv->imem_regmap = NULL;
	} else {
		priv->imem_cookie_off =
			dev_read_u32_default(dev, "imem-boot-cookie-offset", 0);
	}

	priv->tcsr_regmap = syscon_regmap_lookup_by_phandle(dev, "tcsr");
	if (IS_ERR(priv->tcsr_regmap)) {
		dev_warn(dev, "Unable to find 'tcsr' regmap (%ld)\n",
			 PTR_ERR(priv->tcsr_regmap));
		priv->tcsr_regmap = NULL;
	} else {
		priv->tcsr_hwver_off =
			dev_read_u32_default(dev, "tcsr-hw-version-offset", 0);
	}

	if (!priv->imem_regmap && !priv->tcsr_regmap) {
		dev_err(dev, "Neither 'imem' nor 'tcsr' regmap found\n");
		return -EINVAL;
	}

	return 0;
}

static const struct udevice_id qcom_hwinfo_ids[] = {
	{ .compatible = "qcom,hwinfo" },
	{ }
};

U_BOOT_DRIVER(qcom_hwinfo) = {
	.name = "qcom_hwinfo",
	.id = UCLASS_MISC,
	.of_match = qcom_hwinfo_ids,
	.of_to_plat = qcom_hwinfo_of_to_plat,
	.priv_auto = sizeof(struct qcom_hwinfo_priv),
};