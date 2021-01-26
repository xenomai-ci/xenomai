/*
 * Copyright (C) 2001,2002,2003,2007,2012 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <linux/sched.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/thread.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/trace.h>
#include <cobalt/kernel/arith.h>
#include <trace/events/cobalt-core.h>

/**
 * @ingroup cobalt_core
 * @defgroup cobalt_core_timer Timer services
 *
 * The Xenomai timer facility depends on a clock source (xnclock) for
 * scheduling the next activation times.
 *
 * The core provides and depends on a monotonic clock source (nkclock)
 * with nanosecond resolution, driving the platform timer hardware
 * exposed by the interrupt pipeline.
 *
 * @{
 */

int xntimer_heading_p(struct xntimer *timer)
{
	struct xnsched *sched = timer->sched;
	xntimerq_t *q;
	xntimerh_t *h;

	q = xntimer_percpu_queue(timer);
	h = xntimerq_head(q);
	if (h == &timer->aplink)
		return 1;

	if (sched->lflags & XNHDEFER) {
		h = xntimerq_second(q, h);
		if (h == &timer->aplink)
			return 1;
	}

	return 0;
}

void xntimer_enqueue_and_program(struct xntimer *timer, xntimerq_t *q)
{
	xntimer_enqueue(timer, q);
	if (xntimer_heading_p(timer)) {
		struct xnsched *sched = xntimer_sched(timer);
		struct xnclock *clock = xntimer_clock(timer);
		if (sched != xnsched_current())
			xnclock_remote_shot(clock, sched);
		else
			xnclock_program_shot(clock, sched);
	}
}

/**
 * Arm a timer.
 *
 * Activates a timer so that the associated timeout handler will be
 * fired after each expiration time. A timer can be either periodic or
 * one-shot, depending on the reload value passed to this routine. The
 * given timer must have been previously initialized.
 *
 * A timer is attached to the clock specified in xntimer_init().
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param value The date of the initial timer shot, expressed in
 * nanoseconds.
 *
 * @param interval The reload value of the timer. It is a periodic
 * interval value to be used for reprogramming the next timer shot,
 * expressed in nanoseconds. If @a interval is equal to XN_INFINITE,
 * the timer will not be reloaded after it has expired.
 *
 * @param mode The timer mode. It can be XN_RELATIVE if @a value shall
 * be interpreted as a relative date, XN_ABSOLUTE for an absolute date
 * based on the monotonic clock of the related time base (as returned
 * my xnclock_read_monotonic()), or XN_REALTIME if the absolute date
 * is based on the adjustable real-time date for the relevant clock
 * (obtained from xnclock_read_realtime()).
 *
 * @return 0 is returned upon success, or -ETIMEDOUT if an absolute
 * date in the past has been given. In such an event, the timer is
 * nevertheless armed for the next shot in the timeline if @a interval
 * is different from XN_INFINITE.
 *
 * @coretags{unrestricted, atomic-entry}
 */
int xntimer_start(struct xntimer *timer,
		  xnticks_t value, xnticks_t interval,
		  xntmode_t mode)
{
	struct xnclock *clock = xntimer_clock(timer);
	xntimerq_t *q = xntimer_percpu_queue(timer);
	xnticks_t date, now, delay, period;
	unsigned long gravity;
	int ret = 0;

	atomic_only();

	trace_cobalt_timer_start(timer, value, interval, mode);

	if ((timer->status & XNTIMER_DEQUEUED) == 0)
		xntimer_dequeue(timer, q);

	now = xnclock_read_raw(clock);

	timer->status &= ~(XNTIMER_REALTIME | XNTIMER_FIRED | XNTIMER_PERIODIC);
	switch (mode) {
	case XN_RELATIVE:
		if ((xnsticks_t)value < 0)
			return -ETIMEDOUT;
		date = xnclock_ns_to_ticks(clock, value) + now;
		break;
	case XN_REALTIME:
		timer->status |= XNTIMER_REALTIME;
		value -= xnclock_get_offset(clock);
		/* fall through */
	default: /* XN_ABSOLUTE || XN_REALTIME */
		date = xnclock_ns_to_ticks(clock, value);
		if ((xnsticks_t)(date - now) <= 0) {
			if (interval == XN_INFINITE)
				return -ETIMEDOUT;
			/*
			 * We are late on arrival for the first
			 * delivery, wait for the next shot on the
			 * periodic time line.
			 */
			delay = now - date;
			period = xnclock_ns_to_ticks(clock, interval);
			date += period * (xnarch_div64(delay, period) + 1);
		}
		break;
	}

	/*
	 * To cope with the basic system latency, we apply a clock
	 * gravity value, which is the amount of time expressed in
	 * clock ticks by which we should anticipate the shot for any
	 * outstanding timer. The gravity value varies with the type
	 * of context the timer wakes up, i.e. irq handler, kernel or
	 * user thread.
	 */
	gravity = xntimer_gravity(timer);
	xntimerh_date(&timer->aplink) = date - gravity;
	if (now >= xntimerh_date(&timer->aplink))
		xntimerh_date(&timer->aplink) += gravity / 2;

	timer->interval_ns = XN_INFINITE;
	timer->interval = XN_INFINITE;
	if (interval != XN_INFINITE) {
		timer->interval_ns = interval;
		timer->interval = xnclock_ns_to_ticks(clock, interval);
		timer->periodic_ticks = 0;
		timer->start_date = date;
		timer->pexpect_ticks = 0;
		timer->status |= XNTIMER_PERIODIC;
	}

	timer->status |= XNTIMER_RUNNING;
	xntimer_enqueue_and_program(timer, q);

	return ret;
}
EXPORT_SYMBOL_GPL(xntimer_start);

