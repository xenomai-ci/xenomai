// SPDX-License-Identifier: LGPL-2.1
/*
 * Copyright (C) 2021 Intel Inc, Hongzhan Chen <hongzhan.chen@intel.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/sched.h>
#include <cobalt/uapi/sched.h>
#include <sched.h>
#include "event-parse.h"
#include "trace-seq.h"

static void write_policy(struct trace_seq *p, int policy)
{
	trace_seq_printf(p, "policy=");

	switch (policy) {
	case SCHED_QUOTA:
		trace_seq_printf(p, "quota ");
		break;
	case SCHED_TP:
		trace_seq_printf(p, "tp ");
		break;
	case SCHED_NORMAL:
		trace_seq_printf(p, "normal ");
		break;
	case SCHED_SPORADIC:
		trace_seq_printf(p, "sporadic ");
		break;
	case SCHED_RR:
		trace_seq_printf(p, "rr ");
		break;
	case SCHED_FIFO:
		trace_seq_printf(p, "fifo ");
		break;
	case SCHED_COBALT:
		trace_seq_printf(p, "cobalt ");
		break;
	case SCHED_WEAK:
		trace_seq_printf(p, "weak ");
		break;
	default:
		trace_seq_printf(p, "unknown ");
		break;
	}
}

/* save param */
static void write_param(struct tep_format_field *field,
				struct tep_record *record,
				struct trace_seq *p, int policy)
{
	int offset;
	struct sched_param_ex *params;

	offset = field->offset;

	if (!strncmp(field->type, "__data_loc", 10)) {
		unsigned long long v;

		if (tep_read_number_field(field, record->data, &v)) {
			trace_seq_printf(p, "invalid_data_loc");
			return;
		}
		offset = v & 0xffff;

	}

	params = (struct sched_param_ex *)((char *)record->data + offset);

	trace_seq_printf(p, "param: { ");

	switch (policy) {
	case SCHED_QUOTA:
		trace_seq_printf(p, "priority=%d, group=%d",
				 params->sched_priority,
				 params->sched_quota_group);
		break;
	case SCHED_TP:
		trace_seq_printf(p, "priority=%d, partition=%d",
				 params->sched_priority,
				 params->sched_tp_partition);
		break;
	case SCHED_NORMAL:
		break;
	case SCHED_SPORADIC:
		trace_seq_printf(p, "priority=%d, low_priority=%d, ",
				 params->sched_priority,
				 params->sched_ss_low_priority);

		trace_seq_printf(p, "budget=(%ld.%09ld), period=(%ld.%09ld), ",
				 params->sched_ss_init_budget.tv_sec,
				 params->sched_ss_init_budget.tv_nsec);

		trace_seq_printf(p, "maxrepl=%d",
				 params->sched_ss_max_repl);
		break;
	case SCHED_RR:
	case SCHED_FIFO:
	case SCHED_COBALT:
	case SCHED_WEAK:
	default:
		trace_seq_printf(p, "priority=%d", params->sched_priority);
		break;
	}
	trace_seq_printf(p, " }");
	trace_seq_putc(p, '\0');

}

static int cobalt_schedparam_handler(struct trace_seq *s,
				struct tep_record *record,
				struct tep_event *event, void *context)
{
	struct tep_format_field *field;
	unsigned long long val;

	if (tep_get_field_val(s, event, "pth", record, &val, 1))
		return trace_seq_putc(s, '!');
	trace_seq_puts(s, "pth: ");
	trace_seq_printf(s, "0x%08llx ", val);

	if (tep_get_field_val(s, event, "policy", record, &val, 1) == 0)
		write_policy(s, val);

	field = tep_find_field(event, "param_ex");
	if (field)
		write_param(field, record, s, val);

	return 0;
}

int TEP_PLUGIN_LOADER(struct tep_handle *tep)
{
	tep_register_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_setschedparam",
					cobalt_schedparam_handler, NULL);

	tep_register_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_getschedparam",
					cobalt_schedparam_handler, NULL);

	tep_register_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_create",
					cobalt_schedparam_handler, NULL);

	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *tep)
{
	tep_unregister_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_setschedparam",
					cobalt_schedparam_handler, NULL);

	tep_unregister_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_getschedparam",
					cobalt_schedparam_handler, NULL);

	tep_unregister_event_handler(tep, -1, "cobalt_posix", "cobalt_pthread_create",
					cobalt_schedparam_handler, NULL);
}
