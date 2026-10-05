// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm main-stage kernel FIT board-config selection
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Picks the kernel FIT's per-board config node by overriding the generic
 * weak hook board_fit_config_name_match() (boot/common_fit.c), which
 * fit_find_config_node() calls once per /configurations subnode when
 * bootm is given no explicit "#config" suffix. Each node's existing
 * description = "qcom,board-id=<board_id>,<variant>" string is matched
 * against live hardware detection.
 *
 * qcom,board-id's first cell packs four bitfields. The generic binding
 * (dts/upstream/Bindings/arm/qcom.yaml) documents only the low three:
 * bits 23-16 Platform Version Major, bits 15-8 Platform Version Minor,
 * bits 7-0 Platform Type (exact match) - and calls bits 31-24 "Unused".
 * On this chip's .its, bits 31-24 are NOT unused - they're a family/
 * sub-platform discriminator (confirmed on real hardware: live
 * params.subtype reads 0x2 on echo-idp-qmp5-emmc, matching that board's
 * board-id top byte (0x2010022 -> 0x02) exactly, while every other
 * family in the .its has a different, internally-consistent top byte -
 * ATP/base-IDP/RCM=0x00, QMP4=0x01, QMP5(all versions)=0x02, QTP=0x04,
 * LGA(all versions)=0x05). Without this fourth field, Platform
 * Type+Version alone is ambiguous: echo-idp-emmc (0x10022) and
 * echo-idp-qmp5-emmc (0x2010022) share the same Type (0x22) and Version
 * (Major=0x01,Minor=0x00), differing only in this top byte - omitting it
 * caused a real misdetection on hardware (QMP5 board boot-selected the
 * base-IDP config instead, since echo-idp-emmc's conf-* node is listed
 * first in the .its).
 *
 * qcom_hwdetect_get_params()'s board_version is packed from SMEM the same
 * way as Major/Minor here - (major << 4) | minor - so those two bytes are
 * packed identically before comparing against it.
 *
 * The second array cell (the "variant" suffix - e.g. 0x602/0x605/0x400)
 * packs a DDR-size nibble at bits 10-8, the same bit position and width
 * ABL's GetBoardMatchDtb() (QcomModulePkg/Library/BootLib/LocateDeviceTree.c,
 * DDR_MASK=0x700/DDR_SHIFT=8) reads out of this exact field, compared
 * against a live-detected DDR type packed at the same bit position
 * (QcomModulePkg/Library/BootLib/Board.c). qcom_hwdetect.c's ddr_size_type
 * uses the same enum ordinals as ABL's DdrType, so (variant & 0x700) >> 8
 * is directly comparable to params.ddr_size_type. If a node's DDR nibble
 * is non-zero and doesn't match the live value, it is rejected even though
 * the first cell matched, so iteration falls through to a better-matching
 * node later in the FIT - this makes variant selection correct regardless
 * of node order. A zero DDR nibble (base nodes) is never rejected on this
 * basis. Any non-DDR bits of the variant cell (board-revision-only
 * distinctions) are still not checked - nodes differing only there remain
 * first-listed-wins.
 *
 * All matching data lives in the .its file's description properties -
 * this code carries no per-board table, so adding a new board only
 * requires adding a conf-* node to the .its file, never a U-Boot source
 * change.
 *
 * This hook is only reachable when CONFIG_MULTI_DTB_FIT is enabled, which
 * also wraps U-Boot's own control DTB in a FIT container. That used to
 * panic with "Couldn't find a valid memory map!" on real hardware, because
 * the early SMEM-based memory-map parser in board_fdt_blob_setup() ran
 * against the still-wrapped blob. board.c's board_fdt_blob_setup() now
 * unwraps that FIT itself before the memory parser runs, so this hook can
 * be used for the kernel FIT as well without reintroducing that panic.
 */

#include <image.h>
#include <log.h>
#include <vsprintf.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "qcom_hwdetect.h"

#define QCOM_BOARD_ID_PREFIX "qcom,board-id="

