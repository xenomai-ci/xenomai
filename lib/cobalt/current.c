/*
 * Copyright (C) 2009 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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

#include <string.h>
#include <pthread.h>
#include <asm/xenomai/syscall.h>
#include <boilerplate/list.h>
#include "current.h"
#include "internal.h"

static DEFINE_PRIVATE_LIST(tsd_hooks);

#ifdef HAVE_TLS

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
xnhandle_t cobalt_current = XN_NO_HANDLE;

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct xnthread_user_window *cobalt_current_window;

static inline void __cobalt_set_tsd(xnhandle_t current, __u32 u_winoff)
{
	struct xnthread_user_window *window;

	cobalt_current = current;
	window = cobalt_umm_shared + u_winoff;
	cobalt_current_window = window;
	cobalt_commit_memory(cobalt_current_window);
}

static inline void __cobalt_clear_tsd(void)
{
	cobalt_current = XN_NO_HANDLE;
	cobalt_current_window = NULL;
}

static void init_current_keys(void)
{
	cobalt_current = XN_NO_HANDLE;
}

#else /* !HAVE_TLS */

pthread_key_t cobalt_current_window_key;
pthread_key_t cobalt_current_key;

static inline void __cobalt_set_tsd(xnhandle_t current,
				    __u32 u_winoff)
{
	struct xnthread_user_window *window;

	current = (current != XN_NO_HANDLE ? current : (xnhandle_t)0);
	pthread_setspecific(cobalt_current_key, (void *)(uintptr_t)current);

	window = cobalt_umm_shared + u_winoff;
	pthread_setspecific(cobalt_current_window_key, window);
	cobalt_commit_memory(window);
}

static inline void __cobalt_clear_tsd(void)
{
	pthread_setspecific(cobalt_current_key, NULL);
	pthread_setspecific(cobalt_current_window_key, NULL);
}

static void init_current_keys(void)
{
	int ret;

	ret = pthread_key_create(&cobalt_current_key, NULL);
	if (ret)
		goto fail;

	ret = pthread_key_create(&cobalt_current_window_key, NULL);
	if (ret == 0)
		return;
fail:
	early_panic("error creating TSD key: %s", strerror(ret));
}

#endif /* !HAVE_TLS */

void cobalt_clear_tsd(void)
{
	struct cobalt_tsd_hook *th;

	if (cobalt_get_current() == XN_NO_HANDLE)
		return;

	__cobalt_clear_tsd();

	if (!pvlist_empty(&tsd_hooks)) {
		pvlist_for_each_entry(th, &tsd_hooks, next)
			th->delete_tsd();
	}
}

xnhandle_t cobalt_get_current_slow(void)
{
	xnhandle_t current;
	int err;

	err = XENOMAI_SYSCALL1(sc_cobalt_get_current, &current);

	return err ? XN_NO_HANDLE : current;
}

void cobalt_set_tsd(__u32 u_winoff)
{
	struct cobalt_tsd_hook *th;
	xnhandle_t current;
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_get_current, &current);
	if (ret)
		panic("cannot retrieve current handle: %s", strerror(-ret));

	__cobalt_set_tsd(current, u_winoff);

	if (!pvlist_empty(&tsd_hooks)) {
		pvlist_for_each_entry(th, &tsd_hooks, next)
			th->create_tsd();
	}
}

void cobalt_init_current_keys(void)
{
	static pthread_once_t cobalt_init_current_keys_once = PTHREAD_ONCE_INIT;
	pthread_once(&cobalt_init_current_keys_once, init_current_keys);
}

void cobalt_register_tsd_hook(struct cobalt_tsd_hook *th)
{
	/*
	 * CAUTION: we assume inherently mt-safe conditions. Unless
	 * multiple dlopen() ends up loading extension libs
	 * concurrently, we should be ok.
	 */
	pvlist_append(&th->next, &tsd_hooks);
}
