// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2021 Intel Inc, Hongzhan Chen <hongzhan.chen@intel.com>
 */

/**
 *  @file    xenomai_cobalt_switch_events.c
 *  @brief   handle xenomai cobalt switch context event
 */

// C
#include <stdlib.h>
#include <stdio.h>

// trace-cmd
#include <trace-cmd.h>

// KernelShark
#include "xenomai_cobalt_switch_events.h"
#include "libkshark-tepdata.h"

/** Plugin context instance. */

//! @cond Doxygen_Suppress

#define SWITCH_INBAND_SHIFT	PREV_STATE_SHIFT

#define SWITCH_INBAND_MASK	PREV_STATE_MASK

//! @endcond

static void cobalt_free_context(struct plugin_cobalt_context *plugin_ctx)
{
	if (!plugin_ctx)
		return;

	kshark_free_data_container(plugin_ctx->cs_data);
}

/* Use the most significant byte to store state marking switch in-band. */
static void plugin_cobalt_set_switch_inband_state(ks_num_field_t *field,
					ks_num_field_t inband_state)
{
	unsigned long long mask = SWITCH_INBAND_MASK << SWITCH_INBAND_SHIFT;
	*field &= ~mask;
	*field |= (inband_state & SWITCH_INBAND_MASK) << SWITCH_INBAND_SHIFT;
}

/**
 * @brief Retrieve the state of switch-in-band from the data field stored in
 *        the kshark_data_container object.
 *
 * @param field: Input location for the data field.
 */
__hidden int plugin_cobalt_check_switch_inband(ks_num_field_t field)
{
	unsigned long long mask = SWITCH_INBAND_MASK << SWITCH_INBAND_SHIFT;

	return (field & mask) >> SWITCH_INBAND_SHIFT;
}

/** A general purpose macro is used to define plugin context. */
KS_DEFINE_PLUGIN_CONTEXT(struct plugin_cobalt_context, cobalt_free_context);

static bool plugin_cobalt_init_context(struct kshark_data_stream *stream,
				      struct plugin_cobalt_context *plugin_ctx)
{
	struct tep_event *event;

	if (!kshark_is_tep(stream))
		return false;

	plugin_ctx->tep = kshark_get_tep(stream);

	event = tep_find_event_by_name(plugin_ctx->tep,
					"cobalt_core", "cobalt_switch_context");
	if (!event)
		return false;

	plugin_ctx->cobalt_switch_event = event;
	plugin_ctx->cobalt_switch_next_field =
		tep_find_any_field(event, "next_pid");

	plugin_ctx->second_pass_done = false;

	plugin_ctx->cs_data = kshark_init_data_container();
	if (!plugin_ctx->cs_data)
		return false;

	return true;
}

static void plugin_cobalt_switch_action(struct kshark_data_stream *stream,
				      void *rec, struct kshark_entry *entry)
{
	struct tep_record *record = (struct tep_record *) rec;
	struct plugin_cobalt_context *plugin_ctx;
	unsigned long long next_pid;
	ks_num_field_t ks_field = 0;
	int ret;

	plugin_ctx = __get_context(stream->stream_id);
	if (!plugin_ctx)
		return;

	ret = tep_read_number_field(plugin_ctx->cobalt_switch_next_field,
				    record->data, &next_pid);

	if (ret == 0 && next_pid >= 0) {
		plugin_sched_set_pid(&ks_field, entry->pid);
		if (next_pid == 0) {
			plugin_cobalt_set_switch_inband_state(&ks_field,
					SWITCH_INBAND_STATE);
		}

		kshark_data_container_append(plugin_ctx->cs_data,
				entry, ks_field);

		if (next_pid > 0)
			entry->pid = next_pid;
	}
}

/** Load this plugin. */
int KSHARK_PLOT_PLUGIN_INITIALIZER(struct kshark_data_stream *stream)
{
	struct plugin_cobalt_context *plugin_ctx;

	plugin_ctx = __init(stream->stream_id);
	if (!plugin_ctx || !plugin_cobalt_init_context(stream, plugin_ctx)) {
		__close(stream->stream_id);
		return 0;
	}

	if (plugin_ctx->cobalt_switch_event) {
		kshark_register_event_handler(stream,
						plugin_ctx->cobalt_switch_event->id,
						plugin_cobalt_switch_action);
	}

	kshark_register_draw_handler(stream, plugin_cobalt_draw);

	return 1;
}

/** Unload this plugin. */
int KSHARK_PLOT_PLUGIN_DEINITIALIZER(struct kshark_data_stream *stream)
{
	struct plugin_cobalt_context *plugin_ctx = __get_context(stream->stream_id);
	int ret = 0;

	if (plugin_ctx) {
		if (plugin_ctx->cobalt_switch_event) {
			kshark_unregister_event_handler(stream,
							plugin_ctx->cobalt_switch_event->id,
							plugin_cobalt_switch_action);
		}

		kshark_unregister_draw_handler(stream, plugin_cobalt_draw);

		ret = 1;
	}

	__close(stream->stream_id);

	return ret;
}

/** Initialize the control interface of the plugin. */
void *KSHARK_MENU_PLUGIN_INITIALIZER(void *gui_ptr)
{
	return plugin_set_gui_ptr(gui_ptr);
}
