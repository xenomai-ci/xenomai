/*
 * Copyright (C) 2008-2011 Philippe Gerum <rpm@xenomai.org>.
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
 * Thread object abstraction.
 */
#include <signal.h>
#include <memory.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sched.h>
#include "boilerplate/signal.h"
#include "boilerplate/atomic.h"
#include "boilerplate/lock.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/syncobj.h"
#include "copperplate/cluster.h"
#include "copperplate/clockobj.h"
#include "copperplate/eventobj.h"
#include "copperplate/heapobj.h"
#include "internal.h"

union copperplate_wait_union {
	struct syncluster_wait_struct syncluster_wait;
	struct eventobj_wait_struct eventobj_wait;
};

union main_wait_union {
	union copperplate_wait_union copperplate_wait;
	char untyped_wait[1024];
};

static void finalize_thread(void *p);

static void set_global_priority(struct threadobj *thobj, int policy,
				const struct sched_param_ex *param_ex);

static int request_setschedparam(struct threadobj *thobj, int policy,
				 const struct sched_param_ex *param_ex);

static int request_cancel(struct threadobj *thobj);

static sigset_t sigperiod_set;

static int threadobj_agent_prio;

int threadobj_high_prio;

int threadobj_irq_prio;

#ifdef HAVE_TLS
__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct threadobj *__threadobj_current;
#endif

/*
 * We need the thread object key regardless of whether TLS is
 * available to us, to run the thread finalizer routine.
 */
pthread_key_t threadobj_tskey;

void threadobj_init_key(void)
{
	if (pthread_key_create(&threadobj_tskey, finalize_thread))
		early_panic("failed to allocate TSD key");
}

#ifdef CONFIG_XENO_PSHARED

static pid_t agent_pid;

#define RMT_SETSCHED	0
#define RMT_CANCEL	1

struct remote_cancel {
	pthread_t ptid;
	int policy;
	struct sched_param_ex param_ex;
};

struct remote_setsched {
	pthread_t ptid;
	int policy;
	struct sched_param_ex param_ex;
};

struct remote_request {
	int req;	/* RMT_xx */
	union {
		struct remote_cancel cancel;
		struct remote_setsched setsched;
	} u;
};

static int agent_prologue(void *arg)
{
	agent_pid = get_thread_pid();
	copperplate_set_current_name("remote-agent");
	threadobj_set_current(THREADOBJ_IRQCONTEXT);

	return 0;
}

static void *agent_loop(void *arg)
{
	struct remote_request *rq;
	siginfo_t si;
	sigset_t set;
	int sig, ret;

	sigemptyset(&set);
	sigaddset(&set, SIGAGENT);

	for (;;) {
		sig = __RT(sigwaitinfo(&set, &si));
		if (sig < 0) {
			if (errno == EINTR)
				continue;
			panic("agent thread cannot wait for request, %s",
			      symerror(-errno));
		}
		rq = si.si_ptr;
		switch (rq->req) {
		case RMT_SETSCHED:
			ret = copperplate_renice_local_thread(rq->u.setsched.ptid,
							      rq->u.setsched.policy,
							      &rq->u.setsched.param_ex);
			break;
		case RMT_CANCEL:
			if (rq->u.cancel.policy != -1)
				copperplate_renice_local_thread(rq->u.cancel.ptid,
								rq->u.cancel.policy,
								&rq->u.cancel.param_ex);
			ret = pthread_cancel(rq->u.cancel.ptid);
			break;
		default:
			panic("invalid remote request #%d", rq->req);
		}
		if (ret)
			warning("remote request #%d failed, %s",
				rq->req, symerror(ret));
		xnfree(rq);
	}

	return NULL;
}

static inline int send_agent(struct threadobj *thobj,
			     struct remote_request *rq)
{
	union sigval val = { .sival_ptr = rq };

	/*
	 * We are not supposed to issue remote requests when nobody
	 * else may share our session.
	 */
	assert(agent_pid != 0);

	/*
	 * XXX: No backtracing, may legitimately fail if the remote
	 * process goes away (hopefully cleanly). However, the request
	 * blocks attached to unprocessed pending signals may leak, as
	 * requests are fully asynchronous. Fortunately, processes
	 * creating user threads are unlikely to ungracefully leave
	 * the session they belong to intentionally.
	 */
	return __RT(sigqueue(agent_pid, SIGAGENT, val));
}

