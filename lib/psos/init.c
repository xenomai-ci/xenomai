/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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
#include <getopt.h>
#include <xenomai/init.h>
#include <copperplate/registry.h>
#include <copperplate/clockobj.h>

#include "tm.h"
#include "task.h"
#include "sem.h"
#include "queue.h"
#include "pt.h"
#include "rn.h"

/**
 * @defgroup psos pSOS&reg; emulator
 *
 * A pSOS&reg; emulation library on top of Xenomai.
 *
 * The emulator mimicks the behavior described in the public
 * documentation of the pSOS 2.x API for the following class of
 * services:
 *
 * - Tasks, Events, Queues, Semaphores
 * - Partitions, Regions, Timers
 */
int psos_long_names = 0;

static unsigned int clock_resolution = 1000000; /* 1ms */

static unsigned int time_slice_in_ticks = 5;

static const struct option psos_options[] = {
	{
#define clock_resolution_opt	0
		.name = "psos-clock-resolution",
		.has_arg = required_argument,
	},
	{
#define time_slice_opt	1
		.name = "psos-time-slice",
		.has_arg = required_argument,
	},
	{
#define long_names_opt	2
		.name = "psos-long-names",
		.has_arg = no_argument,
		.flag = &psos_long_names,
		.val = 1
	},
	{ /* Sentinel */ }
};

static int psos_parse_option(int optnum, const char *optarg)
{
	switch (optnum) {
	case clock_resolution_opt:
		clock_resolution = atoi(optarg);
		break;
	case time_slice_opt:
		time_slice_in_ticks = atoi(optarg);
		break;
	case long_names_opt:
		break;
	default:
		/* Paranoid, can't happen. */
		return -EINVAL;
	}

	return 0;
}

static void psos_help(void)
{
        fprintf(stderr, "--psos-clock-resolution=<ns>	tick value (default 1ms)\n");
        fprintf(stderr, "--psos-time-slice=<psos-ticks>	round-robin time slice\n");
        fprintf(stderr, "--psos-long-names		enable long names for objects (> 4 characters)\n");
}

static int psos_init(void)
{
	int ret;

	registry_add_dir("/psos");
	registry_add_dir("/psos/tasks");
	registry_add_dir("/psos/semaphores");
	registry_add_dir("/psos/queues");
	registry_add_dir("/psos/timers");
	registry_add_dir("/psos/partitions");
	registry_add_dir("/psos/regions");

	cluster_init(&psos_task_table, "psos.task");
	cluster_init(&psos_sem_table, "psos.sema4");
	cluster_init(&psos_queue_table, "psos.queue");
	pvcluster_init(&psos_pt_table, "psos.pt");
	pvcluster_init(&psos_rn_table, "psos.rn");

	ret = clockobj_init(&psos_clock, clock_resolution);
	if (ret) {
		warning("%s: failed to initialize pSOS clock (res=%u ns)",
			__FUNCTION__, clock_resolution);
		return __bt(ret);
	}

	/* Convert pSOS ticks to timespec. */
	clockobj_ticks_to_timespec(&psos_clock, time_slice_in_ticks, &psos_rrperiod);

	return 0;
}

static struct setup_descriptor psos_skin = {
	.name = "psos",
	.init = psos_init,
	.options = psos_options,
	.parse_option = psos_parse_option,
	.help = psos_help,
};

interface_setup_call(psos_skin);
