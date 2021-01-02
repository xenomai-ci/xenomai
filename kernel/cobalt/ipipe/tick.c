/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001,2002,2003,2007,2012 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 */
#include <linux/ipipe.h>
#include <linux/ipipe_tickdev.h>
#include <linux/sched.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/intr.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/arith.h>

extern struct xnintr nktimer;

/**
 * @internal
 * @fn static int program_htick_shot(unsigned long delay, struct clock_event_device *cdev)
 *
 * @brief Program next host tick as a Xenomai timer event.
 *
 * Program the next shot for the host tick on the current CPU.
 * Emulation is done using a nucleus timer attached to the master
 * timebase.
 *
 * @param delay The time delta from the current date to the next tick,
 * expressed as a count of nanoseconds.
 *
 * @param cdev An pointer to the clock device which notifies us.
 *
 * @coretags{unrestricted}
 */
static int program_htick_shot(unsigned long delay,
			      struct clock_event_device *cdev)
{
	struct xnsched *sched;
	int ret;
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	sched = xnsched_current();
	ret = xntimer_start(&sched->htimer, delay, XN_INFINITE, XN_RELATIVE);
	xnlock_put_irqrestore(&nklock, s);

	return ret ? -ETIME : 0;
}

/**
 * @internal
 * @fn void switch_htick_mode(enum clock_event_mode mode, struct clock_event_device *cdev)
 *
 * @brief Tick mode switch emulation callback.
 *
 * Changes the host tick mode for the tick device of the current CPU.
 *
 * @param mode The new mode to switch to. The possible values are:
 *
 * - CLOCK_EVT_MODE_ONESHOT, for a switch to oneshot mode.
 *
 * - CLOCK_EVT_MODE_PERIODIC, for a switch to periodic mode. The current
 * implementation for the generic clockevent layer Linux exhibits
 * should never downgrade from a oneshot to a periodic tick mode, so
 * this mode should not be encountered. This said, the associated code
 * is provided, basically for illustration purposes.
 *
 * - CLOCK_EVT_MODE_SHUTDOWN, indicates the removal of the current
 * tick device. Normally, the nucleus only interposes on tick devices
 * which should never be shut down, so this mode should not be
 * encountered.
 *
 * @param cdev An opaque pointer to the clock device which notifies us.
 *
 * @coretags{unrestricted}
 *
 * @note GENERIC_CLOCKEVENTS is required from the host kernel.
 */
static void switch_htick_mode(enum clock_event_mode mode,
			      struct clock_event_device *cdev)
{
	struct xnsched *sched;
	xnticks_t tickval;
	spl_t s;

	if (mode == CLOCK_EVT_MODE_ONESHOT)
		return;

	xnlock_get_irqsave(&nklock, s);

	sched = xnsched_current();

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		tickval = 1000000000UL / HZ;
		xntimer_start(&sched->htimer, tickval, tickval, XN_RELATIVE);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
		xntimer_stop(&sched->htimer);
		break;
	default:
		XENO_BUG(COBALT);
	}

	xnlock_put_irqrestore(&nklock, s);
}

static int grab_timer_on_cpu(int cpu)
{
	int tickval, ret;

	ret = ipipe_timer_start(xnintr_core_clock_handler,
				switch_htick_mode, program_htick_shot, cpu);
	switch (ret) {
	case CLOCK_EVT_MODE_PERIODIC:
		/*
		 * Oneshot tick emulation callback won't be used, ask
		 * the caller to start an internal timer for emulating
		 * a periodic tick.
		 */
		tickval = 1000000000UL / HZ;
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		/* oneshot tick emulation */
		tickval = 1;
		break;

	case CLOCK_EVT_MODE_UNUSED:
		/* we don't need to emulate the tick at all. */
		tickval = 0;
		break;

	case CLOCK_EVT_MODE_SHUTDOWN:
		return -ENODEV;

	default:
		return ret;
	}

	return tickval;
}

