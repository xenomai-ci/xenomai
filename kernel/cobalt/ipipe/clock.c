/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 */

#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/vdso.h>
#include <cobalt/kernel/arith.h>
#include <pipeline/machine.h>

static unsigned long long clockfreq;

#ifdef XNARCH_HAVE_LLMULSHFT

static unsigned int tsc_scale, tsc_shift;

#ifdef XNARCH_HAVE_NODIV_LLIMD

static struct xnarch_u32frac tsc_frac;

long long xnclock_core_ns_to_ticks(long long ns)
{
	return xnarch_nodiv_llimd(ns, tsc_frac.frac, tsc_frac.integ);
}

#else /* !XNARCH_HAVE_NODIV_LLIMD */

long long xnclock_core_ns_to_ticks(long long ns)
{
	return xnarch_llimd(ns, 1 << tsc_shift, tsc_scale);
}

#endif /* !XNARCH_HAVE_NODIV_LLIMD */

xnsticks_t xnclock_core_ticks_to_ns(xnsticks_t ticks)
{
	return xnarch_llmulshft(ticks, tsc_scale, tsc_shift);
}

xnsticks_t xnclock_core_ticks_to_ns_rounded(xnsticks_t ticks)
{
	unsigned int shift = tsc_shift - 1;
	return (xnarch_llmulshft(ticks, tsc_scale, shift) + 1) / 2;
}

#else  /* !XNARCH_HAVE_LLMULSHFT */

xnsticks_t xnclock_core_ticks_to_ns(xnsticks_t ticks)
{
	return xnarch_llimd(ticks, 1000000000, clockfreq);
}

xnsticks_t xnclock_core_ticks_to_ns_rounded(xnsticks_t ticks)
{
	return (xnarch_llimd(ticks, 1000000000, clockfreq/2) + 1) / 2;
}

xnsticks_t xnclock_core_ns_to_ticks(xnsticks_t ns)
{
	return xnarch_llimd(ns, clockfreq, 1000000000);
}

#endif /* !XNARCH_HAVE_LLMULSHFT */

EXPORT_SYMBOL_GPL(xnclock_core_ticks_to_ns);
EXPORT_SYMBOL_GPL(xnclock_core_ticks_to_ns_rounded);
EXPORT_SYMBOL_GPL(xnclock_core_ns_to_ticks);

int pipeline_get_host_time(struct timespec *tp)
{
#ifdef CONFIG_IPIPE_HAVE_HOSTRT
	struct xnvdso_hostrt_data *hostrt_data;
	u64 now, base, mask, cycle_delta;
	__u32 mult, shift;
	unsigned long rem;
	urwstate_t tmp;
	__u64 nsec;

	hostrt_data = get_hostrt_data();
	BUG_ON(!hostrt_data);

	if (unlikely(!hostrt_data->live))
		return -1;

	/*
	 * Note: Disabling HW interrupts around writes to hostrt_data
	 * ensures that a reader (on the Xenomai side) cannot
	 * interrupt a writer (on the Linux kernel side) on the same
	 * CPU.  The urw block is required when a reader is
	 * interleaved by a writer on a different CPU. This follows
	 * the approach from userland, where taking the spinlock is
	 * not possible.
	 */
	unsynced_read_block(&tmp, &hostrt_data->lock) {
		now = xnclock_read_raw(&nkclock);
		base = hostrt_data->cycle_last;
		mask = hostrt_data->mask;
		mult = hostrt_data->mult;
		shift = hostrt_data->shift;
		tp->tv_sec = hostrt_data->wall_sec;
		nsec = hostrt_data->wall_nsec;
	}

	/*
	 * At this point, we have a consistent copy of the fundamental
	 * data structure - calculate the interval between the current
	 * and base time stamp cycles, and convert the difference
	 * to nanoseconds.
	 */
	cycle_delta = (now - base) & mask;
	nsec += (cycle_delta * mult) >> shift;

	/* Convert to the desired sec, usec representation */
	tp->tv_sec += xnclock_divrem_billion(nsec, &rem);
	tp->tv_nsec = rem;

	return 0;
#else
	return -EINVAL;
#endif
}

void pipeline_update_clock_freq(unsigned long long freq)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	clockfreq = freq;
#ifdef XNARCH_HAVE_LLMULSHFT
	xnarch_init_llmulshft(1000000000, freq, &tsc_scale, &tsc_shift);
#ifdef XNARCH_HAVE_NODIV_LLIMD
	xnarch_init_u32frac(&tsc_frac, 1 << tsc_shift, tsc_scale);
#endif
#endif
	cobalt_pipeline.clock_freq = freq;
	xnlock_put_irqrestore(&nklock, s);
}

void pipeline_init_clock(void)
{
	pipeline_update_clock_freq(cobalt_pipeline.clock_freq);
}
