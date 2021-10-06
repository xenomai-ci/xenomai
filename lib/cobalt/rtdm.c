/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
  USA.
 */

#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <rtdm/rtdm.h>
#include <cobalt/uapi/syscall.h>
#include <asm/xenomai/syscall.h>
#include "internal.h"

/* support for very old c libraries not supporting O_TMPFILE */
#ifndef O_TMPFILE
#define O_TMPFILE (020000000 | 0200000)
#endif


static inline int set_errno(int ret)
{
	if (ret >= 0)
		return ret;

	errno = -ret;
	return -1;
}

static int do_open(const char *path, int oflag, mode_t mode)
{
	int fd, oldtype;

	/*
	 * Don't dereference path, as it might be invalid. Leave it to
	 * the kernel service.
	 */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
	fd = XENOMAI_SYSCALL2( sc_cobalt_open, path, oflag);
	pthread_setcanceltype(oldtype, NULL);
	if (fd < 0) {
		if (fd != -EADV && fd != -ENOSYS)
			return set_errno(fd);

		fd = __STD(open(path, oflag, mode));
	}

	return fd;
}

COBALT_IMPL(int, open, (const char *path, int oflag, ...))
{
	mode_t mode = 0;
	va_list ap;

	if ((oflag & O_CREAT)  || (oflag & O_TMPFILE) == O_TMPFILE) {
		va_start(ap, oflag);
		mode = va_arg(ap, int);
		va_end(ap);
	}

	return do_open(path, oflag, mode);
}

COBALT_IMPL(int, open64, (const char *path, int oflag, ...))
{
	mode_t mode = 0;
	va_list ap;

	if ((oflag & O_CREAT)  || (oflag & O_TMPFILE) == O_TMPFILE) {
		va_start(ap, oflag);
		mode = va_arg(ap, int);
		va_end(ap);
	}

	return do_open(path, oflag | O_LARGEFILE, mode);
}

COBALT_IMPL(int, __open_2, (const char *path, int oflag))
{
	/* __open_2() from glibc adds a runtime precondition test for the 'oflag'
	 * parameter to the functionality of open(). It may be used when the macro
	 * _FORTIFY_SOURCE is defined when compiling the application code.
	 */
	if (__OPEN_NEEDS_MODE(oflag)) {
		const char *msg =
			"invalid open call: O_CREAT or O_TMPFILE without mode\n";
		ssize_t n = __STD(write(STDERR_FILENO, msg, strlen(msg)));
		(void)n;
		abort();
	}

	return do_open(path, oflag, 0);
}

COBALT_IMPL(int, __open64_2, (const char *path, int oflag))
{
	/* just like __open_2() and open64() */
	if (__OPEN_NEEDS_MODE(oflag)) {
		const char *msg =
			"invalid open64 call: O_CREAT or O_TMPFILE without mode\n";
		ssize_t n = __STD(write(STDERR_FILENO, msg, strlen(msg)));
		(void)n;
		abort();
	}

	return do_open(path, oflag | O_LARGEFILE, 0);
}

COBALT_IMPL(int, socket, (int protocol_family, int socket_type, int protocol))
{
	int s;

	s = XENOMAI_SYSCALL3(sc_cobalt_socket, protocol_family,
			     socket_type, protocol);
	if (s < 0) {
		if (s != -EAFNOSUPPORT &&
		    s != -EPROTONOSUPPORT &&
		    s != -ENOSYS)
			return set_errno(s);

		s = __STD(socket(protocol_family, socket_type, protocol));
	}

	return s;
}

COBALT_IMPL(int, close, (int fd))
{
	int oldtype;
	int ret;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL1(sc_cobalt_close, fd);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(close(fd));
}

static int do_ioctl(int fd, unsigned int request, void *arg)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL3(sc_cobalt_ioctl,	fd, request, arg);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(int, fcntl, (int fd, int cmd, ...))
{
	va_list ap;
	long arg;
	int ret;

	va_start(ap, cmd);
	arg = va_arg(ap, long);
	va_end(ap);

	ret = XENOMAI_SYSCALL3(sc_cobalt_fcntl, fd, cmd, arg);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(fcntl(fd, cmd, arg));
}

