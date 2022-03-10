// SPDX-License-Identifier: LGPL-2.1

/*
 * Copyright (C) 2021 Intel Inc, Hongzhan Chen <hongzhan.chen@intel.com>
 */

/**
 *  @file    CobaltSwitchEvents.cpp
 *  @brief   Defines a callback function for Xenomai Cobalt context switch
 *           events used to plot in cobalt blue the out-of-band state of
 *           the task
 */

// KernelShark
#include "libkshark.h"
#include "libkshark-plugin.h"
#include "xenomai_cobalt_switch_events.h"
#include "KsPlotTools.hpp"
#include "KsPlugins.hpp"
#include "KsPluginsGUI.hpp"

static void *ks4xenomai_ptr;

/**
 * @brief Provide the plugin with a pointer to the KsMainWindow object (the GUI
 * itself) such that the plugin can manipulate the GUI.
 */
__hidden void *plugin_set_gui_ptr(void *gui_ptr)
{
	ks4xenomai_ptr = gui_ptr;
	return nullptr;
}

/**
 * This class represents the graphical element visualizing OOB state between
 *  two cobalt_switch_context events.
 */
class XenomaiSwitchBox : public LatencyBox
{
	/** On double click do. */
	void _doubleClick() const override
	{
		markEntryB(ks4xenomai_ptr, _data[1]->entry);
		markEntryA(ks4xenomai_ptr, _data[0]->entry);
	}
};

/*
 * Ideally, the cobalt_switch_context has to be the last trace event recorded before
 * the task is preempted. Because of this, when the data is loaded (the first pass),
 * the "pid" field of the cobalt_switch_context entries gets edited by this plugin
 * to be equal to the "next pid" of the cobalt_switch_context event. However, in
 * reality the cobalt_switch_context event may be followed by some trailing events
 * from the same task (printk events for example). This has the effect of extending
 * the graph of the task outside of the actual duration of the task. The "second
 * pass" over the data is used to fix this problem. It takes advantage of the
 * "next" field of the entry (this field is set during the first pass) to search
 * for trailing events after the "cobalt_switch_context". In addition, when
 * we find that it try to switch in-band because next-pid is zero, we prefer to
 * skip this event because it try to leave oob not enterring.
 */
static void secondPass(plugin_cobalt_context *plugin_ctx)
{
	kshark_data_container *cSS;
	kshark_entry *e;
	int pid_rec, switch_inband;

	cSS = plugin_ctx->cs_data;
	for (ssize_t i = 0; i < cSS->size; ++i) {
		switch_inband = plugin_cobalt_check_switch_inband(
				cSS->data[i]->field);

		/* we skip cobalt_switch_context that try to
		 * switch into in-band state because we just handle
		 * out-of-band
		 */
		if (switch_inband)
			continue;
		pid_rec = plugin_sched_get_pid(cSS->data[i]->field);
		e = cSS->data[i]->entry;
		if (!e->next || e->pid == 0 ||
		    e->event_id == e->next->event_id ||
		    pid_rec != e->next->pid)
			continue;

		e = e->next;
		/* Find all trailing events. */
		for (; e->next; e = e->next) {
			if (e->pid == plugin_sched_get_pid(
						cSS->data[i]->field)) {
				/*
				 * Change the "pid" to be equal to the "next
				 * pid" of the cobalt_switch_context event
				 * and leave a sign that you edited this
				 * entry.
				 */
				e->pid = cSS->data[i]->entry->pid;
				e->visible &= ~KS_PLUGIN_UNTOUCHED_MASK;

				/*  This is the last trailing event, we finish
				 *  this round.
				 */
				if (e->next->pid != plugin_sched_get_pid(
						cSS->data[i]->field))
					break;
			}
		}
	}
}

/**
 * @brief Plugin's draw function.
 *
 * @param argv_c: A C pointer to be converted to KsCppArgV (C++ struct).
 * @param sd: Data stream identifier.
 * @param pid: Process Id.
 * @param draw_action: Draw action identifier.
 */
__hidden void plugin_cobalt_draw(kshark_cpp_argv *argv_c,
			  int sd, int pid, int draw_action)
{
	plugin_cobalt_context *plugin_ctx;

	if (!(draw_action & KSHARK_TASK_DRAW) || pid == 0)
		return;

	plugin_ctx = __get_context(sd);
	if (!plugin_ctx)
		return;

	KsCppArgV *argvCpp = KS_ARGV_TO_CPP(argv_c);

	if (!plugin_ctx->second_pass_done) {
		/* The second pass is not done yet. */
		secondPass(plugin_ctx);
		plugin_ctx->second_pass_done = true;
	}

	IsApplicableFunc checkFieldCS = [=] (kshark_data_container *d,
					     ssize_t i) {
		return !(plugin_cobalt_check_switch_inband(d->data[i]->field)) &&
			d->data[i]->entry->pid == pid;
	};

	IsApplicableFunc checkEntryPid = [=] (kshark_data_container *d,
					      ssize_t i) {
		return plugin_sched_get_pid(d->data[i]->field) == pid;
	};

	eventFieldIntervalPlot(argvCpp,
			       plugin_ctx->cs_data, checkFieldCS,
			       plugin_ctx->cs_data, checkEntryPid,
			       makeLatencyBox<XenomaiSwitchBox>,
			       {0, 71, 171}, // Cobalt Blue
			       -1);         // Default size
}
