// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <blk.h>
#include <cpu_func.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>
#include <dm/uclass.h>
#include <init.h>
#include <linux/err.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <malloc.h>
#include <mach/qclib.h>
#include <mach/spl.h>
#include <part.h>
#include <spl.h>

DECLARE_GLOBAL_DATA_PTR;

#if defined(CONFIG_SPL_BUILD)
/*
 * The SPL device tree does not describe the complete early physical layout,
 * so use Shikra's fixed map when enabling the MMU.
 */
#define SHIKRA_SPL_DEVICE_REG_LOW_BASE		0x00000000UL
#define SHIKRA_SPL_SYSTEM_IMEM_BASE		0x0c100000UL
#define SHIKRA_SPL_SYSTEM_IMEM_SIZE		0x00020000UL
#define SHIKRA_SPL_BOOTIMEM_BASE		0x0c200000UL
#define SHIKRA_SPL_BOOTIMEM_SIZE		0x001c0000UL
#define SHIKRA_SPL_DEVICE_REG_HIGH_BASE	(SHIKRA_SPL_BOOTIMEM_BASE + \
						 SHIKRA_SPL_BOOTIMEM_SIZE)
#define SHIKRA_SPL_DDR_BASE			0x80000000UL
#define SHIKRA_SPL_DDR_SIZE			0x80000000UL
#define SHIKRA_SPL_DEVICE_REG_LOW_SIZE		(SHIKRA_SPL_SYSTEM_IMEM_BASE - \
						 SHIKRA_SPL_DEVICE_REG_LOW_BASE)
#define SHIKRA_SPL_DEVICE_REG_HIGH_SIZE	(SHIKRA_SPL_DDR_BASE - \
						 SHIKRA_SPL_DEVICE_REG_HIGH_BASE)

static struct mm_region shikra_spl_mem_map[] = {
	{
		.virt = SHIKRA_SPL_DEVICE_REG_LOW_BASE,
		.phys = SHIKRA_SPL_DEVICE_REG_LOW_BASE,
		.size = SHIKRA_SPL_DEVICE_REG_LOW_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE,
	}, {
		.virt = SHIKRA_SPL_SYSTEM_IMEM_BASE,
		.phys = SHIKRA_SPL_SYSTEM_IMEM_BASE,
		.size = SHIKRA_SPL_SYSTEM_IMEM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		/* SPL and QCLIB execute directly from BOOT_IMEM. */
		.virt = SHIKRA_SPL_BOOTIMEM_BASE,
		.phys = SHIKRA_SPL_BOOTIMEM_BASE,
		.size = SHIKRA_SPL_BOOTIMEM_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		.virt = SHIKRA_SPL_DEVICE_REG_HIGH_BASE,
		.phys = SHIKRA_SPL_DEVICE_REG_HIGH_BASE,
		.size = SHIKRA_SPL_DEVICE_REG_HIGH_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) |
			 PTE_BLOCK_NON_SHARE,
	}, {
		.virt = SHIKRA_SPL_DDR_BASE,
		.phys = SHIKRA_SPL_DDR_BASE,
		.size = SHIKRA_SPL_DDR_SIZE,
		.attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL_NC) |
			 PTE_BLOCK_INNER_SHARE,
	}, {
		0,
	}
};

struct mm_region *qcom_spl_mem_map(void)
{
	return shikra_spl_mem_map;
}

int arm_reserve_mmu(void)
{
#if !(CONFIG_IS_ENABLED(SYS_ICACHE_OFF) && CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
	mem_map = qcom_spl_mem_map();
	gd->arch.tlb_size = PGTABLE_SIZE;
	gd->arch.tlb_addr = (unsigned long)memalign(SZ_64K, gd->arch.tlb_size);
	if (!gd->arch.tlb_addr)
		return -ENOMEM;
#endif

	return 0;
}
#endif

#define IPQ_SPL_BOOTCFG_REG_ADDR	0x1B46070
#define IPQ_SPL_BOOTCFG_DEV_MASK	GENMASK(5, 1)
#define IPQ_SPL_BOOTCFG_DEV_SHFT	0x1

#define SHIKRA_DDR_INFORMATION		"ddr_information"
#define SHIKRA_DDR_TRAINING_DATA	"ddr_training_data"
#define SHIKRA_DDR_SR_EXIT		"ddr_sr_exit"
#define SHIKRA_PMIC_COCOS		"pmic_cocos"
#define SHIKRA_PMIC_WAILUA		"pmic_wailua"
#define SHIKRA_DCB_LP4			"dcb_lp4"
#define SHIKRA_DCB_LP5			"dcb_lp5"
#define SHIKRA_ECC			"ecc"

