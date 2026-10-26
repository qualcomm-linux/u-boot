/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2023 Intel Coporation.
 */

#include <phy_interface.h>
#include <linux/bitops.h>

/* Core registers */

#define XGMAC_MAC_REGS_BASE 0x000

struct xgmac_mac_regs {
	u32 tx_configuration;			/* 0x000 */
	u32 rx_configuration;			/* 0x004 */
	u32 mac_packet_filter;			/* 0x008 */
	u32 unused_00c[(0x070 - 0x00c) / 4];	/* 0x00c */
	u32 q0_tx_flow_ctrl;			/* 0x070 */
	u32 unused_070[(0x090 - 0x074) / 4];	/* 0x074 */
	u32 rx_flow_ctrl;			/* 0x090 */
	u32 unused_094[(0x0a0 - 0x094) / 4];	/* 0x094 */
	u32 rxq_ctrl0;				/* 0x0a0 */
	u32 rxq_ctrl1;				/* 0x0a4 */
	u32 rxq_ctrl2;				/* 0x0a8 */
	u32 unused_0ac[(0x0d0 - 0x0ac) / 4];	/* 0x0ac */
	u32 lpi_control_status;			/* 0x0d0 */
	u32 lpi_timers_control;			/* 0x0d4 */
	u32 lpi_auto_entry_timer;		/* 0x0d8 */
	u32 us_tic_counter;			/* 0x0dc */
	u32 unused_0e0[(0x11c - 0x0e0) / 4];	/* 0x0e0 */
	u32 hw_feature0;			/* 0x11c */
	u32 hw_feature1;			/* 0x120 */
	u32 hw_feature2;			/* 0x124 */
	u32 hw_feature3;			/* 0x128 */
	u32 hw_feature4;			/* 0x12c */
	u32 unused_130[(0x140 - 0x130) / 4];	/* 0x130 */
	u32 mac_extended_conf;			/* 0x140 */
	u32 unused_144[(0x200 - 0x144) / 4];	/* 0x144 */
	u32 mdio_address;			/* 0x200 */
	u32 mdio_data;				/* 0x204 */
	u32 mdio_cont_write_addr;		/* 0x208 */
	u32 mdio_cont_write_data;		/* 0x20c */
	u32 mdio_cont_scan_port_enable;		/* 0x210 */
	u32 mdio_intr_status;			/* 0x214 */
	u32 mdio_intr_enable;			/* 0x218 */
	u32 mdio_port_cnct_dsnct_status;	/* 0x21c */
	u32 mdio_clause_22_port;		/* 0x220 */
	u32 unused_224[(0x300 - 0x224)	/ 4];	/* 0x224 */
	u32 address0_high;			/* 0x300 */
	u32 address0_low;			/* 0x304 */
};

/*
 * D291: upstream's value is 100000, and every one of the five places that uses
 * it treats it as milliseconds - three as "get_timer(start) < TIMEOUT" and two
 * as wait_for_bit_le32()'s timeout argument, which is also milliseconds. So the
 * constant named 100MS has always meant 100 *seconds*, and every one of those
 * waits is a hundred-second silent hang when the bit it wants never comes.
 *
 * That is what d246 hit: the board went quiet immediately after the pre-stop
 * dump of a ping and never came back. xgmac_stop() has two of these loops
 * back to back, so a frame left in the MTL RX queue costs 200 seconds of
 * silence. See the comment on the RX drain loop for why such a frame cannot
 * drain at stop time.
 */
#define XGMAC_TIMEOUT_100MS			100
#define XGMAC_MAC_CONF_SS_SHIFT			29
#define XGMAC_MAC_CONF_SS_10G_XGMII		0
#define XGMAC_MAC_CONF_SS_2_5G_GMII		2
#define XGMAC_MAC_CONF_SS_1G_GMII		3
#define XGMAC_MAC_CONF_SS_100M_MII		4
#define XGMAC_MAC_CONF_SS_5G_XGMII		5
#define XGMAC_MAC_CONF_SS_2_5G_XGMII		6
#define XGMAC_MAC_CONF_SS_2_10M_MII		7

