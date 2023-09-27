/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <mqueue.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

/**
 * @ingroup cobalt_api
 * @defgroup cobalt_api_mq Message queues
 *
 * Cobalt/POSIX message queue services
 *
 * A message queue allow exchanging data between real-time
 * threads. For a POSIX message queue, maximum message length and
 * maximum number of messages are fixed when it is created with
 * mq_open().
 *
 *@{
 */

/**
 * @brief Open a message queue
 *
 * This service opens the message queue named @a name.
 *
 * One of the following values should be set in @a oflags:
 * - O_RDONLY, meaning that the returned queue descriptor may only be used for
 *   receiving messages;
 * - O_WRONLY, meaning that the returned queue descriptor may only be used for
 *   sending messages;
 * - O_RDWR, meaning that the returned queue descriptor may be used for both
 *   sending and receiving messages.
 *
 * If no message queue named @a name exists, and @a oflags has the @a O_CREAT
 * bit set, the message queue is created by this function, taking two more
 * arguments:
 * - a @a mode argument, of type @b mode_t, currently ignored;
 * - an @a attr argument, pointer to an @b mq_attr structure, specifying the
 *   attributes of the new message queue.
 *
 * If @a oflags has the two bits @a O_CREAT and @a O_EXCL set and the message
 * queue alread exists, this service fails.
 *
 * If the O_NONBLOCK bit is set in @a oflags, the mq_send(), mq_receive(),
 * mq_timedsend() and mq_timedreceive() services return @a -1 with @a errno set
 * to EAGAIN instead of blocking their caller.
 *
 * The following arguments of the @b mq_attr structure at the address @a attr
 * are used when creating a message queue:
 * - @a mq_maxmsg is the maximum number of messages in the queue (128 by
 *   default);
 * - @a mq_msgsize is the maximum size of each message (128 by default).
 *
 * @a name may be any arbitrary string, in which slashes have no particular
 * meaning. However, for portability, using a name which starts with a slash and
 * contains no other slash is recommended.
 *
 * @param name name of the message queue to open;
 *
 * @param oflags flags.
 *
 * @return a message queue descriptor on success;
 * @return -1 with @a errno set if:
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - EEXIST, the bits @a O_CREAT and @a O_EXCL were set in @a oflags and the
 *   message queue already exists;
 * - ENOENT, the bit @a O_CREAT is not set in @a oflags and the message queue
 *   does not exist;
 * - ENOSPC, allocation of system memory failed, or insufficient memory available
 *   from the system heap to create the queue, try increasing
 *   CONFIG_XENO_OPT_SYS_HEAPSZ;
 * - EPERM, attempting to create a message queue from an invalid context;
 * - EINVAL, the @a attr argument is invalid;
 * - EMFILE, too many descriptors are currently open.
 * - EAGAIN, no registry slot available, check/raise CONFIG_XENO_OPT_REGISTRY_NRSLOTS.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_open.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(mqd_t, mq_open, (const char *name, int oflags, ...))
{
	struct mq_attr *attr = NULL;
	mode_t mode = 0;
	va_list ap;
	int fd;

	if ((oflags & O_CREAT) != 0) {
		va_start(ap, oflags);
		mode = va_arg(ap, int);	/* unused */
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
	}

	fd = XENOMAI_SYSCALL4(sc_cobalt_mq_open, name, oflags, mode, attr);
	if (fd < 0) {
		errno = -fd;
		return (mqd_t)-1;
	}

	return (mqd_t)fd;
}

/**
 * @brief Close a message queue
 *
 * This service closes the message queue descriptor @a mqd. The
 * message queue is destroyed only when all open descriptors are
 * closed, and when unlinked with a call to the mq_unlink() service.
 *
 * @param mqd message queue descriptor.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is an invalid message queue descriptor;
 * - EPERM, the caller context is invalid.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_close.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(int, mq_close, (mqd_t mqd))
{
	int err;

	err = XENOMAI_SYSCALL1(sc_cobalt_mq_close, mqd);
	if (err) {
		errno = -err;
		return -1;
	}

	return 0;
}

/**
 * @brief Unlink a message queue
 *
 * This service unlinks the message queue named @a name. The message queue is
 * not destroyed until all queue descriptors obtained with the mq_open() service
 * are closed with the mq_close() service. However, after a call to this
 * service, the unlinked queue may no longer be reached with the mq_open()
 * service.
 *
 * @param name name of the message queue to be unlinked.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EPERM, the caller context is invalid;
 * - ENAMETOOLONG, the length of the @a name argument exceeds 64 characters;
 * - ENOENT, the message queue does not exist.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_unlink.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted, switch-secondary}
 */