static void start_agent(void)
{
	struct corethread_attributes cta;
	pthread_t ptid;
	sigset_t set;
	int ret;

	/*
	 * CAUTION: we expect all internal/user threads created by
	 * Copperplate to inherit this signal mask, otherwise
	 * sigqueue(SIGAGENT) might be delivered to the wrong
	 * thread. So make sure the agent support is set up early
	 * enough.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGAGENT);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	cta.policy = threadobj_agent_prio ? SCHED_CORE : SCHED_OTHER;
	cta.param_ex.sched_priority = threadobj_agent_prio;
	cta.prologue = agent_prologue;
	cta.run = agent_loop;
	cta.arg = NULL;
	cta.stacksize = PTHREAD_STACK_DEFAULT;
	cta.detachstate = PTHREAD_CREATE_DETACHED;

	ret = copperplate_create_thread(&cta, &ptid);
	if (ret)
		panic("failed to start agent thread, %s", symerror(ret));
}

#else  /* !CONFIG_XENO_PSHARED */

static inline void start_agent(void)
{
	/* No agent in private (process-local) session. */
}

#endif /* !CONFIG_XENO_PSHARED */

#ifdef CONFIG_XENO_COBALT

#include "cobalt/internal.h"

static inline void pkg_init_corespec(void)
{
	/*
	 * We must have CAP_SYS_NICE since we reached this code either
	 * as root or as a member of the allowed group, as a result of
	 * binding the current process to the Cobalt core earlier in
	 * libcobalt's setup code.
	 */
	threadobj_irq_prio = sched_get_priority_max_ex(SCHED_CORE);
	threadobj_high_prio = sched_get_priority_max_ex(SCHED_FIFO);
	threadobj_agent_prio = threadobj_high_prio;
}

static inline int threadobj_init_corespec(struct threadobj *thobj)
{
	return 0;
}

static inline void threadobj_uninit_corespec(struct threadobj *thobj)
{
}

#ifdef CONFIG_XENO_PSHARED

static inline int threadobj_setup_corespec(struct threadobj *thobj)
{
	thobj->core.handle = cobalt_get_current();
	thobj->core.u_winoff = (void *)cobalt_get_current_window() -
		cobalt_umm_shared;

	return 0;
}

#else /* !CONFIG_XENO_PSHARED */

static inline int threadobj_setup_corespec(struct threadobj *thobj)
{
	thobj->core.handle = cobalt_get_current();
	thobj->core.u_window = cobalt_get_current_window();

	return 0;
}

#endif /* !CONFIG_XENO_PSHARED */

static inline void threadobj_cleanup_corespec(struct threadobj *thobj)
{
}

static inline void threadobj_run_corespec(struct threadobj *thobj)
{
	cobalt_thread_harden();
}

static inline void threadobj_cancel_1_corespec(struct threadobj *thobj) /* thobj->lock held */
{
}

static inline void threadobj_cancel_2_corespec(struct threadobj *thobj) /* thobj->lock held */
{
	/*
	 * Send a SIGDEMT signal to demote the target thread, to make
	 * sure pthread_cancel() will be effective asap.
	 *
	 * In effect, the thread is kicked out of any blocking
	 * syscall, a relax is forced on it (via a mayday trap if
	 * required), and it is then required to leave the real-time
	 * scheduling class.
	 *
	 * - this makes sure the thread returns with EINTR from the
	 * syscall then hits a cancellation point asap.
	 *
	 * - this ensures that the thread can receive the cancellation
	 * signal in case asynchronous cancellation is enabled and get
	 * kicked out from syscall-less code in primary mode
	 * (e.g. busy loops).
	 *
	 * - this makes sure the thread won't preempt the caller
	 * indefinitely when resuming due to priority enforcement
	 * (i.e. when the target thread has higher Xenomai priority
	 * than the caller of threadobj_cancel()), but will receive
	 * the following cancellation request asap.
	 */
	__RT(kill(thobj->pid, SIGDEMT));
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	pid_t pid = thobj->pid;
	int ret;

	__threadobj_check_locked(thobj);

	if (thobj->status & __THREAD_S_SUSPENDED)
		return 0;

	thobj->status |= __THREAD_S_SUSPENDED;
	if (thobj == threadobj_current()) {
		threadobj_unlock(thobj);
		ret = __RT(kill(pid, SIGSUSP));
		threadobj_lock(thobj);
	} else
		ret = __RT(kill(pid, SIGSUSP));

	return __bt(-ret);
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	int ret;

	__threadobj_check_locked(thobj);

	if ((thobj->status & __THREAD_S_SUSPENDED) == 0)
		return 0;

	thobj->status &= ~__THREAD_S_SUSPENDED;
	ret = __RT(kill(thobj->pid, SIGRESM));

	return __bt(-ret);
}

static inline int threadobj_unblocked_corespec(struct threadobj *current)
{
	return (threadobj_get_window(&current->core)->info & XNBREAK) != 0;
}

int __threadobj_lock_sched(struct threadobj *current)
{
	if (current->schedlock_depth++ > 0)
		return 0;

	/*
	 * In essence, we can't be scheduled out as a result of
	 * locking the scheduler, so no need to drop the thread lock
	 * across this call.
	 */
	return __bt(-pthread_setmode_np(0, PTHREAD_LOCK_SCHED, NULL));
}

int threadobj_lock_sched(void)
{
	struct threadobj *current = threadobj_current();

	/* This call is lock-free over Cobalt. */
	return __bt(__threadobj_lock_sched(current));
}

int __threadobj_unlock_sched(struct threadobj *current)
{
	/*
	 * Higher layers may not know about the current scheduler
	 * locking level and fully rely on us to track it, so we
	 * gracefully handle unbalanced calls here, and let them
	 * decide of the outcome in case of error.
	 */
	if (current->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--current->schedlock_depth > 0)
		return 0;

	return __bt(-pthread_setmode_np(PTHREAD_LOCK_SCHED, 0, NULL));
}

int threadobj_unlock_sched(void)
{
	struct threadobj *current = threadobj_current();

	/* This call is lock-free over Cobalt. */
	return __bt(__threadobj_unlock_sched(current));
}

int threadobj_set_mode(int clrmask, int setmask, int *mode_r) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	int __clrmask = 0, __setmask = 0;

	__threadobj_check_locked(current);

	if (setmask & __THREAD_M_WARNSW)
		__setmask |= PTHREAD_WARNSW;
	else if (clrmask & __THREAD_M_WARNSW)
		__clrmask |= PTHREAD_WARNSW;

	if (setmask & __THREAD_M_CONFORMING)
		__setmask |= PTHREAD_CONFORMING;
	else if (clrmask & __THREAD_M_CONFORMING)
		__clrmask |= PTHREAD_CONFORMING;

	if (setmask & __THREAD_M_LOCK)
		__threadobj_lock_sched_once(current);
	else if (clrmask & __THREAD_M_LOCK)
		__threadobj_unlock_sched(current);

	if (mode_r || __setmask || __clrmask)
		return __bt(-pthread_setmode_np(__clrmask, __setmask, mode_r));

	return 0;
}

static inline int map_priority_corespec(int policy,
					const struct sched_param_ex *param_ex)
{
	int prio;

	prio = cobalt_sched_weighted_prio(policy, param_ex);
	assert(prio >= 0);

	return prio;
}

static inline int prepare_rr_corespec(struct threadobj *thobj, int policy,
				      const struct sched_param_ex *param_ex) /* thobj->lock held */
{
	return policy;
}

static inline int enable_rr_corespec(struct threadobj *thobj,
				     const struct sched_param_ex *param_ex) /* thobj->lock held */
{
	return 0;
}

static inline void disable_rr_corespec(struct threadobj *thobj) /* thobj->lock held */
{
	/* nop */
}

int threadobj_stat(struct threadobj *thobj, struct threadobj_stat *p) /* thobj->lock held */
{
	struct cobalt_threadstat stat;
	int ret;

	__threadobj_check_locked(thobj);

	ret = cobalt_thread_stat(thobj->pid, &stat);
	if (ret)
		return __bt(ret);

	p->cpu = stat.cpu;
	p->status = stat.status;
	p->xtime = stat.xtime;
	p->msw = stat.msw;
	p->csw = stat.csw;
	p->xsc = stat.xsc;
	p->pf = stat.pf;
	p->timeout = stat.timeout;
	p->schedlock = thobj->schedlock_depth;

	return 0;
}

#else /* CONFIG_XENO_MERCURY */

static int threadobj_lock_prio;

static void unblock_sighandler(int sig)
{
	struct threadobj *current = threadobj_current();

	/*
	 * SIGRELS is thread-directed, so referring to
	 * current->run_state locklessly is safe as we are
	 * basically introspecting.
	 */
	if (current->run_state == __THREAD_S_DELAYED)
		current->run_state = __THREAD_S_BREAK;
}

