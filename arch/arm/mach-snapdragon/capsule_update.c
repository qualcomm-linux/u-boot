// SPDX-License-Identifier: GPL-2.0+
/*
 * Capsule update support for Qualcomm boards.
 *
 * Copyright (c) 2024 Linaro Ltd.
 * Author: Casey Connolly <casey.connolly@linaro.org>
 */

#define pr_fmt(fmt) "QCOM-FMP: " fmt

#include <dm/device.h>
#include <dm/uclass.h>
#include <efi.h>
#include <efi_loader.h>
#include <malloc.h>
#include <mmc.h>
#include <scsi.h>
#include <part.h>
#include <linux/err.h>

#include "qcom-priv.h"

/*
 * fw_images[] and update_info are built at runtime from the board's GPT
 * partition table (see qcom_configure_capsule_updates()): the set of updatable
 * firmware components varies per board, and U-Boot itself may live on one of
 * several partitions depending on how it was booted.
 *
 * qcom_image_map[] maps each partition base name to a fixed capsule image_index
 * and a slot-independent firmware name. U-Boot's own image is image_index 1 in
 * the same table, split into boot-source-gated rows so the right partition is
 * picked for the running boot source: uefi/xbl (or the legacy "aboot" alias)
 * when flashed as XBL, boot when chainloaded from ABL.
 */

/**
 * struct qcom_image_map - maps a GPT partition to a capsule FMP image
 * @partition_base: base partition name, without the _a/_b slot suffix
 * @fw_name_base: slot-independent firmware name; the FMP GUID is derived
 *                from this, so it must stay constant across A/B toggles
 * @image_index: fixed image index for this component, frozen forever once
 *               a capsule ships referencing it. image_index 1 is reserved for
 *               U-Boot's own image (the three boot-source-gated rows below);
 *               exactly one of those is ever selected per boot.
 * @boot_source: if non-zero, this row is only eligible when qcom_boot_source
 *               matches. Gates U-Boot's own image: uefi/xbl need
 *               QCOM_BOOT_SOURCE_XBL, boot needs QCOM_BOOT_SOURCE_ANDROID.
 *               0 means any boot source (every real firmware component).
 * @alias_base: optional second base name this row also matches (the legacy
 *              "aboot" alias for U-Boot on the xbl partition); NULL for none.
 *              Matched both slotted ("<alias>_a"/"_b") and bare.
 * @match_nonslotted: also match the bare, non-slotted @partition_base. Some
 *                    boards don't use A/B for U-Boot's partition (e.g. a
 *                    single "boot").
 * @image_type_id: fixed FMP image_type_id, hardcoded to the component's A-slot
 *                 partition type GUID. Constant across boards for a given
 *                 component, so one capsule applies to every variant instead of
 *                 the board-specific GUID efi_gen_capsule_guids() would derive
 *                 from the DT compatible. U-Boot's three rows share one GUID
 *                 (they're mutually exclusive per boot); every row is non-zero
 *                 so efi_gen_capsule_guids() skips derivation for the table.
 *
 * Every row matches "<partition_base>_a" and "<partition_base>_b".
 * @alias_base and @match_nonslotted reproduce the uefi/xbl/aboot/boot name
 * handling find_target_partition() used before U-Boot's image joined this
 * table; the firmware component rows leave both unset.
 */
struct qcom_image_map {
	const char *partition_base;
	const u16 *fw_name_base;
	u8 image_index;
	enum qcom_boot_source boot_source;
	const char *alias_base;
	bool match_nonslotted;
	efi_guid_t image_type_id;
};

/*
 * Fixed image_type_id for each component: its A-slot partition type GUID.
 * Constant across boards, so one capsule applies to every variant. uefi/xbl/
 * boot (U-Boot's own image) share QCOM_UBOOT_IMAGE_TYPE_GUID since only one is
 * ever selected per boot.
 */
