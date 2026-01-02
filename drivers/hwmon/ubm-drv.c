// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Hewlett-Packard Development Company, L.P.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/sysfs.h>

struct ubm_drv_drvdata {
	struct i2c_client *client;
	u8 low;
	u8 high;
	char device[256];
	struct kobject *kobj;
	struct mutex update_lock;
	struct device *hwmon_dev;
	struct device *dev;
};

static struct device *sysroot = NULL;

struct gpio_led_data {
        struct led_classdev cdev;
        struct gpio_desc *gpiod;
        u8 can_sleep;
        u8 blinking;
        gpio_blink_set_t platform_gpio_blink_set;
};

struct gpio_leds_priv {
        int num_leds;
        struct gpio_led_data leds[];
};

#define REG_TEMP 0x5

struct mutex drv_lock;
unsigned int currentSpdIndex=0;


static int ubmdrv_update_client(struct device *dev, u8 reg)
{
	struct ubm_drv_drvdata *drvdata = dev_get_drvdata(dev);
	u16 ret = 0;
	u16 value = 0;
	switch (reg) {
	case REG_TEMP:
		value = i2c_smbus_read_word_data(drvdata->client,REG_TEMP);
		drvdata->high = value & 0xFF;
		drvdata->low = (value & 0xFF00) >> 8;
		break;
	default:
		dev_err(&drvdata->client->dev, "ubmdrv_error_reg 0x%x unknown\n", reg);
		return -EOPNOTSUPP;
	}

	return ret;
}

static ssize_t show_drv_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ubm_drv_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	int sign = 0;
	unsigned char integerV;
	u8 low;
	ret = ubmdrv_update_client(dev, REG_TEMP);
	if (ret < 0)
		return ret;
	integerV = (unsigned char)((drvdata->low >> 4 ) | ( drvdata->high << 4));
	if ( (drvdata->low >> 2 ) & 0x1) 
		low = low + 25;
	if ( (drvdata->low >> 3 ) & 0x1)
		low = low + 50;
	sign = ( drvdata->high & 0x10);
	if ( sign ) 
		return sprintf(buf,"-%d%02d0\n", integerV & 0x7F,low);
	return sprintf(buf,"%d%02d0\n", integerV & 0x7F,low);
}

static ssize_t show_drv_label(struct device *dev, struct device_attribute *attr,
                                char *buf)
{
        return sprintf(buf, "drv\n");
}


static SENSOR_DEVICE_ATTR(temp1_input, 0444, show_drv_temp, NULL, 0);

static struct attribute *ubm_drv_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(ubm_drv);

static const struct of_device_id ubm_drv_of_match[] = {
        { .compatible = "ubm-drv" },
        {},
};
MODULE_DEVICE_TABLE(of, ubm_drv_of_match);

static void ubm_drv_remove(struct i2c_client *client)                
{
	struct ubm_drv_drvdata *drvdata = i2c_get_clientdata(client);;
	hwmon_device_unregister(&client->dev);
	printk("gxp-drv: Remove %s\n", drvdata->device);
	sysfs_remove_link(drvdata->kobj, &drvdata->device[0]);
}

static int ubm_drv_probe(struct i2c_client *client)
{
	struct ubm_drv_drvdata *drvdata;
	struct device *hwmon_dev;
        struct device *dev = &client->dev;
        struct device_node *np;
        struct platform_device *pdev;
	int i,j,index,ret,value;
	struct gpio_leds_priv *leds;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL)) {
		return -EIO;
	}

	drvdata = devm_kzalloc(&client->dev, sizeof(struct ubm_drv_drvdata),
			GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->client = client;
	i2c_set_clientdata(client, drvdata);

	// We shall check if the device exist otherwise return -ENODEV

	value = i2c_smbus_read_word_data(drvdata->client,REG_TEMP);
	if ( value < 0 )
	{
		dev_info(dev,"ubm-drv: device %02x not present %s\n", drvdata->client->addr, drvdata->client->name);
		return -ENODEV;
	}

	mutex_init(&drvdata->update_lock);
	mutex_init(&drv_lock);

	drvdata->hwmon_dev = NULL;

	mutex_lock(&drv_lock);
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev, "hpenvmesensor",
			drvdata, ubm_drv_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);
	drvdata->hwmon_dev = hwmon_dev;

        np = of_parse_phandle((&client->dev)->of_node, "uid-led", 0);
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if ( !pdev )
	{
		goto fin;
	}
	leds = platform_get_drvdata(pdev);
	if (!leds) {
		platform_device_put(pdev);
	}
	if ( sysroot == NULL )
	{
		sysroot = root_device_register("ubm-drives");
		drvdata->dev = sysroot; 
	}
	memset(&drvdata->device[0],0,256);

        sprintf(&drvdata->device[0],"%d", client->adapter->nr);
	drvdata->kobj = &(sysroot->kobj);
	sysfs_create_link(&(sysroot->kobj), &(pdev->dev.kobj), &drvdata->device[0]);
fin:
	mutex_unlock(&drv_lock);
	return 0;
}

static struct i2c_driver ubm_drv_driver = {
	.class = I2C_CLASS_HWMON,
	.probe		= ubm_drv_probe,
	.remove		= ubm_drv_remove,
	.driver = {
		.name	= "ubm-drv",
		.of_match_table = ubm_drv_of_match,
	},
};
module_i2c_driver(ubm_drv_driver);

MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("HPE UBM Drives driver");
MODULE_LICENSE("GPL");
