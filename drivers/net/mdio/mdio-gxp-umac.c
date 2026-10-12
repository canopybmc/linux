// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP UMAC MDIO bus driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/iopoll.h>
#include <linux/err.h>

#include <linux/of_mdio.h>

#define DRV_NAME "mdio-gxp-umac"

#define GXP_MDIO_MII 0x00
#define GXP_MDIO_MII_PHY_ADDR_MASK 0x001F0000
#define GXP_MDIO_MII_PHY_ADDR_SHIFT 16
#define GXP_MDIO_MII_MOWNER 0x00000200
#define GXP_MDIO_MII_MRNW 0x00000100
#define GXP_MDIO_MII_REG_ADDR_MASK 0x0000001F
#define GXP_MDIO_MII_DATA_MASK 0x0000ffff
#define GXP_MDIO_MII_DATA 0x04

#define GXP_MDIO_MMI_READ 1
#define GXP_MDIO_MMI_WRITE 0

#define GXP_MDIO_INTERVAL_US 1000
#define GXP_MDIO_TIMEOUT_US (GXP_MDIO_INTERVAL_US * 10)

struct gxp_umac_mdio_priv {
	void __iomem *base;
};

static int gxp_umac_mdio_wait_idle(struct gxp_umac_mdio_priv *priv)
{
	u32 sts;

	return readl_poll_timeout(priv->base + GXP_MDIO_MII, sts,
				 !(sts & GXP_MDIO_MII_MOWNER),
				 GXP_MDIO_INTERVAL_US, GXP_MDIO_TIMEOUT_US);
}

static int gxp_umac_mdio_op(struct gxp_umac_mdio_priv *priv, int phy_id,
			    int reg, int op)
{
	u32 sts;

	sts = readl(priv->base + GXP_MDIO_MII);
	sts &= ~(GXP_MDIO_MII_PHY_ADDR_MASK | GXP_MDIO_MII_REG_ADDR_MASK);
	sts |= ((phy_id << GXP_MDIO_MII_PHY_ADDR_SHIFT) &
		GXP_MDIO_MII_PHY_ADDR_MASK);
	sts |= (reg & GXP_MDIO_MII_REG_ADDR_MASK);

	if (op)
		sts |= GXP_MDIO_MII_MRNW;
	else
		sts &= ~GXP_MDIO_MII_MRNW;

	writel(sts, priv->base + GXP_MDIO_MII);

	sts |= GXP_MDIO_MII_MOWNER;
	writel(sts, priv->base + GXP_MDIO_MII);

	return gxp_umac_mdio_wait_idle(priv);
}

static int gxp_umac_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct gxp_umac_mdio_priv *priv = bus->priv;
	int ret;

	ret = gxp_umac_mdio_wait_idle(priv);
	if (ret < 0)
		return ret;

	ret = gxp_umac_mdio_op(priv, phy_id, reg, GXP_MDIO_MMI_READ);
	if (ret < 0)
		return ret;

	return readl(priv->base + GXP_MDIO_MII_DATA) & GXP_MDIO_MII_DATA_MASK;
}

static int gxp_umac_mdio_write(struct mii_bus *bus, int phy_id, int reg, u16 value)
{
	struct gxp_umac_mdio_priv *priv = bus->priv;
	int ret;

	ret = gxp_umac_mdio_wait_idle(priv);
	if (ret < 0)
		return ret;

	writel(value & GXP_MDIO_MII_DATA_MASK, priv->base + GXP_MDIO_MII_DATA);
	return gxp_umac_mdio_op(priv, phy_id, reg, GXP_MDIO_MMI_WRITE);
}

static int gxp_umac_mdio_probe(struct platform_device *pdev)
{
	struct gxp_umac_mdio_priv *priv;
	struct mii_bus *bus;
	int rc;

	bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	priv = bus->priv;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	bus->name = dev_name(&pdev->dev);
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", pdev->name);
	bus->parent = &pdev->dev;
	bus->read = gxp_umac_mdio_read;
	bus->write = gxp_umac_mdio_write;

	rc = devm_of_mdiobus_register(&pdev->dev, bus, pdev->dev.of_node);
	if (rc)
		return dev_err_probe(&pdev->dev, rc, "Cannot register MDIO bus\n");

	return 0;
}

static const struct of_device_id gxp_mdio_of_matches[] = {
	{
		.compatible = "hpe,gxp-umac-mdio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gxp_mdio_of_matches);

static struct platform_driver umac_mdio_driver = {
	.driver = {
		.name    = DRV_NAME,
		.of_match_table = gxp_mdio_of_matches,
	},
	.probe   = gxp_umac_mdio_probe,
};

module_platform_driver(umac_mdio_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("HPE GXP UMAC MDIO driver");
MODULE_LICENSE("GPL");