#define QCOM_UBOOT_IMAGE_TYPE_GUID \
	EFI_GUID(0x400ffdcd, 0x22e0, 0x47e7, 0x9a, 0x23, 0xf1, 0x6e, 0xd9, 0x38, 0x23, 0x88)
#define QCOM_XBL_IMAGE_TYPE_GUID \
	EFI_GUID(0xdea0ba2c, 0xcbdd, 0x4805, 0xb4, 0xf9, 0xf4, 0x28, 0x25, 0x1c, 0x3e, 0x98)
#define QCOM_XBL_CONFIG_IMAGE_TYPE_GUID \
	EFI_GUID(0x5a325ae4, 0x4276, 0xb66d, 0x0a, 0xdd, 0x34, 0x94, 0xdf, 0x27, 0x70, 0x6a)
#define QCOM_UEFI_SECAPP_IMAGE_TYPE_GUID \
	EFI_GUID(0xbe8a7e08, 0x1b7a, 0x4cae, 0x99, 0x3a, 0xd5, 0xb7, 0xfb, 0x55, 0xb3, 0xc2)
#define QCOM_TZ_IMAGE_TYPE_GUID \
	EFI_GUID(0xa053aa7f, 0x40b8, 0x4b1c, 0xba, 0x08, 0x2f, 0x68, 0xac, 0x71, 0xa4, 0xf4)
#define QCOM_HYP_IMAGE_TYPE_GUID \
	EFI_GUID(0xe1a6a689, 0x0c8d, 0x4cc6, 0xb4, 0xe8, 0x55, 0xa4, 0x32, 0x0f, 0xbd, 0x8a)
#define QCOM_AOP_IMAGE_TYPE_GUID \
	EFI_GUID(0xd69e90a5, 0x4cab, 0x0071, 0xf6, 0xdf, 0xab, 0x97, 0x7f, 0x14, 0x1a, 0x7f)
#define QCOM_DEVCFG_IMAGE_TYPE_GUID \
	EFI_GUID(0xf65d4b16, 0x343d, 0x4e25, 0xaa, 0xfc, 0xbe, 0x99, 0xb6, 0x55, 0x6a, 0x6d)
#define QCOM_QUPFW_IMAGE_TYPE_GUID \
	EFI_GUID(0x21d1219f, 0x2ed1, 0x4ab4, 0x93, 0x0a, 0x41, 0xa1, 0x6a, 0xe7, 0x5f, 0x7f)
#define QCOM_XBL_RAMDUMP_IMAGE_TYPE_GUID \
	EFI_GUID(0x0382f197, 0xe41f, 0x4e84, 0xb1, 0x8b, 0x0b, 0x56, 0x4a, 0xea, 0xd8, 0x75)
#define QCOM_CPUCP_IMAGE_TYPE_GUID \
	EFI_GUID(0x1e8615bd, 0x6d8c, 0x41ad, 0xb3, 0xea, 0x50, 0xe8, 0xbf, 0x40, 0xe4, 0x3f)
#define QCOM_SHRM_IMAGE_TYPE_GUID \
	EFI_GUID(0xcb74ca22, 0x2f0d, 0x4b82, 0xa1, 0xd6, 0xc4, 0x21, 0x3f, 0x34, 0x8d, 0x73)
#define QCOM_IMAGEFV_IMAGE_TYPE_GUID \
	EFI_GUID(0x17911177, 0xc9e6, 0x4372, 0x93, 0x3c, 0x80, 0x4b, 0x67, 0x8e, 0x66, 0x6f)
#define QCOM_MULTIIMGOEM_IMAGE_TYPE_GUID \
	EFI_GUID(0xe126a436, 0x757e, 0x42d0, 0x8d, 0x19, 0x0f, 0x36, 0x2f, 0x7a, 0x62, 0xb8)
#define QCOM_APDP_IMAGE_TYPE_GUID \
	EFI_GUID(0xe6e98da2, 0xe22a, 0x4d12, 0xab, 0x33, 0x16, 0x9e, 0x7d, 0xea, 0xa5, 0x07)
