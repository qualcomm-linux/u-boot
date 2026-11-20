// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm SA8775P DWMAC SGMII SerDes PHY driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <dm.h>
#include <dm/device_compat.h>
#include <generic-phy.h>
#include <phy.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <clk.h>
#include <power/regulator.h>
#include <asm/io.h>

#include "phy-qcom-qmp-pcs-sgmii.h"
#include "phy-qcom-qmp-qserdes-com-v5.h"
#include "phy-qcom-qmp-qserdes-txrx-v5.h"

/* QSERDES sub-block offsets from the SerDes MMIO base. */
#define QSERDES_COM_BASE		0x000	/* PLL / COM block */
#define QSERDES_TX_BASE			0x400	/* TX lane */
#define QSERDES_RX_BASE			0x600	/* RX lane */
#define QSERDES_PCS_BASE		0xc00	/* PCS block */

#define QSERDES_COM_C_READY			BIT(0)
#define QSERDES_PCS_READY			BIT(0)
#define QSERDES_PCS_SGMIIPHY_READY		BIT(7)
#define QSERDES_COM_C_PLL_LOCKED		BIT(1)

#define PLL_POLL_RETRIES			500
#define PLL_POLL_SLEEP_US			1500

struct qcom_sgmii_eth_priv {
	void __iomem	*base;
	struct udevice	*vdda_0p9;
	struct clk	 clk;
	phy_interface_t	 interface;
	bool		 powered;
};

static inline void serdes_writel(struct qcom_sgmii_eth_priv *priv,
				 u32 val, u32 offset)
{
	writel(val, priv->base + offset);
}

static inline u32 serdes_readl(struct qcom_sgmii_eth_priv *priv, u32 offset)
{
	return readl(priv->base + offset);
}

