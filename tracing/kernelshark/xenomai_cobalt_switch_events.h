/* SPDX-License-Identifier: LGPL-2.1 */

/*
 * Copyright (C) 2021 Intel Inc, Hongzhan Chen<hongzhan.chen@intel.com>
 */

/**
 *  @file    xenomai_cobalt_switch_events.h
 *  @brief   Plugin for xenomai cobalt switch context event
 */

#ifndef _KS_PLUGIN_COBALT_SWITCH_H
#define _KS_PLUGIN_COBALT_SWITCH_H

// KernelShark
#include "libkshark.h"
#include "plugins/common_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/** cobalt_switch_context is trying to switch in-band. */
#define SWITCH_INBAND_STATE	1

/** Structure representing a plugin-specific context. */
struct plugin_cobalt_context {
	/** Page event used to parse the page. */
	struct tep_handle	*tep;

	/** Pointer to the cobalt_switch_event object. */
	struct tep_event	*cobalt_switch_event;

	 /** Pointer to the cobalt_switch_next_field format descriptor. */
	struct tep_format_field *cobalt_switch_next_field;

	/** True if the second pass is already done. */
	bool	second_pass_done;

	/** Data container for cobalt_switch_context data. */
	struct kshark_data_container	*cs_data;

};

KS_DECLARE_PLUGIN_CONTEXT_METHODS(struct plugin_cobalt_context)

int plugin_cobalt_check_switch_inband(ks_num_field_t field);

void plugin_cobalt_draw(struct kshark_cpp_argv *argv, int sd, int pid,
		 int draw_action);

void *plugin_set_gui_ptr(void *gui_ptr);

#ifdef __cplusplus
}
#endif

#endif
