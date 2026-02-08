// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP XREG (Extended Register Block) driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 *
 * This driver provides extended GPIO functionality and interrupt handling
 * for the HPE GXP BMC SoC. It exposes 150 GPIO lines for LEDs, fan status,
 * PSU presence, and button handling with interrupt support.
 */

#include <linux/device.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/soc/hpe/gxp-regs.h>

#include "gxp-soclib.h"

/* GPIO pin to interrupt group mapping */
#define XREG_INT_GRP5_PIN_BASE	59
#define XREG_INT_GRP6_PIN_BASE	90

/* GPIO pin definitions */
enum xreg_gpio_pin {
	IOP_LED1 = 0,
	IOP_LED2,
	IOP_LED3,
	IOP_LED4,
	IOP_LED5,
	IOP_LED6,
	IOP_LED7,
	IOP_LED8,
	FAN1_INST = 8,
	FAN2_INST,
	FAN3_INST,
	FAN4_INST,
	FAN5_INST,
	FAN6_INST,
	FAN7_INST,
	FAN8_INST,
	FAN9_INST,
	FAN10_INST,
	FAN11_INST,
	FAN12_INST,
	FAN13_INST,
	FAN14_INST,
	FAN15_INST,
	FAN16_INST,
	FAN1_FAIL = 24,
	FAN2_FAIL,
	FAN3_FAIL,
	FAN4_FAIL,
	FAN5_FAIL,
	FAN6_FAIL,
	FAN7_FAIL,
	FAN8_FAIL,
	FAN9_FAIL,
	FAN10_FAIL,
	FAN11_FAIL,
	FAN12_FAIL,
	FAN13_FAIL,
	FAN14_FAIL,
	FAN15_FAIL,
	FAN16_FAIL,
	FAN1_ID = 40,
	FAN2_ID,
	FAN3_ID,
	FAN4_ID,
	FAN5_ID,
	FAN6_ID,
	FAN7_ID,
	FAN8_ID,
	FAN9_ID,
	FAN10_ID,
	FAN11_ID,
	FAN12_ID,
	FAN13_ID,
	FAN14_ID,
	FAN15_ID,
	FAN16_ID,
	LED_IDENTIFY = 56,
	LED_HEALTH_RED,
	LED_HEALTH_AMBER,
	PWR_BTN_INT = 59,
	UID_PRESS_INT,
	SLP_INT,
	ACM_FORCE_OFF = 70,
	ACM_REMOVED,
	ACM_REQ_N,
	PSU1_INST = 90,
	PSU2_INST,
	PSU3_INST,
	PSU4_INST,
	PSU5_INST,
	PSU6_INST,
	PSU7_INST,
	PSU8_INST,
	PSU1_AC = 100,
	PSU2_AC,
	PSU3_AC,
	PSU4_AC,
	PSU5_AC,
	PSU6_AC,
	PSU7_AC,
	PSU8_AC,
	PSU1_DC = 110,
	PSU2_DC,
	PSU3_DC,
	PSU4_DC,
	PSU5_DC,
	PSU6_DC,
	PSU7_DC,
	PSU8_DC
};

struct gxp_xreg_drvdata {
	void __iomem *base;
	struct regmap *xreg_map;
	struct gpio_chip gpio_chip;
	struct device *sysfs_dev;
	void __iomem *soc_ready_reg;
	void __iomem *flash_select_reg;
	int irq;
	u8 psu_presence;
};

static ssize_t server_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;

	regmap_read(drvdata->xreg_map, XREG_SERVER_ID, &value);

	return sysfs_emit(buf, "0x%04x\n", (value & 0xffff00) >> 8);
}
static DEVICE_ATTR_RO(server_id);

static ssize_t sideband_sel_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;

	regmap_read(drvdata->xreg_map, XREG_PSU_SIDEBAND, &value);

	return sysfs_emit(buf, "0x%02x\n", value & XREG_PSU_SIDEBAND_MASK);
}

static ssize_t sideband_sel_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	int input;
	int rc;

	rc = kstrtoint(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	if (input & ~XREG_PSU_SIDEBAND_MASK)
		return -EINVAL;

	regmap_update_bits(drvdata->xreg_map, XREG_PSU_SIDEBAND,
			   XREG_PSU_SIDEBAND_MASK, input);

	return count;
}
static DEVICE_ATTR_RW(sideband_sel);

