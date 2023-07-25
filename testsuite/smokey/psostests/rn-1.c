// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static char rn_mem[65536];

static u_long tid, rnid;

static void alloc_task(u_long a1, u_long a2, u_long a3, u_long a4)
{
	u_long size, alloc_size = 0;
	int ret, n;
	void *buf;

	traceobj_enter(&trobj);

	srandom(0x11223344);

	for (n = 0;; n++) {
		size = (random() % (sizeof(rn_mem) / 8)) + 4;
		ret = rn_getseg(rnid, size, RN_NOWAIT, 0, &buf);
		if (ret) {
			traceobj_assert(&trobj, ret == ERR_NOSEG);
			break;
		}
		memset(buf, 0xaa, size);
		alloc_size += size;
	}

	traceobj_exit(&trobj);
}

int main(int argc, char *const argv[])
{
	u_long args[] = { 1, 2, 3, 4 }, asize, _rnid;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = rn_create("REGION", rn_mem, sizeof(rn_mem),
			32, RN_FIFO|RN_NODEL, &rnid, &asize);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_create("TASK", 20, 0, 0, 0, &tid);
	traceobj_assert(&trobj, ret == SUCCESS);

	ret = t_start(tid, 0, alloc_task, args);
	traceobj_assert(&trobj, ret == SUCCESS);

	traceobj_join(&trobj);

	ret = rn_ident("REGION", &_rnid);
	traceobj_assert(&trobj, ret == SUCCESS && _rnid == rnid);

	ret = rn_delete(rnid);
	traceobj_assert(&trobj, ret == ERR_SEGINUSE);

	exit(0);
}
