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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "rtdm/ipc.h"
#include "copperplate/threadobj.h"
#include "copperplate/heapobj.h"
#include "copperplate/cluster.h"
#include "reference.h"
#include "internal.h"
#include "pipe.h"
#include "timer.h"

/**
 * @ingroup alchemy
 * @defgroup alchemy_pipe Message pipe services
 *
 * Two-way communication channel between Xenomai & Linux domains
 *
 * A message pipe is a two-way communication channel between Xenomai
 * threads and normal Linux threads using regular file I/O operations
 * on a pseudo-device. Pipes can be operated in a message-oriented
 * fashion so that message boundaries are preserved, and also in
 * byte-oriented streaming mode from real-time to normal Linux
 * threads for optimal throughput.
 *
 * Xenomai threads open their side of the pipe using the
 * rt_pipe_create() service; regular Linux threads do the same by
 * opening one of the /dev/rtpN special devices, where N is the minor
 * number agreed upon between both ends of each pipe.
 *
 * In addition, named pipes are available through the registry
 * support, which automatically creates a symbolic link from entries
 * under /proc/xenomai/registry/rtipc/xddp/ to the corresponding
 * special device file.
 *
 * @note Alchemy's message pipes are fully based on the @ref
 * RTIPC_PROTO "XDDP protocol" available from the RTDM/ipc driver.
 *
 * @{
 */
struct syncluster alchemy_pipe_table;

static DEFINE_NAME_GENERATOR(pipe_namegen, "pipe",
			     struct alchemy_pipe, name);

DEFINE_LOOKUP_PRIVATE(pipe, RT_PIPE);

/**
 * @fn int rt_pipe_create(RT_PIPE *pipe, const char *name, int minor, size_t poolsize)
 * @brief Create a message pipe.
 *
 * This service opens a bi-directional communication channel for
 * exchanging messages between Xenomai threads and regular Linux
 * threads. Pipes natively preserve message boundaries, but can also
 * be used in byte-oriented streaming mode from Xenomai to Linux.
 *
 * rt_pipe_create() always returns immediately, even if no thread has
 * opened the associated special device file yet. On the contrary, the
 * non real-time side could block upon attempt to open the special
 * device file until rt_pipe_create() is issued on the same pipe from
 * a Xenomai thread, unless O_NONBLOCK was given to the open(2) system
 * call.
 *
 * @param pipe The address of a pipe descriptor which can be later used
 * to identify uniquely the created object, upon success of this call.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * pipe. When non-NULL and non-empty, a copy of this string is used
 * for indexing the created pipe into the object registry.
 *
 * Named pipes are supported through the use of the registry. Passing
 * a valid @a name parameter when creating a message pipe causes a
 * symbolic link to be created from
 * /proc/xenomai/registry/rtipc/xddp/@a name to the associated special
 * device (i.e. /dev/rtp*), so that the specific @a minor information
 * does not need to be known from those processes for opening the
 * proper device file. In such a case, both sides of the pipe only
 * need to agree upon a symbolic name to refer to the same data path,
 * which is especially useful whenever the @a minor number is picked
 * up dynamically using an adaptive algorithm, such as passing
 * P_MINOR_AUTO as @a minor value.
 *
 * @param minor The minor number of the device associated with the
 * pipe.  Passing P_MINOR_AUTO causes the minor number to be
 * auto-allocated. In such a case, a symbolic link will be
 * automatically created from
 * /proc/xenomai/registry/rtipc/xddp/@a name to the allocated pipe
 * device entry. Valid minor numbers range from 0 to
 * CONFIG_XENO_OPT_PIPE_NRDEV-1.
 *
 * @param poolsize Specifies the size of a dedicated buffer pool for the
 * pipe. Passing 0 means that all message allocations for this pipe are
 * performed on the Cobalt core heap.
 *
 * @return The @a minor number assigned to the connection is returned
 * upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get memory from the
 * main heap in order to create the pipe.
 *
 * - -ENODEV is returned if @a minor is different from P_MINOR_AUTO
 * and is not a valid minor number.
 *
 * - -EEXIST is returned if the @a name is conflicting with an already
 * registered pipe.
 *
 * - -EBUSY is returned if @a minor is already open.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context, e.g. interrupt or non-Xenomai thread.
 *
 * @apitags{xthread-only, mode-unrestricted, switch-secondary}
 */