#define SHIKRA_DDR_TRAINING_DATA_ADDR	0x0c368000
#define SHIKRA_DDR_TRAINING_DATA_SIZE	0x8000
#define SHIKRA_PMIC_COCOS_SIZE		0xbc00
#define SHIKRA_PMIC_WAILUA_SIZE	0x6000
#define SHIKRA_DCB_LP4_SIZE		0x4800
#define SHIKRA_DCB_LP5_SIZE		0x4800
#define SHIKRA_ECC_SIZE			0x400

#define SHIKRA_SHRM2_SPROC_CTRL_ADDR			0x0c36000
#define SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK	BIT(1)
#define SHIKRA_SHRM2_SPROC_CTRL_DEBUG_SW_RESET_BMSK	BIT(3)

/* GCC_APCS_MISC holds RPM in reset until QCLIB has initialized DDR. */
#define SHIKRA_GCC_APCS_MISC_ADDR			0x01491000
#define SHIKRA_GCC_APCS_MISC_RPM_RESET_BMSK		BIT(0)

#define QCOM_SPL_FIT_IMG_PARTITION	"core_nhlos_a"

static u64 g_ddr_sr_exit_address __section(".data");

enum {
	IPQ_SPL_BOOTCFG_DEV_MMC = 0x0,
	IPQ_SPL_BOOTCFG_DEV_SD_EMMC	= 0x1,
	IPQ_SPL_BOOTCFG_DEV_EDL		= 0x2,
	IPQ_SPL_BOOTCFG_DEV_NAND	= 0x3,
	IPQ_SPL_BOOTCFG_DEV_EMMC_SD		= 0x4,
	IPQ_SPL_BOOTCFG_DEV_MAX
};

#if defined(CONFIG_SPL_BUILD)
void board_init_f(ulong dummy)
{
	int ret = 0;

	memset(__bss_start, 0, __bss_end - __bss_start);

	qcom_spl_malloc_init_f();

	ret = spl_early_init();
	if (ret) {
		pr_debug("spl_early_init() failed (%d)\n", ret);
		goto fail;
	}

	event_notify_null(EVT_LAST_STAGE_INIT);

	preloader_console_init();

#if !(CONFIG_IS_ENABLED(SYS_ICACHE_OFF) && CONFIG_IS_ENABLED(SYS_DCACHE_OFF))
	ret = arm_reserve_mmu();
	if (ret) {
		pr_debug("arm_reserve_mmu() failed (%d)\n", ret);
		goto fail;
	}

	enable_caches();
#endif

	ret = qcom_spl_loader_pre_ddr(spl_boot_device());
	if (ret) {
		pr_debug("qcom_spl_loader_pre_ddr() failed (%d)\n", ret);
		goto fail;
	}

	ret = qcom_spl_invoke_qclib();
	if (ret) {
		pr_debug("qcom_spl_invoke_qclib() failed (%d)\n", ret);
		goto fail;
	}

	board_init_r(NULL, 0);

fail:
	if (ret)
		reset_cpu();
}
#endif

u64 shikra_get_ddr_sr_exit_address(void)
{
	return g_ddr_sr_exit_address;
}

void shikra_set_ddr_sr_exit_address(u64 address)
{
	g_ddr_sr_exit_address = address;
}

static int shikra_add_iftbl_entry(struct interface_table *table,
				  const char *name, u64 address, u32 size)
{
	uint entry_idx = table->num_entries;

	if (entry_idx >= table->max_entries)
		return -ENOSPC;

	memset(&table->if_table_entries[entry_idx], 0,
	       sizeof(table->if_table_entries[entry_idx]));
	memcpy(table->if_table_entries[entry_idx].entry_name, name, strlen(name));
	table->if_table_entries[entry_idx].address = address;
	table->if_table_entries[entry_idx].size = size;
	table->num_entries = entry_idx + 1;

	return 0;
}

static int shikra_add_iftbl_fit_blob(struct interface_table *table,
				     const void *fit, int images_node,
				     const char *name, u32 size)
{
	int node, ret;
	u64 address;

	node = fdt_subnode_offset(fit, images_node, name);
	if (node < 0)
		return -ENOENT;

	ret = qcom_spl_get_fit_img_entry_point((void *)fit, node, &address);
	if (ret)
		return ret;

	return shikra_add_iftbl_entry(table, name, address, size);
}