static void roundrobin_handler(int sig)
{
	/*
	 * We do manual round-robin over SCHED_FIFO to allow for
	 * multiple arbitrary time slices (i.e. vs the kernel
	 * pre-defined and fixed one).
	 */
	sched_yield();
}

static void sleep_suspended(void)
{
	sigset_t set;

	/*
	 * A suspended thread is supposed to do nothing but wait for
	 * the wake up signal, so we may happily block all signals but
	 * SIGRESM. Note that SIGRRB won't be accumulated during the
	 * sleep time anyhow, as the round-robin timer is based on
	 * CLOCK_THREAD_CPUTIME_ID, and we'll obviously don't consume
	 * any CPU time while blocked.
	 */
	sigfillset(&set);
	sigdelset(&set, SIGRESM);
	sigsuspend(&set);
}

static void suspend_sighandler(int sig)
{
	sleep_suspended();
}

static void nop_sighandler(int sig)
{
	/* nop */
}

static inline void pkg_init_corespec(void)
{
	struct sigaction sa;

	/*
	 * We don't have builtin scheduler-lock feature over Mercury,
	 * so we emulate it by reserving the highest thread priority
	 * level from the SCHED_FIFO class to disable involuntary
	 * preemption.
	 *
	 * NOTE: The remote agent thread will also run with the
	 * highest thread priority level (threadobj_agent_prio) in
	 * shared multi-processing mode, which won't affect any thread
	 * holding the scheduler lock, unless the latter has to block
	 * for some reason, defeating the purpose of such lock anyway.
	 */
	threadobj_irq_prio = sched_get_priority_max(SCHED_FIFO);
	threadobj_lock_prio = threadobj_irq_prio - 1;
	threadobj_high_prio = threadobj_irq_prio - 2;
	threadobj_agent_prio = threadobj_high_prio;
	/*
	 * We allow a non-privileged process to start a low priority
	 * agent thread only, on the assumption that it lacks
	 * CAP_SYS_NICE, but this is pretty much the maximum extent of
	 * our abilities for such processes. Other internal threads
	 * requiring SCHED_CORE/FIFO scheduling such as the timer
	 * manager won't start properly, therefore the corresponding
	 * services won't be available.
	 */
	if (geteuid())
		threadobj_agent_prio = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = unblock_sighandler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGRELS, &sa, NULL);
	sa.sa_handler = roundrobin_handler;
	sigaction(SIGRRB, &sa, NULL);
	sa.sa_handler = suspend_sighandler;
	sigaction(SIGSUSP, &sa, NULL);
	sa.sa_handler = nop_sighandler;
	sigaction(SIGRESM, &sa, NULL);
	sigaction(SIGPERIOD, &sa, NULL);
}

static inline int threadobj_init_corespec(struct threadobj *thobj)
{
	pthread_condattr_t cattr;
	int ret;

	thobj->core.rr_timer = NULL;
	/*
	 * Over Mercury, we need an additional per-thread condvar to
	 * implement the complex monitor for the syncobj abstraction.
	 */
	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	ret = __bt(-pthread_condattr_setclock(&cattr, CLOCK_COPPERPLATE));
	if (ret)
		warning("failed setting condvar clock, %s"
			"(try --disable-clock-monotonic-raw)",
			symerror(ret));
	else
		ret = __bt(-pthread_cond_init(&thobj->core.grant_sync, &cattr));
	pthread_condattr_destroy(&cattr);

#ifdef CONFIG_XENO_WORKAROUND_CONDVAR_PI
	thobj->core.policy_unboosted = -1;
#endif
	return ret;
}

static inline void threadobj_uninit_corespec(struct threadobj *thobj)
{
	pthread_cond_destroy(&thobj->core.grant_sync);
}

static inline int threadobj_setup_corespec(struct threadobj *thobj)
{
	struct sigevent sev;
	sigset_t set;
	int ret;

	/*
	 * Do the per-thread setup for supporting the suspend/resume
	 * actions over Mercury. We have two basic requirements for
	 * this mechanism:
	 *
	 * - suspended requests must be handled asap, regardless of
	 * what the target thread is doing when notified (syscall
	 * wait, pure runtime etc.), hence the use of signals.
	 *
	 * - we must process the suspension signal on behalf of the
	 * target thread, as we want that thread to block upon
	 * receipt.
	 *
	 * In addition, we block the periodic signal, which we only
	 * want to receive from within threadobj_wait_period().
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGRESM);
	sigaddset(&set, SIGPERIOD);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	/*
	 * Create the per-thread round-robin timer.
	 */
	memset(&sev, 0, sizeof(sev));
	sev.sigev_signo = SIGRRB;
	sev.sigev_notify = SIGEV_SIGNAL|SIGEV_THREAD_ID;
	sev.sigev_notify_thread_id = threadobj_get_pid(thobj);
	ret = timer_create(CLOCK_THREAD_CPUTIME_ID, &sev,
			   &thobj->core.rr_timer);
	if (ret)
		return __bt(-errno);

	return 0;
}

static inline void threadobj_cleanup_corespec(struct threadobj *thobj)
{
	if (thobj->core.rr_timer)
		timer_delete(thobj->core.rr_timer);
}

static inline void threadobj_run_corespec(struct threadobj *thobj)
{
}

static inline void threadobj_cancel_1_corespec(struct threadobj *thobj) /* thobj->lock held */
{
	/*
	 * If the target thread we are about to cancel gets suspended
	 * while it is currently warming up, we have to unblock it
	 * from sleep_suspended(), so that we don't get stuck in
	 * cancel_sync(), waiting for a warmed up state which will
	 * never come.
	 *
	 * Just send it SIGRESM unconditionally, this will either
	 * unblock it if the thread waits in sleep_suspended(), or
	 * lead to a nop since that signal is blocked otherwise.
	 */
	copperplate_kill_tid(thobj->pid, SIGRESM);
}

static inline void threadobj_cancel_2_corespec(struct threadobj *thobj) /* thobj->lock held */
{
}

int threadobj_suspend(struct threadobj *thobj) /* thobj->lock held */
{
	__threadobj_check_locked(thobj);

	if (thobj == threadobj_current()) {
		thobj->status |= __THREAD_S_SUSPENDED;
		threadobj_unlock(thobj);
		sleep_suspended();
		threadobj_lock(thobj);
	} else if ((thobj->status & __THREAD_S_SUSPENDED) == 0) {
		/*
		 * We prevent suspension requests from cumulating, so
		 * that we always have a flat, consistent sequence of
		 * alternate suspend/resume events. It's up to the
		 * client code to handle nested requests if need be.
		 */
		thobj->status |= __THREAD_S_SUSPENDED;
		copperplate_kill_tid(thobj->pid, SIGSUSP);
	}

	return 0;
}

