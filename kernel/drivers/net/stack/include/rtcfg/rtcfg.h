/***
 *
 *  include/rtcfg/rtcfg.h
 *
 *  Real-Time Configuration Distribution Protocol
 *
 *  Copyright (C) 2003-2005 Jan Kiszka <jan.kiszka@web.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __RTCFG_H_INTERNAL_
#define __RTCFG_H_INTERNAL_

#include <rtdm/driver.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/***
 * RTcfg debugging
 */
#ifdef CONFIG_XENO_DRIVERS_NET_RTCFG_DEBUG

extern int rtcfg_debug;

/* use 0 for production, 1 for verification, >2 for debug */
#define RTCFG_DEFAULT_DEBUG_LEVEL 10

#define RTCFG_DEBUG(n, fmt, ...)                                               \
	(rtcfg_debug >= (n)) ? (pr_debug(fmt, ##__VA_ARGS__)) : 0
#else
#define RTCFG_DEBUG(n, fmt, ...)
#endif /* CONFIG_RTCFG_DEBUG */

#endif /* __RTCFG_H_INTERNAL_ */
