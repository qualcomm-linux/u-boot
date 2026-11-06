// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000-2003
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

/*
 * Misc boot support
 */
#include <command.h>
#include <dm.h>
#include <net.h>
#include <time.h>
#include <vsprintf.h>
#include <linux/delay.h>
#include <reboot-mode/reboot-mode.h>

#ifdef CONFIG_CMD_GO

/* Allow ports to override the default behavior */
__attribute__((weak))
unsigned long do_go_exec(ulong (*entry)(int, char * const []), int argc,
				 char *const argv[])
{
	return entry (argc, argv);
}

static int do_go(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	ulong	addr, rc;
	int     rcode = 0;

	if (argc < 2)
		return CMD_RET_USAGE;

	addr = hextoul(argv[1], NULL);

	printf ("## Starting application at 0x%08lX ...\n", addr);
	flush();

	/*
	 * pass address parameter as argv[0] (aka command name),
	 * and all remaining args
	 */
	rc = do_go_exec ((void *)addr, argc - 1, argv + 1);
	if (rc != 0) rcode = 1;

	printf ("## Application terminated, rc = 0x%lX\n", rc);
	return rcode;
}

/* -------------------------------------------------------------------- */

U_BOOT_CMD(
	go, CONFIG_SYS_MAXARGS, 1,	do_go,
	"start application at address 'addr'",
	"addr [arg ...]\n    - start application at address 'addr'\n"
	"      passing 'arg' as arguments"
);

#endif

static int reset_list_modes(void)
{
	const struct reboot_mode_uclass_platdata *plat_data;
	struct reboot_mode_ops *ops;
	struct udevice *dev;
	int i, found = 0;

	uclass_foreach_dev_probe(UCLASS_REBOOT_MODE, dev) {
		ops = reboot_mode_get_ops(dev);
		if (!ops || !ops->trigger)
			continue;

		plat_data = dev_get_uclass_plat(dev);
		for (i = 0; i < plat_data->count; i++) {
			if (!found++)
				printf("Available reset modes:\n");
			printf("  %s\n", plat_data->modes[i].mode_name);
		}
	}

	if (!found)
		printf("No reset modes available\n");

	return CMD_RET_SUCCESS;
}

static int reset_to_mode(const char *name)
{
	if (reboot_mode_find(name, NULL, NULL)) {
		printf("Unknown reset mode \"%s\"\n", name);
		reset_list_modes();

		return CMD_RET_FAILURE;
	}

	printf("resetting into \"%s\" mode ...\n", name);
	mdelay(100);

	/* Does not return on success */
	reboot_mode_request(name);

	printf("Failed to reset into \"%s\" mode\n", name);

	return CMD_RET_FAILURE;
}

static int do_reset_cmd(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	if (IS_ENABLED(CONFIG_DM_REBOOT_MODE) && argc > 1) {
		if (!strcmp(argv[1], "-l") && argc == 2)
			return reset_list_modes();
		if (!strcmp(argv[1], "-m"))
			return argc == 3 ? reset_to_mode(argv[2]) :
					   CMD_RET_USAGE;
	}

	return do_reset(cmdtp, flag, argc, argv);
}

U_BOOT_CMD(
	reset, 3, 0,	do_reset_cmd,
	"Perform RESET of the CPU",
	"- cold boot without level specifier\n"
	"reset -w - warm reset if implemented\n"
#if IS_ENABLED(CONFIG_DM_REBOOT_MODE)
	"reset -m <mode> - reset into a mode declared in the device tree\n"
	"reset -l - list the available reset modes\n"
#endif
);

#ifdef CONFIG_CMD_POWEROFF
U_BOOT_CMD(
	poweroff, 1, 0,	do_poweroff,
	"Perform POWEROFF of the device",
	""
);
#endif