/**
 * @fn int xntimer_stop(struct xntimer *timer)
 *
 * @brief Disarm a timer.
 *
 * This service deactivates a timer previously armed using
 * xntimer_start(). Once disarmed, the timer can be subsequently
 * re-armed using the latter service.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @coretags{unrestricted, atomic-entry}
 */
void __xntimer_stop(struct xntimer *timer)
{
	struct xnclock *clock = xntimer_clock(timer);
	xntimerq_t *q = xntimer_percpu_queue(timer);
	struct xnsched *sched;
	int heading = 1;

	atomic_only();

	trace_cobalt_timer_stop(timer);

	if ((timer->status & XNTIMER_DEQUEUED) == 0) {
		heading = xntimer_heading_p(timer);
		xntimer_dequeue(timer, q);
	}
	timer->status &= ~(XNTIMER_FIRED|XNTIMER_RUNNING);
	sched = xntimer_sched(timer);

	/*
	 * If we removed the heading timer, reprogram the next shot if
	 * any. If the timer was running on another CPU, let it tick.
	 */
	if (heading && sched == xnsched_current())
		xnclock_program_shot(clock, sched);
}
EXPORT_SYMBOL_GPL(__xntimer_stop);

/**
 * @fn xnticks_t xntimer_get_date(struct xntimer *timer)
 *
 * @brief Return the absolute expiration date.
 *
 * Return the next expiration date of a timer as an absolute count of
 * nanoseconds.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The expiration date in nanoseconds. The special value
 * XN_INFINITE is returned if @a timer is currently disabled.
 *
 * @coretags{unrestricted, atomic-entry}
 */
xnticks_t xntimer_get_date(struct xntimer *timer)
{
	atomic_only();

	if (!xntimer_running_p(timer))
		return XN_INFINITE;

	return xnclock_ticks_to_ns(xntimer_clock(timer), xntimer_expiry(timer));
}
EXPORT_SYMBOL_GPL(xntimer_get_date);

/**
 * @fn xnticks_t xntimer_get_timeout(struct xntimer *timer)
 *
 * @brief Return the relative expiration date.
 *
 * This call returns the count of nanoseconds remaining until the
 * timer expires.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The count of nanoseconds until expiry. The special value
 * XN_INFINITE is returned if @a timer is currently disabled.  It
 * might happen that the timer expires when this service runs (even if
 * the associated handler has not been fired yet); in such a case, 1
 * is returned.
 *
 * @coretags{unrestricted, atomic-entry}
 */
xnticks_t __xntimer_get_timeout(struct xntimer *timer)
{
	struct xnclock *clock;
	xnticks_t expiry, now;

	atomic_only();

	clock = xntimer_clock(timer);
	now = xnclock_read_raw(clock);
	expiry = xntimer_expiry(timer);
	if (expiry < now)
		return 1;  /* Will elapse shortly. */

	return xnclock_ticks_to_ns(clock, expiry - now);
}
EXPORT_SYMBOL_GPL(__xntimer_get_timeout);

