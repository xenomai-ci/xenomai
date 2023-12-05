/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
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

#include <xeno_config.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <boilerplate/signal.h>

#include "cobalt/internal.h"
#include "internal.h"

sigset_t trank_sigperiod_set;

#ifdef HAVE_TLS

__thread __attribute__ ((tls_model (CONFIG_XENO_TLS_MODEL)))
struct trank_context trank_context;

static void trank_init_context(void)
{
	memset(&trank_context, 0, sizeof(trank_context));
}

static inline void trank_destroy_context(void)
{
	/* nop */
}

#else /* !HAVE_TLS */

#include <malloc.h>

pthread_key_t trank_context_key;

static void trank_init_context(void)
{
	struct trank_context *tc;

	tc = malloc(sizeof(*tc));
	if (tc == NULL)
		early_panic("error creating TSD: %s", strerror(ENOMEM));
		
	memset(tc, 0, sizeof(*tc));
	pthread_setspecific(trank_context_key, tc);
}

static void __trank_destroy_context(void *p)
{
	free(p);
}

static void trank_destroy_context(void)
{
	struct trank_context *tc;

	tc = pthread_getspecific(trank_context_key);
	if (tc) {
		pthread_setspecific(trank_context_key, NULL);
		__trank_destroy_context(tc);
	}
}

#endif /* !HAVE_TLS */

static struct cobalt_tsd_hook tsd_hook = {
	.create_tsd = trank_init_context,
	.delete_tsd = trank_destroy_context,
};

int trank_init_interface(void)
{
#ifndef HAVE_TLS
	int ret;

	ret = pthread_key_create(&trank_context_key, __trank_destroy_context);
	if (ret)
		early_panic("error creating TSD key: %s", strerror(ret));
#endif
	sigaddset(&trank_sigperiod_set, SIGPERIOD);
	cobalt_register_tsd_hook(&tsd_hook);

	return 0;
}
