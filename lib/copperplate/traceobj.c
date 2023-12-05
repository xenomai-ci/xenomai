/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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

#include <stdio.h>
#include <stdlib.h>
#include "boilerplate/lock.h"
#include "copperplate/traceobj.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "xenomai/init.h"
#include "internal.h"
#ifdef CONFIG_XENO_VALGRIND_API
#include <valgrind/valgrind.h>
static inline int valgrind_detected(void)
{
	return RUNNING_ON_VALGRIND;
}
#else
static inline int valgrind_detected(void)
{
	return 0;
}
#endif

struct tracemark {
	const char *file;
	int line;
	int mark;
};

int traceobj_init(struct traceobj *trobj, const char *label, int nr_marks)
{
	pthread_mutexattr_t mattr;
	pthread_condattr_t cattr;
	int ret;

	pthread_mutexattr_init(&mattr);
	pthread_mutexattr_settype(&mattr, mutex_type_attribute);
	pthread_mutexattr_setprotocol(&mattr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_mutex_init(&trobj->lock, &mattr)));
	pthread_mutexattr_destroy(&mattr);
	if (ret)
		return ret;

	pthread_condattr_init(&cattr);
	pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_PRIVATE);
	ret = __bt(-__RT(pthread_cond_init(&trobj->join, &cattr)));
	pthread_condattr_destroy(&cattr);
	if (ret) {
		__RT(pthread_mutex_destroy(&trobj->lock));
		return ret;
	}

	/*
	 * We make sure not to unblock from threadobj_join() until at
	 * least one thread has called trace_enter() for this trace
	 * object.
	 */
	trobj->nr_threads = -1;

	trobj->label = label;
	trobj->nr_marks = nr_marks;
	trobj->cur_mark = 0;

	if (nr_marks > 0) {
		trobj->marks = malloc(sizeof(struct tracemark) * nr_marks);
		if (trobj->marks == NULL)
			panic("cannot allocate mark table for tracing");
	}

	return 0;
}

static void compare_marks(struct traceobj *trobj, int tseq[], int nr_seq) /* lock held */
{
	int mark;

	for (mark = 0; mark < trobj->cur_mark || mark < nr_seq; mark++) {
		if (mark >= trobj->cur_mark) {
			fprintf(stderr, " <missing mark> |  [%d] expected\n",
				tseq[mark]);
		} else if (mark < nr_seq)
			__RT(fprintf(stderr, "at %s:%d  |  [%d] should be [%d]\n",
				     trobj->marks[mark].file,
				     trobj->marks[mark].line,
				     trobj->marks[mark].mark,
				     tseq[mark]));
		else
			__RT(fprintf(stderr, "at %s:%d  |  unexpected [%d]\n",
				     trobj->marks[mark].file,
				     trobj->marks[mark].line,
				     trobj->marks[mark].mark));
	}

	fflush(stderr);
}

void traceobj_verify(struct traceobj *trobj, int tseq[], int nr_seq)
{
	int end_mark, mark, state;
	struct service svc;

	CANCEL_DEFER(svc);

	read_lock_safe(&trobj->lock, state);

	if (nr_seq > trobj->nr_marks)
		goto fail;

	end_mark = trobj->cur_mark;
	if (end_mark == 0) {
		read_unlock_safe(&trobj->lock, state);
		panic("no mark defined");
	}

	if (end_mark != nr_seq)
		goto fail;

	for (mark = 0; mark < end_mark; mark++) {
		if (trobj->marks[mark].mark != tseq[mark])
			goto fail;
	}
out:
	read_unlock_safe(&trobj->lock, state);

	CANCEL_RESTORE(svc);

	return;

fail:
	if (valgrind_detected()) {
		warning("valgrind detected: ignoring sequence mismatch");
		goto out;
	}

	warning("mismatching execution sequence detected");
	compare_marks(trobj, tseq, nr_seq);
	read_unlock_safe(&trobj->lock, state);

	CANCEL_RESTORE(svc);

#ifdef CONFIG_XENO_MERCURY
	/*
	 * The Mercury core does not force any affinity, which may
	 * lead to wrong results with some unit tests checking strict
	 * ordering of operations. Tell the user about this. Normally,
	 * such unit tests on Mercury should be pinned on a single CPU
	 * using --cpu-affinity.
	 */
	if (CPU_COUNT(&__base_setup_data.cpu_affinity) == 0)
		warning("NOTE: --cpu-affinity option was not given - this might explain?");
#endif
#ifndef CONFIG_XENO_ASYNC_CANCEL
	/*
	 * Lack of async cancellation support might also explain why
	 * some tests have failed.
	 */
	warning("NOTE: --disable-async-cancel option was given - this might explain?");
#endif
	exit(5);
}

