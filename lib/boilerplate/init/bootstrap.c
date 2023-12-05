/*
 * The source code in this particular file is released using a dual
 * license scheme.  You can choose the licence that better fits your
 * requirements.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2017 Philippe Gerum <rpm@xenomai.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * -----------------------------------------------------------------------
 *
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

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <xenomai/init.h>

static int early_argc;

static char *const *early_argv;

/*
 * The bootstrap module object is built in two forms:
 *
 * - in static object form, to be glued to the main executable, which
 *   should include a wrapper interposing on the main() routine for
 *   auto-init purpose. Such wrapper is activated when symbol wrapping
 *   is enabled at link time (--wrap).
 *    
 * - in dynamic object form, to be included in a shared library target
 *   which enables the auto-init feature. This form should not include
 *   any wrapper to a main() routine - which does not exist - but only
 *   a constructor routine performing the inits.
 *
 * The macro __BOOTSTRAP_DSO__ tells us whether we are building the
 * bootstrap module to be glued into a dynamic shared object. If not,
 * the main() interception code should be present in the relocatable
 * object.
 */

#ifdef __BOOTSTRAP_DSO__

static inline void call_init(int *argcp, char *const **argvp)
{
	xenomai_init_dso(argcp, argvp);
}

#else

const int xenomai_auto_bootstrap = 1;

int __real_main(int argc, char *const argv[]);

int __wrap_main(int argc, char *const argv[])
__attribute__((alias("xenomai_main"), weak));

int xenomai_main(int argc, char *const argv[])
{
	if (early_argc)
		return __real_main(early_argc, early_argv);
	
	xenomai_init(&argc, &argv);

	return __real_main(argc, argv);
}

static inline void call_init(int *argcp, char *const **argvp)
{
	xenomai_init(argcp, argvp);
}

#endif /* !__BOOTSTRAP_DSO__ */

__bootstrap_ctor static void xenomai_bootstrap(void)
{
	char *arglist, *argend, *p, **v, *const *argv;
	ssize_t len, ret;
	int fd, n, argc;

	len = 1024;

	for (;;) {
		fd = __STD(open("/proc/self/cmdline", O_RDONLY));
		if (fd < 0)
			return;

		arglist = __STD(malloc(len));
		if (arglist == NULL) {
			__STD(close(fd));
			return;
		}

		ret = __STD(read(fd, arglist, len));
		__STD(close(fd));

		if (ret < 0) {
			__STD(free(arglist));
			return;
		}

		if (ret < len)
			break;

		__STD(free(arglist));
		len <<= 1;
	}

	argend = arglist + ret;
	p = arglist;
	n = 0;
	while (p < argend) {
		n++;
		p += strlen(p) + 1;
	}

	v = __STD(malloc((n + 1) * sizeof(char *)));
	if (v == NULL) {
		__STD(free(arglist));
		return;
	}

	p = arglist;
	n = 0;
	while (p < argend) {
		v[n++] = p;
		p += strlen(p) + 1;
	}

	v[n] = NULL;
	argv = v;
	argc = n;

	call_init(&argc, &argv);
	early_argc = argc;
	early_argv = argv;
}
