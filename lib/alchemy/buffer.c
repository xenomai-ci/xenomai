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
#include <copperplate/threadobj.h>
#include "reference.h"
#include "internal.h"
#include "buffer.h"
#include "timer.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_buffer Buffer services
 *
 * Lightweight FIFO IPC mechanism
 *
 * A buffer is a lightweight IPC mechanism, implementing a fast,
 * one-way producer-consumer data path. All messages written are
 * buffered in a single memory area in strict FIFO order, until read
 * either in blocking or non-blocking mode.
 *
 * Message are always atomically handled on the write side (i.e. no
 * interleave, no short writes), whilst only complete messages are
 * normally returned to the read side. However, short reads may happen
 * under a well-defined situation (see note in rt_buffer_read()),
 * albeit they can be fully avoided by proper use of the buffer.
 *
 * @{
 */
struct syncluster alchemy_buffer_table;

static DEFINE_NAME_GENERATOR(buffer_namegen, "buffer",
			     struct alchemy_buffer, name);

DEFINE_SYNC_LOOKUP(buffer, RT_BUFFER);

#ifdef CONFIG_XENO_REGISTRY

static inline
void prepare_waiter_cache(struct obstack *cache, int item_count)
{
	const struct alchemy_buffer *bcb;
	obstack_blank(cache, item_count * sizeof(bcb->name));
}

static int prepare_grant_cache(struct fsobstack *o,
			       struct obstack *cache, int item_count)
{
	fsobstack_grow_format(o, "--\n[INPUT-WAIT]\n");
	prepare_waiter_cache(cache, item_count);

	return 0;
}

static int prepare_drain_cache(struct fsobstack *o,
			       struct obstack *cache, int item_count)
{
	fsobstack_grow_format(o, "--\n[OUTPUT-WAIT]\n");
	prepare_waiter_cache(cache, item_count);

	return 0;
}

static size_t collect_waiter_data(void *p, struct threadobj *thobj)
{
	const char *name = threadobj_get_name(thobj);
	int len = strlen(name);

	strcpy(p, name);
	*(char *)(p + len) = '\n';

	return len + 1;
}

static struct fsobstack_syncops fill_grant_ops = {
	.prepare_cache = prepare_grant_cache,
	.collect_data = collect_waiter_data,
};

static struct fsobstack_syncops fill_drain_ops = {
	.prepare_cache = prepare_drain_cache,
	.collect_data = collect_waiter_data,
};

static int buffer_registry_open(struct fsobj *fsobj, void *priv)
{
	struct fsobstack *o = priv;
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	size_t bufsz, fillsz;
	int ret, mode;

	bcb = container_of(fsobj, struct alchemy_buffer, fsobj);

	ret = syncobj_lock(&bcb->sobj, &syns);
	if (ret)
		return -EIO;

	bufsz = bcb->bufsz;
	fillsz = bcb->fillsz;
	mode = bcb->mode;

	syncobj_unlock(&bcb->sobj, &syns);

	fsobstack_init(o);

	fsobstack_grow_format(o, "%6s  %10s  %9s\n",
			      "[TYPE]", "[TOTALMEM]", "[USEDMEM]");

	fsobstack_grow_format(o, " %s   %9Zu  %9Zu\n",
			      mode & B_PRIO ? "PRIO" : "FIFO",
			      bufsz, fillsz);

	fsobstack_grow_syncobj_grant(o, &bcb->sobj, &fill_grant_ops);
	fsobstack_grow_syncobj_drain(o, &bcb->sobj, &fill_drain_ops);

	fsobstack_finish(o);

	return 0;
}

static struct registry_operations registry_ops = {
	.open		= buffer_registry_open,
	.release	= fsobj_obstack_release,
	.read		= fsobj_obstack_read
};

#else /* !CONFIG_XENO_REGISTRY */

static struct registry_operations registry_ops;

#endif /* CONFIG_XENO_REGISTRY */

static void buffer_finalize(struct syncobj *sobj)
{
	struct alchemy_buffer *bcb;

	bcb = container_of(sobj, struct alchemy_buffer, sobj);
	registry_destroy_file(&bcb->fsobj);
	xnfree(__mptr(bcb->buf));
	xnfree(bcb);
}
fnref_register(libalchemy, buffer_finalize);

/**
 * @fn int rt_buffer_create(RT_BUFFER *bf, const char *name, size_t bufsz, int mode)
 * @brief Create an IPC buffer.
 *
 * This routine creates an IPC object that allows tasks to send and
 * receive data asynchronously via a memory buffer. Data may be of an
 * arbitrary length, albeit this IPC is best suited for small to
 * medium-sized messages, since data always have to be copied to the
 * buffer during transit. Large messages may be more efficiently
 * handled by message queues (RT_QUEUE).
 *
 * @param bf The address of a buffer descriptor which can be later
 * used to identify uniquely the created object, upon success of this
 * call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * buffer. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created buffer into the object registry.
 *
 * @param bufsz The size of the buffer space available to hold
 * data. The required memory is obtained from the main heap.
 *
 * @param mode The buffer creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new buffer:
 *
 * - B_FIFO makes tasks pend in FIFO order for reading data from the
 *   buffer.
 *
 * - B_PRIO makes tasks pend in priority order for reading data from
 *   the buffer.
 *
 * This parameter also applies to tasks blocked on the buffer's write
 * side (see rt_buffer_write()).
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mode is invalid or @a bufsz is zero.
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the buffer.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered buffer.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 *
 * @note Buffers can be shared by multiple processes which belong to
 * the same Xenomai session.
 */
int rt_buffer_create(RT_BUFFER *bf, const char *name,
		     size_t bufsz, int mode)
{
	struct alchemy_buffer *bcb;
	struct service svc;
	int sobj_flags = 0;
	void *buf;
	int ret;

	if (threadobj_irq_p())
		return -EPERM;

	if (bufsz == 0 || (mode & ~B_PRIO) != 0)
		return -EINVAL;

	CANCEL_DEFER(svc);

	bcb = xnmalloc(sizeof(*bcb));
	if (bcb == NULL) {
		ret = __bt(-ENOMEM);
		goto fail;
	}

	buf = xnmalloc(bufsz);
	if (buf == NULL) {
		ret = __bt(-ENOMEM);
		goto fail_bufalloc;
	}

	bcb->buf = __moff(buf);
	generate_name(bcb->name, name, &buffer_namegen);
	bcb->mode = mode;
	bcb->bufsz = bufsz;
	bcb->rdoff = 0;
	bcb->wroff = 0;
	bcb->fillsz = 0;
	if (mode & B_PRIO)
		sobj_flags = SYNCOBJ_PRIO;

	ret = syncobj_init(&bcb->sobj, CLOCK_COPPERPLATE, sobj_flags,
			   fnref_put(libalchemy, buffer_finalize));
	if (ret)
		goto fail_syncinit;

	bcb->magic = buffer_magic;

	registry_init_file_obstack(&bcb->fsobj, &registry_ops);
	ret = __bt(registry_add_file(&bcb->fsobj, O_RDONLY,
				     "/alchemy/buffers/%s", bcb->name));
	if (ret)
		warning("failed to export buffer %s to registry, %s",
			bcb->name, symerror(ret));

	ret = syncluster_addobj(&alchemy_buffer_table, bcb->name, &bcb->cobj);
	if (ret)
		goto fail_register;

	bf->handle = mainheap_ref(bcb, uintptr_t);

	CANCEL_RESTORE(svc);

	return 0;

fail_register:
	registry_destroy_file(&bcb->fsobj);
	syncobj_uninit(&bcb->sobj);
fail_syncinit:
	xnfree(buf);
fail_bufalloc:
	xnfree(bcb);
fail:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_buffer_delete(RT_BUFFER *bf)
 * @brief Delete an IPC buffer.
 *
 * This routine deletes a buffer object previously created by a call
 * to rt_buffer_create().
 *
 * @param bf The buffer descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a valid buffer descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_buffer_delete(RT_BUFFER *bf)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	syncluster_delobj(&alchemy_buffer_table, &bcb->cobj);
	bcb->magic = ~buffer_magic;
	syncobj_destroy(&bcb->sobj, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_read(RT_BUFFER *bf, void *ptr, size_t len, RTIME timeout)
 * @brief Read from an IPC buffer (with relative scalar timeout).
 *
 * This routine is a variant of rt_buffer_read_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr A pointer to a memory area which will be written upon
 * success with the received data.
 *
 * @param len The length in bytes of the memory area pointed to by @a
 * ptr.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until enough
 * data is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case not enough data is
 * available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn ssize_t rt_buffer_read_until(RT_BUFFER *bf, void *ptr, size_t len, RTIME abs_timeout)
 * @brief Read from an IPC buffer (with absolute scalar timeout).
 *
 * This routine is a variant of rt_buffer_read_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr A pointer to a memory area which will be written upon
 * success with the received data.
 *
 * @param len The length in bytes of the memory area pointed to by @a
 * ptr.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * enough data is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case not enough data is
 * available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn ssize_t rt_buffer_read_timed(RT_BUFFER *bf, void *ptr, size_t len, const struct timespec *abs_timeout)
 * @brief Read from an IPC buffer.
 *
 * This routine reads the next message from the specified buffer. If
 * no message is available on entry, the caller is allowed to block
 * until enough data is written to the buffer, or a timeout elapses.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr A pointer to a memory area which will be written upon
 * success with the received data.
 *
 * @param len The length in bytes of the memory area pointed to by @a
 * ptr. Under normal circumstances, rt_buffer_read_timed() only
 * returns entire messages as specified by the @a len argument, or an
 * error value. However, short reads are allowed when a potential
 * deadlock situation is detected (see note below).
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for a
 * message to be available from the buffer. Passing NULL causes the caller
 * to block indefinitely until enough data is available. Passing
 * { .tv_sec = 0, .tv_nsec = 0 } causes the service to return immediately
 * without blocking in case not enough data is available.
 *
 * @return The number of bytes read from the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before a
 * complete message arrives.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and not enough data is immediately available on
 * entry to form a complete message.

 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before enough data became available to form a complete
 * message.
 *
 * - -EINVAL is returned if @a bf is not a valid buffer descriptor, or
 * @a len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is deleted while the caller was
 * waiting for data. In such event, @a bf is no more valid upon return
 * of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @note A short read (i.e. fewer bytes returned than requested by @a
 * len) may happen whenever a pathological use of the buffer is
 * encountered. This condition only arises when the system detects
 * that one or more writers are waiting for sending data, while a
 * reader would have to wait for receiving a complete message at the
 * same time. For instance, consider the following sequence, involving
 * a 1024-byte buffer (bf) and two threads:
 *
 * writer thread > rt_write_buffer(&bf, ptr, 1, TM_INFINITE);
 *        (one byte to read, 1023 bytes available for sending)
 * writer thread > rt_write_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (writer blocks - no space for another 1024-byte message)
 * reader thread > rt_read_buffer(&bf, ptr, 1024, TM_INFINITE);
 *        (short read - a truncated (1-byte) message is returned)
 *
 * In order to prevent both threads to wait for each other
 * indefinitely, a short read is allowed, which may be completed by a
 * subsequent call to rt_buffer_read() or rt_buffer_read_until().  If
 * that case arises, thread priorities, buffer and/or message lengths
 * should likely be fixed, in order to eliminate such condition.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
ssize_t rt_buffer_read_timed(RT_BUFFER *bf,
			     void *ptr, size_t size,
			     const struct timespec *abs_timeout)
{
	struct alchemy_buffer_wait *wait = NULL;
	struct alchemy_buffer *bcb;
	struct threadobj *thobj;
	size_t len, rbytes, n;
	struct syncstate syns;
	struct service svc;
	size_t rdoff;
	int ret = 0;
	void *p;

	len = size;
	if (len == 0)
		return 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	CANCEL_DEFER(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	/*
	 * We may only return complete messages to readers, so there
	 * is no point in waiting for messages which are larger than
	 * what the buffer can hold.
	 */
	if (len > bcb->bufsz) {
		ret = -EINVAL;
		goto done;
	}
redo:
	for (;;) {
		/*
		 * We should be able to read a complete message of the
		 * requested length, or block.
		 */
		if (bcb->fillsz < len)
			goto wait;

		/* Read from the buffer in a circular way. */
		rdoff = bcb->rdoff;
		rbytes = len;
		p = ptr;

		do {
			if (rdoff + rbytes > bcb->bufsz)
				n = bcb->bufsz - rdoff;
			else
				n = rbytes;
			memcpy(p, __mptr(bcb->buf) + rdoff, n);
			p += n;
			rdoff = (rdoff + n) % bcb->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bcb->fillsz -= len;
		bcb->rdoff = rdoff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads waiting for the buffer to
		 * drain, if we freed enough room for the leading one
		 * to post its message.
		 */
		thobj = syncobj_peek_drain(&bcb->sobj);
		if (thobj == NULL)
			goto done;

		wait = threadobj_get_wait(thobj);
		if (wait->size + bcb->fillsz <= bcb->bufsz)
			syncobj_drain(&bcb->sobj);

		goto done;
	wait:
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}

		/*
		 * Check whether writers are already waiting for
		 * sending data, while we are about to wait for
		 * receiving some. In such a case, we have a
		 * pathological use of the buffer. We must allow for a
		 * short read to prevent a deadlock.
		 */
		if (bcb->fillsz > 0 && syncobj_count_drain(&bcb->sobj)) {
			len = bcb->fillsz;
			goto redo;
		}

		if (wait == NULL)
			wait = threadobj_prepare_wait(struct alchemy_buffer_wait);

		wait->size = len;

		ret = syncobj_wait_grant(&bcb->sobj, abs_timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}
done:
	put_alchemy_buffer(bcb, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn ssize_t rt_buffer_write(RT_BUFFER *bf, const void *ptr, size_t len, RTIME timeout)
 * @brief Write to an IPC buffer (with relative scalar timeout).
 *
 * This routine is a variant of rt_buffer_write_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param len The length in bytes of the message data.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until enough
 * buffer space is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case of buffer space
 * shortage.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn ssize_t rt_buffer_write_until(RT_BUFFER *bf, const void *ptr, size_t len, RTIME abs_timeout)
 * @brief Write to an IPC buffer (with absolute scalar timeout).
 *
 * This routine is a variant of rt_buffer_write_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param len The length in bytes of the message data.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * enough buffer space is available. Passing TM_NONBLOCK causes the
 * service to return immediately without blocking in case of buffer
 * space shortage.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn ssize_t rt_buffer_write_timed(RT_BUFFER *bf, const void *ptr, size_t len, const struct timespec *abs_timeout)
 * @brief Write to an IPC buffer.
 *
 * This routine writes a message to the specified buffer. If not
 * enough buffer space is available on entry to hold the message, the
 * caller is allowed to block until enough room is freed, or a timeout
 * elapses, whichever comes first.
 *
 * @param bf The buffer descriptor.
 *
 * @param ptr The address of the message data to be written to the
 * buffer.
 *
 * @param len The length in bytes of the message data. Zero is a valid
 * value, in which case the buffer is left untouched, and zero is
 * returned to the caller.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for enough
 * buffer space to be available to hold the message. Passing NULL causes
 * the caller to block indefinitely until enough buffer space is
 * available. Passing { .tv_sec = 0, .tv_nsec = 0 } causes the service
 * to return immediately without blocking in case of buffer space
 * shortage.
 *
 * @return The number of bytes written to the buffer is returned upon
 * success. Otherwise:
 *
 * - -ETIMEDOUT is returned if the absolute @a abs_timeout date is
 * reached before enough buffer space is available to hold the
 * message.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and no buffer space is immediately available on
 * entry to hold the message.

 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before enough buffer space became available to hold
 * the message.
 *
 * - -EINVAL is returned if @a bf is not a valid buffer descriptor, or
 * @a len is greater than the actual buffer length.
 *
 * - -EIDRM is returned if @a bf is deleted while the caller was
 * waiting for buffer space. In such event, @a bf is no more valid
 * upon return of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
ssize_t rt_buffer_write_timed(RT_BUFFER *bf,
			      const void *ptr, size_t size,
			      const struct timespec *abs_timeout)
{
	struct alchemy_buffer_wait *wait = NULL;
	struct alchemy_buffer *bcb;
	struct threadobj *thobj;
	size_t len, rbytes, n;
	struct syncstate syns;
	struct service svc;
	const void *p;
	size_t wroff;
	int ret = 0;

	len = size;
	if (len == 0)
		return 0;

	if (!threadobj_current_p() && !alchemy_poll_mode(abs_timeout))
		return -EPERM;

	CANCEL_DEFER(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	/*
	 * We may only send complete messages, so there is no point in
	 * accepting messages which are larger than what the buffer
	 * can hold.
	 */
	if (len > bcb->bufsz) {
		ret = -EINVAL;
		goto done;
	}

	for (;;) {
		/*
		 * We should be able to write the entire message at
		 * once, or block.
		 */
		if (bcb->fillsz + len > bcb->bufsz)
			goto wait;

		/* Write to the buffer in a circular way. */
		wroff = bcb->wroff;
		rbytes = len;
		p = ptr;

		do {
			if (wroff + rbytes > bcb->bufsz)
				n = bcb->bufsz - wroff;
			else
				n = rbytes;

			memcpy(__mptr(bcb->buf) + wroff, p, n);
			p += n;
			wroff = (wroff + n) % bcb->bufsz;
			rbytes -= n;
		} while (rbytes > 0);

		bcb->fillsz += len;
		bcb->wroff = wroff;
		ret = (ssize_t)len;

		/*
		 * Wake up all threads waiting for input, if we
		 * accumulated enough data to feed the leading one.
		 */
		thobj = syncobj_peek_grant(&bcb->sobj);
		if (thobj == NULL)
			goto done;

		wait = threadobj_get_wait(thobj);
		if (wait->size <= bcb->fillsz)
			syncobj_grant_all(&bcb->sobj);

		goto done;
	wait:
		if (alchemy_poll_mode(abs_timeout)) {
			ret = -EWOULDBLOCK;
			goto done;
		}

		if (wait == NULL)
			wait = threadobj_prepare_wait(struct alchemy_buffer_wait);

		wait->size = len;

		/*
		 * Check whether readers are already waiting for
		 * receiving data, while we are about to wait for
		 * sending some. In such a case, we have the converse
		 * pathological use of the buffer. We must kick
		 * readers to allow for a short read to prevent a
		 * deadlock.
		 *
		 * XXX: instead of broadcasting a general wake up
		 * event, we could be smarter and wake up only the
		 * number of waiters required to consume the amount of
		 * data we want to send, but this does not seem worth
		 * the burden: this is an error condition, we just
		 * have to mitigate its effect, avoiding a deadlock.
		 */
		if (bcb->fillsz > 0 && syncobj_count_grant(&bcb->sobj))
			syncobj_grant_all(&bcb->sobj);

		ret = syncobj_wait_drain(&bcb->sobj, abs_timeout, &syns);
		if (ret) {
			if (ret == -EIDRM)
				goto out;
			break;
		}
	}
done:
	put_alchemy_buffer(bcb, &syns);
out:
	if (wait)
		threadobj_finish_wait();

	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_buffer_clear(RT_BUFFER *bf)
 * @brief Clear an IPC buffer.
 *
 * This routine empties a buffer from any data.
 *
 * @param bf The buffer descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a valid buffer descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_buffer_clear(RT_BUFFER *bf)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	bcb->wroff = 0;
	bcb->rdoff = 0;
	bcb->fillsz = 0;
	syncobj_drain(&bcb->sobj);

	put_alchemy_buffer(bcb, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
 * @brief Query buffer status.
 *
 * This routine returns the status information about the specified
 * buffer.
 *
 * @param bf The buffer descriptor.
 *
 * @param info A pointer to the @ref RT_BUFFER_INFO "return
 * buffer" to copy the information to.
 *
 * @return Zero is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a bf is not a valid buffer descriptor.
 *
 * @apitags{unrestricted, switch-primary}
 */
int rt_buffer_inquire(RT_BUFFER *bf, RT_BUFFER_INFO *info)
{
	struct alchemy_buffer *bcb;
	struct syncstate syns;
	struct service svc;
	int ret = 0;

	CANCEL_DEFER(svc);

	bcb = get_alchemy_buffer(bf, &syns, &ret);
	if (bcb == NULL)
		goto out;

	info->iwaiters = syncobj_count_grant(&bcb->sobj);
	info->owaiters = syncobj_count_drain(&bcb->sobj);
	info->totalmem = bcb->bufsz;
	info->availmem = bcb->bufsz - bcb->fillsz;
	strcpy(info->name, bcb->name);

	put_alchemy_buffer(bcb, &syns);
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn int rt_buffer_bind(RT_BUFFER *bf, const char *name, RTIME timeout)
 * @brief Bind to an IPC buffer.
 *
 * This routine creates a new descriptor to refer to an existing IPC
 * buffer identified by its symbolic name. If the object does not
 * exist on entry, the caller may block until a buffer of the given
 * name is created.
 *
 * @param bf The address of a buffer descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the
 * buffer to bind to. This string should match the object name
 * argument passed to rt_buffer_create().
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
int rt_buffer_bind(RT_BUFFER *bf,
		   const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_buffer_table,
				   timeout,
				   offsetof(struct alchemy_buffer, cobj),
				   &bf->handle);
}

/**
 * @fn int rt_buffer_unbind(RT_BUFFER *bf)
 * @brief Unbind from an IPC buffer.
 *
 * @param bf The buffer descriptor.
 *
 * This routine releases a previous binding to an IPC buffer. After
 * this call has returned, the descriptor is no more valid for
 * referencing this object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_buffer_unbind(RT_BUFFER *bf)
{
	bf->handle = 0;
	return 0;
}

/** @} */