COBALT_IMPL(int, ioctl, (int fd, unsigned int request, ...))
{
	va_list ap;
	void *arg;
	int ret;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	ret = do_ioctl(fd, request, arg);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(ioctl(fd, request, arg));
}

COBALT_IMPL(ssize_t, read, (int fd, void *buf, size_t nbyte))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL3(sc_cobalt_read, fd, buf, nbyte);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(read(fd, buf, nbyte));
}

COBALT_IMPL(ssize_t, write, (int fd, const void *buf, size_t nbyte))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL3(sc_cobalt_write,	fd, buf, nbyte);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(write(fd, buf, nbyte));
}

static ssize_t do_recvmsg(int fd, struct msghdr *msg, int flags)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL3(sc_cobalt_recvmsg, fd, msg, flags);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(ssize_t, recvmsg, (int fd, struct msghdr *msg, int flags))
{
	int ret;

	ret = do_recvmsg(fd, msg, flags);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recvmsg(fd, msg, flags));
}

COBALT_IMPL(int, recvmmsg, (int fd, struct mmsghdr *msgvec, unsigned int vlen,
			    unsigned int flags, struct timespec *timeout))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

#ifdef __USE_TIME_BITS64
	ret = XENOMAI_SYSCALL5(sc_cobalt_recvmmsg64, fd, msgvec,
				vlen, flags, timeout);
#else
	ret = XENOMAI_SYSCALL5(sc_cobalt_recvmmsg, fd, msgvec, vlen, flags, timeout);
#endif

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recvmmsg(fd, msgvec, vlen, flags, timeout));
}

static ssize_t do_sendmsg(int fd, const struct msghdr *msg, int flags)
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL3(sc_cobalt_sendmsg, fd, msg, flags);

	pthread_setcanceltype(oldtype, NULL);

	return ret;
}

COBALT_IMPL(ssize_t, sendmsg, (int fd, const struct msghdr *msg, int flags))
{
	int ret;

	ret = do_sendmsg(fd, msg, flags);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(sendmsg(fd, msg, flags));
}

COBALT_IMPL(int, sendmmsg, (int fd, struct mmsghdr *msgvec,
			    unsigned int vlen, unsigned int flags))
{
	int ret, oldtype;

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

	ret = XENOMAI_SYSCALL4(sc_cobalt_sendmmsg, fd, msgvec, vlen, flags);

	pthread_setcanceltype(oldtype, NULL);

	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(sendmmsg(fd, msgvec, vlen, flags));
}

COBALT_IMPL(ssize_t, recvfrom, (int fd, void *buf, size_t len, int flags,
				struct sockaddr *from, socklen_t *fromlen))
{
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_name = from,
		.msg_namelen = from != NULL ? *fromlen : 0,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
	};
	int ret;

	ret = do_recvmsg(fd, &msg, flags);
	if (ret != -EADV && ret != -ENOSYS) {
		if (ret < 0)
			return set_errno(ret);

		if (from)
			*fromlen = msg.msg_namelen;

		return ret;
	}

	return __STD(recvfrom(fd, buf, len, flags, from, fromlen));
}

COBALT_IMPL(ssize_t, sendto, (int fd, const void *buf, size_t len, int flags,
			      const struct sockaddr *to, socklen_t tolen))
{
	struct iovec iov = {
		.iov_base = (void *)buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_name = (struct sockaddr *)to,
		.msg_namelen = tolen,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
	};
	int ret;

	ret = do_sendmsg(fd, &msg, flags);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(sendto(fd, buf, len, flags, to, tolen));
}

COBALT_IMPL(ssize_t, recv, (int fd, void *buf, size_t len, int flags))
{
	struct iovec iov = {
		.iov_base = (void *)buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
	};
	int ret;

	ret = do_recvmsg(fd, &msg, flags);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(recv(fd, buf, len, flags));
}