int threadobj_resume(struct threadobj *thobj) /* thobj->lock held */
{
	__threadobj_check_locked(thobj);

	if (thobj != threadobj_current() &&
	    (thobj->status & __THREAD_S_SUSPENDED) != 0) {
		thobj->status &= ~__THREAD_S_SUSPENDED;
		/*
		 * We prevent resumption requests from cumulating. See
		 * threadobj_suspend().
		 */
		copperplate_kill_tid(thobj->pid, SIGRESM);
	}

	return 0;
}

static inline int threadobj_unblocked_corespec(struct threadobj *current)
{
	return current->run_state != __THREAD_S_DELAYED;
}

int __threadobj_lock_sched(struct threadobj *current) /* current->lock held */
{
	struct sched_param_ex param_ex;
	int ret;

	__threadobj_check_locked(current);

	if (current->schedlock_depth > 0)
		goto done;

	current->core.schedparam_unlocked = current->schedparam;
	current->core.policy_unlocked = current->policy;
	param_ex.sched_priority = threadobj_lock_prio;
	ret = threadobj_set_schedparam(current, SCHED_FIFO, &param_ex);
	if (ret)
		return __bt(ret);
done:
	current->schedlock_depth++;

	return 0;
}

int threadobj_lock_sched(void)
{
	struct threadobj *current = threadobj_current();
	int ret;

	threadobj_lock(current);
	ret = __threadobj_lock_sched(current);
	threadobj_unlock(current);

	return __bt(ret);
}

int __threadobj_unlock_sched(struct threadobj *current) /* current->lock held */
{
	__threadobj_check_locked(current);

	if (current->schedlock_depth == 0)
		return __bt(-EINVAL);

	if (--current->schedlock_depth > 0)
		return 0;

	return __bt(threadobj_set_schedparam(current,
					     current->core.policy_unlocked,
					     &current->core.schedparam_unlocked));
}

int threadobj_unlock_sched(void)
{
	struct threadobj *current = threadobj_current();
	int ret;

	threadobj_lock(current);
	ret = __threadobj_unlock_sched(current);
	threadobj_unlock(current);

	return __bt(ret);
}

int threadobj_set_mode(int clrmask, int setmask, int *mode_r) /* current->lock held */
{
	struct threadobj *current = threadobj_current();
	int ret = 0, old = 0;

	__threadobj_check_locked(current);

	if (current->schedlock_depth > 0)
		old |= __THREAD_M_LOCK;

	if (setmask & __THREAD_M_LOCK) {
		ret = __threadobj_lock_sched_once(current);
		if (ret == -EBUSY)
			ret = 0;
	} else if (clrmask & __THREAD_M_LOCK)
		__threadobj_unlock_sched(current);

	if (mode_r)
		*mode_r = old;

	return __bt(ret);
}

static inline int map_priority_corespec(int policy,
					const struct sched_param_ex *param_ex)
{
	return param_ex->sched_priority;
}

static inline int prepare_rr_corespec(struct threadobj *thobj, int policy,
				      const struct sched_param_ex *param_ex) /* thobj->lock held */
{
	return SCHED_FIFO;
}

static int enable_rr_corespec(struct threadobj *thobj,
			      const struct sched_param_ex *param_ex) /* thobj->lock held */
{
	struct itimerspec value;
	int ret;

	value.it_interval = param_ex->sched_rr_quantum;
	value.it_value = value.it_interval;
	ret = timer_settime(thobj->core.rr_timer, 0, &value, NULL);
	if (ret)
		return __bt(-errno);

	return 0;
}

static void disable_rr_corespec(struct threadobj *thobj) /* thobj->lock held */
{
 	struct itimerspec value;

	value.it_value.tv_sec = 0;
	value.it_value.tv_nsec = 0;
	value.it_interval = value.it_value;
	timer_settime(thobj->core.rr_timer, 0, &value, NULL);
}

int threadobj_stat(struct threadobj *thobj,
		   struct threadobj_stat *stat) /* thobj->lock held */
{
	char procstat[64], buf[BUFSIZ], *p;
	struct timespec now, delta;
	FILE *fp;
	int n;

	__threadobj_check_locked(thobj);

	snprintf(procstat, sizeof(procstat), "/proc/%d/stat", thobj->pid);
	fp = fopen(procstat, "r");
	if (fp == NULL)
		return -EINVAL;

	p = fgets(buf, sizeof(buf), fp);
	fclose(fp);

	if (p == NULL)
		return -EIO;

	p += strlen(buf);
	for (n = 0; n < 14; n++) {
		while (*--p != ' ') {
			if (p <= buf)
				return -EINVAL;
		}
	}

	stat->cpu = atoi(++p);
	stat->status = threadobj_get_status(thobj);

	if (thobj->run_state & (__THREAD_S_TIMEDWAIT|__THREAD_S_DELAYED)) {
		__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
		timespec_sub(&delta, &thobj->core.timeout, &now);
		stat->timeout = timespec_scalar(&delta);
		/*
		 * The timeout might fire as we are calculating the
		 * delta: sanitize any negative value as 1.
		 */
		if ((sticks_t)stat->timeout < 0)
			stat->timeout = 1;
	} else
		stat->timeout = 0;

	stat->schedlock = thobj->schedlock_depth;

	return 0;
}

#ifdef CONFIG_XENO_WORKAROUND_CONDVAR_PI

/*
 * This workaround does NOT deal with concurrent updates of the caller
 * priority by other threads while the former is boosted. If your code
 * depends so much on strict PI to fix up CPU starvation, but you
 * insist on using a broken glibc that does not implement PI properly
 * nevertheless, then you have to refrain from issuing
 * pthread_setschedparam() for threads which might be currently
 * boosted.
 */
static void __threadobj_boost(void)
{
	struct threadobj *current = threadobj_current();
	struct sched_param param = {
		.sched_priority = threadobj_irq_prio, /* Highest one. */
	};
	int ret;

	if (current == NULL)	/* IRQ or invalid context */
		return;

	if (current->schedlock_depth > 0) {
		current->core.policy_unboosted = SCHED_FIFO;
		current->core.schedparam_unboosted.sched_priority = threadobj_lock_prio;
	} else {
		current->core.policy_unboosted = current->policy;
		current->core.schedparam_unboosted = current->schedparam;
	}
	compiler_barrier();

	ret = pthread_setschedparam(current->ptid, SCHED_FIFO, &param);
	if (ret) {
		current->core.policy_unboosted = -1;
		warning("thread boost failed, %s", symerror(-ret));
	}
}

