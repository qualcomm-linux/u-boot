// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2024 The Android Open Source Project
 */

#include <blk.h>
#include <div64.h>
#include <env.h>
#include <fastboot.h>
#include <fastboot-internal.h>
#include <fb_block.h>
#include <image-sparse.h>
#include <malloc.h>
#include <part.h>
#include <vsprintf.h>

/**
 * FASTBOOT_MAX_BLOCKS_ERASE - maximum blocks to erase per derase call
 *
 * in the ERASE case we can have much larger buffer size since
 * we're not transferring an actual buffer
 */
#define FASTBOOT_MAX_BLOCKS_ERASE 1048576
/**
 * FASTBOOT_MAX_BLOCKS_SOFT_ERASE - maximum blocks to software erase at once
 */
#define FASTBOOT_MAX_BLOCKS_SOFT_ERASE 4096
/**
 * FASTBOOT_MAX_BLOCKS_WRITE - maximum blocks to write per dwrite call
 */
#define FASTBOOT_MAX_BLOCKS_WRITE 65536

__weak lbaint_t fb_mmc_get_boot_offset(void)
{
	return 0;
}

struct fb_block_sparse {
	struct blk_desc	*dev_desc;
};

/* Write 0s instead of using erase operation, inefficient but functional */
static lbaint_t fb_block_soft_erase(struct blk_desc *block_dev, lbaint_t blk,
				    lbaint_t cur_blkcnt, lbaint_t erase_buf_blks,
				    void *erase_buffer)
{
	lbaint_t blks_written = 0;
	lbaint_t j;

	memset(erase_buffer, 0, erase_buf_blks * block_dev->blksz);

	for (j = 0; j < cur_blkcnt; j += erase_buf_blks) {
		lbaint_t remain = min(cur_blkcnt - j, erase_buf_blks);

		blks_written += blk_dwrite(block_dev, blk + j,
					   remain, erase_buffer);
		printf(".");
	}

	return blks_written;
}

static lbaint_t fb_block_write(struct blk_desc *block_dev, lbaint_t start,
			       lbaint_t blkcnt, const void *buffer)
{
	lbaint_t blk = start;
	lbaint_t blks_written = 0;
	lbaint_t blks = 0;
	void *erase_buf = NULL;
	int erase_buf_blks = 0;
	lbaint_t step = buffer ? FASTBOOT_MAX_BLOCKS_WRITE : FASTBOOT_MAX_BLOCKS_ERASE;
	lbaint_t i;

	for (i = 0; i < blkcnt; i += step) {
		lbaint_t cur_blkcnt = min(blkcnt - i, step);

		if (buffer) {
			if (fastboot_progress_callback)
				fastboot_progress_callback("writing");
			blks_written = blk_dwrite(block_dev, blk, cur_blkcnt,
						  buffer + (i * block_dev->blksz));
		} else {
			if (fastboot_progress_callback)
				fastboot_progress_callback("erasing");

			if (!erase_buf) {
				blks_written = blk_derase(block_dev, blk, cur_blkcnt);

				/* Allocate erase buffer if erase is not implemented */
				if ((long)blks_written == -ENOSYS) {
					erase_buf_blks = min_t(long, blkcnt,
							       FASTBOOT_MAX_BLOCKS_SOFT_ERASE);
					erase_buf = malloc(erase_buf_blks * block_dev->blksz);

					printf("Slowly writing empty buffers due to missing erase operation\n");
				}
			}

			if (erase_buf)
				blks_written = fb_block_soft_erase(block_dev, blk, cur_blkcnt,
								   erase_buf_blks, erase_buf);
		}
		blk += blks_written;
		blks += blks_written;
	}

	if (erase_buf)
		free(erase_buf);

	return blks;
}

static lbaint_t fb_block_sparse_write(struct sparse_storage *info,
				      lbaint_t blk, lbaint_t blkcnt,
				      const void *buffer)
{
	struct fb_block_sparse *sparse = info->priv;
	struct blk_desc *dev_desc = sparse->dev_desc;

	return fb_block_write(dev_desc, blk, blkcnt, buffer);
}

static lbaint_t fb_block_sparse_reserve(struct sparse_storage *info,
					lbaint_t blk, lbaint_t blkcnt)
{
	return blkcnt;
}

/**
 * FASTBOOT_BLOCK_IFACE_LEN - max length of a block interface name, including NUL
 */
#define FASTBOOT_BLOCK_IFACE_LEN 16

/**
 * block_interface_override - runtime-set block interface name, empty if unset
 */