#define QCOM_RPM_IMAGE_TYPE_GUID \
	EFI_GUID(0x098df793, 0xd712, 0x413d, 0x9d, 0x4e, 0x89, 0xd7, 0x11, 0x77, 0x22, 0x28)

static const struct qcom_image_map qcom_image_map[] = {
	/*
	 * U-Boot's own image (image_index 1). Exactly one of these rows is
	 * selected per boot:
	 *  - boot_source gating makes the uefi/xbl (XBL) rows and the boot
	 *    (ANDROID) row mutually exclusive;
	 *  - uefi is listed before xbl, so it wins image_index 1 when both
	 *    partitions exist and the generic "xbl" row below stays a normal
	 *    updatable component;
	 *  - when U-Boot is on xbl (no uefi), the xbl row here claims that
	 *    partition first and the component row is skipped, since one
	 *    partition can't be two capsule images.
	 * All three rows share one image_type_id (uefi's A-slot type GUID): only
	 * one is ever selected per boot, so there's no ambiguity, and a single
	 * "U-Boot" capsule can self-update whichever partition U-Boot booted from.
	 */
	{ "uefi", u"UBOOT_UEFI_PARTITION", 1, QCOM_BOOT_SOURCE_XBL,     NULL,    false,
	  QCOM_UBOOT_IMAGE_TYPE_GUID },
	{ "xbl",  u"UBOOT_XBL_PARTITION",  1, QCOM_BOOT_SOURCE_XBL,     "aboot", false,
	  QCOM_UBOOT_IMAGE_TYPE_GUID },
	{ "boot", u"UBOOT_BOOT_PARTITION", 1, QCOM_BOOT_SOURCE_ANDROID, NULL,    true,
	  QCOM_UBOOT_IMAGE_TYPE_GUID },

	{ "xbl",         u"QCOM-XBL",          2, 0, NULL, false,
	  QCOM_XBL_IMAGE_TYPE_GUID },
	{ "xbl_config",  u"QCOM-XBL-CONFIG",   3, 0, NULL, false,
	  QCOM_XBL_CONFIG_IMAGE_TYPE_GUID },
	{ "uefisecapp",  u"QCOM-UEFI-SECAPP",  4, 0, NULL, false,
	  QCOM_UEFI_SECAPP_IMAGE_TYPE_GUID },
	{ "tz",          u"QCOM-TZ",           5, 0, NULL, false,
	  QCOM_TZ_IMAGE_TYPE_GUID },
	{ "hyp",         u"QCOM-HYP",          6, 0, NULL, false,
	  QCOM_HYP_IMAGE_TYPE_GUID },
	{ "aop",         u"QCOM-AOP",          7, 0, NULL, false,
	  QCOM_AOP_IMAGE_TYPE_GUID },
	{ "devcfg",      u"QCOM-DEVCFG",       8, 0, NULL, false,
	  QCOM_DEVCFG_IMAGE_TYPE_GUID },
	{ "qupfw",       u"QCOM-QUPFW",        9, 0, NULL, false,
	  QCOM_QUPFW_IMAGE_TYPE_GUID },
	{ "xbl_ramdump", u"QCOM-XBL-RAMDUMP", 10, 0, NULL, false,
	  QCOM_XBL_RAMDUMP_IMAGE_TYPE_GUID },
	{ "cpucp",       u"QCOM-CPUCP",       11, 0, NULL, false,
	  QCOM_CPUCP_IMAGE_TYPE_GUID },
	{ "shrm",        u"QCOM-SHRM",        12, 0, NULL, false,
	  QCOM_SHRM_IMAGE_TYPE_GUID },
	{ "imagefv",     u"QCOM-IMAGEFV",     13, 0, NULL, false,
	  QCOM_IMAGEFV_IMAGE_TYPE_GUID },
	{ "multiimgoem", u"QCOM-MULTIIMGOEM", 14, 0, NULL, false,
	  QCOM_MULTIIMGOEM_IMAGE_TYPE_GUID },
	{ "apdp",        u"QCOM-APDP",        15, 0, NULL, false,
	  QCOM_APDP_IMAGE_TYPE_GUID },
	{ "rpm",         u"QCOM-RPM",         16, 0, NULL, false,
	  QCOM_RPM_IMAGE_TYPE_GUID },
};

