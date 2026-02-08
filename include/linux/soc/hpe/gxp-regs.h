/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HPE GXP SoC Register Definitions
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 *
 * This header provides named constants for GXP SoC register offsets,
 * replacing magic numbers in drivers with self-documenting identifiers.
 */

#ifndef __LINUX_SOC_HPE_GXP_REGS_H
#define __LINUX_SOC_HPE_GXP_REGS_H

/*
 * XREG (Extended Register Block) at 0xD1000000
 *
 * The XREG block provides extended GPIO functionality for LEDs, fans,
 * PSU status, and interrupt handling through two interrupt groups.
 */

/* Server identification */
#define XREG_SERVER_ID		0x00	/* Server ID [23:8] */
#define XREG_SERVER_ID_MASK	GENMASK(23, 8)
#define XREG_SERVER_ID_SHIFT	8
#define GXP_SERVER_ID_AMPERE	0x250	/* Ampere-based ProLiant (RL300, RL340) */

/* LED control registers */
#define XREG_IOP_LED		0x04	/* IOP LED control [7:0] */
#define XREG_LED_CTRL		0x08	/* LED/ACM control */
#define XREG_HEALTH_LED		0x0c	/* Health LED control */

/* XREG_IOP_LED bits */
#define XREG_IOP_LED_MASK	GENMASK(7, 0)

/* XREG_LED_CTRL bits */
#define XREG_LED_CTRL_UID_ON	BIT(14)	/* UID LED on */
#define XREG_LED_CTRL_UID_BLINK	BIT(15)	/* UID LED blink enable */
#define XREG_LED_CTRL_ACM_OFF	BIT(16)	/* ACM force off */
#define XREG_LED_CTRL_ACM_REM	BIT(17)	/* ACM removed (RO) */
#define XREG_LED_CTRL_ACM_REQ	BIT(18)	/* ACM request */

/* XREG_HEALTH_LED bits */
#define XREG_HEALTH_LED_AMBER	BIT(14)	/* Health LED amber */
#define XREG_HEALTH_LED_RED	BIT(15)	/* Health LED red */

/* PSU control and status */
#define XREG_PSU_SIDEBAND	0x40	/* PSU sideband selection [1:0] */
#define XREG_PSU_ENABLE		0x41	/* PSU enable */
#define XREG_PSU_PRESENCE	0x42	/* PSU presence [7:0] */
#define XREG_PSU_POWER_STATUS	0x44	/* PSU AC/DC status */

/* XREG_PSU_SIDEBAND bits */
#define XREG_PSU_SIDEBAND_MASK	GENMASK(1, 0)

/* XREG_PSU_PRESENCE bits */
#define XREG_PSU_PRESENCE_MASK	GENMASK(23, 16)
#define XREG_PSU_PRESENCE_SHIFT	16

/* XREG_PSU_POWER_STATUS bits */
#define XREG_PSU_AC_OK_MASK	GENMASK(7, 0)
#define XREG_PSU_DC_OK_MASK	GENMASK(15, 8)
#define XREG_PSU_DC_OK_SHIFT	8

/* Fan status registers */
#define XREG_FAN_INSTALLED	0x78	/* Fan installed status */
#define XREG_FAN_FAIL_ID	0x7c	/* Fan fail and ID [23:0] */
#define XREG_FAN_ID_EXT		0x80	/* Extended fan ID [7:0] */

/* XREG_FAN_INSTALLED bits */
#define XREG_FAN1_8_INST_MASK	GENMASK(23, 16)
#define XREG_FAN1_8_INST_SHIFT	16
#define XREG_FAN9_16_INST_MASK	GENMASK(31, 24)
#define XREG_FAN9_16_INST_SHIFT	24

/* XREG_FAN_FAIL_ID bits (dual-motor fans use 2 bits each) */
#define XREG_FAN_FAIL_MASK	GENMASK(15, 0)
#define XREG_FAN1_8_ID_MASK	GENMASK(31, 24)
#define XREG_FAN1_8_ID_SHIFT	24

/* XREG_FAN_ID_EXT bits */
#define XREG_FAN9_16_ID_MASK	GENMASK(7, 0)

/* Boot and power control */
#define XREG_BOOT_CONTROL	0x09	/* Host boot gate control */
#define XREG_POWER_STATE	0x4b	/* Exit S5 power state */
#define XREG_SPD_MODE		0x84	/* I2C SPD mode selection */
#define XREG_SOC_READY		0xe2	/* Ampere SoC communication ready (RO) */
#define XREG_FLASH_SELECT	0x119	/* SPI flash allocation */
#define XREG_SOC_RELEASE	0x11a	/* SoC release (bit 3) */

/*
 * XREG_BOOT_CONTROL valid values
 *
 * 0x24 releases the host boot gate, allowing the host CPU to boot.
 * 0x00 holds the host boot gate, preventing host CPU boot.
 *
 * Verified against HPE OpenBMC:
 *   host-boot-enable: devmem 0xd1000009 8 36 (0x24 = release)
 *   down scripts:     devmem 0xd1000009 8 0  (0x00 = hold)
 */
#define XREG_BOOT_CONTROL_RELEASE	0x24	/* Allow host boot */
#define XREG_BOOT_CONTROL_HOLD		0x00	/* Hold host boot */

