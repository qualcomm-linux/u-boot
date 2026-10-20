// SPDX-License-Identifier: BSD-3-Clause
/*
 * Qualcomm SPMI bus driver
 *
 * (C) Copyright 2015 Mateusz Kulikowski <mateusz.kulikowski@gmail.com>
 *
 * Loosely based on Little Kernel driver
 */

#include <dm.h>
#include <errno.h>
#include <fdtdec.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <spmi/spmi.h>

/* PMIC Arbiter configuration registers */
#define PMIC_ARB_VERSION 0x0000
#define PMIC_ARB_VERSION_V2_MIN 0x20010000
#define PMIC_ARB_VERSION_V3_MIN 0x30000000
#define PMIC_ARB_VERSION_V5_MIN 0x50000000
#define PMIC_ARB_VERSION_V7_MIN	0x70000000
#define PMIC_ARB_VERSION_V8_MIN	0x80000000

/*
 * Upper bound on how long to wait for SPMI_REG_STATUS to become non-zero.
 * A real transaction completes in microseconds; this is deliberately
 * several orders of magnitude larger so it can only fire on genuinely
 * stuck hardware.
 */
#define SPMI_STATUS_TIMEOUT_US	100000

#define PMIC_ARB_FEATURES		0x0004
#define PMIC_ARB_FEATURES_PERIPH_MASK	GENMASK(10, 0)

#define APID_MAP_OFFSET_V1_V2_V3 (0x800)
#define APID_MAP_OFFSET_V5 (0x900)
#define APID_MAP_OFFSET_V7 (0x2000)
/*
 * v8 differs from v7 in three ways that all matter for a single register
 * read. All three are taken from Linux's drivers/spmi/spmi-pmic-arb.c,
 * which does support v8:
 *
 *  1. the APID map is indexed from the "chnl_map" region, not from "core":
 *       pmic_arb_apid_map_offset_v8() = 0x4 * n, whose own comment reads
 *       "For v8, offset is from chnl_map base register, not core."
 *  2. observer channels are per-EE as well as per-APID:
 *       0x40000 * ee + 0x20 * apid  (pmic_arb_offset_v8())
 *  3. read/write channels use a 0x200 stride, not v7's 0x1000:
 *       0x200 * apid  (pmic_arb_offset_v8())
 *
 * Ownership also moves: v5/v7 read it from "cnfg" + 0x4*n, but v8 has a
 * dedicated "chnl_owner" region (pmic_arb_apid_owner_v8(),
 * pmic_arb_get_bus_resources_v8() requires reg-name "chnl_owner").
 */
#define SPMI_V8_OBS_EE_OFFSET(ee)	((ee) * 0x40000)
#define SPMI_V8_OBS_CH_OFFSET(chnl)	((chnl) * 0x20)
#define SPMI_V8_RW_CH_OFFSET(chnl)	((chnl) * 0x200)
#define SPMI_V8_PPID_MASK		0x1fff
#define ARB_CHANNEL_OFFSET(n) (0x4 * (n))
#define SPMI_CH_OFFSET(chnl) ((chnl) * 0x8000)
#define SPMI_V5_OBS_CH_OFFSET(chnl) ((chnl) * 0x80)
#define SPMI_V7_OBS_CH_OFFSET(chnl) ((chnl) * 0x20)
#define SPMI_V5_RW_CH_OFFSET(chnl) ((chnl) * 0x10000)
#define SPMI_V7_RW_CH_OFFSET(chnl) ((chnl) * 0x1000)

#define SPMI_OWNERSHIP_PERIPH2OWNER(x)	((x) & 0x7)

#define SPMI_REG_CMD0 0x0
#define SPMI_REG_CONFIG 0x4
#define SPMI_REG_STATUS 0x8
#define SPMI_REG_WDATA 0x10
#define SPMI_REG_RDATA 0x18

#define SPMI_CMD_OPCODE_SHIFT 27
#define SPMI_CMD_SLAVE_ID_SHIFT 20
#define SPMI_CMD_ADDR_SHIFT 12
#define SPMI_CMD_ADDR_OFFSET_SHIFT 4
#define SPMI_CMD_BYTE_CNT_SHIFT 0

#define SPMI_CMD_EXT_REG_WRITE_LONG 0x00
#define SPMI_CMD_EXT_REG_READ_LONG 0x01

#define SPMI_STATUS_DONE 0x1

