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

#include <cobalt/arith.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/time.h>
#include "internal.h"

/*
 * If we have no fast path via the vDSO for reading timestamps, ask
 * the Cobalt core.
 */
static int gettime_fallback(clockid_t clk_id, struct timespec *tp)
{
	return __RT(clock_gettime(clk_id, tp));
}

int (*__cobalt_vdso_gettime)(clockid_t clk_id,
			struct timespec *tp) = gettime_fallback;

#ifdef XNARCH_HAVE_NODIV_LLIMD

static struct xnarch_u32frac bln_frac;

unsigned long long cobalt_divrem_billion(unsigned long long value,
					 unsigned long *rem)
{
	unsigned long long q;
	unsigned r;

	q = xnarch_nodiv_ullimd(value, bln_frac.frac, bln_frac.integ);
	r = value - q * 1000000000;
	if (r >= 1000000000) {
		++q;
		r -= 1000000000;
	}
	*rem = r;
	return q;
}

#else /* !XNARCH_HAVE_NODIV_LLIMD */

unsigned long long cobalt_divrem_billion(unsigned long long value,
					 unsigned long *rem)
{
	return xnarch_ulldiv(value, 1000000000, rem);

}
#endif /* !XNARCH_HAVE_NODIV_LLIMD */

void cobalt_ticks_init(void)
{
	void *vcall = cobalt_lookup_vdso(COBALT_VDSO_VERSION,
					 COBALT_VDSO_GETTIME);
	if (vcall)
		__cobalt_vdso_gettime = vcall;
#ifdef XNARCH_HAVE_NODIV_LLIMD
	xnarch_init_u32frac(&bln_frac, 1, 1000000000);
#endif
}
