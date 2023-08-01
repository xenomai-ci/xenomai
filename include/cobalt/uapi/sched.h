/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_SCHED_H
#define _COBALT_UAPI_SCHED_H

#include <cobalt/uapi/kernel/types.h>

#define SCHED_COBALT		42
#define SCHED_WEAK		43

#ifndef SCHED_SPORADIC
#define SCHED_SPORADIC		10
#define sched_ss_low_priority	sched_u.ss.__sched_low_priority
#define sched_ss_repl_period	sched_u.ss.__sched_repl_period
#define sched_ss_init_budget	sched_u.ss.__sched_init_budget
#define sched_ss_max_repl	sched_u.ss.__sched_max_repl
#endif	/* !SCHED_SPORADIC */

struct __sched_ss_param {
	__s32 __sched_low_priority;
	int: 32;
	struct xn_ts64 __sched_repl_period;
	struct xn_ts64 __sched_init_budget;
	__s32 __sched_max_repl;
	int: 32;
};

#define sched_rr_quantum	sched_u.rr.__sched_rr_quantum

struct __sched_rr_param {
	struct xn_ts64 __sched_rr_quantum;
};

#ifndef SCHED_TP
#define SCHED_TP		11
#define sched_tp_partition	sched_u.tp.__sched_partition
#endif	/* !SCHED_TP */

struct __sched_tp_param {
	__s32 __sched_partition;
};

struct sched_tp_window {
	struct xn_ts64 offset;
	struct xn_ts64 duration;
	__s32 ptid;
	int: 32;
};

enum {
	sched_tp_install,
	sched_tp_uninstall,
	sched_tp_start,
	sched_tp_stop,
};
	
struct __sched_config_tp {
	__s32 op;
	__s32 nr_windows;
	struct sched_tp_window windows[0];
};

#define sched_tp_confsz(nr_win) \
  (sizeof(struct __sched_config_tp) + nr_win * sizeof(struct sched_tp_window))

#ifndef SCHED_QUOTA
#define SCHED_QUOTA		12
#define sched_quota_group	sched_u.quota.__sched_group
#endif	/* !SCHED_QUOTA */

struct __sched_quota_param {
	__s32 __sched_group;
};

enum {
	sched_quota_add,
	sched_quota_remove,
	sched_quota_force_remove,
	sched_quota_set,
	sched_quota_get,
};

struct __sched_config_quota {
	__s32 op;
	union {
		struct {
			__s32 pshared;
		} add;
		struct {
			__s32 tgid;
		} remove;
		struct {
			__s32 tgid;
			__s32 quota;
			__s32 quota_peak;
		} set;
		struct {
			__s32 tgid;
		} get;
	};
	struct __sched_quota_info {
		__s32 tgid;
		__s32 quota;
		__s32 quota_peak;
		__s32 quota_sum;
	} info;
};

#define sched_quota_confsz()  sizeof(struct __sched_config_quota)

struct sched_param_ex {
	__s32 sched_priority;
	int: 32;
	union {
		struct __sched_ss_param ss;
		struct __sched_rr_param rr;
		struct __sched_tp_param tp;
		struct __sched_quota_param quota;
	} sched_u;
};

union sched_config {
	struct __sched_config_tp tp;
	struct __sched_config_quota quota;
};

#endif /* !_COBALT_UAPI_SCHED_H */
