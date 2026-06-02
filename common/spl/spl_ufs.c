// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Alexey Charkov <alchark@gmail.com>
 */

#include <dm.h>
#include <log.h>
#include <spl.h>
#include <spl_load.h>
#include <scsi.h>
#include <part.h>
#include <blk.h>
#include <linux/compiler.h>
#include <errno.h>

/* Block read callback for spl_load framework */
static ulong spl_ufs_load_read(struct spl_load_info *load, ulong off,
			       ulong size, void *buf)
{
	struct blk_desc *bd = load->priv;
	lbaint_t sector = off >> bd->log2blksz;
	lbaint_t count = size >> bd->log2blksz;

	return blk_dread(bd, sector, count, buf) << bd->log2blksz;
}

/* Load image from raw sector */
static int ufs_load_image_raw_sector(struct spl_image_info *spl_image,
				     struct spl_boot_device *bootdev,
				     struct blk_desc *bd,
				     unsigned long sector)
{
	struct spl_load_info load;
	int ret;

	debug("spl: ufs: loading from sector 0x%lx\n", sector);

	spl_load_init(&load, spl_ufs_load_read, bd, bd->blksz);
	ret = spl_load(spl_image, bootdev, &load, 0, sector << bd->log2blksz);

	if (ret) {
		printf("%s: ufs block read error\n", __func__);
		log_debug("(error=%d)\n", ret);
		return ret;
	}

	return 0;
}

/* Find and load from partition */
static int ufs_load_image_raw_partition(struct spl_image_info *spl_image,
					struct spl_boot_device *bootdev,
					struct blk_desc *bd)
{
	struct disk_partition part_info;
	int ret = -ENOENT;

	/* Try partition name first if configured */
#ifdef CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NAME
	if (strlen(CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NAME) > 0) {
		debug("spl: ufs: trying partition name '%s'\n",
		      CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NAME);
		ret = part_get_info_by_name(bd,
					    CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NAME,
					    &part_info);
		if (ret >= 0)
			debug("spl: ufs: found partition '%s' at 0x%lx\n",
			      CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NAME,
			      (ulong)part_info.start);
		else
			debug("spl: ufs: partition name not found: %d\n", ret);
	}
#endif

	/* Fall back to partition number if name lookup failed */
	if (ret < 0 && IS_ENABLED(CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NUM)) {
		debug("spl: ufs: trying partition number %d\n",
		      CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NUM);
		ret = part_get_info(bd, CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NUM,
				    &part_info);
		if (ret >= 0)
			debug("spl: ufs: found partition %d at 0x%lx\n",
			      CONFIG_SPL_UFS_RAW_U_BOOT_PARTITION_NUM,
			      (ulong)part_info.start);
		else
			debug("spl: ufs: partition number not found: %d\n", ret);
	}

	if (ret >= 0)
		return ufs_load_image_raw_sector(spl_image, bootdev, bd,
						 part_info.start);

	puts("spl: ufs: partition error\n");
	return ret;
}

u32 __weak spl_ufs_boot_mode(const u32 boot_device)
{
	return UFS_MODE_RAW;
}

int spl_ufs_load(struct spl_image_info *spl_image,
		 struct spl_boot_device *bootdev,
		 const char *filename)
{
	u32 boot_mode;
	int ret = 0;
	int devnum = CONFIG_SPL_UFS_RAW_U_BOOT_DEVNUM;
	struct blk_desc *bd;

	log_debug("spl: ufs: devnum=%d\n", devnum);

	ret = scsi_scan(false);
	if (ret) {
		printf("spl: ufs: scsi scan failed: %d\n", ret);
		return ret;
	}

	bd = blk_get_devnum_by_uclass_id(UCLASS_SCSI, devnum);
	if (!bd) {
		printf("spl: ufs: could not get device %d\n", devnum);
		return -ENODEV;
	}

	boot_mode = spl_ufs_boot_mode(bootdev->boot_device);
	ret = -EINVAL;

	switch (boot_mode) {
	case UFS_MODE_RAW:
		debug("spl: ufs: boot mode: raw\n");

#ifdef CONFIG_SPL_UFS_RAW_U_BOOT_USE_SECTOR
		ret = ufs_load_image_raw_sector(spl_image, bootdev, bd,
						CONFIG_SPL_UFS_RAW_U_BOOT_SECTOR);
		if (!ret)
			return 0;
#elif defined(CONFIG_SPL_UFS_RAW_U_BOOT_USE_PARTITION)
		ret = ufs_load_image_raw_partition(spl_image, bootdev, bd);
		if (!ret)
			return 0;
#endif
		break;
	default:
		puts("spl: ufs: wrong boot mode\n");
	}

	return ret;
}

/* SPL load image entry point */
static int spl_ufs_load_image(struct spl_image_info *spl_image,
			      struct spl_boot_device *bootdev)
{
	return spl_ufs_load(spl_image, bootdev,
#ifdef CONFIG_SPL_FS_LOAD_PAYLOAD_NAME
			    CONFIG_SPL_FS_LOAD_PAYLOAD_NAME);
#else
			    NULL);
#endif
}

SPL_LOAD_IMAGE_METHOD("UFS", 0, BOOT_DEVICE_UFS, spl_ufs_load_image);