/*
 * Fixed-capacity, no heap allocation:
 * one slot per qcom_image_map[] row. Only one of the three image_index 1
 * (U-Boot) rows is ever used, so this is larger than the real maximum.
 * update_info.num_images is set at runtime to the number actually found, which
 * varies since not every board has every component.
 */
static struct efi_fw_image fw_images[ARRAY_SIZE(qcom_image_map)];

struct efi_capsule_update_info update_info = {
	.dfu_string = NULL,
	.num_images = 0,
	.images = fw_images,
};

/*
 * Worst case: every fw_images[] entry on its own LUN (each needing an "&scsi
 * N=" or "&mmc N=" group separator) with a full-length "<name> part <num>"
 * token. See qcom_build_dfu_string().
 */
#define QCOM_DFU_STRING_LEN	(64 * ARRAY_SIZE(qcom_image_map))

/* LSB first */
struct part_slot_status {
	u16: 2;
	u16 active : 1;
	u16: 3;
	u16 successful : 1;
	u16 unbootable : 1;
	u16 tries_remaining : 4;
};

enum ab_slot {
	SLOT_NONE,
	SLOT_A,
	SLOT_B,
};

/* Shamelessly copied from lib/efi_loader/efi_device_path.c @ 33 */
/*
 * Determine if an MMC device is an SD card.
 *
 * @desc	block device descriptor
 * Return:	true if the device is an SD card
 */
static bool is_sd(struct blk_desc *desc)
{
	struct mmc *mmc = find_mmc_device(desc->devnum);

	if (!mmc)
		return false;

	return IS_SD(mmc) != 0U;
}

/**
 * struct qcom_partition_info - a selected partition for one component
 * @name: partition name as found on disk, e.g. "tz_a"
 * @part_num: partition number on the block device
 * @devnum: block device number @name lives on
 * @uclass_id: uclass (UCLASS_SCSI/UCLASS_MMC) of @devnum. @devnum and
 *             @uclass_id are carried through instead of a struct blk_desc
 *             pointer so partitions found on different LUNs (e.g. Kodiak/Lemans
 *             split xbl/xbl_config onto one LUN, tz/hyp/aop onto another) group
 *             back into the correct per-device DFU string tokens
 * @fw_name: slot-independent FMP name (from the matched map row's fw_name_base)
 * @image_index: fw_images[] image_index (from the matched map row)
 * @image_type_id: fixed FMP GUID (from the matched map row's image_type_id)
 */
struct qcom_partition_info {
	char name[32];
	u32 part_num;
	int devnum;
	enum uclass_id uclass_id;
	const u16 *fw_name;
	u8 image_index;
	efi_guid_t image_type_id;
};

/*
 * Used only within qcom_configure_capsule_updates() (scan -> resolve -> build).
 */
static struct qcom_partition_info qcom_partitions[ARRAY_SIZE(qcom_image_map)];

/**
 * struct qcom_slot_cand - one candidate partition for a map row's slot
 * @part_num: partition number, or -1 if no partition filled this slot
 * @devnum: block device number the partition was found on
 * @uclass_id: uclass of that block device
 * @name: partition name as found on disk
 * @active: A/B "active" flag from the partition's slot status
 */
struct qcom_slot_cand {
	int part_num;
	int devnum;
	enum uclass_id uclass_id;
	char name[32];
	bool active;
};

/**
 * struct qcom_row_cand - candidate partitions for one qcom_image_map[] row
 * @slot: indexed by enum ab_slot (SLOT_NONE for a non-slotted/alias-bare match)
 */
