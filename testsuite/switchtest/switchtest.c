/*
 * Copyright (C) 2006-2013 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <getopt.h>
#include <asm/unistd.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/uapi/fptest.h>
#include <cobalt/trace.h>
#include <rtdm/testing.h>
#include <sys/cobalt.h>
#include <xenomai/init.h>

static unsigned int nr_cpus;

#define for_each_cpu(__cpu)				\
	for (__cpu = 0; __cpu < CPU_SETSIZE; __cpu++)	\
		if (CPU_ISSET(__cpu, &__base_setup_data.cpu_affinity))

#define for_each_cpu_index(__cpu, __index)				\
	for (__cpu = 0, __index = -1; __cpu < CPU_SETSIZE; __cpu++)	\
		if (CPU_ISSET(__cpu, &__base_setup_data.cpu_affinity) && ++__index >= 0)

#if CONFIG_SMP
#define smp_sched_setaffinity(pid,len,mask) sched_setaffinity(pid,len,mask)
#else /* !CONFIG_SMP */
#define smp_sched_setaffinity(pid,len,mask) 0
#endif /* !CONFIG_SMP */

/* Thread type. */
typedef enum {
	SLEEPER = 0,
	RTK  = 1,	 /* kernel-space thread. */
	RTUP = 2,	 /* user-space real-time thread in primary mode. */
	RTUS = 3,	 /* user-space real-time thread in secondary mode. */
	RTUO = 4,	 /* user-space real-time thread oscillating
			    between primary and secondary mode. */
	SWITCHER = 8,
	FPU_STRESS = 16,
} threadtype;

typedef enum {
	AFP  = 1,	 /* arm the FPU task bit (only make sense for RTK) */
	UFPP = 2,	 /* use the FPU while in primary mode. */
	UFPS = 4	 /* use the FPU while in secondary mode. */
} fpflags;

#define TASK_SWITCH_MODES 3

enum task_switch_mode {
	TASK_SWITCH_PREVIOUS = 0,
	TASK_SWITCH_NEXT = 1,
	TASK_SWITCH_MODE = 2
};

struct cpu_tasks;

struct task_params {
	threadtype type;
	fpflags fp;
	pthread_t thread;
	struct cpu_tasks *cpu;
	struct rttst_swtest_task swt;
};

struct cpu_tasks {
	unsigned int index;
	struct task_params *tasks;
	unsigned tasks_count;
	unsigned capacity;
	unsigned fd;
	unsigned long last_switches_count;
};

static sem_t sleeper_start;
static int quiet, status;
static struct timespec start;
static pthread_mutex_t headers_lock;
static unsigned long data_lines = 21;
static unsigned freeze_on_error;
static int fp_features;
static pthread_t main_tid;

static inline unsigned stack_size(unsigned size)
{
	return size > PTHREAD_STACK_MIN ? size : PTHREAD_STACK_MIN;
}

static inline void clean_exit(int retval)
{
	status = retval;
	__STD(pthread_kill(main_tid, SIGTERM));
	for (;;)
		/* Wait for cancellation. */
		__STD(sem_wait(&sleeper_start));
}

static void timespec_substract(struct timespec *result,
			const struct timespec *lhs,
			const struct timespec *rhs)
{
	result->tv_sec = lhs->tv_sec - rhs->tv_sec;
	if (lhs->tv_nsec >= rhs->tv_nsec)
		result->tv_nsec = lhs->tv_nsec - rhs->tv_nsec;
	else {
		result->tv_sec -= 1;
		result->tv_nsec = lhs->tv_nsec + (1000000000 - rhs->tv_nsec);
	}
}

static char *task_name(char *buf, size_t sz,
		       struct cpu_tasks *cpu, unsigned task)
{
	char *basename [] = {
		[SLEEPER] = "sleeper",
		[RTK] = "rtk",
		[RTUP] = "rtup",
		[RTUS] = "rtus",
		[RTUO] = "rtuo",
		[SWITCHER] = "switcher",
		[FPU_STRESS] = "fpu_stress",
	};
	struct {
		unsigned flag;
		char *name;
	} flags [] = {
		{ .flag = AFP, .name = "fp" },
		{ .flag = UFPP, .name = "ufpp" },
		{ .flag = UFPS, .name = "ufps" },
	};
	struct task_params *param;
	unsigned pos, i;

	if (task > cpu->tasks_count)
		return "???";

	if (task == cpu->tasks_count)
		param = &cpu->tasks[task];
	else
		for (param = &cpu->tasks[0]; param->swt.index != task; param++)
			;

	pos = snprintf(buf, sz, "%s", basename[param->type]);
	for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) {
		if (!(param->fp & flags[i].flag))
			continue;

		pos += snprintf(&buf[pos],
				sz - pos, "_%s", flags[i].name);
	}

#ifdef CONFIG_SMP
	pos += snprintf(&buf[pos], sz - pos, "%u", cpu->index);