COBALT_IMPL(int, mq_unlink, (const char *name))
{
	int err;

	err = XENOMAI_SYSCALL1(sc_cobalt_mq_unlink, name);
	if (!err)
		return 0;

	errno = -err;
	return -1;
}

/**
 * @brief Get message queue attributes
 *
 * This service stores, at the address @a attr, the attributes of the messages
 * queue descriptor @a mqd.
 *
 * The following attributes are set:
 * - @a mq_flags, flags of the message queue descriptor @a mqd;
 * - @a mq_maxmsg, maximum number of messages in the message queue;
 * - @a mq_msgsize, maximum message size;
 * - @a mq_curmsgs, number of messages currently in the queue.
 *
 * @param mqd message queue descriptor;
 *
 * @param attr address where the message queue attributes will be stored on
 * success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is not a valid descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_getattr.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, mq_getattr, (mqd_t mqd, struct mq_attr *attr))
{
	int err;

	err = XENOMAI_SYSCALL2(sc_cobalt_mq_getattr, mqd, attr);
	if (!err)
		return 0;

	errno = -err;
	return -1;
}

/**
 * @brief Set message queue attributes
 *
 * This service sets the flags of the @a mqd descriptor to the value
 * of the member @a mq_flags of the @b mq_attr structure pointed to by
 * @a attr.
 *
 * The previous value of the message queue attributes are stored at the address
 * @a oattr if it is not @a NULL.
 *
 * Only setting or clearing the O_NONBLOCK flag has an effect.
 *
 * @param mqd message queue descriptor;
 *
 * @param attr pointer to new attributes (only @a mq_flags is used);
 *
 * @param oattr if not @a NULL, address where previous message queue attributes
 * will be stored on success.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EBADF, @a mqd is not a valid message queue descriptor.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_setattr.html">
 * Specification.</a>
 *
 * @apitags{thread-unrestricted}
 */
COBALT_IMPL(int, mq_setattr, (mqd_t mqd,
			      const struct mq_attr *__restrict__ attr,
			      struct mq_attr *__restrict__ oattr))
{
	int err = 0, flags;

	if (oattr) {
		err = XENOMAI_SYSCALL2(sc_cobalt_mq_getattr, mqd, oattr);
		if (err < 0)
			goto out_err;
		flags = oattr->mq_flags;
	} else {
		err = __WRAP(fcntl(mqd, F_GETFL));
		if (err < 0)
			goto out_err;
		flags = err;
	}

	flags = (flags & ~O_NONBLOCK) | (attr->mq_flags & O_NONBLOCK);

	err = __WRAP(fcntl(mqd, F_SETFL, flags));
	if (!err)
		return 0;

  out_err:
	errno = -err;
	return -1;
}

static inline int __do_mq_timesend(mqd_t q, const char *buffer, size_t len,
				   unsigned int prio, const struct timespec *to)
{
#ifdef __USE_TIME_BITS64
	long sc_nr = sc_cobalt_mq_timedsend64;
#else
	long sc_nr = sc_cobalt_mq_timedsend;
#endif

	return XENOMAI_SYSCALL5(sc_nr, q, buffer, len, prio, to);
}

