// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <copperplate/traceobj.h>
#include <psos/psos.h>

#define TEST_DATE  ((2008 << 16)|(4 << 8)|25) /* 4/25/2008 */
#define TEST_TIME  ((11 << 16)|(17 << 8)|30)  /* 11:17:30 */
#define TEST_TICKS 10

static struct traceobj trobj;

int main(int argc, char *const argv[])
{
	unsigned long date, time, ticks;
	int ret, tries = 0;

	traceobj_init(&trobj, argv[0], 0);

	for (;;) {
		ret = tm_set(TEST_DATE, TEST_TIME, TEST_TICKS);
		traceobj_assert(&trobj, ret == SUCCESS);
		ret = tm_get(&date, &time, &ticks);
		traceobj_assert(&trobj, ret == SUCCESS);
		if (time == TEST_TIME)
			break;
		if (++tries > 3)
			break;
	}

	traceobj_assert(&trobj, date == TEST_DATE);
	traceobj_assert(&trobj, time == TEST_TIME);

	exit(0);
}
