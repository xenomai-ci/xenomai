/*
 * Copyright (C) 2013 Philippe Gerum <rpm@xenomai.org>.
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

#include "boilerplate/time.h"

void timespec_sub(struct timespec *__restrict r,
		  const struct timespec *__restrict t1,
		  const struct timespec *__restrict t2)
{
	r->tv_sec = t1->tv_sec - t2->tv_sec;
	r->tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

void timespec_subs(struct timespec *__restrict r,
		   const struct timespec *__restrict t1,
		   sticks_t t2)
{
	sticks_t s, rem;

	s = t2 / 1000000000;
	rem = t2 - s * 1000000000;
	r->tv_sec = t1->tv_sec - s;
	r->tv_nsec = t1->tv_nsec - rem;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += 1000000000;
	}
}

void timespec_add(struct timespec *__restrict r,
		  const struct timespec *__restrict t1,
		  const struct timespec *__restrict t2)
{
	r->tv_sec = t1->tv_sec + t2->tv_sec;
	r->tv_nsec = t1->tv_nsec + t2->tv_nsec;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

void timespec_adds(struct timespec *__restrict r,
		   const struct timespec *__restrict t1,
		   sticks_t t2)
{
	sticks_t s, rem;

	s = t2 / 1000000000;
	rem = t2 - s * 1000000000;
	r->tv_sec = t1->tv_sec + s;
	r->tv_nsec = t1->tv_nsec + rem;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}

void timespec_sets(struct timespec *__restrict r,
		   ticks_t ns)
{
	r->tv_sec = ns / 1000000000UL;
	r->tv_nsec = ns - r->tv_sec * 1000000000UL;
	if (r->tv_nsec >= 1000000000) {
		r->tv_sec++;
		r->tv_nsec -= 1000000000;
	}
}
