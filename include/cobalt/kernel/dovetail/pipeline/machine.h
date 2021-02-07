/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2020 Philippe Gerum  <rpm@xenomai.org>
 */

#ifndef _COBALT_KERNEL_DOVETAIL_MACHINE_H
#define _COBALT_KERNEL_DOVETAIL_MACHINE_H

#include <linux/percpu.h>

#ifdef CONFIG_FTRACE
#define boot_lat_trace_notice "[LTRACE]"
#else
#define boot_lat_trace_notice ""
#endif

struct vm_area_struct;

struct cobalt_machine {
	const char *name;
	int (*init)(void);
	int (*late_init)(void);
	void (*cleanup)(void);
	void (*prefault)(struct vm_area_struct *vma);
	const char *const *fault_labels;
};

extern struct cobalt_machine cobalt_machine;

struct cobalt_machine_cpudata {
	unsigned int faults[32];
};

DECLARE_PER_CPU(struct cobalt_machine_cpudata, cobalt_machine_cpudata);

struct cobalt_pipeline {
#ifdef CONFIG_SMP
	cpumask_t supported_cpus;
#endif
};

int pipeline_init(void);

int pipeline_late_init(void);

void pipeline_cleanup(void);

extern struct cobalt_pipeline cobalt_pipeline;

#endif /* !_COBALT_KERNEL_IPIPE_MACHINE_H */