#ifndef DOXYGEN_CPP
CURRENT_IMPL(int, rt_pipe_create,
	     (RT_PIPE *pipe, const char *name, int minor, size_t poolsize))
#else
int rt_pipe_create(RT_PIPE *pipe,
		   const char *name, int minor, size_t poolsize)
#endif
{
	struct rtipc_port_label plabel;
	struct sockaddr_ipc saddr;
	struct alchemy_pipe *pcb;
	struct service svc;
	size_t streambufsz;
	socklen_t addrlen;
	int ret, sock;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	pcb = xnmalloc(sizeof(*pcb));
	if (pcb == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	sock = __RT(socket(AF_RTIPC, SOCK_DGRAM, IPCPROTO_XDDP));
	if (sock < 0) {
		warning("RTIPC/XDDP protocol not supported by kernel");
		ret = -errno;
		xnfree(pcb);
		goto out;
	}

	if (name && *name) {
		namecpy(plabel.label, name);
		ret = __RT(setsockopt(sock, SOL_XDDP, XDDP_LABEL,
				      &plabel, sizeof(plabel)));
		if (ret)
			goto fail_sockopt;
	}

	if (poolsize > 0) {
		ret = __RT(setsockopt(sock, SOL_XDDP, XDDP_POOLSZ,
				      &poolsize, sizeof(poolsize)));
		if (ret)
			goto fail_sockopt;
	}

	streambufsz = ALCHEMY_PIPE_STREAMSZ;
	ret = __RT(setsockopt(sock, SOL_XDDP, XDDP_BUFSZ,
			      &streambufsz, sizeof(streambufsz)));
	if (ret)
		goto fail_sockopt;

	memset(&saddr, 0, sizeof(saddr));
	saddr.sipc_family = AF_RTIPC;
	saddr.sipc_port = minor;
	ret = __RT(bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)));
	if (ret)
		goto fail_sockopt;

	if (minor == P_MINOR_AUTO) {
		/* Fetch the assigned minor device. */
		addrlen = sizeof(saddr);
		ret = __RT(getsockname(sock, (struct sockaddr *)&saddr, &addrlen));
		if (ret)
			goto fail_sockopt;
		if (addrlen != sizeof(saddr)) {
			ret = -EINVAL;
			goto fail_register;
		}
		minor = saddr.sipc_port;
	}

	generate_name(pcb->name, name, &pipe_namegen);
	pcb->sock = sock;
	pcb->minor = minor;
	pcb->magic = pipe_magic;

	ret = syncluster_addobj(&alchemy_pipe_table, pcb->name, &pcb->cobj);
	if (ret)
		goto fail_register;

	pipe->handle = mainheap_ref(pcb, uintptr_t);

	CANCEL_RESTORE(svc);

	return minor;
fail_sockopt:
	ret = -errno;
	if (ret == -EADDRINUSE)
		ret = -EBUSY;
fail_register:
	__RT(close(sock));
	xnfree(pcb);
out:
	CANCEL_RESTORE(svc);

	return ret;	
}

/**
 * @fn int rt_pipe_delete(RT_PIPE *pipe)
 * @brief Delete a message pipe.
 *
 * This routine deletes a pipe object previously created by a call to
 * rt_pipe_create(). All resources attached to that pipe are
 * automatically released, all pending data is flushed.
 *
 * @param pipe The pipe descriptor.
 *
 * @return Zero is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a valid pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * @apitags{mode-unrestricted, switch-secondary}
 */
