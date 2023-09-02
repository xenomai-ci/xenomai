/*
 * Copyright (C) 2011-2012,2015 Gilles Chanteperdrix <gch@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>

#include <smokey/smokey.h>

#include <cobalt/sys/cobalt.h>

#define DURATION 10000000

#if CONFIG_SMP
#define smp_sched_setaffinity(pid,len,mask) sched_setaffinity(pid,len,mask)
#define smp_sched_getaffinity(pid,len,mask) sched_getaffinity(pid,len,mask)
#else /* !CONFIG_SMP */
#define smp_sched_setaffinity(pid,len,mask) 0
#define smp_sched_getaffinity(pid,len,mask) 0
#endif /* !CONFIG_SMP */

smokey_test_plugin(tsc,
		SMOKEY_ARGLIST(
			SMOKEY_INT(duration),
			),
		"Check that emulated tsc is monotonic"
);

static int run_tsc(struct smokey_test *t, int argc, char *const argv[])
{
	unsigned long long runtime, start, jump, tsc1, tsc2;
	unsigned long long sum, g_sum;
	unsigned long long loops, g_loops;
	unsigned dt, min, max, g_min, g_max;
	unsigned long long secs;
	unsigned i, margin;

#if CONFIG_SMP
	/* Pin the test to the CPU it is currently running on */
	cpu_set_t mask;

	if (smp_sched_getaffinity(0, sizeof(mask), &mask) == 0)
		for (i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++)
			if (CPU_ISSET(i, &mask)) {
				CPU_ZERO(&mask);
				CPU_SET(i, &mask);

				smp_sched_setaffinity(0, sizeof(mask), &mask);
				smokey_trace("Pinned to cpu %d", i);
				break;
			}
#endif

	g_min = ~0U;
	g_max = 0;
	g_sum = 0;
	g_loops = 0;

	smokey_parse_args(t, argc, argv);

	runtime = cobalt_read_ns();
	margin = 2000;
	if (margin < 80)
		margin = 80;

	if (SMOKEY_ARG_ISSET(tsc, duration)) {
		secs = SMOKEY_ARG_INT(tsc, duration);
		min = (secs + 59) / 60;
		secs = min * 60;
	} else
		secs = 15;
	min = secs / 60;
	smokey_trace("Checking tsc for %u minute(s)", min);

	for(i = 0; i < secs; i++) {
		min = ~0U;
		max = 0;
		sum = 0;
		loops = 0;
		tsc2 = start = cobalt_read_ns();
		do {
			tsc1 = cobalt_read_ns();
			if (tsc1 < tsc2) {
				fprintf(stderr, "%016Lx -> %016Lx\n",
					tsc2, tsc1);
				goto err1;
			}
			tsc2 = cobalt_read_ns();
			if (tsc2 < tsc1) {
				fprintf(stderr, "%016Lx -> %016Lx\n",
					tsc1, tsc2);
				goto err2;
			}

			dt = tsc2 - tsc1;

			if (dt > margin)
				continue;

			if (dt < min)
				min = dt;
			if (dt > max)
				max = dt;
			sum += dt;
			++loops;
		} while (tsc2 - start < ONE_BILLION);

		smokey_trace("min: %u, max: %u, avg: %g",
			min, max, (double)sum / loops);

		if (min < g_min)
			g_min = min;
		if (max > g_max)
			g_max = max;
		g_sum += sum;
		g_loops += loops;
	}

	smokey_trace("min: %u, max: %u, avg: %g -> %g us",
		g_min, g_max, (double)g_sum / g_loops,
		(double)g_sum / (1000 * g_loops));

	return EXIT_SUCCESS;

  err1:
	runtime = tsc2 - runtime;
	jump = tsc2 - tsc1;
	goto display;
  err2:
	runtime = tsc1 - runtime;
	jump = tsc1 - tsc2;

  display:
	fprintf(stderr, "tsc not monotonic after %Lu ticks, ",
		runtime);
	fprintf(stderr, "jumped back %Lu tick\n", jump);

	return EXIT_FAILURE;

}
