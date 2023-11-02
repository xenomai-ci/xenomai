// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <smokey/smokey.h>

static int run_vxworkstests(struct smokey_test *t, int argc, char *const argv[])
{
	int ret = 0;

	char *tests[] = {
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
		"vxworkstests_task2"
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
smokey_test_plugin(vxworkstests, SMOKEY_NOARGS, "Run external vxworkstests");

