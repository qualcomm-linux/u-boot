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
#include <power/pmic.h>

#define PON_PBS_RESET_TYPE_WARM		0x01
#define PON_PBS_RESET_TYPE_HARD		0x07

/*
 * Reset State of PON_PBS_PS_HOLD_RESET_CTL2 register
 */
#define PON_PBS_PS_HOLD_RESET_CTL2_S2_RESET_EN_BMSK	0x80

struct qcom_pshold_priv {
	phys_addr_t base;

	struct udevice *pmic;
	u32 pbs_base;
	bool have_pon;

	u32 pon_pbs_pshold_sw_ctl_offset;
	u32 pon_pbs_pshold_reset_ctl2_offset;
};

static int qcom_pshold_pon_cfg(struct qcom_pshold_priv *priv, u8 reset_type)
{
	int ret;

	ret = pmic_reg_write(priv->pmic,
			     priv->pbs_base +
			     priv->pon_pbs_pshold_reset_ctl2_offset,
			     0x00);
	if (ret)
		return ret;

	udelay(100);

	ret = pmic_reg_write(priv->pmic,
			     priv->pbs_base + priv->pon_pbs_pshold_sw_ctl_offset,
			     reset_type);
	if (ret)
		return ret;

	udelay(300);

	ret = pmic_reg_write(priv->pmic,
			     priv->pbs_base + priv->pon_pbs_pshold_reset_ctl2_offset,
			     PON_PBS_PS_HOLD_RESET_CTL2_S2_RESET_EN_BMSK);
	if (ret)
		return ret;

	udelay(100);

	return 0;
}

static int qcom_pshold_read_pon_offset(struct udevice *dev, struct qcom_pshold_priv *priv)
{
	int ret;

	ret = dev_read_u32(dev,
			   "qcom,pbs-pshold-sw-ctl",
			   &priv->pon_pbs_pshold_sw_ctl_offset);
	if (ret)
		return ret;

	ret = dev_read_u32(dev,
			   "qcom,pbs-pshold-reset-ctl2",
			   &priv->pon_pbs_pshold_reset_ctl2_offset);
	if (ret)
		return ret;

	return 0;
}

static int qcom_pshold_request(struct udevice *dev, enum sysreset_t type)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);

	if (priv->have_pon) {
		u8 reset_type = (type == SYSRESET_WARM) ?
				 PON_PBS_RESET_TYPE_WARM :
				 PON_PBS_RESET_TYPE_HARD;
		int ret;

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

static int qcom_pshold_probe(struct udevice *dev)
{
	struct qcom_pshold_priv *priv = dev_get_priv(dev);
	struct ofnode_phandle_args args;
	ofnode pmic_node;
	int index, ret;

	priv->have_pon = false;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ret = dev_read_phandle_with_args(dev, "qcom,pon", NULL, 0, 0, &args);
	if (ret)
		return 0;

	index = ofnode_stringlist_search(args.node, "reg-names", "pbs");
	if (index < 0) {
		debug("%s: referenced PON node has no 'pbs' reg\n", dev->name);
		return 0;
	}

	ret = ofnode_read_u32_index(args.node, "reg", index, &priv->pbs_base);
	if (ret) {
		debug("%s: failed to read PON 'pbs' reg: %d\n", dev->name, ret);
		return 0;
	}

	ret = qcom_pshold_read_pon_offset(dev, priv);
	if (ret) {
		debug("%s: failed to read PON offset configs: %d\n", dev->name, ret);
		return 0;
	}

	pmic_node = ofnode_get_parent(args.node);
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
	.name       = "qcom_pshold",
	.id         = UCLASS_SYSRESET,
	.of_match   = qcom_pshold_ids,
	.probe      = qcom_pshold_probe,
	.priv_auto  = sizeof(struct qcom_pshold_priv),
	.ops        = &qcom_pshold_ops,
};
