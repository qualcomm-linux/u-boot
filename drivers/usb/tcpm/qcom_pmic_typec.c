// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, Linaro Ltd. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <dm.h>
#include <log.h>
#include <time.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <power/pmic.h>
#include <power/regulator.h>
#include <usb/tcpm.h>

#define TYPEC_SNK_STATUS_REG		0x06
#define DETECTED_SNK_TYPE_MASK		GENMASK(6, 0)
#define SNK_RP_STD			BIT(3)
#define SNK_RP_1P5			BIT(2)
#define SNK_RP_3P0			BIT(1)

#define TYPEC_SRC_STATUS_REG		0x08
#define DETECTED_SRC_TYPE_MASK		GENMASK(4, 0)
#define SRC_RD_OPEN			BIT(3)
#define SRC_RD_RA_VCONN			BIT(2)
#define AUDIO_ACCESS_RA_RA		BIT(0)

#define TYPEC_SM_STATUS_REG		0x0a
#define TYPEC_SM_VBUS_VSAFE5V		BIT(5)
#define TYPEC_SM_VBUS_VSAFE0V		BIT(6)

#define TYPEC_MISC_STATUS_REG		0x0b
#define SNK_SRC_MODE			BIT(6)
#define TYPEC_VBUS_DETECT		BIT(5)
#define CC_ORIENTATION			BIT(1)
#define CC_ATTACHED			BIT(0)

#define TYPEC_MODE_CFG_REG		0x44
#define EN_TRY_SNK			BIT(4)
#define EN_SRC_ONLY			BIT(2)
#define EN_SNK_ONLY			BIT(1)
#define TYPEC_DISABLE_CMD		BIT(0)

#define TYPEC_VCONN_CONTROL_REG		0x46
#define VCONN_EN_ORIENTATION		BIT(2)
#define VCONN_EN_VALUE			BIT(1)
#define VCONN_EN_SRC			BIT(0)

#define TYPEC_EXIT_STATE_CFG_REG	0x50
#define SEL_SRC_UPPER_REF		BIT(2)
#define USE_TPD_FOR_EXITING_ATTACHSRC	BIT(1)

#define TYPEC_CURRSRC_CFG_REG		0x52
#define TYPEC_SRC_RP_SEL_330UA		BIT(1)
#define TYPEC_SRC_RP_SEL_180UA		BIT(0)
#define TYPEC_SRC_RP_SEL_80UA		0

/* CC debounce time after a mode change */
#define CC_DEBOUNCE_US			2000
#define VBUS_SETTLE_TIMEOUT_MS		250

struct qcom_pmic_typec_priv {
	struct udevice *pmic;
	struct udevice *vbus;
	phys_addr_t base;
	bool vbus_enabled;
	u32 last_misc;
	unsigned long debounce_end;
};

static int typec_read(struct udevice *dev, unsigned int reg)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);

	return pmic_reg_read(priv->pmic, priv->base + reg);
}

static int typec_write(struct udevice *dev, unsigned int reg, u8 val)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);

	return pmic_reg_write(priv->pmic, priv->base + reg, val);
}

static void typec_start_debounce(struct udevice *dev)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);

	priv->debounce_end = timer_get_us() + CC_DEBOUNCE_US;
}

static int qcom_pmic_typec_get_connector_node(struct udevice *dev,
					      ofnode *connector_node)
{
	*connector_node = dev_read_subnode(dev, "connector");
	if (!ofnode_valid(*connector_node)) {
		dev_err(dev, "'connector' node is not found\n");
		return -ENODEV;
	}

	return 0;
}

static int qcom_pmic_typec_init(struct udevice *dev)
{
	int ret;

	/* Configure VCONN for software control */
	ret = typec_read(dev, TYPEC_VCONN_CONTROL_REG);
	if (ret < 0)
		return ret;
	ret &= ~(VCONN_EN_SRC | VCONN_EN_VALUE);
	ret = typec_write(dev, TYPEC_VCONN_CONTROL_REG, ret | VCONN_EN_SRC);
	if (ret < 0)
		return ret;

	/* Set CC threshold to 1.6 Volts | tPDdebounce = 10-20ms */
	ret = typec_read(dev, TYPEC_EXIT_STATE_CFG_REG);
	if (ret < 0)
		return ret;

	return typec_write(dev, TYPEC_EXIT_STATE_CFG_REG,
			   ret | SEL_SRC_UPPER_REF |
			   USE_TPD_FOR_EXITING_ATTACHSRC);
}

static int qcom_pmic_typec_get_vbus(struct udevice *dev)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);
	int misc;

	if (priv->vbus_enabled)
		return 1;

	misc = typec_read(dev, TYPEC_MISC_STATUS_REG);
	if (misc < 0)
		return 0;

	return !!(misc & TYPEC_VBUS_DETECT);
}