/* Program the 1.25-Gbps SGMII/1000BASE-X table. */
static void qcom_sgmii_eth_init_1g(struct qcom_sgmii_eth_priv *priv)
{
	/* PCS: assert SW_RESET, enable POWER_DOWN_CONTROL */
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_POWER_DOWN_CONTROL);

	/* COM (PLL) registers — mode0 = 1G */
	serdes_writel(priv, 0x0F, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_IVCO);
	serdes_writel(priv, 0x06, QSERDES_COM_BASE + QSERDES_V5_COM_CP_CTRL_MODE0);
	serdes_writel(priv, 0x16, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_RCTRL_MODE0);
	serdes_writel(priv, 0x36, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_CCTRL_MODE0);
	serdes_writel(priv, 0x1A, QSERDES_COM_BASE + QSERDES_V5_COM_SYSCLK_EN_SEL);
	serdes_writel(priv, 0x0A, QSERDES_COM_BASE + QSERDES_V5_COM_LOCK_CMP1_MODE0);
	serdes_writel(priv, 0x1A, QSERDES_COM_BASE + QSERDES_V5_COM_LOCK_CMP2_MODE0);
	serdes_writel(priv, 0x82, QSERDES_COM_BASE + QSERDES_V5_COM_DEC_START_MODE0);
	serdes_writel(priv, 0x55, QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START1_MODE0);
	serdes_writel(priv, 0x55, QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START2_MODE0);
	serdes_writel(priv, 0x03, QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START3_MODE0);
	serdes_writel(priv, 0x24, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE1_MODE0);
	serdes_writel(priv, 0x02, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE2_MODE0);
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE_INITVAL2);
	serdes_writel(priv, 0x04, QSERDES_COM_BASE + QSERDES_V5_COM_HSCLK_SEL);
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_HSCLK_HS_SWITCH_SEL);
	serdes_writel(priv, 0x0A, QSERDES_COM_BASE + QSERDES_V5_COM_CORECLK_DIV_MODE0);
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_CORE_CLK_EN);
	serdes_writel(priv, 0xB9, QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE1_MODE0);
	serdes_writel(priv, 0x1E, QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE2_MODE0);
	serdes_writel(priv, 0x11, QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_HSCLK_SEL);

	/* TX lane registers */
	serdes_writel(priv, 0x05, QSERDES_TX_BASE + QSERDES_V5_TX_TX_BAND);
	serdes_writel(priv, 0x0A, QSERDES_TX_BASE + QSERDES_V5_TX_SLEW_CNTL);
	serdes_writel(priv, 0x09, QSERDES_TX_BASE + QSERDES_V5_TX_RES_CODE_LANE_OFFSET_TX);
	serdes_writel(priv, 0x09, QSERDES_TX_BASE + QSERDES_V5_TX_RES_CODE_LANE_OFFSET_RX);
	serdes_writel(priv, 0x05, QSERDES_TX_BASE + QSERDES_V5_TX_LANE_MODE_1);
	serdes_writel(priv, 0x00, QSERDES_TX_BASE + QSERDES_V5_TX_LANE_MODE_3);
	serdes_writel(priv, 0x12, QSERDES_TX_BASE + QSERDES_V5_TX_RCV_DETECT_LVL_2);
	serdes_writel(priv, 0x0C, QSERDES_TX_BASE + QSERDES_V5_TX_TRAN_DRVR_EMP_EN);

	/* RX lane registers */
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FO_GAIN);
	serdes_writel(priv, 0x06, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_SO_GAIN);
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_FO_GAIN);
	serdes_writel(priv, 0x7F, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_SO_SATURATION_AND_ENABLE);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_COUNT_LOW);
	serdes_writel(priv, 0x01, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_COUNT_HIGH);
	serdes_writel(priv, 0x81, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_PI_CONTROLS);
	serdes_writel(priv, 0x80, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_PI_CTRL2);
	serdes_writel(priv, 0x04, QSERDES_RX_BASE + QSERDES_V5_RX_RX_TERM_BW);
	serdes_writel(priv, 0x08, QSERDES_RX_BASE + QSERDES_V5_RX_VGA_CAL_CNTRL2);
	serdes_writel(priv, 0x0F, QSERDES_RX_BASE + QSERDES_V5_RX_GM_CAL);
	serdes_writel(priv, 0x04, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL1);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL2);
	serdes_writel(priv, 0x4A, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL3);
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL4);
	serdes_writel(priv, 0x80, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_TSETTLE_LOW);
	serdes_writel(priv, 0x01, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_TSETTLE_HIGH);
	serdes_writel(priv, 0x20, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_MEASURE_TIME);
	serdes_writel(priv, 0x17, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_RX_OFFSET_ADAPTOR_CNTRL2);
	serdes_writel(priv, 0x0F, QSERDES_RX_BASE + QSERDES_V5_RX_SIGDET_CNTRL);
	serdes_writel(priv, 0x1E, QSERDES_RX_BASE + QSERDES_V5_RX_SIGDET_DEGLITCH_CNTRL);
	serdes_writel(priv, 0x05, QSERDES_RX_BASE + QSERDES_V5_RX_RX_BAND);
	serdes_writel(priv, 0xE0, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_LOW);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH2);
	serdes_writel(priv, 0x09, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH3);
	serdes_writel(priv, 0xB1, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH4);
	serdes_writel(priv, 0xE0, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_LOW);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH2);
	serdes_writel(priv, 0x09, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH3);
	serdes_writel(priv, 0xB1, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH4);
	serdes_writel(priv, 0xE0, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_LOW);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH2);
	serdes_writel(priv, 0x3B, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH3);
	serdes_writel(priv, 0xB7, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH4);
	serdes_writel(priv, 0x0C, QSERDES_RX_BASE + QSERDES_V5_RX_DCC_CTRL1);

	/* PCS final registers — release SW_RESET, start PHY */
	serdes_writel(priv, 0x0C, QSERDES_PCS_BASE + QPHY_PCS_LINE_RESET_TIME);
	serdes_writel(priv, 0x1F, QSERDES_PCS_BASE + QPHY_PCS_TX_LARGE_AMP_DRV_LVL);
	serdes_writel(priv, 0x03, QSERDES_PCS_BASE + QPHY_PCS_TX_SMALL_AMP_DRV_LVL);
	serdes_writel(priv, 0x83, QSERDES_PCS_BASE + QPHY_PCS_TX_MID_TERM_CTRL1);
	serdes_writel(priv, 0x08, QSERDES_PCS_BASE + QPHY_PCS_TX_MID_TERM_CTRL2);
	serdes_writel(priv, 0x0C, QSERDES_PCS_BASE + QPHY_PCS_SGMII_MISC_CTRL8);  /* 1G: 0x0C */
	serdes_writel(priv, 0x00, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_PHY_START);
}

