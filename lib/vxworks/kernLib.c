/*
 * Copyright (C) 2012 Philippe Gerum <rpm@xenomai.org>.
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

#include <vxworks/kernLib.h>
#include <vxworks/errnoLib.h>

#include "tickLib.h"
#include "taskLib.h"

static int switch_slicing(struct threadobj *thobj, struct timespec *quantum)
{
	struct sched_param_ex param_ex;
	int policy;

	param_ex.sched_priority = threadobj_get_priority(thobj);

	if (quantum) {
		policy = SCHED_RR;
		param_ex.sched_rr_quantum.tv_sec = quantum->tv_sec;
		param_ex.sched_rr_quantum.tv_nsec = quantum->tv_nsec;
	} else {
		policy = param_ex.sched_priority ? SCHED_FIFO : SCHED_OTHER;
	}

	return threadobj_set_schedparam(thobj, policy, &param_ex);
}

STATUS kernelTimeSlice(int ticks)
{
	struct timespec quantum, *p = NULL;
	struct wind_task *task;

	if (ticks) {
		/* Convert VxWorks ticks to timespec. */
		clockobj_ticks_to_timespec(&wind_clock, ticks, &quantum);
		p = &quantum;
	}

	/*
	 * Enable/disable round-robin for all threads known by the
	 * current process.
	 */
	wind_time_slice = ticks;
	do_each_wind_task(task, switch_slicing(&task->thobj, p));

	return OK;
}
