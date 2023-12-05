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

#include <assert.h>
#include <errno.h>
#include "copperplate/threadobj.h"
#include "copperplate/eventobj.h"

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

int eventobj_init(struct eventobj *evobj, unsigned int value, int flags,
		  fnref_type(void (*)(struct eventobj *evobj)) finalizer)
{
	int ret, event_flags = event_scope_attribute;

	if (flags & EVOBJ_PRIO)
		event_flags |= COBALT_EVENT_PRIO;

	ret = cobalt_event_init(&evobj->core.event, value, event_flags);
	if (ret)
		return __bt(ret);

	evobj->finalizer = finalizer;

	return 0;
}

int eventobj_destroy(struct eventobj *evobj)
{
	void (*finalizer)(struct eventobj *evobj);
	int ret;

	ret = cobalt_event_destroy(&evobj->core.event);
	if (ret)
		return ret;

	fnref_get(finalizer, evobj->finalizer);
	finalizer(evobj);

	return 0;
}

void eventobj_uninit(struct eventobj *evobj)
{
	int ret = cobalt_event_destroy(&evobj->core.event);
	assert(ret == 0);
	(void)ret;
}

int eventobj_wait(struct eventobj *evobj,
		  unsigned int bits, unsigned int *bits_r,
		  int mode, const struct timespec *timeout)
{
	int ret;

	ret = cobalt_event_wait(&evobj->core.event,
				bits, bits_r, mode, timeout);
	if (ret)
		return ret;

	return 0;
}

int eventobj_post(struct eventobj *evobj, unsigned int bits)
{
	int ret;

	ret = cobalt_event_post(&evobj->core.event, bits);
	if (ret)
		return ret;

	return 0;
}

int eventobj_clear(struct eventobj *evobj, unsigned int bits,
		   unsigned int *bits_r)
{
	unsigned int oldval;

	oldval = cobalt_event_clear(&evobj->core.event, bits);
	if (bits_r)
		*bits_r = oldval;

	return 0;
}

int eventobj_inquire(struct eventobj *evobj, size_t waitsz,
		     struct eventobj_waitentry *waitlist,
		     unsigned int *bits_r)
{
	struct cobalt_threadstat stat;
	struct cobalt_event_info info;
	int nrwait, pidsz, n, ret;
	pid_t *pidlist = NULL;

	pidsz = sizeof(pid_t) * (waitsz / sizeof(*waitlist));
	if (pidsz > 0) {
		pidlist = pvmalloc(pidsz);
		if (pidlist == NULL)
			return -ENOMEM;
	}

	nrwait = cobalt_event_inquire(&evobj->core.event, &info, pidlist, pidsz);
	if (nrwait < 0)
		goto out;

	*bits_r = info.value;

	if (pidlist == NULL)
		return nrwait;

	for (n = 0; n < nrwait; n++, waitlist++) {
		ret = cobalt_thread_stat(pidlist[n], &stat);
		/* If waiter disappeared, fill in a dummy entry. */
		if (ret) {
			waitlist->pid = -1;
			strcpy(waitlist->name, "???");
		} else {
			waitlist->pid = pidlist[n];
			strcpy(waitlist->name, stat.name);
		}
	}
out:
	if (pidlist)
		pvfree(pidlist);

	return nrwait;
}

#else /* CONFIG_XENO_MERCURY */

static void eventobj_finalize(struct syncobj *sobj)
{
	struct eventobj *evobj = container_of(sobj, struct eventobj, core.sobj);
	void (*finalizer)(struct eventobj *evobj);

	fnref_get(finalizer, evobj->finalizer);
	finalizer(evobj);
}
fnref_register(libcopperplate, eventobj_finalize);

int eventobj_init(struct eventobj *evobj, unsigned int value, int flags,
		  fnref_type(void (*)(struct eventobj *evobj)) finalizer)
{
	int sobj_flags = 0, ret;

	if (flags & EVOBJ_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	ret = syncobj_init(&evobj->core.sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libcopperplate, eventobj_finalize));
	if (ret)
		return __bt(ret);

	evobj->core.flags = flags;
	evobj->core.value = value;
	evobj->finalizer = finalizer;

	return 0;
}