#define XGMAC_MAC_CONF_JD			BIT(16)
#define XGMAC_MAC_CONF_JE			BIT(8)
#define XGMAC_MAC_CONF_WD			BIT(7)
#define XGMAC_MAC_CONF_GPSLCE			BIT(6)
#define XGMAC_MAC_CONF_CST			BIT(2)
#define XGMAC_MAC_CONF_ACS			BIT(1)
#define XGMAC_MAC_CONF_TE			BIT(0)
#define XGMAC_MAC_CONF_RE			BIT(0)

#define XGMAC_MAC_EXT_CONF_HD			BIT(24)

#define XGMAC_MAC_PACKET_FILTER_RA		BIT(31)
#define XGMAC_MAC_PACKET_FILTER_PR		BIT(0)

#define XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_SHIFT	16
#define XGMAC_MAC_Q0_TX_FLOW_CTRL_PT_MASK	GENMASK(15, 0)
#define XGMAC_MAC_Q0_TX_FLOW_CTRL_TFE		BIT(1)

#define XGMAC_MAC_RX_FLOW_CTRL_RFE		BIT(0)
#define XGMAC_MAC_RXQ_CTRL0_RXQ0EN_SHIFT	0
#define XGMAC_MAC_RXQ_CTRL0_RXQ0EN_MASK		GENMASK(1, 0)
#define XGMAC_MAC_RXQ_CTRL0_RXQ0EN_NOT_ENABLED	0
#define XGMAC_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB	2
#define XGMAC_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_AV	1

#define XGMAC_MAC_RXQ_CTRL1_MCBCQEN		BIT(15)

#define XGMAC_MAC_RXQ_CTRL2_PSRQ0_SHIFT		0
#define XGMAC_MAC_RXQ_CTRL2_PSRQ0_MASK		GENMASK(7, 0)

#define XGMAC_MAC_HW_FEATURE1_TXFIFOSIZE_SHIFT	6
#define XGMAC_MAC_HW_FEATURE1_TXFIFOSIZE_MASK	GENMASK(4, 0)
#define XGMAC_MAC_HW_FEATURE1_RXFIFOSIZE_SHIFT	0
#define XGMAC_MAC_HW_FEATURE1_RXFIFOSIZE_MASK	GENMASK(4, 0)

#define XGMAC_MDIO_SINGLE_CMD_SHIFT		16
#define XGMAC_MDIO_SINGLE_CMD_ADDR_CMD_READ	3 << XGMAC_MDIO_SINGLE_CMD_SHIFT
#define XGMAC_MDIO_SINGLE_CMD_ADDR_CMD_WRITE	BIT(16)
#define XGMAC_MAC_MDIO_ADDRESS_PA_SHIFT		16
#define XGMAC_MAC_MDIO_ADDRESS_PA_MASK		GENMASK(15, 0)
#define XGMAC_MAC_MDIO_ADDRESS_RA_MASK		GENMASK(15, 0)
#define XGMAC_MAC_MDIO_ADDRESS_DA_SHIFT		21
#define XGMAC_MAC_MDIO_ADDRESS_CR_SHIFT		19
#define XGMAC_MAC_MDIO_ADDRESS_CR_100_150	0
#define XGMAC_MAC_MDIO_ADDRESS_CR_150_250	1
#define XGMAC_MAC_MDIO_ADDRESS_CR_250_300	2
#define XGMAC_MAC_MDIO_ADDRESS_CR_300_350	3
#define XGMAC_MAC_MDIO_ADDRESS_CR_350_400	4
#define XGMAC_MAC_MDIO_ADDRESS_CR_400_500	5
#define XGMAC_MAC_MDIO_ADDRESS_SADDR		BIT(18)
#define XGMAC_MAC_MDIO_ADDRESS_SBUSY		BIT(22)
#define XGMAC_MAC_MDIO_REG_ADDR_C22P_MASK	GENMASK(4, 0)
#define XGMAC_MAC_MDIO_DATA_GD_MASK		GENMASK(15, 0)

