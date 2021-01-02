/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2019 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <pipeline/machine.h>
#include <linux/ipipe_tickdev.h>
#include <cobalt/kernel/sched.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/apc.h>

static unsigned long timerfreq_arg;
module_param_named(timerfreq, timerfreq_arg, ulong, 0444);

static unsigned long clockfreq_arg;
module_param_named(clockfreq, clockfreq_arg, ulong, 0444);

void apc_dispatch(unsigned int virq, void *arg);

int __init pipeline_init(void)
{
	struct ipipe_sysinfo sysinfo;
	int ret, virq;

	ret = ipipe_select_timers(&xnsched_realtime_cpus);
	if (ret < 0)
		return ret;

	ipipe_get_sysinfo(&sysinfo);

	if (timerfreq_arg == 0)
		timerfreq_arg = sysinfo.sys_hrtimer_freq;

	if (clockfreq_arg == 0)
		clockfreq_arg = sysinfo.sys_hrclock_freq;

	if (clockfreq_arg == 0) {
		printk(XENO_ERR "null clock frequency? Aborting.\n");
		return -ENODEV;
	}

	cobalt_pipeline.timer_freq = timerfreq_arg;
	cobalt_pipeline.clock_freq = clockfreq_arg;

	if (cobalt_machine.init) {
		ret = cobalt_machine.init();
		if (ret)
			return ret;
	}

	ipipe_register_head(&xnsched_realtime_domain, "Xenomai");

	ret = -EBUSY;
	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto fail_apc;

	cobalt_pipeline.apc_virq = virq;

	ipipe_request_irq(ipipe_root_domain,
			  cobalt_pipeline.apc_virq,
			  apc_dispatch,
			  NULL, NULL);

	virq = ipipe_alloc_virq();
	if (virq == 0)
		goto fail_escalate;

	cobalt_pipeline.escalate_virq = virq;

	ipipe_request_irq(&xnsched_realtime_domain,
			  cobalt_pipeline.escalate_virq,
			  (ipipe_irq_handler_t)__xnsched_run_handler,
			  NULL, NULL);

	ret = xnclock_init(cobalt_pipeline.clock_freq);
	if (ret)
		goto fail_clock;

	return 0;

fail_clock:
	ipipe_free_irq(&xnsched_realtime_domain,
		       cobalt_pipeline.escalate_virq);
	ipipe_free_virq(cobalt_pipeline.escalate_virq);
fail_escalate:
	ipipe_free_irq(ipipe_root_domain,
		       cobalt_pipeline.apc_virq);
	ipipe_free_virq(cobalt_pipeline.apc_virq);
fail_apc:
	ipipe_unregister_head(&xnsched_realtime_domain);

	if (cobalt_machine.cleanup)
		cobalt_machine.cleanup();

	return ret;
}

int __init pipeline_late_init(void)
{
	if (cobalt_machine.late_init)
		return cobalt_machine.late_init();

	return 0;
}

__init void pipeline_cleanup(void)
{
	ipipe_unregister_head(&xnsched_realtime_domain);
	ipipe_free_irq(&xnsched_realtime_domain,
		       cobalt_pipeline.escalate_virq);
	ipipe_free_virq(cobalt_pipeline.escalate_virq);
	ipipe_timers_release();
	xnclock_cleanup();
}
