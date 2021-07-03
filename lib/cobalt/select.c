/*
 * Copyright (C) 2010 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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
#include <pthread.h>
#include <sys/select.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

COBALT_IMPL(int, select, (int __nfds, fd_set *__restrict __readfds,
			  fd_set *__restrict __writefds,
			  fd_set *__restrict __exceptfds,
			  struct timeval *__restrict __timeout))
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = XENOMAI_SYSCALL5(sc_cobalt_select, __nfds,
			       __readfds, __writefds, __exceptfds, __timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (err == -EADV || err == -EPERM || err == -ENOSYS)
		return __STD(select(__nfds, __readfds,
				    __writefds, __exceptfds, __timeout));

	if (err >= 0)
		return err;

	errno = -err;
	return -1;
}