/* MTL Registers */

#define XGMAC_MTL_REGS_BASE 0x1000

struct xgmac_mtl_regs {
	u32 mtl_operation_mode;			/* 0x1000 */
	u32 unused_1004[(0x1030 - 0x1004) / 4];	/* 0x1004 */
	u32 mtl_rxq_dma_map0;			/* 0x1030 */
	u32 mtl_rxq_dma_map1;			/* 0x1034 */
	u32 mtl_rxq_dma_map2;			/* 0x1038 */
	u32 mtl_rxq_dma_map3;			/* 0x103c */
	u32 mtl_tc_prty_map0;			/* 0x1040 */
	u32 mtl_tc_prty_map1;			/* 0x1044 */
	u32 unused_1048[(0x1100 - 0x1048) / 4]; /* 0x1048 */
	u32 txq0_operation_mode;		/* 0x1100 */
	u32 unused_1104;			/* 0x1104 */
	u32 txq0_debug;				/* 0x1108 */
	u32 unused_100c[(0x1118 - 0x110c) / 4];	/* 0x110c */
	u32 txq0_quantum_weight;		/* 0x1118 */
	u32 unused_111c[(0x1140 - 0x111c) / 4];	/* 0x111c */
	u32 rxq0_operation_mode;		/* 0x1140 */
	u32 unused_1144;			/* 0x1144 */
	u32 rxq0_debug;				/* 0x1148 */
};

/*
 * Per-channel MTL/DMA register blocks.
 *
 * Upstream reaches the channel registers through xgmac_mtl_regs and
 * xgmac_dma_regs at fixed offsets 0x1100 and 0x3100 from the MAC base.
 * Qualcomm's XGMAC integration relocates the channel blocks - see
 * dwxgmac_addrs in the vendor Linux dwmac-qcom-ethqos.c, which gives
 * mtl_chan_base 0x8000, dma_even_chan_base 0x8500, dma_odd_chan_base 0x8580
 * and a 0x1000 stride - while leaving the *global* MTL and DMA registers
 * (MTL_OPERATION_MODE at 0x1000, DMA_MODE/DMA_SYSBUS_MODE at 0x3000/0x3004)
 * exactly where upstream expects them. The 0x3000 DMA_MODE path is
 * hardware-proven on this board: XGMAC_DMA_MODE_SWR is set and observed to
 * clear there.
 *
 * So the channel registers get their own structs whose base comes from
 * struct xgmac_config. The layout *within* a block is carried over unchanged
 * from the upstream structs above; that part is an inference from the
 * Synopsys layout rather than something the vendor headers in ref/ confirm,
 * because no dwxgmac2.h is available. xgmac_start() therefore reads back the
 * descriptor-list registers and prints them whenever dma_chan_base is
 * overridden, so a wrong intra-block offset shows up immediately.
 */
struct xgmac_mtl_chan_regs {
	u32 txq0_operation_mode;		/* 0x00 */
	u32 unused_04;				/* 0x04 */
	u32 txq0_debug;				/* 0x08 */
	u32 unused_0c[(0x18 - 0x0c) / 4];	/* 0x0c */
	u32 txq0_quantum_weight;		/* 0x18 */
	u32 unused_1c[(0x40 - 0x1c) / 4];	/* 0x1c */
	u32 rxq0_operation_mode;		/* 0x40 */
	u32 unused_44;				/* 0x44 */
	u32 rxq0_debug;				/* 0x48 */
};

#define XGMAC_MTL_CHAN_BASE_DEFAULT	0x1100

