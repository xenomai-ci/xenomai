// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <smokey/smokey.h>

static int run_psostests(struct smokey_test *t, int argc, char *const argv[])
{
	int ret = 0;

	char *tests[] = {
		"psostests_mq1",
		"psostests_mq2",
		"psostests_mq3",
		"psostests_pt1",
		// 0"000.679| BUG in __traceobj_assert_failed(): [rn1] trace assertion failed:
		// testsuite/psostests/rn-1.c:46
		// => "ret == 0"
		//"psostests_rn1"
		"psostests_sem1",
		"psostests_sem2",
		"psostests_task1",
		// 0"014.452| BUG in __traceobj_assert_failed(): [FGND] trace assertion failed:
		//		../../../../../../../../../workspace/sources/xenomai/testsuite/smokey/psostests/task-2.c:56 => "ret == 0"
		//"psostests_task2",
		"psostests_task3",
		"psostests_task4",
		"psostests_task5",
		// 0"011.370| BUG in __traceobj_assert_failed(): [FGND] trace assertion failed:
		//		testsuite/psostests/task-6.c:62
		// => "ret == 0 && oldprio == myprio"
		//"psostests_task6",
		"psostests_task7",
		// Runs forever (100% CPU)
		//"psostests_task8"
		// 0"002.198| WARNING: [main] lack of resources for core thread, EBUSY
		// 0"003.421| BUG in __traceobj_assert_failed(): [root] trace assertion failed:
		//	   ../../../../../../../../../workspace/sources/xenomai/testsuite/smokey/psostests/task-9.c:29 => "ret == 0"
		//"psostests_task9",
		"psostests_tm1",
		"psostests_tm2",
		"psostests_tm3",
		"psostests_tm4",
		"psostests_tm5",
		"psostests_tm6",
		"psostests_tm7"
	};

	for (size_t t = 0; t < sizeof(tests) / sizeof(tests[0]); t++)
	{
		int fails = 0;

		ret = smokey_run_extprog(XENO_TEST_DIR, tests[t],
					 "--cpu-affinity=0");
		if (ret) {
			fails++;
			if (smokey_keep_going)
				continue;
			if (smokey_verbose_mode)
				error(1, -ret, "test %s failed", tests[t]);
			return 1;
		}
		smokey_note("%s OK", tests[t]);
	}

	return ret;
}
smokey_test_plugin(psostests, SMOKEY_NOARGS, "Run external psostests");

