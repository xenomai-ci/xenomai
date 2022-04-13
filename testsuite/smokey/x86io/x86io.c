/*
 * Test for a working iopl() after a regression on 5.15:
 * https://www.xenomai.org/pipermail/xenomai/2022-March/047451.html
 *
 * Copyright (C) 2022 sigma star gmbh
 * Author Richard Weinberger <richard@sigma-star.at>
 *
 * Released under the terms of GPLv2.
 */
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <smokey/smokey.h>
#include <string.h>
#include <sys/io.h>
#include <unistd.h>

#define PORT (0x378)

static int saw_segv;

static void *tfn(void *d)
{
	struct sched_param schedp = {0};
	int ret;

	schedp.sched_priority = 1;
	__Terrno(ret, sched_setscheduler(0, SCHED_FIFO, &schedp));

	(void)inb(PORT);

	return (void *)(unsigned long)ret;
}

static void sgfn(int sig, siginfo_t *si, void *ctx)
{
	saw_segv = 1;
}

smokey_test_plugin(x86io, SMOKEY_NOARGS, "Check x86 port io");

int run_x86io(struct smokey_test *t, int argc, char *const argv[])
{
	struct sigaction sa, old_sa;
	unsigned long ptret;
	pthread_t pt;
	int ret;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sgfn;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	if (!__Terrno(ret, sigaction(SIGSEGV, &sa, &old_sa)))
		goto out;

	if (!__Terrno(ret, iopl(3)))
		goto out_restore;

	if (!__T(ret, pthread_create(&pt, NULL, tfn, NULL)))
		goto out_restore;

	if (!__T(ret, pthread_join(pt, (void *)&ptret)))
		goto out_restore;

out_restore:
	sigaction(SIGSEGV, &old_sa, NULL);

out:
	if (ret)
		return -ret;
	if (ptret)
		return -ptret;
	if (saw_segv)
		return -EFAULT;
	return 0;
}
