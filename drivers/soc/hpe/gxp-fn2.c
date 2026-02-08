// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP FN2 (Embedded Management Processor Support and Configuration) driver
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 *
 * This driver provides GPIO functionality for power state control on the
 * HPE GXP BMC SoC. It exposes GPIO lines for VPBTN (virtual power button),
 * PGOOD, PERST, and POST_COMPLETE with interrupt support.
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

/* GPIO pin definitions */
enum fn2_gpio_pin {
	VPBTN = 0,		/* Output: virtual power button */
	PGOOD,			/* Input: power good */
	PERST,			/* Input: PCIe reset */
	POST_COMPLETE,		/* Input: POST complete */
	BTN_STATE,		/* Input: button state */
};

struct gxp_fn2_drvdata {
	void __iomem *base;
	struct regmap *fn2_map;
	struct gpio_chip gpio_chip;
	int irq;
	int btn_state;
};

static int gxp_fn2_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gxp_fn2_drvdata *drvdata = gpiochip_get_data(chip);
	unsigned int val;

	switch (offset) {
	case VPBTN:
		return 0; /* Write-only */
	case BTN_STATE:
		return drvdata->btn_state;
	case PGOOD:
		regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);
		return !!(val & FN2_SEVSTAT_PGOOD_STATE);
	case PERST:
		regmap_read(drvdata->fn2_map, FN2_SEVSTAT, &val);
		return !!(val & FN2_SEVSTAT_PERST_STATE);
	case POST_COMPLETE:
		/* TODO: read from SRAM */
		return 0;
	default:
		return 0;
	}
}

static int gxp_fn2_gpio_set(struct gpio_chip *chip, unsigned int offset,
			     int value)
{
	struct gxp_fn2_drvdata *drvdata = gpiochip_get_data(chip);
	unsigned int val;

	switch (offset) {
	case VPBTN:
		/* Set virtual power button */
		regmap_update_bits(drvdata->fn2_map, FN2_VPBTN_CTRL,
				   FN2_VPBTN_PRESSED,
				   value ? FN2_VPBTN_PRESSED : 0);
		regmap_read(drvdata->fn2_map, FN2_VPBTN_CTRL, &val);
		drvdata->btn_state = !!(val & FN2_VPBTN_PRESSED);
		break;
	default:
		break;
	}

	return 0;
}

static int gxp_fn2_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	switch (offset) {
	case VPBTN:
		return GPIO_LINE_DIRECTION_OUT;
	default:
		return GPIO_LINE_DIRECTION_IN;
	}
}

static int gxp_fn2_gpio_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	switch (offset) {
	case PGOOD:
	case PERST:
	case BTN_STATE:
	case POST_COMPLETE:
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gxp_fn2_gpio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int value)
{
	switch (offset) {
	case VPBTN:
		gxp_fn2_gpio_set(chip, offset, value);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static void gxp_fn2_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_fn2_drvdata *drvdata = gpiochip_get_data(chip);

	/* Clear latched interrupt */
	regmap_update_bits(drvdata->fn2_map, FN2_SEVSTAT, 0xFFFF, 0xFFFF);
}

static void gxp_fn2_gpio_irq_set_mask(struct irq_data *d, bool set)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);
	struct gxp_fn2_drvdata *drvdata = gpiochip_get_data(chip);

	regmap_update_bits(drvdata->fn2_map, FN2_SEVMASK, FN2_SEVMASK_EN,
			   set ? FN2_SEVMASK_EN : 0);
}

static void gxp_fn2_gpio_irq_mask(struct irq_data *d)
{
	gxp_fn2_gpio_irq_set_mask(d, false);
}

static void gxp_fn2_gpio_irq_unmask(struct irq_data *d)
{
	gxp_fn2_gpio_irq_set_mask(d, true);
}

