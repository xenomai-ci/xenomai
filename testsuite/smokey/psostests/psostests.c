// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <smokey/smokey.h>

static const char * const tests[] = {
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

static int run_psostests(struct smokey_test *t, int argc, char *const argv[])
{
#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED
	(void)tests;
	smokey_note("psostests skipped. --enable-lores-clock missing.");
	return 0;
#else
	int test_ret = 0;
	int ret = 0;
	int tmp;

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
		tmp = smokey_run_extprog(XENO_TEST_DIR, tests[i],
					 "--cpu-affinity=0", &test_ret);
		if (test_ret)
			ret = test_ret; /* Return the last failed test result */
		if (tmp)
			break;
	}

	return ret;
#endif
}
smokey_test_plugin(psostests, SMOKEY_NOARGS, "Run external psostests");

