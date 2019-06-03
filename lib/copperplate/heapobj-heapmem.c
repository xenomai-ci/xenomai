/*
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdlib.h>
#include "boilerplate/heapmem.h"
#include "copperplate/heapobj.h"
#include "copperplate/debug.h"
#include "copperplate/tunables.h"
#include "xenomai/init.h"

#define MIN_HEAPMEM_HEAPSZ  (64 * 1024)

struct heap_memory heapmem_main;

int __heapobj_init_private(struct heapobj *hobj, const char *name,
			   size_t size, void *mem)
{
	struct heap_memory *heap;
	void *_mem = mem;
	int ret;

	heap = malloc(sizeof(*heap));
	if (heap == NULL)
		return -ENOMEM;

	if (mem == NULL) {
		size = HEAPMEM_ARENA_SIZE(size); /* Count meta-data in. */
		mem = malloc(size);
		if (mem == NULL) {
			free(heap);
			return -ENOMEM;
		}
	}
	
	if (name)
		snprintf(hobj->name, sizeof(hobj->name), "%s", name);
	else
		snprintf(hobj->name, sizeof(hobj->name), "%p", hobj);

	hobj->pool = heap;
	hobj->size = size;

	ret = heapmem_init(hobj->pool, mem, size);
	if (ret) {
		if (_mem == NULL)
			free(mem);
		free(heap);
		return ret;
	}

	return 0;
}

int heapobj_init_array_private(struct heapobj *hobj, const char *name,
			       size_t size, int elems)
{
	size_t log2 = sizeof(size) * CHAR_BIT - 1 -
		xenomai_count_leading_zeros(size);

	/*
	 * Heapmem aligns individual object sizes on the next ^2
	 * boundary, do likewise when determining the overall heap
	 * size, so that we can allocate as many as @elems items.
	 */
	if (size & (size - 1))
		log2++;

	size = 1 << log2;

	return __bt(__heapobj_init_private(hobj, name,
					   size * elems, NULL));
}

int heapobj_pkg_init_private(void)
{
	size_t size;
	void *mem;
	int ret;

#ifdef CONFIG_XENO_PSHARED
	size = MIN_HEAPMEM_HEAPSZ;
#else
	size = __copperplate_setup_data.mem_pool;
	if (size < MIN_HEAPMEM_HEAPSZ)
		size = MIN_HEAPMEM_HEAPSZ;
#endif
	size = HEAPMEM_ARENA_SIZE(size);
	mem = malloc(size);
	if (mem == NULL)
		return -ENOMEM;

	ret = heapmem_init(&heapmem_main, mem, size);
	if (ret) {
		free(mem);
		return ret;
	}

	return 0;
}
