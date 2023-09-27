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
#ifndef _COBALT_WRAPPERS_H
#define _COBALT_WRAPPERS_H

#include <boilerplate/compiler.h>

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define __WRAP(call)		__wrap_ ## call
#define __STD(call)		__real_ ## call
#define __COBALT(call)		__cobalt_ ## call
#define __RT(call)		__COBALT(call)
#define COBALT_DECL(T, FN, I)                                                  \
	__typeof__(T) __RT(FN) I;                                              \
	__typeof__(T) __STD(FN) I;                                             \
	__typeof__(T) __WRAP(FN) I

#if __USE_TIME_BITS64
/*
 * Make __RT() and __STD() usable in combination with time64_t related services.
 */
#define COBALT_IMPL_TIME64(T, FN, FN_64, I)                                    \
	__typeof__(T) __wrap_##FN_64 I                                         \
		__attribute__((alias("__cobalt_" __stringify(FN)), weak));     \
	COBALT_IMPL(T, FN, I)
#define COBALT_DECL_TIME64(T, FN, A, I)                                        \
	extern T __REDIRECT_NTH(__STD(FN), I, __real_##A);                     \
	COBALT_DECL(T, FN, I)
#else
#define COBALT_IMPL_TIME64(T, FN, FN_64, I) COBALT_IMPL(T, FN, I)
#define COBALT_DECL_TIME64(T, FN, FN_64, I) COBALT_DECL(T, FN, I)
#endif

/*
 * 
 * Each "foo" Cobalt routine shadowing a POSIX service may be
 * overriden by an external library (see --with-cobalt-override
 * option), in which case we generate the following symbols:
 * 
 * __real_foo() => Original POSIX implementation.
 * __cobalt_foo() => Cobalt implementation.
 * __wrap_foo() => Weak alias to __cobalt_foo(), may be
 * overriden.
 *
 * In the latter case, the external library shall provide its own
 * implementation of __wrap_foo(), overriding Cobalt's foo()
 * version. The original Cobalt implementation can still be
 * referenced as __COBALT(foo).
 */
#define COBALT_IMPL(T, FN, I)                                                  \
	__typeof__(T) __wrap_##FN I                                            \
		__attribute__((alias("__cobalt_" __stringify(FN)), weak));     \
	__typeof__(T) __cobalt_##FN I

#endif /* !_COBALT_WRAPPERS_H */