#endif /* !CONFIG_SMP */

	snprintf(&buf[pos], sz - pos, "-%u", param->swt.index);

	return buf;
}

static void handle_bad_fpreg(struct cpu_tasks *cpu, unsigned fp_val)
{
	struct rttst_swtest_error err;
	unsigned from, to;
	char buffer[64];

	if (freeze_on_error)
		xntrace_user_freeze(0, 0);

	ioctl(cpu->fd, RTTST_RTIOC_SWTEST_GET_LAST_ERROR, &err);

	if (fp_val == ~0)
		fp_val = err.fp_val;

	from = err.last_switch.from;
	to = err.last_switch.to;

	fprintf(stderr, "Error after context switch from task %d(%s) ",
		from, task_name(buffer, sizeof(buffer), cpu, from));
	fprintf(stderr, "to task %d(%s),\nFPU registers were set to %u ",
		to, task_name(buffer, sizeof(buffer), cpu, to), fp_val);
	fp_val %= 1000;
	if (fp_val < 500)
		fprintf(stderr, "(maybe task %s)\n",
			task_name(buffer, sizeof(buffer), cpu, fp_val));
	else {
		fp_val -= 500;
		if (fp_val > cpu->tasks_count)
			fprintf(stderr, "(unidentified task)\n");
		else
			fprintf(stderr, "(maybe task %s, having used fpu in "
				"kernel-space)\n",
				task_name(buffer, sizeof(buffer), cpu, fp_val));
	}

	clean_exit(EXIT_FAILURE);
}

static void display_cleanup(void *cookie)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *) cookie;
	__STD(pthread_mutex_unlock(mutex));
}

static void display_switches_count(struct cpu_tasks *cpu, struct timespec *now)
{
	static unsigned nlines = 0;
	__u32 switches_count;

	if (ioctl(cpu->fd,
		  RTTST_RTIOC_SWTEST_GET_SWITCHES_COUNT,&switches_count)) {
		perror("sleeper: ioctl(RTTST_RTIOC_SWTEST_GET_SWITCHES_COUNT)");
		clean_exit(EXIT_FAILURE);
	}

	if (switches_count &&
	    switches_count == cpu->last_switches_count) {
		fprintf(stderr, "No context switches during one second, "
			"aborting.\n");
		clean_exit(EXIT_FAILURE);
	}

	if (quiet)
		return;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(display_cleanup, &headers_lock);
	__STD(pthread_mutex_lock(&headers_lock));

	if (data_lines && (nlines++ % data_lines) == 0) {
		struct timespec diff;
		long dt;

		timespec_substract(&diff, now, &start);
		dt = diff.tv_sec;

		printf("RTT|  %.2ld:%.2ld:%.2ld\n",
		       dt / 3600, (dt / 60) % 60, dt % 60);
#ifdef CONFIG_SMP
		printf("RTH|%12s|%12s|%12s\n",
		       "---------cpu","ctx switches","-------total");
#else /* !CONFIG_SMP */
		printf("RTH|%12s|%12s\n", "ctx switches","-------total");
#endif /* !CONFIG_SMP */
	}

#ifdef CONFIG_SMP
	printf("RTD|%12u|%12lu|%12u\n", cpu->index,
	       switches_count - cpu->last_switches_count, switches_count);
#else /* !CONFIG_SMP */
	printf("RTD|%12lu|%12u\n",
	       switches_count - cpu->last_switches_count, switches_count);
#endif /* !CONFIG_SMP */

	pthread_cleanup_pop(1);
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	cpu->last_switches_count = switches_count;
}

static int printout(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	
	if (quiet < 2)
		vprintf(fmt, ap);

	va_end(ap);

	return 0;
}

#define check_fp_result(__expected)	\
	fp_regs_check(fp_features, __expected, printout)

static void _assert_primary_mode(char const *calling_func)
{
	if (cobalt_thread_mode() & (XNRELAX|XNWEAK)) {
		fprintf(stderr,
			"Switch to primary mode failed in %s.",
			calling_func);
		clean_exit(EXIT_FAILURE);
	}
}

#define assert_primary_mode() _assert_primary_mode(__func__)

static void _assert_secondary_mode(char const *calling_func)
{
	if (!(cobalt_thread_mode() & XNRELAX)) {
		fprintf(stderr,
			"Switch to secondary mode failed in %s.",
			calling_func);
		clean_exit(EXIT_FAILURE);
	}
}

#define assert_secondary_mode() _assert_secondary_mode(__func__)

static void switch_to_primary_mode(void)
{
	cobalt_thread_harden();
	assert_primary_mode();
}

static void switch_to_secondary_mode(void)
{
	cobalt_thread_relax();
	assert_secondary_mode();
}

static void switch_to_secondary_mode_by_using_linux_syscall(void)
{
	syscall(__NR_gettid);
	assert_secondary_mode();
}

#define SWITCH_FUNC_COUNT 4