static void __threadobj_unboost(void)

{
	struct threadobj *current = threadobj_current();
	struct sched_param param;
	int ret;

	if (current == NULL) 	/* IRQ or invalid context */
		return;

	param.sched_priority = current->core.schedparam_unboosted.sched_priority;

	ret = pthread_setschedparam(current->ptid,
				    current->core.policy_unboosted, &param);
	if (ret)
		warning("thread unboost failed, %s", symerror(-ret));

	current->core.policy_unboosted = -1;
}

int threadobj_cond_timedwait(pthread_cond_t *cond,
			     pthread_mutex_t *lock,
			     const struct timespec *timeout)
{
	int ret;

	__threadobj_boost();
	ret = pthread_cond_timedwait(cond, lock, timeout);
	__threadobj_unboost();

	return ret;
}

int threadobj_cond_wait(pthread_cond_t *cond,
			pthread_mutex_t *lock)
{
	int ret;

	__threadobj_boost();
	ret = pthread_cond_wait(cond, lock);
	__threadobj_unboost();

	return ret;
}

int threadobj_cond_signal(pthread_cond_t *cond)
{
	int ret;

	__threadobj_boost();
	ret = pthread_cond_signal(cond);
	__threadobj_unboost();

	return ret;
}

int threadobj_cond_broadcast(pthread_cond_t *cond)
{
	int ret;

	__threadobj_boost();
	ret = pthread_cond_broadcast(cond);
	__threadobj_unboost();

	return ret;
}

#endif /* !CONFIG_XENO_WORKAROUND_CONDVAR_PI */

#endif /* CONFIG_XENO_MERCURY */

static int request_setschedparam(struct threadobj *thobj, int policy,
				 const struct sched_param_ex *param_ex)
{				/* thobj->lock held */
	int ret;

#ifdef CONFIG_XENO_PSHARED
	struct remote_request *rq;

	if (!threadobj_local_p(thobj)) {
		rq = xnmalloc(sizeof(*rq));
		if (rq == NULL)
			return -ENOMEM;

		rq->req = RMT_SETSCHED;
		rq->u.setsched.ptid = thobj->ptid;
		rq->u.setsched.policy = policy;
		rq->u.setsched.param_ex = *param_ex;

		ret = __bt(send_agent(thobj, rq));
		if (ret)
			xnfree(rq);
		return ret;
	}
#endif
	/*
	 * We must drop the lock temporarily across the setsched
	 * operation, as libcobalt may switch us to secondary mode
	 * when doing so (i.e. libc call to reflect the new priority
	 * on the linux side).
	 *
	 * If we can't relock the target thread, this must mean that
	 * it vanished in the meantime: return -EIDRM for the caller
	 * to handle this case specifically.
	 */
	threadobj_unlock(thobj);
	ret = copperplate_renice_local_thread(thobj->ptid, policy, param_ex);
	if (threadobj_lock(thobj))
		ret = -EIDRM;

	return ret;
}

static int request_cancel(struct threadobj *thobj) /* thobj->lock held, dropped. */
{
	struct threadobj *current = threadobj_current();
	int thprio = thobj->global_priority;
	pthread_t ptid = thobj->ptid;
#ifdef CONFIG_XENO_PSHARED
	struct remote_request *rq;
	int ret;

	if (!threadobj_local_p(thobj)) {
		threadobj_unlock(thobj);
		rq = xnmalloc(sizeof(*rq));
		if (rq == NULL)
			return -ENOMEM;

		rq->req = RMT_CANCEL;
		rq->u.cancel.ptid = ptid;
		rq->u.cancel.policy = -1;
		if (current) {
			rq->u.cancel.policy = current->policy;
			rq->u.cancel.param_ex = current->schedparam;
		}
		ret = __bt(send_agent(thobj, rq));
		if (ret)
			xnfree(rq);
		return ret;
	}
#endif
	threadobj_unlock(thobj);

	/*
	 * The caller will have to wait for the killed thread to enter
	 * its finalizer, so we boost the latter thread to prevent a
	 * priority inversion if need be.
	 *
	 * NOTE: Since we dropped the lock, we might race if ptid
	 * disappears while we are busy killing it, glibc will check
	 * and dismiss if so.
	 */

	if (current && thprio < current->global_priority)
		copperplate_renice_local_thread(ptid, current->policy,
						&current->schedparam);
	pthread_cancel(ptid);

	return 0;
}

void *__threadobj_alloc(size_t tcb_struct_size,
			size_t wait_union_size,
			int thobj_offset)
{
	struct threadobj *thobj;
	void *p;

	if (wait_union_size < sizeof(union copperplate_wait_union))
		wait_union_size = sizeof(union copperplate_wait_union);

	tcb_struct_size = (tcb_struct_size+sizeof(double)-1) & ~(sizeof(double)-1);
	p = xnmalloc(tcb_struct_size + wait_union_size);
	if (p == NULL)
		return NULL;

	thobj = p + thobj_offset;
	thobj->core_offset = thobj_offset;
	thobj->wait_union = __moff(p + tcb_struct_size);
	thobj->wait_size = wait_union_size;

	return p;
}

static void set_global_priority(struct threadobj *thobj, int policy,
				const struct sched_param_ex *param_ex)
{
	thobj->schedparam = *param_ex;
	thobj->policy = policy;
	thobj->global_priority = map_priority_corespec(policy, param_ex);
}

