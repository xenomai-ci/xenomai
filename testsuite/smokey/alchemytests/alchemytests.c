// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <smokey/smokey.h>

static const char * const tests[] = {
	"alchemytests_alarm1",
	"alchemytests_buffer1",
	"alchemytests_event1",
	"alchemytests_heap1",
	"alchemytests_heap2",
	"alchemytests_mq1",
	"alchemytests_mq2",
	"alchemytests_mq3",
	"alchemytests_mutex1",
	"alchemytests_mutex2",
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
	"alchemytests_task10",
	"alchemytests_task11"
};

static int run_alchemytests(struct smokey_test *t, int argc, char *const argv[])
{
	const char *const mod = "xeno_rtipc";
	const char *args = "--vm";
	int test_ret = 0;
	int ret = 0;
	int tmp;

	ret = setenv("XENO_CPU_AFFINITY", "0", 0);
	if (ret != 0) {
		fprintf(stderr, "%s failed to set XENO_CPU_AFFINITY: %m\n",
			__func__);
		return ret;
	}

	/* Try loading the xeno_rtipc module as the pipe test depends on it */
	tmp = smokey_modprobe(mod, true);
	if (tmp)
		smokey_note("Loading the %s module failed.", mod);

	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		tmp = smokey_run_extprog(XENO_TEST_DIR, tests[i],
					 smokey_on_vm ? args : args + 5,
					 &test_ret);
		if (test_ret)
			ret = test_ret; /* Return the last failed test result */
		if (tmp)
			break;
	}

	smokey_rmmod(mod);

	return ret;
}
smokey_test_plugin(alchemytests, SMOKEY_NOARGS, "Run external alchemytests");