static void (*switch_funcs[SWITCH_FUNC_COUNT]) (void) = {
	switch_to_secondary_mode,
	switch_to_primary_mode,
	switch_to_secondary_mode_by_using_linux_syscall,
	switch_to_primary_mode,
};

/* Switch two times: primary to secondary and back from secondary to primary */
#define MODE_SWITCHES_KERNEL 2

static void *sleeper_switcher(void *cookie)
{
	struct task_params *param = (struct task_params *) cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	struct timespec ts, last;
	int fd = param->cpu->fd;
	struct rttst_swtest_dir rtsw;
	cpu_set_t cpu_set;
	unsigned i = 1;		/* Start at 1 to avoid returning to a
				   non-existing task. */
	int ret;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (smp_sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("sleeper: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.switch_mode = 0;
	rtsw.from = param->swt.index;
	to = param->swt.index;

	ts.tv_sec = 0;
	ts.tv_nsec = 1000000;

	ret = __STD(sem_wait(&sleeper_start));
	if (ret) {
		fprintf(stderr, "sem_wait FAILED (%d)\n", errno);
		fflush(stderr);
		exit(77);
	}

	clock_gettime(CLOCK_REALTIME, &last);

	/* ioctl is not a cancellation point, but we want cancellation to be
	   allowed  when suspended in ioctl. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	for (;;) {
		struct timespec now, diff;
		unsigned expected, fp_val;
		int err;
		if (param->type == SLEEPER)
			__STD(nanosleep(&ts, NULL));

		clock_gettime(CLOCK_REALTIME, &now);

		timespec_substract(&diff, &now, &last);
		if (diff.tv_sec >= 1) {
			last = now;

			display_switches_count(param->cpu, &now);
		}

		if (tasks_count == 1)
			continue;

		switch (i % TASK_SWITCH_MODES) {
		case TASK_SWITCH_PREVIOUS:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case TASK_SWITCH_NEXT:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPS)
			fp_regs_set(fp_features, expected);
		err = ioctl(fd, RTTST_RTIOC_SWTEST_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPS) {
			fp_val = check_fp_result(expected);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}


static double dot(volatile double *a, volatile double *b, int n)
{
    int k = n - 1;
    double s = 0.0;
    for(; k >= 0; k--)
	s = s + a[k]*b[k];

    return s;
}

static void *fpu_stress(void *cookie)
{
	static volatile double a[10000], b[sizeof(a)/sizeof(a[0])];
	struct task_params *param = (struct task_params *) cookie;
	cpu_set_t cpu_set;
	unsigned i;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (smp_sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("sleeper: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	for (i = 0; i < sizeof(a)/sizeof(a[0]); i++)
		a[i] = b[i] = 3.14;

	for (;;) {
		double s = dot(a, b, sizeof(a)/sizeof(a[0]));
		if ((unsigned) (s + 0.5) != 98596) {
			fprintf(stderr, "fpu stress task failure! dot: %g\n", s);
			clean_exit(EXIT_FAILURE);
		}
		pthread_testcancel();
	}

	return NULL;
}

static void *rtup(void *cookie)
{
	struct task_params *param = (struct task_params *) cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	int err, fd = param->cpu->fd;
	struct rttst_swtest_dir rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (smp_sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtup: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.switch_mode = 0;
	rtsw.from = param->swt.index;
	to = param->swt.index;

	/* ioctl is not a cancellation point, but we want cancellation to be
	   allowed when suspended in ioctl. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	switch_to_primary_mode();

	do {
		err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % TASK_SWITCH_MODES) {
		case TASK_SWITCH_PREVIOUS:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case TASK_SWITCH_NEXT:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPP)
			fp_regs_set(fp_features, expected);
		err = ioctl(fd, RTTST_RTIOC_SWTEST_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPP) {
			fp_val = check_fp_result(expected);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

static void *rtus(void *cookie)
{
	struct task_params *param = (struct task_params *) cookie;
	unsigned to, tasks_count = param->cpu->tasks_count;
	int err, fd = param->cpu->fd;
	struct rttst_swtest_dir rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (smp_sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtus: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.switch_mode = 0;
	rtsw.from = param->swt.index;
	to = param->swt.index;

	/* ioctl is not a cancellation point, but we want cancellation to be
	   allowed when suspended in ioctl. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	switch_to_secondary_mode();

	do {
		err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % TASK_SWITCH_MODES) {
		case TASK_SWITCH_PREVIOUS:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case TASK_SWITCH_NEXT:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if (param->fp & UFPS)
			fp_regs_set(fp_features, expected);
		err = ioctl(fd, RTTST_RTIOC_SWTEST_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0);
		case -1:
			clean_exit(EXIT_FAILURE);
		}
		if (param->fp & UFPS) {
			fp_val = check_fp_result(expected);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val);
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

static void *rtuo(void *cookie)
{
	struct task_params *param = (struct task_params *) cookie;
	unsigned mode, to, tasks_count = param->cpu->tasks_count;
	int err, fd = param->cpu->fd;
	struct rttst_swtest_dir rtsw;
	cpu_set_t cpu_set;
	unsigned i = 0;

	CPU_ZERO(&cpu_set);
	CPU_SET(param->cpu->index, &cpu_set);
	if (smp_sched_setaffinity(0, sizeof(cpu_set), &cpu_set)) {
		perror("rtuo: sched_setaffinity");
		clean_exit(EXIT_FAILURE);
	}

	rtsw.switch_mode = 0;
	rtsw.from = param->swt.index;
	to = param->swt.index;

	/* ioctl is not a cancellation point, but we want cancellation to be
	   allowed when suspended in ioctl. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

	switch_to_primary_mode();
	mode = COBALT_PRIMARY;

	do {
		err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);
	} while (err == -1 && errno == EINTR);

	if (err == -1)
		return NULL;

	for (;;) {
		unsigned expected, fp_val;

		switch (i % TASK_SWITCH_MODES) {
		case TASK_SWITCH_PREVIOUS:
			/* to == from means "return to last task" */
			rtsw.to = rtsw.from;
			break;

		case TASK_SWITCH_NEXT:
			if (++to == rtsw.from)
				++to;
			if (to > tasks_count - 1)
				to = 0;
			if (to == rtsw.from)
				++to;
			rtsw.to = to;

			/* If i % 3 == 2, repeat the same switch. */
		}

		expected = rtsw.from + i * 1000;
		if ((mode == COBALT_PRIMARY && param->fp & UFPP) ||
		    (mode == COBALT_SECONDARY && param->fp & UFPS)) {
			fp_regs_set(fp_features, expected);
		}

		err = ioctl(fd, RTTST_RTIOC_SWTEST_SWITCH_TO, &rtsw);
		while (err == -1 && errno == EINTR)
			err = ioctl(fd, RTTST_RTIOC_SWTEST_PEND, &param->swt);

		/* Return to default: do not switch in syscall */
		rtsw.switch_mode = 0;

		switch (err) {
		case 0:
			break;
		case 1:
			handle_bad_fpreg(param->cpu, ~0);
		case -1:
			clean_exit(EXIT_FAILURE);
		}

		if ((mode == COBALT_PRIMARY && param->fp & UFPP) ||
		    (mode == COBALT_SECONDARY && param->fp & UFPS)) {
			fp_val = check_fp_result(expected);
			if (fp_val != expected)
				handle_bad_fpreg(param->cpu, fp_val);
		}

		/* Switch between primary and secondary mode */
		if (i % TASK_SWITCH_MODES == TASK_SWITCH_MODE) {
			uint switch_iteration = (i / TASK_SWITCH_MODES %
				(SWITCH_FUNC_COUNT + MODE_SWITCHES_KERNEL));

			if (switch_iteration < SWITCH_FUNC_COUNT) {
				switch_funcs[switch_iteration]();
			} else {
				/* Switch mode on next
				 * RTTST_RTIOC_SWTEST_SWITCH_TO syscall */
				rtsw.switch_mode = 1;
			}

			mode = !mode;
		}

		if(++i == 4000000)
			i = 0;
	}

	return NULL;
}