int threadobj_init(struct threadobj *thobj,
		   struct threadobj_init_data *idata)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	int ret;

	thobj->magic = idata->magic;
	thobj->ptid = 0;
	thobj->tracer = NULL;
	thobj->wait_sobj = NULL;
	thobj->finalizer = idata->finalizer;
	thobj->schedlock_depth = 0;
	thobj->status = __THREAD_S_WARMUP;
	thobj->run_state = __THREAD_S_DORMANT;
	set_global_priority(thobj, idata->policy, &idata->param_ex);
	holder_init(&thobj->wait_link); /* mandatory */
	thobj->cnode = __node_id;
	thobj->pid = 0;
	thobj->cancel_sem = NULL;
	thobj->periodic_timer = NULL;

	/*
	 * CAUTION: wait_union and wait_size have been set in
	 * __threadobj_alloc(), do not overwrite.
	 */

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, mutex_scope_attribute);
	ret = __bt(-__RT(pthread_mutex_init(&thobj->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, mutex_scope_attribute);
	ret = __bt(-__RT(pthread_cond_init(&thobj->barrier, &cattr)));
	pthread_condattr_destroy(&cattr);
	if (ret) {
		__RT(pthread_mutex_destroy(&thobj->lock));
		return ret;
	}

	return threadobj_init_corespec(thobj);
}

static void uninit_thread(struct threadobj *thobj)
{
	threadobj_uninit_corespec(thobj);
	__RT(pthread_cond_destroy(&thobj->barrier));
	__RT(pthread_mutex_destroy(&thobj->lock));
}

static void destroy_thread(struct threadobj *thobj)
{
	threadobj_cleanup_corespec(thobj);
	if (thobj->status & __THREAD_S_PERIODIC)
		__RT(timer_delete(thobj->periodic_timer));
	uninit_thread(thobj);
}

void threadobj_uninit(struct threadobj *thobj) /* thobj->lock free */
{
	assert((thobj->status & (__THREAD_S_STARTED|__THREAD_S_ACTIVE)) == 0);
	uninit_thread(thobj);
}

/*
 * NOTE: to spare us the need for passing the equivalent of a
 * syncstate argument to each thread locking operation, we hold the
 * cancel state of the locker directly into the locked thread, prior
 * to disabling cancellation for the calling thread.
 *
 * However, this means that we must save some state information on the
 * stack prior to calling any service which releases that lock
 * implicitly, such as pthread_cond_wait(). Failing to do so would
 * introduce the possibility for the saved state to be overwritten by
 * another thread which managed to grab the lock after
 * pthread_cond_wait() dropped it.
 *
 * XXX: cancel_state is held in the descriptor of the target thread,
 * not the current one, because we allow non-copperplate threads to
 * call these services, and these have no threadobj descriptor.
 */

static int wait_on_barrier(struct threadobj *thobj, int mask)
{
	int oldstate, status;

	for (;;) {
		status = thobj->status;
		if (status & mask)
			break;
		oldstate = thobj->cancel_state;
		push_cleanup_lock(&thobj->lock);
		__threadobj_tag_unlocked(thobj);
		threadobj_cond_wait(&thobj->barrier, &thobj->lock);
		__threadobj_tag_locked(thobj);
		pop_cleanup_lock(&thobj->lock);
		thobj->cancel_state = oldstate;
	}

	return status;
}

int threadobj_start(struct threadobj *thobj)	/* thobj->lock held. */
{
	struct threadobj *current = threadobj_current();
	int ret = 0, oldstate;

	__threadobj_check_locked(thobj);

	if (thobj->status & __THREAD_S_STARTED)
		return 0;

	thobj->status |= __THREAD_S_STARTED;
	threadobj_cond_signal(&thobj->barrier);

	if (current && thobj->global_priority <= current->global_priority)
		return 0;

	/*
	 * Caller needs synchronization with the thread being started,
	 * which has higher priority. We shall wait until that thread
	 * enters the user code, or aborts prior to reaching that
	 * point, whichever comes first.
	 *
	 * We must not exit until the synchronization has fully taken
	 * place, disable cancellability until then.
	 */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);

	thobj->status |= __THREAD_S_SAFE;
	wait_on_barrier(thobj, __THREAD_S_ACTIVE);

	/*
	 * If the started thread has exited before we woke up from the
	 * barrier, its TCB was not reclaimed, to prevent us from
	 * treading on stale memory. Reclaim it now, and tell the
	 * caller to forget about it as well.
	 */
	if (thobj->run_state == __THREAD_S_DORMANT) {
		/* Keep cancel-safe after unlock. */
		thobj->cancel_state = PTHREAD_CANCEL_DISABLE;
		threadobj_unlock(thobj);
		destroy_thread(thobj);
		threadobj_free(thobj);
		ret = -EIDRM;
	} else
		thobj->status &= ~__THREAD_S_SAFE;

	pthread_setcancelstate(oldstate, NULL);

	return ret;
}

void threadobj_wait_start(void) /* current->lock free. */
{
	struct threadobj *current = threadobj_current();
	int status;

	threadobj_lock(current);
	status = wait_on_barrier(current, __THREAD_S_STARTED|__THREAD_S_ABORTED);
	threadobj_unlock(current);

	/*
	 * We may have preempted the guy who set __THREAD_S_ABORTED in
	 * our status before it had a chance to issue pthread_cancel()
	 * on us, so we need to go idle into a cancellation point to
	 * wait for it: use pause() for this.
	 */
	while (status & __THREAD_S_ABORTED)
		pause();
}

void threadobj_notify_entry(void) /* current->lock free. */
{
	struct threadobj *current = threadobj_current();

	threadobj_lock(current);
	current->status |= __THREAD_S_ACTIVE;
	current->run_state = __THREAD_S_RUNNING;
	threadobj_cond_signal(&current->barrier);
	threadobj_unlock(current);
}

/* thobj->lock free. */
int threadobj_prologue(struct threadobj *thobj, const char *name)
{
	struct threadobj *current = threadobj_current();
	int ret;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

	/*
	 * Check whether we overlay the default main TCB we set in
	 * main_overlay(), releasing it if so.
	 */
	if (current) {
		/*
		 * CAUTION: we may not overlay non-default TCB. The
		 * upper API should catch this issue before we get
		 * called.
		 */
		assert(current->magic == 0);
		sysgroup_remove(thread, &current->memspec);
		finalize_thread(current);
	}

	if (name) {
		namecpy(thobj->name, name);
		copperplate_set_current_name(name);
	} else {
		ret = copperplate_get_current_name(thobj->name,
						   sizeof(thobj->name));
		if (ret)
			warning("cannot get process name, %s", symerror(ret));
	}

	thobj->ptid = pthread_self();
	thobj->pid = get_thread_pid();
	thobj->errno_pointer = &errno;
	backtrace_init_context(&thobj->btd, name);
	ret = threadobj_setup_corespec(thobj);
	if (ret) {
		warning("prologue failed for thread %s, %s",
			name ?: "<anonymous>", symerror(ret));
		return __bt(ret);
	}

	threadobj_set_current(thobj);

	/*
	 * Link the thread to the shared queue, so that sysregd can
	 * retrieve it. Nop if --disable-pshared.
	 */
	sysgroup_add(thread, &thobj->memspec);

	threadobj_lock(thobj);
	thobj->status &= ~__THREAD_S_WARMUP;
	threadobj_cond_signal(&thobj->barrier);
	threadobj_unlock(thobj);

#ifdef CONFIG_XENO_ASYNC_CANCEL
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif
	threadobj_run_corespec(thobj);

	return 0;
}

int threadobj_shadow(struct threadobj *thobj, const char *name)
{
	assert(thobj != threadobj_current());
	threadobj_lock(thobj);
	assert((thobj->status & (__THREAD_S_STARTED|__THREAD_S_ACTIVE)) == 0);
	thobj->status |= __THREAD_S_STARTED|__THREAD_S_ACTIVE;
	threadobj_unlock(thobj);

	return __bt(threadobj_prologue(thobj, name));
}

