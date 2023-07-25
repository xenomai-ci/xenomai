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

TEST(vxworkstests)
TEST(vxworkstests_lst1)
TEST(vxworkstests_msgQ1)
TEST(vxworkstests_msgQ2)
TEST(vxworkstests_msgQ3)
TEST(vxworkstests_rng1)
TEST(vxworkstests_sem1)
TEST(vxworkstests_sem2)
TEST(vxworkstests_sem3)
TEST(vxworkstests_sem4)
TEST(vxworkstests_wd1)
TEST(vxworkstests_task1)
TEST(vxworkstests_task2)