int rt_pipe_delete(RT_PIPE *pipe)
{
	struct alchemy_pipe *pcb;
	struct service svc;
	int ret = 0;

	if (threadobj_irq_p())
		return -EPERM;

	CANCEL_DEFER(svc);

	pcb = find_alchemy_pipe(pipe, &ret);
	if (pcb == NULL)
		goto out;

	ret = __RT(close(pcb->sock));
	if (ret) {
		ret = -errno;
		if (ret == -EBADF)
			ret = -EIDRM;
		goto out;
	}

	syncluster_delobj(&alchemy_pipe_table, &pcb->cobj);
	pcb->magic = ~pipe_magic;
out:
	CANCEL_RESTORE(svc);

	return ret;
}

/**
 * @fn ssize_t rt_pipe_read(RT_PIPE *pipe, void *buf, size_t size, RTIME timeout)
 * @brief Read from a pipe (with relative scalar timeout).
 *
 * This routine is a variant of rt_queue_read_timed() accepting a
 * relative timeout specification expressed as a scalar value.
 *
 * @param pipe The pipe descriptor.
 *
 * @param buf A pointer to a memory area which will be written upon
 * success with the message received.
 *
 * @param size The count of bytes from the received message to read up
 * into @a buf. If @a size is lower than the actual message size,
 * -ENOBUFS is returned since the incompletely received message would
 * be lost. If @a size is zero, this call returns immediately with no
 * other action.
 *
 * @param timeout A delay expressed in clock ticks. Passing
 * TM_INFINITE causes the caller to block indefinitely until
 * a message is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case no message is
 * available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
ssize_t rt_pipe_read(RT_PIPE *pipe,
		     void *buf, size_t size, RTIME timeout)
{
	struct alchemy_pipe *pcb;
	int err = 0, flags;
	struct timespec ts;
	struct timeval tv;
	ssize_t ret;

	pcb = find_alchemy_pipe(pipe, &err);
	if (pcb == NULL)
		return err;

	if (timeout == TM_NONBLOCK)
		flags = MSG_DONTWAIT;
	else {
		if (!threadobj_current_p())
			return -EPERM;
		if (timeout != TM_INFINITE) {
			clockobj_ticks_to_timespec(&alchemy_clock, timeout,
						   &ts);
			tv.tv_sec = ts.tv_sec;
			tv.tv_usec = ts.tv_nsec / 1000;
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
		__RT(setsockopt(pcb->sock, SOL_SOCKET,
				SO_RCVTIMEO, &tv, sizeof(tv)));
		flags = 0;
	}

	ret = __RT(recvfrom(pcb->sock, buf, size, flags, NULL, 0));
	if (ret < 0)
		ret = -errno;

	return ret;
}

/**
 * @fn ssize_t rt_pipe_read_until(RT_PIPE *pipe, void *buf, size_t size, RTIME abs_timeout)
 * @brief Read from a pipe (with absolute scalar timeout).
 *
 * This routine is a variant of rt_queue_read_timed() accepting an
 * absolute timeout specification expressed as a scalar value.
 *
 * @param pipe The pipe descriptor.
 *
 * @param buf A pointer to a memory area which will be written upon
 * success with the message received.
 *
 * @param size The count of bytes from the received message to read up
 * into @a buf. If @a size is lower than the actual message size,
 * -ENOBUFS is returned since the incompletely received message would
 * be lost. If @a size is zero, this call returns immediately with no
 * other action.
 *
 * @param abs_timeout An absolute date expressed in clock ticks.
 * Passing TM_INFINITE causes the caller to block indefinitely until
 * a message is available. Passing TM_NONBLOCK causes the service
 * to return immediately without blocking in case no message is
 * available.
 *
 * @apitags{xthread-nowait, switch-primary}
 */