/**
 * @fn int pipeline_install_tick_proxy(void)
 * @brief Grab the hardware timer on all real-time CPUs.
 *
 * pipeline_install_tick_proxy() grabs and tunes the hardware timer for all
 * real-time CPUs.
 *
 * Host tick emulation is performed for sharing the clock chip between
 * Linux and Xenomai.
 *
 * @return a positive value is returned on success, representing the
 * duration of a Linux periodic tick expressed as a count of
 * nanoseconds; zero should be returned when the Linux kernel does not
 * undergo periodic timing on the given CPU (e.g. oneshot
 * mode). Otherwise:
 *
 * - -EBUSY is returned if the hardware timer has already been
 * grabbed.  xntimer_release_hardware() must be issued before
 * pipeline_install_tick_proxy() is called again.
 *
 * - -ENODEV is returned if the hardware timer cannot be used.  This
 * situation may occur after the kernel disabled the timer due to
 * invalid calibration results; in such a case, such hardware is
 * unusable for any timing duties.
 *
 * @coretags{secondary-only}
 */

int pipeline_install_tick_proxy(void)
{
	struct xnsched *sched;
	int ret, cpu, _cpu;
	spl_t s;

#ifdef CONFIG_XENO_OPT_STATS_IRQS
	/*
	 * Only for statistical purpose, the timer interrupt is
	 * attached by pipeline_install_tick_proxy().
	 */
	xnintr_init(&nktimer, "[timer]",
		    per_cpu(ipipe_percpu.hrtimer_irq, 0), NULL, NULL, 0);
#endif /* CONFIG_XENO_OPT_STATS_IRQS */

	nkclock.wallclock_offset =
		ktime_to_ns(ktime_get_real()) - xnclock_read_monotonic(&nkclock);

	ret = xntimer_setup_ipi();
	if (ret)
		return ret;

	for_each_realtime_cpu(cpu) {
		ret = grab_timer_on_cpu(cpu);
		if (ret < 0)
			goto fail;

		xnlock_get_irqsave(&nklock, s);

		/*
		 * If the current tick device for the target CPU is
		 * periodic, we won't be called back for host tick
		 * emulation. Therefore, we need to start a periodic
		 * nucleus timer which will emulate the ticking for
		 * that CPU, since we are going to hijack the hw clock
		 * chip for managing our own system timer.
		 *
		 * CAUTION:
		 *
		 * - nucleus timers may be started only _after_ the hw
		 * timer has been set up for the target CPU through a
		 * call to pipeline_install_tick_proxy().
		 *
		 * - we don't compensate for the elapsed portion of
		 * the current host tick, since we cannot get this
		 * information easily for all CPUs except the current
		 * one, and also because of the declining relevance of
		 * the jiffies clocksource anyway.
		 *
		 * - we must not hold the nklock across calls to
		 * pipeline_install_tick_proxy().
		 */

		sched = xnsched_struct(cpu);
		/* Set up timer with host tick period if valid. */
		if (ret > 1)
			xntimer_start(&sched->htimer, ret, ret, XN_RELATIVE);
		else if (ret == 1)
			xntimer_start(&sched->htimer, 0, 0, XN_RELATIVE);

		xnlock_put_irqrestore(&nklock, s);
	}

	return 0;
fail:
	for_each_realtime_cpu(_cpu) {
		if (_cpu == cpu)
			break;
		xnlock_get_irqsave(&nklock, s);
		sched = xnsched_struct(cpu);
		xntimer_stop(&sched->htimer);
		xnlock_put_irqrestore(&nklock, s);
		ipipe_timer_stop(_cpu);
	}

	xntimer_release_ipi();

	return ret;
}

/**
 * @fn void pipeline_uninstall_tick_proxy(void)
 * @brief Release hardware timers.
 *
 * Releases hardware timers previously grabbed by a call to
 * pipeline_install_tick_proxy().
 *
 * @coretags{secondary-only}
 */
void pipeline_uninstall_tick_proxy(void)
{
	int cpu;

	/*
	 * We must not hold the nklock while stopping the hardware
	 * timer, since this could cause deadlock situations to arise
	 * on SMP systems.
	 */
	for_each_realtime_cpu(cpu)
		ipipe_timer_stop(cpu);

	xntimer_release_ipi();

#ifdef CONFIG_XENO_OPT_STATS_IRQS
	xnintr_destroy(&nktimer);
#endif /* CONFIG_XENO_OPT_STATS_IRQS */
}
