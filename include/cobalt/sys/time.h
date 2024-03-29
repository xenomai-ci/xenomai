/*
 * Copyright (C) 2011 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#pragma GCC system_header
#include_next <sys/time.h>

#ifndef _COBALT_SYS_TIME_H
#define _COBALT_SYS_TIME_H

#include <cobalt/wrappers.h>

struct timezone;

#ifdef __cplusplus
extern "C" {
#endif

COBALT_DECL_TIME64(int, gettimeofday, __gettimeofday64,
		   (struct timeval *tv, struct timezone *tz));

#ifdef __cplusplus
}
#endif

#endif /* !_COBALT_SYS_TIME_H */