#define SPMI_MAX_CHANNELS 128
#define SPMI_MAX_CHANNELS_V5	512
#define SPMI_MAX_CHANNELS_V7	1024
#define SPMI_MAX_SLAVES 16
#define SPMI_MAX_PERIPH 256

#define SPMI_CHANNEL_READ_ONLY	BIT(31)
#define SPMI_CHANNEL_VALID	BIT(30)
#define SPMI_CHANNEL_MASK	0xffff

enum arb_ver {
	V1 = 1,
	V2,
	V3,
	V5 = 5,
	V7 = 7,
	V8 = 8
};

/*
 * PMIC arbiter version 5 uses different register offsets for read/write vs
 * observer channels.
 */
enum pmic_arb_channel {
	PMIC_ARB_CHANNEL_RW,
	PMIC_ARB_CHANNEL_OBS,
};

struct msm_spmi_priv {
	phys_addr_t arb_chnl;  /* ARB channel mapping base */
	phys_addr_t spmi_chnls; /* SPMI channels */
	phys_addr_t spmi_obs;  /* SPMI observer */
	phys_addr_t spmi_cnfg;  /* SPMI config */
	phys_addr_t apid_owner; /* v8 only: APID owner table ("chnl_owner") */
	u32 owner;	/* Current owner */
	unsigned int max_channels; /* Max channels */
	/* SPMI channel map */
	uint32_t channel_map[SPMI_MAX_SLAVES][SPMI_MAX_PERIPH];
	/* SPMI bus arbiter version */
	u32 arb_ver;
};

static u32 pmic_arb_fmt_cmd_v1(u8 opc, u8 sid, u8 pid, u8 off)
{
	return (opc << 27) | (sid << 20) | (pid << 12) | (off << 4) | 1;
}

static u32 pmic_arb_fmt_cmd_v2(u8 opc, u8 off)
{
	return (opc << 27) | (off << 4) | 1;
}

static int msm_spmi_write(struct udevice *dev, int usid, int pid, int off,
			  uint8_t val)
{
	struct msm_spmi_priv *priv = dev_get_priv(dev);
	unsigned channel;
	unsigned int ch_offset;
	uint32_t reg = 0;
	int timeout_us;

	if (usid >= SPMI_MAX_SLAVES)
		return -EIO;
	if (pid >= SPMI_MAX_PERIPH)
		return -EIO;
	if (!(priv->channel_map[usid][pid] & SPMI_CHANNEL_VALID))
		return -EINVAL;
	if (priv->channel_map[usid][pid] & SPMI_CHANNEL_READ_ONLY)
		return -EPERM;

	channel = priv->channel_map[usid][pid] & SPMI_CHANNEL_MASK;

	debug("%s: [%d:%d] %s: channel %d\n", dev->name, usid, pid, __func__, channel);

	switch (priv->arb_ver) {
	case V1:
		ch_offset = SPMI_CH_OFFSET(channel);

		reg = pmic_arb_fmt_cmd_v1(SPMI_CMD_EXT_REG_WRITE_LONG,
					  usid, pid, off);
		break;

	case V2:
		ch_offset = SPMI_CH_OFFSET(channel);

		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_WRITE_LONG, off);
		break;

	case V5:
		ch_offset = SPMI_V5_RW_CH_OFFSET(channel);

		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_WRITE_LONG, off);
		break;

	case V7:
		ch_offset = SPMI_V7_RW_CH_OFFSET(channel);

		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_WRITE_LONG, off);
		break;

	case V8:
		/*
		 * v8 r/w stride is 0x200, not v7's 0x1000
		 * (Linux pmic_arb_offset_v8()).
		 */
		ch_offset = SPMI_V8_RW_CH_OFFSET(channel);

		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_WRITE_LONG, off);
		break;
	}

	/* Disable IRQ mode for the current channel*/
	writel(0x0, priv->spmi_chnls + ch_offset + SPMI_REG_CONFIG);

	/* Write single byte */
	writel(val, priv->spmi_chnls + ch_offset + SPMI_REG_WDATA);

	/* Send write command */
	writel(reg, priv->spmi_chnls + ch_offset + SPMI_REG_CMD0);

	/* Wait till CMD DONE status, bounded so an unresponsive or
	 * wrongly-mapped channel returns an error instead of hanging boot
	 * forever (SPMI is used very early; nothing above this imposes a
	 * timeout otherwise).
	 */
	reg = 0;
	timeout_us = SPMI_STATUS_TIMEOUT_US;
	while (!reg && timeout_us--) {
		reg = readl(priv->spmi_chnls + ch_offset +
			    SPMI_REG_STATUS);
		udelay(1);
	}
	if (!reg) {
		printf("SPMI write timeout: status never set (usid %d off 0x%x)\n",
		       usid, off);
		return -ETIMEDOUT;
	}

	if (reg ^ SPMI_STATUS_DONE) {
		printf("SPMI write failure.\n");
		return -EIO;
	}

	return 0;
}

