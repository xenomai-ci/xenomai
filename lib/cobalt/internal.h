/*
 * Copyright (C) 2008 Philippe Gerum <rpm@xenomai.org>.
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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _LIB_COBALT_INTERNAL_H
#define _LIB_COBALT_INTERNAL_H

#include <limits.h>
#include <boilerplate/ancillaries.h>
#include <cobalt/sys/cobalt.h>
#include "current.h"

extern void *cobalt_umm_private;

extern void *cobalt_umm_shared;

static inline int cobalt_is_relaxed(void)
{
	return cobalt_get_current_mode() & XNRELAX;
}

static inline int cobalt_should_warn(void)
{
	return (cobalt_get_current_mode() & (XNRELAX|XNWARN)) == XNWARN;
}

#ifdef CONFIG_XENO_LAZY_SETSCHED
static inline int cobalt_eager_setsched(void)
{
	return cobalt_is_relaxed();
}
#else
static inline int cobalt_eager_setsched(void)
{
	return 1;
}
#endif

static inline
struct cobalt_mutex_state *mutex_get_state(struct cobalt_mutex_shadow *shadow)
{
	if (shadow->attr.pshared)
		return cobalt_umm_shared + shadow->state_offset;

	return cobalt_umm_private + shadow->state_offset;
}

static inline atomic_t *mutex_get_ownerp(struct cobalt_mutex_shadow *shadow)
{
	return &mutex_get_state(shadow)->owner;
}

void cobalt_sigshadow_install_once(void);

void cobalt_thread_init(void);

int cobalt_thread_probe(pid_t pid);

void cobalt_sched_init(void);

void cobalt_print_init(void);

void cobalt_print_init_atfork(void);

void cobalt_ticks_init(unsigned long long freq);

void cobalt_mutex_init(void);

void cobalt_default_condattr_init(void);

int cobalt_xlate_schedparam(int policy,
			    const struct sched_param_ex *param_ex,
			    struct sched_param *param);
int cobalt_init(void);

struct cobalt_featinfo;

void cobalt_arch_check_features(struct cobalt_featinfo *finfo);

extern struct sigaction __cobalt_orig_sigdebug;

extern int __cobalt_std_fifo_minpri,
	   __cobalt_std_fifo_maxpri;

extern int __cobalt_std_rr_minpri,
	   __cobalt_std_rr_maxpri;

#endif /* _LIB_COBALT_INTERNAL_H */
