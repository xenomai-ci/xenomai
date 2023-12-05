/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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

#include <sys/types.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <malloc.h>

#include "boilerplate/ancillaries.h"
#include "boilerplate/lock.h"
#include "boilerplate/signal.h"

static pthread_key_t btkey;

static struct backtrace_data main_btd = {
	.name = "main",
};

void __debug(const char *name, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__printout(name, NULL, fmt, ap);
	va_end(ap);
}

void backtrace_log(int retval, const char *fn,
		   const char *file, int lineno)
{
	struct backtrace_data *btd;
	struct error_frame *ef;
	int state;

	btd = pthread_getspecific(btkey);
	if (btd == NULL)
		btd = &main_btd;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &state);

	ef = malloc(sizeof(*ef));
	if (ef == NULL)
		goto out;

	ef->retval = retval;
	ef->lineno = lineno;
	ef->fn = fn;
	ef->file = file;

	write_lock(&btd->lock);

	if (btd->inner == NULL)
		/* Fire the hook for the inner trace. */
		error_hook(ef);

	ef->next = btd->inner;
	btd->inner = ef;

	write_unlock(&btd->lock);
out:
	pthread_setcanceltype(state, NULL);
}

static void flush_backtrace(struct backtrace_data *btd)
{
	struct error_frame *ef, *nef;
	int state;

	/* Locking order must be __printlock, then btlock. */

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &state);

	write_lock(&btd->lock);

	for (ef = btd->inner; ef; ef = nef) {
		nef = ef->next;
		free(ef);
	}

	btd->inner = NULL;
	write_unlock(&btd->lock);

	pthread_setcanceltype(state, NULL);
}

void backtrace_init_context(struct backtrace_data *btd,
			    const char *name)
{
	__RT(pthread_mutex_init(&btd->lock, NULL));
	btd->inner = NULL;
	btd->name = name ?: "<anonymous>";
	pthread_setspecific(btkey, btd);
}

void backtrace_destroy_context(struct backtrace_data *btd)
{
	flush_backtrace(btd);
	__RT(pthread_mutex_destroy(&btd->lock));
}

static const char *dashes = "------------------------------------------------------------------------------";

void backtrace_dump(struct backtrace_data *btd)
{
	struct error_frame *ef;
	FILE *tracefp = stderr;
	int n = 0;

	if (btd == NULL)
		btd = &main_btd;

	SIGSAFE_LOCK_ENTRY(&__printlock);

	if (btd->inner == NULL)
		goto no_error;

	fprintf(tracefp,
		"%s\n[ ERROR BACKTRACE: thread %s ]\n\n",
		dashes, btd->name);

	for (ef = btd->inner; ef; ef = ef->next, n++)
		fprintf(tracefp, "%s #%-2d %s in %s(), %s:%d\n",
			ef->next ? "  " : "=>",
			n, symerror(ef->retval),
			ef->fn, ef->file, ef->lineno);

	fputs(dashes, tracefp);
	fputc('\n', tracefp);
	fflush(tracefp);
	flush_backtrace(btd);

no_error:
	SIGSAFE_LOCK_EXIT(&__printlock);
}

void backtrace_check(void)
{
	struct backtrace_data *btd;

	btd = pthread_getspecific(btkey);
	if (btd == NULL)
		btd = &main_btd;

	backtrace_dump(btd);
}

char *__get_error_buf(size_t *sizep)
{
	struct backtrace_data *btd;

	btd = pthread_getspecific(btkey);
	if (btd == NULL)
		btd = &main_btd;

	*sizep = sizeof(btd->eundef);

	return btd->eundef;
}

void debug_init(void)
{
	__RT(pthread_mutex_init(&main_btd.lock, NULL));
	pthread_key_create(&btkey, NULL);
}
