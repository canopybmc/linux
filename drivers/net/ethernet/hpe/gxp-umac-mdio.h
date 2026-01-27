/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HPE GXP UMAC MDIO bus driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 */

#ifndef _UMAC_MDIO_H_
#define _UMAC_MDIO_H_

#define UMAC_MII                0x00
#define UMAC_MII_NMRST          0x00008000
#define UMAC_MII_PHY_ADDR_MASK  0x001F0000
#define UMAC_MII_PHY_ADDR_SHIFT 16
#define UMAC_MII_MOWNER         0x00000200
#define UMAC_MII_MRNW           0x00000100
#define UMAC_MII_REG_ADDR_MASK  0x0000001F
#define UMAC_MII_DATA           0x04
#define UMAC_MII_LINK           0x08
#define UMAC_MII_CFG            0x0C

#endif
