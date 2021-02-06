/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001,2002,2003,2007,2012 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 */

#include <linux/tick.h>
#include <linux/clockchips.h>
#include <cobalt/kernel/intr.h>
#include <pipeline/tick.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/timer.h>

static DEFINE_PER_CPU(struct clock_proxy_device *, proxy_device);

const char *pipeline_timer_name(void)
{
	struct clock_proxy_device *dev = __this_cpu_read(proxy_device);
	struct clock_event_device *real_dev = dev->real_device;

	/*
	 * Return the name of the current clock event chip, which is
	 * the real device controlled by the proxy tick device.
	 */
	return real_dev->name;
}

void pipeline_set_timer_shot(unsigned long delay) /* ns */
{
	struct clock_proxy_device *dev = __this_cpu_read(proxy_device);
	struct clock_event_device *real_dev = dev->real_device;
	u64 cycles;
	ktime_t t;
	int ret;

	if (real_dev->features & CLOCK_EVT_FEAT_KTIME) {
		t = ktime_add(delay, xnclock_core_read_raw());
		real_dev->set_next_ktime(t, real_dev);
	} else {
		if (delay <= 0) {
			delay = real_dev->min_delta_ns;
		} else {
			delay = min_t(int64_t, delay,
				real_dev->max_delta_ns);
			delay = max_t(int64_t, delay,
				real_dev->min_delta_ns);
		}
		cycles = ((u64)delay * real_dev->mult) >> real_dev->shift;
		ret = real_dev->set_next_event(cycles, real_dev);
		if (ret)
			real_dev->set_next_event(real_dev->min_delta_ticks,
						real_dev);
	}
}

static int proxy_set_next_ktime(ktime_t expires,
				struct clock_event_device *proxy_dev) /* hard irqs on/off */
{
	struct xnsched *sched;
	unsigned long flags;
	ktime_t delta;
	int ret;

	/*
	 * Expiration dates of in-band timers are based on the common
	 * monotonic time base. If the timeout date has already
	 * elapsed, make sure xntimer_start() does not fail with
	 * -ETIMEDOUT but programs the hardware for ticking
	 * immediately instead.
	 */
	delta = ktime_sub(expires, ktime_get());
	if (delta < 0)
		delta = 0;

	xnlock_get_irqsave(&nklock, flags);
	sched = xnsched_current();
	ret = xntimer_start(&sched->htimer, delta, XN_INFINITE, XN_RELATIVE);
	xnlock_put_irqrestore(&nklock, flags);

	return ret ? -ETIME : 0;
}

bool pipeline_must_force_program_tick(struct xnsched *sched)
{
	return sched->lflags & XNTSTOP;
}

static int proxy_set_oneshot_stopped(struct clock_event_device *proxy_dev)
{
	struct clock_event_device *real_dev;
	struct clock_proxy_device *dev;
	struct xnsched *sched;
	spl_t s;

	dev = container_of(proxy_dev, struct clock_proxy_device, proxy_device);

	/*
	 * In-band wants to disable the clock hardware on entering a
	 * tickless state, so we have to stop our in-band tick
	 * emulation. Propagate the request for shutting down the
	 * hardware to the real device only if we have no outstanding
	 * OOB timers. CAUTION: the in-band timer is counted when
	 * assessing the RQ_IDLE condition, so we need to stop it
	 * prior to testing the latter.
	 */
	xnlock_get_irqsave(&nklock, s);
	sched = xnsched_current();
	xntimer_stop(&sched->htimer);
	sched->lflags |= XNTSTOP;

	if (sched->lflags & XNIDLE) {
		real_dev = dev->real_device;
		real_dev->set_state_oneshot_stopped(real_dev);
	}

	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

static void setup_proxy(struct clock_proxy_device *dev)
{
	struct clock_event_device *proxy_dev = &dev->proxy_device;

	dev->handle_oob_event = (typeof(dev->handle_oob_event))
		xnintr_core_clock_handler;
	proxy_dev->features |= CLOCK_EVT_FEAT_KTIME;
	proxy_dev->set_next_ktime = proxy_set_next_ktime;
	if (proxy_dev->set_state_oneshot_stopped)
		proxy_dev->set_state_oneshot_stopped = proxy_set_oneshot_stopped;
	__this_cpu_write(proxy_device, dev);
}

#ifdef CONFIG_SMP
static irqreturn_t tick_ipi_handler(int irq, void *dev_id)
{
	xnintr_core_clock_handler();

	return IRQ_HANDLED;
}
#endif

int pipeline_install_tick_proxy(void)
{
	int ret;

#ifdef CONFIG_SMP
	/*
	 * We may be running a SMP kernel on a uniprocessor machine
	 * whose interrupt controller provides no IPI: attempt to hook
	 * the timer IPI only if the hardware can support multiple
	 * CPUs.
	 */
	if (num_possible_cpus() > 1) {
		ret = __request_percpu_irq(TIMER_OOB_IPI,
					tick_ipi_handler,
					IRQF_OOB, "Xenomai timer IPI",
					&cobalt_machine_cpudata);
		if (ret)
			return ret;
	}
#endif

	/* Install the proxy tick device */
	ret = tick_install_proxy(setup_proxy, &xnsched_realtime_cpus);
	if (ret)
		goto fail_proxy;

	return 0;

fail_proxy:
#ifdef CONFIG_SMP
	if (num_possible_cpus() > 1)
		free_percpu_irq(TIMER_OOB_IPI, &cobalt_machine_cpudata);
#endif

	return ret;
}

void pipeline_uninstall_tick_proxy(void)
{
	/* Uninstall the proxy tick device. */
	tick_uninstall_proxy(&xnsched_realtime_cpus);

#ifdef CONFIG_SMP
	if (num_possible_cpus() > 1)
		free_percpu_irq(TIMER_OOB_IPI, &cobalt_machine_cpudata);
#endif
}
