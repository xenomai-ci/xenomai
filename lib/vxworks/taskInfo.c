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

#include <pthread.h>
#include <string.h>
#include <boilerplate/ancillaries.h>
#include <copperplate/threadobj.h>
#include <vxworks/errnoLib.h>
#include <vxworks/taskInfo.h>

#include "taskLib.h"

const char *taskName(TASK_ID task_id)
{
	struct wind_task *task;
	struct service svc;
	const char *name;

	CANCEL_DEFER(svc);

	task = get_wind_task_or_self(task_id);
	if (task == NULL) {
		name = NULL;
		goto out;
	}

	name = task->name;
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);
	/*
	 * This is racy and unsafe, but this service is terminally
	 * flawed by design anyway.
	 */
	return name;
}

TASK_ID taskIdDefault(TASK_ID task_id)
{
	static TASK_ID value;

	if (task_id)
		value = task_id;

	return value;
}

TASK_ID taskNameToId(const char *name)
{
	struct clusterobj *cobj;
	struct wind_task *task;
	struct service svc;

	CANCEL_DEFER(svc);
	cobj = cluster_findobj(&wind_task_table, name);
	CANCEL_RESTORE(svc);
	if (cobj == NULL)
		return ERROR;

	task = container_of(cobj, struct wind_task, cobj);

	return (TASK_ID)task->tcb;
}

BOOL taskIsReady(TASK_ID task_id)
{
	struct wind_task *task;
	struct service svc;
	int status = 0;

	CANCEL_DEFER(svc);

	task = get_wind_task(task_id);
	if (task == NULL)
		goto out;

	status = get_task_status(task);
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);

	return status == WIND_READY;
}

BOOL taskIsSuspended(TASK_ID task_id)
{
	struct wind_task *task;
	struct service svc;
	int status = 0;

	CANCEL_DEFER(svc);

	task = get_wind_task(task_id);
	if (task == NULL)
		goto out;

	status = threadobj_get_status(&task->thobj);
	put_wind_task(task);
out:
	CANCEL_RESTORE(svc);

	return (status & __THREAD_S_SUSPENDED) != 0;
}

STATUS taskGetInfo(TASK_ID task_id, TASK_DESC *desc)
{
	int vfirst, vlast, ret;
	struct wind_task *task;
	struct WIND_TCB *tcb;
	pthread_attr_t attr;
	STATUS status = OK;
	struct service svc;
	size_t stacksize;
	void *stackbase;

	CANCEL_DEFER(svc);

	task = get_wind_task(task_id);
	if (task == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		status = ERROR;
		goto out;
	}

	tcb = task->tcb;
	desc->td_tid = task_id;
	desc->td_priority = wind_task_get_priority(task);
	desc->td_status = get_task_status(task);
	desc->td_flags = tcb->flags;
	namecpy(desc->td_name, task->name);
	desc->td_entry = tcb->entry;
	desc->td_errorStatus = *task->thobj.errno_pointer;
	ret = pthread_getattr_np(task->thobj.ptid, &attr);
	put_wind_task(task);

	/*
	 * If the target does not support pthread_getattr_np(), we are
	 * out of luck for determining the stack information. We just
	 * zero it.
	 */
	if (ret) {
		/* No idea, buddy. */
		desc->td_stacksize = 0;
		desc->td_pStackBase = NULL;
	} else {
		pthread_attr_getstack(&attr, &stackbase, &stacksize);
		desc->td_stacksize = stacksize;
		desc->td_pStackBase = stackbase;

		if (&vfirst < &vlast)
			/* Stack grows upward. */
			desc->td_pStackEnd = (caddr_t)stackbase + stacksize;
		else
			/* Stack grows downward. */
			desc->td_pStackEnd = (caddr_t)stackbase - stacksize;
	}
out:
	CANCEL_RESTORE(svc);

	return status;
}
