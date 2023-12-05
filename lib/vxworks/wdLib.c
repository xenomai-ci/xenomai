/*
 * Copyright (C) 2008-2010 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * Watchdog support.
 *
 * Not shareable (we can't tell whether the handler would always be
 * available in all processes).
 */

#include <errno.h>
#include <stdlib.h>
#include <memory.h>
#include <copperplate/heapobj.h>
#include <copperplate/threadobj.h>
#include <vxworks/errnoLib.h>

#include "wdLib.h"
#include "tickLib.h"

#define wd_magic	0x3a4b5c6d

static struct wind_wd *get_wd(WDOG_ID wdog_id)
{
	struct wind_wd *wd = (struct wind_wd *)wdog_id;

	if (wd == NULL || ((intptr_t)wd & (sizeof(intptr_t)-1)) != 0)
		return NULL;

	if (wd->magic != wd_magic)
		return NULL;

	if (timerobj_lock(&wd->tmobj))
		return NULL;

	return wd->magic != wd_magic ? NULL : wd;
}

static inline void put_wd(struct wind_wd *wd)
{
	timerobj_unlock(&wd->tmobj);
}

static void watchdog_handler(struct timerobj *tmobj)
{
	struct wind_wd *wd = container_of(tmobj, struct wind_wd, tmobj);
	wd->handler(wd->arg);
}

WDOG_ID wdCreate(void)
{
	struct wind_wd *wd;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	wd = pvmalloc(sizeof(*wd));
	if (wd == NULL)
		goto fail;

	ret = timerobj_init(&wd->tmobj);
	if (ret) {
		pvfree(wd);
	fail:
		errno = S_memLib_NOT_ENOUGH_MEMORY;
		wd = NULL;
		goto out;
	}

	wd->magic = wd_magic;
out:
	CANCEL_RESTORE(svc);

	return (WDOG_ID)wd;
}

STATUS wdDelete(WDOG_ID wdog_id)
{
	struct wind_wd *wd;
	struct service svc;
	int ret = OK;

	CANCEL_DEFER(svc);

	wd = get_wd(wdog_id);
	if (wd == NULL) {
		errno = S_objLib_OBJ_ID_ERROR;
		ret = ERROR;
		goto out;
	}

	timerobj_destroy(&wd->tmobj);
	wd->magic = ~wd_magic;
	pvfree(wd);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

STATUS wdStart(WDOG_ID wdog_id, int delay, void (*handler)(long), long arg)
{
	struct itimerspec it;
	struct service svc;
	struct wind_wd *wd;
	int ret;

	CANCEL_DEFER(svc);

	wd = get_wd(wdog_id);
	if (wd == NULL)
		goto objid_error;

	wd->handler = handler;
	wd->arg = arg;

	clockobj_ticks_to_timeout(&wind_clock, delay, &it.it_value);
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;
	ret = timerobj_start(&wd->tmobj, watchdog_handler, &it);
	if (ret) {
	objid_error:
		CANCEL_RESTORE(svc);
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_RESTORE(svc);

	return OK;
}

STATUS wdCancel(WDOG_ID wdog_id)
{
	struct wind_wd *wd;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	wd = get_wd(wdog_id);
	if (wd == NULL)
		goto objid_error;

	ret = timerobj_stop(&wd->tmobj);
	if (ret) {
	objid_error:
		CANCEL_RESTORE(svc);
		errno = S_objLib_OBJ_ID_ERROR;
		return ERROR;
	}

	CANCEL_RESTORE(svc);

	return OK;
}
