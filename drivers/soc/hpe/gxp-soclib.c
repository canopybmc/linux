// SPDX-License-Identifier: GPL-2.0
/*
 * HPE GXP SoC class library
 *
 * Copyright (C) 2019 Hewlett Packard Enterprise Development LP.
 * Copyright (C) 2026 9elements GmbH
 *
 * This provides a common soc_class for GXP SoC drivers to create sysfs
 * device nodes under /sys/class/soc/.
 */

#include <linux/device.h>
#include <linux/module.h>

const struct class *gxp_soc_class;
EXPORT_SYMBOL_GPL(gxp_soc_class);

static int __init gxp_soclib_init(void)
{
	gxp_soc_class = class_create("gxp-soc");
	if (IS_ERR(gxp_soc_class))
		return PTR_ERR(gxp_soc_class);
	return 0;
}

static void __exit gxp_soclib_exit(void)
{
	class_destroy((struct class *)gxp_soc_class);
}

subsys_initcall(gxp_soclib_init);
module_exit(gxp_soclib_exit);

MODULE_AUTHOR("Gilbert Chen <gilbert.chen@hpe.com>");
MODULE_AUTHOR("Marcello Sylvester Bauer <marcello.bauer@9elements.com>");
MODULE_DESCRIPTION("HPE GXP SoC class library");
MODULE_LICENSE("GPL");
