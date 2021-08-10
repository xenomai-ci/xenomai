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
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <cobalt/uapi/signal.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

COBALT_IMPL(int, sigwait, (const sigset_t *set, int *sig))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = -XENOMAI_SYSCALL2(sc_cobalt_sigwait, set, sig);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(int, sigwaitinfo, (const sigset_t *set, siginfo_t *si))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL2(sc_cobalt_sigwaitinfo, set, si);
	if (ret < 0) {
		errno = -ret;
		ret = -1;
	}

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(int, sigtimedwait, (const sigset_t *set, siginfo_t *si,
				const struct timespec *timeout))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef __USE_TIME_BITS64
	ret = XENOMAI_SYSCALL3(sc_cobalt_sigtimedwait64, set, si, timeout);
#else
	ret = XENOMAI_SYSCALL3(sc_cobalt_sigtimedwait, set, si, timeout);
#endif
	if (ret < 0) {
		errno = -ret;
		ret = -1;
	}

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(int, sigpending, (sigset_t *set))
{
	int ret;

	ret = XENOMAI_SYSCALL1(sc_cobalt_sigpending, set);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}

COBALT_IMPL(int, kill, (pid_t pid, int sig))
{
	int ret;

	/*
	 * Delegate processing of special pids to the regular
	 * kernel. We only deal with thread-directed signals.
	 */
	if (pid <= 0)
		return __STD(kill(pid, sig));

	ret = XENOMAI_SYSCALL2(sc_cobalt_kill, pid, sig);
	if (ret) {
		/* Retry with regular kill is no RT target was found. */
		if (ret == -ESRCH)
			return __STD(kill(pid, sig));

		errno = -ret;
		return -1;
	}

	return 0;
}

COBALT_IMPL(int, sigqueue, (pid_t pid, int sig, const union sigval value))
{
	int ret;

	ret = XENOMAI_SYSCALL3(sc_cobalt_sigqueue, pid, sig, &value);
	if (ret) {
		errno = -ret;
		return -1;
	}

	return 0;
}
