// SPDX-License-Identifier: GPL-2.0-only
/*
 * HPE GXP SoC core temperature sensor driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define OFS_TSENCMD	0x00
#define OFS_TSENDAT	0x04
#define OFS_TSENSTAT	0x06

struct gxp_coretemp_drvdata {
	void __iomem *base;
};

static int gxp_coretemp_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct gxp_coretemp_drvdata *drvdata = dev_get_drvdata(dev);
	u16 raw;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	/* Check if sensor data is valid */
	if (!(readw(drvdata->base + OFS_TSENSTAT) & 0x0001)) {
		*val = 0;
		return 0;
	}

	/* Read raw ADC value and convert to millidegrees Celsius */
	raw = readw(drvdata->base + OFS_TSENDAT);
	*val = ((raw * 3874) / 1000 - 2821) * 100;

	return 0;
}

static umode_t gxp_coretemp_is_visible(const void *data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type == hwmon_temp && attr == hwmon_temp_input)
		return 0444;

	return 0;
}

static const struct hwmon_ops gxp_coretemp_ops = {
	.is_visible = gxp_coretemp_is_visible,
	.read = gxp_coretemp_read,
};

static const struct hwmon_channel_info * const gxp_coretemp_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_chip_info gxp_coretemp_chip_info = {
	.ops = &gxp_coretemp_ops,
	.info = gxp_coretemp_info,
};

static int gxp_coretemp_init_hw(struct gxp_coretemp_drvdata *drvdata)
{
	/* Initialize temperature sensor hardware */
	writew(0xc0a0, drvdata->base + OFS_TSENCMD);
	writew(0xc080, drvdata->base + OFS_TSENCMD);
	writew(0xc081, drvdata->base + OFS_TSENCMD);

	usleep_range(64, 128);

	writew(0xc083, drvdata->base + OFS_TSENCMD);

	return 0;
}

static int gxp_coretemp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gxp_coretemp_drvdata *drvdata;
	struct device *hwmon_dev;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(drvdata->base))
		return dev_err_probe(dev, PTR_ERR(drvdata->base),
				     "failed to map registers\n");

	ret = gxp_coretemp_init_hw(drvdata);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "gxp_coretemp",
							 drvdata,
							 &gxp_coretemp_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id gxp_coretemp_of_match[] = {
	{ .compatible = "hpe,gxp-coretemp" },
	{}
};
MODULE_DEVICE_TABLE(of, gxp_coretemp_of_match);

static struct platform_driver gxp_coretemp_driver = {
	.probe = gxp_coretemp_probe,
	.driver = {
		.name = "gxp-coretemp",
		.of_match_table = gxp_coretemp_of_match,
	},
};
module_platform_driver(gxp_coretemp_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_DESCRIPTION("HPE GXP SoC core temperature sensor");
MODULE_LICENSE("GPL");
