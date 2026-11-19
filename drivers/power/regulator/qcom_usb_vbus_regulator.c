// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Linaro Limited
 */
#define pr_fmt(fmt) "qcom_usb_vbus: " fmt

#include <bitfield.h>
#include <errno.h>
#include <dm.h>
#include <fdtdec.h>
#include <log.h>
#include <asm/gpio.h>
#include <linux/bitops.h>
#include <linux/printk.h>
#include <power/pmic.h>
#include <power/regulator.h>

enum pm8x50b_vbus {
	PM8150B,
	PM8550B,
	PM4125,
};

#define OTG_EN				BIT(0)

#define OTG_EN_SRC_CFG			BIT(1)

/* PM4125 uses a "VBOOST" block instead of the OTG block */
#define PM4125_VBOOST_EN_SRC_CFG	BIT(0)
#define PM4125_VBOOST_SEL_MASK		GENMASK(1, 0)

static const int pm4125_vboost_uV[] = { 4250000, 4500000, 4750000, 5000000 };

struct qcom_otg_regs {
	u32 otg_cmd;
	u32 otg_cfg;
	u32 en_src_cfg;
	u32 vsel_reg;	/* 0 if the voltage is not selectable */
};
struct qcom_usb_vbus_priv {
	phys_addr_t base;
	struct qcom_otg_regs *regs;
};

static const struct qcom_otg_regs qcom_otg[] = {
	[PM8150B] = {
		.otg_cmd = 0x40,
		.otg_cfg = 0x53,
		.en_src_cfg = OTG_EN_SRC_CFG,
	},
	[PM8550B] = {
		.otg_cmd = 0x50,
		.otg_cfg = 0x56,
		.en_src_cfg = OTG_EN_SRC_CFG,
	},
	[PM4125] = {
		.otg_cmd = 0x50,
		.otg_cfg = 0x56,
		.en_src_cfg = PM4125_VBOOST_EN_SRC_CFG,
		.vsel_reg = 0x52,
	},
};

static int qcom_usb_vbus_regulator_of_to_plat(struct udevice *dev)
{
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	return 0;
}

static int qcom_usb_vbus_regulator_get_enable(struct udevice *dev)
{
	const struct qcom_otg_regs *regs = &qcom_otg[dev_get_driver_data(dev)];
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);
	int otg_en_reg = priv->base + regs->otg_cmd;
	int ret;

	ret = pmic_reg_read(dev->parent, otg_en_reg);
	if (ret < 0)
		log_err("failed to read usb vbus: %d\n", ret);
	else
		ret &= OTG_EN;

	return ret;
}

static int qcom_usb_vbus_regulator_set_enable(struct udevice *dev, bool enable)
{
	const struct qcom_otg_regs *regs = &qcom_otg[dev_get_driver_data(dev)];
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);
	int otg_en_reg = priv->base + regs->otg_cmd;
	int ret;

	if (enable) {
		ret = pmic_clrsetbits(dev->parent, otg_en_reg, 0, OTG_EN);
		if (ret < 0) {
			log_err("error enabling: %d\n", ret);
			return ret;
		}
	} else {
		ret = pmic_clrsetbits(dev->parent, otg_en_reg, OTG_EN, 0);
		if (ret < 0) {
			log_err("error disabling: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int qcom_usb_vbus_regulator_get_value(struct udevice *dev)
{
	const struct qcom_otg_regs *regs = &qcom_otg[dev_get_driver_data(dev)];
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);
	int ret;

	if (!regs->vsel_reg)
		return -ENOSYS;

	ret = pmic_reg_read(dev->parent, priv->base + regs->vsel_reg);
	if (ret < 0)
		return ret;

	return pm4125_vboost_uV[ret & PM4125_VBOOST_SEL_MASK];
}

static int qcom_usb_vbus_regulator_set_value(struct udevice *dev, int uV)
{
	const struct qcom_otg_regs *regs = &qcom_otg[dev_get_driver_data(dev)];
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);
	int i;

	if (!regs->vsel_reg)
		return -ENOSYS;

	for (i = 0; i < ARRAY_SIZE(pm4125_vboost_uV); i++) {
		if (pm4125_vboost_uV[i] == uV)
			return pmic_clrsetbits(dev->parent,
					       priv->base + regs->vsel_reg,
					       PM4125_VBOOST_SEL_MASK, i);
	}

	return -EINVAL;
}

static int qcom_usb_vbus_regulator_probe(struct udevice *dev)
{
	const struct qcom_otg_regs *regs = &qcom_otg[dev_get_driver_data(dev)];
	struct qcom_usb_vbus_priv *priv = dev_get_priv(dev);
	int otg_cfg_reg = priv->base + regs->otg_cfg;
	int ret;

	/* Disable HW logic for VBUS enable */
	ret = pmic_clrsetbits(dev->parent, otg_cfg_reg, regs->en_src_cfg, 0);
	if (ret < 0) {
		log_err("error setting EN_SRC_CFG: %d\n", ret);
		return ret;
	}

	if (regs->vsel_reg) {
		struct dm_regulator_uclass_plat *uc_pdata =
			dev_get_uclass_plat(dev);
		int i;

		/* Highest supported voltage inside the DT constraints */
		for (i = ARRAY_SIZE(pm4125_vboost_uV) - 1; i >= 0; i--) {
			if (pm4125_vboost_uV[i] >= uc_pdata->min_uV &&
			    pm4125_vboost_uV[i] <= uc_pdata->max_uV)
				break;
		}
		if (i < 0) {
			log_err("no voltage in %d-%d uV range\n",
				uc_pdata->min_uV, uc_pdata->max_uV);
			return -EINVAL;
		}

		ret = qcom_usb_vbus_regulator_set_value(dev, pm4125_vboost_uV[i]);
		if (ret < 0) {
			log_err("error setting %d uV: %d\n",
				pm4125_vboost_uV[i], ret);
			return ret;
		}
	}

	return 0;
}

static const struct dm_regulator_ops qcom_usb_vbus_regulator_ops = {
	.get_enable = qcom_usb_vbus_regulator_get_enable,
	.set_enable = qcom_usb_vbus_regulator_set_enable,
	.get_value = qcom_usb_vbus_regulator_get_value,
	.set_value = qcom_usb_vbus_regulator_set_value,
};

static const struct udevice_id qcom_usb_vbus_regulator_ids[] = {
	{ .compatible = "qcom,pm4125-vbus-reg", .data = PM4125 },
	{ .compatible = "qcom,pm8150b-vbus-reg", .data = PM8150B },
	{ .compatible = "qcom,pm8550b-vbus-reg", .data = PM8550B },
	{ },
};

U_BOOT_DRIVER(qcom_usb_vbus_regulator) = {
	.name = "qcom-usb-vbus-regulator",
	.id = UCLASS_REGULATOR,
	.of_match = qcom_usb_vbus_regulator_ids,
	.of_to_plat = qcom_usb_vbus_regulator_of_to_plat,
	.ops = &qcom_usb_vbus_regulator_ops,
	.probe = qcom_usb_vbus_regulator_probe,
	.priv_auto = sizeof(struct qcom_usb_vbus_priv),
};
