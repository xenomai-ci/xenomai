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
TEST(psostests_rn1)
TEST(psostests_sem1)
TEST(psostests_sem2)
TEST(psostests_task1)
TEST(psostests_task2)
TEST(psostests_task3)
TEST(psostests_task4)
TEST(psostests_task5)
TEST(psostests_task6)
TEST(psostests_task7)
TEST(psostests_task8)
TEST(psostests_task9)
TEST(psostests_tm1)
TEST(psostests_tm2)
TEST(psostests_tm3)
TEST(psostests_tm4)
TEST(psostests_tm5)
TEST(psostests_tm6)
TEST(psostests_tm7)
