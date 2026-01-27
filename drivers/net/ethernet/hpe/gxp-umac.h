/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HPE GXP UMAC ethernet driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 */

#ifndef _UMAC_H_
#define _UMAC_H_

#define UMAC_CONFIG_STATUS      0x00
#define UMAC_CFG_PROMISC        0x00008000
#define UMAC_CFG_FE_LOOPBK      0x00004000
#define UMAC_CFG_ACCEPT         0x00002000
#define UMAC_CFG_TXEN           0x00001000
#define UMAC_CFG_RXEN           0x00000800
#define UMAC_CFG_GTX_CLK_EN     0x00000400
#define UMAC_CFG_TX_CLK_EN      0x00000200
#define UMAC_CFG_KEEP_CORRUPT   0x00000100
#define UMAC_STS_RX_PKT_MISSED  0x00000080
#define UMAC_STS_MAC_RX_BUSY    0x00000020
#define UMAC_STS_RX_FLOODED     0x00000010
#define UMAC_CFG_GIGABIT_MODE   0x00000004
#define UMAC_CFG_FIFO_LOOPBK    0x00000002
#define UMAC_CFG_FULL_DUPLEX    0x00000001
#define UMAC_RING_PTR           0x04
#define UMAC_TX_RING_PTR_MASK   0x7FFF0000
#define UMAC_TX_RING_PTR_SHIFT  16
#define UMAC_RX_RING_PTR_MASK   0x00007FFF
#define UMAC_RX_RING_PTR_SHIFT  0

#define UMAC_RING_PROMPT      0x08
#define UMAC_CLEAR_STATUS     0x0C
#define UMAC_CKSUM_CONFIG     0x10
#define UMAC_RING_SIZE        0x14
#define UMAC_TX_RING_SIZE_MASK  0xFF000000
#define UMAC_TX_RING_SIZE_SHIFT 24
#define UMAC_RX_RING_SIZE_MASK  0x00FF0000
#define UMAC_RX_RING_SIZE_SHIFT 16
#define UMAC_LAST_RX_PKT_SIZE   0x0000FFFF

#define UMAC_RING_SIZE_4        0x00
#define UMAC_RING_SIZE_8        0x01
#define UMAC_RING_SIZE_16       0x03
#define UMAC_RING_SIZE_32       0x07
#define UMAC_RING_SIZE_64       0x0F
#define UMAC_RING_SIZE_128      0x1F
#define UMAC_RING_SIZE_256      0x3F

#define UMAC_MAC_ADDR_HI      0x18
#define UMAC_MAC_ADDR_MID     0x1C
#define UMAC_MAC_ADDR_LO      0x20
#define UMAC_MC_ADDR_FILT_HI  0x24
#define UMAC_MC_ADDR_FILT_LO  0x28
#define UMAC_CONFIG_STATUS2   0x2C
#define UMAC_INTERRUPT        0x30
#define UMAC_RX_INT_OFLOW       0x00000080
#define UMAC_TX_INT_OFLOW       0x00000040
#define UMAC_OVERRRUN_INT       0x00000010
#define UMAC_RX_INTEN           0x00000008
#define UMAC_RX_INT             0x00000004
#define UMAC_TX_INTEN           0x00000002
#define UMAC_TX_INT             0x00000001

#define UMAC_OVERRUN_COUNT    0x34
#define UMAC_RX_INT_CONFIG    0x38
#define UMAC_TX_INT_CONFIG    0x3C
#define UMAC_PACKET_LENGTH    0x40
#define UMAC_BCAST_FILTER     0x44
#define UMAC_BCAST_PROMPT     0x48
#define UMAC_RX_RING_ADDR     0x4C
#define UMAC_TX_RING_ADDR     0x50
#define UMAC_DMA_CONFIG       0x54
#define UMAC_BURST_CONFIG     0x58
#define UMAC_PAUSE_CONFIG     0x5C
#define UMAC_PAUSE_CONTROL    0x60
#define UMAC_CONGESTN_CONFIG  0x64

#define UMAC_MAX_TX_DESC_ENTRIES	0x100
#define UMAC_MAX_RX_DESC_ENTRIES	0x100
#define UMAC_MAX_TX_FRAME_SIZE		0x600
#define UMAC_MAX_RX_FRAME_SIZE		0x600

#define UMAC_RING_ENTRY_HW_OWN		0x8000

#define UMAC_RING_RX_FRAME_ERR      0x4000
#define UMAC_RING_RX_CRC_ERR        0x2000
#define UMAC_RING_RX_LEN_ERR        0x1000
#define UMAC_RING_RX_OVERRUN        0x0800
#define UMAC_RING_RX_PKT_TYPE_MASK  0x0700
#define UMAC_RING_RX_MII_ERR        0x0080
#define UMAC_RING_RX_CAR_EXT_ERR    0x0040
#define UMAC_RING_RX_BAD_PAUSE_ERR  0x0020
#define UMAC_RING_RX_ERR_MASK       0x38E0

#define UMAC_MIN_FRAME_SIZE       60
#define UMAC_MAX_PAYLOAD_SIZE     1500
#define UMAC_MAX_FRAME_SIZE       1514
#define UMAC_MAX_PACKET_ROUNDED   0x600
#define MAX_PKT_SIZE		  1518

struct umac_rx_desc_entry {
	u32  dmaaddress;
	u16  status;
	u16  count;
	u16  checksum;
	u16  control;
	u32  reserved;
} __aligned(16);

struct umac_rx_descs {
	struct umac_rx_desc_entry entrylist[UMAC_MAX_RX_DESC_ENTRIES];
	u8 framelist[UMAC_MAX_RX_DESC_ENTRIES][UMAC_MAX_RX_FRAME_SIZE];
} __packed;

struct umac_tx_desc_entry {
	u32  dmaaddress;
	u16  status;
	u16  count;
	u32  cksumoffset;
	u32  reserved;
} __aligned(16);

struct umac_tx_descs {
	struct umac_tx_desc_entry entrylist[UMAC_MAX_TX_DESC_ENTRIES];
	u8 framelist[UMAC_MAX_TX_DESC_ENTRIES][UMAC_MAX_TX_FRAME_SIZE];
} __packed;

#endif