/* Program the 3.125-Gbps 2500BASE-X table. */
static void qcom_sgmii_eth_init_2p5g(struct qcom_sgmii_eth_priv *priv)
{
	/* PCS: assert SW_RESET, enable POWER_DOWN_CONTROL */
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_POWER_DOWN_CONTROL);

	/* COM (PLL) registers — mode0 = 2.5G */
	serdes_writel(priv, 0x0F, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_IVCO);
	serdes_writel(priv, 0x06, QSERDES_COM_BASE + QSERDES_V5_COM_CP_CTRL_MODE0);
	serdes_writel(priv, 0x16, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_RCTRL_MODE0);
	serdes_writel(priv, 0x36, QSERDES_COM_BASE + QSERDES_V5_COM_PLL_CCTRL_MODE0);
	serdes_writel(priv, 0x1A, QSERDES_COM_BASE + QSERDES_V5_COM_SYSCLK_EN_SEL);
	serdes_writel(priv, 0x1A, QSERDES_COM_BASE + QSERDES_V5_COM_LOCK_CMP1_MODE0); /* differs */
	serdes_writel(priv, 0x41, QSERDES_COM_BASE + QSERDES_V5_COM_LOCK_CMP2_MODE0); /* differs */
	serdes_writel(priv, 0x7A, QSERDES_COM_BASE + QSERDES_V5_COM_DEC_START_MODE0); /* differs */
	serdes_writel(priv, 0x00,
		      QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START1_MODE0); /* differs */
	serdes_writel(priv, 0x20,
		      QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START2_MODE0); /* differs */
	serdes_writel(priv, 0x01,
		      QSERDES_COM_BASE + QSERDES_V5_COM_DIV_FRAC_START3_MODE0); /* differs */
	serdes_writel(priv, 0xA1, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE1_MODE0); /* differs */
	serdes_writel(priv, 0x02, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE2_MODE0);
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_VCO_TUNE_INITVAL2);
	serdes_writel(priv, 0x03, QSERDES_COM_BASE + QSERDES_V5_COM_HSCLK_SEL);       /* differs */
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_HSCLK_HS_SWITCH_SEL);
	serdes_writel(priv, 0x05,
		      QSERDES_COM_BASE + QSERDES_V5_COM_CORECLK_DIV_MODE0); /* differs */
	serdes_writel(priv, 0x00, QSERDES_COM_BASE + QSERDES_V5_COM_CORE_CLK_EN);
	serdes_writel(priv, 0xCD,
		      QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE1_MODE0); /* differs */
	serdes_writel(priv, 0x1C,
		      QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_CMP_CODE2_MODE0); /* differs */
	serdes_writel(priv, 0x11, QSERDES_COM_BASE + QSERDES_V5_COM_BIN_VCOCAL_HSCLK_SEL);

	/* TX lane registers */
	serdes_writel(priv, 0x04, QSERDES_TX_BASE + QSERDES_V5_TX_TX_BAND);           /* differs */
	serdes_writel(priv, 0x0A, QSERDES_TX_BASE + QSERDES_V5_TX_SLEW_CNTL);
	serdes_writel(priv, 0x09, QSERDES_TX_BASE + QSERDES_V5_TX_RES_CODE_LANE_OFFSET_TX);
	serdes_writel(priv, 0x02,
		      QSERDES_TX_BASE + QSERDES_V5_TX_RES_CODE_LANE_OFFSET_RX); /* differs */
	serdes_writel(priv, 0x05, QSERDES_TX_BASE + QSERDES_V5_TX_LANE_MODE_1);
	serdes_writel(priv, 0x00, QSERDES_TX_BASE + QSERDES_V5_TX_LANE_MODE_3);
	serdes_writel(priv, 0x12, QSERDES_TX_BASE + QSERDES_V5_TX_RCV_DETECT_LVL_2);
	serdes_writel(priv, 0x0C, QSERDES_TX_BASE + QSERDES_V5_TX_TRAN_DRVR_EMP_EN);

	/* RX lane registers */
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FO_GAIN);
	serdes_writel(priv, 0x06, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_SO_GAIN);
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_FO_GAIN);
	serdes_writel(priv, 0x7F, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_SO_SATURATION_AND_ENABLE);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_COUNT_LOW);
	serdes_writel(priv, 0x01, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_FASTLOCK_COUNT_HIGH);
	serdes_writel(priv, 0x81, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_PI_CONTROLS);
	serdes_writel(priv, 0x80, QSERDES_RX_BASE + QSERDES_V5_RX_UCDR_PI_CTRL2);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_RX_TERM_BW);        /* differs */
	serdes_writel(priv, 0x08, QSERDES_RX_BASE + QSERDES_V5_RX_VGA_CAL_CNTRL2);
	serdes_writel(priv, 0x0F, QSERDES_RX_BASE + QSERDES_V5_RX_GM_CAL);
	serdes_writel(priv, 0x04, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL1);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL2);
	serdes_writel(priv, 0x4A, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL3);
	serdes_writel(priv, 0x0A, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQU_ADAPTOR_CNTRL4);
	serdes_writel(priv, 0x80, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_TSETTLE_LOW);
	serdes_writel(priv, 0x01, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_TSETTLE_HIGH);
	serdes_writel(priv, 0x20, QSERDES_RX_BASE + QSERDES_V5_RX_RX_IDAC_MEASURE_TIME);
	serdes_writel(priv, 0x17, QSERDES_RX_BASE + QSERDES_V5_RX_RX_EQ_OFFSET_ADAPTOR_CNTRL1);
	serdes_writel(priv, 0x00, QSERDES_RX_BASE + QSERDES_V5_RX_RX_OFFSET_ADAPTOR_CNTRL2);
	serdes_writel(priv, 0x0F, QSERDES_RX_BASE + QSERDES_V5_RX_SIGDET_CNTRL);
	serdes_writel(priv, 0x1E, QSERDES_RX_BASE + QSERDES_V5_RX_SIGDET_DEGLITCH_CNTRL);
	serdes_writel(priv, 0x18, QSERDES_RX_BASE + QSERDES_V5_RX_RX_BAND);           /* differs */
	serdes_writel(priv, 0x18, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_LOW);    /* differs */
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH2);
	serdes_writel(priv, 0x0C, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH3);  /* differs */
	serdes_writel(priv, 0xB8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_00_HIGH4);  /* differs */
	serdes_writel(priv, 0xE0, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_LOW);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH2);
	serdes_writel(priv, 0x09, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH3);
	serdes_writel(priv, 0xB1, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_01_HIGH4);
	serdes_writel(priv, 0xE0, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_LOW);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH);
	serdes_writel(priv, 0xC8, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH2);
	serdes_writel(priv, 0x3B, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH3);
	serdes_writel(priv, 0xB7, QSERDES_RX_BASE + QSERDES_V5_RX_RX_MODE_10_HIGH4);
	serdes_writel(priv, 0x0C, QSERDES_RX_BASE + QSERDES_V5_RX_DCC_CTRL1);

	/* PCS final registers — release SW_RESET, start PHY */
	serdes_writel(priv, 0x0C, QSERDES_PCS_BASE + QPHY_PCS_LINE_RESET_TIME);
	serdes_writel(priv, 0x1F, QSERDES_PCS_BASE + QPHY_PCS_TX_LARGE_AMP_DRV_LVL);
	serdes_writel(priv, 0x03, QSERDES_PCS_BASE + QPHY_PCS_TX_SMALL_AMP_DRV_LVL);
	serdes_writel(priv, 0x83, QSERDES_PCS_BASE + QPHY_PCS_TX_MID_TERM_CTRL1);
	serdes_writel(priv, 0x08, QSERDES_PCS_BASE + QPHY_PCS_TX_MID_TERM_CTRL2);
	serdes_writel(priv, 0x8C, QSERDES_PCS_BASE + QPHY_PCS_SGMII_MISC_CTRL8);  /* 2.5G: 0x8C */
	serdes_writel(priv, 0x00, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_PHY_START);
}

