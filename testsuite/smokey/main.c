/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <stdio.h>
#include <error.h>
#include <errno.h>
#include <smokey/smokey.h>

int main(int argc, char *const argv[])
{
	struct sched_param param = { .sched_priority = 0 };
	struct smokey_test *t;
	int ret, fails = 0;

	if (pvlist_empty(&smokey_test_list))
		return 0;

	for_each_smokey_test(t) {
		pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
		smokey_trace("\n===  running %s ===\n", t->name);
		ret = t->run(t, argc, argv);
		if (ret) {
			if (ret == -ENOSYS) {
				smokey_note("%s skipped (no kernel support)",
					    t->name);
				continue;
			}
			fails++;
			if (smokey_verbose_mode)
				smokey_note("test %s failed: %d", t->name,
					    -ret);
			if (smokey_keep_going)
				continue;
			return 1;
		}
		smokey_note("%s OK", t->name);
	}

	return fails != 0;
}
