/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2001,2002,2003,2007,2012 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
 */

#include <linux/tick.h>
#include <cobalt/kernel/intr.h>
#include <pipeline/tick.h>

int pipeline_install_tick_proxy(void)
{
	int ret;

	ret = pipeline_request_timer_ipi(xnintr_core_clock_handler);
	if (ret)
		return ret;

	/* Install the proxy tick device */
	TODO();	ret = 0;
	if (ret)
		goto fail_proxy;

	return 0;

fail_proxy:
	pipeline_free_timer_ipi();

	return ret;
}

void pipeline_uninstall_tick_proxy(void)
{
	/* Uninstall the proxy tick device. */
	TODO();

	pipeline_free_timer_ipi();
}
