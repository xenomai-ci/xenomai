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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <limits.h>
#include <unistd.h>
#include <stdint.h>
#include <semaphore.h>
#include <boilerplate/setup.h>
#include <cobalt/uapi/kernel/heap.h>
#include <cobalt/ticks.h>
#include <cobalt/tunables.h>
#include <asm/xenomai/syscall.h>
#include <xenomai/init.h>
#include "umm.h"
#include "internal.h"

/**
 * @ingroup cobalt
 * @defgroup cobalt_api POSIX interface
 * @anchor cobalt_api
 *
 * The Cobalt/POSIX interface is an implementation of a subset of the
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/">
 * Single Unix specification</a> over the Cobalt core.
 *
 * The routines from this subset are implemented as wrapper functions
 * as defined by the linker (--wrap option, see man ld(1)).  The
 * linker flags for enabling symbol wrapping can be obtained from the
 * following command: *xeno-config --posix --ldflags*.
 * The full documentation for *xeno-config* can be found at
 * https://xenomai.org/documentation/xenomai-3/html/man1/xeno-config/index.html.
 *
 * When symbol wrapping is enabled:
 *
 *   - calls to POSIX services for which Cobalt provides a (real-time)
 * implementation are redirected to the library implementing the
 * wrapper, by default libcobalt. A list of wrapped symbols libcobalt
 * overrides can be found in the source tree, in
 * lib/cobalt/cobalt.wrappers.
 *
 * With or without symbol wrapping:
 *
 *   - the wrapper function of a POSIX routine can be explicitly
 * invoked by enclosing the function call with the __RT() macro. Since
 * the wrapper symbol is weak, it may be overriden by a 3rd party
 * library, typically to implement its own version of the POSIX call,
 * instead or on top of libcobalt's. e.g. __RT(sem_init(&sem, 0, 0))
 * would initialize a real-time semaphore, usually from libcobalt
 * unless a stronger sem_init() wrapper has been provided by a 3rd
 * party library.
 *
 *   - the libcobalt implementation of a POSIX routine can be
 * explicitly invoked by enclosing the function call with the
 * __COBALT() macro.  e.g. __COBALT(sem_init(&sem, 0, 0)) would always
 * initialize a Cobalt semaphore (strong symbol).
 *
 *   - the regular *libc implementation of a POSIX routine can be
 * explicitly invoked by enclosing the function call with the __STD()
 * macro. This form basically prevents the symbol wrapping to take
 * place. e.g.  __STD(sem_init(&sem, 0, 0)) would always initialize a
 * regular *libc semaphore. This is strictly equivalent to calling the
 * __real_* form of such routine as documented for ld(1).
 *
 * Qualifying POSIX calls explicitly as described above may prove
 * useful for invoking real-time services selectively within a large
 * POSIX code base, for which globally enabling symbol wrapping would
 * be unpractical. This may also help in implementing real-time
 * service libraries for which depending on the linker's symbol
 * wrapping mechanism is not suitable.
 *
 * This said, conforming to the POSIX standard unspoiled by macro
 * tricks for developing an application may be a significant upside as
 * well. YMMV.
 */

__weak int __cobalt_control_bind = 0;

int __cobalt_main_prio = -1;

struct sigaction __cobalt_orig_sigdebug;

static const struct option cobalt_options[] = {
	{
#define main_prio_opt		0
		.name = "main-prio",
		.has_arg = required_argument,
	},
	{
#define print_bufsz_opt	1
		.name = "print-buffer-size",
		.has_arg = required_argument,
	},
	{
#define print_bufcnt_opt	2
		.name = "print-buffer-count",
		.has_arg = required_argument,
	},
	{
#define print_syncdelay_opt	3
		.name = "print-sync-delay",
		.has_arg = required_argument,
	},
	{ /* Sentinel */ }
};

static void sigill_handler(int sig)
{
	const char m[] = "no Xenomai/cobalt support in kernel?\n";
	ssize_t rc __attribute__ ((unused));
	rc = write(2, m, sizeof(m) - 1);
	exit(EXIT_FAILURE);
}

static void low_init(void)
{
	sighandler_t old_sigill_handler;
	struct cobalt_bindreq breq;
	struct cobalt_featinfo *f;
	int ret;

	old_sigill_handler = signal(SIGILL, sigill_handler);
	if (old_sigill_handler == SIG_ERR)
		early_panic("signal(SIGILL): %s", strerror(errno));

	f = &breq.feat_ret;
	breq.feat_req = XENOMAI_FEAT_DEP;
	if (__cobalt_control_bind)
		breq.feat_req |= __xn_feat_control;
	breq.abi_rev = XENOMAI_ABI_REV;
	ret = XENOMAI_SYSBIND(&breq);

	signal(SIGILL, old_sigill_handler);

	switch (ret) {
	case 0:
		break;
	case -EINVAL:
		early_panic("missing feature: %s", f->feat_mis_s);
	case -ENOEXEC:
		early_panic("ABI mismatch: required r%lu, provided r%lu",
			    XENOMAI_ABI_REV, f->feat_abirev);
	case -EAGAIN:
		early_panic("Cobalt core present but stopped "
			    "(use corectl --start)");
	case -ENOSYS:
		early_panic("Cobalt core not enabled in kernel");
	default:
		early_panic("binding failed: %s", strerror(-ret));
	}

	trace_me("connected to Cobalt");

	if (mlockall(MCL_CURRENT | MCL_FUTURE))
		early_panic("mlockall: %s", strerror(errno));

	trace_me("memory locked");
	cobalt_ticks_init();
	cobalt_features_init(f);
	cobalt_init_umm(f->vdso_offset);
	trace_me("memory heaps mapped");
	cobalt_init_current_keys();
}