static ssize_t boot_control_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n",
			  readb(drvdata->base + XREG_BOOT_CONTROL));
}

static ssize_t boot_control_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	if (value != XREG_BOOT_CONTROL_RELEASE &&
	    value != XREG_BOOT_CONTROL_HOLD)
		return -EINVAL;

	writeb(value, drvdata->base + XREG_BOOT_CONTROL);

	return count;
}
static DEVICE_ATTR_RW(boot_control);

static ssize_t psu_enable_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n",
			  readb(drvdata->base + XREG_PSU_ENABLE));
}

static ssize_t psu_enable_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	writeb(value & 0xff, drvdata->base + XREG_PSU_ENABLE);

	return count;
}
static DEVICE_ATTR_RW(psu_enable);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n",
			  readb(drvdata->base + XREG_POWER_STATE));
}

static ssize_t power_state_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	writeb(value & 0xff, drvdata->base + XREG_POWER_STATE);

	return count;
}
static DEVICE_ATTR_RW(power_state);

static ssize_t spd_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;

	regmap_read(drvdata->xreg_map, XREG_SPD_MODE, &value);

	return sysfs_emit(buf, "0x%02x\n", value & 0xff);
}

static ssize_t spd_mode_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	regmap_write(drvdata->xreg_map, XREG_SPD_MODE, value & 0xff);

	return count;
}
static DEVICE_ATTR_RW(spd_mode);

static ssize_t soc_ready_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n", readb(drvdata->soc_ready_reg));
}
static DEVICE_ATTR_RO(soc_ready);

static ssize_t flash_select_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n", readb(drvdata->flash_select_reg));
}

static ssize_t flash_select_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	writeb(value & 0xff, drvdata->flash_select_reg);

	return count;
}
static DEVICE_ATTR_RW(flash_select);

static ssize_t soc_release_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n",
			  readb(drvdata->flash_select_reg + 1));
}

static ssize_t soc_release_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gxp_xreg_drvdata *drvdata = dev_get_drvdata(dev);
	unsigned int value;
	int rc;

	rc = kstrtouint(buf, 0, &value);
	if (rc < 0)
		return -EINVAL;

	writeb(value & 0xff, drvdata->flash_select_reg + 1);

	return count;
}
static DEVICE_ATTR_RW(soc_release);

static struct attribute *xreg_attrs[] = {
	&dev_attr_server_id.attr,
	&dev_attr_sideband_sel.attr,
	&dev_attr_boot_control.attr,
	&dev_attr_psu_enable.attr,
	&dev_attr_power_state.attr,
	&dev_attr_spd_mode.attr,
	&dev_attr_soc_ready.attr,
	&dev_attr_flash_select.attr,
	&dev_attr_soc_release.attr,
	NULL,
};
ATTRIBUTE_GROUPS(xreg);

