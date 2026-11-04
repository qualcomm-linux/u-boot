// SPDX-License-Identifier: GPL-2.0-only
/*
 * QCS6490 / RB3 Gen2 PCIe power/reset initialization.
 *
 * Drives the PCIe1 (RC1) downstream power-enable and reset lines for the
 * QPS615 PCIe switch and the Renesas uPD720201 xHCI controller directly
 * via the DM GPIO API, issuing a real assert -> hold -> deassert pulse
 * on every boot (see commit message for why a DT gpio-hog is not enough).
 *
 * Hardware topology (pcie1, RC1, x2 lanes):
 *   Kodiak PCIe RC1 -> QPS615 PCIe switch -> Renesas uPD720201 xHCI
 *
 * Power/reset lines:
 *   QPS615 switch:
 *     vdd_ntn_0p9/vdd_ntn_1p8 - regulator-fixed nodes, self-enabled via DT
 *     resx                    - PM8350C GPIO 1, active-low reset
 *
 *   Renesas uPD720201 xHCI (downstream of the switch):
 *     vreg_pcie0_3p3  - PM7250B GPIO 1 output-high -> 3.3V supply enable
 *     vreg_pcie0_1p05 - PM7250B GPIO 4 output-high -> 1.05V supply enable
 *     hub_rst         - PM8350C GPIO 4, active-low reset
 *
 * Copyright (c) Qualcomm Technologies, Inc. All rights reserved.
 */

#include <asm/gpio.h>
#include <dm.h>
#include <dm/uclass.h>
#include <linux/delay.h>
#include <log.h>

/*
 * PM8350C/PM7250B PMIC GPIO numbers used here are 1-based in the DT; the
 * DM GPIO offset within the qcom_spmi_pmic_gpio device is 0-based.
 */
#define PM8350C_RESX_OFFSET		0	/* gpio1: QPS615 resx */
#define PM8350C_HUB_RST_OFFSET		3	/* gpio4: Renesas hub reset */
#define PM7250B_PCIE0_3P3_OFFSET	0	/* gpio1: 3.3V enable */
#define PM7250B_PCIE0_1P05_OFFSET	3	/* gpio4: 1.05V enable */

/* Settle delays, matching the fixed regulators' own ramp delays */
#define PCIE0_3P3_RAMP_US		4300
#define PCIE0_1P05_RAMP_US		10000
#define RESET_HOLD_MS			15	/* reset-low hold through rail ramp */
#define PCIE_POWER_SETTLE_MS		15	/* margin after resets deasserted */

/**
 * pmic_gpio_get_desc() - Build a gpio_desc for a PMIC GPIO offset and request it
 *
 * @gpio_dev:	PMIC GPIO uclass device (e.g. resolved via pmic_gpio_uclass_get())
 * @offset:	0-based GPIO offset within @gpio_dev
 * @label:	descriptive label for the request
 * @desc:	returned descriptor
 *
 * Return: 0 on success, negative on error
 */
static int pmic_gpio_get_desc(struct udevice *gpio_dev, uint offset,
			      const char *label, struct gpio_desc *desc)
{
	desc->dev = gpio_dev;
	desc->offset = offset;
	desc->flags = 0;

	return dm_gpio_request(desc, label);
}

/**
 * pmic_gpio_drive() - Configure a PMIC GPIO as output and drive it high/low
 */
static int pmic_gpio_drive(struct gpio_desc *desc, int value)
{
	ulong flags = GPIOD_IS_OUT | (value ? GPIOD_IS_OUT_ACTIVE : 0);
	int ret;

	ret = dm_gpio_set_dir_flags(desc, flags);
	if (ret)
		return ret;

	return dm_gpio_set_value(desc, value);
}

/**
 * pmic_gpio_uclass_get() - Find a PMIC GPIO controller by its DT compatible
 *
 * pm8350c_gpios/pm7250b_gpios are DT phandle labels, not U-Boot device
 * names (the device name comes from the DT node name, e.g. "gpio@8800"),
 * so uclass_get_device_by_name() cannot find them. Look up by compatible
 * string instead, which is unique per PMIC on this board.
 */
