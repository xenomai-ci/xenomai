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

#include <errno.h>
#include <string.h>
#include <copperplate/threadobj.h>
#include <copperplate/registry-obstack.h>
#include "reference.h"
#include "internal.h"
#include "heap.h"
#include "timer.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_heap Heap management services
 *
 * Region of memory dedicated to real-time allocation
 *
 * Heaps are regions of memory used for dynamic memory allocation in
 * a time-bounded fashion. Blocks of memory are allocated and freed in
 * an arbitrary order and the pattern of allocation and size of blocks
 * is not known until run time.
 *
 * @{
 */
struct syncluster alchemy_heap_table;

static DEFINE_NAME_GENERATOR(heap_namegen, "heap",
			     struct alchemy_heap, name);

DEFINE_SYNC_LOOKUP(heap, RT_HEAP);

#ifdef CONFIG_XENO_REGISTRY

struct heap_waiter_data {
	char name[XNOBJECT_NAME_LEN];
	size_t reqsz;
};

static int prepare_waiter_cache(struct fsobstack *o,
				struct obstack *cache, int item_count)
{
	fsobstack_grow_format(o, "--\n%-10s  %s\n", "[REQ-SIZE]", "[WAITER]");
	obstack_blank(cache, item_count * sizeof(struct heap_waiter_data));

	return 0;
}

static size_t collect_waiter_data(void *p, struct threadobj *thobj)
{
	struct alchemy_heap_wait *wait;
	struct heap_waiter_data data;

	strcpy(data.name, threadobj_get_name(thobj));
	wait = threadobj_get_wait(thobj);
	data.reqsz = wait->size;
	memcpy(p, &data, sizeof(data));

	return sizeof(data);
}

static size_t format_waiter_data(struct fsobstack *o, void *p)
{
	struct heap_waiter_data *data = p;

	fsobstack_grow_format(o, "%9Zu    %s\n",
			      data->reqsz, data->name);

	return sizeof(*data);
}

static struct fsobstack_syncops fill_ops = {
	.prepare_cache = prepare_waiter_cache,
	.collect_data = collect_waiter_data,
	.format_data = format_waiter_data,
};

static int heap_registry_open(struct fsobj *fsobj, void *priv)
{
	size_t usable_mem, used_mem;
	struct fsobstack *o = priv;
	struct alchemy_heap *hcb;
	struct syncstate syns;
	int mode, ret;

	hcb = container_of(fsobj, struct alchemy_heap, fsobj);

	ret = syncobj_lock(&hcb->sobj, &syns);
	if (ret)
		return -EIO;

	usable_mem = heapobj_size(&hcb->hobj);
	used_mem = heapobj_inquire(&hcb->hobj);
	mode = hcb->mode;

	syncobj_unlock(&hcb->sobj, &syns);

	fsobstack_init(o);

	fsobstack_grow_format(o, "%6s  %10s  %9s\n",
			      "[TYPE]", "[TOTALMEM]", "[USEDMEM]");

	fsobstack_grow_format(o, " %s  %10Zu %10Zu\n",
			      mode & H_PRIO ? "PRIO" : "FIFO",
			      usable_mem,
			      used_mem);

	fsobstack_grow_syncobj_grant(o, &hcb->sobj, &fill_ops);

	fsobstack_finish(o);

	return 0;
}

static struct registry_operations registry_ops = {
	.open		= heap_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static void heap_finalize(struct syncobj *sobj)
{
	struct alchemy_heap *hcb;

	hcb = container_of(sobj, struct alchemy_heap, sobj);
	registry_destroy_file(&hcb->fsobj);
	heapobj_destroy(&hcb->hobj);
	xnfree(hcb);
}
fnref_register(libalchemy, heap_finalize);

/**
 * @fn int rt_heap_create(RT_HEAP *heap, const char *name, size_t heapsz, int mode)
 * @brief Create a heap.
 *
 * This routine creates a memory heap suitable for time-bounded
 * allocation requests of RAM chunks. When not enough memory is
 * available, tasks may be blocked until their allocation request can
 * be fulfilled.
 *
 * By default, heaps support allocation of multiple blocks of memory
 * in an arbitrary order. However, it is possible to ask for
 * single-block management by passing the H_SINGLE flag into the @a
 * mode parameter, in which case the entire memory space managed by
 * the heap is made available as a unique block.  In this mode, all
 * allocation requests made through rt_heap_alloc() will return the
 * same block address, pointing at the beginning of the heap memory.
 *
 * @param heap The address of a heap descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * heap. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created heap into the object registry.
 *
 * @param heapsz The size (in bytes) of the memory pool, blocks will
 * be claimed and released to.  This area is not extensible, so this
 * value must be compatible with the highest memory pressure that
 * could be expected. The valid range is between 1 byte and 2Gb.
 *
 * @param mode The heap creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new heap:
 *
 * - H_FIFO makes tasks pend in FIFO order on the heap when waiting
 * for available blocks.
 *
 * - H_PRIO makes tasks pend in priority order on the heap when
 * waiting for available blocks.
 *
 * - H_SINGLE causes the entire heap space to be managed as a single
 * memory block.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mode is invalid, or @a heapsz is zero
 * or larger than 2Gb.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the heap.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered heap.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @note Heaps can be shared by multiple processes which belong to the
 * same Xenomai session.
 */
int rt_heap_create(RT_HEAP *heap,
		   const char *name, size_t heapsz, int mode)
{
	struct alchemy_heap *hcb;
	int sobj_flags = 0, ret;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	if (heapsz == 0 || heapsz >= 1U << 31)
		return -EINVAL;

	if (mode & ~(H_PRIO|H_SINGLE))
		return -EINVAL;

	CANCEL_DEFER(svc);

	ret = -ENOMEM;
	hcb = xnmalloc(sizeof(*hcb));
	if (hcb == NULL)
		goto fail_cballoc;

	/*
	 * The memory pool has to be part of the main heap for proper
	 * sharing between processes.
	 */
	if (heapobj_init(&hcb->hobj, NULL, heapsz))
		goto fail_bufalloc;

	generate_name(hcb->name, name, &heap_namegen);
	hcb->mode = mode;
	hcb->size = heapsz;
	hcb->sba = __moff_nullable(NULL);

	if (mode & H_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	ret = syncobj_init(&hcb->sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libalchemy, heap_finalize));
	if (ret)
		goto fail_syncinit;

	hcb->magic = heap_magic;

	registry_init_file_obstack(&hcb->fsobj, &registry_ops);
	ret = __bt(registry_add_file(&hcb->fsobj, O_RDONLY,
				     "/alchemy/heaps/%s", hcb->name));
	if (ret)
		warning("failed to export heap %s to registry, %s",
			hcb->name, symerror(ret));

	ret = syncluster_addobj(&alchemy_heap_table, hcb->name, &hcb->cobj);
	if (ret)
		goto fail_register;

	heap->handle = mainheap_ref(hcb, uintptr_t);

	CANCEL_RESTORE(svc);

	return 0;

fail_register:
	registry_destroy_file(&hcb->fsobj);
	syncobj_uninit(&hcb->sobj);
fail_syncinit:
	heapobj_destroy(&hcb->hobj);
fail_bufalloc:
	xnfree(hcb);
fail_cballoc:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_heap_delete(RT_HEAP *heap)
 * @brief Delete a heap.
 *
 * This routine deletes a heap object previously created by a call to
 * rt_heap_create(), releasing all tasks currently blocked on it.
 *
 * @param heap The heap descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a valid heap descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_heap_delete(RT_HEAP *heap)
{
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_heap_table, &hcb->cobj);
	hcb->magic = ~heap_magic;
	syncobj_destroy(&hcb->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_heap_alloc(RT_HEAP *heap, size_t size, RTIME timeout, void **blockp)
 * @brief Allocate a block from a heap (with relative scalar timeout).
 *
 * This routine is a variant of rt_heap_alloc_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 * Passing TM_INFINITE in @a timeout causes the caller to block
 * indefinitely until a block is available. Passing TM_NONBLOCK
 * causes the service to return immediately without blocking in case
 * a block is not available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_heap_alloc_until(RT_HEAP *heap, size_t size, RTIME abs_timeout, void **blockp)
 * @brief Allocate a block from a heap (with absolute scalar timeout).
 *
 * This routine is a variant of rt_heap_alloc_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 * Passing TM_INFINITE in @a timeout causes the caller to block
 * indefinitely until a block is available. Passing TM_NONBLOCK
 * causes the service to return immediately without blocking in case
 * a block is not available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_heap_alloc_timed(RT_HEAP *heap, size_t size, const struct timespec *abs_timeout, void **blockp)
 * @brief Allocate a block from a heap.
 *
 * This service allocates a block from a given heap, or returns the
 * address of the single memory segment if H_SINGLE was mentioned in
 * the creation mode to rt_heap_create(). When not enough memory is
 * available on entry to this service, tasks may be blocked until
 * their allocation request can be fulfilled.
 *
 * @param heap The heap descriptor.
 *
 * @param size The requested size (in bytes) of the block. If the heap
 * is managed as a single-block area (H_SINGLE), this value can be
 * either zero, or the same value given to rt_heap_create(). In that
 * case, the same block covering the entire heap space is returned to
 * all callers of this service.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for a block
 * of the requested size to be available from the heap. Passing NULL causes
 * the caller to block indefinitely until a block is available. Passing
 * { .tv_sec = 0, .tv_nsec = 0 } causes the service to return immediately
 * without blocking in case a block is not available.
 *
 * @param blockp A pointer to a memory location which will be written
 * upon success with the address of the allocated block, or the start
 * address of the single memory segment. In the former case, the block
 * can be freed using rt_heap_free().
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before a
 * block is available.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is equal to { .tv_sec
 * = 0, .tv_nsec = 0 } and no block is immediately available on entry
 * to fulfill the allocation request.

 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before a block became available.
 *
 * - -EINVAL is returned if @a heap is not a valid heap descriptor, or
 * @a heap is managed as a single-block area (i.e. H_SINGLE mode) and
 * @a size is non-zero but does not match the original heap size
 * passed to rt_heap_create().
 *
 * - -EIDRM is returned if @a heap is deleted while the caller was
 * waiting for a block. In such event, @a heap is no more valid upon
 * return of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 *
 * @note If shared multi-processing is enabled (i.e. --enable-pshared
 * was passed to the configure script), requests for a block size
 * larger than twice the allocation page size are rounded up to the
 * next page size. The allocation page size is currently 512 bytes
 * long (HOBJ_PAGE_SIZE), which means that any request larger than 1k
 * will be rounded up to the next 512 byte boundary.
 */
int rt_heap_alloc_timed(RT_HEAP *heap,
			size_t size, const struct timespec *abs_timeout,
			void **blockp)
{
	struct alchemy_heap_wait *wait;
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	void *p = NULL;
	int ret = 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	CANCEL_DEFER(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	if (hcb->mode & H_SINGLE) {
		p = __mptr_nullable(hcb->sba);
		if (p)
			goto done;
		if (size == 0)
			size = heapobj_size(&hcb->hobj);
		else if (size != hcb->size) {
			ret = -EINVAL;
			goto done;
		}
		p = heapobj_alloc(&hcb->hobj, size);
		if (p == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		hcb->sba = __moff(p);
		goto done;
	}

	p = heapobj_alloc(&hcb->hobj, size);
	if (p)
		goto done;

	if (alchemy_poll_mode(abs_timeout)) {
		ret = -EWOULDBLOCK;
		goto done;
	}

	wait = threadobj_prepare_wait(struct alchemy_heap_wait);
	wait->size = size;

	ret = syncobj_wait_grant(&hcb->sobj, abs_timeout, &syns);
	if (ret) {
		if (ret == -EIDRM) {
			threadobj_finish_wait();
			goto out;
		}
	} else
		p = __mptr(wait->ptr);

	threadobj_finish_wait();
done:
	*blockp = p;

	put_alchemy_heap(hcb, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_heap_free(RT_HEAP *heap, void *block)
 * @brief Release a block to a heap.
 *
 * This service should be used to release a block to the heap it
 * belongs to. An attempt to fulfill the request of every task blocked
 * on rt_heap_alloc() is made once @a block is returned to the memory
 * pool.
 *
 * @param heap The heap descriptor.
 *
 * @param block The address of the block to free.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a valid heap descriptor, or
 * @a block is not a valid block previously allocated by the
 * rt_heap_alloc() service from @a heap.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_heap_free(RT_HEAP *heap, void *block)
{
	struct alchemy_heap_wait *wait;
	struct threadobj *thobj, *tmp;
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;
	void *ptr;

	CANCEL_DEFER(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	if (hcb->mode & H_SINGLE)
		goto done;

	if (heapobj_validate(&hcb->hobj, block) == 0) {
		ret = -EINVAL;
		goto done;
	}

	heapobj_free(&hcb->hobj, block);

	if (!syncobj_grant_wait_p(&hcb->sobj))
		goto done;
	/*
	 * We might be releasing a block large enough to satisfy
	 * multiple requests, so we iterate over all waiters.
	 */
	syncobj_for_each_grant_waiter_safe(&hcb->sobj, thobj, tmp) {
		wait = threadobj_get_wait(thobj);
		ptr = heapobj_alloc(&hcb->hobj, wait->size);
		if (ptr) {
			wait->ptr = __moff(ptr);
			syncobj_grant_to(&hcb->sobj, thobj);
		}
	}
done:
	put_alchemy_heap(hcb, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_heap_inquire(RT_HEAP *heap, RT_HEAP_INFO *info)
 * @brief Query heap status.
 *
 * This routine returns the status information about @a heap.
 *
 * @param heap The heap descriptor.
 *
 * @param info A pointer to the @ref RT_HEAP_INFO "return
 * buffer" to copy the information to.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a valid heap descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_heap_inquire(RT_HEAP *heap, RT_HEAP_INFO *info)
{
	struct alchemy_heap *hcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	hcb = get_alchemy_heap(heap, &syns, &ret);
	if (hcb == NULL)
		goto out;

	info->nwaiters = syncobj_count_grant(&hcb->sobj);
	info->heapsize = hcb->size;
	info->usablemem = heapobj_size(&hcb->hobj);
	info->usedmem = heapobj_inquire(&hcb->hobj);
	strcpy(info->name, hcb->name);

	put_alchemy_heap(hcb, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_heap_bind(RT_HEAP *heap, const char *name, RTIME timeout)
 * @brief Bind to a heap.
 *
 * This routine creates a new descriptor to refer to an existing heap
 * identified by its symbolic name. If the object does not exist on
 * entry, the caller may block until a heap of the given name is
 * created.
 *
 * @param heap The address of a heap descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the
 * heap to bind to. This string should match the object name
 * argument passed to rt_heap_create().
 *
 * @param timeout The number of clock ticks to wait for the
 * registration to occur (see note). Passing TM_INFINITE causes the
 * caller to block indefinitely until the object is
 * registered. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if the object is not registered on
 * entry.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before the retrieval has completed.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the searched object is not registered on entry.
 *
 * - -ETIMEDOUT is returned if the object cannot be retrieved within
 * the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 *
 * @note The @a timeout value is interpreted as a multiple of the
 * Alchemy clock resolution (see --alchemy-clock-resolution option,
 * defaults to 1 nanosecond).
 */
int rt_heap_bind(RT_HEAP *heap,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_heap_table,
				   timeout,
				   offsetof(struct alchemy_heap, cobj),
				   &heap->handle);
}

/**
 * @fn int rt_heap_unbind(RT_HEAP *heap)
 * @brief Unbind from a heap.
 *
 * @param heap The heap descriptor.
 *
 * This routine releases a previous binding to a heap. After this call
 * has returned, the descriptor is no more valid for referencing this
 * object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_heap_unbind(RT_HEAP *heap)
{
	heap->handle = 0;
	return 0;
}

/** @} */