#define QCOM_BOARD_ID_PLATFORM_TYPE(id)	((id) & 0xff)
#define QCOM_BOARD_ID_VERSION_MAJOR(id)	(((id) >> 16) & 0xff)
#define QCOM_BOARD_ID_VERSION_MINOR(id)	(((id) >> 8) & 0xff)
#define QCOM_BOARD_ID_FAMILY(id)		(((id) >> 24) & 0xff)

/* DDR-size nibble within the board-id variant cell, matching ABL's
 * DDR_MASK/DDR_SHIFT convention (see file header).
 */
#define QCOM_BOARD_ID_VARIANT_DDR_MASK		0x700
#define QCOM_BOARD_ID_VARIANT_DDR_SHIFT		8
#define QCOM_BOARD_ID_VARIANT_DDR(variant) \
	(((variant) & QCOM_BOARD_ID_VARIANT_DDR_MASK) >> QCOM_BOARD_ID_VARIANT_DDR_SHIFT)

/*
 * Parses a "qcom,board-id=<board_id>,<variant>" description string. Both
 * cells are hex (0x-prefixed), matching the .its file's own format.
 * @board_id: raw first cell (Platform Type/Version Major/Minor packed,
 *	      see file header)
 * @variant: raw second cell (DDR-size/revision suffix, see file header)
 */
static bool qcom_parse_board_id(const char *desc, u32 *board_id, u32 *variant)
{
	char *sep;

	if (strncmp(desc, QCOM_BOARD_ID_PREFIX, strlen(QCOM_BOARD_ID_PREFIX)))
		return false;

	desc += strlen(QCOM_BOARD_ID_PREFIX);
	*board_id = hextoul(desc, &sep);
	if (sep == desc || *sep != ',')
		return false;

	desc = sep + 1;
	*variant = hextoul(desc, &sep);
	if (sep == desc)
		return false;

	return true;
}

int board_fit_config_name_match(const char *name)
{
	struct qcom_hw_params params;
	u32 board_id, variant, ddr_nibble;
	u32 platform_type, packed_version, family;

	/*
	 * This hook is also reached once, harmlessly, from the control-DTB
	 * unwrap path in board.c's board_fdt_blob_setup() (via
	 * locate_dtb_in_fit() -> fit_find_config_node()) before qcom,hwinfo
	 * has been probed - log_debug rather than printf so that expected
	 * failure doesn't look like a real rejection on the console.
	 */
	if (qcom_hwdetect_get_params(&params)) {
		log_debug("qcom_fit: hwdetect failed, config '%s' rejected\n",
			  name);
		return -EINVAL;
	}

	if (!qcom_parse_board_id(name, &board_id, &variant)) {
		log_debug("qcom_fit: '%s' has no parseable qcom,board-id, skipping\n",
			  name);
		return -EINVAL;
	}

	platform_type = QCOM_BOARD_ID_PLATFORM_TYPE(board_id);
	packed_version = (QCOM_BOARD_ID_VERSION_MAJOR(board_id) << 4) |
			  QCOM_BOARD_ID_VERSION_MINOR(board_id);
	family = QCOM_BOARD_ID_FAMILY(board_id);
	ddr_nibble = QCOM_BOARD_ID_VARIANT_DDR(variant);

	log_debug("qcom_fit: checking '%s' (board_id=0x%x -> type=0x%x,version=0x%x,family=0x%x variant=0x%x ddr=0x%x) against live hw (platform=0x%x,version=0x%x subtype=0x%x ddr_size_type=0x%x)\n",
		  name, board_id, platform_type, packed_version, family, variant,
		  ddr_nibble, params.platform, params.board_version, params.subtype,
		  params.ddr_size_type);

	if (platform_type != params.platform ||
	    packed_version != params.board_version ||
	    family != params.subtype)
		return -EINVAL;

	if (ddr_nibble && ddr_nibble != params.ddr_size_type) {
		log_debug("qcom_fit: '%s' ddr mismatch (node=0x%x live=0x%x), rejecting\n",
			  name, ddr_nibble, params.ddr_size_type);
		return -EINVAL;
	}

	log_debug("qcom_fit: MATCH on '%s'\n", name);
	return 0;
}