static int qcom_sgmii_eth_pll_poll(struct udevice *dev,
				   struct qcom_sgmii_eth_priv *priv,
				   u32 offset, u32 bit,
				   const char *name)
{
	int i;

	for (i = 0; i < PLL_POLL_RETRIES; i++) {
		if (serdes_readl(priv, offset) & bit) {
			return 0;
		}
		udelay(PLL_POLL_SLEEP_US);
	}
	dev_err(dev, "SerDes PLL poll timed out waiting for %s\n", name);
	return -ETIMEDOUT;
}

static int qcom_sgmii_eth_pll_wait(struct udevice *dev,
				   struct qcom_sgmii_eth_priv *priv)
{
	int ret;

	/* Poll 1: COM C_READY — PLL output clock is ready */
	ret = qcom_sgmii_eth_pll_poll(dev, priv,
				      QSERDES_COM_BASE + QSERDES_V5_COM_C_READY_STATUS,
				      QSERDES_COM_C_READY,
				      "COM_C_READY");
	if (ret)
		return ret;

	/* Poll 2: PCS_READY — PCS block has completed reset */
	ret = qcom_sgmii_eth_pll_poll(dev, priv,
				      QSERDES_PCS_BASE + QPHY_PCS_PCS_READY_STATUS,
				      QSERDES_PCS_READY,
				      "PCS_READY");
	if (ret)
		return ret;

