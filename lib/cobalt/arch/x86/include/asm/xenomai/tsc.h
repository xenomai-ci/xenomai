/*
 * Copyright (C) 2001,2002,2003,2007 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _LIB_COBALT_X86_TSC_H
#define _LIB_COBALT_X86_TSC_H

static inline unsigned long long cobalt_read_legacy_tsc(void)
{
#ifdef __i386__
	unsigned long long t;

	__asm__ __volatile__ ("rdtsc" : "=A" (t));
	return t;

#else /* x86_64 */
	unsigned int __a,__d;

	__asm__ __volatile__ ("rdtsc" : "=a" (__a), "=d" (__d));
	return ((unsigned long)__a) | (((unsigned long)__d) << 32);
#endif /* x86_64 */
}

#endif /* !_LIB_COBALT_X86_TSC_H */
