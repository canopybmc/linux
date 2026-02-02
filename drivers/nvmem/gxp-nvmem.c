// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP Virtual EEPROM NVMEM Driver
 *
 * Copyright (C) 2026 9elements GmbH
 *
 * The GXP BMC SoC contains a memory-mapped virtual EEPROM that stores board
 * identification data, including the serial number, part number, and MAC
 * addresses.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define GXP_EEPROM_BLOCK_SIZE	128

struct gxp_nvmem {
	void __iomem *base;
};

static bool gxp_nvmem_validate_block(void __iomem *block)
{
	u16 sum = 0;
	u8 version;
	int i;

	version = readb(block);
	if (version != 2 && version != 3)
		return false;

	for (i = 0; i < GXP_EEPROM_BLOCK_SIZE / sizeof(u16); i++)
		sum += readw(block + i * sizeof(u16));

	return sum == 0;
}

static int gxp_nvmem_read(void *context, unsigned int offset,
			  void *val, size_t bytes)
{
	struct gxp_nvmem *priv = context;

	memcpy_fromio(val, priv->base + offset, bytes);
	return 0;
}

static int gxp_nvmem_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.name = "gxp-nvmem",
		.id = NVMEM_DEVID_AUTO,
		.reg_read = gxp_nvmem_read,
		.read_only = true,
	};
	struct gxp_nvmem *priv;
	struct resource *res;
	int i, num_blocks;
	size_t size;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	size = resource_size(res);
	if (size == 0 || size % GXP_EEPROM_BLOCK_SIZE != 0) {
		dev_err(&pdev->dev, "size must be a multiple of %d bytes\n",
			GXP_EEPROM_BLOCK_SIZE);
		return -EINVAL;
	}

	num_blocks = size / GXP_EEPROM_BLOCK_SIZE;
	for (i = 0; i < num_blocks; i++) {
		if (!gxp_nvmem_validate_block(priv->base +
					      i * GXP_EEPROM_BLOCK_SIZE)) {
			dev_err(&pdev->dev, "checksum failed for block %d\n", i);
			return -EBADMSG;
		}
	}

	config.dev = &pdev->dev;
	config.priv = priv;
	config.size = size;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(&pdev->dev, &config));
}

static const struct of_device_id gxp_nvmem_match[] = {
	{ .compatible = "hpe,gxp-nvmem" },
	{ }
};
MODULE_DEVICE_TABLE(of, gxp_nvmem_match);

static struct platform_driver gxp_nvmem_driver = {
	.probe = gxp_nvmem_probe,
	.driver = {
		.name = "gxp-nvmem",
		.of_match_table = gxp_nvmem_match,
	},
};
module_platform_driver(gxp_nvmem_driver);

MODULE_AUTHOR("Marcello Sylvester Bauer <marcello.bauer@9elements.com>");
MODULE_DESCRIPTION("HPE GXP Virtual EEPROM NVMEM Driver");
MODULE_LICENSE("GPL");