int qcom_spl_soc_qclib_override(struct interface_table *table,
				 const void *fit, int images_node)
{
	static const struct {
		const char *name;
		u32 size;
	} fit_blobs[] = {
		{ SHIKRA_PMIC_COCOS, SHIKRA_PMIC_COCOS_SIZE },
		{ SHIKRA_PMIC_WAILUA, SHIKRA_PMIC_WAILUA_SIZE },
		{ SHIKRA_DCB_LP4, SHIKRA_DCB_LP4_SIZE },
		{ SHIKRA_DCB_LP5, SHIKRA_DCB_LP5_SIZE },
		{ SHIKRA_ECC, SHIKRA_ECC_SIZE },
	};
	uint i;
	int ret;

	table->global_attributes |= QCLIB_GA_ENABLE_UART_LOGGING;
	for (i = 0; i < ARRAY_SIZE(fit_blobs); i++) {
		ret = shikra_add_iftbl_fit_blob(table, fit, images_node,
						 fit_blobs[i].name, fit_blobs[i].size);
		if (ret)
			return ret;
	}

	ret = shikra_add_iftbl_entry(table, SHIKRA_DDR_TRAINING_DATA,
				     SHIKRA_DDR_TRAINING_DATA_ADDR,
				     SHIKRA_DDR_TRAINING_DATA_SIZE);
	if (ret)
		return ret;
	ret = shikra_add_iftbl_entry(table, SHIKRA_DDR_INFORMATION, 0, 0);
	if (ret)
		return ret;

	return shikra_add_iftbl_entry(table, SHIKRA_DDR_SR_EXIT, 0, 0);
}

int qcom_spl_soc_pre_qclib_routine(void)
{
	writel(SHIKRA_SHRM2_SPROC_CTRL_DEBUG_SW_RESET_BMSK |
	       SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK,
	       (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);
	writel(SHIKRA_SHRM2_SPROC_CTRL_CORE_SW_RESET_BMSK,
	       (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);
	writel(0, (void *)SHIKRA_SHRM2_SPROC_CTRL_ADDR);

	return 0;
}

int qcom_spl_soc_post_qclib_routine(struct interface_table *table)
{
	struct interface_table_entry entry;
	int ret;

	/*
	 * QCLIB has completed DDR initialization and loaded the RPM image.
	 * Release RPM before handing control to the post-DDR SPL stage.
	 */
	clrbits_le32((void *)SHIKRA_GCC_APCS_MISC_ADDR,
		     SHIKRA_GCC_APCS_MISC_RPM_RESET_BMSK);

	ret = qcom_spl_get_iftbl_entry_by_name(table, SHIKRA_DDR_SR_EXIT,
						&entry);
	if (ret)
		return ret;

	shikra_set_ddr_sr_exit_address(entry.address);
	return 0;
}

#if CONFIG_IS_ENABLED(MMC)
int spl_mmc_boot_partition(const u32 boot_device)
{
	struct blk_desc *desc;
	struct disk_partition info;
	int partition;

	desc = blk_get_devnum_by_uclass_id(UCLASS_MMC, 0);
	if (!desc)
		return -ENODEV;

	partition = part_get_info_by_name(desc, QCOM_SPL_FIT_IMG_PARTITION,
					  &info);
	if (partition < 0)
		return -ENOENT;

	return partition;
}
#endif

/**
 * spl_boot_device() - Get the boot device.
 *
 * Return: U-Boot boot device identifier, or -EINVAL if the device is invalid.
 */
u32 spl_boot_device(void)
{
	u8 boot_device_cfg =
	(readl(IPQ_SPL_BOOTCFG_REG_ADDR) & IPQ_SPL_BOOTCFG_DEV_MASK) >>
	IPQ_SPL_BOOTCFG_DEV_SHFT;
	u32 boot_device_smem;

	switch (boot_device_cfg) {
	case IPQ_SPL_BOOTCFG_DEV_MMC:
		boot_device_smem = BOOT_DEVICE_MMC1;
		printf("Selected boot device: MMC\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_SD_EMMC:
		boot_device_smem = BOOT_DEVICE_MMC2;
		printf("Selected boot device: SD MMC\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_EDL:
		boot_device_smem = BOOT_DEVICE_USB;
		printf("Selected boot device: SPI-NAND\n");
		break;
	case IPQ_SPL_BOOTCFG_DEV_NAND:
		boot_device_smem = BOOT_DEVICE_NAND;
		printf("Selected boot device: SPI-NAND\n");
		break;
	default:
		pr_err("Invalid boot device configured: %d\n",
		       boot_device_cfg);
		return -EINVAL;
	}

	return boot_device_smem;
}