/**
 * @fn ssize_t rt_pipe_read_timed(RT_PIPE *pipe, void *buf, size_t size, const struct timespec *abs_timeout)
 * @brief Read a message from a pipe.
 *
 * This service reads the next available message from a given pipe.
 *
 * @param pipe The pipe descriptor.
 *
 * @param buf A pointer to a memory area which will be written upon
 * success with the message received.
 *
 * @param size The count of bytes from the received message to read up
 * into @a buf. If @a size is lower than the actual message size,
 * -ENOBUFS is returned since the incompletely received message would
 * be lost. If @a size is zero, this call returns immediately with no
 * other action.
 *
 * @param abs_timeout An absolute date expressed in seconds / nanoseconds,
 * based on the Alchemy clock, specifying a time limit to wait for a
 * message to be available from the pipe. Passing NULL causes the caller
 * to block indefinitely until a message is available. Passing
 * { .tv_sec = 0, .tv_nsec = 0 } causes the service to return immediately
 * without blocking in case no message is available.
 *
 * @return The number of bytes available from the received message is
 * returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned if @a abs_timeout is reached before a
 * message arrives.
 *
 * - -EWOULDBLOCK is returned if @a abs_timeout is { .tv_sec = 0,
 * .tv_nsec = 0 } and no message is immediately available on entry to
 * the call.
 *
 * - -EINTR is returned if rt_task_unblock() was called for the
 * current task before a message was available.
 *
 * - -EINVAL is returned if @a pipe is not a valid pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is deleted while the caller was
 * waiting for a message. In such event, @a pipe is no more valid upon
 * return of this service.
 *
 * - -EPERM is returned if this service should block, but was not
 * called from a Xenomai thread.
 *
 * @apitags{xthread-nowait, switch-primary}
 */
ssize_t rt_pipe_read_timed(RT_PIPE *pipe,
			   void *buf, size_t size,
			   const struct timespec *abs_timeout)
{
	struct timespec now, timeout;
	struct alchemy_pipe *pcb;
	int err = 0, flags;
	struct timeval tv;
	ssize_t ret;

	pcb = find_alchemy_pipe(pipe, &err);
	if (pcb == NULL)
		return err;

	if (alchemy_poll_mode(abs_timeout))
		flags = MSG_DONTWAIT;
	else {
		if (!threadobj_current_p())
			return -EPERM;
		if (abs_timeout) {
			__RT(clock_gettime(CLOCK_COPPERPLATE, &now));
			timespec_sub(&timeout, abs_timeout, &now);
			tv.tv_sec = timeout.tv_sec;
			tv.tv_usec = timeout.tv_nsec / 1000;
		} else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}
		__RT(setsockopt(pcb->sock, SOL_SOCKET,
				SO_RCVTIMEO, &tv, sizeof(tv)));
		flags = 0;
	}

	ret = __RT(recvfrom(pcb->sock, buf, size, flags, NULL, 0));
	if (ret < 0)
		ret = -errno;

	return ret;
}

static ssize_t do_write_pipe(RT_PIPE *pipe,
			     const void *buf, size_t size, int flags)
{
	struct alchemy_pipe *pcb;
	struct service svc;
	ssize_t ret;
	int err = 0;

	CANCEL_DEFER(svc);

	pcb = find_alchemy_pipe(pipe, &err);
	if (pcb == NULL) {
		ret = err;
		goto out;
	}

	ret = __RT(sendto(pcb->sock, buf, size, flags, NULL, 0));
	if (ret < 0) {
		ret = -errno;
		if (ret == -EBADF)
			ret = -EIDRM;
	}
out:
	CANCEL_RESTORE(svc);

	return ret;
}

 /**
 * @fn ssize_t rt_pipe_write(RT_PIPE *pipe,const void *buf,size_t size,int mode)
 * @brief Write a message to a pipe.
 *
 * This service writes a complete message to be received from the
 * associated special device. rt_pipe_write() always preserves message
 * boundaries, which means that all data sent through a single call of
 * this service will be gathered in a single read(2) operation from
 * the special device.
 *
 * This service differs from rt_pipe_send() in that it accepts a
 * pointer to the raw data to be sent, instead of a canned message
 * buffer.
 *
 * @param pipe The pipe descriptor.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before transmission.
 *
 * @param size The size in bytes of the message (payload data
 * only). Zero is a valid value, in which case the service returns
 * immediately without sending any message.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - P_URGENT causes the message to be prepended to the output
 * queue, ensuring a LIFO ordering.
 *
 * - P_NORMAL causes the message to be appended to the output
 * queue, ensuring a FIFO ordering.
 *
 * @return Upon success, this service returns @a size. Upon error, one
 * of the following error codes is returned:
 *
 * - -EINVAL is returned if @a mode is invalid or @a pipe is not a
 * pipe descriptor.
 *
 * - -ENOMEM is returned if not enough buffer space is available to
 * complete the operation.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * @note Writing data to a pipe before any peer has opened the
 * associated special device is allowed. The output will be buffered
 * until then, only restricted by the available memory in the
 * associated buffer pool (see rt_pipe_create()).
 *
 * @apitags{xcontext, switch-primary}
 */
