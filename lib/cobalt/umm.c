/*
 * Copyright (C) 2009 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <rtdm/rtdm.h>
#include <cobalt/uapi/kernel/heap.h>
#include <asm/xenomai/syscall.h>
#include "current.h"
#include "umm.h"
#include "internal.h"

void *cobalt_umm_private = NULL;

void *cobalt_umm_shared = NULL;

static pthread_once_t init_bind_once = PTHREAD_ONCE_INIT;

static uint32_t private_size;

static void *__map_umm(const char *name, uint32_t *size_r)
{
	struct cobalt_memdev_stat statbuf;
	int fd, ret;
	void *addr;

	fd = __RT(open(name, O_RDWR));
	if (fd < 0) {
		early_warning("cannot open RTDM device %s: %s", name,
			      strerror(errno));
		return MAP_FAILED;
	}

	ret = __RT(ioctl(fd, MEMDEV_RTIOC_STAT, &statbuf));
	if (ret) {
		__RT(close(fd));
		early_warning("failed getting status of %s: %s",
			      name, strerror(errno));
		return MAP_FAILED;
	}

	addr = __RT(mmap(NULL, statbuf.size, PROT_READ|PROT_WRITE,
			 MAP_SHARED, fd, 0));
	__RT(close(fd));

	*size_r = statbuf.size;

	return addr;
}

#define map_umm(__name, __size_r)  __map_umm("/dev/rtdm/" __name, __size_r)

void cobalt_unmap_umm(void)
{
	void *addr;

	/*
	 * Remapping the private heap must be done after the process
	 * has re-attached to the Cobalt core, in order to reinstate a
	 * proper private heap, Otherwise the global heap would be
	 * used instead, leading to unwanted effects.
	 *
	 * On machines without an MMU, there is no such thing as fork.
	 *
	 * We replace former mappings with an invalid one, to detect
	 * any spurious late access.
	 */
	addr = __STD(mmap(cobalt_umm_private,
			  private_size, PROT_NONE,
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0));

	if (addr != cobalt_umm_private)
		munmap(cobalt_umm_private, private_size);

	cobalt_umm_private = NULL;
	init_bind_once = PTHREAD_ONCE_INIT;
}

/*
 * Will be called once on behalf of xenomai_init(), and when
 * re-binding after a fork.
 */
static void init_bind(void)
{
	cobalt_umm_private = map_umm(COBALT_MEMDEV_PRIVATE, &private_size);
	if (cobalt_umm_private == MAP_FAILED) {
		early_warning("cannot map private umm area: %s",
			      strerror(errno));
		early_panic("(CONFIG_DEVTMPFS_MOUNT not enabled?)");
	}
}

/* Will be called only once, upon call to xenomai_init(). */
void cobalt_init_umm(void)
{
	uint32_t size;

	pthread_once(&init_bind_once, init_bind);

	cobalt_umm_shared = map_umm(COBALT_MEMDEV_SHARED, &size);
	if (cobalt_umm_shared == MAP_FAILED)
		early_panic("cannot map shared umm area: %s",
			    strerror(errno));
}
