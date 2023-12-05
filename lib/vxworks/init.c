/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <xenomai/init.h>
#include <vxworks/errnoLib.h>

#include "tickLib.h"
#include "taskLib.h"

/**
 * @defgroup vxworks VxWorks&reg; emulator
 *
 * A VxWorks&reg; emulation library on top of Xenomai.
 *
 * The emulator mimicks the behavior described in the public
 * documentation of the WIND 5.x API for the following class of
 * services:
 *
 * - taskLib, taskInfoLib, taskHookLib,
 * - semLib, msgQLib, wdLib, memPartLib
 * - intLib, tickLib, sysLib (partial)
 * - errnoLib, lstLib, kernelLib (partial)
 */

static unsigned int clock_resolution = 1000000; /* 1ms */

static const struct option vxworks_options[] = {
	{
#define clock_resolution_opt	0
		.name = "vxworks-clock-resolution",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
};

static int vxworks_parse_option(int optnum, const char *optarg)
{
	switch (optnum) {
	case clock_resolution_opt:
		clock_resolution = atoi(optarg);
		break;
	default:
		/* Paranoid, can't happen. */
		return -EINVAL;
	}

	return 0;
}

static void vxworks_help(void)
{
        fprintf(stderr, "--vxworks-clock-resolution=<ns> tick value (default 1ms)\n");
}

static int vxworks_init(void)
{
	int ret;

	registry_add_dir("/vxworks");
	registry_add_dir("/vxworks/tasks");
	registry_add_dir("/vxworks/semaphores");
	registry_add_dir("/vxworks/queues");
	registry_add_dir("/vxworks/watchdogs");

	cluster_init(&wind_task_table, "vxworks.task");

	ret = clockobj_init(&wind_clock, clock_resolution);
	if (ret) {
		warning("%s: failed to initialize VxWorks clock (res=%u ns)",
			__FUNCTION__, clock_resolution);
		return __bt(ret);
	}

	__RT(pthread_mutex_init(&wind_task_lock, NULL));

	return 0;
}

static struct setup_descriptor vxworks_skin = {
	.name = "vxworks",
	.init = vxworks_init,
	.options = vxworks_options,
	.parse_option = vxworks_parse_option,
	.help = vxworks_help,
};

interface_setup_call(vxworks_skin);