/**
 * @fn void xntimer_init(struct xntimer *timer,struct xnclock *clock,void (*handler)(struct xntimer *timer), struct xnsched *sched, int flags)
 * @brief Initialize a timer object.
 *
 * Creates a timer. When created, a timer is left disarmed; it must be
 * started using xntimer_start() in order to be activated.
 *
 * @param timer The address of a timer descriptor the nucleus will use
 * to store the object-specific data.  This descriptor must always be
 * valid while the object is active therefore it must be allocated in
 * permanent memory.
 *
 * @param clock The clock the timer relates to. Xenomai defines a
 * monotonic system clock, with nanosecond resolution, named
 * nkclock. In addition, external clocks driven by other tick sources
 * may be created dynamically if CONFIG_XENO_OPT_EXTCLOCK is defined.
 *
 * @param handler The routine to call upon expiration of the timer.
 *
 * @param sched An optional pointer to the per-CPU scheduler slot the
 * new timer is affine to. If non-NULL, the timer will fire on the CPU
 * @a sched is bound to, otherwise it will fire either on the current
 * CPU if real-time, or on the first real-time CPU.
 *
 * @param flags A set of flags describing the timer. A set of clock
 * gravity hints can be passed via the @a flags argument, used for
 * optimizing the built-in heuristics aimed at latency reduction:
 *
 * - XNTIMER_IGRAVITY, the timer activates a leaf timer handler.
 * - XNTIMER_KGRAVITY, the timer activates a kernel thread.
 * - XNTIMER_UGRAVITY, the timer activates a user-space thread.
 *
 * There is no limitation on the number of timers which can be
 * created/active concurrently.
 *
 * @coretags{unrestricted}
 */
#ifdef DOXYGEN_CPP
void xntimer_init(struct xntimer *timer, struct xnclock *clock,
		  void (*handler)(struct xntimer *timer),
		  struct xnsched *sched,
		  int flags);
#endif

void __xntimer_init(struct xntimer *timer,
		    struct xnclock *clock,
		    void (*handler)(struct xntimer *timer),
		    struct xnsched *sched,
		    int flags)
{
	spl_t s __maybe_unused;

#ifdef CONFIG_XENO_OPT_EXTCLOCK
	timer->clock = clock;
#endif
	xntimerh_init(&timer->aplink);
	xntimerh_date(&timer->aplink) = XN_INFINITE;
	xntimer_set_priority(timer, XNTIMER_STDPRIO);
	timer->status = (XNTIMER_DEQUEUED|(flags & XNTIMER_INIT_MASK));
	timer->handler = handler;
	timer->interval_ns = 0;
	timer->sched = NULL;

	/*
	 * Set the timer affinity, preferably to xnsched_cpu(sched) if
	 * sched was given, CPU0 otherwise.
	 */
	if (sched == NULL)
		sched = xnsched_struct(0);

	xntimer_set_affinity(timer, sched);

#ifdef CONFIG_XENO_OPT_STATS
#ifdef CONFIG_XENO_OPT_EXTCLOCK
	timer->tracker = clock;
#endif
	ksformat(timer->name, XNOBJECT_NAME_LEN, "%d/%s",
		 task_pid_nr(current), current->comm);
	xntimer_reset_stats(timer);
	xnlock_get_irqsave(&nklock, s);
	list_add_tail(&timer->next_stat, &clock->timerq);
	clock->nrtimers++;
	xnvfile_touch(&clock->timer_vfile);
	xnlock_put_irqrestore(&nklock, s);
#endif /* CONFIG_XENO_OPT_STATS */
}
EXPORT_SYMBOL_GPL(__xntimer_init);

void xntimer_set_gravity(struct xntimer *timer, int gravity)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	timer->status &= ~XNTIMER_GRAVITY_MASK;
	timer->status |= gravity;
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_set_gravity);

#ifdef CONFIG_XENO_OPT_EXTCLOCK

#ifdef CONFIG_XENO_OPT_STATS

static void __xntimer_switch_tracking(struct xntimer *timer,
				      struct xnclock *newclock)
{
	struct xnclock *oldclock = timer->tracker;

	list_del(&timer->next_stat);
	oldclock->nrtimers--;
	xnvfile_touch(&oldclock->timer_vfile);
	list_add_tail(&timer->next_stat, &newclock->timerq);
	newclock->nrtimers++;
	xnvfile_touch(&newclock->timer_vfile);
	timer->tracker = newclock;
}

void xntimer_switch_tracking(struct xntimer *timer,
			     struct xnclock *newclock)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	__xntimer_switch_tracking(timer, newclock);
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_switch_tracking);

#else

static inline
void __xntimer_switch_tracking(struct xntimer *timer,
			       struct xnclock *newclock)
{ }

#endif /* CONFIG_XENO_OPT_STATS */

