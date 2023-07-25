// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

static struct traceobj trobj;

static char pt_mem[65536];

int main(int argc, char *const argv[])
{
	u_long nbufs, ptid, _ptid, n;
	void *buf, *lbuf;
	int ret;

	traceobj_init(&trobj, argv[0], 0);

	ret = pt_create("PART", pt_mem, NULL, sizeof(pt_mem), 16, PT_NODEL, &ptid, &nbufs);
	traceobj_assert(&trobj, ret == SUCCESS);

	for (n = 0, lbuf = NULL;; n++, lbuf = buf) {
		ret = pt_getbuf(ptid, &buf);
		if (ret) {
			traceobj_assert(&trobj, ret == ERR_NOBUF);
			break;
		}
		if (lbuf)
			traceobj_assert(&trobj, (caddr_t)lbuf + 16 == (caddr_t)buf);
		memset(buf, 0xaa, 16);
	}

	traceobj_assert(&trobj, nbufs == n);

	ret = pt_delete(ptid);
	traceobj_assert(&trobj, ret == ERR_BUFINUSE);

	for (buf = lbuf; n > 0; n--, buf = (caddr_t)buf - 16) {
		ret = pt_retbuf(ptid, buf);
		traceobj_assert(&trobj, ret == SUCCESS);
	}

	ret = pt_ident("PART", 0, &_ptid);
	traceobj_assert(&trobj, ret == SUCCESS && _ptid == ptid);

	ret = pt_delete(ptid);
	traceobj_assert(&trobj, ret == SUCCESS);

	exit(0);
}
