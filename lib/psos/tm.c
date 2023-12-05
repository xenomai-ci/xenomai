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

#include <memory.h>
#include <copperplate/threadobj.h>
#include <psos/psos.h>
#include "task.h"
#include "tm.h"

struct clockobj psos_clock;

struct timespec psos_rrperiod;

#define tm_magic	0x8181fcfc

static struct psos_tm *get_tm(u_long tmid, int *err_r)
{
	struct psos_tm *tm = (struct psos_tm *)tmid;

	/*
	 * Unlike most other pSOS objects (except partitions), the
	 * timer control block is NOT laid into the main heap, so
	 * don't have to apply mainheap_deref() to convert timer
	 * handles to pointers, but we do a plain cast instead.  (This
	 * said, mainheap_deref() is smart enough to deal with private
	 * pointers, but we just avoid useless overhead).
	 */
	if (tm == NULL || ((uintptr_t)tm & (sizeof(uintptr_t)-1)) != 0)
		goto objid_error;

	if (tm->magic != tm_magic)
		goto objid_error;

	if (timerobj_lock(&tm->tmobj))
		goto objid_error;

	return tm;

objid_error:
	*err_r = ERR_BADTMID;

	return NULL;
}

static void delete_timer(struct psos_tm *tm, int signal_p)
{
	struct psos_task *task;
	int ret;

	tm->magic = ~tm_magic;	/* Prevent further reference. */
	timerobj_destroy(&tm->tmobj);
	task = get_psos_task(tm->tid, &ret);
	if (task) {
		pvlist_remove(&tm->link);
		/*
		 * Send out the pending event set carried by the
		 * deleted timer to the owner thread if requested.
		 */
		if (signal_p)
			__ev_send(task, tm->events);
		put_psos_task(task);
	}
	pvfree(tm);
}

static void post_event_once(struct timerobj *tmobj)
{
	struct psos_tm *tm = container_of(tmobj, struct psos_tm, tmobj);
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);

	ret = timerobj_lock(&tm->tmobj);
	if (ret == 0)
		delete_timer(tm, 1);

	CANCEL_RESTORE(svc);
}

static void post_event_periodic(struct timerobj *tmobj)
{
	struct psos_tm *tm = container_of(tmobj, struct psos_tm, tmobj);
	ev_send(tm->tid, tm->events);
}

static u_long start_evtimer(u_long events,
			    struct itimerspec *it, u_long *tmid_r)
{
	void (*handler)(struct timerobj *tmobj);
	struct psos_task *current;
	struct psos_tm *tm;
	int ret;

	tm = pvmalloc(sizeof(*tm));
	if (tm == NULL)
		return ERR_NOTIMERS;

	pvholder_init(&tm->link);
	tm->events = events;
	tm->magic = tm_magic;

	current = get_psos_task_or_self(0, &ret);
	if (current == NULL) {
		pvfree(tm);
		return ret;
	}

	tm->tid = mainheap_ref(current, u_long);
	pvlist_append(&tm->link, &current->timer_list);

	ret = timerobj_init(&tm->tmobj);
	/*
	 * Make sure to queue fully built timers only, by holding the
	 * task lock until we are back from timerobj_init(), so that
	 * calling timerobj_destroy() from the finalizer will always
	 * be a valid operation for all queue members.
	 */
	put_psos_task(current);
	if (ret)
		goto fail;

	handler = post_event_periodic;
	if (it->it_interval.tv_sec == 0 &&
	    it->it_interval.tv_nsec == 0)
		handler = post_event_once;

	*tmid_r = (u_long)tm;

	timerobj_lock(&tm->tmobj);

	ret = timerobj_start(&tm->tmobj, handler, it);
	if (ret) {
		timerobj_destroy(&tm->tmobj);
	fail:
		pvlist_remove(&tm->link);
		pvfree(tm);
		return ERR_NOTIMERS;
	}

	return SUCCESS;
}

u_long tm_evafter(u_long ticks, u_long events, u_long *tmid_r)
{
	struct itimerspec it;
	struct service svc;
	int ret;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;

	CANCEL_DEFER(svc);
	clockobj_ticks_to_timeout(&psos_clock, ticks, &it.it_value);
	ret = start_evtimer(events, &it, tmid_r);
	CANCEL_RESTORE(svc);

	return ret;
}

u_long tm_evevery(u_long ticks, u_long events, u_long *tmid_r)
{
	struct itimerspec it;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);
	clockobj_ticks_to_timeout(&psos_clock, ticks, &it.it_value);
	clockobj_ticks_to_timespec(&psos_clock, ticks, &it.it_interval);
	ret = start_evtimer(events, &it, tmid_r);
	CANCEL_RESTORE(svc);

	return ret;
}

