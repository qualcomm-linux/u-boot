/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synopsys DesignWare XPCS register map, Amboseli USXGMII subset.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Transcribed from the Linux driver's private header,
 *   ref/amboseli-eth/pcs-xpcs-qcom.h
 * keeping the kernel's names so the two can be diffed. Only the registers and
 * fields the USXGMII bring-up path touches are here; the SGMII, 2500BASEX, EEE,
 * FUSA/safety and time-sync groups are omitted deliberately - this port targets
 * emac0 in USXGMII mode and unused defines are just surface area to get wrong.
 *
 * The window is emac0's "qcom-xpcs-handle" phandle target, pcs@22314000, size
 * 0x5100. Offsets here are byte offsets into that window and every access is a
 * plain 32-bit readl/writel: the kernel's qcom_xpcs_read()/write()
 * (pcs-xpcs-qcom.c:229-237) are exactly readl/writel(qxpcs->addr + reg), with no
 * MDIO indirection and no shifting. The registers themselves are 16-bit MMD
 * registers flat-mapped four bytes apart, which is why the offsets are the MMD
 * register number times four with the MMD selecting the 0x1000-aligned block.
 */

#ifndef _PCS_XPCS_QCOM_AMBOSELI_H
#define _PCS_XPCS_QCOM_AMBOSELI_H

/* Device identity. Both halves are 16-bit; ID1 is the high word. */
#define SYNOPSYS_XPCS_ID			0x7996ced0
#define SYNOPSYS_XPCS_MASK			0xffffffff

/* SR_XS_PCS - Clause 45 PCS MMD (MMD 3), flat-mapped at 0x0000. */
#define DW_SR_MII_PCS_CTRL1			0x0000
#define DW_SR_MII_PCS_STS1			0x0004
#define DW_SR_MII_PCS_DEV_ID1			0x0008
#define DW_SR_MII_PCS_DEV_ID2			0x000c
#define DW_SR_MII_PCS_CTRL2			0x001c

/* SR_MII_PCS_STS1: bit 2 is Receive Link Up (RLU). */
#define DW_SR_XS_PCS_STS1_RLU			BIT(2)
#define DW_SR_XS_PCS_STS1_FLT			BIT(7)	/* Fault (STS2 says which) */

/* SR_XS_PCS_STS2, Clause 45 register 3.8, so offset 0x0020 in this flat map (register number times four, MMD 3 at 0x0000). */
#define DW_SR_XS_PCS_STS2			0x0020
#define DW_SR_XS_PCS_STS2_TX_FLT		BIT(11)
#define DW_SR_XS_PCS_STS2_RX_FLT		BIT(10)
#define DW_SR_XS_PCS_STS2_DEV_PRESENT_SHIFT	14
#define DW_SR_XS_PCS_STS2_DEV_PRESENT_MASK	GENMASK(1, 0)

/* The offset is 0x2040, taken from the kernel's private header (ref/amboseli-eth/pcs-xpcs-qcom.h:20) - NOT 0x2004, which is VR_XS_PCS_DIG_CTRL2 and reads as zero. */
#define DW_VR_XS_PCS_DIG_STS			0x2040

/* Further status registers named by the kernel header, added during bring-up so a pass/fail diff has more than one place to show a difference. */
#define DW_SR_MII_PCS_STS3			0x0024
#define DW_SR_MII_PCS_LSTS			0x0060
#define DW_VR_MII_PCS_ICG_ERRCNT1		0x2044
#define DW_SR_MII_MMD_STS			0x4004

/* SR_MII_PCS_CTRL2 PCS type select. Clearing all four bits selects 10GBASE-R. */
#define PCS_TYPE_SEL_10GBR			GENMASK(3, 0)

/* SR_MII_PCS_CTRL1 */
#define LPM_EN					BIT(11)

/* VR_XS_PCS - vendor-specific PCS block. */
#define DW_VR_MII_PCS_DIG_CTRL1			0x2000
#define DW_VR_MII_PCS_KR_CTRL			0x201c

