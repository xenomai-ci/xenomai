/*
 * gdb test.
 *
 * Copyright (C) Siemens AG, 2015-2019
 *
 * Authors.
 *  Jan Kiszka <jan.kiszka@siemens.com>
 *
 * Released under the terms of GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <xeno_config.h>
#include <smokey/smokey.h>
#include "lib/cobalt/current.h"

static void send_command(const char *string, int wait_for_prompt);

static void check_inner(const char *fn, int line, const char *msg,
			int status, int expected)
{
	if (status == expected)
		return;

	rt_print_flush_buffers();
	fprintf(stderr, "FAILURE %s:%d: %s returned %d instead of %d - %s\n",
		fn, line, msg, status, expected, strerror(status));
	send_command("q\n", 0);
	exit(EXIT_FAILURE);
}

#define check(msg, status, expected) ({					\
	int __status = status;						\
	check_inner(__FUNCTION__, __LINE__, msg, __status, expected);	\
	__status;							\
})

#define check_no_error(msg, status) ({					\
	int __status = status;						\
	check_inner(__func__, __LINE__, msg,				\
		    __status < 0 ? errno : __status, 0);		\
	__status;							\
})

smokey_test_plugin(gdb,
		   SMOKEY_ARGLIST(
			   SMOKEY_BOOL(run_target),
		   ),
		   "Check gdb-based debugging of application."
);

#ifdef HAVE_FORK
#define do_fork fork
#else
#define do_fork vfork
#endif

static int pipe_in[2], pipe_out[2];
static volatile unsigned long long thread_loops, expected_loops;
static volatile int primary_mode;
static volatile int terminate;
static int child_terminated;
static sem_t kickoff_lo;

static void handle_sigchld(int signum)
{
	int status;

	wait(&status);
	if (WEXITSTATUS(status) == ESRCH)
		smokey_note("gdb: skipped (gdb not available)");
	else
		check("gdb terminated unexpectedly", 0, 1);
	child_terminated = 1;
	close(pipe_out[0]);
}

static void wait_for_pattern(const char *string)
{
	const char *match = string;
	char c;

	while (*match != 0 && read(pipe_out[0], &c, 1) == 1) {
		if (*match == c)
			match++;
		else
			match = string;
		if (smokey_verbose_mode > 2)
			putc(c, stdout);
	}
}

static void send_command(const char *string, int wait_for_prompt)
{
	int ret;

	ret = write(pipe_in[1], string, strlen(string));
	check("write(pipe_in)", ret, strlen(string));
	if (wait_for_prompt)
		wait_for_pattern("(gdb) ");
}

static void check_output(const char *fn, int line, const char *expression,
			 const char *pattern)
{
	char *read_string = malloc(strlen(pattern) + 1);
	const char *match = pattern;
	int pos = 0;

	while (*match != 0 && read(pipe_out[0], &read_string[pos], 1) == 1) {
		if (*match != read_string[pos]) {
			rt_print_flush_buffers();
			read_string[pos + 1] = 0;
			fprintf(stderr, "FAILURE %s:%d: checking expression "
				"\"%s\", expected \"%s\", found \"%s\"\n",
				fn, line, expression, pattern, read_string);
			send_command("q\n", 0);
			exit(EXIT_FAILURE);
		}
		if (smokey_verbose_mode > 2)
			putc(read_string[pos], stdout);
		match++;
		pos++;
	}
	free(read_string);
}

#define eval_expression(expression, expected_value)			\
	eval_expression_inner(__FUNCTION__, __LINE__, expression,	\
			      expected_value)

static void eval_expression_inner(const char *fn, int line,
				  const char *expression,
				  const char *expected_value)
{
	int ret;

	ret = write(pipe_in[1], "p ", 2);
	check("write(pipe_in)", ret, 2);
	ret = write(pipe_in[1], expression, strlen(expression));
	check("write(pipe_in)", ret, strlen(expression));
	send_command("\n", 0);

	check_output(fn, line, expression, "$");
	wait_for_pattern(" = ");
	check_output(fn, line, expression, expected_value);
	check_output(fn, line, expression, "\n(gdb) ");
}

static void __attribute__((noinline)) spin_lo_prio(void)
{
	int err;

	err = sem_wait(&kickoff_lo);
	check_no_error("sem_wait", err);

	while (!terminate)
		thread_loops++;
}

static void __attribute__((noinline)) breakpoint_target(void)
{
	asm volatile("" ::: "memory");
}

static void *thread_hi_func(void *arg)
{
	struct timespec ts = {0, 1000000};
	int err;

	pthread_setname_np(pthread_self(), "hi-thread");

	err = sem_post(&kickoff_lo);
	check_no_error("sem_post", err);

	nanosleep(&ts, NULL);

	/* 1st breakpoint: synchronous stop */
	expected_loops = thread_loops;
	breakpoint_target();

	/* 2nd bp: synchronous continue */
	expected_loops = thread_loops;
	breakpoint_target();
	terminate = 1;

	return NULL;
}