struct qcom_row_cand {
	struct qcom_slot_cand slot[3];
};

/*
 * Candidate table, one entry per qcom_image_map[] row, accumulated across every
 * scanned block device before a single resolve pass picks one partition per
 * row.
 */
static struct qcom_row_cand qcom_cands[ARRAY_SIZE(qcom_image_map)];

/**
 * qcom_match_partition() - test a partition name against a map row
 * @name: partition name as found on disk
 * @map: candidate map row
 * @slot: output, which A/B slot (SLOT_NONE for a non-slotted / bare-alias match)
 *
 * Every row matches "<partition_base>_a"/"<partition_base>_b". U-Boot's rows
 * may also match a bare non-slotted name (@match_nonslotted) and a legacy alias
 * base (@alias_base, both slotted and bare).
 *
 * Return: true if @name belongs to @map
 */
static bool qcom_match_partition(const char *name,
				 const struct qcom_image_map *map,
				 enum ab_slot *slot)
{
	const char *base[2] = { map->partition_base, map->alias_base };
	/*
	 * partition_base only matches a bare, non-slotted name when the row
	 * opts in via @match_nonslotted; the legacy @alias_base always does.
	 */
	const bool match_bare[2] = { map->match_nonslotted, true };
	char cand[36];
	int i;

	for (i = 0; i < 2; i++) {
		if (!base[i])
			continue;

		snprintf(cand, sizeof(cand), "%s_a", base[i]);
		if (!strcmp(name, cand)) {
			*slot = SLOT_A;
			return true;
		}
		snprintf(cand, sizeof(cand), "%s_b", base[i]);
		if (!strcmp(name, cand)) {
			*slot = SLOT_B;
			return true;
		}
		if (match_bare[i] && !strcmp(name, base[i])) {
			*slot = SLOT_NONE;
			return true;
		}
	}

	return false;
}

/**
 * qcom_scan_device() - record candidate partitions on one block device
 * @desc: block device to scan
 * @cands: candidate table, one entry per qcom_image_map[] row
 *
 * Single pass over every partition on @desc. Each partition is checked against
 * every eligible map row (rows whose boot_source doesn't match the live
 * qcom_boot_source are skipped) and recorded into that row's A/B/NONE slot. One
 * partition can match several rows ("xbl_a" feeds both the U-Boot xbl row and
 * the generic xbl component row), so the inner loop doesn't stop at the first
 * match.
 *
 * Candidates from an earlier device aren't overwritten, so the first device
 * wins when the same partition table is exposed on more than one (e.g. mirrored
 * UFS boot LUNs). The candidate table is global rather than per-device so the
 * later qcom_resolve_images() pass can apply uefi-over-xbl priority even when
 * the two partitions live on different LUNs.
 */
static void qcom_scan_device(struct blk_desc *desc, struct qcom_row_cand *cands)
{
	struct disk_partition info;
	struct part_slot_status *slot_status;
	int partnum, i;

	for (partnum = 1; !part_get_info(desc, partnum, &info); partnum++) {
		slot_status = (struct part_slot_status *)&info.type_flags;

		for (i = 0; i < ARRAY_SIZE(qcom_image_map); i++) {
			const struct qcom_image_map *m = &qcom_image_map[i];
			struct qcom_slot_cand *c;
			enum ab_slot slot;

			if (m->boot_source && m->boot_source != qcom_boot_source)
				continue;
			if (!qcom_match_partition((char *)info.name, m, &slot))
				continue;

			c = &cands[i].slot[slot];
			if (c->part_num >= 0)	/* first device wins */
				continue;

			c->part_num = partnum;
			c->active = !!slot_status->active;
			c->devnum = desc->devnum;
			c->uclass_id = desc->uclass_id;
			strlcpy(c->name, (char *)info.name, sizeof(c->name));
		}
	}
}