static int parse_arg(struct task_params *param,
		     const char *text,
		     struct cpu_tasks *cpus)
{
	struct t2f {
		const char *text;
		unsigned flag;
	};

	static struct t2f type2flags [] = {
		{ "rtk",  RTK  },
		{ "rtup", RTUP },
		{ "rtus", RTUS },
		{ "rtuo", RTUO }
	};

	static struct t2f fp2flags [] = {
		{ "_fp",   AFP	 },
		{ "_ufpp", UFPP },
		{ "_ufps", UFPS }
	};

	unsigned long cpu;
	char *cpu_end;
	unsigned i;
	int n;

	param->type = param->fp = 0;
	param->cpu = &cpus[0];

	for(i = 0; i < sizeof(type2flags)/sizeof(struct t2f); i++) {
		size_t len = strlen(type2flags[i].text);

		if(!strncmp(text, type2flags[i].text, len)) {
			param->type = type2flags[i].flag;
			text += len;
			goto fpflags;
		}
	}

	return -1;

  fpflags:
	if (*text == '\0')
		return 0;

	if (isdigit(*text))
		goto cpu_nr;

	for(i = 0; i < sizeof(fp2flags)/sizeof(struct t2f); i++) {
		size_t len = strlen(fp2flags[i].text);

		if(!strncmp(text, fp2flags[i].text, len)) {
			param->fp |= fp2flags[i].flag;
			text += len;

			goto fpflags;
		}
	}

	return -1;

  cpu_nr:
	cpu = strtoul(text, &cpu_end, 0);

	if (*cpu_end != '\0' || (cpu == ULONG_MAX && errno))
		return -1;

	param->cpu = &cpus[nr_cpus]; /* Invalid at first. */
	for_each_cpu_index(i, n)
		if (i == cpu) {
			param->cpu = &cpus[n];
			break;
		}
	
	return 0;
}

