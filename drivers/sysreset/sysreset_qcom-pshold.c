// SPDX-License-Identifier: GPL-2.0+
/*
 * Qualcomm PSHOLD reset driver
 *
 * Copyright (c) 2024 Sartura Ltd.
 *
 * Author: Robert Marko <robert.marko@sartura.hr>
 * Based on the Linux msm-poweroff driver.
 *
 */

#include <dm.h>
#include <sysreset.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <power/pmic.h>

#define PON_PBS_RESET_TYPE_SHUTDOWN		0x04
#define PON_PBS_RESET_TYPE_HARD			0x07

#define PON_PBS_RESET_CTL2_S2_RESET_EN_BMSK		0x80

struct qcom_pshold_pon_data {
	const char *pon_compatible;
	u32 pbs_base;
	u8 sw_ctl_offset;
	u8 reset_ctl2_offset;
};

static const struct qcom_pshold_pon_data qcom_pshold_pon_data_table[] = {
	{
		.pon_compatible = "qcom,pmk8350-pon",
		.pbs_base = 0x800,
		.sw_ctl_offset = 0x52,
		.reset_ctl2_offset = 0x53,
	},
};

struct qcom_pshold_priv {
	phys_addr_t base;
	bool have_pon;
	struct udevice *pmic;
	const struct qcom_pshold_pon_data *pon_data;
};

static int qcom_pshold_pon_cfg(struct qcom_pshold_priv *priv, u8 reset_type)
{
	u32 sw_ctl = priv->pon_data->pbs_base + priv->pon_data->sw_ctl_offset;
	u32 reset_ctl2 = priv->pon_data->pbs_base +
			  priv->pon_data->reset_ctl2_offset;
	int ret;

	ret = pmic_reg_write(priv->pmic, reset_ctl2, 0x0);
	if (ret)
		return ret;

	udelay(100);

	ret = pmic_reg_write(priv->pmic, sw_ctl, reset_type);
	if (ret)
		return ret;

	udelay(300);

	ret = pmic_reg_write(priv->pmic, reset_ctl2,
			     PON_PBS_RESET_CTL2_S2_RESET_EN_BMSK);
	if (ret)
		return ret;

	udelay(100);

	return 0;
}

static int qcom_pshold_request(struct udevice *dev, enum sysreset_t type)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);

	if (priv->have_pon) {
		u8 reset_type;
		int ret;

		switch (type) {
		case SYSRESET_COLD:
		case SYSRESET_POWER:
			reset_type = PON_PBS_RESET_TYPE_HARD;
			break;
		case SYSRESET_POWER_OFF:
			reset_type = PON_PBS_RESET_TYPE_SHUTDOWN;
			break;
		default:
			return -EPROTONOSUPPORT;
		}

		ret = qcom_pshold_pon_cfg(priv, reset_type);
		if (ret)
			debug("%s: failed to configure PON reset type: %d\n",
			      dev->name, ret);
	}

	writel(0, priv->base);
	mdelay(10000);

	return 0;
}

static struct sysreset_ops qcom_pshold_ops = {
	.request = qcom_pshold_request,
};

static ofnode qcom_pshold_find_pon(struct qcom_pshold_priv *priv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qcom_pshold_pon_data_table); i++) {
		ofnode node;

		node = ofnode_by_compatible(ofnode_null(),
					    qcom_pshold_pon_data_table[i].pon_compatible);
		if (ofnode_valid(node)) {
			priv->pon_data = &qcom_pshold_pon_data_table[i];
			return node;
		}
	}

	return ofnode_null();
}

static int qcom_pshold_probe(struct udevice *dev)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);
	ofnode pon_node, pmic_node;
	int ret;

	priv->have_pon = false;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	pon_node = qcom_pshold_find_pon(priv);
	if (!ofnode_valid(pon_node))
		return 0;

	pmic_node = ofnode_get_parent(pon_node);
	if (!ofnode_valid(pmic_node)) {
		debug("%s: failed to get PON's parent PMIC node\n", dev->name);
		return 0;
	}

	ret = uclass_get_device_by_ofnode(UCLASS_PMIC, pmic_node, &priv->pmic);
	if (ret) {
		debug("%s: failed to get PMIC device: %d\n", dev->name, ret);
		return 0;
	}

	priv->have_pon = true;

	return 0;
}

static const struct udevice_id qcom_pshold_ids[] = {
	{ .compatible = "qcom,pshold", },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(qcom_pshold) = {
	.name 		= "qcom_pshold",
	.id 		= UCLASS_SYSRESET,
	.of_match 	= qcom_pshold_ids,
	.probe 		= qcom_pshold_probe,
	.priv_auto	= sizeof(struct qcom_pshold_priv),
	.ops 		= &qcom_pshold_ops,
};