static int msm_spmi_read(struct udevice *dev, int usid, int pid, int off)
{
	struct msm_spmi_priv *priv = dev_get_priv(dev);
	unsigned channel;
	unsigned int ch_offset;
	uint32_t reg = 0;
	int timeout_us;

	if (usid >= SPMI_MAX_SLAVES)
		return -EIO;
	if (pid >= SPMI_MAX_PERIPH)
		return -EIO;
	if (!(priv->channel_map[usid][pid] & SPMI_CHANNEL_VALID))
		return -EINVAL;

	channel = priv->channel_map[usid][pid] & SPMI_CHANNEL_MASK;

	debug("%s: [%d:%d] %s: channel %d\n", dev->name, usid, pid, __func__, channel);

	switch (priv->arb_ver) {
	case V1:
		ch_offset = SPMI_CH_OFFSET(channel);

		/* Prepare read command */
		reg = pmic_arb_fmt_cmd_v1(SPMI_CMD_EXT_REG_READ_LONG,
					  usid, pid, off);
		break;

	case V2:
		ch_offset = SPMI_CH_OFFSET(channel);

		/* Prepare read command */
		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_READ_LONG, off);
		break;

	case V5:
		ch_offset = SPMI_V5_OBS_CH_OFFSET(channel);

		/* Prepare read command */
		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_READ_LONG, off);
		break;

	case V7:
		ch_offset = SPMI_V7_OBS_CH_OFFSET(channel);

		/* Prepare read command */
		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_READ_LONG, off);
		break;

	case V8:
		/*
		 * v8 observer channels are per-EE as well as per-APID
		 * (Linux pmic_arb_offset_v8()). Omitting the EE term is
		 * what makes a v8 arbiter answer with FAILURE/DENIED
		 * instead of DONE.
		 */
		ch_offset = SPMI_V8_OBS_EE_OFFSET(priv->owner) +
			    SPMI_V8_OBS_CH_OFFSET(channel);

		/* Prepare read command */
		reg = pmic_arb_fmt_cmd_v2(SPMI_CMD_EXT_REG_READ_LONG, off);
		break;
	}

	/* Disable IRQ mode for the current channel*/
	writel(0x0, priv->spmi_obs + ch_offset + SPMI_REG_CONFIG);

	/* Request read */
	writel(reg, priv->spmi_obs + ch_offset + SPMI_REG_CMD0);

	/* Wait till CMD DONE status - bounded; see the write path for why. */
	reg = 0;
	timeout_us = SPMI_STATUS_TIMEOUT_US;
	while (!reg && timeout_us--) {
		reg = readl(priv->spmi_obs + ch_offset + SPMI_REG_STATUS);
		udelay(1);
	}
	if (!reg) {
		printf("SPMI read timeout: status never set (usid %d off 0x%x)\n",
		       usid, off);
		return -ETIMEDOUT;
	}

	if (reg ^ SPMI_STATUS_DONE) {
		printf("SPMI read failure.\n");
		return -EIO;
	}

	/* Read the data */
	return readl(priv->spmi_obs + ch_offset +
				SPMI_REG_RDATA) & 0xFF;
}

static struct dm_spmi_ops msm_spmi_ops = {
	.read = msm_spmi_read,
	.write = msm_spmi_write,
};

/*
 * In order to allow multiple EEs to write to a single PPID in arbiter
 * version 5 and 7, there is more than one APID mapped to each PPID.
 * The owner field for each of these mappings specifies the EE which is
 * allowed to write to the APID.
 */
