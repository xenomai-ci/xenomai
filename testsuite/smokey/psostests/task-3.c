// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static u_long tidA, tidB;

int main(int argc, char *const argv[])
{
	u_long tid;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = t_create("TSKA", 20, 0, 0, 0, &tidA);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TSKB", 21, 0, 0, 0, &tidB);
	traceobj_assert(&trobj, ret == SUCCESS);

	tid = ~tidA;
	ret = t_ident("TSKA", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_assert(&trobj, tid == tidA);

	tid = ~tidB;
	ret = t_ident("TSKB", 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);
	traceobj_assert(&trobj, tid == tidB);

	ret = t_delete(tidA);
	traceobj_assert(&trobj, ret == SUCCESS);
	ret = t_ident("TSKA", 0, &tid);
	traceobj_assert(&trobj, ret == ERR_OBJNF);

	ret = t_ident("TSKB", 1, &tid);
	traceobj_assert(&trobj, ret == ERR_NODENO);

	exit(0);
}
