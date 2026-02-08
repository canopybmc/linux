// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP CSM (Slave Instrumentation and System Support) driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 *
 * This driver provides sysfs interfaces for controlling the virtual EHCI
 * enable and software shutdown functions of the HPE GXP BMC.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/soc/hpe/gxp-regs.h>

#include "gxp-soclib.h"

struct gxp_csm_drvdata {
	void __iomem *base;
	void __iomem *shutdown_reg;
	void __iomem *shutdown_reason_reg;
	void __iomem *rom_state_reg;
	void __iomem *spd_config_reg;
	/* protects access to all registers */
	struct mutex mutex;
	struct device *sysfs_dev;
};

static ssize_t vehci_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned char value;

	mutex_lock(&drvdata->mutex);
	value = readb(drvdata->base + CSM_AFUNEN2);
	mutex_unlock(&drvdata->mutex);

	return sysfs_emit(buf, "%d\n", (value & CSM_AFUNEN2_VEHCI_EN) ? 1 : 0);
}

static ssize_t vehci_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int input;
	unsigned char value;
	int rc;

	rc = kstrtouint(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	value = readb(drvdata->base + CSM_AFUNEN2);
	if (input)
		value |= CSM_AFUNEN2_VEHCI_EN;
	else
		value &= ~CSM_AFUNEN2_VEHCI_EN;
	writeb(value, drvdata->base + CSM_AFUNEN2);

	mutex_unlock(&drvdata->mutex);

	return count;
}
static DEVICE_ATTR_RW(vehci_enable);

static ssize_t sw_shutdown_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);

	if (value)
		writeb(CSM_SHUTDOWN_MAGIC, drvdata->shutdown_reg);

	mutex_unlock(&drvdata->mutex);

	return count;
}
static DEVICE_ATTR_WO(sw_shutdown);

static ssize_t shutdown_reason_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned char value;

	mutex_lock(&drvdata->mutex);
	value = readb(drvdata->shutdown_reason_reg);
	mutex_unlock(&drvdata->mutex);

	return sysfs_emit(buf, "0x%02x\n", value);
}

static ssize_t shutdown_reason_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	writeb(value & 0xff, drvdata->shutdown_reason_reg);
	mutex_unlock(&drvdata->mutex);

	return count;
}
static DEVICE_ATTR_RW(shutdown_reason);

static ssize_t rom_state_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned char value;

	mutex_lock(&drvdata->mutex);
	value = readb(drvdata->rom_state_reg);
	mutex_unlock(&drvdata->mutex);

	return sysfs_emit(buf, "0x%02x\n", value);
}

static ssize_t rom_state_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	writeb(value & 0xff, drvdata->rom_state_reg);
	mutex_unlock(&drvdata->mutex);

	return count;
}
static DEVICE_ATTR_RW(rom_state);

static ssize_t spd_config_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned char value;

	mutex_lock(&drvdata->mutex);
	value = readb(drvdata->spd_config_reg);
	mutex_unlock(&drvdata->mutex);

	return sysfs_emit(buf, "0x%02x\n", value);
}

static ssize_t spd_config_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gxp_csm_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	mutex_lock(&drvdata->mutex);
	writeb(value & 0xff, drvdata->spd_config_reg);
	mutex_unlock(&drvdata->mutex);

	return count;
}
static DEVICE_ATTR_RW(spd_config);

static struct attribute *csm_attrs[] = {
	&dev_attr_sw_shutdown.attr,
	&dev_attr_vehci_enable.attr,
	&dev_attr_shutdown_reason.attr,
	&dev_attr_rom_state.attr,
	&dev_attr_spd_config.attr,
	NULL,
};
ATTRIBUTE_GROUPS(csm);

static int gxp_csm_probe(struct platform_device *pdev)
{
	struct gxp_csm_drvdata *drvdata;
	void __iomem *base;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	/* Map CSM register block */
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);
	drvdata->base = base;
	drvdata->shutdown_reg = base + CSM_SHUTDOWN;
	drvdata->shutdown_reason_reg = base + CSM_SHUTDOWN_REASON;
	drvdata->rom_state_reg = base + CSM_ROM_STATE;
	drvdata->spd_config_reg = base + CSM_SPD_CONFIG;

	mutex_init(&drvdata->mutex);

	/* Create sysfs node under /sys/class/gxp-soc/csm */
	drvdata->sysfs_dev = device_create_with_groups(gxp_soc_class, &pdev->dev,
						       0, drvdata, csm_groups,
						       "csm");
	if (IS_ERR(drvdata->sysfs_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->sysfs_dev),
				     "failed to create sysfs device\n");

	return 0;
}

static void gxp_csm_remove(struct platform_device *pdev)
{
	struct gxp_csm_drvdata *drvdata = platform_get_drvdata(pdev);

	device_unregister(drvdata->sysfs_dev);
}

static const struct of_device_id gxp_csm_of_match[] = {
	{ .compatible = "hpe,gxp-csm" },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_csm_of_match);

static struct platform_driver gxp_csm_driver = {
	.probe = gxp_csm_probe,
	.remove = gxp_csm_remove,
	.driver = {
		.name = "gxp-csm",
		.of_match_table = gxp_csm_of_match,
	},
};
module_platform_driver(gxp_csm_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_AUTHOR("Marcello Sylvester Bauer <marcello.bauer@9elements.com>");
MODULE_DESCRIPTION("HPE GXP CSM Driver");
MODULE_LICENSE("GPL");