static int run_gdb(struct smokey_test *t, int argc, char *const argv[])
{
	struct sched_param params;
	pthread_t thread_hi;
	pthread_attr_t attr;
	char run_param[32];
	cpu_set_t cpu_set;
	pid_t childpid;
	int status;
	int err;

	smokey_parse_args(t, argc, argv);

	if (SMOKEY_ARG_ISSET(gdb, run_target) &&
            SMOKEY_ARG_BOOL(gdb, run_target)) {
		/* we are the to-be-debugged target */

		CPU_ZERO(&cpu_set);
		CPU_SET(0, &cpu_set);
		err = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set),
					     &cpu_set);
		check_no_error("pthread_setaffinity_np", err);
		params.sched_priority = 1;
		err = pthread_setschedparam(pthread_self(), SCHED_FIFO,
					    &params);
		check_no_error("pthread_setschedparam", err);

		check("is primary", cobalt_get_current_mode() & XNRELAX, 0);
		breakpoint_target();
		primary_mode = !(cobalt_get_current_mode() & XNRELAX);
		breakpoint_target();

		err = sem_init(&kickoff_lo, 0, 0);
		check_no_error("sem_init", err);

		err = pthread_attr_init(&attr);
		check_no_error("pthread_attr_init", err);
		err = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
		check_no_error("pthread_attr_setinheritsched", err);
		err = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
		check_no_error("pthread_attr_setschedpolicy", err);
		err = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set), &cpu_set);
		check_no_error("pthread_attr_setaffinity_np", err);

		params.sched_priority = 2;
		err = pthread_attr_setschedparam(&attr, &params);
		check_no_error("pthread_attr_setschedparam", err);
		err = pthread_create(&thread_hi, &attr, thread_hi_func, NULL);
		check_no_error("pthread_create", err);

		spin_lo_prio();

		pthread_join(thread_hi, NULL);
	} else {
		/* we are the gdb controller */

		signal(SIGCHLD, handle_sigchld);

		err = pipe(pipe_in);
		check_no_error("pipe_in", err);

		err = pipe(pipe_out);
		check_no_error("pipe_out", err);

		childpid = do_fork();
		switch (childpid) {
		case -1:
			error(1, errno, "fork/vfork");

		case 0:
			/* redirect input */
			close(0);
			err = dup(pipe_in[0]);
			check_no_error("dup(pipe_in)", err < 0 ? err : 0);

			/* redirect output and stderr */
			close(1);
			err = dup(pipe_out[1]);
			check_no_error("dup(pipe_out)", err < 0 ? err : 0);
			close(2);
			err = dup(1);
			check_no_error("dup(1)", err < 0 ? err : 0);

			snprintf(run_param, sizeof(run_param), "--run=%d",
				 t->__reserved.id);
			execlp("gdb", "gdb", "--nx", "--args",
			      argv[0], run_param, "run_target", (char *)NULL);
			_exit(ESRCH);

		default:
			wait_for_pattern("(gdb) ");

			/* the signal handler only is used to skip the test if
			   gdb is missing */
			if (child_terminated)
				break;
			else
				signal(SIGCHLD, SIG_DFL);

			send_command("b breakpoint_target\n", 1);
			send_command("r\n", 1);

			smokey_trace("resume in primary");
			send_command("c\n", 1);
			eval_expression("primary_mode", "1");
			send_command("c\n", 1);

			smokey_trace("synchronous stop");
			eval_expression("thread_loops==expected_loops", "1");
			send_command("c\n", 1);

			smokey_trace("synchronous continue");
			eval_expression("thread_loops==expected_loops", "1");

			send_command("q\n", 0);

			err = waitpid(childpid, &status, 0);
			check("waitpid", err, childpid);
			check("gdb execution", WEXITSTATUS(status), 0);
			close(pipe_out[0]);
		}

		signal(SIGCHLD, SIG_DFL);
	}

	return 0;
}
