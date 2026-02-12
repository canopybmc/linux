// SPDX-License-Identifier: GPL-2.0-only
/*
 * HPE GXP CPLD Host Power Supply
 *
 * Exposes the CPLD-controlled host power state as a regulator.
 * The enable state is read from the FN2 system event status register
 * (PGOOD bit). The power rail is controlled by the CPLD, not software.
 *
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/soc/hpe/gxp-regs.h>

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

static int gxp_cpld_reg_probe(struct platform_device *pdev)
{
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
				     "failed to get parent regmap\n");

	config.dev = &pdev->dev;
	config.of_node = pdev->dev.of_node;
	config.regmap = regmap;

	rdev = devm_regulator_register(&pdev->dev, &gxp_cpld_reg_desc,
				       &config);
	return PTR_ERR_OR_ZERO(rdev);
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
