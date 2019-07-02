/*
 * fork->exec test.
 *
 * Copyright (C) Philippe Gerum <rpm@xenomai.org>
 *
 * Released under the terms of GPLv2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <xeno_config.h>
#include <boilerplate/libc.h>
#include <smokey/smokey.h>

smokey_test_plugin(posix_fork,
		   SMOKEY_NOARGS,
		   "Check POSIX fork->exec sequence."
);

/*
 * The purpose of this test is to check whether Cobalt detects and
 * handles a fork->exec sequence properly for Xenomai-enabled threads,
 * with respect to managing their respective shadow contexts. Cobalt
 * should drop the child's shadow upon detecting exec(), then create
 * another one for the emerging process's main() thread as usual.
 *
 * We don't have to do much beyond firing such sequence fo testing: if
 * Cobalt messes up, the kernel will certainly crash.
 */
static int run_posix_fork(struct smokey_test *t, int argc, char *const argv[])
{
	/*
	 * Re-exec ourselves without running any test, this is
	 * enough for creating a shadow context.
	 */
	return smokey_fork_exec(argv[0], "smokey");
}
