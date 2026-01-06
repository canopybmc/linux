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
#include <linux/debugfs.h>
#include <linux/bitops.h>

struct gxp_ddr5_drvdata {
	struct i2c_client *client;
	u8 low;
	u8 high;
	struct mutex update_lock;
	struct device *hwmon_dev;
	struct dentry *debugfs;
};

#define REG_11 0xb
#define REG_TEMP 0x31
#define HUB_REG(reg) ((u8) ~0x80 | reg)
#define SPD_SIZE 1024

struct mutex drv_lock;
unsigned int currentSpdIndex=0;


unsigned char spd[SPD_SIZE]; // store SPD
int spd_len = 0;

static ssize_t spd_read(struct file *filp, struct kobject *kobj,
                           const struct bin_attribute *bin_attr,
                           char *buf, loff_t off, size_t count);

static const struct bin_attribute spd_attr = {
        .attr = {
                .name = "spd",
                .mode = 0444,
        },
        .size = SPD_SIZE,
        .read = spd_read,
};

static void gxp_ddr5_fs_init(struct gxp_ddr5_drvdata *drvdata)
{
/* Create the sysfs eeprom file */
	int err=0;
        err = sysfs_create_bin_file(&drvdata->client->dev.kobj, &spd_attr);
}

static ssize_t spd_read(struct file *filp, struct kobject *kobj,
                           const struct bin_attribute *bin_attr,
                           char *buf, loff_t off, size_t count)
{
	int currentIndex=0;
	currentSpdIndex = off;
	while(( currentIndex < count) && (currentSpdIndex != SPD_SIZE))
	{
			buf[currentIndex] = (unsigned char) spd[currentSpdIndex];
			currentIndex++;
			currentSpdIndex++;
	}
	return currentIndex;
//	return simple_read_from_buffer(buffer, len, offset, spd, spd_len);
}


static int gxpddr5_update_client(struct device *dev, u8 reg)
{
	struct gxp_ddr5_drvdata *drvdata = dev_get_drvdata(dev);
	u16 ret = 0;
	switch (reg) {
	case REG_TEMP:
		drvdata->low = i2c_smbus_read_byte_data(drvdata->client,REG_TEMP);
		drvdata->high = i2c_smbus_read_byte_data(drvdata->client,(REG_TEMP+1));
		// printk("%x %x\n", drvdata->low, drvdata->high);
		break;
	default:
		dev_err(&drvdata->client->dev, "gxddr5_error_reg 0x%x unknown\n", reg);
		return -EOPNOTSUPP;
	}

	return ret;
}

static ssize_t show_ddr5_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gxp_ddr5_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	int sign = 0;
	unsigned char integerV;
	u8 low;
	ret = gxpddr5_update_client(dev, REG_TEMP);
	if (ret < 0)
		return ret;
	integerV = (unsigned char)((drvdata->low >> 4 ) | ( drvdata->high << 4));
	if ( (drvdata->low >> 2 ) & 0x1) 
		low = low + 25;
	if ( (drvdata->low >> 3 ) & 0x1)
		low = low + 50;
	sign = ( drvdata->high & 0x10);
	// printk("%02x %0x\n", drvdata->high, drvdata->low);
	if ( sign ) 
		return sprintf(buf,"-%d%02d0\n", integerV & 0x7F,low);
	return sprintf(buf,"%d%02d0\n", integerV & 0x7F,low);
}

static ssize_t show_ddr5_label(struct device *dev, struct device_attribute *attr,
                                char *buf)
{
        return sprintf(buf, "ddr5\n");
}


static SENSOR_DEVICE_ATTR(temp1_input, 0444, show_ddr5_temp, NULL, 0);

static struct attribute *gxp_ddr5_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(gxp_ddr5);

static const struct of_device_id gxp_ddr5_of_match[] = {
        { .compatible = "hpe,gxp-ddr5" },
        {},
};
MODULE_DEVICE_TABLE(of, gxp_ddr5_of_match);

static void gxp_ddr5_remove(struct i2c_client *client)
                
{
	sysfs_remove_bin_file(&client->dev.kobj, &spd_attr);
	hwmon_device_unregister(&client->dev);
}

static int gxp_ddr5_probe(struct i2c_client *client)
{
	struct gxp_ddr5_drvdata *drvdata;
	struct device *hwmon_dev;
	int i,j,index;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL)) {
		return -EIO;
	}

	drvdata = devm_kzalloc(&client->dev, sizeof(struct gxp_ddr5_drvdata),
			GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->client = client;
	i2c_set_clientdata(client, drvdata);

	mutex_init(&drvdata->update_lock);
	mutex_init(&drv_lock);

	drvdata->hwmon_dev = NULL;

	ret = i2c_smbus_write_byte_data(drvdata->client,0xb,0);
	if ( ret < 0 )
	{
		printk("gxp-ddr5: device not present aborting init\n");
		return 0;
	}
	
	mutex_lock(&drv_lock);
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev, "spd5118",
			drvdata, gxp_ddr5_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);
	drvdata->hwmon_dev = hwmon_dev;
	mutex_unlock(&drv_lock);
	// We have to update initialize the Hub
	i2c_smbus_write_byte_data(drvdata->client,0xb,0 );
	// We can try to read the spd
	index = 0;
	for ( j = 0 ; j < 8 ; j++ )
	{
		i2c_smbus_write_byte_data(drvdata->client,0xb,j);
		for ( i = 0x0; i < 0x80 ; i++ )
		{
			spd[index++] = i2c_smbus_read_byte_data(client, 0x80|i);
		}	
	}
	i2c_smbus_write_byte_data(drvdata->client,0xb,0 );
	gxp_ddr5_fs_init(drvdata);
	return 0;
}

static struct i2c_driver gxp_ddr5_driver = {
	.class = I2C_CLASS_HWMON,
	.probe		= gxp_ddr5_probe,
	.remove		= gxp_ddr5_remove,
	.driver = {
		.name	= "gxp-ddr5",
		.of_match_table = gxp_ddr5_of_match,
	},
};
module_i2c_driver(gxp_ddr5_driver);

MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("HPE GXP DDR5 driver");
MODULE_LICENSE("GPL");
