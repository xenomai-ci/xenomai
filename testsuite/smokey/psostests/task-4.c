// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static int tseq[] = {
	1, 2, 3
};

static u_long tid;

static void task(u_long a1, u_long a2, u_long a3, u_long a4)
{
	u_long regval;
	int ret;

	traceobj_enter(&trobj);

	traceobj_mark(&trobj, 1);

	regval = ~0;
	ret = t_getreg(0, 0, &regval);
	traceobj_assert(&trobj, ret == SUCCESS && regval == 0);

	ret = t_setreg(0, 0, 0xdeadbeef);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_setreg(0, 1024, 0);
	traceobj_assert(&trobj, ret == ERR_REGNUM);

	regval = 0;
	ret = t_getreg(tid, 0, &regval);
	traceobj_assert(&trobj, ret == SUCCESS && regval == 0xdeadbeef);

	regval = 0;
	ret = t_getreg(0, 0, &regval);
	traceobj_assert(&trobj, ret == SUCCESS && regval == 0xdeadbeef);

	ret = t_getreg(tid, 1024, &regval);
	traceobj_assert(&trobj, ret == ERR_REGNUM);

	traceobj_mark(&trobj, 2);

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 };
	int ret;

	traceobj_init(&trobj, argv[0], sizeof(tseq) / sizeof(int));

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_mark(&trobj, 3);

	traceobj_join(&trobj);

	traceobj_verify(&trobj, tseq, sizeof(tseq) / sizeof(int));

	exit(0);
}