COBALT_IMPL(ssize_t, send, (int fd, const void *buf, size_t len, int flags))
{
	struct iovec iov = {
		.iov_base = (void *)buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = NULL,
		.msg_controllen = 0,
	};
	int ret;

	ret = do_sendmsg(fd, &msg, flags);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(send(fd, buf, len, flags));
}

COBALT_IMPL(int, getsockopt, (int fd, int level, int optname, void *optval,
			      socklen_t *optlen))
{
	struct _rtdm_getsockopt_args args = { level, optname, optval, optlen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_GETSOCKOPT, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, setsockopt, (int fd, int level, int optname, const void *optval,
			      socklen_t optlen))
{
	struct _rtdm_setsockopt_args args = {
		level, optname, (void *)optval, optlen
	};
	int ret;

	ret = do_ioctl(fd, _RTIOC_SETSOCKOPT, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(setsockopt(fd, level, optname, optval, optlen));
}

COBALT_IMPL(int, bind, (int fd, const struct sockaddr *my_addr, socklen_t addrlen))
{
	struct _rtdm_setsockaddr_args args = { my_addr, addrlen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_BIND, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(bind(fd, my_addr, addrlen));
}

COBALT_IMPL(int, connect, (int fd, const struct sockaddr *serv_addr, socklen_t addrlen))
{
	struct _rtdm_setsockaddr_args args = { serv_addr, addrlen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_CONNECT, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(connect(fd, serv_addr, addrlen));
}

COBALT_IMPL(int, listen, (int fd, int backlog))
{
	int ret;

	ret = do_ioctl(fd, _RTIOC_LISTEN, (void *)(long)backlog);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(listen(fd, backlog));
}

COBALT_IMPL(int, accept, (int fd, struct sockaddr *addr, socklen_t *addrlen))
{
	struct _rtdm_getsockaddr_args args = { addr, addrlen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_ACCEPT, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(accept(fd, addr, addrlen));
}

COBALT_IMPL(int, getsockname, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	struct _rtdm_getsockaddr_args args = { name, namelen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_GETSOCKNAME, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getsockname(fd, name, namelen));
}

COBALT_IMPL(int, getpeername, (int fd, struct sockaddr *name, socklen_t *namelen))
{
	struct _rtdm_getsockaddr_args args = { name, namelen };
	int ret;

	ret = do_ioctl(fd, _RTIOC_GETPEERNAME, &args);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(getpeername(fd, name, namelen));
}

COBALT_IMPL(int, shutdown, (int fd, int how))
{
	int ret;

	ret = do_ioctl(fd, _RTIOC_SHUTDOWN, (void *)(long)how);
	if (ret != -EADV && ret != -ENOSYS)
		return set_errno(ret);

	return __STD(shutdown(fd, how));
}

COBALT_IMPL(void *, mmap64, (void *addr, size_t length, int prot, int flags,
			     int fd, off64_t offset))
{
	struct _rtdm_mmap_request rma;
	int ret;

	if (fd < 0) /* We don't do anonymous mappings. */
		goto regular;

	/* RTDM ignores the address hint, and rejects MAP_FIXED. */
	rma.length = length;
	rma.offset = offset;
	rma.prot = prot;
	rma.flags = flags;

	ret = XENOMAI_SYSCALL3(sc_cobalt_mmap, fd, &rma, &addr);
	if (ret != -EADV && ret != -ENOSYS) {
		ret = set_errno(ret);
		if (ret)
			return MAP_FAILED;

		return addr;
	}

regular:
#if mmap64 == mmap
	return __STD(mmap(addr, length, prot, flags, fd, offset));
#else
	return __STD(mmap64(addr, length, prot, flags, fd, offset));
#endif
}

COBALT_IMPL(void *, mmap, (void *addr, size_t length, int prot, int flags,
			   int fd, off_t offset))
{
	return __COBALT(mmap64(addr, length, prot, flags, fd, offset));
}