static int date_to_tmstruct(u_long date, u_long time, u_long ticks,
			    struct tm *tm)
{
	if (ticks > clockobj_get_frequency(&psos_clock))
		return ERR_ILLTICKS;

	memset(tm, 0, sizeof(*tm));

	tm->tm_sec = time & 0xff;
	tm->tm_min = (time >> 8) & 0xff;
	tm->tm_hour = time >> 16;
	if (tm->tm_sec > 59 || tm->tm_min > 59 || tm->tm_hour > 23)
		return ERR_ILLTIME;

	/*
	 * XXX: we want the calendar time to use the time(2) epoch,
	 * i.e. 00:00:00 UTC, January 1, 1970.
	 */

	tm->tm_mday = date & 0xff;
	tm->tm_mon = ((date >> 8) & 0xff) - 1;
	tm->tm_year = (date >> 16) - 1900;
	if (tm->tm_mday < 1 || tm->tm_mday > 31 ||
	    tm->tm_mon > 11 || tm->tm_year < 70)
		return ERR_ILLDATE;

	return SUCCESS;
}

static int date_to_timespec(u_long date, u_long time, u_long ticks,
			    struct timespec *ts)
{
	struct tm tm;
	int ret;

	ret = date_to_tmstruct(date, time, ticks, &tm);
	if (ret)
		return ret;

	clockobj_caltime_to_timeout(&psos_clock, &tm, ticks, ts);

	return SUCCESS;
}

u_long tm_evwhen(u_long date, u_long time, u_long ticks,
		 u_long events, u_long *tmid_r)
{
	struct itimerspec it;
	struct service svc;
	int ret;

	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0;

	CANCEL_DEFER(svc);

	ret = date_to_timespec(date, time, ticks, &it.it_value);
	if (ret == SUCCESS)
		ret = start_evtimer(events, &it, tmid_r);

	CANCEL_RESTORE(svc);

	return ret;
}

u_long tm_cancel(u_long tmid)
{
	struct psos_tm *tm;
	struct service svc;
	int ret = SUCCESS;

	CANCEL_DEFER(svc);

	tm = get_tm(tmid, &ret);
	if (tm)
		delete_timer(tm, 0);

	CANCEL_RESTORE(svc);

	return ret;
}

u_long tm_wkafter(u_long ticks)
{
	struct timespec rqt;
	struct service svc;

	if (ticks == 0) {
		sched_yield();	/* Manual round-robin. */
		return SUCCESS;
	}

	CANCEL_DEFER(svc);
	clockobj_ticks_to_timeout(&psos_clock, ticks, &rqt);
	CANCEL_RESTORE(svc);
	threadobj_sleep(&rqt);

	return SUCCESS;
}

u_long tm_wkwhen(u_long date, u_long time, u_long ticks)
{
	struct timespec rqt;
	struct service svc;
	int ret;

	CANCEL_DEFER(svc);
	ret = date_to_timespec(date, time, ticks, &rqt);
	CANCEL_RESTORE(svc);
	if (ret)
		return ERR_ILLDATE;

	threadobj_sleep(&rqt);

	return SUCCESS;
}

u_long tm_set(u_long date, u_long time, u_long ticks)
{
	struct service svc;
	struct tm tm;
	ticks_t t;
	int ret;

	CANCEL_DEFER(svc);

	ret = date_to_tmstruct(date, time, ticks, &tm);
	if (ret)
		goto out;

	clockobj_caltime_to_ticks(&psos_clock, &tm, ticks, &t);
	clockobj_set_date(&psos_clock, t);
out:
	CANCEL_RESTORE(svc);

	return SUCCESS;
}

u_long tm_get(u_long *date_r, u_long *time_r, u_long *ticks_r)
{
	struct service svc;
	struct tm tm;
	ticks_t t;

	CANCEL_DEFER(svc);
	clockobj_get_date(&psos_clock, &t);
	CANCEL_RESTORE(svc);
	clockobj_ticks_to_caltime(&psos_clock, t, &tm, ticks_r);
	*date_r = ((tm.tm_year + 1900) << 16) | ((tm.tm_mon + 1) << 8) | tm.tm_mday;
	*time_r = (tm.tm_hour << 16) | (tm.tm_min << 8) | tm.tm_sec;

	return SUCCESS;
}

u_long tm_getm(unsigned long long *ns_r)
{
	*ns_r = clockobj_get_ns();
	return SUCCESS;
}