/**
 * qcom_pick_slot() - choose the best candidate slot for a map row
 * @rc: candidate slots for one row
 *
 * Priority: active A > active B > non-slotted > inactive A > inactive B.
 *
 * Return: the chosen enum ab_slot, or -1 if no candidate was recorded
 */
static int qcom_pick_slot(const struct qcom_row_cand *rc)
{
	if (rc->slot[SLOT_A].part_num >= 0 && rc->slot[SLOT_A].active)
		return SLOT_A;
	if (rc->slot[SLOT_B].part_num >= 0 && rc->slot[SLOT_B].active)
		return SLOT_B;
	if (rc->slot[SLOT_NONE].part_num >= 0)
		return SLOT_NONE;
	if (rc->slot[SLOT_A].part_num >= 0)
		return SLOT_A;
	if (rc->slot[SLOT_B].part_num >= 0)
		return SLOT_B;

	return -1;
}

/**
 * qcom_resolve_images() - pick one partition per map row from the candidates
 * @cands: candidate table filled by qcom_scan_device() across every device
 * @partitions: output array; room for ARRAY_SIZE(qcom_image_map) entries
 *
 * Walks qcom_image_map[] in order, so U-Boot's own rows (image_index 1) resolve
 * first and land in partitions[0]. Two dedup rules:
 *
 *  1. One selected partition per image_index. The three U-Boot rows share
 *     image_index 1, so once one is picked the others are skipped -- this is
 *     the uefi-over-xbl priority (uefi is listed first).
 *  2. Never claim the same physical partition twice. When U-Boot is on xbl (no
 *     uefi), the U-Boot xbl row claims that partition and the generic xbl
 *     component row, matching the same partition, is skipped.
 *
 * A row with no candidate on this board is left out, so num_images varies per
 * board.
 *
 * Return: number of partitions written to @partitions
 */
