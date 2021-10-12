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

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <copperplate/threadobj.h>
#include <copperplate/heapobj.h>
#include <copperplate/registry-obstack.h>
#include "reference.h"
#include "internal.h"
#include "event.h"
#include "timer.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_event Event flag group services
 *
 * Inter-task notification mechanism based on discrete flags
 *
 * An event flag group is a synchronization object represented by a
 * long-word structure; every available bit in this word represents a
 * user-defined event flag.
 *
 * When a bit is set, the associated event is said to have
 * occurred. Xenomai tasks can use this mechanism to signal the
 * occurrence of particular events to other tasks.
 *
 * Tasks can either wait for events to occur in a conjunctive manner
 * (all awaited events must have occurred to satisfy the wait
 * request), or in a disjunctive way (at least one of the awaited
 * events must have occurred to satisfy the wait request).
 *
 * @{
 */

struct syncluster alchemy_event_table;

static DEFINE_NAME_GENERATOR(event_namegen, "event",
			     struct alchemy_event, name);

DEFINE_LOOKUP_PRIVATE(event, RT_EVENT);

#ifdef CONFIG_XENO_REGISTRY

static int event_registry_open(struct fsobj *fsobj, void *priv)
{
	struct eventobj_waitentry *waitlist, *p;
	struct fsobstack *o = priv;
	struct alchemy_event *evcb;
	unsigned int val;
	size_t waitsz;
	int ret;

	evcb = container_of(fsobj, struct alchemy_event, fsobj);

	waitsz = sizeof(*p) * 256;
	waitlist = __STD(malloc(waitsz));
	if (waitlist == NULL)
		return -ENOMEM;

	ret = eventobj_inquire(&evcb->evobj, waitsz, waitlist, &val);
	if (ret < 0)
		goto out;

	fsobstack_init(o);

	fsobstack_grow_format(o, "=%lx\n", val);

	if (ret) {
		fsobstack_grow_format(o, "--\n[WAITER]\n");
		p = waitlist;
		do {
			fsobstack_grow_format(o, "%s\n", p->name);
			p++;
		} while (--ret > 0);
	}

	fsobstack_finish(o);
out:
	__STD(free(waitlist));

	return ret;
}

static struct registry_operations registry_ops = {
	.open		= event_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static void event_finalize(struct eventobj *evobj)
{
	struct alchemy_event *evcb = container_of(evobj, struct alchemy_event, evobj);

	registry_destroy_file(&evcb->fsobj);
	/* We should never fail here, so we backtrace. */
	__bt(syncluster_delobj(&alchemy_event_table, &evcb->cobj));
	evcb->magic = ~event_magic;
	xnfree(evcb);
}
fnref_register(libalchemy, event_finalize);

/**
 * @fn int rt_event_create(RT_EVENT *event, const char *name, unsigned int ivalue, int mode)
 * @brief Create an event flag group.
 *
 * Event groups provide for task synchronization by allowing a set of
 * flags (or "events") to be waited for and posted atomically. An
 * event group contains a mask of received events; an arbitrary set of
 * event flags can be pended or posted in a single operation.
 *
 * @param event The address of an event descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * event. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created event into the object registry.
 *
 * @param ivalue The initial value of the group's event mask.
 *
 * @param mode The event group creation mode. The following flags can
 * be OR'ed into this bitmask:
 *
 * - EV_FIFO makes tasks pend in FIFO order on the event flag group.
 *
 * - EV_PRIO makes tasks pend in priority order on the event flag group.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mode is invalid.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the event flag group.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered event flag group.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @note Event flag groups can be shared by multiple processes which
 * belong to the same Xenomai session.
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_event_create, (RT_EVENT *event, const char *name,
				    unsigned int ivalue, int mode))
#else
int rt_event_create(RT_EVENT *event, const char *name,
		    unsigned int ivalue, int mode)
#endif
{
	int evobj_flags = 0, ret = 0;
	struct alchemy_event *evcb;
	struct service svc;

	if (threadobj_irq_p())
		return -EPERM;

	if (mode & ~EV_PRIO)
		return -EINVAL;

	CANCEL_DEFER(svc);

	evcb = xnmalloc(sizeof(*evcb));
	if (evcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	generate_name(evcb->name, name, &event_namegen);
	if (mode & EV_PRIO)
		evobj_flags = EVOBJ_PRIO;

	ret = eventobj_init(&evcb->evobj, ivalue, evobj_flags,
			    fnref_put(libalchemy, event_finalize));
	if (ret) {
		xnfree(evcb);
		goto out;
	}

	evcb->magic = event_magic;

	registry_init_file_obstack(&evcb->fsobj, &registry_ops);
	ret = __bt(registry_add_file(&evcb->fsobj, O_RDONLY,
				     "/alchemy/events/%s", evcb->name));
	if (ret) {
		warning("failed to export event %s to registry, %s",
			evcb->name, symerror(ret));
		ret = 0;
	}

	ret = syncluster_addobj(&alchemy_event_table, evcb->name, &evcb->cobj);
	if (ret) {
		registry_destroy_file(&evcb->fsobj);
		eventobj_uninit(&evcb->evobj);
		xnfree(evcb);
	} else
		event->handle = mainheap_ref(evcb, uintptr_t);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_delete(RT_EVENT *event)
 * @brief Delete an event flag group.
 *
 * This routine deletes a event flag group previously created by a
 * call to rt_event_create().
 *
 * @param event The event descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a valid event flag group
 * descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_event_delete(RT_EVENT *event)
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	/*
	 * XXX: we rely on copperplate's eventobj to check for event
	 * existence, so we refrain from altering the object memory
	 * until we know it was valid. So the only safe place to
	 * negate the magic tag, deregister from the cluster and
	 * release the memory is in the finalizer routine, which is
	 * only called for valid objects.
	 */
	ret = eventobj_destroy(&evcb->evobj);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_wait(RT_EVENT *event, unsigned int mask, unsigned int *mask_r, int mode, RTIME timeout)
 * @brief Wait for an arbitrary set of events (with relative scalar timeout).
 *
 * This routine is a variant of rt_event_wait_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of bits to wait for.
 *
 * @param mask_r The value of the event mask at the time the task was
 * readied.
 *
 * @param mode The pend mode.
 *
 * @param timeout A delay expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * the request is satisfied. Passing TM_NONBLOCK causes the service
 * to return without blocking in case the request cannot be satisfied
 * immediately.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_event_wait_until(RT_EVENT *event, unsigned int mask, unsigned int *mask_r, int mode, RTIME abs_timeout)
 * @brief Wait for an arbitrary set of events (with absolute scalar timeout).
 *
 * This routine is a variant of rt_event_wait_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of bits to wait for.
 *
 * @param mask_r The value of the event mask at the time the task was
 * readied.
 *
 * @param mode The pend mode.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * the request is satisfied. Passing TM_NONBLOCK causes the service
 * to return without blocking in case the request cannot be satisfied
 * immediately.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn int rt_event_wait_timed(RT_EVENT *event, unsigned int mask, unsigned int *mask_r, int mode, const struct timespec *abs_timeout)
 * @brief Wait for an arbitrary set of events.
 *
 * Waits for one or more events to be signaled in @a event, or until a
 * timeout elapses.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of bits to wait for. Passing zero causes this
 * service to return immediately with a success value; the current
 * value of the event mask is also copied to @a mask_r.
 *
 * @param mask_r The value of the event mask at the time the task was
 * readied.
 *
 * @param mode The pend mode. The following flags can be OR'ed into
 * this bitmask, each of them affecting the operation:
 *
 * - EV_ANY makes the task pend in disjunctive mode (i.e. OR); this
 * means that the request is fulfilled when at least one bit set into
 * @a mask is set in the current event mask.
 *
 * - EV_ALL makes the task pend in conjunctive mode (i.e. AND); this
 * means that the request is fulfilled when at all bits set into @a
 * mask are set in the current event mask.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for the
 * request to be satisfied. Passing NULL causes the caller to block
 * indefinitely until the request is satisfied. Passing
 * { .tv_sec = 0, .tv_nsec = 0 } causes the service to return without
 * blocking in case the request cannot be satisfied immediately.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before the
 * request is satisfied.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and the requested flags are not set on entry to the
 * call.

 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before the request is satisfied.
 *
 * - -EINVAL is returned if @a mode is invalid, or @a event is not a
 * valid event flag group descriptor.
 *
 * - -EIDRM is returned if @a event is deleted while the caller was
 * sleeping on it. In such a case, @a event is no more valid upon
 * return of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
int rt_event_wait_timed(RT_EVENT *event,
			unsigned int mask, unsigned int *mask_r,
			int mode, const struct timespec *abs_timeout)
{
	int evobj_mode = 0, ret = 0;
	struct alchemy_event *evcb;
	struct service svc;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	if (mode & ~EVOBJ_ANY)
		return -EINVAL;

	CANCEL_DEFER(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	if (mode & EV_ANY)
		evobj_mode = EVOBJ_ANY;

	ret = eventobj_wait(&evcb->evobj, mask, mask_r,
			    evobj_mode, abs_timeout);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_signal(RT_EVENT *event, unsigned int mask)
 * @brief Signal an event.
 *
 * Post a set of flags to @a event. All tasks having their wait
 * request satisfied as a result of this operation are immediately
 * readied.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of events to be posted.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not an event flag group
 * descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_event_signal,
	     (RT_EVENT *event, unsigned int mask))
#else
int rt_event_signal(RT_EVENT *event, unsigned int mask)
#endif
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_post(&evcb->evobj, mask);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_clear(RT_EVENT *event,unsigned int mask,unsigned int *mask_r)
 * @brief Clear event flags.
 *
 * This routine clears a set of flags from @a event.
 *
 * @param event The event descriptor.
 *
 * @param mask The set of event flags to be cleared.
 *
 * @param mask_r If non-NULL, @a mask_r is the address of a memory
 * location which will receive the previous value of the event flag
 * group before the flags are cleared.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a valid event flag group
 * descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_event_clear,
	     (RT_EVENT *event, unsigned int mask, unsigned int *mask_r))
#else
int rt_event_clear(RT_EVENT *event,
		   unsigned int mask, unsigned int *mask_r)
#endif
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_clear(&evcb->evobj, mask, mask_r);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_inquire(RT_EVENT *event, RT_EVENT_INFO *info)
 * @brief Query event flag group status.
 *
 * This routine returns the status information about @a event.
 *
 * @param event The event descriptor.
 *
 * @param info A pointer to the @ref RT_EVENT_INFO "return
 * buffer" to copy the information to.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a valid event flag group
 * descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_event_inquire(RT_EVENT *event, RT_EVENT_INFO *info)
{
	struct alchemy_event *evcb;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	evcb = find_alchemy_event(event, &ret);
	if (evcb == NULL)
		goto out;

	ret = eventobj_inquire(&evcb->evobj, 0, NULL, &info->value);
	if (ret < 0)
		goto out;

	strcpy(info->name, evcb->name);
	info->nwaiters = ret;
	ret = 0;
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_event_bind(RT_EVENT *event, const char *name, RTIME timeout)
 * @brief Bind to an event flag group.
 *
 * This routine creates a new descriptor to refer to an existing event
 * flag group identified by its symbolic name. If the object does not
 * exist on entry, the caller may block until an event flag group of
 * the given name is created.
 *
 * @param event The address of an event flag group descriptor filled
 * in by the operation. Contents of this memory is undefined upon
 * failure.
 *
 * @param name A valid NULL-terminated name which identifies the event
 * flag group to bind to. This string should match the object name
 * argument passed to rt_event_create().
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
int rt_event_bind(RT_EVENT *event,
		  const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_event_table,
				   timeout,
				   offsetof(struct alchemy_event, cobj),
				   &event->handle);
}

/**
 * @fn int rt_event_unbind(RT_EVENT *event)
 * @brief Unbind from an event flag group.
 *
 * @param event The event descriptor.
 *
 * This routine releases a previous binding to an event flag
 * group. After this call has returned, the descriptor is no more
 * valid for referencing this object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_event_unbind(RT_EVENT *event)
{
	event->handle = 0;
	return 0;
}

/** @} */