/**
 * @brief Set the reference clock of a timer.
 *
 * This service changes the reference clock pacing a timer. If the
 * clock timers are tracked, the tracking information is updated too.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param newclock The address of a valid clock descriptor.
 *
 * @coretags{unrestricted, atomic-entry}
 */
void xntimer_set_clock(struct xntimer *timer,
		       struct xnclock *newclock)
{
	atomic_only();

	if (timer->clock != newclock) {
		xntimer_stop(timer);
		timer->clock = newclock;
		/*
		 * Since the timer was stopped, we can wait until it
		 * is restarted for fixing its CPU affinity.
		 */
		__xntimer_switch_tracking(timer, newclock);
	}
}

#endif /* CONFIG_XENO_OPT_EXTCLOCK */

/**
 * @fn void xntimer_destroy(struct xntimer *timer)
 *
 * @brief Release a timer object.
 *
 * Destroys a timer. After it has been destroyed, all resources
 * associated with the timer have been released. The timer is
 * automatically deactivated before deletion if active on entry.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @coretags{unrestricted}
 */
void xntimer_destroy(struct xntimer *timer)
{
	struct xnclock *clock __maybe_unused = xntimer_clock(timer);
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	xntimer_stop(timer);
	timer->status |= XNTIMER_KILLED;
	timer->sched = NULL;
#ifdef CONFIG_XENO_OPT_STATS
	list_del(&timer->next_stat);
	clock->nrtimers--;
	xnvfile_touch(&clock->timer_vfile);
#endif /* CONFIG_XENO_OPT_STATS */
	xnlock_put_irqrestore(&nklock, s);
}
EXPORT_SYMBOL_GPL(xntimer_destroy);

#ifdef CONFIG_SMP

/**
 * Migrate a timer.
 *
 * This call migrates a timer to another cpu. In order to avoid
 * pathological cases, it must be called from the CPU to which @a
 * timer is currently attached.
 *
 * @param timer The address of the timer object to be migrated.
 *
 * @param sched The address of the destination per-CPU scheduler
 * slot.
 *
 * @coretags{unrestricted, atomic-entry}
 */
void __xntimer_migrate(struct xntimer *timer, struct xnsched *sched)
{				/* nklocked, IRQs off, sched != timer->sched */
	struct xnclock *clock;
	xntimerq_t *q;

	trace_cobalt_timer_migrate(timer, xnsched_cpu(sched));

	/*
	 * This assertion triggers when the timer is migrated to a CPU
	 * for which we do not expect any clock events/IRQs from the
	 * associated clock device. If so, the timer would never fire
	 * since clock ticks would never happen on that CPU.
	 */
	XENO_WARN_ON_SMP(COBALT,
			 !cpumask_empty(&xntimer_clock(timer)->affinity) &&
			 !cpumask_test_cpu(xnsched_cpu(sched),
					   &xntimer_clock(timer)->affinity));

	if (timer->status & XNTIMER_RUNNING) {
		xntimer_stop(timer);
		timer->sched = sched;
		clock = xntimer_clock(timer);
		q = xntimer_percpu_queue(timer);
		xntimer_enqueue(timer, q);
		if (xntimer_heading_p(timer))
			xnclock_remote_shot(clock, sched);
	} else
		timer->sched = sched;
}
EXPORT_SYMBOL_GPL(__xntimer_migrate);

static inline int get_clock_cpu(struct xnclock *clock, int cpu)
{
	/*
	 * Check a CPU number against the possible set of CPUs
	 * receiving events from the underlying clock device. If the
	 * suggested CPU does not receive events from this device,
	 * return the first one which does instead.
	 *
	 * A global clock device with no particular IRQ affinity may
	 * tick on any CPU, but timers should always be queued on
	 * CPU0.
	 *
	 * NOTE: we have scheduler slots initialized for all online
	 * CPUs, we can program and receive clock ticks on any of
	 * them. So there is no point in restricting the valid CPU set
	 * to cobalt_cpu_affinity, which specifically refers to the
	 * set of CPUs which may run real-time threads. Although
	 * receiving a clock tick for waking up a thread living on a
	 * remote CPU is not optimal since this involves IPI-signaled
	 * rescheds, this is still a valid case.
	 */
	if (cpumask_empty(&clock->affinity))
		return 0;

	if (cpumask_test_cpu(cpu, &clock->affinity))
		return cpu;
	
	return cpumask_first(&clock->affinity);
}