static int gxp_gpio_xreg_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_xreg_drvdata *drvdata = gpiochip_get_data(chip);
	unsigned int val;

	switch (offset) {
	case IOP_LED1 ... IOP_LED8:
		regmap_read(drvdata->xreg_map, XREG_IOP_LED, &val);
		return !!(val & BIT(offset));
	case FAN1_INST ... FAN8_INST:
		regmap_read(drvdata->xreg_map, XREG_FAN_INSTALLED, &val);
		return !!(val & BIT((offset - FAN1_INST) + XREG_FAN1_8_INST_SHIFT));
	case FAN9_INST ... FAN16_INST:
		regmap_read(drvdata->xreg_map, XREG_FAN_INSTALLED, &val);
		return !!(val & BIT((offset - FAN9_INST) + XREG_FAN9_16_INST_SHIFT));
	case PSU1_INST ... PSU8_INST:
		regmap_read(drvdata->xreg_map, XREG_PSU_SIDEBAND, &val);
		return !!(val & BIT((offset - PSU1_INST) + XREG_PSU_PRESENCE_SHIFT));
	case PSU1_AC ... PSU8_AC:
		regmap_read(drvdata->xreg_map, XREG_PSU_POWER_STATUS, &val);
		return !!(val & BIT(offset - PSU1_AC));
	case PSU1_DC ... PSU8_DC:
		regmap_read(drvdata->xreg_map, XREG_PSU_POWER_STATUS, &val);
		return !!(val & BIT((offset - PSU1_DC) + XREG_PSU_DC_OK_SHIFT));
	case FAN1_FAIL ... FAN16_FAIL:
		regmap_read(drvdata->xreg_map, XREG_FAN_FAIL_ID, &val);
		/* Dual motor fans use x2 bits */
		return !!(val & BIT(2 * (offset - FAN1_FAIL)));
	case FAN1_ID ... FAN8_ID:
		regmap_read(drvdata->xreg_map, XREG_FAN_FAIL_ID, &val);
		return !!(val & BIT((offset - FAN1_ID) + XREG_FAN1_8_ID_SHIFT));
	case FAN9_ID ... FAN16_ID:
		regmap_read(drvdata->xreg_map, XREG_FAN_ID_EXT, &val);
		return !!(val & BIT(offset - FAN9_ID));
	case PWR_BTN_INT ... SLP_INT:
		val = readl(drvdata->base + XREG_INT_GRP5_FLAG);
		/* Active low for default */
		return !(val & BIT((offset - PWR_BTN_INT) + 16));
	case ACM_FORCE_OFF ... ACM_REQ_N:
		regmap_read(drvdata->xreg_map, XREG_LED_CTRL, &val);
		return !(val & BIT((offset - ACM_FORCE_OFF) + 16));
	default:
		return 0;
	}
}

static int gxp_gpio_xreg_set(struct gpio_chip *chip, unsigned int offset,
			      int value)
{
	struct gxp_xreg_drvdata *drvdata = gpiochip_get_data(chip);

	switch (offset) {
	case IOP_LED1 ... IOP_LED8:
		regmap_update_bits(drvdata->xreg_map, XREG_IOP_LED, BIT(offset),
				   value ? BIT(offset) : 0);
		break;
	case LED_IDENTIFY:
		regmap_update_bits(drvdata->xreg_map, XREG_IOP_LED,
				   XREG_LED_CTRL_UID_BLINK | XREG_LED_CTRL_UID_ON,
				   value ? (XREG_LED_CTRL_UID_BLINK | XREG_LED_CTRL_UID_ON)
					 : XREG_LED_CTRL_UID_BLINK);
		break;
	case LED_HEALTH_RED:
		regmap_update_bits(drvdata->xreg_map, XREG_HEALTH_LED,
				   XREG_HEALTH_LED_RED,
				   value ? XREG_HEALTH_LED_RED : 0);
		break;
	case LED_HEALTH_AMBER:
		regmap_update_bits(drvdata->xreg_map, XREG_HEALTH_LED,
				   XREG_HEALTH_LED_AMBER,
				   value ? XREG_HEALTH_LED_AMBER : 0);
		break;
	case ACM_FORCE_OFF:
		regmap_update_bits(drvdata->xreg_map, XREG_LED_CTRL,
				   XREG_LED_CTRL_ACM_OFF,
				   value ? XREG_LED_CTRL_ACM_OFF : 0);
		break;
	case ACM_REQ_N:
		regmap_update_bits(drvdata->xreg_map, XREG_LED_CTRL,
				   XREG_LED_CTRL_ACM_REQ,
				   value ? XREG_LED_CTRL_ACM_REQ : 0);
		break;
	default:
		break;
	}

	return 0;
}

static int gxp_gpio_xreg_get_direction(struct gpio_chip *chip,
					unsigned int offset)
{
	switch (offset) {
	case IOP_LED1 ... IOP_LED8:
	case LED_IDENTIFY ... LED_HEALTH_AMBER:
	case ACM_FORCE_OFF:
	case ACM_REQ_N:
		return GPIO_LINE_DIRECTION_OUT;
	default:
		return GPIO_LINE_DIRECTION_IN;
	}
}

