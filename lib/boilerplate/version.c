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

#include <xeno_config.h>
#include "git-stamp.h"

#ifndef GIT_STAMP
#define devel_suffix  ""
#else
#define devel_suffix  " -- " GIT_STAMP
#endif

#ifdef CONFIG_XENO_COBALT
#define core_suffix "/cobalt v"
#else /* CONFIG_XENO_MERCURY */
#define core_suffix "/mercury v"
#endif

const char *xenomai_version_string = PACKAGE_NAME \
	core_suffix PACKAGE_VERSION devel_suffix;

#ifdef __PROGRAM__

#include <stdio.h>
#include <string.h>

int main(int argc, char *const argv[])
{
	int ret = puts(xenomai_version_string) == EOF;

	if (ret == 0 && argc > 1 &&
	    (strcmp(argv[1], "-a") == 0 || strcmp(argv[1], "--all") == 0))
		printf("Target: %s\nCompiler: %s\nBuild args: %s\n",
		       CONFIG_XENO_HOST_STRING,
		       CONFIG_XENO_COMPILER,
		       CONFIG_XENO_BUILD_ARGS);

	return ret;
}

#endif