#define XGMAC_MTL_TXQ0_OPERATION_MODE_TQS_SHIFT		16
#define XGMAC_MTL_TXQ0_OPERATION_MODE_TQS_MASK		GENMASK(8, 0)
#define XGMAC_MTL_TXQ0_OPERATION_MODE_TXQEN_SHIFT	2
#define XGMAC_MTL_TXQ0_OPERATION_MODE_TXQEN_ENABLED	2
#define XGMAC_MTL_TXQ0_OPERATION_MODE_TSF		BIT(1)
#define XGMAC_MTL_TXQ0_OPERATION_MODE_FTQ		BIT(0)

#define XGMAC_MTL_TXQ0_DEBUG_TXQSTS			BIT(4)
#define XGMAC_MTL_TXQ0_DEBUG_TRCSTS_SHIFT		1
#define XGMAC_MTL_TXQ0_DEBUG_TRCSTS_MASK		GENMASK(2, 0)
#define XGMAC_MTL_TXQ0_DEBUG_TRCSTS_READ_STATE		0x1

#define XGMAC_MTL_RXQ0_OPERATION_MODE_RQS_SHIFT		16
#define XGMAC_MTL_RXQ0_OPERATION_MODE_RQS_MASK		GENMASK(9, 0)
#define XGMAC_MTL_RXQ0_OPERATION_MODE_EHFC		BIT(7)
#define XGMAC_MTL_RXQ0_OPERATION_MODE_RSF		BIT(5)

#define XGMAC_MTL_RXQ0_DEBUG_PRXQ_SHIFT			16
#define XGMAC_MTL_RXQ0_DEBUG_PRXQ_MASK			GENMASK(14, 0)
#define XGMAC_MTL_RXQ0_DEBUG_RXQSTS_SHIFT		4
#define XGMAC_MTL_RXQ0_DEBUG_RXQSTS_MASK		GENMASK(1, 0)

/*
 * MMC (MAC Management Counters), absolute offsets from mac_regs. The block base
 * is 0x800; the DWC XGMAC databook numbers these from that base, so each value
 * below is 0x800 + the databook offset. MMC_CONTROL bit 0 is CNTRST, which is
 * self-clearing and makes the counters per-run.
 */
#define XGMAC_MMC_CONTROL				0x800
#define XGMAC_MMC_CONTROL_CNTRST			BIT(0)

#define XGMAC_MMC_TXOCT_GB				0x814
#define XGMAC_MMC_TXPKT_GB				0x81c
#define XGMAC_MMC_TXBCAST_G				0x824
#define XGMAC_MMC_TXMCAST_G				0x82c
#define XGMAC_MMC_TX64					0x834
#define XGMAC_MMC_TXUNI_GB				0x864
#define XGMAC_MMC_TXMCAST_GB				0x86c
#define XGMAC_MMC_TXBCAST_GB				0x874
#define XGMAC_MMC_TXUNDERFLOW				0x87c
#define XGMAC_MMC_TXOCT_G				0x884
#define XGMAC_MMC_TXPKT_G				0x88c
#define XGMAC_MMC_TXPAUSE				0x894

/*
 * The RX half of the map is omitted deliberately. The layout that fits the TX
 * counters exactly does not fit RX - see the xgmac_mmc_names[] comment and D217
 * - so those offsets print raw rather than under names that proved wrong.
 */

/* DMA Registers */

#define XGMAC_DMA_REGS_BASE 0x3000

