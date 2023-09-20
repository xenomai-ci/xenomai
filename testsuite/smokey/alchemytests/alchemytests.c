// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <smokey/smokey.h>

static int run_alchemytests(struct smokey_test *t, int argc, char *const argv[])
{
	int ret = 0;

	char *tests[] = {
		"alchemytests_alarm1",
		"alchemytests_buffer1",
		"alchemytests_event1",
		"alchemytests_heap1",
		"alchemytests_heap2",
		"alchemytests_mq1",
		"alchemytests_mq2",
		"alchemytests_mq3",
		"alchemytests_mutex1",
		"alchemytests_pipe1",
		"alchemytests_sem1",
		"alchemytests_sem2",
		"alchemytests_task1",
		"alchemytests_task2",
		"alchemytests_task3",
		"alchemytests_task4",
		"alchemytests_task5",
		"alchemytests_task6",
		"alchemytests_task7",
		"alchemytests_task8",
		"alchemytests_task9",
		"alchemytests_task10"
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
smokey_test_plugin(alchemytests, SMOKEY_NOARGS, "Run external alchemytests");