/* VR_MII_PCS_DIG_CTRL1 */
#define DW_USXGMII_RST				BIT(10)
#define DW_USXGMII_EN				BIT(9)

/* VR_MII_PCS_KR_CTRL: USXGMII rate select. Zero selects the 10G rate. */
#define USXG_MODE_SEL				GENMASK(12, 10)

/* SR_MII - Clause 22 / Clause 37 MII MMD, flat-mapped at 0x4000. */
#define DW_SR_MII_MMD_CTRL			0x4000

/* SR_MII_MMD_CTRL */
#define DW_LBE					BIT(14)	/* RX-clock loopback enable */
#define AN_CL37_EN				BIT(12)	/* Clause 37 autoneg enable */
#define DW_USXGMII_SS_MASK			(BIT(13) | BIT(6) | BIT(5))
#define DW_USXGMII_10000			(BIT(13) | BIT(6))
#define DW_USXGMII_5000				(BIT(13) | BIT(5))
#define DW_GMII_2500				(BIT(5))
#define DW_GMII_1000				(BIT(6))
#define DW_GMII_100				(BIT(13))
/* 10 Mbps is all three bits clear, hence no define - see the kernel's switch. */

/* VR_MII - vendor-specific MII block. */
#define DW_VR_MII_DIG_CTRL1			0x5000
#define DW_VR_MII_AN_CTRL			0x5004
#define DW_VR_MII_AN_INTR_STS			0x5008

/* VR_MII_AN_CTRL */
#define DW_VR_MII_AN_CTRL_TX_CONFIG_SHIFT	3
#define DW_VR_MII_TX_CONFIG_PHY_SIDE		0x1
#define DW_VR_MII_SGMII_LINK_STS		BIT(4)
#define DW_VR_MII_AN_INTR_EN			BIT(0)
#define DW_VR_MII_CTRL				BIT(8)

/* VR_MII_AN_INTR_STS, USXGMII fields. */
#define DW_VR_MII_ANCMPLT_INTR			BIT(0)
/* USXGMII does not use these fields, but pass 25 showed the external PHY sending Clause-37 style codewords (AN_INTR_STS 0x000a = FD set, SP 0x2 = 1000) instead of USXGMII ones, and no USXGMII AN completion. */
#define DW_VR_MII_AN_STS_C37_ANSGM_FD		BIT(1)
#define DW_VR_MII_AN_STS_C37_ANSGM_SP_SHIFT	2
#define DW_VR_MII_AN_STS_C37_ANSGM_SP		GENMASK(3, 2)
#define DW_VR_MII_C37_ANSGM_SP_10		0x0
#define DW_VR_MII_C37_ANSGM_SP_100		0x1
#define DW_VR_MII_C37_ANSGM_SP_1000		0x2

#define DW_VR_MII_USXG_ANSGM_SP_SHIFT		10
#define DW_VR_MII_USXG_ANSGM_SP			GENMASK(12, 10)
#define DW_VR_MII_USXG_ANSGM_SP_10		0x0
#define DW_VR_MII_USXG_ANSGM_SP_100		0x1
#define DW_VR_MII_USXG_ANSGM_SP_1000		0x2
#define DW_VR_MII_USXG_ANSGM_SP_10G		0x3
#define DW_VR_MII_USXG_ANSGM_SP_2P5G		0x4
#define DW_VR_MII_USXG_ANSGM_SP_5G		0x5
#define DW_VR_MII_USXG_ANSGM_SP_LNKSTS		BIT(14)

/* Bit 15 is the soft-reset field of several registers; it self-clears. */
#define SOFT_RST				BIT(15)
#define SW_RST_BIT_STATUS			0x8000

/* Timeouts. */
#define AMBOSELI_XPCS_RESET_RETRIES		32
#define AMBOSELI_XPCS_AN_TIMEOUT_MS		750
#define AMBOSELI_XPCS_RLU_TIMEOUT_MS		500

#endif /* _PCS_XPCS_QCOM_AMBOSELI_H */
