/*
 * Copyright (C) 2009 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_POSIX_SCHED_H
#define _COBALT_POSIX_SCHED_H

#include <linux/list.h>
#include <cobalt/kernel/sched.h>
#include <xenomai/posix/syscall.h>

struct cobalt_kqueues;

struct cobalt_sched_group {
#ifdef CONFIG_XENO_OPT_SCHED_QUOTA
	struct xnsched_quota_group quota;
#endif
	struct cobalt_kqueues *kq;
	int pshared;
	struct list_head next;
};

int __cobalt_sched_weightprio(int policy,
			      const struct sched_param_ex *param_ex);

int __cobalt_sched_setconfig_np(int cpu, int policy,
				void __user *u_config,
				size_t len,
				union sched_config *(*fetch_config)
				(int policy, const void __user *u_config,
				 size_t *len),
				int (*ack_config)(int policy,
						  const union sched_config *config,
						  void __user *u_config));

ssize_t __cobalt_sched_getconfig_np(int cpu, int policy,
				    void __user *u_config,
				    size_t len,
				    union sched_config *(*fetch_config)
				    (int policy, const void __user *u_config,
				     size_t *len),
				    ssize_t (*put_config)(int policy,
							  void __user *u_config, size_t u_len,
							  const union sched_config *config,
							  size_t len));
struct xnsched_class *
cobalt_sched_policy_param(union xnsched_policy_param *param,
			  int u_policy, const struct sched_param_ex *param_ex,
			  xnticks_t *tslice_r);

COBALT_SYSCALL_DECL(sched_yield, int, (void));

COBALT_SYSCALL_DECL(sched_weightprio,
		    int, (int policy,
			  const struct sched_param_ex __user *u_param));

COBALT_SYSCALL_DECL(sched_minprio, int, (int policy));

COBALT_SYSCALL_DECL(sched_maxprio, int, (int policy));

COBALT_SYSCALL_DECL(sched_setconfig_np,
		    int, (int cpu,
			  int policy,
			  union sched_config __user *u_config,
			  size_t len));

COBALT_SYSCALL_DECL(sched_getconfig_np,
		    ssize_t, (int cpu, int policy,
			      union sched_config __user *u_config,
			      size_t len));

void cobalt_sched_cleanup(struct cobalt_kqueues *q);

void cobalt_sched_pkg_init(void);

void cobalt_sched_pkg_cleanup(void);

#endif /* !_COBALT_POSIX_SCHED_H */