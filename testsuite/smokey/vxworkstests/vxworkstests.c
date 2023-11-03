// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <smokey/smokey.h>

static const char * const tests[] = {
	"vxworkstests_lst1",
	"vxworkstests_msgQ1",
	"vxworkstests_msgQ2",
	"vxworkstests_msgQ3",
	"vxworkstests_rng1",
	"vxworkstests_sem1",
	"vxworkstests_sem2",
	"vxworkstests_sem3",
	"vxworkstests_sem4",
	"vxworkstests_wd1",
	"vxworkstests_task1",
	"vxworkstests_task2",
};

static int run_vxworkstests(struct smokey_test *t, int argc, char *const argv[])
{
#ifdef CONFIG_XENO_LORES_CLOCK_DISABLED
	(void)tests;
	smokey_note("vxworkstest skipped. --enable-lores-clock missing.");
	return 0;
#else
	int test_ret = 0;
	int ret = 0;
	int tmp;

	for (size_t i = 0; i < ARRAY_LEN(tests); i++) {
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
smokey_test_plugin(vxworkstests, SMOKEY_NOARGS, "Run external vxworkstests");

