/*
 * Copyright (C) 2013 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

COBALT_IMPL(int, timerfd_create, (int clockid, int flags))
{
	int fd;

	fd = XENOMAI_SYSCALL2(sc_cobalt_timerfd_create, clockid, flags);
	if (fd < 0) {
		errno = -fd;
		return -1;
	}
	
	return fd;
}

COBALT_IMPL(int, timerfd_settime,
	    (int fd, int flags, const struct itimerspec *new_value,
	     struct itimerspec *old_value))
{
	int ret;

#ifdef __USE_TIME_BITS64
	long sc_nr = sc_cobalt_timerfd_settime64;
#else
	long sc_nr = sc_cobalt_timerfd_settime;
#endif

	ret = -XENOMAI_SYSCALL4(sc_nr, fd, flags, new_value, old_value);
	if (ret == 0)
		return ret;
	
	errno = ret;
	return -1;
}

COBALT_IMPL(int, timerfd_gettime, (int fd, struct itimerspec *curr_value))
{
	int ret;
	
	ret = -XENOMAI_SYSCALL2(sc_cobalt_timerfd_gettime, fd, curr_value);
	if (ret == 0)
		return ret;
	
	errno = ret;
	return -1;
}
