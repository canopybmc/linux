// SPDX-License-Identifier: GPL-2.0-only
/*
 * HPE GXP CPLD Host Power Supply
 *
 * Exposes the CPLD-controlled host power state as a regulator.
 * The enable state is read from the FN2 system event status register
 * (PGOOD bit). The power rail is controlled by the CPLD, not software.
 *
 * When a PGOOD interrupt is provided via device tree, the driver fires
 * regulator notifier events on power transitions so that consumer
 * drivers (e.g. PECI) can manage device lifecycle accordingly.
 *
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/soc/hpe/gxp-regs.h>

struct gxp_cpld_reg_data {
	struct regulator_dev *rdev;
	struct regmap *regmap;
};

/* No-op: CPLD controls the power rail, not software */
static int gxp_cpld_reg_noop(struct regulator_dev *rdev)
{
	return 0;
}

static const struct regulator_ops gxp_cpld_reg_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.enable     = gxp_cpld_reg_noop,
	.disable    = gxp_cpld_reg_noop,
};

static const struct regulator_desc gxp_cpld_reg_desc = {
	.name        = "gxp-host-power",
	.type        = REGULATOR_VOLTAGE,
	.ops         = &gxp_cpld_reg_ops,
	.owner       = THIS_MODULE,
	.enable_reg  = FN2_SEVSTAT,
	.enable_mask = FN2_SEVSTAT_PGOOD_STATE,
};

static irqreturn_t gxp_cpld_reg_pgood_irq(int irq, void *data)
{
	struct gxp_cpld_reg_data *priv = data;
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, FN2_SEVSTAT, &val);
	if (ret)
		return IRQ_NONE;

	if (val & FN2_SEVSTAT_PGOOD_STATE)
		regulator_notifier_call_chain(priv->rdev,
					      REGULATOR_EVENT_ENABLE, NULL);
	else
		regulator_notifier_call_chain(priv->rdev,
					      REGULATOR_EVENT_DISABLE, NULL);

	return IRQ_HANDLED;
}

static int gxp_cpld_reg_probe(struct platform_device *pdev)
{
	struct gxp_cpld_reg_data *priv;
	struct regulator_config config = {};
	struct regmap *regmap;
	int irq, ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
				     "failed to get parent regmap\n");

	priv->regmap = regmap;

	config.dev = &pdev->dev;
	config.of_node = pdev->dev.of_node;
	config.regmap = regmap;

	priv->rdev = devm_regulator_register(&pdev->dev, &gxp_cpld_reg_desc,
					     &config);
	if (IS_ERR(priv->rdev))
		return PTR_ERR(priv->rdev);

	irq = platform_get_irq_optional(pdev, 0);
	if (irq > 0) {
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						gxp_cpld_reg_pgood_irq,
						IRQF_ONESHOT | IRQF_SHARED,
						"gxp-cpld-regulator", priv);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "failed to request pgood irq\n");
	}

	return 0;
}

static const struct of_device_id gxp_cpld_reg_of_match[] = {
	{ .compatible = "hpe,gxp-host-power-supply" },
	{}
};
MODULE_DEVICE_TABLE(of, gxp_cpld_reg_of_match);

static struct platform_driver gxp_cpld_reg_driver = {
	.probe  = gxp_cpld_reg_probe,
	.driver = {
		.name = "gxp-cpld-regulator",
		.of_match_table = gxp_cpld_reg_of_match,
	},
};
module_platform_driver(gxp_cpld_reg_driver);

MODULE_DESCRIPTION("HPE GXP CPLD Host Power Supply");
MODULE_LICENSE("GPL");
