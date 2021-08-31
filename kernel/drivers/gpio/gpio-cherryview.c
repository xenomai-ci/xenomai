// SPDX-License-Identifier: GPL-2.0
/*
 * @note Copyright (C) 2021 Hongzhan Chen <hongzhan.chen@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <rtdm/gpio.h>

#define RTDM_SUBCLASS_CHERRYVIEW  7

static const char *label_array[] = {
	"INT33FF:00",
	"INT33FF:01",
	"INT33FF:02",
	"INT33FF:03",
};

static int __init cherryview_gpio_init(void)
{
	return rtdm_gpiochip_array_find(NULL, label_array,
					ARRAY_SIZE(label_array),
					RTDM_SUBCLASS_CHERRYVIEW);
}
module_init(cherryview_gpio_init);

static void __exit cherryview_gpio_exit(void)
{
	rtdm_gpiochip_remove_by_type(RTDM_SUBCLASS_CHERRYVIEW);
}
module_exit(cherryview_gpio_exit);

MODULE_LICENSE("GPL");