static char block_interface_override[FASTBOOT_BLOCK_IFACE_LEN];
/**
 * block_device_override - runtime-set block device number, -1 if unset
 */
static int block_device_override = -1;

/**
 * fb_block_interface() - Resolve the block interface name to use
 *
 * Returns the runtime override set via fastboot_block_set_target() if one
 * is active, otherwise falls back to the build-time config default.
 */
static const char *fb_block_interface(void)
{
	if (block_interface_override[0])
		return block_interface_override;

	return config_opt_enabled(CONFIG_FASTBOOT_FLASH_BLOCK,
				  CONFIG_FASTBOOT_FLASH_BLOCK_INTERFACE_NAME, NULL);
}

/**
 * fb_block_device() - Resolve the block device number to use
 *
 * Returns the runtime override set via fastboot_block_set_target() if one
 * is active, otherwise falls back to the build-time config default.
 */
static int fb_block_device(void)
{
	if (block_device_override >= 0)
		return block_device_override;

	return config_opt_enabled(CONFIG_FASTBOOT_FLASH_BLOCK,
				  CONFIG_FASTBOOT_FLASH_BLOCK_DEVICE_ID, -1);
}

int fastboot_block_set_target(const char *interface, int device)
{
	if (interface) {
		if (strlen(interface) >= sizeof(block_interface_override))
			return -EINVAL;
		strlcpy(block_interface_override, interface,
			sizeof(block_interface_override));
	}

	if (device >= 0)
		block_device_override = device;

	return 0;
}