static u32 qcom_resolve_images(struct qcom_row_cand *cands,
			       struct qcom_partition_info *partitions)
{
	u32 count = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(qcom_image_map); i++) {
		const struct qcom_image_map *m = &qcom_image_map[i];
		struct qcom_slot_cand *c;
		struct qcom_partition_info *p;
		bool dup = false;
		int sel;
		u32 j;

		if (m->boot_source && m->boot_source != qcom_boot_source)
			continue;

		sel = qcom_pick_slot(&cands[i]);
		if (sel < 0)
			continue;
		c = &cands[i].slot[sel];

		/* rule 1: one selected partition per image_index */
		for (j = 0; j < count; j++) {
			if (partitions[j].image_index == m->image_index) {
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		/* rule 2: never claim the same physical partition twice */
		for (j = 0; j < count; j++) {
			if (partitions[j].devnum == c->devnum &&
			    partitions[j].uclass_id == c->uclass_id &&
			    partitions[j].part_num == (u32)c->part_num) {
				dup = true;
				break;
			}
		}
		if (dup)
			continue;

		p = &partitions[count++];
		strlcpy(p->name, c->name, sizeof(p->name));
		p->part_num = c->part_num;
		p->devnum = c->devnum;
		p->uclass_id = c->uclass_id;
		p->fw_name = m->fw_name_base;
		p->image_index = m->image_index;
		guidcpy(&p->image_type_id, &m->image_type_id);

		log_debug("qcom capsule: %s -> image %u (part %u, dev %d)\n",
			  p->name, p->image_index, p->part_num, p->devnum);
	}

	return count;
}

/**
 * qcom_build_fw_images() - populate fw_images[] from the discovered partitions
 * @partitions: partitions from qcom_resolve_images(); entry 0 is U-Boot's own
 *              image (image_index 1), the rest are components
 * @num_partitions: number of valid entries in @partitions
 *
 * One fw_images[] entry per partition, in the same order, so fw_images[] and
 * @partitions stay index-aligned. .image_type_id is set from the matched map
 * row's fixed A-slot GUID. Since fw_images[0] is then non-zero,
 * efi_gen_capsule_guids() skips derivation for the whole table (it derives only
 * when entry 0 is still zero), so the hardcoded GUIDs stand.
 *
 * Return: number of images written (equal to @num_partitions)
 */
static u32 qcom_build_fw_images(const struct qcom_partition_info *partitions,
				u32 num_partitions)
{
	u32 i;

	for (i = 0; i < num_partitions; i++) {
		fw_images[i].fw_name = (u16 *)partitions[i].fw_name;
		fw_images[i].image_index = partitions[i].image_index;
		guidcpy(&fw_images[i].image_type_id, &partitions[i].image_type_id);
	}

	return num_partitions;
}

/**
 * qcom_build_dfu_string() - build the dfu_alt_info string for the partitions
 * @partitions: partitions accumulated across every scanned block device
 * @num_partitions: number of valid entries in @partitions; also the number of
 *                  valid fw_images[] entries, with matching indices
 * @buf: output buffer
 * @buf_size: size of @buf
 *
 * Builds an "interface devstring=alt;alt&interface devstring=alt" string (see
 * dfu_config_interfaces() in drivers/dfu/dfu.c for the grammar), grouping
 * partitions by originating device (devnum+uclass_id) with '&' so partitions
 * split across LUNs (e.g. Kodiak/Lemans has xbl/xbl_config on one UFS LUN and
 * the rest on another) still resolve. efi_firmware_raw_set_image() always calls
 * dfu_write_by_alt() with a NULL interface/devstring, which forces the
 * multi-interface '&'-grouped parser, so a single-group string would silently
 * drop every partition on a LUN other than the first.
 *
 * dfu_alt_add() assigns each token's dfu_alt_num by its position in the parsed
 * string, so fw_images[i].dfu_alt_num is recorded here as tokens are appended,
 * not derived from image_index (which isn't contiguous or string-ordered once
 * components are missing). Relies on qcom_build_fw_images() having already
 * filled fw_images[0..num_partitions) from this same @partitions array.
 *
 * Return: true on success, false if @buf was too small
 */
static bool qcom_build_dfu_string(struct qcom_partition_info *partitions,
				  u32 num_partitions, char *buf, size_t buf_size)
{
	int prev_devnum = -1;
	enum uclass_id prev_uclass_id = UCLASS_INVALID;
	u32 i;
	size_t len;

	buf[0] = '\0';
	len = 0;

	for (i = 0; i < num_partitions; i++) {
		struct qcom_partition_info *p = &partitions[i];
		char token[48];
		bool new_group = p->devnum != prev_devnum ||
				  p->uclass_id != prev_uclass_id;

		if (new_group) {
			char prefix[24];

			if (i != 0)
				len = strlcat(buf, "&", buf_size);

			switch (p->uclass_id) {
			case UCLASS_SCSI:
				snprintf(prefix, sizeof(prefix), "scsi %d=", p->devnum);
				break;
			case UCLASS_MMC:
				snprintf(prefix, sizeof(prefix), "mmc %d=", p->devnum);
				break;
			default:
				log_err("qcom capsule: unsupported storage uclass %d for %s\n",
					p->uclass_id, p->name);
				return false;
			}
			len = strlcat(buf, prefix, buf_size);
		} else {
			len = strlcat(buf, ";", buf_size);
		}

		if (p->uclass_id == UCLASS_MMC)
			snprintf(token, sizeof(token), "%s part %u %u",
				 p->name, p->devnum, p->part_num);
		else
			snprintf(token, sizeof(token), "%s part %u",
				 p->name, p->part_num);
		len = strlcat(buf, token, buf_size);

		if (len >= buf_size) {
			log_err("qcom capsule: dfu_alt_info string truncated\n");
			return false;
		}

		fw_images[i].dfu_alt_num = i;
		prev_devnum = p->devnum;
		prev_uclass_id = p->uclass_id;
	}

	return true;
}

/**
 * qcom_configure_capsule_updates() - Configure the DFU string and fw_images[]
 * for capsule updates
 *
 * Scans every probed block device, recording candidates for every qcom_image_map[]
 * row, and builds fw_images[] and the matching dfu_alt_info string. 
 */
void qcom_configure_capsule_updates(void)
{
	static char dfu_string[QCOM_DFU_STRING_LEN];
	struct qcom_partition_info *partitions = qcom_partitions;
	u32 num_partitions;
	struct udevice *dev;
	bool have_ufs = false;
	int i, ret;

	memset(qcom_cands, 0, sizeof(qcom_cands));
	for (i = 0; i < ARRAY_SIZE(qcom_image_map); i++) {
		qcom_cands[i].slot[SLOT_NONE].part_num = -1;
		qcom_cands[i].slot[SLOT_A].part_num = -1;
		qcom_cands[i].slot[SLOT_B].part_num = -1;
	}
	memset(partitions, 0, sizeof(qcom_partitions));

	if (IS_ENABLED(CONFIG_SCSI)) {
		ret = scsi_scan(false);
		if (ret) {
			debug("Failed to scan SCSI devices: %d\n", ret);
			return;
		}
	}

	/*
	 * Check to see if we have UFS storage, if so firmware MUST be on it and
	 * we can skip all non-UFS block devices.
	 */
	uclass_foreach_dev_probe(UCLASS_UFS, dev) {
		have_ufs = true;
		break;
	}

	uclass_foreach_dev_probe(UCLASS_BLK, dev) {
		struct blk_desc *desc;

		if (device_get_uclass_id(dev) != UCLASS_BLK)
			continue;

		desc = dev_get_uclass_plat(dev);

		/* If we have a UFS then don't look at any other block devices */
		if (have_ufs) {
			if (device_get_uclass_id(dev->parent->parent) != UCLASS_UFS)
				continue;
		} else if (IS_ENABLED(CONFIG_MMC) && is_sd(desc)) {
			/* If we don't have UFS, then firmware is on the eMMC */
			log_debug("skipped SD-Card (devnum %d)\n", desc->devnum);
			continue;
		}

		if (!desc || desc->part_type == PART_TYPE_UNKNOWN)
			continue;

		qcom_scan_device(desc, qcom_cands);
	}

	num_partitions = qcom_resolve_images(qcom_cands, partitions);
	if (!num_partitions) {
		log_err("qcom capsule: no updatable partitions found\n");
		return;
	}
	if (partitions[0].image_index != 1)
		log_warning("qcom capsule: U-Boot's own partition not found; configuring components only\n");

	qcom_build_fw_images(partitions, num_partitions);
	if (!qcom_build_dfu_string(partitions, num_partitions, dfu_string,
				   sizeof(dfu_string)))
		return;

	log_debug("dfu_alt_info: %s\n", dfu_string);

	update_info.num_images = num_partitions;
	update_info.dfu_string = dfu_string;
}

/**
 * efi_firmware_get_dfu_alt_num() - resolve an image_index to its DFU alt number
 * @image_index: fw_images[].image_index to resolve
 *
 * Strong override of the __weak default in lib/efi_loader/efi_firmware.c.
 * Qualcomm's fw_images[] is built at runtime and its image_index values aren't
 * guaranteed contiguous (a board may lack some components), so dfu_alt_num
 * can't be derived positionally -- look up the value recorded by
 * qcom_build_dfu_string() for the matching image_index instead, mirroring the
 * scan efi_firmware_get_image_type_id() already does.
 *
 * Falls back to the weak default's image_index - 1 if not found, which
 * shouldn't happen since every image_index passed in comes from fw_images[].
 *
 * Return: the DFU alt setting number for @image_index
 */
u8 efi_firmware_get_dfu_alt_num(u8 image_index)
{
	struct efi_fw_image *fw_array = update_info.images;
	int i;

	for (i = 0; i < update_info.num_images; i++) {
		if (fw_array[i].image_index == image_index)
			return fw_array[i].dfu_alt_num;
	}

	return image_index - 1;
}
