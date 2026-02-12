// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP PECI controller driver
 *
 * Copyright (C) 2021 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/peci.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

/* PECI registers */
#define GXP_PECI_CMD		0x00
#define   GXP_PECI_CMD_START		BIT(0)
#define   GXP_PECI_CMD_ADDR_MASK	GENMASK(15, 8)
#define   GXP_PECI_CMD_WR_LEN_MASK	GENMASK(23, 16)
#define   GXP_PECI_CMD_RD_LEN_MASK	GENMASK(31, 24)

#define GXP_PECI_STAT		0x04
#define   GXP_PECI_STAT_DONE		BIT(0)
#define   GXP_PECI_STAT_ABORT_FCS	BIT(8)
#define   GXP_PECI_STAT_BAD_WR_FCS	BIT(9)
#define   GXP_PECI_STAT_BAD_RD_FCS	BIT(10)
#define   GXP_PECI_STAT_COLLISION	BIT(11)
#define   GXP_PECI_STAT_TIM_NEG_FAIL	BIT(12)

#define GXP_PECI_DATA_OUT	0x100
#define GXP_PECI_DATA_IN	0x180

#define GXP_PECI_CMD_TIMEOUT_MS	1000

struct gxp_peci {
	struct peci_controller *controller;
	struct device *dev;
	void __iomem *base;
	struct regulator *peci_supply;
	int irq;
	spinlock_t lock; /* protects completion status handling */
	struct completion xfer_complete;
	u32 status;
};

static int gxp_peci_xfer(struct peci_controller *controller, u8 addr,
			 struct peci_request *req)
{
	struct gxp_peci *priv = dev_get_drvdata(controller->dev.parent);
	unsigned long timeout = msecs_to_jiffies(GXP_PECI_CMD_TIMEOUT_MS);
	u32 cmd;
	int i, ret;

	if (priv->peci_supply && !regulator_is_enabled(priv->peci_supply)) {
		dev_dbg(priv->dev, "xfer blocked, host power off\n");
		return -EIO;
	}

	spin_lock_irq(&priv->lock);
	reinit_completion(&priv->xfer_complete);

	for (i = 0; i < req->tx.len; i++)
		writeb(req->tx.buf[i], priv->base + GXP_PECI_DATA_OUT + i);

	cmd = FIELD_PREP(GXP_PECI_CMD_ADDR_MASK, addr) |
	      FIELD_PREP(GXP_PECI_CMD_WR_LEN_MASK, req->tx.len) |
	      FIELD_PREP(GXP_PECI_CMD_RD_LEN_MASK, req->rx.len) |
	      GXP_PECI_CMD_START;

	priv->status = 0;
	writel(cmd, priv->base + GXP_PECI_CMD);
	spin_unlock_irq(&priv->lock);

	ret = wait_for_completion_interruptible_timeout(&priv->xfer_complete,
							timeout);
	if (ret < 0)
		return ret;

	if (ret == 0) {
		dev_dbg(priv->dev, "timeout waiting for a response\n");
		return -ETIMEDOUT;
	}

	spin_lock_irq(&priv->lock);

	if (priv->status != GXP_PECI_STAT_DONE) {
		spin_unlock_irq(&priv->lock);
		dev_dbg(priv->dev, "no valid response, status: %#010x\n",
			priv->status);
		return -EIO;
	}

	for (i = 0; i < req->rx.len; i++)
		req->rx.buf[i] = readb(priv->base + GXP_PECI_DATA_IN + i);

	spin_unlock_irq(&priv->lock);

	return 0;
}

static irqreturn_t gxp_peci_irq_handler(int irq, void *arg)
{
	struct gxp_peci *priv = arg;
	u32 status;

	spin_lock(&priv->lock);
	status = readl(priv->base + GXP_PECI_STAT);
	writel(status, priv->base + GXP_PECI_STAT);
	priv->status = status;

	if (status & GXP_PECI_STAT_DONE)
		complete(&priv->xfer_complete);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static const struct peci_controller_ops gxp_peci_ops = {
	.xfer = gxp_peci_xfer,
};

static int gxp_peci_probe(struct platform_device *pdev)
{
	struct peci_controller *controller;
	struct gxp_peci *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	ret = devm_request_irq(&pdev->dev, priv->irq, gxp_peci_irq_handler,
			       0, "peci-gxp-irq", priv);
	if (ret)
		return ret;

	priv->peci_supply = devm_regulator_get_optional(&pdev->dev, "peci");
	if (IS_ERR(priv->peci_supply)) {
		if (PTR_ERR(priv->peci_supply) != -ENODEV)
			return dev_err_probe(&pdev->dev,
					     PTR_ERR(priv->peci_supply),
					     "failed to get peci supply\n");
		priv->peci_supply = NULL;
	}

	init_completion(&priv->xfer_complete);
	spin_lock_init(&priv->lock);

	controller = devm_peci_controller_add(priv->dev, &gxp_peci_ops);
	if (IS_ERR(controller))
		return dev_err_probe(priv->dev, PTR_ERR(controller),
				     "failed to add gxp peci controller\n");

	priv->controller = controller;

	return 0;
}

static const struct of_device_id gxp_peci_of_table[] = {
	{ .compatible = "hpe,gxp-peci" },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_peci_of_table);

static struct platform_driver gxp_peci_driver = {
	.probe  = gxp_peci_probe,
	.driver = {
		.name           = KBUILD_MODNAME,
		.of_match_table = gxp_peci_of_table,
	},
};
module_platform_driver(gxp_peci_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("HPE GXP PECI controller driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("PECI");
