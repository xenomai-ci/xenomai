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
#include <getopt.h>
#include <xenomai/init.h>

#include "timer.h"
#include "task.h"
#include "sem.h"
#include "event.h"
#include "cond.h"
#include "mutex.h"
#include "queue.h"
#include "buffer.h"
#include "heap.h"
#include "alarm.h"
#include "pipe.h"

/**
 * @defgroup alchemy Alchemy API
 *
 * A programming interface reminiscent from traditional RTOS APIs
 *
 * This interface is an evolution of the former @a native API
 * available with the Xenomai 2.x series.
 */

static unsigned int clock_resolution = 1; /* nanosecond. */

static const struct option alchemy_options[] = {
	{
#define clock_resolution_opt	0
		.name = "alchemy-clock-resolution",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
};

static int alchemy_parse_option(int optnum, const char *optarg)
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

static void alchemy_help(void)
{
        fprintf(stderr, "--alchemy-clock-resolution=<ns> tick value (default 1ns, tickless)\n");
}

#ifdef CONFIG_XENO_COBALT

static inline void init_corespec(void)
{
	syncluster_init(&alchemy_pipe_table, "alchemy.pipe");
	registry_add_dir("/alchemy/pipes");
}

#else

static inline void init_corespec(void) { }

#endif

static int alchemy_init(void)
{
	int ret;

	syncluster_init(&alchemy_task_table, "alchemy.task");
	syncluster_init(&alchemy_sem_table, "alchemy.sem");
	syncluster_init(&alchemy_event_table, "alchemy.event");
	syncluster_init(&alchemy_cond_table, "alchemy.cond");
	syncluster_init(&alchemy_mutex_table, "alchemy.mutex");
	syncluster_init(&alchemy_queue_table, "alchemy.queue");
	syncluster_init(&alchemy_buffer_table, "alchemy.buffer");
	syncluster_init(&alchemy_heap_table, "alchemy.heap");
	pvcluster_init(&alchemy_alarm_table, "alchemy.alarm");

	ret = clockobj_init(&alchemy_clock, clock_resolution);
	if (ret) {
		warning("%s: failed to initialize Alchemy clock (res=%u ns)",
			__FUNCTION__, clock_resolution);
		return __bt(ret);
	}

	registry_add_dir("/alchemy");
	registry_add_dir("/alchemy/tasks");
	registry_add_dir("/alchemy/semaphores");
	registry_add_dir("/alchemy/events");
	registry_add_dir("/alchemy/condvars");
	registry_add_dir("/alchemy/mutexes");
	registry_add_dir("/alchemy/queues");
	registry_add_dir("/alchemy/buffers");
	registry_add_dir("/alchemy/heaps");
	registry_add_dir("/alchemy/alarms");

	init_corespec();

	return 0;
}

static struct setup_descriptor alchemy_skin = {
	.name = "alchemy",
	.init = alchemy_init,
	.options = alchemy_options,
	.parse_option = alchemy_parse_option,
	.help = alchemy_help,
};

interface_setup_call(alchemy_skin);