struct xgmac_dma_regs {
	u32 mode;					/* 0x3000 */
	u32 sysbus_mode;				/* 0x3004 */
	u32 unused_3008[(0x3100 - 0x3008) / 4];		/* 0x3008 */
	u32 ch0_control;				/* 0x3100 */
	u32 ch0_tx_control;				/* 0x3104 */
	u32 ch0_rx_control;				/* 0x3108 */
	u32 slot_func_control_status;			/* 0x310c */
	u32 ch0_txdesc_list_haddress;			/* 0x3110 */
	u32 ch0_txdesc_list_address;			/* 0x3114 */
	u32 ch0_rxdesc_list_haddress;			/* 0x3118 */
	u32 ch0_rxdesc_list_address;			/* 0x311c */
	u32 unused_3120;				/* 0x3120 */
	u32 ch0_txdesc_tail_pointer;			/* 0x3124 */
	u32 unused_3128;				/* 0x3128 */
	u32 ch0_rxdesc_tail_pointer;			/* 0x312c */
	u32 ch0_txdesc_ring_length;			/* 0x3130 */
	u32 ch0_rxdesc_ring_length;			/* 0x3134 */
	u32 unused_3138[(0x3160 - 0x3138) / 4];		/* 0x3138 */
	u32 ch0_status;					/* 0x3160 */
};

struct xgmac_dma_chan_regs {
	u32 ch0_control;			/* 0x00 */
	u32 ch0_tx_control;			/* 0x04 */
	u32 ch0_rx_control;			/* 0x08 */
	u32 slot_func_control_status;		/* 0x0c */
	u32 ch0_txdesc_list_haddress;		/* 0x10 */
	u32 ch0_txdesc_list_address;		/* 0x14 */
	u32 ch0_rxdesc_list_haddress;		/* 0x18 */
	u32 ch0_rxdesc_list_address;		/* 0x1c */
	u32 unused_20;				/* 0x20 */
	u32 ch0_txdesc_tail_pointer;		/* 0x24 */
	u32 unused_28;				/* 0x28 */
	u32 ch0_rxdesc_tail_pointer;		/* 0x2c */
	u32 ch0_txdesc_ring_length;		/* 0x30 */
	u32 ch0_rxdesc_ring_length;		/* 0x34 */
	u32 unused_38[(0x60 - 0x38) / 4];	/* 0x38 */
	u32 ch0_status;				/* 0x60 */
};

#define XGMAC_DMA_CHAN_BASE_DEFAULT	0x3100

#define XGMAC_DMA_MODE_SWR				BIT(0)
#define XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_SHIFT		24
#define XGMAC_DMA_SYSBUS_MODE_WR_OSR_LMT_MASK		GENMASK(4, 0)
#define XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT		16
#define XGMAC_DMA_SYSBUS_MODE_RD_OSR_LMT_MASK		GENMASK(4, 0)
#define XGMAC_DMA_SYSBUS_MODE_AAL			BIT(12)
#define XGMAC_DMA_SYSBUS_MODE_EAME			BIT(11)
#define XGMAC_DMA_SYSBUS_MODE_BLEN32			BIT(4)
#define XGMAC_DMA_SYSBUS_MODE_BLEN16			BIT(3)
#define XGMAC_DMA_SYSBUS_MODE_BLEN8			BIT(2)
#define XGMAC_DMA_SYSBUS_MODE_BLEN4			BIT(1)
#define XGMAC_DMA_SYSBUS_MODE_UNDEF			BIT(0)
/*
 * D285: what the vendor DT asks for on this SoC, in both OSR limit fields.
 * The fields are six bits wide (29:24 and 21:16); the masks above are the
 * upstream five-bit ones and are kept as-is so "setenv eth_axi31 1"
 * reproduces the d244 value exactly.
 */
#define XGMAC_DMA_SYSBUS_MODE_OSR_LMT_VENDOR		15

#define XGMAC_DMA_CH0_CONTROL_DSL_SHIFT			18
#define XGMAC_DMA_CH0_CONTROL_PBLX8			BIT(16)

#define XGMAC_DMA_CH0_TX_CONTROL_TXPBL_SHIFT		16
#define XGMAC_DMA_CH0_TX_CONTROL_TXPBL_MASK		GENMASK(5, 0)
#define XGMAC_DMA_CH0_TX_CONTROL_OSP			BIT(4)
#define XGMAC_DMA_CH0_TX_CONTROL_ST			BIT(0)