static void msm_spmi_channel_map_v5(struct msm_spmi_priv *priv, unsigned int i,
				    uint8_t slave_id, uint8_t pid)
{
	phys_addr_t owner_addr;
	uint32_t cnfg;
	uint8_t owner;
	bool prev_valid;
	uint32_t prev_read_only;

	if (priv->arb_ver == V5)
		owner_addr = priv->spmi_cnfg + 0x700 + ARB_CHANNEL_OFFSET(i);
	else /* V7 */
		owner_addr = priv->spmi_cnfg + ARB_CHANNEL_OFFSET(i);

	cnfg = readl(owner_addr);
	owner = SPMI_OWNERSHIP_PERIPH2OWNER(cnfg);
	prev_valid = priv->channel_map[slave_id][pid] & SPMI_CHANNEL_VALID;
	prev_read_only = priv->channel_map[slave_id][pid] & SPMI_CHANNEL_READ_ONLY;

	if (!prev_valid) {
		/* First PPID mapping */
		priv->channel_map[slave_id][pid] = i | SPMI_CHANNEL_VALID;
		if (owner != priv->owner)
			priv->channel_map[slave_id][pid] |= SPMI_CHANNEL_READ_ONLY;

	} else if (owner == priv->owner) {
		/*
		 * Found a channel owned by our EE - ALWAYS switch to it!
		 * even if we already have a mapping, we must prefer the one
		 * owned by our EE to avoid hardware access violations.
		 */
		priv->channel_map[slave_id][pid] = i | SPMI_CHANNEL_VALID;
		/* Clear READ_ONLY flag since we own this channel */

	} else if (prev_read_only) {
		/*
		 * Previous mapping was read-only and this one is also not ours.
		 * Update to this channel.
		 */
		priv->channel_map[slave_id][pid] = i | SPMI_CHANNEL_VALID | SPMI_CHANNEL_READ_ONLY;

	}
	/* else: Previous was writable and owned by us, this one isn't - keep previous */
}