static int pmic_gpio_uclass_get(const char *compat, struct udevice **devp)
{
	ofnode node;

	node = ofnode_by_compatible(ofnode_null(), compat);
	if (!ofnode_valid(node))
		return -ENODEV;

	return uclass_get_device_by_ofnode(UCLASS_GPIO, node, devp);
}

/**
 * qcs6490_pcie_power_init() - Sequence PCIe1 downstream power and resets
 *
 * Assert both resets low first, enable the Renesas power rails, hold the
 * resets through the rail ramp-up + settle time, then deassert both.
 * Runs unconditionally on every boot (cold or warm).
 *
 * Return: 0 on success (non-fatal if a PMIC GPIO bus is not found)
 */
static int qcs6490_pcie_power_init(void)
{
	struct udevice *pm8350c_gpio, *pm7250b_gpio;
	struct gpio_desc resx, hub_rst, pcie0_3p3, pcie0_1p05;
	int ret;

	ret = pmic_gpio_uclass_get("qcom,pm8350c-gpio", &pm8350c_gpio);
	if (ret) {
		log_warning("pm8350c_gpios not found (%d), skipping PCIe power init\n",
			    ret);
		return 0;	/* non-fatal */
	}

	ret = pmic_gpio_uclass_get("qcom,pm7250b-gpio", &pm7250b_gpio);
	if (ret) {
		log_warning("pm7250b_gpios not found (%d), skipping PCIe power init\n",
			    ret);
		return 0;	/* non-fatal */
	}

	if (pmic_gpio_get_desc(pm8350c_gpio, PM8350C_RESX_OFFSET,
			       "qps615-resx", &resx) ||
	    pmic_gpio_get_desc(pm8350c_gpio, PM8350C_HUB_RST_OFFSET,
			       "renesas-hub-reset", &hub_rst) ||
	    pmic_gpio_get_desc(pm7250b_gpio, PM7250B_PCIE0_3P3_OFFSET,
			       "vreg-pcie0-3p3-en", &pcie0_3p3) ||
	    pmic_gpio_get_desc(pm7250b_gpio, PM7250B_PCIE0_1P05_OFFSET,
			       "vreg-pcie0-1p05-en", &pcie0_1p05)) {
		log_warning("failed to request PCIe power/reset GPIOs\n");
		return 0;	/* non-fatal */
	}

	/* Assert both resets low before touching power rails */
	pmic_gpio_drive(&resx, 0);
	pmic_gpio_drive(&hub_rst, 0);

	/* Renesas xHCI 3.3V */
	pmic_gpio_drive(&pcie0_3p3, 1);
	udelay(PCIE0_3P3_RAMP_US);

	/* Renesas xHCI 1.05V */
	pmic_gpio_drive(&pcie0_1p05, 1);
	udelay(PCIE0_1P05_RAMP_US);

	/* Hold resets asserted through the rail ramp-up */
	mdelay(RESET_HOLD_MS);

	/* Deassert both resets (active-low -> drive high) */
	pmic_gpio_drive(&resx, 1);
	pmic_gpio_drive(&hub_rst, 1);

	mdelay(PCIE_POWER_SETTLE_MS);

	return 0;
}

/**
 * qcom_board_init() - QCS6490 / RB3 Gen2 board-specific initialization
 *
 * Overrides the empty __weak qcom_board_init() in mach-snapdragon/board.c.
 * Sequences PCIe1 downstream power/reset so RC1 enumeration works reliably
 * on both cold boot and warm reboot.
 */
void qcom_board_init(void)
{
	if (ofnode_device_is_compatible(ofnode_root(), "qcom,qcm6490") ||
	    ofnode_device_is_compatible(ofnode_root(), "qcom,qcs6490"))
		qcs6490_pcie_power_init();
}