#define XGMAC_DMA_CH0_RX_CONTROL_RXPBL_SHIFT		16
#define XGMAC_DMA_CH0_RX_CONTROL_RXPBL_MASK		GENMASK(5, 0)
/*
 * RBSZ occupies bits [14:1] of DMA_CH0_RX_CONTROL and holds the buffer size in
 * bytes, so the byte count is written shifted left by one. SHIFT 4 with
 * GENMASK(10, 0) puts the count at [14:4] instead, and the hardware then reads
 * it as the count times eight: XGMAC_MAX_PACKET_SIZE 1600 became a 12800-byte
 * per-buffer grant against an XGMAC_RX_STRIDE_ALIGNED slot of 2048, i.e. the
 * DMA was permitted to write 6.25x past the end of every RX buffer.
 *
 * The sibling driver in this same tree has it right: dwc_eth_qos.h:180-181 is
 * SHIFT 1 with MASK 0x3fff, which is GENMASK(13, 0). So does Linux stmmac's
 * dwxgmac2_set_bfsize(), GENMASK(14, 1) with shift 1. Only this copy differs,
 * which is what makes it a transcription error rather than a core difference.
 */
#define XGMAC_DMA_CH0_RX_CONTROL_RBSZ_SHIFT		1
#define XGMAC_DMA_CH0_RX_CONTROL_RBSZ_MASK		GENMASK(13, 0)
#define XGMAC_DMA_CH0_RX_CONTROL_SR			BIT(0)

/* Descriptors */
/* MAC_LPI_Control_Status, 0x0d0. D260. */
#define XGMAC_MAC_LPI_CS_TLPIEN			BIT(0)
#define XGMAC_MAC_LPI_CS_TLPIEX			BIT(1)
#define XGMAC_MAC_LPI_CS_RLPIEN			BIT(2)
#define XGMAC_MAC_LPI_CS_RLPIEX			BIT(3)
#define XGMAC_MAC_LPI_CS_TLPIST			BIT(8)
#define XGMAC_MAC_LPI_CS_RLPIST			BIT(9)
#define XGMAC_MAC_LPI_CS_LPIEN			BIT(16)
#define XGMAC_MAC_LPI_CS_PLS			BIT(17)
#define XGMAC_MAC_LPI_CS_LPITXA			BIT(19)
#define XGMAC_MAC_LPI_CS_LPIATE			BIT(20)
#define XGMAC_MAC_LPI_CS_LPITCSE		BIT(21)

#define XGMAC_DESCRIPTORS_TX		8
#define XGMAC_DESCRIPTORS_RX		8
/*
 * T-68: the ring size is the last thing that separates "a count of 64" from
 * "eight traversals of an eight-entry ring" as the explanation for the stall
 * period, and the two are numerically identical at the size we have always
 * used. Descriptor memory and the RX buffer arena are sized for MAX at probe;
 * xgmac_select_layout() picks the live count from "eth_ring" at every
 * xgmac_start(), the same allocate-for-the-maximum pattern D301 used for
 * desc_size. "setenv eth_ring 8" reproduces the historical behaviour exactly.
 */
#define XGMAC_DESCRIPTORS_MAX		16
#define XGMAC_BUFFER_ALIGN		ARCH_DMA_MINALIGN
#define XGMAC_MAX_PACKET_SIZE		ALIGN(1568, ARCH_DMA_MINALIGN)