/*
 * Most traditional RTOSes guarantee that the task/thread delete
 * operation is strictly synchronous, i.e. the deletion service
 * returns to the caller only __after__ the deleted thread entered an
 * innocuous state, i.e. dormant/dead.
 *
 * For this reason, we always wait until the canceled thread has
 * finalized (see cancel_sync()), at the expense of a potential
 * priority inversion affecting the caller of threadobj_cancel().
 */
static void cancel_sync(struct threadobj *thobj) /* thobj->lock held */
{
	int oldstate, ret = 0;
	sem_t *sem;

	threadobj_cancel_1_corespec(thobj);

	/*
	 * We have to allocate the cancel sync sema4 in the main heap
	 * dynamically, so that it always lives in valid memory when
	 * we wait on it. This has to be true regardless of whether
	 * --enable-pshared is in effect, or thobj becomes stale after
	 * the finalizer has run (we cannot host this sema4 in thobj
	 * for this reason).
	 */
	sem = xnmalloc(sizeof(*sem));
	if (sem == NULL)
		ret = -ENOMEM;
	else
		__STD(sem_init(sem, sem_scope_attribute, 0));

	thobj->cancel_sem = sem;

	/*
	 * If the thread to delete is warming up, wait until it
	 * reaches the start barrier before sending the cancellation
	 * signal.
	 */
	while (thobj->status & __THREAD_S_WARMUP) {
		oldstate = thobj->cancel_state;
		push_cleanup_lock(&thobj->lock);
		__threadobj_tag_unlocked(thobj);
		threadobj_cond_wait(&thobj->barrier, &thobj->lock);
		__threadobj_tag_locked(thobj);
		pop_cleanup_lock(&thobj->lock);
		thobj->cancel_state = oldstate;
	}

	/*
	 * Ok, now we shall raise the abort flag if the thread was not
	 * started yet, to kick it out of the barrier wait. We are
	 * covered by the target thread lock we hold, so we can't race
	 * with threadobj_start().
	 */
	if ((thobj->status & __THREAD_S_STARTED) == 0) {
		thobj->status |= __THREAD_S_ABORTED;
		threadobj_cond_signal(&thobj->barrier);
	}

	threadobj_cancel_2_corespec(thobj);

	request_cancel(thobj);

	if (sem) {
		do
			ret = __STD(sem_wait(sem));
		while (ret == -1 && errno == EINTR);
	}

	/*
	 * Not being able to sync up with the cancelled thread is not
	 * considered fatal, despite it's likely bad news for sure, so
	 * that we can keep on cleaning up the mess, hoping for the
	 * best.
	 */
	if (sem == NULL || ret)
		warning("cannot sync with thread finalizer, %s",
			symerror(sem ? -errno : ret));
	if (sem) {
		__STD(sem_destroy(sem));
		xnfree(sem);
	}
}

/* thobj->lock held on entry, released on return */
int threadobj_cancel(struct threadobj *thobj)
{
	__threadobj_check_locked(thobj);

	/*
	 * This basically makes the thread enter a zombie state, since
	 * it won't be reachable by anyone after its magic has been
	 * trashed.
	 */
	thobj->magic = ~thobj->magic;

	if (thobj == threadobj_current()) {
		threadobj_unlock(thobj);
		pthread_exit(NULL);
	}

	cancel_sync(thobj);

	return 0;
}

static void finalize_thread(void *p) /* thobj->lock free */
{
	struct threadobj *thobj = p;

	if (thobj == NULL || thobj == THREADOBJ_IRQCONTEXT)
		return;

	thobj->magic = ~thobj->magic;
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	threadobj_set_current(p);

	if (thobj->wait_sobj)
		__syncobj_cleanup_wait(thobj->wait_sobj, thobj);

	sysgroup_remove(thread, &thobj->memspec);

	if (thobj->tracer)
		traceobj_unwind(thobj->tracer);

	backtrace_dump(&thobj->btd);
	backtrace_destroy_context(&thobj->btd);

	if (thobj->finalizer)
		thobj->finalizer(thobj);

	if (thobj->cancel_sem)
		/* Release the killer from threadobj_cancel(). */
		__STD(sem_post)(thobj->cancel_sem);

	thobj->run_state = __THREAD_S_DORMANT;

	/*
	 * Do not reclaim the TCB core resources if another thread is
	 * waiting for us to start, pending on
	 * wait_on_barrier(). Instead, hand it over to this thread.
	 */
	threadobj_lock(thobj);
	if ((thobj->status & __THREAD_S_SAFE) == 0) {
		threadobj_unlock(thobj);
		destroy_thread(thobj);
		threadobj_free(thobj);
	} else
		threadobj_unlock(thobj);

	threadobj_set_current(NULL);
}

int threadobj_unblock(struct threadobj *thobj) /* thobj->lock held */
{
	struct syncstate syns;
	struct syncobj *sobj;
	int ret;

	__threadobj_check_locked(thobj);

	sobj = thobj->wait_sobj;
	if (sobj) {
		ret = syncobj_lock(sobj, &syns);
		/*
		 * Remove PEND (+DELAY timeout).
		 * CAUTION: thobj->wait_obj goes NULL upon flush.
		 */
		if (ret == 0) {
			syncobj_flush(sobj);
			syncobj_unlock(sobj, &syns);
			return 0;
		}
	}

	/* Remove standalone DELAY condition. */

	if (!threadobj_local_p(thobj))
		return __bt(-copperplate_kill_tid(thobj->pid, SIGRELS));

	return __bt(-__RT(pthread_kill(thobj->ptid, SIGRELS)));
}

int threadobj_sleep(const struct timespec *ts)
{
	struct threadobj *current = threadobj_current();
	sigset_t set;
	int ret;

	/*
	 * threadobj_sleep() shall return -EINTR immediately upon
	 * threadobj_unblock(), to honor forced wakeup semantics for
	 * RTOS personalities.
	 *
	 * Otherwise, the sleep should be silently restarted until
	 * completion after a Linux signal is handled.
	 */
	current->run_state = __THREAD_S_DELAYED;
	threadobj_save_timeout(&current->core, ts);

	do {
		/*
		 * Waiting on a null signal set causes an infinite
		 * delay, so that only threadobj_unblock() or a linux
		 * signal can unblock us.
		 */
		if (ts->tv_sec == 0 && ts->tv_nsec == 0) {
			sigemptyset(&set);
			ret = __RT(sigwaitinfo(&set, NULL)) ? errno : 0;
		} else
			ret = __RT(clock_nanosleep(CLOCK_COPPERPLATE,
						   TIMER_ABSTIME, ts, NULL));
	} while (ret == EINTR && !threadobj_unblocked_corespec(current));

	current->run_state = __THREAD_S_RUNNING;

	return -ret;
}

