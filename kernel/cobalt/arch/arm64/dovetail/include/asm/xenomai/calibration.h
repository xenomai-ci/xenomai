/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_ARM64_DOVETAIL_CALIBRATION_H
#define _COBALT_ARM64_DOVETAIL_CALIBRATION_H

static inline void xnarch_get_latencies(struct xnclock_gravity *p)
{
	unsigned int sched_latency;

#if CONFIG_XENO_OPT_TIMING_SCHEDLAT != 0
	sched_latency = CONFIG_XENO_OPT_TIMING_SCHEDLAT;
#else
	sched_latency = 5000;
#endif
	p->user = sched_latency;
	p->kernel = CONFIG_XENO_OPT_TIMING_KSCHEDLAT;
	p->irq = CONFIG_XENO_OPT_TIMING_IRQLAT;
}

#endif /* !_COBALT_ARM64_DOVETAIL_CALIBRATION_H */
