// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <libgen.h>
#include <smokey/smokey.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST(name)								   \
	smokey_test_plugin(name, SMOKEY_NOARGS, "Run external test");		   \
	static int run_##name(struct smokey_test *t, int argc, char *const argv[]) \
	{									   \
		return __run_extprog(t, argc, argv);				   \
	}

static int __run_extprog(struct smokey_test *t, int argc, char *const argv[])
{
	int ret;
	char *tst_path;

	ret = asprintf(&tst_path, "%s/%s --cpu-affinity=0", XENO_TEST_DIR, t->name);
	if (ret == -1)
		return -ENOMEM;

	ret = system(tst_path);
	free(tst_path);

	return ret;
}

TEST(psostests)
TEST(psostests_mq1)
TEST(psostests_mq2)
TEST(psostests_mq3)
TEST(psostests_pt1)
// 0"000.679| BUG in __traceobj_assert_failed(): [rn1] trace assertion failed:
//            testsuite/psostests/rn-1.c:46
// => "ret == 0"
//TEST(psostests_rn1)
TEST(psostests_sem1)
TEST(psostests_sem2)
TEST(psostests_task1)
// 0"014.452| BUG in __traceobj_assert_failed(): [FGND] trace assertion failed:
//            ../../../../../../../../../workspace/sources/xenomai/testsuite/smokey/psostests/task-2.c:56 => "ret == 0"
//TEST(psostests_task2)
TEST(psostests_task3)
TEST(psostests_task4)
TEST(psostests_task5)
// 0"011.370| BUG in __traceobj_assert_failed(): [FGND] trace assertion failed:
//            testsuite/psostests/task-6.c:62
// => "ret == 0 && oldprio == myprio"
// TEST(psostests_task6)
TEST(psostests_task7)
// runs forever (100% CPU)
//TEST(psostests_task8)
// 0"002.198| WARNING: [main] lack of resources for core thread, EBUSY
// 0"003.421| BUG in __traceobj_assert_failed(): [root] trace assertion failed:
//           ../../../../../../../../../workspace/sources/xenomai/testsuite/smokey/psostests/task-9.c:29 => "ret == 0"
//TEST(psostests_task9)
TEST(psostests_tm1)
TEST(psostests_tm2)
TEST(psostests_tm3)
TEST(psostests_tm4)
TEST(psostests_tm5)
TEST(psostests_tm6)
TEST(psostests_tm7)