int threadobj_set_periodic(struct threadobj *thobj,
			   const struct timespec *__restrict__ idate,
			   const struct timespec *__restrict__ period)
{				/* thobj->lock held */
	struct itimerspec its;
	struct sigevent sev;
	timer_t timer;
	int ret;

	__threadobj_check_locked(thobj);

	timer = thobj->periodic_timer;
	if (!timespec_scalar(idate) && !timespec_scalar(period)) {
		if (thobj->status & __THREAD_S_PERIODIC) {
			thobj->status &= ~__THREAD_S_PERIODIC;
			__RT(timer_delete(timer));
		}
		return 0;
	}
	
	if (!(thobj->status & __THREAD_S_PERIODIC)) {
		memset(&sev, 0, sizeof(sev));
		sev.sigev_signo = SIGPERIOD;
		sev.sigev_notify = SIGEV_SIGNAL|SIGEV_THREAD_ID;
		sev.sigev_notify_thread_id = threadobj_get_pid(thobj);
		ret = __RT(timer_create(CLOCK_COPPERPLATE, &sev, &timer));
		if (ret)
			return __bt(-errno);
		thobj->periodic_timer = timer;
		thobj->status |= __THREAD_S_PERIODIC;
	}

	its.it_value = *idate;
	its.it_interval = *period;

	ret = __RT(timer_settime(timer, TIMER_ABSTIME, &its, NULL));
	if (ret)
		return __bt(-errno);

	return 0;
}

int threadobj_wait_period(unsigned long *overruns_r)
{
	struct threadobj *current = threadobj_current();
	siginfo_t si;
	int sig;

	if (!(current->status & __THREAD_S_PERIODIC))
		return -EWOULDBLOCK;

	for (;;) {
		current->run_state = __THREAD_S_DELAYED;
		sig = __RT(sigwaitinfo(&sigperiod_set, &si));
		current->run_state = __THREAD_S_RUNNING;
		if (sig == SIGPERIOD)
			break;
		if (errno == EINTR)
			return -EINTR;
		panic("cannot wait for next period, %s", symerror(-errno));
	}

	if (si.si_overrun) {
		if (overruns_r)
			*overruns_r = si.si_overrun;
		return -ETIMEDOUT;
	}

	return 0;
}

void threadobj_spin(ticks_t ns)
{
	ticks_t end;

	end = clockobj_get_tsc() + clockobj_ns_to_tsc(ns);
	while (clockobj_get_tsc() < end)
		cpu_relax();
}

int threadobj_set_schedparam(struct threadobj *thobj, int policy,
			     const struct sched_param_ex *param_ex) /* thobj->lock held */
{
	int ret, _policy;

	__threadobj_check_locked(thobj);

	if (thobj->schedlock_depth > 0)
		return __bt(-EPERM);

	_policy = policy;
	if (policy == SCHED_RR)
		_policy = prepare_rr_corespec(thobj, policy, param_ex);
	/*
	 * NOTE: if the current thread suddently starves as a result
	 * of switching itself to a scheduling class with no runtime
	 * budget, it will hold its own lock for an indefinite amount
	 * of time, i.e. until it gets some budget again. That seems a
	 * more acceptable/less likely risk than introducing a race
	 * window between the moment set_schedparam() is actually
	 * applied at OS level, and the update of the priority
	 * information in set_global_priority(), as both must be seen
	 * as a single logical operation.
	 */
	ret = request_setschedparam(thobj, _policy, param_ex);
	if (ret)
		return ret;

	/*
	 * XXX: only local threads may switch to SCHED_RR since both
	 * Cobalt and Mercury need this for different reasons.
	 *
	 * This seems an acceptable limitation compared to introducing
	 * a significantly more complex implementation only for
	 * supporting a somewhat weird feature (i.e. controlling the
	 * round-robin state of threads running in remote processes).
	 */
	if (policy == SCHED_RR) {
		if (!threadobj_local_p(thobj))
			return -EINVAL;
		ret = enable_rr_corespec(thobj, param_ex);
		if (ret)
			return __bt(ret);
		thobj->tslice = param_ex->sched_rr_quantum;
	} else if (thobj->policy == SCHED_RR) /* Switching off round-robin. */
		disable_rr_corespec(thobj);

	set_global_priority(thobj, policy, param_ex);

	return 0;
}

int threadobj_set_schedprio(struct threadobj *thobj, int priority)
{				/* thobj->lock held */
	struct sched_param_ex param_ex;
	int policy;

	__threadobj_check_locked(thobj);

	param_ex = thobj->schedparam;
	param_ex.sched_priority = priority;
	policy = thobj->policy;

	if (policy == SCHED_RR)
		param_ex.sched_rr_quantum = thobj->tslice;

	return threadobj_set_schedparam(thobj, policy, &param_ex);
}

#ifdef CONFIG_XENO_PSHARED
static void main_exit(void)
{
	threadobj_free(threadobj_current());
}
#endif

static inline int main_overlay(void)
{
	struct threadobj_init_data idata;
	struct threadobj *tcb;
	int ret;

	/*
	 * Make the main() context a basic yet complete thread object,
	 * so that it may use any service which requires the caller to
	 * have a Copperplate TCB (e.g. all blocking services). We
	 * allocate a wait union which should be sufficient for
	 * calling any blocking service from any high-level API from
	 * an unshadowed main thread. APIs might have reasons not to
	 * allow such call though, in which case they should check
	 * explicitly for those conditions.
	 */
	tcb = __threadobj_alloc(sizeof(*tcb),
				sizeof(union main_wait_union),
				0);
	if (tcb == NULL)
		panic("failed to allocate main tcb");

	idata.magic = 0x0;
	idata.finalizer = NULL;
	idata.policy = SCHED_OTHER;
	idata.param_ex.sched_priority = 0;
	ret = threadobj_init(tcb, &idata);
	if (ret) {
		__threadobj_free(tcb);
		return __bt(ret);
	}

	tcb->status = __THREAD_S_STARTED|__THREAD_S_ACTIVE;
	threadobj_prologue(tcb, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

#ifdef CONFIG_XENO_PSHARED
	atexit(main_exit);
#endif

	return 0;
}

int threadobj_pkg_init(int anon_session)
{
	sigaddset(&sigperiod_set, SIGPERIOD);
	pkg_init_corespec();

	if (!anon_session)
		start_agent();

	return main_overlay();
}