static int cobalt_init_2(void);

static void cobalt_fork_handler(void)
{
	cobalt_unmap_umm();
	cobalt_clear_tsd();
	cobalt_print_init_atfork();
	if (cobalt_init_2())
		exit(EXIT_FAILURE);
}

static inline void commit_stack_memory(void)
{
	char stk[PTHREAD_STACK_MIN / 2];
	cobalt_commit_memory(stk);
}

static void cobalt_init_1(void)
{
	struct sigaction sa;

	sa.sa_sigaction = cobalt_sigdebug_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGDEBUG, &sa, &__cobalt_orig_sigdebug);

	/*
	 * NOTE: a placeholder for pthread_atfork() may return an
	 * error status with uClibc, so we don't check the return
	 * value on purpose.
	 */
	pthread_atfork(NULL, NULL, cobalt_fork_handler);

	if (sizeof(struct cobalt_mutex_shadow) > sizeof(pthread_mutex_t))
		early_panic("sizeof(pthread_mutex_t): %Zd <"
			    " sizeof(cobalt_mutex_shadow): %Zd!",
			    sizeof(pthread_mutex_t),
			    sizeof(struct cobalt_mutex_shadow));

	if (sizeof(struct cobalt_cond_shadow) > sizeof(pthread_cond_t))
		early_panic("sizeof(pthread_cond_t): %Zd <"
			    " sizeof(cobalt_cond_shadow): %Zd!",
			    sizeof(pthread_cond_t),
			    sizeof(struct cobalt_cond_shadow));

	if (sizeof(struct cobalt_sem_shadow) > sizeof(sem_t))
		early_panic("sizeof(sem_t): %Zd <"
			    " sizeof(cobalt_sem_shadow): %Zd!",
			    sizeof(sem_t),
			    sizeof(struct cobalt_sem_shadow));
}

static int cobalt_init_2(void)
{
	pthread_t ptid = pthread_self();
	struct sched_param parm;
	int policy, ret;

	commit_stack_memory();	/* We only need this for the main thread */
	cobalt_default_condattr_init();

	low_init();
	cobalt_mutex_init();
	cobalt_sched_init();
	cobalt_thread_init();
	cobalt_print_init();

	if (__cobalt_control_bind)
		return 0;

	ret = __STD(pthread_getschedparam(ptid, &policy, &parm));
	if (ret) {
		early_warning("pthread_getschedparam failed");
		return -ret;
	}

	/*
	 * Turn the main thread into a Cobalt thread.
	 * __cobalt_main_prio might have been overriden by some
	 * compilation unit which has been linked in, to force the
	 * scheduling parameters. Otherwise, the current policy and
	 * priority are reused, for declaring the thread to the
	 * Cobalt scheduler.
	 *
	 * SCHED_FIFO is assumed for __cobalt_main_prio > 0.
	 */
	if (__cobalt_main_prio > 0) {
		policy = SCHED_FIFO;
		parm.sched_priority = __cobalt_main_prio;
	} else if (__cobalt_main_prio == 0) {
		policy = SCHED_OTHER;
		parm.sched_priority = 0;
	}

	ret = __RT(pthread_setschedparam(ptid, policy, &parm));
	if (ret) {
		early_warning("pthread_setschedparam failed { policy=%d, prio=%d }",
			      policy, parm.sched_priority);
		return -ret;
	}

	return 0;
}

int cobalt_init(void)
{
	cobalt_init_1();

	return cobalt_init_2();
}

static int get_int_arg(const char *name, const char *arg,
		       int *valp, int min)
{
	int value, ret;
	char *p;

	errno = 0;
	value = (int)strtol(arg, &p, 10);
	if (errno || *p || value < min) {
		ret = -errno ?: -EINVAL;
		early_warning("invalid value for %s: %s", name, arg);
		return ret;
	}

	*valp = value;

	return 0;
}

static int cobalt_parse_option(int optnum, const char *optarg)
{
	int value, ret;

	switch (optnum) {
	case main_prio_opt:
		ret = get_int_arg("--main-prio", optarg, &value, INT32_MIN);
		if (ret)
			return ret;
		__cobalt_main_prio = value;
		break;
	case print_bufsz_opt:
		ret = get_int_arg("--print-buffer-size", optarg, &value, 0);
		if (ret)
			return ret;
		__cobalt_print_bufsz = value;
		break;
	case print_bufcnt_opt:
		ret = get_int_arg("--print-buffer-count", optarg, &value, 0);
		if (ret)
			return ret;
		__cobalt_print_bufcount = value;
		break;
	case print_syncdelay_opt:
		ret = get_int_arg("--print-sync-delay", optarg, &value, 0);
		if (ret)
			return ret;
		__cobalt_print_syncdelay = value;
		break;
	default:
		/* Paranoid, can't happen. */
		return -EINVAL;
	}

	return 0;
}

static void cobalt_help(void)
{
	fprintf(stderr, "--main-prio=<prio>		main thread priority\n");
	fprintf(stderr, "--print-buffer-size=<bytes>	size of a print relay buffer (16k)\n");
	fprintf(stderr, "--print-buffer-count=<num>	number of print relay buffers (4)\n");
	fprintf(stderr, "--print-sync-delay=<ms>	max delay of output synchronization (100 ms)\n");
}

static struct setup_descriptor cobalt_interface = {
	.name = "cobalt",
	.init = cobalt_init,
	.options = cobalt_options,
	.parse_option = cobalt_parse_option,
	.help = cobalt_help,
};

core_setup_call(cobalt_interface);
