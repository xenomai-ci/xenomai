/*
 * Analogy for Linux, calibration program
 *
 * Copyright (C) 2014 Jorge A. Ramirez-Ortiz <jro@xenomai.org>
 *
 * from original code from the Comedi project
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */


#ifndef __ANALOGY_CALIBRATE_H__
#define __ANALOGY_CALIBRATE_H__

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "error.h"

extern struct timespec calibration_start_time;
extern a4l_desc_t descriptor;
extern FILE *cal;

#define RETURN	 1
#define CONT 	 0
#define EXIT	-1

#define error(action, code, fmt, ...) 						\
do {										\
       error_at_line(action, code, __FILE__, __LINE__, fmt, ##__VA_ARGS__ );	\
       if (action == RETURN) 							\
               return -1;							\
}while(0)

struct breakdown_time {
	int ms;
	int us;
	int ns;
};

static inline void do_time_breakdown(struct breakdown_time *p,
				     const struct timespec *t)
{
	unsigned long long ms, us, ns;

	ns = t->tv_sec * 1000000000ULL;
	ns += t->tv_nsec;
	ms = ns / 1000000ULL;
	us = (ns % 1000000ULL) / 1000ULL;

	p->ms = (int)ms;
	p->us = (int)us;
	p->ns = (int)ns;
}

static inline void timespec_sub(struct timespec *r,
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

static inline void __debug(char *fmt, ...)
{
	struct timespec now, delta;
	struct breakdown_time tm;
	char *header, *msg;
	int hlen, mlen;
	va_list ap;
	__attribute__((unused)) int err;

	va_start(ap, fmt);

	clock_gettime(CLOCK_MONOTONIC, &now);
	timespec_sub(&delta, &now, &calibration_start_time);
	do_time_breakdown(&tm, &delta);

	hlen = asprintf(&header, "%4d\"%.3d.%.3d| ",
			tm.ms / 1000, tm.ms % 1000, tm.us);

	mlen = vasprintf(&msg, fmt, ap);

	err = write(fileno(stdout), header, hlen);
	err = write(fileno(stdout), msg, mlen);

	free(header);
	free(msg);

	va_end(ap);
}


static inline int __array_search(char *elem, const char *array[], int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (strncmp(elem, array[i], strlen(array[i])) == 0)
			return 0;

	return -1;
}


static inline double rng_max(a4l_rnginfo_t *range)
{
	double a, b;
	b = A4L_RNG_FACTOR * 1.0;

	a = (double) range->max;
	a = a / b;
	return a;
}

static inline double rng_min(a4l_rnginfo_t *range)
{
	double a, b;

	b = A4L_RNG_FACTOR * 1.0;
	a = (double) range->min;
	a = a / b;
	return a;
}




#endif
