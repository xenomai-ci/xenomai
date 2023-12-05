/*
 * Copyright (C) 2015 Philippe Gerum <rpm@xenomai.org>.
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

#include <copperplate/heapobj.h>
#include <vxworks/taskHookLib.h>
#include <vxworks/errnoLib.h>

#include "taskLib.h"
#include "taskHookLib.h"

DEFINE_PRIVATE_LIST(wind_create_hooks);

DEFINE_PRIVATE_LIST(wind_delete_hooks);

static STATUS add_hook(struct pvlistobj *list, FUNCPTR hook, int prepend)
{
	struct wind_task_hook *p;

	p = xnmalloc(sizeof(*p));
	if (p == NULL)
		return ERROR;

	p->handler = (void (*)(TASK_ID))hook;
	write_lock_nocancel(&wind_task_lock);

	if (prepend)
		pvlist_prepend(&p->next, list);
	else
		pvlist_append(&p->next, list);

	write_unlock(&wind_task_lock);

	return OK;
}

static STATUS remove_hook(struct pvlistobj *list, FUNCPTR hook)
{
	struct wind_task_hook *p = NULL;

	write_lock_nocancel(&wind_task_lock);

	pvlist_for_each_entry(p, list, next) {
		if (p->handler == (void (*)(TASK_ID))hook) {
			pvlist_remove(&p->next);
			goto found;
		}
	}

	p = NULL;
found:
	write_unlock(&wind_task_lock);

	if (p) {
		xnfree(p);
		return OK;
	}

	return ERROR;
}

void wind_run_hooks(struct pvlistobj *list, struct wind_task *task)
{
	struct wind_task_hook *p;
	TASK_ID tid;

	write_lock_nocancel(&wind_task_lock);

	tid = mainheap_ref(&task->priv_tcb, TASK_ID);

	pvlist_for_each_entry(p, list, next)
		p->handler(tid);

	write_unlock(&wind_task_lock);
}

STATUS taskCreateHookAdd(FUNCPTR createHook)
{
	return add_hook(&wind_create_hooks, createHook, 0);
}

STATUS taskCreateHookDelete(FUNCPTR createHook)
{
	return remove_hook(&wind_create_hooks, createHook);
}

STATUS taskDeleteHookAdd(FUNCPTR deleteHook)
{
	return add_hook(&wind_create_hooks, deleteHook, 1);
}

STATUS taskDeleteHookDelete(FUNCPTR deleteHook)
{
	return remove_hook(&wind_create_hooks, deleteHook);
}