static int gxp_gpio_xreg_direction_input(struct gpio_chip *chip,
					  unsigned int offset)
{
	switch (offset) {
	case 8 ... 55:
	case 59 ... 65:
	case 90 ... 118:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_gpio_xreg_direction_output(struct gpio_chip *chip,
					   unsigned int offset, int value)
{
	switch (offset) {
	case IOP_LED1 ... IOP_LED8:
	case LED_IDENTIFY ... LED_HEALTH_AMBER:
	case ACM_FORCE_OFF:
	case ACM_REQ_N:
		gxp_gpio_xreg_set(chip, offset, value);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void gxp_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_xreg_drvdata *drvdata = gpiochip_get_data(chip);
	unsigned int val, tmp;

	/* Clear latched interrupt for GRP5 */
	val = readl(drvdata->base + XREG_INT_GRP5_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP5_BASE);

	/* Clear latched interrupt for GRP6 */
	val = readl(drvdata->base + XREG_INT_GRP6_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP6_BASE);
}

static void gxp_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_xreg_drvdata *drvdata = gpiochip_get_data(chip);
	unsigned int val, tmp;

	/* Update GRP5 mask */
	val = readl(drvdata->base + XREG_INT_GRP5_BASE);
	tmp = val & ~(BIT(8) | BIT(10));
	if (!set)
		tmp |= BIT(8) | BIT(10);
	writel(tmp, drvdata->base + XREG_INT_GRP5_BASE);

	/* Clear interrupt flags */
	val = readl(drvdata->base + XREG_INT_GRP5_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP5_BASE);

	/* Update GRP6 mask */
	val = readl(drvdata->base + XREG_INT_GRP6_BASE);
	tmp = val & ~BIT(10);
	if (!set)
		tmp |= BIT(10);
	writel(tmp, drvdata->base + XREG_INT_GRP6_BASE);

	/* Clear interrupt flags */
	val = readl(drvdata->base + XREG_INT_GRP6_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP6_BASE);
}

static void gxp_gpio_irq_mask(struct irq_data *d)
{
	gxp_gpio_irq_set_mask(d, false);
}

static void gxp_gpio_irq_unmask(struct irq_data *d)
{
	gxp_gpio_irq_set_mask(d, true);
}

static int gxp_gpio_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static irqreturn_t gxp_xreg_irq_handle(int irq, void *_drvdata)
{
	struct gxp_xreg_drvdata *drvdata = _drvdata;
	unsigned int val, tmp;
	unsigned long flags;
	int i;

	/* Handle XREG interrupt group 5 (power button, UID, SLP) */
	val = readb(drvdata->base + XREG_INT_GRP5_FLAG);
	flags = val;
	for_each_set_bit(i, &flags, 3) {
		int hwirq = i + XREG_INT_GRP5_PIN_BASE;
		int girq = irq_find_mapping(drvdata->gpio_chip.irq.domain, hwirq);

		if (girq)
			generic_handle_irq(girq);
	}

	/* Handle XREG interrupt group 6 (PSU events) */
	val = readb(drvdata->base + XREG_INT_GRP6_FLAG);
	flags = val;
	if (flags & BIT(2)) {
		u8 old_psu = drvdata->psu_presence;
		u8 new_psu = readb(drvdata->base + XREG_PSU_PRESENCE);

		if (old_psu != new_psu) {
			for (i = 0; i < 8; i++) {
				if ((new_psu ^ old_psu) & BIT(i)) {
					int hwirq = i + XREG_INT_GRP6_PIN_BASE;
					int girq;

					girq = irq_find_mapping(drvdata->gpio_chip.irq.domain,
								hwirq);
					if (girq)
						generic_handle_irq(girq);
				}
			}
			drvdata->psu_presence = new_psu;
		}
	}

	/* Clear interrupt flags for GRP5 */
	val = readl(drvdata->base + XREG_INT_GRP5_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP5_BASE);

	/* Clear interrupt flags for GRP6 */
	val = readl(drvdata->base + XREG_INT_GRP6_BASE);
	tmp = val & ~0xFF;
	tmp |= 0xFF;
	writel(tmp, drvdata->base + XREG_INT_GRP6_BASE);

	return IRQ_HANDLED;
}

static const struct irq_chip gxp_gpio_irqchip = {
	.name		= "gxp-xreg",
	.irq_ack	= gxp_gpio_irq_ack,
	.irq_mask	= gxp_gpio_irq_mask,
	.irq_unmask	= gxp_gpio_irq_unmask,
	.irq_set_type	= gxp_gpio_set_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct of_device_id gxp_xreg_of_match[] = {
	{ .compatible = "hpe,gxp-xreg" },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_xreg_of_match);

static int gxp_xreg_probe(struct platform_device *pdev)
{
	struct gxp_xreg_drvdata *drvdata;
	struct gpio_irq_chip *girq;
	unsigned int val, tmp;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	/* Map XREG register block */
	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);
	drvdata->soc_ready_reg = drvdata->base + XREG_SOC_READY;
	drvdata->flash_select_reg = drvdata->base + XREG_FLASH_SELECT;

	drvdata->xreg_map = syscon_regmap_lookup_by_compatible("hpe,gxp-xreg");
	if (IS_ERR(drvdata->xreg_map))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->xreg_map),
				     "failed to find xreg regmap\n");

	/* Setup GPIO chip */
	drvdata->gpio_chip.label = "gxp-xreg";
	drvdata->gpio_chip.parent = &pdev->dev;
	drvdata->gpio_chip.owner = THIS_MODULE;
	drvdata->gpio_chip.get = gxp_gpio_xreg_get;
	drvdata->gpio_chip.set = gxp_gpio_xreg_set;
	drvdata->gpio_chip.get_direction = gxp_gpio_xreg_get_direction;
	drvdata->gpio_chip.direction_input = gxp_gpio_xreg_direction_input;
	drvdata->gpio_chip.direction_output = gxp_gpio_xreg_direction_output;
	drvdata->gpio_chip.base = -1;
	drvdata->gpio_chip.ngpio = 150;

	/* Setup IRQ chip */
	girq = &drvdata->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &gxp_gpio_irqchip);
	girq->handler = handle_edge_irq;
	girq->default_type = IRQ_TYPE_NONE;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, drvdata);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not register gpiochip\n");

	/* Initialize PSU presence */
	drvdata->psu_presence = readb(drvdata->base + XREG_PSU_PRESENCE);

	/* Setup interrupt from XREG - enable GRP5 and GRP6 */
	val = readl(drvdata->base + XREG_INT_HI_PRI_EN);
	tmp = val & ~(XREG_INT_GRP5_EN | XREG_INT_GRP6_EN);
	tmp |= XREG_INT_GRP5_EN | XREG_INT_GRP6_EN;
	writel(tmp, drvdata->base + XREG_INT_HI_PRI_EN);

	val = readl(drvdata->base + XREG_INT_GRP_STAT_MASK);
	tmp = val & ~(XREG_INT_GRP5_EN | XREG_INT_GRP6_EN);
	writel(tmp, drvdata->base + XREG_INT_GRP_STAT_MASK);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to get irq\n");
	drvdata->irq = ret;

	ret = devm_request_irq(&pdev->dev, drvdata->irq, gxp_xreg_irq_handle,
			       IRQF_SHARED, "gxp-xreg", drvdata);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to request irq\n");

	/* Create sysfs node under /sys/class/gxp-soc/xreg */
	drvdata->sysfs_dev = device_create_with_groups(gxp_soc_class, &pdev->dev,
						       0, drvdata, xreg_groups,
						       "xreg");
	if (IS_ERR(drvdata->sysfs_dev))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->sysfs_dev),
				     "failed to create sysfs device\n");

	return 0;
}

static void gxp_xreg_remove(struct platform_device *pdev)
{
	struct gxp_xreg_drvdata *drvdata = platform_get_drvdata(pdev);

	device_unregister(drvdata->sysfs_dev);
}

static struct platform_driver gxp_xreg_driver = {
	.probe = gxp_xreg_probe,
	.remove = gxp_xreg_remove,
	.driver = {
		.name = "gxp-xreg",
		.of_match_table = gxp_xreg_of_match,
	},
};
module_platform_driver(gxp_xreg_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_AUTHOR("Marcello Sylvester Bauer <marcello.bauer@9elements.com>");
MODULE_DESCRIPTION("HPE GXP XREG Driver");
MODULE_LICENSE("GPL");
