/*
 * Copyright (C) 2010 Philippe Gerum <rpm@xenomai.org>.
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
 * Timer object abstraction.
 */

#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include "boilerplate/list.h"
#include "boilerplate/signal.h"
#include "boilerplate/lock.h"
#include "copperplate/threadobj.h"
#include "copperplate/timerobj.h"
#include "copperplate/clockobj.h"
#include "copperplate/debug.h"
#include "internal.h"

static pthread_mutex_t svlock;

static pthread_t svthread;

static pid_t svpid;

static DEFINE_PRIVATE_LIST(svtimers);

#ifdef CONFIG_XENO_COBALT

static inline void timersv_init_corespec(void) { }

#else /* CONFIG_XENO_MERCURY */

static inline void timersv_init_corespec(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
}

#endif /* CONFIG_XENO_MERCURY */

/*
 * XXX: at some point, we may consider using a timer wheel instead of
 * a simple linked list to index timers. The latter method is
 * efficient for up to ten outstanding timers or so, which should be
 * enough for most applications. However, there exist poorly designed
 * apps involving dozens of active timers, particularly in the legacy
 * embedded world.
 */
static void timerobj_enqueue(struct timerobj *tmobj)
{
	struct timerobj *__tmobj;

	if (pvlist_empty(&svtimers)) {
		pvlist_append(&tmobj->next, &svtimers);
		return;
	}

	pvlist_for_each_entry_reverse(__tmobj, &svtimers, next) {
		if (timespec_before_or_same(&__tmobj->itspec.it_value,
					    &tmobj->itspec.it_value))
			break;
	}

	atpvh(&__tmobj->next, &tmobj->next);
}

static int server_prologue(void *arg)
{
	svpid = get_thread_pid();
	copperplate_set_current_name("timer-internal");
	timersv_init_corespec();
	threadobj_set_current(THREADOBJ_IRQCONTEXT);

	return 0;
}

static void *timerobj_server(void *arg)
{
	void (*handler)(struct timerobj *tmobj);
	struct timespec now, value, interval;
	struct timerobj *tmobj, *tmp;
	sigset_t set;
	int sig, ret;

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	for (;;) {
		ret = __RT(sigwait(&set, &sig));
		if (ret && ret != -EINTR)
			break;
		/*
		 * We have a single server thread for now, so handlers
		 * are fully serialized.
		 */
		write_lock_nocancel(&svlock);

		__RT(clock_gettime(CLOCK_COPPERPLATE, &now));

		pvlist_for_each_entry_safe(tmobj, tmp, &svtimers, next) {
			value = tmobj->itspec.it_value;
			interval = tmobj->itspec.it_interval;
			handler = tmobj->handler;
			if (timespec_after(&value, &now))
				break;
			pvlist_remove_init(&tmobj->next);
			if (interval.tv_sec > 0 || interval.tv_nsec > 0) {
				timespec_add(&tmobj->itspec.it_value,
					     &value, &interval);
				timerobj_enqueue(tmobj);
			}
			write_unlock(&svlock);
			handler(tmobj);
			write_lock_nocancel(&svlock);
		}

		write_unlock(&svlock);
	}

	return NULL;
}

static void timerobj_spawn_server(void)
{
	struct corethread_attributes cta;

	cta.policy = SCHED_CORE;
	cta.param_ex.sched_priority = threadobj_irq_prio;
	cta.prologue = server_prologue;
	cta.run = timerobj_server;
	cta.arg = NULL;
	cta.stacksize = PTHREAD_STACK_DEFAULT;
	cta.detachstate = PTHREAD_CREATE_DETACHED;

	__bt(copperplate_create_thread(&cta, &svthread));
}

int timerobj_init(struct timerobj *tmobj)
{
	static pthread_once_t spawn_once;
	pthread_mutexattr_t mattr;
	struct sigevent sev;
	int ret;

	/*
	 * XXX: We need a threaded handler so that we may invoke core
	 * async-unsafe services from there (e.g. syncobj post
	 * routines are not async-safe, but the higher layers may
	 * invoke them from a timer handler).
	 *
	 * We don't rely on glibc's SIGEV_THREAD feature, because it
	 * is unreliable with some glibc releases (2.4 -> 2.9 at the
	 * very least), and spawning a short-lived thread at each
	 * timeout expiration to run the handler is just overkill.
	 */
	pthread_once(&spawn_once, timerobj_spawn_server);
	if (!svthread)
		return __bt(-EAGAIN);

	tmobj->handler = NULL;
	pvholder_init(&tmobj->next); /* so we may use pvholder_linked() */

	memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_THREAD_ID;
	sev.sigev_signo = SIGALRM;
	sev.sigev_notify_thread_id = svpid;

	ret = __RT(timer_create(CLOCK_COPPERPLATE, &sev, &tmobj->timer));
	if (ret)
		return __bt(-errno);

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	ret = pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	assert(ret == 0);
	ret = __bt(-__RT(pthread_mutex_init(&tmobj->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);

	return ret;
}

void timerobj_destroy(struct timerobj *tmobj) /* lock held, dropped */
{
	write_lock_nocancel(&svlock);

	if (pvholder_linked(&tmobj->next))
		pvlist_remove_init(&tmobj->next);

	write_unlock(&svlock);

	__RT(timer_delete(tmobj->timer));
	__RT(pthread_mutex_unlock(&tmobj->lock));
	__RT(pthread_mutex_destroy(&tmobj->lock));
}

int timerobj_start(struct timerobj *tmobj,
		   void (*handler)(struct timerobj *tmobj),
		   struct itimerspec *it) /* lock held, dropped */
{
	int ret = 0;

	/*
	 * We hold the queue lock long enough to prevent the timer
	 * from being dequeued by the carrier thread before it has
	 * been armed, e.g. the user handler might destroy it under
	 * our feet if so, causing timer_settime() to fail, which
	 * would in turn lead to a double-deletion if the caller
	 * happens to check the return code then drop the timer
	 * (again).
	 */
	write_lock_nocancel(&svlock);

       if (pvholder_linked(&tmobj->next))
               pvlist_remove_init(&tmobj->next);

	tmobj->handler = handler;
	tmobj->itspec = *it;

	if (__RT(timer_settime(tmobj->timer, TIMER_ABSTIME, it, NULL))) {
		ret = __bt(-errno);
		goto fail;
	}

	timerobj_enqueue(tmobj);
fail:
	write_unlock(&svlock);
	timerobj_unlock(tmobj);

	return ret;
}

int timerobj_stop(struct timerobj *tmobj) /* lock held, dropped */
{
	static const struct itimerspec itimer_stop;

	write_lock_nocancel(&svlock);

	if (pvholder_linked(&tmobj->next))
		pvlist_remove_init(&tmobj->next);

	__RT(timer_settime(tmobj->timer, 0, &itimer_stop, NULL));
	tmobj->handler = NULL;
	write_unlock(&svlock);
	timerobj_unlock(tmobj);

	return 0;
}

int timerobj_pkg_init(void)
{
	pthread_mutexattr_t mattr;
	int ret;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&svlock, &mattr)));
	pthread_mutexattr_destroy(&mattr);

	return ret;
}
