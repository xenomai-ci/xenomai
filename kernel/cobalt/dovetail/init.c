/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#include <linux/init.h>
#include <pipeline/machine.h>
#include <cobalt/kernel/clock.h>
#include <cobalt/kernel/assert.h>

int __init pipeline_init(void)
{
	int ret;

	if (cobalt_machine.init) {
		ret = cobalt_machine.init();
		if (ret)
			return ret;
	}

	/* Enable the Xenomai out-of-band stage */
	enable_oob_stage("Xenomai");

	ret = xnclock_init();
	if (ret)
		goto fail_clock;

	return 0;

fail_clock:
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
	/* Disable the Xenomai stage */
	disable_oob_stage();

	xnclock_cleanup();
}