/*
 * D281: the RX buffers must not straddle a 4 KB page.
 *
 * XGMAC_MAX_PACKET_SIZE is 1600, which is not a power of two, so laying the
 * eight RX buffers out at that stride puts some of them across a 4 KB
 * boundary and which ones depends on where memalign() happened to land. Both
 * d242 and d243 came out with RXD2, RXD4 and RXD7 straddling, RXD2 first - and
 * both wedged at rx_desc_idx 2, mid-frame, with Cur_RxBuf 816 bytes into
 * RXD2's buffer. A 2048-byte stride off a 2048-aligned base makes every buffer
 * page-contained no matter where the allocation lands, which is what RXD0 and
 * RXD1 already were - the two that always work.
 *
 * RBSZ stays at XGMAC_MAX_PACKET_SIZE: the DMA is still told it has 1600 bytes
 * per buffer, so nothing about frame sizing changes, only the addresses. The
 * stride is read from the environment once in probe into xgmac->rx_stride, so
 * "setenv eth_buf1600 1" before the first network command of the boot restores
 * the old packed layout for an A/B.
 */
#define XGMAC_RX_STRIDE_ALIGNED		2048
#define XGMAC_RX_BUFFER_SIZE		(XGMAC_DESCRIPTORS_MAX * XGMAC_RX_STRIDE_ALIGNED)

#define XGMAC_RDES3_PKT_LENGTH_MASK	GENMASK(13, 0)
/*
 * D299: the write-back format of RDES3 carries more than a length. CTXT marks
 * a context descriptor, whose low bits are not a packet length at all, and ES
 * marks an error summary. Upstream reads the length unconditionally and hands
 * it to invalidate_dcache_range(); see xgmac_recv().
 */
#define XGMAC_RDES3_CTXT	BIT(30)
#define XGMAC_RDES3_ES		BIT(15)

#define XGMAC_DMA_CH0_STATUS_TI			BIT(0)
#define XGMAC_DMA_CH0_STATUS_TPS			BIT(1)
#define XGMAC_DMA_CH0_STATUS_TBU			BIT(2)
#define XGMAC_DMA_CH0_STATUS_RI			BIT(6)
#define XGMAC_DMA_CH0_STATUS_RBU			BIT(7)
#define XGMAC_DMA_CH0_STATUS_RPS			BIT(8)
#define XGMAC_DMA_CH0_STATUS_FBE			BIT(12)

struct xgmac_desc {
	u32 des0;
	u32 des1;
	u32 des2;
	u32 des3;
};

#define XGMAC_DESC3_OWN		BIT(31)
#define XGMAC_DESC3_FD		BIT(29)
#define XGMAC_DESC3_LD		BIT(28)

/*
 * D272: DMA_CH0_Control.DSL counts this many bytes per unit - measured on
 * hardware, not derived from axi_bus_width (see the desc_pad comment).
 */
#define XGMAC_DSL_UNIT		16

#define XGMAC_AXI_WIDTH_32	4
#define XGMAC_AXI_WIDTH_64	8
#define XGMAC_AXI_WIDTH_128	16

struct xgmac_config {
	bool reg_access_always_ok;
	/*
	 * Bind the PHY through the clause-45 driver even when the clause-22
	 * scan in get_phy_device_by_mask() claimed it first. Needed for PHYs
	 * that answer on both clauses: U-Boot decides is_c45 purely from scan
	 * order and never looks at the DT compatible string, so a C45 PHY with
	 * a readable C22 space gets genphy, which then polls BMSR for an
	 * autoneg-complete bit the PHY only maintains in MMD 7.
	 */
	bool phy_force_c45;
	/* 0 = upstream defaults (XGMAC_{MTL,DMA}_CHAN_BASE_DEFAULT) */
	u32 mtl_chan_base;
	u32 dma_chan_base;
	int swr_wait;
	int config_mac;
	int config_mac_mdio;
	unsigned int axi_bus_width;
	phy_interface_t (*interface)(const struct udevice *dev);
	struct xgmac_ops *ops;
};