static bool is_all_digits(const char *s, size_t len)
{
	size_t i;

	if (!len)
		return false;

	for (i = 0; i < len; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;

	return true;
}

/**
 * parse_device_partition() - Parse [<interface>:][<device>:]<partition> format
 * @part_name: Input string, e.g. "mtd:0:boot", "0:boot", or "boot"
 * @interface_buf: Output buffer for the interface name, FASTBOOT_BLOCK_IFACE_LEN bytes
 * @device: Output device number
 * @partition_name: Output partition name pointer, using the device-tier-stripped
 *	interpretation
 * @literal_partition_name: Optional output pointer to the unstripped interpretation
 *	of the device tier (e.g. "0:SBL" instead of "SBL"), or NULL if no device-tier
 *	stripping occurred. Pass NULL if the caller doesn't need this.
 *
 * Parses at most two leading ':'-delimited segments: a non-numeric segment is
 * an interface override, a numeric segment is a device override. Whatever
 * remains, colons and all, is left untouched as the partition name.
 *
 * Returns: 0 on success, -EINVAL if format is invalid
 */
static int parse_device_partition(const char *part_name, char *interface_buf,
				  int *device, const char **partition_name,
				  const char **literal_partition_name)
{
	const char *default_interface = fb_block_interface();
	const char *p = part_name;
	const char *colon_pos;
	size_t iface_len;

	strlcpy(interface_buf, default_interface ? default_interface : "",
		FASTBOOT_BLOCK_IFACE_LEN);
	*device = fb_block_device();
	if (literal_partition_name)
		*literal_partition_name = NULL;

	colon_pos = strchr(p, ':');

	/* Reject invalid format like ":partition" */
	if (colon_pos && colon_pos == p)
		return -EINVAL;

	/* Leading non-numeric segment is an interface override */
	if (colon_pos && !is_all_digits(p, colon_pos - p)) {
		iface_len = colon_pos - p;
		if (iface_len >= FASTBOOT_BLOCK_IFACE_LEN)
			return -EINVAL;

		memcpy(interface_buf, p, iface_len);
		interface_buf[iface_len] = '\0';

		p = colon_pos + 1;
		colon_pos = strchr(p, ':');

		if (colon_pos && colon_pos == p)
			return -EINVAL;
	}

	/* Leading numeric segment of what remains is a device override */
	if (colon_pos && is_all_digits(p, colon_pos - p)) {
		*device = simple_strtoul(p, NULL, 10);
		if (literal_partition_name)
			*literal_partition_name = p;
		p = colon_pos + 1;
	}

	*partition_name = p;

	return 0;
}

/**
 * is_partition_table_name() - Check if name matches partition table target
 * @part_name: Partition name to check (already stripped of interface/device tiers)
 * @table_name: Config name for partition table (e.g., "gpt", "mbr")
 *
 * Returns: true if part_name matches table_name
 */
static bool is_partition_table_name(const char *part_name, const char *table_name)
{
	return strcmp(part_name, table_name) == 0;
}

/**
 * fastboot_block_resolve_alias() - Look up fastboot_partition_alias_<name>
 * @name: Candidate partition name
 *
 * Returns: the aliased literal partition name, or NULL if no alias is defined
 */
static const char *fastboot_block_resolve_alias(const char *name)
{
	/* strlen("fastboot_partition_alias_") + PART_NAME_LEN + 1 */
	char env_alias_name[25 + PART_NAME_LEN + 1];

	strlcpy(env_alias_name, "fastboot_partition_alias_", sizeof(env_alias_name));
	strlcat(env_alias_name, name, sizeof(env_alias_name));

	return env_get(env_alias_name);
}

int fastboot_block_get_part_info(const char *part_name,
				 struct blk_desc **dev_desc,
				 struct disk_partition *part_info,
				 char *response)
{
	char interface[FASTBOOT_BLOCK_IFACE_LEN];
	int device, ret, literal_ret;
	const char *partition_name, *literal_partition_name, *aliased_name;
	struct disk_partition literal_info;

	if (!part_name || !strcmp(part_name, "")) {
		fastboot_fail("partition not given", response);
		return -ENOENT;
	}

	if (parse_device_partition(part_name, interface, &device,
				   &partition_name, &literal_partition_name) < 0) {
		fastboot_fail("invalid partition name format", response);
		return -EINVAL;
	}

	if (!interface[0]) {
		fastboot_fail("block interface isn't provided", response);
		return -EINVAL;
	}

	*dev_desc = blk_get_dev(interface, device);
	if (!*dev_desc) {
		fastboot_fail("no such device", response);
		return -ENODEV;
	}

	/* An alias is an explicit, unambiguous statement of intent */
	aliased_name = fastboot_block_resolve_alias(partition_name);
	if (aliased_name) {
		partition_name = aliased_name;
		literal_partition_name = NULL;
	}

	ret = part_get_info_by_name(*dev_desc, partition_name, part_info);

	if (literal_partition_name) {
		literal_ret = part_get_info_by_name(*dev_desc, literal_partition_name,
						    &literal_info);

		if (ret >= 0 && literal_ret >= 0 && part_info->start != literal_info.start) {
			fastboot_fail("ambiguous partition name, define an alias", response);
			return -EINVAL;
		}

		if (ret < 0 && literal_ret >= 0) {
			*part_info = literal_info;
			ret = literal_ret;
		}
	}

	if (ret < 0)
		fastboot_fail("failed to get partition info", response);

	return ret;
}

void fastboot_block_raw_erase_disk(struct blk_desc *dev_desc, const char *disk_name,
				   char *response)
{
	lbaint_t written;

	debug("Start Erasing %s...\n", disk_name);

	written = fb_block_write(dev_desc, fb_mmc_get_boot_offset(),
				 dev_desc->lba, NULL);
	if (written != dev_desc->lba) {
		pr_err("Failed to erase %s\n", disk_name);
		fastboot_response("FAIL", response, "Failed to erase %s", disk_name);
		return;
	}

	printf("........ erased " LBAFU " bytes from '%s'\n",
	       dev_desc->lba * dev_desc->blksz, disk_name);
	fastboot_okay(NULL, response);
}

void fastboot_block_raw_erase(struct blk_desc *dev_desc, struct disk_partition *info,
			      const char *part_name, uint alignment, char *response)
{
	lbaint_t written, blks_start, blks_size;

	if (alignment) {
		blks_start = (info->start + alignment - 1) & ~(alignment - 1);
		if (info->size >= alignment)
			blks_size = (info->size - (blks_start - info->start)) &
				(~(alignment - 1));
		else
			blks_size = 0;

		printf("Erasing blocks " LBAFU " to " LBAFU " due to alignment\n",
		       blks_start, blks_start + blks_size);
	} else {
		blks_start = info->start;
		blks_size = info->size;
	}

	written = fb_block_write(dev_desc, blks_start, blks_size, NULL);
	if (written != blks_size) {
		fastboot_fail("failed to erase partition", response);
		return;
	}

	printf("........ erased " LBAFU " bytes from '%s'\n",
	       blks_size * info->blksz, part_name);
	fastboot_okay(NULL, response);
}

void fastboot_block_erase(const char *part_name, char *response)
{
	struct blk_desc *dev_desc;
	struct disk_partition part_info;

	if (fastboot_block_get_part_info(part_name, &dev_desc, &part_info, response) < 0)
		return;

	fastboot_block_raw_erase(dev_desc, &part_info, part_name,
				 fb_mmc_get_boot_offset(), response);
}

void fastboot_block_write_raw_disk(struct blk_desc *dev_desc, const char *disk_name,
				   void *buffer, u32 download_bytes, char *response)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (dev_desc->blksz - 1)) & ~(dev_desc->blksz - 1));
	blkcnt = lldiv(blkcnt, dev_desc->blksz);

	if ((blkcnt + fb_mmc_get_boot_offset()) > dev_desc->lba) {
		pr_err("too large for disk: '%s'\n", disk_name);
		fastboot_fail("too large for disk", response);
		return;
	}

	printf("Flashing Raw Image\n");

	blks = fb_block_write(dev_desc, fb_mmc_get_boot_offset(), blkcnt, buffer);

	if (blks != blkcnt) {
		pr_err("failed writing to %s\n", disk_name);
		fastboot_fail("failed writing to device", response);
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * dev_desc->blksz,
	       disk_name);
	fastboot_okay(NULL, response);
}