void traceobj_destroy(struct traceobj *trobj)
{
	free(trobj->marks);
	__RT(pthread_mutex_destroy(&trobj->lock));
}

static void dump_marks(struct traceobj *trobj) /* lock held */
{
	int mark;

	for (mark = 0; mark < trobj->cur_mark; mark++)
		fprintf(stderr, "[%d] at %s:%d\n",
			trobj->marks[mark].mark,
			trobj->marks[mark].file,
			trobj->marks[mark].line);

	fflush(stderr);
}

static void dump_marks_on_error(struct traceobj *trobj)
{
	struct service svc;

	CANCEL_DEFER(svc);

	read_lock(&trobj->lock);
	dump_marks(trobj);
	read_unlock(&trobj->lock);

	CANCEL_RESTORE(svc);
}

void __traceobj_assert_failed(struct traceobj *trobj,
			      const char *file, int line, const char *cond)
{
	dump_marks_on_error(trobj);
	panic("trace assertion failed:\n              %s:%d => \"%s\"", file, line, cond);
}

void __traceobj_check_abort(struct traceobj *trobj,
			    const char *file, int line,
			    int received, int expected)
{
	dump_marks_on_error(trobj);
	panic("wrong return status:\n              %s:%d => %s (want %s)", file, line,
	      symerror(received), symerror(expected));
}

void __traceobj_check_warn(struct traceobj *trobj,
			   const char *file, int line,
			   int received, int expected)
{
	dump_marks_on_error(trobj);
	warning("wrong return status:\n              %s:%d => %s (want %s)", file, line,
		symerror(received), symerror(expected));
}

void __traceobj_mark(struct traceobj *trobj,
		     const char *file, int line, int mark)
{
	struct tracemark *tmk;
	struct service svc;
	int cur_mark;

	CANCEL_DEFER(svc);

	pthread_testcancel();
	push_cleanup_lock(&trobj->lock);
	write_lock(&trobj->lock);

	cur_mark = trobj->cur_mark;
	if (cur_mark >= trobj->nr_marks) {
		dump_marks(trobj);
		panic("too many marks: [%d] at %s:%d", mark, file, line);
	}

	tmk = trobj->marks + cur_mark;
	tmk->file = file;
	tmk->line = line;
	tmk->mark = mark;
	trobj->cur_mark++;

	write_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);

	CANCEL_RESTORE(svc);
}

void traceobj_enter(struct traceobj *trobj)
{
	struct threadobj *current = threadobj_current();
	struct service svc;

	if (current)
		current->tracer = trobj;

	CANCEL_DEFER(svc);

	write_lock_nocancel(&trobj->lock);

	if (++trobj->nr_threads == 0)
		trobj->nr_threads = 1;

	write_unlock(&trobj->lock);

	CANCEL_RESTORE(svc);
}

/* May be directly called from finalizer. */
void traceobj_unwind(struct traceobj *trobj)
{
	struct service svc;
	int state;

	CANCEL_DEFER(svc);

	write_lock_safe(&trobj->lock, state);

	if (--trobj->nr_threads <= 0)
		threadobj_cond_signal(&trobj->join);

	write_unlock_safe(&trobj->lock, state);

	CANCEL_RESTORE(svc);
}

void traceobj_exit(struct traceobj *trobj)
{
	struct threadobj *current = threadobj_current();

	if (current)
		current->tracer = NULL;

	traceobj_unwind(trobj);
}

void traceobj_join(struct traceobj *trobj)
{
	struct service svc;

	CANCEL_DEFER(svc);

	push_cleanup_lock(&trobj->lock);
	read_lock(&trobj->lock);

	while (trobj->nr_threads < 0 || trobj->nr_threads > 0)
		threadobj_cond_wait(&trobj->join, &trobj->lock);

	read_unlock(&trobj->lock);
	pop_cleanup_lock(&trobj->lock);

	CANCEL_RESTORE(svc);
}
