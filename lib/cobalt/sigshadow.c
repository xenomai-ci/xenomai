/*
 * Copyright (C) 2007 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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

#include <pthread.h>
#include <signal.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

#if !HAVE_BACKTRACE
static inline int backtrace(void **buffer, int size)
{
	/* Not all *libcs support backtrace(). */
	return 0;
}
#else
#include <execinfo.h>
#endif

static struct sigaction sigshadow_action_orig;

static bool request_backtrace_init;
static bool past_constructors;

/*
 * The following handler is part of the inner user-interface: should
 * remain extern.
 */
int cobalt_sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	void *frames[SIGSHADOW_BACKTRACE_DEPTH];
	int action, arg, nr, skip;

	if (si->si_code != SI_QUEUE)
		return 0;

	action = sigshadow_action(si->si_int);

	switch (action) {
	case SIGSHADOW_ACTION_HARDEN:
		XENOMAI_SYSCALL1(sc_cobalt_migrate, COBALT_PRIMARY);
		break;
	case SIGSHADOW_ACTION_BACKTRACE:
		arg = sigshadow_arg(si->si_int);
		nr = backtrace(frames, ARRAY_SIZE(frames));
		/* Skip the sighandler context. */
		skip = nr > 3 ? 3 : 0;
		XENOMAI_SYSCALL3(sc_cobalt_backtrace, nr - skip, frames + skip, arg);
		break;
	case SIGSHADOW_ACTION_HOME:
		/*
		 * We have been asked to call home from the current
		 * context: sending a query for retrieving our handle
		 * will just do this.
		 */
		cobalt_get_current_slow();
		break;
	default:
		return 0;
	}

	return 1;
}

static void sigshadow_handler(int sig, siginfo_t *si, void *ctxt)
{
	const struct sigaction *const sa = &sigshadow_action_orig;
	sigset_t saved_sigset;

	if (cobalt_sigshadow_handler(sig, si, ctxt))
		return;

	/* Not a signal sent by the Cobalt core */
	if (((sa->sa_flags & SA_SIGINFO) == 0 && sa->sa_handler == NULL)
	    || ((sa->sa_flags & SA_SIGINFO) && sa->sa_sigaction == NULL))
		return;

	pthread_sigmask(SIG_SETMASK, &sa->sa_mask, &saved_sigset);

	if (!(sa->sa_flags & SA_SIGINFO))
		sa->sa_handler(sig);
	else
		sa->sa_sigaction(sig, si, ctxt);

	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
}

/*
 * Kickstart backtrace() so that it may call malloc() from a safe context right
 * now, not later on from the sigshadow handler.
 *
 * We can only trigger this after main() started or very late during
 * construction because, when statically linked, frame_dummy in ctrstuff must
 * have run already.
 */
static void __attribute__((constructor(65535))) trigger_backtrace(void)
{
	void *dummy[SIGSHADOW_BACKTRACE_DEPTH];

	if (request_backtrace_init)
		backtrace(dummy, SIGSHADOW_BACKTRACE_DEPTH);

	past_constructors = true;
	/* ensure visibility of past_constructors after init phase */
	smp_mb();
}

static void install_sigshadow(void)
{
	struct sigaction new_sigshadow_action;
	sigset_t saved_sigset;
	sigset_t mask_sigset;

	request_backtrace_init = true;
	if (past_constructors)
		trigger_backtrace();

	sigemptyset(&mask_sigset);
	sigaddset(&mask_sigset, SIGSHADOW);

	new_sigshadow_action.sa_flags = SA_SIGINFO | SA_RESTART;
	new_sigshadow_action.sa_sigaction = sigshadow_handler;
	sigemptyset(&new_sigshadow_action.sa_mask);
	pthread_sigmask(SIG_BLOCK, &mask_sigset, &saved_sigset);

	sigaction(SIGSHADOW,
		  &new_sigshadow_action, &sigshadow_action_orig);

	if ((sigshadow_action_orig.sa_flags & SA_NODEFER) == 0)
		sigaddset(&sigshadow_action_orig.sa_mask, SIGSHADOW);

	pthread_sigmask(SIG_SETMASK, &saved_sigset, NULL);
}

void cobalt_sigshadow_install_once(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_once(&once, install_sigshadow);
}
