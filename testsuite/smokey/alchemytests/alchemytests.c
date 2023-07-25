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

TEST(alchemytests)
TEST(alchemytests_alarm1)
TEST(alchemytests_buffer1)
TEST(alchemytests_event1)
TEST(alchemytests_heap1)
TEST(alchemytests_heap2)
TEST(alchemytests_mq1)
TEST(alchemytests_mq2)
TEST(alchemytests_mq3)
TEST(alchemytests_mutex1)
TEST(alchemytests_pipe1)
TEST(alchemytests_sem1)
TEST(alchemytests_sem2)
TEST(alchemytests_task1)
TEST(alchemytests_task2)
TEST(alchemytests_task3)
TEST(alchemytests_task4)
TEST(alchemytests_task5)
TEST(alchemytests_task6)
TEST(alchemytests_task7)
TEST(alchemytests_task8)
TEST(alchemytests_task9)
TEST(alchemytests_task10)