ssize_t rt_pipe_write(RT_PIPE *pipe,
		      const void *buf, size_t size, int mode)
{
	int flags = 0;

	if (mode & ~P_URGENT)
		return -EINVAL;

	if (mode & P_URGENT)
		flags |= MSG_OOB;

	return do_write_pipe(pipe, buf, size, flags);
}

 /**
 * @brief Stream bytes through a pipe.
 *
 * This service writes a sequence of bytes to be received from the
 * associated special device. Unlike rt_pipe_send(), this service does
 * not preserve message boundaries. Instead, an internal buffer is
 * filled on the fly with the data, which will be consumed as soon as
 * the receiver wakes up.
 *
 * Data buffers sent by the rt_pipe_stream() service are always
 * transmitted in FIFO order (i.e. P_NORMAL mode).
 *
 * @param pipe The pipe descriptor.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before transmission.
 *
 * @param size The size in bytes of the buffer. Zero is a valid value,
 * in which case the service returns immediately without sending any
 * data.
 *
 * @return The number of bytes sent upon success; this value may be
 * lower than @a size, depending on the available space in the
 * internal buffer. Otherwise:
 *
 * - -EINVAL is returned if @a mode is invalid or @a pipe is not a
 * pipe descriptor.
 *
 * - -ENOMEM is returned if not enough buffer space is available to
 * complete the operation.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * @note Writing data to a pipe before any peer has opened the
 * associated special device is allowed. The output will be buffered
 * until then, only restricted by the available memory in the
 * associated buffer pool (see rt_pipe_create()).
 *
 * @apitags{xcontext, switch-primary}
 */
ssize_t rt_pipe_stream(RT_PIPE *pipe,
		       const void *buf, size_t size)
{
	return do_write_pipe(pipe, buf, size, MSG_MORE);
}

/**
 * @fn int rt_pipe_bind(RT_PIPE *pipe, const char *name, RTIME timeout)
 * @brief Bind to a message pipe.
 *
 * This routine creates a new descriptor to refer to an existing
 * message pipe identified by its symbolic name. If the object does
 * not exist on entry, the caller may block until a pipe of the given
 * name is created.
 *
 * @param pipe The address of a pipe descriptor filled in by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @param name A valid NULL-terminated name which identifies the
 * pipe to bind to. This string should match the object name
 * argument passed to rt_pipe_create().
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
 * @apitags{xthread-nowait}
 *
 * @note The @a timeout value is interpreted as a multiple of the
 * Alchemy clock resolution (see --alchemy-clock-resolution option,
 * defaults to 1 nanosecond).
 */
int rt_pipe_bind(RT_PIPE *pipe,
		 const char *name, RTIME timeout)
{
	return alchemy_bind_object(name,
				   &alchemy_pipe_table,
				   timeout,
				   offsetof(struct alchemy_pipe, cobj),
				   &pipe->handle);
}

/**
 * @fn int rt_pipe_unbind(RT_PIPE *pipe)
 * @brief Unbind from a message pipe.
 *
 * @param pipe The pipe descriptor.
 *
 * This routine releases a previous binding to a message pipe. After
 * this call has returned, the descriptor is no more valid for
 * referencing this object.
 *
 * @apitags{thread-unrestricted}
 */
int rt_pipe_unbind(RT_PIPE *pipe)
{
	pipe->handle = 0;
	return 0;
}

/** @} */
