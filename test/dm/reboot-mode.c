// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) 2018 Theobroma Systems Design und Consulting GmbH
 */

#include <dm.h>
#include <reboot-mode/reboot-mode.h>
#include <console.h>
#include <env.h>
#include <log.h>
#include <asm/gpio.h>
#include <asm/rtc.h>
#include <asm/test.h>
#include <dm/test.h>
#include <test/test.h>
#include <test/ut.h>
#include <rtc.h>
#include <linux/byteorder/generic.h>

/*
 * A trigger-only reboot-mode backend used to prove framework-driven dispatch
 * and enumeration without touching hardware. Instead of resetting, its
 * trigger() records the decoded magic cells so the test can inspect them.
 */
static u32 test_trigger_magic[REBOOT_MODE_MAX_MAGIC];
static int test_trigger_count;

static int test_trigger(struct udevice *dev, const u32 *magic, int count)
{
	int i;

	test_trigger_count = count;
	for (i = 0; i < count && i < REBOOT_MODE_MAX_MAGIC; i++)
		test_trigger_magic[i] = magic[i];

	/* A real backend does not return here; the test one does. */
	return -EINPROGRESS;
}

static const struct reboot_mode_ops test_trigger_ops = {
	.trigger = test_trigger,
};

static const struct udevice_id test_trigger_ids[] = {
	{ .compatible = "reboot-mode-test-trigger" },
	{ }
};

U_BOOT_DRIVER(reboot_mode_test_trigger) = {
	.name = "reboot_mode_test_trigger",
	.id = UCLASS_REBOOT_MODE,
	.of_match = test_trigger_ids,
	.ops = &test_trigger_ops,
};

static int dm_test_reboot_mode_gpio(struct unit_test_state *uts)
{
	struct udevice *gpio_dev;
	struct udevice *rm_dev;
	int gpio0_offset = 0;
	int gpio1_offset = 1;

	uclass_get_device_by_name(UCLASS_GPIO, "pinmux-gpios", &gpio_dev);

	/* Prepare the GPIOs for "download" mode */
	sandbox_gpio_set_direction(gpio_dev, gpio0_offset, 0);
	sandbox_gpio_set_direction(gpio_dev, gpio1_offset, 0);
	sandbox_gpio_set_value(gpio_dev, gpio0_offset, 1);
	sandbox_gpio_set_value(gpio_dev, gpio1_offset, 1);

	ut_assertok(uclass_get_device_by_name(UCLASS_REBOOT_MODE,
					      "reboot-mode0", &rm_dev));
	ut_assertok(dm_reboot_mode_update(rm_dev));

	ut_asserteq_str("download", env_get("bootstatus"));

	return 0;
}
DM_TEST(dm_test_reboot_mode_gpio,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);

static int dm_test_reboot_mode_rtc(struct unit_test_state *uts)
{
	struct udevice *rtc_dev;
	struct udevice *rm_dev;
	u32 read_val;
	u32 test_magic_val = cpu_to_be32(0x21969147);

	uclass_get_device_by_name(UCLASS_RTC, "rtc@43",
				  &rtc_dev);
	dm_rtc_write(rtc_dev, REG_AUX0, (u8 *)&test_magic_val, 4);

	ut_assertok(uclass_get_device_by_name(UCLASS_REBOOT_MODE,
					      "reboot-mode@14", &rm_dev));
	ut_assertok(dm_reboot_mode_update(rm_dev));

	ut_asserteq_str("test", env_get("bootstatus"));

	dm_rtc_read(rtc_dev, REG_AUX0, (u8 *)&read_val, 4);
	ut_asserteq(read_val, 0);

	return 0;
}
DM_TEST(dm_test_reboot_mode_rtc,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);

/* reboot_mode_request() triggers a named mode with the right magic cells */
static int dm_test_reboot_mode_request(struct unit_test_state *uts)
{
	test_trigger_count = 0;
	test_trigger_magic[0] = 0;
	test_trigger_magic[1] = 0;

	/*
	 * "obelisk" is a 2-cell mode <0x80000000 0x00000001>. The trigger
	 * backend records the cells instead of resetting and returns
	 * -EINPROGRESS.
	 */
	ut_asserteq(-EINPROGRESS, reboot_mode_request("obelisk"));
	ut_asserteq(2, test_trigger_count);
	ut_asserteq(0x80000000, test_trigger_magic[0]);
	ut_asserteq(0x00000001, test_trigger_magic[1]);

	/* "sarcophagus" is a 1-cell mode <0x80000000> */
	test_trigger_count = 0;
	ut_asserteq(-EINPROGRESS, reboot_mode_request("sarcophagus"));
	ut_asserteq(1, test_trigger_count);
	ut_asserteq(0x80000000, test_trigger_magic[0]);

	/* An unknown mode, and a backing-store-only mode, are not triggerable */
	ut_asserteq(-ENOENT, reboot_mode_request("nonesuch"));
	ut_asserteq(-ENOENT, reboot_mode_request("download"));

	return 0;
}
DM_TEST(dm_test_reboot_mode_request,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);

/* reboot_mode_list() enumerates only triggerable modes */
static int dm_test_reboot_mode_list(struct unit_test_state *uts)
{
	ut_assertok(console_record_reset_enable());

	ut_assertok(reboot_mode_list());

	ut_assert_nextline("Available reset modes:");
	ut_assert_nextline("  obelisk");
	ut_assert_nextline("  sarcophagus");
	/* Backing-store modes (test/download) have no trigger and are hidden */
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_reboot_mode_list,
	UTF_PROBE_TEST | UTF_SCAN_FDT | UTF_FLAT_TREE);