void fastboot_block_write_raw_image(struct blk_desc *dev_desc,
				    struct disk_partition *info, const char *part_name,
				    void *buffer, u32 download_bytes, char *response)
{
	lbaint_t blkcnt;
	lbaint_t blks;

	/* determine number of blocks to write */
	blkcnt = ((download_bytes + (info->blksz - 1)) & ~(info->blksz - 1));
	blkcnt = lldiv(blkcnt, info->blksz);

	if (blkcnt > info->size) {
		pr_err("too large for partition: '%s'\n", part_name);
		fastboot_fail("too large for partition", response);
		return;
	}

	printf("Flashing Raw Image\n");

	blks = fb_block_write(dev_desc, info->start, blkcnt, buffer);

	if (blks != blkcnt) {
		pr_err("failed writing to device %d\n", dev_desc->devnum);
		fastboot_fail("failed writing to device", response);
		return;
	}

	printf("........ wrote " LBAFU " bytes to '%s'\n", blkcnt * info->blksz,
	       part_name);
	fastboot_okay(NULL, response);
}

void fastboot_block_write_sparse_image(struct blk_desc *dev_desc, struct disk_partition *info,
				       const char *part_name, void *buffer, char *response)
{
	struct fb_block_sparse sparse_priv;
	struct sparse_storage sparse;
	int err;

	sparse_priv.dev_desc = dev_desc;

	sparse.blksz = info->blksz;
	sparse.start = info->start;
	sparse.size = info->size;
	sparse.write = fb_block_sparse_write;
	sparse.reserve = fb_block_sparse_reserve;
	sparse.mssg = fastboot_fail;

	printf("Flashing sparse image at offset " LBAFU "\n",
	       sparse.start);

	sparse.priv = &sparse_priv;
	err = write_sparse_image(&sparse, part_name, buffer,
				 response);
	if (!err)
		fastboot_okay(NULL, response);
}

void fastboot_block_flash_write(const char *part_name, void *download_buffer,
				u32 download_bytes, char *response)
{
	struct blk_desc *dev_desc;
	struct disk_partition part_info;

	if (CONFIG_IS_ENABLED(EFI_PARTITION) || CONFIG_IS_ENABLED(DOS_PARTITION)) {
		char interface[FASTBOOT_BLOCK_IFACE_LEN];
		const char *partition_name;
		int device;

		if (!parse_device_partition(part_name, interface, &device, &partition_name, NULL)) {
#if CONFIG_IS_ENABLED(EFI_PARTITION)
			if (is_partition_table_name(partition_name, CONFIG_FASTBOOT_GPT_NAME)) {
				fastboot_flash_gpt_partition_table(interface, device,
								   download_buffer, response);
				return;
			}
#endif

#if CONFIG_IS_ENABLED(DOS_PARTITION)
			if (is_partition_table_name(partition_name, CONFIG_FASTBOOT_MBR_NAME)) {
				fastboot_flash_mbr_partition_table(interface, device,
								   download_buffer, response);
				return;
			}
#endif
		}
	}

	if (fastboot_block_get_part_info(part_name, &dev_desc, &part_info, response) < 0)
		return;

	if (is_sparse_image(download_buffer)) {
		fastboot_block_write_sparse_image(dev_desc, &part_info, part_name,
						  download_buffer, response);
	} else {
		fastboot_block_write_raw_image(dev_desc, &part_info, part_name,
					       download_buffer, download_bytes, response);
	}
}
