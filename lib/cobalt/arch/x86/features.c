/*
 * Copyright (C) 2008 Gilles Chanteperdrix <gch@xenomai.org>.
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <boilerplate/atomic.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/uapi/fptest.h>
#include "internal.h"

void cobalt_arch_check_features(struct cobalt_featinfo *finfo)
{
#if defined(__i386__) && defined(CONFIG_XENO_X86_VSYSCALL)
	size_t n = confstr(_CS_GNU_LIBPTHREAD_VERSION, NULL, 0);
	if (n > 0) {
		char buf[n];

		confstr (_CS_GNU_LIBPTHREAD_VERSION, buf, n);

		if (strstr (buf, "NPTL"))
			return;
	}

	early_warning("--enable-x86-vsyscall requires NPTL, which does not match");
	early_warning("your configuration. Please upgrade, or rebuild the");
	early_panic("Xenomai libraries passing --disable-x86-vsyscall");
#endif /* __i386__ && CONFIG_XENO_X86_VSYSCALL */
}

int cobalt_fp_detect(void)
{
	char buffer[1024];
	int features = 0;
	FILE *fp;

	fp = fopen("/proc/cpuinfo", "r");
	if(fp == NULL)
		return 0;

	while (fgets(buffer, sizeof(buffer), fp)) {
		if(strncmp(buffer, "flags", sizeof("flags") - 1))
			continue;
		if (strstr(buffer, "sse2"))
			features |= __COBALT_HAVE_SSE2;
		if (strstr(buffer, "avx"))
			features |= __COBALT_HAVE_AVX;
		break;
	}

	fclose(fp);

	return features;
}