struct xgmac_ops {
	void (*xgmac_inval_desc)(void *desc);
	void (*xgmac_flush_desc)(void *desc);
	void (*xgmac_inval_buffer)(void *buf, size_t size);
	void (*xgmac_flush_buffer)(void *buf, size_t size);
	int (*xgmac_probe_resources)(struct udevice *dev);
	int (*xgmac_remove_resources)(struct udevice *dev);
	int (*xgmac_stop_resets)(struct udevice *dev);
	int (*xgmac_start_resets)(struct udevice *dev);
	int (*xgmac_stop_clks)(struct udevice *dev);
	int (*xgmac_start_clks)(struct udevice *dev);
	int (*xgmac_calibrate_pads)(struct udevice *dev);
	int (*xgmac_disable_calibration)(struct udevice *dev);
	int (*xgmac_get_enetaddr)(struct udevice *dev);
	/*
	 * Optional. Called once the PHY has linked up and the negotiated speed
	 * is known, mirroring stmmac's plat_dat->fix_mac_speed callback. SoC
	 * glue that has to reprogram an external MAC-to-SerDes wrapper for the
	 * negotiated line rate hooks in here; the pre-link programming done
	 * from xgmac_start_resets() cannot know the speed yet.
	 */
	int (*xgmac_fix_mac_speed)(struct udevice *dev, int speed);
	/*
	 * Optional. Called after the PHY link check and before both
	 * xgmac_fix_mac_speed and xgmac_adjust_link, for glue that owns a
	 * SoC-side PCS: bring that PCS up here - the PHY is linked, so it is
	 * supplying the RX clock the PCS needs to complete its own resets - and
	 * correct what the PHY driver reported on the way out.
	 *
	 * The correction is not optional on this core. gen10g_startup()
	 * hardcodes SPEED_10000 ("For now just lie and say it's 10G all the
	 * time") while xgmac_adjust_link() has no SPEED_10000 case at all and
	 * returns -EINVAL for it, so a generic-C45 PHY here can never start
	 * until something overwrites phydev->speed with the truth. See D115 and
	 * D120.
	 */
	int (*xgmac_pcs_adjust_phy)(struct udevice *dev,
				    struct phy_device *phydev);
	/*
	 * Optional: print PCS/SerDes status from the datapath dump. Added in
	 * D214 so the dump can show what lies beyond the MAC, which is where
	 * pass 52 bounded the transmit fault. May be NULL.
	 */
	void (*xgmac_pcs_dump)(struct udevice *dev);
};

struct xgmac_priv {
	struct udevice *dev;
	const struct xgmac_config *config;
	fdt_addr_t regs;
	struct xgmac_mac_regs *mac_regs;
	struct xgmac_mtl_regs *mtl_regs;
	struct xgmac_dma_regs *dma_regs;
	struct xgmac_mtl_chan_regs *mtl_chan;
	struct xgmac_dma_chan_regs *dma_chan;
	/* mtl_chan/dma_chan point at the config's relocated bases, not upstream's */
	bool chan_relocated;
	struct reset_ctl reset_ctl;
	struct reset_ctl_bulk reset_bulk;
	struct clk clk_common;
	struct mii_dev *mii;
	struct phy_device *phy;
	ofnode phy_of_node;
	void *syscon_phy;
	u32 syscon_phy_regshift;
	u32 max_speed;
	void *tx_descs;
	void *rx_descs;
	int tx_desc_idx, rx_desc_idx;
	unsigned int desc_size;
	unsigned int desc_per_cacheline;
	unsigned int rx_stride;
	/* T-68: live ring sizes, <= XGMAC_DESCRIPTORS_MAX */
	unsigned int desc_tx;
	unsigned int desc_rx;
	void *tx_dma_buf;
	void *rx_dma_buf;
	void *rx_pkt;
	bool started;
	bool reg_access_ok;
	bool clk_ck_enabled;
};

void xgmac_inval_desc_generic(void *desc);
void xgmac_flush_desc_generic(void *desc);
void xgmac_inval_buffer_generic(void *buf, size_t size);
void xgmac_flush_buffer_generic(void *buf, size_t size);
int xgmac_null_ops(struct udevice *dev);

extern struct xgmac_config xgmac_socfpga_config;
extern struct xgmac_config xgmac_amboseli_config;