static int check_arg(const struct task_params *param, struct cpu_tasks *end_cpu)
{
	if (param->cpu > end_cpu - 1)
		return 0;

	switch (param->type) {
	case SLEEPER:
	case SWITCHER:
	case FPU_STRESS:
		break;

	case RTK:
		if (param->fp & UFPS)
			return 0;
		break;

	case RTUP:
		if (param->fp & (AFP|UFPS))
			return 0;
		break;

	case RTUS:
		if (param->fp & (AFP|UFPP))
			return 0;
		break;

	case RTUO:
		if (param->fp & AFP)
			return 0;
		break;
	default:
		return 0;
	}

	return 1;
}

static int task_create(struct cpu_tasks *cpu,
		       struct task_params *param,
		       pthread_attr_t *rt_attr)
{
	char buffer[64];
	typedef void *thread_routine(void *);
	thread_routine *task_routine [] = {
		[RTUP] = &rtup,
		[RTUS] = &rtus,
		[RTUO] = &rtuo
	};
	int err;

	switch(param->type) {
	case RTK:
		param->swt.flags = (param->fp & AFP ? RTTST_SWTEST_FPU : 0)
			| (freeze_on_error ? RTTST_SWTEST_FREEZE : 0);

		err=ioctl(cpu->fd,RTTST_RTIOC_SWTEST_CREATE_KTASK,&param->swt);
		if (err) {
			perror("ioctl(RTTST_RTIOC_SWTEST_CREATE_KTASK)");
			return -1;
		}
		break;

	case RTUP:
	case RTUS:
	case RTUO:
	case SLEEPER:
	case SWITCHER:
		param->swt.flags = 0;

		err=ioctl(cpu->fd,RTTST_RTIOC_SWTEST_REGISTER_UTASK,&param->swt);
		if (err) {
			perror("ioctl(RTTST_RTIOC_SWTEST_REGISTER_UTASK)");
			return -1;
		}
		break;

	case FPU_STRESS:
		break;

	default:
		fprintf(stderr, "Invalid task type %d. Aborting\n", param->type);
		return EINVAL;
	}

	if (param->type == RTK)
		return 0;

	if (param->type == SLEEPER || param->type == SWITCHER) {
		pthread_attr_t attr;

		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, stack_size(32768));

		err = __STD(pthread_create(&param->thread,
					   &attr,
					   sleeper_switcher,
					   param));

		pthread_attr_destroy(&attr);

		if (err)
			fprintf(stderr,"pthread_create: %s\n",strerror(err));


		return err;
	}

	if (param->type == FPU_STRESS) {
		pthread_attr_t attr;

		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, stack_size(65536));

		err = __STD(pthread_create(&param->thread,
					   &attr,
					   fpu_stress,
					   param));

		pthread_attr_destroy(&attr);

		if (err)
			fprintf(stderr,"pthread_create: %s\n",strerror(err));


		return err;
	}

	err = pthread_create(&param->thread, rt_attr,
			     task_routine[param->type], param);
	if (err) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		return err;
	}

	err = pthread_setname_np(param->thread,
				 task_name(buffer, sizeof(buffer),
					   param->cpu,param->swt.index));

	if (err)
		fprintf(stderr,"pthread_setname_np: %s\n", strerror(err));

	return err;
}

static int open_rttest(char *buf, size_t size, unsigned count)
{
	int fd, ret;

	fd = open("/dev/rtdm/switchtest", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "switchtest: cannot open /dev/rtdm/switchtest\n"
			"(modprobe xeno_switchtest?)\n");
		return -1;
	}

	ret = ioctl(fd, RTTST_RTIOC_SWTEST_SET_TASKS_COUNT, count);
	if (ret) {
		fprintf(stderr, "switchtest: ioctl: %m\n");
		return -1;
	}

	return fd;
}

const char *all_nofp [] = {
	"rtk",
	"rtk",
	"rtup",
	"rtup",
	"rtus",
	"rtus",
	"rtuo",
	"rtuo",
};

const char *all_fp [] = {
	"rtk",
	"rtk",
	"rtk_fp",
	"rtk_fp",
	"rtk_fp_ufpp",
	"rtk_fp_ufpp",
	"rtup",
	"rtup",
	"rtup_ufpp",
	"rtup_ufpp",
	"rtus",
	"rtus",
	"rtus_ufps",
	"rtus_ufps",
	"rtuo",
	"rtuo",
	"rtuo_ufpp",
	"rtuo_ufpp",
	"rtuo_ufps",
	"rtuo_ufps",
	"rtuo_ufpp_ufps",
	"rtuo_ufpp_ufps"
};

static unsigned long xatoul(const char *str)
{
	unsigned long result;
	char *endptr;

	result = strtoul(str, &endptr, 0);

	if (result == ULONG_MAX && errno == ERANGE) {
		fprintf(stderr, "Overflow while parsing %s\n", str);
		exit(EXIT_FAILURE);
	}

	if (*endptr != '\0') {
		fprintf(stderr, "Error while parsing \"%s\" as a number\n", str);
		exit(EXIT_FAILURE);
	}

	return result;
}

