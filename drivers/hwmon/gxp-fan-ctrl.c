// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P. */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/hpe/gxp-regs.h>

#define GXP_FAN_PWM_BASE	0x10

struct gxp_fan_ctrl_drvdata {
	void __iomem	*base;
	struct regmap	*xreg_map;
	struct regmap	*fn2_map;
};

static bool fan_installed(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int val;

	regmap_read(drvdata->xreg_map, XREG_FAN_INSTALLED, &val);

	return !!(val & BIT(fan + XREG_FAN1_8_INST_SHIFT));
}

static long fan_failed(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int val;

	regmap_read(drvdata->xreg_map, XREG_FAN_FAIL_ID, &val);

	return !!(val & GENMASK(fan * 2 + 1, fan * 2));
}

static long fan_enabled(struct device *dev, int fan)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int val;

	/*
	 * Check the power status as if the platform is off the value
	 * reported for the PWM will be incorrect. Report fan as
	 * disabled.
	 */
	regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);

	return !!((val & FN2_SEVSTAT_PGOOD_STATE) && fan_installed(dev, fan));
}

static int gxp_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		if (val > 255 || val < 0)
			return -EINVAL;
		writeb(val, drvdata->base + GXP_FAN_PWM_BASE + channel);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_fan_ctrl_write(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return gxp_pwm_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	switch (attr) {
	case hwmon_fan_enable:
		*val = fan_enabled(dev, channel);
		return 0;
	case hwmon_fan_fault:
		*val = fan_failed(dev, channel);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int reg;

	/*
	 * Check the power status of the platform. If the platform is off
	 * the value reported for the PWM will be incorrect. In this case
	 * report a PWM of zero.
	 */

	regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &reg);

	if (reg & FN2_SEVSTAT_PGOOD_STATE)
		*val = fan_installed(dev, channel) ?
			readb(drvdata->base + GXP_FAN_PWM_BASE + channel) : 0;
	else
		*val = 0;

	return 0;
}

static int gxp_fan_ctrl_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return gxp_fan_read(dev, attr, channel, val);
	case hwmon_pwm:
		return gxp_pwm_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t gxp_fan_ctrl_is_visible(const void *_data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	umode_t mode = 0;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_enable:
		case hwmon_fan_fault:
			mode = 0444;
			break;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return mode;
}

static const struct hwmon_ops gxp_fan_ctrl_ops = {
	.is_visible = gxp_fan_ctrl_is_visible,
	.read = gxp_fan_ctrl_read,
	.write = gxp_fan_ctrl_write,
};

static const struct hwmon_channel_info * const gxp_fan_ctrl_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE,
			   HWMON_F_FAULT | HWMON_F_ENABLE),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info gxp_fan_ctrl_chip_info = {
	.ops = &gxp_fan_ctrl_ops,
	.info = gxp_fan_ctrl_info,

};

static int gxp_fan_ctrl_probe(struct platform_device *pdev)
{
	struct gxp_fan_ctrl_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;

	drvdata = devm_kzalloc(dev, sizeof(struct gxp_fan_ctrl_drvdata),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return dev_err_probe(dev, PTR_ERR(drvdata->base),
				     "failed to map base\n");

	drvdata->xreg_map = syscon_regmap_lookup_by_phandle(dev->of_node,
							    "hpe,xreg");
	if (IS_ERR(drvdata->xreg_map))
		return dev_err_probe(dev, PTR_ERR(drvdata->xreg_map),
				     "failed to find hpe,xreg syscon\n");

	drvdata->fn2_map = syscon_regmap_lookup_by_phandle(dev->of_node,
							   "hpe,fn2");
	if (IS_ERR(drvdata->fn2_map))
		return dev_err_probe(dev, PTR_ERR(drvdata->fn2_map),
				     "failed to find hpe,fn2 syscon\n");

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "hpe_gxp_fan_ctrl",
							 drvdata,
							 &gxp_fan_ctrl_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct of_device_id gxp_fan_ctrl_of_match[] = {
	{ .compatible = "hpe,gxp-fan-ctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_fan_ctrl_of_match);

static struct platform_driver gxp_fan_ctrl_driver = {
	.probe		= gxp_fan_ctrl_probe,
	.driver = {
		.name	= "gxp-fan-ctrl",
		.of_match_table = gxp_fan_ctrl_of_match,
	},
};
module_platform_driver(gxp_fan_ctrl_driver);

MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_DESCRIPTION("HPE GXP fan controller");
MODULE_LICENSE("GPL");