	/* Poll 3: SGMIIPHY_READY — SGMII PHY is operational */
	ret = qcom_sgmii_eth_pll_poll(dev, priv,
				      QSERDES_PCS_BASE + QPHY_PCS_PCS_READY_STATUS,
				      QSERDES_PCS_SGMIIPHY_READY,
				      "SGMIIPHY_READY");
	if (ret)
		return ret;

	/* Poll 4: PLL locked — VCO has locked to target frequency */
	ret = qcom_sgmii_eth_pll_poll(dev, priv,
				      QSERDES_COM_BASE + QSERDES_V5_COM_CMN_STATUS,
				      QSERDES_COM_C_PLL_LOCKED,
				      "PLL_LOCKED");
	return ret;
}

static int qcom_sgmii_eth_probe(struct udevice *dev)
{
	struct qcom_sgmii_eth_priv *priv = dev_get_priv(dev);
	fdt_addr_t base;
	int ret;

	/* Map SerDes register base from DT "reg" property */
	base = dev_read_addr(dev);
	if (base == FDT_ADDR_T_NONE) {
		dev_err(dev, "Missing 'reg' property for SerDes node\n");
		return -EINVAL;
	}
	priv->base = (void __iomem *)(uintptr_t)base;

	ret = clk_get_by_name(dev, "sgmi_ref", &priv->clk);
	if (ret) {
		dev_err(dev, "clk_get_by_name(sgmi_ref) failed: %d\n", ret);
		return ret;
	}

	ret = device_get_supply_regulator(dev, "vdda-0p9-supply", &priv->vdda_0p9);
	if (ret) {
		dev_err(dev, "device_get_supply_regulator(vdda-0p9) failed: %d\n", ret);
		return ret;
	}

	priv->interface = PHY_INTERFACE_MODE_SGMII;
	priv->powered = false;

	return 0;
}

static int qcom_sgmii_eth_calibrate(struct udevice *dev,
				    struct qcom_sgmii_eth_priv *priv,
				    phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
		qcom_sgmii_eth_init_1g(priv);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		qcom_sgmii_eth_init_2p5g(priv);
		break;
	default:
		dev_err(dev, "unsupported Ethernet interface: %d\n", interface);
		return -EOPNOTSUPP;
	}

	return qcom_sgmii_eth_pll_wait(dev, priv);
}