static void usage(FILE *fd, const char *progname)
{
	unsigned i, j;

	fprintf(fd,
		"Usage:\n"
		"%s [options] threadspec threadspec...\n"
		"Create threads of various types and attempt to switch context "
		"between these\nthreads, printing the count of context switches "
		"every second.\n\n"
		"Available options are:\n"
		"--help or -h, cause this program to print this help string and "
		"exit;\n"
		"--lines <lines> or -l <lines> print headers every <lines> "
		"lines.\n"
		"--quiet or -q, prevent this program from printing every "
		"second the count of\ncontext switches;\n"
		"--really-quiet or -Q, prevent this program from printing any output;\n"
		"--timeout <duration> or -T <duration>, limit the test duration "
		"to <duration>\nseconds;\n"
		"--nofpu or -n, disables any use of FPU instructions.\n"
		"--stress <period> or -s <period> enable a stress mode where:\n"
		"  context switches occur every <period> us;\n"
		"  a background task uses fpu (and check) fpu all the time.\n"
		"--freeze trace upon error.\n\n"
		"Each 'threadspec' specifies the characteristics of a "
		"thread to be created:\n"
		"threadspec = (rtk|rtup|rtus|rtuo)(_fp|_ufpp|_ufps)*[0-9]*\n"
		"rtk for a kernel-space real-time thread;\n"
		"rtup for a user-space real-time thread running in primary"
		" mode,\n"
		"rtus for a user-space real-time thread running in secondary"
		" mode,\n"
		"rtuo for a user-space real-time thread oscillating between"
		" primary and\nsecondary mode,\n\n"
		"_fp means that the created thread will have the XNFPU bit"
		" armed (only valid for\nrtk),\n"
		"_ufpp means that the created thread will use the FPU when in "
		"primary mode\n(invalid for rtus),\n"
		"_ufps means that the created thread will use the FPU when in "
		"secondary mode\n(invalid for rtk and rtup),\n\n"
		"[0-9]* specifies the ID of the CPU where the created thread "
		"will run, 0 if\nunspecified.\n\n"
		"Passing no 'threadspec' is equivalent to running:\n%s",
		progname, progname);

	for_each_cpu(i) {
		for (j = 0; j < sizeof(all_fp)/sizeof(char *); j++)
			fprintf(fd, " %s%d", all_fp[j], i);
	}

	fprintf(fd,
		"\n\nPassing only the --nofpu or -n argument is equivalent to "
		"running:\n%s", progname);

	for_each_cpu(i) {
		for (j = 0; j < sizeof(all_nofp)/sizeof(char *); j++)
			fprintf(fd, " %s%d", all_nofp[j], i);
	}
	fprintf(fd, "\n\n");
}

void application_usage(void)
{
	usage(stdout, get_program_name());
}

static sigjmp_buf jump;

static void illegal_instruction(int sig)
{
	signal(sig, SIG_DFL);
	siglongjmp(jump, 1);
}

/* We run the FPU check in a thread to avoid clobbering the main thread FPU
   backup area. This is important on x86, where this results on all RT threads
   FPU backup areas to be clobbered, and thus their FPU context being switched
   systematically (and the case where FPU has never been used not to be tested). */
static void *check_fpu_thread(void *cookie)
{
	int check;

	/* Check if fp routines are dummy or if hw fpu is not supported. */
	if (quiet < 2)
		fprintf(stderr, "== Testing FPU check routines...\n");
	if(sigsetjmp(jump, 1)) {
		if (quiet < 2)
			fprintf(stderr,
			"== Hardware FPU not available on your board"
			" or not enabled in Linux kernel\n== configuration:"
			" skipping FPU switches tests.\n");
		return NULL;
	}
	signal(SIGILL, illegal_instruction);
	fp_regs_set(fp_features, 1);
	check = check_fp_result(2);
	signal(SIGILL, SIG_DFL);
	if (check != 1) {
		if (quiet < 2)
			fprintf(stderr,
				"== FPU check routines: unimplemented, "
				"skipping FPU switches tests.\n");
		return NULL;
	}

	if (quiet < 2)
		fprintf(stderr, "== FPU check routines: OK.\n");

	return (void *) 1;
}

static int check_fpu(void)
{
	pthread_t tid;
	void *status;
	int err;

	err = __STD(pthread_create(&tid, NULL, check_fpu_thread, NULL));
	if (err) {
		fprintf(stderr, "pthread_create: %s\n", strerror(err));
		exit(EXIT_FAILURE);
	}

	err = pthread_join(tid, &status);
	if (err) {
		fprintf(stderr, "pthread_join: %s\n", strerror(err));
		exit(EXIT_FAILURE);
	}

	return (long) status;
}

