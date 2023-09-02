/*
 * Copyright (C) 2011 Gilles Chanteperdrix <gch@xenomai.org>.
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
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <cobalt/wrappers.h>
#include <cobalt/ticks.h>
#include <asm/xenomai/syscall.h>
#include <asm/xenomai/features.h>
#include <asm/xenomai/uapi/fptest.h>
#include "internal.h"

void cobalt_arch_check_features(struct cobalt_featinfo *finfo)
{
}

int cobalt_fp_detect(void)
{
	char buffer[1024];
	int features = 0;
	FILE *fp;

	fp = fopen("/proc/cpuinfo", "r");
	if (fp == NULL)
		return 0;

	while (fgets(buffer, sizeof(buffer), fp)) {
		if(strncmp(buffer, "Features", sizeof("Features") - 1))
			continue;
		if (strstr(buffer, "fp")) {
			features |= __COBALT_HAVE_FPU;
			break;
		}
	}

	fclose(fp);

	return features;
}