int eventobj_destroy(struct eventobj *evobj)
{
	struct syncstate syns;
	int ret;

	if (syncobj_lock(&evobj->core.sobj, &syns))
		return -EINVAL;

	ret = syncobj_destroy(&evobj->core.sobj, &syns);
	if (ret < 0)
		return ret;

	return 0;
}

void eventobj_uninit(struct eventobj *evobj)
{
	syncobj_uninit(&evobj->core.sobj);
}

int eventobj_wait(struct eventobj *evobj,
		  unsigned int bits, unsigned int *bits_r,
		  int mode, const struct timespec *timeout)
{
	struct eventobj_wait_struct *wait;
	unsigned int waitval, testval;
	struct syncstate syns;
	int ret = 0;

	ret = syncobj_lock(&evobj->core.sobj, &syns);
	if (ret)
		return ret;

	if (bits == 0) {
		*bits_r = evobj->core.value;
		goto done;
	}

	waitval = evobj->core.value & bits;
	testval = mode & EVOBJ_ANY ? waitval : bits;

	if (waitval && waitval == testval) {
		*bits_r = waitval;
		goto done;
	}

	/* Have to wait. */

	if (timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	wait = threadobj_prepare_wait(struct eventobj_wait_struct);
	wait->value = bits;
	wait->mode = mode;

	ret = syncobj_wait_grant(&evobj->core.sobj, timeout, &syns);
	if (ret == -EIDRM) {
		threadobj_finish_wait();
		return ret;
	}

	if (ret == 0)
		*bits_r = wait->value;

	threadobj_finish_wait();
done:
	syncobj_unlock(&evobj->core.sobj, &syns);

	return ret;
}

int eventobj_post(struct eventobj *evobj, unsigned int bits)
{
	struct eventobj_wait_struct *wait;
	unsigned int waitval, testval;
	struct threadobj *thobj, *tmp;
	struct syncstate syns;
	int ret;

	ret = syncobj_lock(&evobj->core.sobj, &syns);
	if (ret)
		return ret;

	evobj->core.value |= bits;

	if (!syncobj_grant_wait_p(&evobj->core.sobj))
		goto done;

	syncobj_for_each_grant_waiter_safe(&evobj->core.sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		waitval = wait->value & bits;
		testval = wait->mode & EVOBJ_ANY ? waitval : wait->value;
		if (waitval && waitval == testval) {
			wait->value = waitval;
			syncobj_grant_to(&evobj->core.sobj, thobj);
		}
	}
done:
	syncobj_unlock(&evobj->core.sobj, &syns);

	return 0;
}

int eventobj_clear(struct eventobj *evobj,
		   unsigned int bits,
		   unsigned int *bits_r)
{
	struct syncstate syns;
	unsigned int oldval;
	int ret;

	ret = syncobj_lock(&evobj->core.sobj, &syns);
	if (ret)
		return ret;

	oldval = evobj->core.value;
	evobj->core.value &= ~bits;

	syncobj_unlock(&evobj->core.sobj, &syns);

	if (bits_r)
		*bits_r = oldval;

	return 0;
}

int eventobj_inquire(struct eventobj *evobj, size_t waitsz,
		     struct eventobj_waitentry *waitlist,
		     unsigned int *bits_r)
{
	struct threadobj *thobj;
	struct syncstate syns;
	int ret, nrwait;

	ret = syncobj_lock(&evobj->core.sobj, &syns);
	if (ret)
		return ret;

	nrwait = syncobj_count_grant(&evobj->core.sobj);
	if (nrwait > 0) {
		syncobj_for_each_grant_waiter(&evobj->core.sobj, thobj) {
			waitlist->pid = threadobj_get_pid(thobj);
			strcpy(waitlist->name, threadobj_get_name(thobj));
			waitlist++;
		}
	}

	*bits_r = evobj->core.value;

	syncobj_unlock(&evobj->core.sobj, &syns);

	return nrwait;
}

#endif /* CONFIG_XENO_MERCURY */