static int qcom_pmic_typec_set_cc(struct udevice *dev, enum typec_cc_status cc)
{
	u8 currsrc;
	int ret;

	switch (cc) {
	case TYPEC_CC_OPEN:
	case TYPEC_CC_RP_DEF:
		currsrc = TYPEC_SRC_RP_SEL_80UA;
		break;
	case TYPEC_CC_RP_1_5:
		currsrc = TYPEC_SRC_RP_SEL_180UA;
		break;
	case TYPEC_CC_RP_3_0:
		currsrc = TYPEC_SRC_RP_SEL_330UA;
		break;
	case TYPEC_CC_RD:
		/* Sink: nothing to advertise */
		typec_start_debounce(dev);
		return 0;
	default:
		return -EINVAL;
	}

	ret = typec_write(dev, TYPEC_CURRSRC_CFG_REG, currsrc);
	if (ret < 0)
		return ret;

	typec_start_debounce(dev);

	return 0;
}

static int qcom_pmic_typec_get_cc(struct udevice *dev,
				  enum typec_cc_status *cc1,
				  enum typec_cc_status *cc2)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);
	enum typec_cc_status cc;
	int misc, val;

	misc = typec_read(dev, TYPEC_MISC_STATUS_REG);
	if (misc < 0)
		return misc;

	/* The PMIC is still debouncing a mode or Rp change */
	if (priv->debounce_end && timer_get_us() < priv->debounce_end)
		return -EBUSY;
	priv->debounce_end = 0;

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	if (!(misc & CC_ATTACHED))
		return 0;

	if (misc & SNK_SRC_MODE) {
		val = typec_read(dev, TYPEC_SRC_STATUS_REG);
		if (val < 0)
			return val;

		switch (val & DETECTED_SRC_TYPE_MASK) {
		case AUDIO_ACCESS_RA_RA:
			cc = TYPEC_CC_RA;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		case SRC_RD_OPEN:
			cc = TYPEC_CC_RD;
			break;
		case SRC_RD_RA_VCONN:
			cc = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		default:
			dev_warn(dev, "unexpected src status %.2x\n", val);
			cc = TYPEC_CC_RD;
			break;
		}
	} else {
		val = typec_read(dev, TYPEC_SNK_STATUS_REG);
		if (val < 0)
			return val;

		switch (val & DETECTED_SNK_TYPE_MASK) {
		case SNK_RP_STD:
			cc = TYPEC_CC_RP_DEF;
			break;
		case SNK_RP_1P5:
			cc = TYPEC_CC_RP_1_5;
			break;
		case SNK_RP_3P0:
			cc = TYPEC_CC_RP_3_0;
			break;
		default:
			dev_warn(dev, "unexpected snk status %.2x\n", val);
			cc = TYPEC_CC_RP_DEF;
			break;
		}
	}

	if (misc & CC_ORIENTATION)
		*cc2 = cc;
	else
		*cc1 = cc;

	return 0;
}

static int qcom_pmic_typec_set_polarity(struct udevice *dev,
					enum typec_cc_polarity polarity)
{
	/* The PMIC detects the orientation itself */
	return 0;
}

static int qcom_pmic_typec_set_vconn(struct udevice *dev, bool on)
{
	int misc, ctrl;
	u8 mask, val;

	misc = typec_read(dev, TYPEC_MISC_STATUS_REG);
	if (misc < 0)
		return misc;

	/* Set VCONN on the inversion of the active CC channel */
	if (on) {
		mask = VCONN_EN_ORIENTATION | VCONN_EN_VALUE;
		val = VCONN_EN_VALUE | VCONN_EN_SRC;
		if (!(misc & CC_ORIENTATION))
			val |= VCONN_EN_ORIENTATION;
	} else {
		mask = VCONN_EN_VALUE;
		val = 0;
	}

	ctrl = typec_read(dev, TYPEC_VCONN_CONTROL_REG);
	if (ctrl < 0)
		return ctrl;

	return typec_write(dev, TYPEC_VCONN_CONTROL_REG, (ctrl & ~mask) | val);
}

static int qcom_pmic_typec_set_vbus(struct udevice *dev, bool on, bool charge)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);
	unsigned long timeout;
	int val = on ? TYPEC_SM_VBUS_VSAFE5V : TYPEC_SM_VBUS_VSAFE0V;
	int ret;

	if (priv->vbus_enabled == on)
		return 0;

	ret = regulator_set_enable(priv->vbus, on);
	if (ret)
		return ret;
	priv->vbus_enabled = on;

	/* Wait for the transition to vSafe5V or vSafe0V */
	timeout = timer_get_us() + VBUS_SETTLE_TIMEOUT_MS * 1000;
	do {
		ret = typec_read(dev, TYPEC_SM_STATUS_REG);
		if (ret >= 0 && (ret & val))
			break;
		udelay(100);
	} while (timer_get_us() < timeout);

	if (ret < 0 || !(ret & val))
		dev_warn(dev, "vbus vsafe%dv fail\n", on ? 5 : 0);

	return 0;
}