static int gxp_fn2_gpio_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static irqreturn_t gxp_fn2_irq_handle(int irq, void *_drvdata)
{
	struct gxp_fn2_drvdata *drvdata = _drvdata;
	unsigned int val;
	int girq;

	/* Handle system event */
	val = readb(drvdata->base + FN2_SEVSTAT);

	if (val & FN2_SEVSTAT_PGOOD) {
		girq = irq_find_mapping(drvdata->gpio_chip.irq.domain, PGOOD);
		if (girq)
			generic_handle_irq(girq);
		else
			regmap_update_bits(drvdata->fn2_map, FN2_SEVSTAT,
					   0xFFFF, 0xFFFF);
	}

	if (val & FN2_SEVSTAT_PERST) {
		girq = irq_find_mapping(drvdata->gpio_chip.irq.domain, PERST);
		if (girq)
			generic_handle_irq(girq);
		else
			regmap_update_bits(drvdata->fn2_map, FN2_SEVSTAT,
					   0xFFFF, 0xFFFF);
	}

	return IRQ_HANDLED;
}

static const struct irq_chip gxp_fn2_irqchip = {
	.name		= "gxp-fn2",
	.irq_ack	= gxp_fn2_gpio_irq_ack,
	.irq_mask	= gxp_fn2_gpio_irq_mask,
	.irq_unmask	= gxp_fn2_gpio_irq_unmask,
	.irq_set_type	= gxp_fn2_gpio_set_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct of_device_id gxp_fn2_of_match[] = {
	{ .compatible = "hpe,gxp-fn2" },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_fn2_of_match);

static int gxp_fn2_probe(struct platform_device *pdev)
{
	struct gxp_fn2_drvdata *drvdata;
	struct gpio_irq_chip *girq;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	platform_set_drvdata(pdev, drvdata);

	/* Map FN2 registers */
	drvdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(drvdata->base))
		return PTR_ERR(drvdata->base);

	drvdata->fn2_map = syscon_regmap_lookup_by_compatible("hpe,gxp-fn2");
	if (IS_ERR(drvdata->fn2_map))
		return dev_err_probe(&pdev->dev, PTR_ERR(drvdata->fn2_map),
				     "failed to find fn2 regmap\n");

	/* Setup GPIO chip */
	drvdata->gpio_chip.label = "gxp-fn2";
	drvdata->gpio_chip.parent = &pdev->dev;
	drvdata->gpio_chip.owner = THIS_MODULE;
	drvdata->gpio_chip.get = gxp_fn2_gpio_get;
	drvdata->gpio_chip.set = gxp_fn2_gpio_set;
	drvdata->gpio_chip.get_direction = gxp_fn2_gpio_get_direction;
	drvdata->gpio_chip.direction_input = gxp_fn2_gpio_direction_input;
	drvdata->gpio_chip.direction_output = gxp_fn2_gpio_direction_output;
	drvdata->gpio_chip.base = -1;
	drvdata->gpio_chip.ngpio = 50;

	/* Setup IRQ chip */
	girq = &drvdata->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &gxp_fn2_irqchip);
	girq->handler = handle_edge_irq;
	girq->default_type = IRQ_TYPE_NONE;

	ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, drvdata);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret,
				     "could not register gpiochip\n");

	/* Setup interrupt from FN2 system event register */
	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to get irq\n");
	drvdata->irq = ret;

	ret = devm_request_irq(&pdev->dev, drvdata->irq, gxp_fn2_irq_handle,
			       IRQF_SHARED, "gxp-fn2", drvdata);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "failed to request irq\n");

	return 0;
}

static struct platform_driver gxp_fn2_driver = {
	.probe = gxp_fn2_probe,
	.driver = {
		.name = "gxp-fn2",
		.of_match_table = gxp_fn2_of_match,
	},
};
module_platform_driver(gxp_fn2_driver);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_AUTHOR("Marcello Sylvester Bauer <marcello.bauer@9elements.com>");
MODULE_DESCRIPTION("HPE GXP FN2 Driver");
MODULE_LICENSE("GPL");