/**
 * Send a message to a message queue.
 *
 * If the message queue @a fd is not full, this service sends the message of
 * length @a len pointed to by the argument @a buffer, with priority @a prio. A
 * message with greater priority is inserted in the queue before a message with
 * lower priority.
 *
 * If the message queue is full and the flag @a O_NONBLOCK is not set, the
 * calling thread is suspended until the queue is not full. If the message queue
 * is full and the flag @a O_NONBLOCK is set, the message is not sent and the
 * service returns immediately a value of -1 with @a errno set to EAGAIN.
 *
 * @param q message queue descriptor;
 *
 * @param buffer pointer to the message to be sent;
 *
 * @param len length of the message;
 *
 * @param prio priority of the message.
 *
 * @return 0 and send a message on success;
 * @return -1 with no message sent and @a errno set if:
 * - EBADF, @a fd is not a valid message queue descriptor open for writing;
 * - EMSGSIZE, the message length @a len exceeds the @a mq_msgsize attribute of
 *   the message queue;
 * - EAGAIN, the flag O_NONBLOCK is set for the descriptor @a fd and the message
 *   queue is full;
 * - EPERM, the caller context is invalid;
 * - EINTR, the service was interrupted by a signal.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_send.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, mq_send, (mqd_t q, const char *buffer, size_t len, unsigned prio))
{
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = __do_mq_timesend(q, buffer, len, prio, NULL);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return 0;

	errno = -err;
	return -1;
}

/**
 * Attempt, during a bounded time, to send a message to a message queue.
 *
 * This service is equivalent to mq_send(), except that if the message queue is
 * full and the flag @a O_NONBLOCK is not set for the descriptor @a fd, the
 * calling thread is only suspended until the timeout specified by @a
 * abs_timeout expires.
 *
 * @param q message queue descriptor;
 *
 * @param buffer pointer to the message to be sent;
 *
 * @param len length of the message;
 *
 * @param prio priority of the message;
 *
 * @param timeout the timeout, expressed as an absolute value of the
 * CLOCK_REALTIME clock.
 *
 * @return 0 and send a message on success;
 * @return -1 with no message sent and @a errno set if:
 * - EBADF, @a fd is not a valid message queue descriptor open for writing;
 * - EMSGSIZE, the message length exceeds the @a mq_msgsize attribute of the
 *   message queue;
 * - EAGAIN, the flag O_NONBLOCK is set for the descriptor @a fd and the message
 *   queue is full;
 * - EPERM, the caller context is invalid;
 * - ETIMEDOUT, the specified timeout expired;
 * - EINTR, the service was interrupted by a signal.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_timedsend.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL_TIME64(int, mq_timedsend, __mq_timedsend_time64,
		   (mqd_t q, const char *buffer, size_t len, unsigned prio,
		    const struct timespec *timeout))
{
	int err, oldtype;

	if (timeout == NULL)
		return -EFAULT;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = __do_mq_timesend(q, buffer, len, prio, timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return 0;

	errno = -err;
	return -1;
}

static inline int __do_mq_timedreceive(mqd_t q, char *buffer, ssize_t *len,
				       unsigned int *prio,
				       const struct timespec *timeout)
{
#ifdef __USE_TIME_BITS64
	long sc_nr = sc_cobalt_mq_timedreceive64;
#else
	long sc_nr = sc_cobalt_mq_timedreceive;
#endif

	return XENOMAI_SYSCALL5(sc_nr, q, buffer, len, prio, timeout);
}

/**
 * Receive a message from a message queue.
 *
 * If the message queue @a fd is not empty and if @a len is greater than the @a
 * mq_msgsize of the message queue, this service copies, at the address
 * @a buffer, the queued message with the highest priority.
 *
 * If the queue is empty and the flag @a O_NONBLOCK is not set for the
 * descriptor @a fd, the calling thread is suspended until some message is sent
 * to the queue. If the queue is empty and the flag @a O_NONBLOCK is set for the
 * descriptor @a fd, this service returns immediately a value of -1 with @a
 * errno set to EAGAIN.
 *
 * @param q the queue descriptor;
 *
 * @param buffer the address where the received message will be stored on
 * success;
 *
 * @param len @a buffer length;
 *
 * @param prio address where the priority of the received message will be
 * stored on success.
 *
 * @return the message length, and copy a message at the address @a buffer on
 * success;
 * @return -1 with no message unqueued and @a errno set if:
 * - EBADF, @a fd is not a valid descriptor open for reading;
 * - EMSGSIZE, the length @a len is lesser than the message queue @a mq_msgsize
 *   attribute;
 * - EAGAIN, the queue is empty, and the flag @a O_NONBLOCK is set for the
 *   descriptor @a fd;
 * - EPERM, the caller context is invalid;
 * - EINTR, the service was interrupted by a signal.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_receive.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(ssize_t, mq_receive, (mqd_t q, char *buffer, size_t len, unsigned *prio))
{
	ssize_t rlen = (ssize_t) len;
	int err, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = __do_mq_timedreceive(q, buffer, &rlen, prio, NULL);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return rlen;

	errno = -err;
	return -1;
}

/**
 * Attempt, during a bounded time, to receive a message from a message queue.
 *
 * This service is equivalent to mq_receive(), except that if the flag @a
 * O_NONBLOCK is not set for the descriptor @a fd and the message queue is
 * empty, the calling thread is only suspended until the timeout @a abs_timeout
 * expires.
 *
 * @param q the queue descriptor;
 *
 * @param buffer the address where the received message will be stored on
 * success;
 *
 * @param len @a buffer length;
 *
 * @param prio address where the priority of the received message will be
 * stored on success.
 *
 * @param timeout the timeout, expressed as an absolute value of the
 * CLOCK_REALTIME clock.
 *
 * @return the message length, and copy a message at the address @a buffer on
 * success;
 * @return -1 with no message unqueued and @a errno set if:
 * - EBADF, @a fd is not a valid descriptor open for reading;
 * - EMSGSIZE, the length @a len is lesser than the message queue @a mq_msgsize
 *   attribute;
 * - EAGAIN, the queue is empty, and the flag @a O_NONBLOCK is set for the
 *   descriptor @a fd;
 * - EPERM, the caller context is invalid;
 * - EINTR, the service was interrupted by a signal;
 * - ETIMEDOUT, the specified timeout expired.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_timedreceive.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL_TIME64(ssize_t, mq_timedreceive, __mq_timedreceive_time64,
		   (mqd_t q, char *__restrict__ buffer, size_t len,
		    unsigned *__restrict__ prio,
		    const struct timespec *__restrict__ timeout))
{
	ssize_t rlen = (ssize_t) len;
	int err, oldtype;

	if (timeout == NULL)
		return -EFAULT;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	err = __do_mq_timedreceive(q, buffer, &rlen, prio, timeout);

	pthread_setcanceltype(oldtype, NULL);

	if (!err)
		return rlen;

	errno = -err;
	return -1;
}

/**
 * @brief Enable notification on message arrival
 *
 * If @a evp is not @a NULL and is the address of a @b sigevent
 * structure with the @a sigev_notify member set to SIGEV_SIGNAL, the
 * current thread will be notified by a signal when a message is sent
 * to the message queue @a mqd, the queue is empty, and no thread is
 * blocked in call to mq_receive() or mq_timedreceive(). After the
 * notification, the thread is unregistered.
 *
 * If @a evp is @a NULL or the @a sigev_notify member is SIGEV_NONE,
 * the current thread is unregistered.
 *
 * Only one thread may be registered at a time.
 *
 * If the current thread is not a Cobalt thread (created with
 * pthread_create()), this service fails.
 *
 * @param mqd message queue descriptor;
 *
 * @param evp pointer to an event notification structure.
 *
 * @retval 0 on success;
 * @retval -1 with @a errno set if:
 * - EINVAL, @a evp is invalid;
 * - EPERM, the caller context is invalid;
 * - EBADF, @a mqd is not a valid message queue descriptor;
 * - EBUSY, another thread is already registered.
 *
 * @see
 * <a href="http://www.opengroup.org/onlinepubs/000095399/functions/mq_notify.html">
 * Specification.</a>
 *
 * @apitags{xthread-only, switch-primary}
 */
COBALT_IMPL(int, mq_notify, (mqd_t mqd, const struct sigevent *evp))
{
	int err;

	err = XENOMAI_SYSCALL2(sc_cobalt_mq_notify, mqd, evp);
	if (err) {
		errno = -err;
		return -1;
	}

	return 0;
}

/** @}*/