/* XREG_FLASH_SELECT values */
#define XREG_FLASH_SELECT_UEFI_EV	0x5d	/* UEFI EV store via SPI ctrl1 */

/* XREG_SOC_RELEASE bits */
#define XREG_SOC_RELEASE_BIT	BIT(3)	/* SoC release control bit */

/* Interrupt control registers */
#define XREG_INT_GRP_STAT_MASK	0x94	/* Interrupt group status mask */
#define XREG_INT_HI_PRI_EN	0xa0	/* High priority interrupt enable */
#define XREG_INT_GRP5_BASE	0xc0	/* Interrupt group 5 base */
#define XREG_INT_GRP6_BASE	0xc4	/* Interrupt group 6 base */
#define XREG_INT_GRP5_FLAG	0xc0	/* Interrupt group 5 flags */
#define XREG_INT_GRP6_FLAG	0xc4	/* Interrupt group 6 flags */
#define XREG_INT_GRP5_MASK	0xc1	/* Interrupt group 5 mask */
#define XREG_INT_GRP6_MASK	0xc5	/* Interrupt group 6 mask */

/* Interrupt group enable bits */
#define XREG_INT_GRP5_EN	BIT(4)
#define XREG_INT_GRP6_EN	BIT(5)

/*
 * CSM (Slave Instrumentation and System Support) at 0x80000000
 *
 * The CSM block provides core GPIO functionality through GPI (General
 * Purpose Input) and GPO (General Purpose Output) chains.
 */

/* Function enable register */
#define CSM_AFUNEN2		0xde	/* Function enable register 2 */
#define CSM_AFUNEN2_VEHCI_EN	BIT(6)	/* Virtual EHCI enable */

/* Software shutdown */
#define CSM_SHUTDOWN		0xe7	/* Shutdown register */
#define CSM_SHUTDOWN_MAGIC	0xb2	/* Magic value to trigger shutdown */

/* Shutdown reason */
#define CSM_SHUTDOWN_REASON	0x74	/* Shutdown cause code (RW) */

/* ROM state */
#define CSM_ROM_STATE		0xb0	/* ROM ready / DIMM access state (RW) */

/* SPD config */
#define CSM_SPD_CONFIG		0x110	/* SPD read mode configuration (RW) */

/* GPI (General Purpose Input) data registers */
#define CSM_GPIDATL		0x40	/* GPI data low [31:0] */
#define CSM_GPIDATH		0x60	/* GPI data high [63:32] */

/* GPO chain 1 data registers */
#define CSM_GPODATL		0xb0	/* GPO chain 1 data low [31:0] */
#define CSM_GPODATH		0xb4	/* GPO chain 1 data high [63:32] */

/* GPO chain 2 data registers */
#define CSM_GPODAT2L		0xf8	/* GPO chain 2 data low [31:0] */
#define CSM_GPODAT2H		0xfc	/* GPO chain 2 data high [63:32] */

/* GPO ownership registers (determines which CPU can control each GPIO) */
#define CSM_GPOOWNL		0x110	/* GPO chain 1 ownership low */
#define CSM_GPOOWNH		0x114	/* GPO chain 1 ownership high */
#define CSM_GPOOWN2L		0x118	/* GPO chain 2 ownership low */
#define CSM_GPOOWN2H		0x11c	/* GPO chain 2 ownership high */

/* Direct access registers (for SW_RESET) */
#define CSM_ASRSTAT		0x5c	/* Reset status */
#define CSM_ASRESTAT		0x5d	/* Extended reset status */
#define CSM_ASRESTAT_SW_RESET	BIT(7)	/* Software reset bit */

/*
 * FN2 (Function 2 - Embedded Management Processor Support) at 0x80200000
 *
 * The FN2 block provides power state control through GPIO lines for
 * VPBTN (virtual power button), PGOOD, and PERST signals.
 */

/* Power button control */
#define FN2_VPBTN_CTRL		0x44	/* Virtual power button control */
#define FN2_VPBTN		0x46	/* Virtual power button (legacy) */
#define FN2_VPBTN_PRESSED	BIT(16)	/* Power button pressed */

/* System event registers */
#define FN2_SEVSTAT		0x70	/* System event status */
#define FN2_SEVMASK		0x74	/* System event mask */

/* FN2_SEVSTAT bits */
#define FN2_SEVSTAT_PGOOD	BIT(0)	/* Power good event */
#define FN2_SEVSTAT_PERST	BIT(1)	/* PCIe reset event */
#define FN2_SEVSTAT_PGOOD_STATE	BIT(24)	/* Current PGOOD state */
#define FN2_SEVSTAT_PERST_STATE	BIT(25)	/* Current PERST state */

/* FN2_SEVMASK bits */
#define FN2_SEVMASK_EN		BIT(0)	/* Event interrupt enable */

/*
 * eSPI Controller at 0x80FE0000
 *
 * The eSPI block provides the Enhanced SPI interface for host-BMC
 * communication. The OOB (Out-of-Band) controller requires a reset
 * before host power-on on platforms that use eSPI (Intel, AMD).
 * Ampere-based servers use SSIF instead and do not need this reset.
 */
#define GXP_ESPI_OOB_CTRL	0x1040		/* eSPI OOB control register */
#define GXP_ESPI_OOB_RESET	0x00060001	/* Reset eSPI OOB controller */

#endif /* __LINUX_SOC_HPE_GXP_REGS_H */