int main(int argc, const char *argv[])
{
	unsigned i, j, n, use_fp = 1, stress = 0;
	pthread_attr_t rt_attr;
	const char *progname = argv[0];
	struct cpu_tasks *cpus;
	struct sched_param sp;
	char devname[RTDM_MAX_DEVNAME_LEN+1];
	sigset_t mask;
	int sig;

	status = EXIT_SUCCESS;
	main_tid = pthread_self();

	/* Initializations. */
	if (__STD(sem_init(&sleeper_start, 0, 0))) {
		perror("sem_init");
		exit(EXIT_FAILURE);
	}

#if CONFIG_SMP
	nr_cpus = CPU_COUNT(&__base_setup_data.cpu_affinity);
	if (nr_cpus == 0) {
		nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
		if (nr_cpus == -1) {
			fprintf(stderr,
				"Error %d while getting the number of cpus (%s)\n",
				errno,
				strerror(errno));
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < nr_cpus; i++)
			CPU_SET(i, &__base_setup_data.cpu_affinity);
	}
#else /* !CONFIG_SMP */
	nr_cpus = 1;
	CPU_SET(0, &__base_setup_data.cpu_affinity);
#endif /* !CONFIG_SMP */

	fp_features = cobalt_fp_detect();

	/* Parse command line options. */
	opterr = 0;
	for (;;) {
		static struct option long_options[] = {
			{ "freeze",  0, NULL, 'f' },
			{ "help",    0, NULL, 'h' },
			{ "lines",   1, NULL, 'l' },
			{ "nofpu",   0, NULL, 'n' },
			{ "quiet",   0, NULL, 'q' },
			{ "really-quiet", 0, NULL, 'Q' },
			{ "stress",  1, NULL, 's' },
			{ "timeout", 1, NULL, 'T' },
			{ NULL,      0, NULL, 0   }
		};
		int i = 0;
		int c = getopt_long(argc, (char *const *) argv, "fhl:nqQs:T:",
				    long_options, &i);

		if (c == -1)
			break;

		switch(c) {
		case 'f':
			freeze_on_error = 1;
			break;

		case 'h':
			usage(stdout, progname);
			exit(EXIT_SUCCESS);

		case 'l':
			data_lines = xatoul(optarg);
			break;

		case 'n':
			use_fp = 0;
			break;

		case 'q':
			quiet = 1;
			break;

		case 'Q':
			quiet = 2;
			break;

		case 's':
			stress = xatoul(optarg);
			break;

		case 'T':
			alarm(xatoul(optarg));
			break;

		case '?':
			usage(stderr, progname);
			fprintf(stderr, "%s: Invalid option.\n", argv[optind-1]);
			exit(EXIT_FAILURE);

		case ':':
			usage(stderr, progname);
			fprintf(stderr, "Missing argument of option %s.\n",
				argv[optind-1]);
			exit(EXIT_FAILURE);
		}
	}

	if (setvbuf(stdout, NULL, _IOLBF, 0)) {
		perror("setvbuf");
		exit(EXIT_FAILURE);
	}

	/* If no argument was passed (or only -n), replace argc and argv with
	   default values, given by all_fp or all_nofp depending on the presence
	   of the -n flag. */
	if (optind == argc) {
		const char **all;
		char buffer[32];
		unsigned count;

		if (use_fp)
			use_fp = check_fpu();

		if (use_fp) {
			all = all_fp;
			count = sizeof(all_fp)/sizeof(char *);
		} else {
			all = all_nofp;
			count = sizeof(all_nofp)/sizeof(char *);
		}

		argc = count * nr_cpus + 1;
		argv = (const char **) malloc(argc * sizeof(char *));
		argv[0] = progname;
		for_each_cpu_index(i, n) {
			for (j = 0; j < count; j++) {
				snprintf(buffer,
					 sizeof(buffer),
					 "%s%d",
					 all[j],
					 i);
				argv[n * count + j + 1] = strdup(buffer);
			}
		}

		optind = 1;
	}

	cpus = malloc(sizeof(*cpus) * nr_cpus);
	if (!cpus) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	for_each_cpu_index(i, n) {
		size_t size;
		cpus[n].fd = -1;
		cpus[n].index = i;
		cpus[n].capacity = 2;
		size = cpus[n].capacity * sizeof(struct task_params);
		cpus[n].tasks_count = 1;
		cpus[n].tasks = (struct task_params *) malloc(size);
		cpus[n].last_switches_count = 0;

		if (!cpus[n].tasks) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		cpus[n].tasks[0].type = stress ? SWITCHER : SLEEPER;
		cpus[n].tasks[0].fp = use_fp ? UFPS : 0;
		cpus[n].tasks[0].cpu = &cpus[n];
		cpus[n].tasks[0].thread = 0;
		cpus[n].tasks[0].swt.index = cpus[n].tasks[0].swt.flags = 0;
	}

	/* Parse arguments and build data structures. */
	for(i = optind; i < argc; i++) {
		struct task_params params;
		struct cpu_tasks *cpu;

		if(parse_arg(&params, argv[i], cpus)) {
			usage(stderr, progname);
			fprintf(stderr, "Unable to parse %s as a thread type. "
				"Aborting.\n", argv[i]);
			exit(EXIT_FAILURE);
		}

		if (!check_arg(&params, &cpus[nr_cpus])) {
			usage(stderr, progname);
			fprintf(stderr,
				"Invalid parameters %s. Aborting\n",
				argv[i]);
			exit(EXIT_FAILURE);
		}

		if (!use_fp && params.fp) {
			usage(stderr, progname);
			fprintf(stderr,
				"%s is invalid because FPU is disabled"
				" (option -n passed).\n", argv[i]);
			exit(EXIT_FAILURE);
		}

		cpu = params.cpu;
		if(++cpu->tasks_count > cpu->capacity) {
			size_t size;
			cpu->capacity += cpu->capacity / 2;
			size = cpu->capacity * sizeof(struct task_params);
			cpu->tasks =
				(struct task_params *) realloc(cpu->tasks, size);
			if (!cpu->tasks) {
				perror("realloc");
				exit(EXIT_FAILURE);
			}
		}

		params.thread = 0;
		params.swt.index = params.swt.flags = 0;
		cpu->tasks[cpu->tasks_count - 1] = params;
	}

	if (stress)
		for_each_cpu_index(i, n) {
			struct task_params params;
			struct cpu_tasks *cpu = &cpus[n];

			if (cpu->tasks_count + 1 > cpu->capacity) {
				size_t size;
				cpu->capacity += cpu->capacity / 2;
				size = cpu->capacity * sizeof(struct task_params);
				cpu->tasks = realloc(cpu->tasks, size);
				if (!cpu->tasks) {
					perror("realloc");
					exit(EXIT_FAILURE);
				}
			}

			params.type = FPU_STRESS;
			params.fp = UFPS;
			params.cpu = cpu;
			params.thread = 0;
			params.swt.index = cpu->tasks_count;
			params.swt.flags = 0;
			cpu->tasks[cpu->tasks_count] = params;
		}

	/* For best compatibility with both LinuxThreads and NPTL, block the
	   termination signals on all threads. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &mask, NULL);

	__STD(pthread_mutex_init(&headers_lock, NULL));

	/* Prepare attributes for real-time tasks. */
	pthread_attr_init(&rt_attr);
	pthread_attr_setinheritsched(&rt_attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(&rt_attr, SCHED_FIFO);
	sp.sched_priority = 1;
	pthread_attr_setschedparam(&rt_attr, &sp);

	if (quiet < 2)
		printf("== Threads:");

	/* Create and register all tasks. */
	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];
		char buffer[64];

		cpu->fd = open_rttest(devname,sizeof(devname),cpu->tasks_count);

		if (cpu->fd == -1)
			goto failure;

		if (ioctl(cpu->fd, RTTST_RTIOC_SWTEST_SET_CPU, i)) {
			perror("ioctl(RTTST_RTIOC_SWTEST_SET_CPU)");
			goto failure;
		}

		if (stress &&
		    ioctl(cpu->fd, RTTST_RTIOC_SWTEST_SET_PAUSE, stress)) {
			perror("ioctl(RTTST_RTIOC_SWTEST_SET_PAUSE)");
			goto failure;
		}

		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];
			if (task_create(cpu, param, &rt_attr)) {
			  failure:
				status = EXIT_FAILURE;
				goto cleanup;
			}
			if (quiet < 2)
				printf(" %s",
				       task_name(buffer, sizeof(buffer),
						 param->cpu, param->swt.index));
		}
	}
	if (quiet < 2)
		printf("\n");

	clock_gettime(CLOCK_REALTIME, &start);

	/* Start the sleeper tasks. */
	for (i = 0; i < nr_cpus; i ++)
		__STD(sem_post(&sleeper_start));

	/* Wait for interruption. */
	__STD(sigwait(&mask, &sig));

	/* Allow a second Ctrl-C in case of lockup. */
	pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

	/* Cleanup. */
  cleanup:
	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];

		/* kill the user-space tasks. */
		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];

			if (param->type != RTK && param->thread)
				pthread_cancel(param->thread);
		}
	}

	for_each_cpu_index(i, n) {
		struct cpu_tasks *cpu = &cpus[n];

		/* join the user-space tasks. */
		for (j = 0; j < cpu->tasks_count + !!stress; j++) {
			struct task_params *param = &cpu->tasks[j];

			if (param->type != RTK && param->thread)
				pthread_join(param->thread, NULL);
		}

		if (cpu->fd != -1) {
			struct timespec now;

			clock_gettime(CLOCK_REALTIME, &now);

			if (quiet == 1)
				quiet = 0;
			display_switches_count(cpu, &now);

			/* Kill the kernel-space tasks. */
			close(cpu->fd);
		}
		free(cpu->tasks);
	}
	free(cpus);
	__STD(sem_destroy(&sleeper_start));
	__STD(pthread_mutex_destroy(&headers_lock));

	return status;
}
