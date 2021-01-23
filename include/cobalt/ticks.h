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
#ifndef _COBALT_TICKS_H
#define _COBALT_TICKS_H

#include <stdbool.h>
#include <cobalt/uapi/kernel/types.h>

/*
 * Depending on the underlying pipeline support, we may represent time
 * stamps as count of nanoseconds (Dovetail), or as values of the
 * hardware tick counter (aka TSC) available with the platform
 * (I-pipe). In the latter - legacy - case, we need to convert from
 * TSC values to nanoseconds and conversely via scaled maths. This
 * indirection will go away once support for the I-pipe is removed.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned long long __cobalt_tsc_clockfreq;

static inline bool cobalt_use_legacy_tsc(void)
{
	return !!__cobalt_tsc_clockfreq;
}

xnsticks_t __cobalt_tsc_to_ns(xnsticks_t ticks);

xnsticks_t __cobalt_tsc_to_ns_rounded(xnsticks_t ticks);

xnsticks_t __cobalt_ns_to_tsc(xnsticks_t ns);

static inline
xnsticks_t cobalt_ns_to_ticks(xnsticks_t ns)
{
	if (cobalt_use_legacy_tsc())
		return __cobalt_ns_to_tsc(ns);

	return ns;
}

static inline
xnsticks_t cobalt_ticks_to_ns(xnsticks_t ticks)
{
	if (cobalt_use_legacy_tsc())
		return __cobalt_tsc_to_ns(ticks);

	return ticks;
}

static inline
xnsticks_t cobalt_ticks_to_ns_rounded(xnsticks_t ticks)
{
	if (cobalt_use_legacy_tsc())
		return __cobalt_tsc_to_ns_rounded(ticks);

	return ticks;
}

unsigned long long cobalt_divrem_billion(unsigned long long value,
					 unsigned long *rem);
#ifdef __cplusplus
}
#endif

#endif /* !_COBALT_TICKS_H */