static int qcom_pmic_typec_set_pd_rx(struct udevice *dev, bool on)
{
	return 0;
}

static int qcom_pmic_typec_set_roles(struct udevice *dev, bool attached,
				     enum typec_role role,
				     enum typec_data_role data)
{
	return 0;
}

static int qcom_pmic_typec_start_toggling(struct udevice *dev,
					  enum typec_port_type port_type,
					  enum typec_cc_status cc)
{
	u8 mode;
	int ret;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		mode = EN_SRC_ONLY;
		break;
	case TYPEC_PORT_SNK:
		mode = EN_SNK_ONLY;
		break;
	case TYPEC_PORT_DRP:
		mode = EN_TRY_SNK;
		break;
	default:
		return -EINVAL;
	}

	ret = qcom_pmic_typec_set_cc(dev, cc);
	if (ret)
		return ret;

	/* Force it to restart at least once */
	ret = typec_write(dev, TYPEC_MODE_CFG_REG, TYPEC_DISABLE_CMD);
	if (ret < 0)
		return ret;

	return typec_write(dev, TYPEC_MODE_CFG_REG, mode);
}

static int qcom_pmic_typec_pd_transmit(struct udevice *dev,
				       enum tcpm_transmit_type type,
				       const struct pd_message *msg,
				       unsigned int negotiated_rev)
{
	/* No PD PHY support */
	return -EOPNOTSUPP;
}

static void qcom_pmic_typec_poll_event(struct udevice *dev)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);
	u32 changed;
	int misc;

	misc = typec_read(dev, TYPEC_MISC_STATUS_REG);
	if (misc < 0)
		return;

	changed = misc ^ priv->last_misc;
	priv->last_misc = misc;

	/* A mode or Rp change was debouncing: report the result now */
	if (priv->debounce_end && timer_get_us() >= priv->debounce_end) {
		tcpm_cc_change(dev);
		return;
	}

	if (changed & (CC_ATTACHED | CC_ORIENTATION | SNK_SRC_MODE))
		tcpm_cc_change(dev);

	if (changed & TYPEC_VBUS_DETECT)
		tcpm_vbus_change(dev);
}

static int qcom_pmic_typec_probe(struct udevice *dev)
{
	struct qcom_pmic_typec_priv *priv = dev_get_priv(dev);
	ofnode connector, supply;
	int ret;

	priv->pmic = dev->parent;
	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	ret = qcom_pmic_typec_get_connector_node(dev, &connector);
	if (ret)
		return ret;

	/* vdd-vbus-supply is deprecated in favour of connector vbus-supply */
	supply = ofnode_parse_phandle(connector, "vbus-supply", 0);
	if (ofnode_valid(supply))
		ret = uclass_get_device_by_ofnode(UCLASS_REGULATOR, supply,
						  &priv->vbus);
	else
		ret = device_get_supply_regulator(dev, "vdd-vbus-supply",
						  &priv->vbus);
	if (ret) {
		dev_err(dev, "failed to get VBUS supply: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dm_tcpm_ops qcom_pmic_typec_ops = {
	.get_connector_node = qcom_pmic_typec_get_connector_node,
	.init = qcom_pmic_typec_init,
	.get_vbus = qcom_pmic_typec_get_vbus,
	.set_cc = qcom_pmic_typec_set_cc,
	.get_cc = qcom_pmic_typec_get_cc,
	.set_polarity = qcom_pmic_typec_set_polarity,
	.set_vconn = qcom_pmic_typec_set_vconn,
	.set_vbus = qcom_pmic_typec_set_vbus,
	.set_pd_rx = qcom_pmic_typec_set_pd_rx,
	.set_roles = qcom_pmic_typec_set_roles,
	.start_toggling = qcom_pmic_typec_start_toggling,
	.pd_transmit = qcom_pmic_typec_pd_transmit,
	.poll_event = qcom_pmic_typec_poll_event,
};

static const struct udevice_id qcom_pmic_typec_ids[] = {
	{ .compatible = "qcom,pmi632-typec" },
	{ }
};

U_BOOT_DRIVER(qcom_pmic_typec) = {
	.name		= "qcom_pmic_typec",
	.id		= UCLASS_TCPM,
	.of_match	= qcom_pmic_typec_ids,
	.probe		= qcom_pmic_typec_probe,
	.ops		= &qcom_pmic_typec_ops,
	.priv_auto	= sizeof(struct qcom_pmic_typec_priv),
};
