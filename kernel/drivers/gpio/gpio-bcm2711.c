// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Siemens AG 2024
 *
 * Authors:
 *  Clara Kowalsky <clara.kowalsky@siemens.com>
 */
#include <linux/module.h>
#include <rtdm/gpio.h>

#define RTDM_SUBCLASS_BCM2711  1

static int __init bcm2711_gpio_init(void)
{
	return rtdm_gpiochip_scan_of(NULL, "brcm,bcm2711-gpio",
				     RTDM_SUBCLASS_BCM2711);
}
module_init(bcm2711_gpio_init);

static void __exit bcm2711_gpio_exit(void)
{
	rtdm_gpiochip_remove_by_type(RTDM_SUBCLASS_BCM2711);
}
module_exit(bcm2711_gpio_exit);

MODULE_LICENSE("GPL");