static int msm_spmi_probe(struct udevice *dev)
{
	struct msm_spmi_priv *priv = dev_get_priv(dev);
	phys_addr_t core_addr;
	u32 hw_ver;
	int i;

	core_addr = dev_read_addr_name(dev, "core");
	priv->spmi_chnls = dev_read_addr_name(dev, "chnls");
	priv->spmi_obs = dev_read_addr_name(dev, "obsrvr");
	dev_read_u32(dev, "qcom,ee", &priv->owner);

	hw_ver = readl(core_addr + PMIC_ARB_VERSION);

	if (hw_ver < PMIC_ARB_VERSION_V3_MIN) {
		priv->arb_ver = V2;
		priv->arb_chnl = core_addr + APID_MAP_OFFSET_V1_V2_V3;
		priv->max_channels = SPMI_MAX_CHANNELS;
	} else if (hw_ver < PMIC_ARB_VERSION_V5_MIN) {
		priv->arb_ver = V3;
		priv->arb_chnl = core_addr + APID_MAP_OFFSET_V1_V2_V3;
		priv->max_channels = SPMI_MAX_CHANNELS;
	} else if (hw_ver < PMIC_ARB_VERSION_V7_MIN) {
		priv->arb_ver = V5;
		priv->arb_chnl = core_addr + APID_MAP_OFFSET_V5;
		priv->max_channels = min_t(u32, readl(core_addr + PMIC_ARB_FEATURES) &
						PMIC_ARB_FEATURES_PERIPH_MASK,
					   SPMI_MAX_CHANNELS_V5);
		priv->spmi_cnfg = dev_read_addr_name(dev, "cnfg");
	} else if (hw_ver < PMIC_ARB_VERSION_V8_MIN) {
		/* TOFIX: handle second bus */
		priv->arb_ver = V7;
		priv->arb_chnl = core_addr + APID_MAP_OFFSET_V7;
		priv->max_channels = min_t(u32, readl(core_addr + PMIC_ARB_FEATURES) &
						PMIC_ARB_FEATURES_PERIPH_MASK,
					   SPMI_MAX_CHANNELS_V7);
		priv->spmi_cnfg = dev_read_addr_name(dev, "cnfg");
	} else {
		/*
		 * v8. Before this case existed, v8 fell into the V7 branch
		 * above and was silently mis-programmed: probe succeeded,
		 * the channel scan produced a garbage map, and the first
		 * transfer came back without STATUS_DONE ("SPMI read
		 * failure"). This chip's arbiter is confirmed v8: its DT
		 * compatible is "qcom,kaanapali-spmi-pmic-arb",
		 * "qcom,glymur-spmi-pmic-arb", and the Linux binding
		 * (qcom,glymur-spmi-pmic-arb.yaml) documents the Glymur
		 * arbiter as implementing HW version 8.
		 *
		 * TOFIX: like V7 above, only the first bus is handled. v8
		 * splits the APID space across up to 4 buses (Linux
		 * pmic_arb_init_apid_v8() walks PMIC_ARB_FEATURES + 4*i to
		 * find each bus's base_apid/apid_count). We assume bus 0,
		 * so base_apid = 0 and the owner-table index equals the
		 * APID.
		 */
		priv->arb_ver = V8;
		/*
		 * The APID map is indexed from "chnl_map", not from
		 * "core" - this is the single biggest difference from v7
		 * and the reason the V7 path cannot work here.
		 */
		priv->arb_chnl = dev_read_addr_name(dev, "chnl_map");
		priv->max_channels = SPMI_MAX_CHANNELS_V7;
		priv->spmi_cnfg = dev_read_addr_name(dev, "cnfg");
		/* v8 keeps ownership in its own region, not in "cnfg". */
		priv->apid_owner = dev_read_addr_name(dev, "chnl_owner");
		if (priv->apid_owner == FDT_ADDR_T_NONE) {
			printf("spmi: v8 requires a \"chnl_owner\" reg region\n");
			return -EINVAL;
		}
	}

	dev_dbg(dev, "PMIC Arb Version-%d (%#x)\n", hw_ver >> 28, hw_ver);

	if (priv->arb_chnl == FDT_ADDR_T_NONE ||
	    priv->spmi_chnls == FDT_ADDR_T_NONE ||
	    priv->spmi_obs == FDT_ADDR_T_NONE)
		return -EINVAL;

	dev_dbg(dev, "priv->arb_chnl address (%#08llx)\n", priv->arb_chnl);
	dev_dbg(dev, "priv->spmi_chnls address (%#08llx)\n", priv->spmi_chnls);
	dev_dbg(dev, "priv->spmi_obs address (%#08llx)\n", priv->spmi_obs);
	/* Scan peripherals connected to each SPMI channel */
	for (i = 0; i < priv->max_channels; i++) {
		uint32_t periph = readl(priv->arb_chnl + ARB_CHANNEL_OFFSET(i));
		uint8_t slave_id;
		uint8_t pid;

		if (priv->arb_ver == V8) {
			/*
			 * v8 packs a 13-bit PPID = (sid << 8) | (addr >> 8)
			 * (Linux PMIC_ARB_V8_PPID_MASK and
			 * pmic_arb_read_apid_map_v8()), and bit 31 is the
			 * IRQ-owner flag rather than part of the id. A zero
			 * entry means the APID is unused.
			 */
			uint32_t ppid = periph & SPMI_V8_PPID_MASK;

			if (!periph)
				continue;

			slave_id = (ppid >> 8) & 0xf;
			pid = ppid & 0xff;
		} else {
			slave_id = (periph & 0xf0000) >> 16;
			pid = (periph & 0xff00) >> 8;
		}

		switch (priv->arb_ver) {
		case V2:
		case V3:
			priv->channel_map[slave_id][pid] = i | SPMI_CHANNEL_VALID;
			break;

		case V5:
		case V7:
			msm_spmi_channel_map_v5(priv, i, slave_id, pid);
			break;

		case V8:
			/*
			 * v8 reads ownership from its own table, indexed
			 * (apid - base_apid); we assume bus 0 so
			 * base_apid = 0 (Linux pmic_arb_apid_owner_v8()).
			 */
			priv->channel_map[slave_id][pid] = i | SPMI_CHANNEL_VALID;
			if (SPMI_OWNERSHIP_PERIPH2OWNER(readl(priv->apid_owner +
							       ARB_CHANNEL_OFFSET(i))) != priv->owner)
				priv->channel_map[slave_id][pid] |= SPMI_CHANNEL_READ_ONLY;
			break;
		}
	}
	return 0;
}

static const struct udevice_id msm_spmi_ids[] = {
	{ .compatible = "qcom,spmi-pmic-arb" },
	/*
	 * Echo/SA535M's arbiter node (echo.dtsi) carries
	 * "qcom,kaanapali-spmi-pmic-arb", "qcom,glymur-spmi-pmic-arb" and
	 * would otherwise never bind: Linux's own match table
	 * (spmi-pmic-arb.c) includes "qcom,glymur-spmi-pmic-arb" for the
	 * same reason.
	 */
	{ .compatible = "qcom,glymur-spmi-pmic-arb" },
	{ }
};

U_BOOT_DRIVER(msm_spmi) = {
	.name = "msm_spmi",
	.id = UCLASS_SPMI,
	.of_match = msm_spmi_ids,
	.ops = &msm_spmi_ops,
	.probe = msm_spmi_probe,
	.priv_auto = sizeof(struct msm_spmi_priv),
};
