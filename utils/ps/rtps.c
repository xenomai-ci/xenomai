/**
 * @note Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include <string.h>
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <stdlib.h>

#define PROC_ACCT  "/proc/xenomai/sched/acct"
#define PROC_PID  "/proc/%d/cmdline"

#define ACCT_FMT_1  "%u %d %lu %lu %lu %lu %lx %Lu %Lu %Lu"
#define ACCT_FMT_2  ACCT_FMT_1 " %[^\n]"
#define ACCT_NFMT_1 10
#define ACCT_NFMT_2 11

int main(int argc, char *argv[])
{
	char cmdpath[sizeof(PROC_PID) + 32], cmdbuf[BUFSIZ], acctbuf[BUFSIZ], name[64];
	unsigned long ssw, csw, xsc, pf, state, sec;
	unsigned long long account_period,
		exectime_period, exectime_total, v;
	unsigned int cpu, hr, min, msec, usec;
	FILE *acctfp, *cmdfp;
	int pid;

	acctfp = fopen(PROC_ACCT, "r");
	if (acctfp == NULL)
		error(1, errno, "cannot open %s\n", PROC_ACCT);

	printf("%-6s %-17s   %-24s %s\n\n",
	       "PID", "TIME", "THREAD", "CMD");

	while (fgets(acctbuf, sizeof(acctbuf), acctfp) != NULL) {
		if (sscanf(acctbuf, ACCT_FMT_2,
		      &cpu, &pid, &ssw, &csw, &xsc, &pf, &state,
		      &account_period, &exectime_period,
		      &exectime_total, name) != ACCT_NFMT_2) {
			strcpy(name, "");
			if (sscanf(acctbuf, ACCT_FMT_1,
			      &cpu, &pid, &ssw, &csw, &xsc, &pf, &state,
			     &account_period, &exectime_period,
			     &exectime_total) != ACCT_NFMT_1) {
				break;
			}
		}

		snprintf(cmdpath, sizeof(cmdpath), PROC_PID, pid);
		cmdfp = fopen(cmdpath, "r");

		if (cmdfp == NULL ||
		    fgets(cmdbuf, sizeof(cmdbuf), cmdfp) == NULL)
			strcpy(cmdbuf, "-");

		if (cmdfp)
			fclose(cmdfp);

		v = exectime_total;
		sec = v / 1000000000LL;
		v %= 1000000000LL;
		msec = v / 1000000LL;
		v %= 1000000LL;
		usec = v / 1000LL;
		hr = sec / (60 * 60);
		sec %= (60 * 60);
		min = sec / 60;
		sec %= 60;
		printf("%-6d %.3u:%.2u:%.2lu.%.3u,%.3u   %-24s %s\n",
		       pid,
		       hr, min, sec, msec, usec,
		       name, cmdbuf);
	}

	exit(0);
}
