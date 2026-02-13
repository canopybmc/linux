// SPDX-License-Identifier: GPL-2.0-only
/*
 * HPE GXP fan controller
 *
 * Copyright (C) 2022 Hewlett-Packard Enterprise Development Company, L.P.
 * Copyright (C) 2026 9elements GmbH
 */

#include <linux/bits.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soc/hpe/gxp-regs.h>

#define GXP_FAN_PWM_BASE	0x10

struct gxp_fan_ctrl_drvdata {
	void __iomem		*base;
	struct regmap		*xreg_map;
	struct regulator	*fan_supply;
	u8			fan_present;
	u8			pwm_shutdown;
};

static bool fan_powered(struct gxp_fan_ctrl_drvdata *drvdata)
{
	if (!drvdata->fan_supply)
		return true;

	return regulator_is_enabled(drvdata->fan_supply) > 0;
}

static int gxp_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int reg;

	switch (attr) {
	case hwmon_fan_input:
		if (!fan_powered(drvdata)) {
			*val = 0;
			return 0;
		}
		*val = readb(drvdata->base + GXP_FAN_PWM_BASE + channel);
		return 0;
	case hwmon_fan_fault:
		regmap_read(drvdata->xreg_map, XREG_FAN_FAIL_ID, &reg);
		*val = !!(reg & GENMASK(channel * 2 + 1, channel * 2));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		if (fan_powered(drvdata))
			*val = readb(drvdata->base + GXP_FAN_PWM_BASE +
				     channel);
		else
			*val = 0;
		return 0;
	case hwmon_pwm_enable:
		*val = fan_powered(drvdata) ? 1 : 0;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
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

static int gxp_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct gxp_fan_ctrl_drvdata *drvdata = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_pwm_input:
		if (!fan_powered(drvdata))
			return -EACCES;
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

static umode_t gxp_fan_ctrl_is_visible(const void *_data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	const struct gxp_fan_ctrl_drvdata *drvdata = _data;

	if (!(drvdata->fan_present & BIT(channel)))
		return 0;

	switch (type) {
	case hwmon_fan:
		if (attr == hwmon_fan_input || attr == hwmon_fan_fault)
			return 0444;
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		case hwmon_pwm_enable:
			return 0444;
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct hwmon_ops gxp_fan_ctrl_ops = {
	.is_visible = gxp_fan_ctrl_is_visible,
	.read = gxp_fan_ctrl_read,
	.write = gxp_fan_ctrl_write,
};

static const struct hwmon_channel_info * const gxp_fan_ctrl_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT,
			   HWMON_F_INPUT | HWMON_F_FAULT),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_chip_info gxp_fan_ctrl_chip_info = {
	.ops = &gxp_fan_ctrl_ops,
	.info = gxp_fan_ctrl_info,
};

static void gxp_fan_ctrl_restore_pwm(void *data)
{
	struct gxp_fan_ctrl_drvdata *drvdata = data;
	int i;

	for (i = 0; i < 8; i++) {
		if (drvdata->fan_present & BIT(i))
			writeb(drvdata->pwm_shutdown,
			       drvdata->base + GXP_FAN_PWM_BASE + i);
	}
}

static int gxp_fan_ctrl_probe(struct platform_device *pdev)
{
	struct gxp_fan_ctrl_drvdata *drvdata;
	struct device *hwmon_dev;
	struct device *dev = &pdev->dev;
	u32 shutdown_pct;
	unsigned int val;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
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

	drvdata->fan_supply = devm_regulator_get_optional(dev, "fan");
	if (IS_ERR(drvdata->fan_supply)) {
		if (PTR_ERR(drvdata->fan_supply) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(drvdata->fan_supply),
					     "failed to get fan supply\n");
		drvdata->fan_supply = NULL;
	}

	regmap_read(drvdata->xreg_map, XREG_FAN_INSTALLED, &val);
	drvdata->fan_present = (val & XREG_FAN1_8_INST_MASK) >>
			       XREG_FAN1_8_INST_SHIFT;

	ret = device_property_read_u32(dev, "fan-shutdown-percent",
				       &shutdown_pct);
	if (!ret)
		drvdata->pwm_shutdown = clamp(shutdown_pct, 0U, 100U) *
					255 / 100;
	else
		drvdata->pwm_shutdown = 128;

	platform_set_drvdata(pdev, drvdata);

	ret = devm_add_action_or_reset(dev, gxp_fan_ctrl_restore_pwm,
				       drvdata);
	if (ret)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 "hpe_gxp_fan_ctrl",
							 drvdata,
							 &gxp_fan_ctrl_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static void gxp_fan_ctrl_shutdown(struct platform_device *pdev)
{
	gxp_fan_ctrl_restore_pwm(platform_get_drvdata(pdev));
}

static const struct of_device_id gxp_fan_ctrl_of_match[] = {
	{ .compatible = "hpe,gxp-fan-ctrl", },
	{},
};
MODULE_DEVICE_TABLE(of, gxp_fan_ctrl_of_match);

static struct platform_driver gxp_fan_ctrl_driver = {
	.probe		= gxp_fan_ctrl_probe,
	.shutdown	= gxp_fan_ctrl_shutdown,
	.driver = {
		.name	= "gxp-fan-ctrl",
		.of_match_table = gxp_fan_ctrl_of_match,
	},
};
module_platform_driver(gxp_fan_ctrl_driver);

MODULE_AUTHOR("Nick Hawkins <nick.hawkins@hpe.com>");
MODULE_DESCRIPTION("HPE GXP fan controller");
MODULE_LICENSE("GPL");