void __xntimer_set_affinity(struct xntimer *timer, struct xnsched *sched)
{				/* nklocked, IRQs off */
	struct xnclock *clock = xntimer_clock(timer);
	int cpu;

	/*
	 * Figure out which CPU is best suited for managing this
	 * timer, preferably picking xnsched_cpu(sched) if the ticking
	 * device moving the timer clock beats on that CPU. Otherwise,
	 * pick the first CPU from the clock affinity mask if set. If
	 * not, the timer is backed by a global device with no
	 * particular IRQ affinity, so it should always be queued to
	 * CPU0.
	 */
	cpu = 0;
	if (!cpumask_empty(&clock->affinity))
		cpu = get_clock_cpu(clock, xnsched_cpu(sched));

	xntimer_migrate(timer, xnsched_struct(cpu));
}
EXPORT_SYMBOL_GPL(__xntimer_set_affinity);

#endif /* CONFIG_SMP */

/**
 * Get the count of overruns for the last tick.
 *
 * This service returns the count of pending overruns for the last
 * tick of a given timer, as measured by the difference between the
 * expected expiry date of the timer and the date @a now passed as
 * argument.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param waiter The thread for which the overrun count is being
 * collected.
 *
 * @param now current date (as
 * xnclock_read_raw(xntimer_clock(timer)))
 *
 * @return the number of overruns of @a timer at date @a now
 *
 * @coretags{unrestricted, atomic-entry}
 */
unsigned long long xntimer_get_overruns(struct xntimer *timer,
					struct xnthread *waiter,
					xnticks_t now)
{
	xnticks_t period = timer->interval;
	unsigned long long overruns = 0;
	xnsticks_t delta;
	xntimerq_t *q;

	atomic_only();

	delta = now - xntimer_pexpect(timer);
	if (unlikely(delta >= (xnsticks_t) period)) {
		period = timer->interval_ns;
		delta = xnclock_ticks_to_ns(xntimer_clock(timer), delta);
		overruns = xnarch_div64(delta, period);
		timer->pexpect_ticks += overruns;
		if (xntimer_running_p(timer)) {
			XENO_BUG_ON(COBALT, (timer->status &
				    (XNTIMER_DEQUEUED|XNTIMER_PERIODIC))
				    != XNTIMER_PERIODIC);
				q = xntimer_percpu_queue(timer);
			xntimer_dequeue(timer, q);
			while (xntimerh_date(&timer->aplink) < now) {
				timer->periodic_ticks++;
				xntimer_update_date(timer);
			}
			xntimer_enqueue_and_program(timer, q);
		}
	}

	timer->pexpect_ticks++;

	/* Hide overruns due to the most recent ptracing session. */
	if (xnthread_test_localinfo(waiter, XNHICCUP))
		return 0;

	return overruns;
}
EXPORT_SYMBOL_GPL(xntimer_get_overruns);

char *xntimer_format_time(xnticks_t ns, char *buf, size_t bufsz)
{
	unsigned long ms, us, rem;
	int len = (int)bufsz;
	char *p = buf;
	xnticks_t sec;

	if (ns == 0 && bufsz > 1) {
		strcpy(buf, "-");
		return buf;
	}

	sec = xnclock_divrem_billion(ns, &rem);
	us = rem / 1000;
	ms = us / 1000;
	us %= 1000;

	if (sec) {
		p += ksformat(p, bufsz, "%Lus", sec);
		len = bufsz - (p - buf);
	}

	if (len > 0 && (ms || (sec && us))) {
		p += ksformat(p, bufsz - (p - buf), "%lums", ms);
		len = bufsz - (p - buf);
	}

	if (len > 0 && us)
		p += ksformat(p, bufsz - (p - buf), "%luus", us);

	return buf;
}
EXPORT_SYMBOL_GPL(xntimer_format_time);

#if defined(CONFIG_XENO_OPT_TIMER_RBTREE)
static inline bool xntimerh_is_lt(xntimerh_t *left, xntimerh_t *right)
{
	return left->date < right->date
		|| (left->date == right->date && left->prio > right->prio);
}

void xntimerq_insert(xntimerq_t *q, xntimerh_t *holder)
{
	struct rb_node **new = &q->root.rb_node, *parent = NULL;

	if (!q->head)
		q->head = holder;
	else if (xntimerh_is_lt(holder, q->head)) {
		parent = &q->head->link;
		new = &parent->rb_left;
		q->head = holder;
	} else while (*new) {
		xntimerh_t *i = container_of(*new, xntimerh_t, link);

		parent = *new;
		if (xntimerh_is_lt(holder, i))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	rb_link_node(&holder->link, parent, new);
	rb_insert_color(&holder->link, &q->root);
}
#endif

/** @} */
