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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#ifndef _COBALT_UAPI_KERNEL_TYPES_H
#define _COBALT_UAPI_KERNEL_TYPES_H

#include <linux/types.h>
#include <cobalt/uapi/kernel/limits.h>

typedef __u64 xnticks_t;

typedef __s64 xnsticks_t;

typedef __u32 xnhandle_t;

#define XN_NO_HANDLE		((xnhandle_t)0)
#define XN_HANDLE_INDEX_MASK	((xnhandle_t)0xf0000000)

/* Fixed bits (part of the identifier) */
#define XNSYNCH_PSHARED		((xnhandle_t)0x40000000)

/* Transient bits (expressing a status) */
#define XNSYNCH_FLCLAIM		((xnhandle_t)0x80000000) /* Contended. */
#define XNSYNCH_FLCEIL		((xnhandle_t)0x20000000) /* Ceiling active. */

#define XN_HANDLE_TRANSIENT_MASK	(XNSYNCH_FLCLAIM|XNSYNCH_FLCEIL)

/*
 * Strip all special bits from the handle, only retaining the object
 * index value in the registry.
 */
static inline xnhandle_t xnhandle_get_index(xnhandle_t handle)
{
	return handle & ~XN_HANDLE_INDEX_MASK;
}

/*
 * Strip the transient bits from the handle, only retaining the fixed
 * part making the identifier.
 */
static inline xnhandle_t xnhandle_get_id(xnhandle_t handle)
{
	return handle & ~XN_HANDLE_TRANSIENT_MASK;
}

struct xn_ts64 {
	__s64 tv_sec;
	__s64 tv_nsec;
};

/*
 * Our representation of time specs at the kernel<->user interface
 * boundary at the moment, until we have fully transitioned to a
 * y2038-safe implementation in libcobalt. Once done, those legacy
 * types will be removed.
 */
struct __user_old_timespec {
	long  tv_sec;
	long  tv_nsec;
};

struct __user_old_itimerspec {
	struct __user_old_timespec it_interval;
	struct __user_old_timespec it_value;
};

struct __user_old_timeval {
	long  tv_sec;
	long  tv_usec;
};

/* Lifted from include/uapi/linux/timex.h. */
struct __user_old_timex {
	unsigned int modes;	/* mode selector */
	__kernel_long_t offset;	/* time offset (usec) */
	__kernel_long_t freq;	/* frequency offset (scaled ppm) */
	__kernel_long_t maxerror;/* maximum error (usec) */
	__kernel_long_t esterror;/* estimated error (usec) */
	int status;		/* clock command/status */
	__kernel_long_t constant;/* pll time constant */
	__kernel_long_t precision;/* clock precision (usec) (read only) */
	__kernel_long_t tolerance;/* clock frequency tolerance (ppm)
				   * (read only)
				   */
	struct __user_old_timeval time;	/* (read only, except for ADJ_SETOFFSET) */
	__kernel_long_t tick;	/* (modified) usecs between clock ticks */

	__kernel_long_t ppsfreq;/* pps frequency (scaled ppm) (ro) */
	__kernel_long_t jitter; /* pps jitter (us) (ro) */
	int shift;              /* interval duration (s) (shift) (ro) */
	__kernel_long_t stabil;            /* pps stability (scaled ppm) (ro) */
	__kernel_long_t jitcnt; /* jitter limit exceeded (ro) */
	__kernel_long_t calcnt; /* calibration intervals (ro) */
	__kernel_long_t errcnt; /* calibration errors (ro) */
	__kernel_long_t stbcnt; /* stability limit exceeded (ro) */

	int tai;		/* TAI offset (ro) */

	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32; int  :32;
	int  :32; int  :32; int  :32;
};

#endif /* !_COBALT_UAPI_KERNEL_TYPES_H */