/* Enable local supplies and calibrate after generic phy-supply is enabled. */
static int qcom_sgmii_eth_power_on(struct phy *phy)
{
	struct udevice *dev = phy->dev;
	struct qcom_sgmii_eth_priv *priv = dev_get_priv(dev);
	int cleanup_ret;
	int ret;

	ret = regulator_set_enable(priv->vdda_0p9, true);
	if (ret) {
		dev_err(dev, "regulator_set_enable(vdda-0p9) failed: %d\n", ret);
		return ret;
	}

	ret = clk_enable(&priv->clk);
	if (ret) {
		dev_err(dev, "clk_enable(sgmi_ref) failed: %d\n", ret);
		cleanup_ret = regulator_set_enable(priv->vdda_0p9, false);
		if (cleanup_ret)
			dev_err(dev, "failed to disable vdda-0p9: %d\n",
				cleanup_ret);
		return ret;
	}

	ret = qcom_sgmii_eth_calibrate(dev, priv, priv->interface);
	if (ret) {
		cleanup_ret = clk_disable(&priv->clk);
		if (cleanup_ret)
			dev_err(dev, "failed to disable sgmi_ref: %d\n",
				cleanup_ret);

		cleanup_ret = regulator_set_enable(priv->vdda_0p9, false);
		if (cleanup_ret)
			dev_err(dev, "failed to disable vdda-0p9: %d\n",
				cleanup_ret);
		return ret;
	}

	priv->powered = true;

	return 0;
}

/*
 * SGMII and 1000BASE-X use the 1.25-Gbps SerDes table. 2500BASE-X uses
 * the 3.125-Gbps table. If the PHY is already powered, changing mode also
 * recalibrates the hardware and verifies PLL lock before caching the mode.
 */
static int qcom_sgmii_eth_set_mode(struct phy *phy, enum phy_mode mode,
				   int submode)
{
	struct udevice *dev = phy->dev;
	struct qcom_sgmii_eth_priv *priv = dev_get_priv(dev);
	phy_interface_t interface = submode;
	int ret;

	if (mode != PHY_MODE_ETHERNET)
		return -EOPNOTSUPP;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (interface == priv->interface)
		return 0;

	if (!priv->powered) {
		priv->interface = interface;
		return 0;
	}

	ret = qcom_sgmii_eth_calibrate(dev, priv, interface);
	if (ret)
		return ret;

	priv->interface = interface;

	return 0;
}

static int qcom_sgmii_eth_power_off(struct phy *phy)
{
	struct udevice *dev = phy->dev;
	struct qcom_sgmii_eth_priv *priv = dev_get_priv(dev);
	int first_err = 0;
	int ret;

	serdes_writel(priv, 0x08, QSERDES_PCS_BASE + QPHY_PCS_TX_MID_TERM_CTRL2);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	udelay(100);
	serdes_writel(priv, 0x00, QSERDES_PCS_BASE + QPHY_PCS_SW_RESET);
	serdes_writel(priv, 0x01, QSERDES_PCS_BASE + QPHY_PCS_PHY_START);

	ret = clk_disable(&priv->clk);
	if (ret) {
		dev_err(dev, "failed to disable sgmi_ref: %d\n", ret);
		first_err = ret;
	}

	ret = regulator_set_enable(priv->vdda_0p9, false);
	if (ret) {
		dev_err(dev, "failed to disable vdda-0p9: %d\n", ret);
		if (!first_err)
			first_err = ret;
	}

	if (!first_err)
		priv->powered = false;

	return first_err;
}

static const struct phy_ops qcom_sgmii_eth_ops = {
	.power_on	= qcom_sgmii_eth_power_on,
	.power_off	= qcom_sgmii_eth_power_off,
	.set_mode	= qcom_sgmii_eth_set_mode,
};

static const struct udevice_id qcom_sgmii_eth_ids[] = {
	{ .compatible = "qcom,sa8775p-dwmac-sgmii-phy" },
	{ }
};

U_BOOT_DRIVER(qcom_sgmii_eth) = {
	.name		= "qcom_sgmii_eth",
	.id		= UCLASS_PHY,
	.of_match	= qcom_sgmii_eth_ids,
	.probe		= qcom_sgmii_eth_probe,
	.ops		= &qcom_sgmii_eth_ops,
	.priv_auto	= sizeof(struct qcom_sgmii_eth_priv),
};
