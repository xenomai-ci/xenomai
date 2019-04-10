/*
 * Copyright (C) 2005 Heikki Lindholm <holindho@cs.helsinki.fi>.
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

/*
 * NOTE: functions in dynamically linked libraries aren't
 * wrapped. These are fallback functions for __real* functions used by
 * the library itself.
 */
#include <xeno_config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <fcntl.h>
#include <sched.h>
#include <memory.h>
#include <unistd.h>
#include <malloc.h>
#include <boilerplate/compiler.h>

/* support for very old c libraries not supporting O_TMPFILE */
#ifndef O_TMPFILE
#define O_TMPFILE (020000000 | 0200000)
#endif

/* sched */
__weak
int __real_pthread_setschedparam(pthread_t thread,
				 int policy, const struct sched_param *param)
{
	return pthread_setschedparam(thread, policy, param);
}

__weak
int __real_pthread_getschedparam(pthread_t thread,
				 int *policy, struct sched_param *param)
{
	return pthread_getschedparam(thread, policy, param);
}

__weak
int __real_sched_yield(void)
{
	return sched_yield();
}

__weak
int __real_sched_get_priority_min(int policy)
{
	return sched_get_priority_min(policy);
}

__weak
int __real_sched_get_priority_max(int policy)
{
	return sched_get_priority_max(policy);
}

__weak
int __real_sched_setscheduler(pid_t pid, int policy,
			      const struct sched_param *param)
{
	return sched_setscheduler(pid, policy, param);
}

__weak
int __real_sched_getscheduler(pid_t pid)
{
	return sched_getscheduler(pid);
}

/* pthread */
__weak
int __real_pthread_create(pthread_t *ptid_r,
			  const pthread_attr_t * attr,
			  void *(*start) (void *), void *arg)
{
	return pthread_create(ptid_r, attr, start, arg);
}

__weak
int __real_pthread_kill(pthread_t ptid, int sig)
{
	return pthread_kill(ptid, sig);
}

__weak
int __real_pthread_join(pthread_t ptid, void **retval)
{
	return pthread_join(ptid, retval);
}

/* attr */
__weak
int __real_pthread_attr_init(pthread_attr_t *attr)
{
	return pthread_attr_init(attr);
}

/* semaphores */
__weak
int __real_sem_init(sem_t * sem, int pshared, unsigned value)
{
	return sem_init(sem, pshared, value);
}

__weak
int __real_sem_destroy(sem_t * sem)
{
	return sem_destroy(sem);
}

__weak
int __real_sem_post(sem_t * sem)
{
	return sem_post(sem);
}

__weak
int __real_sem_wait(sem_t * sem)
{
	return sem_wait(sem);
}

__weak
int __real_sem_trywait(sem_t * sem)
{
	return sem_trywait(sem);
}

__weak
int __real_sem_timedwait(sem_t * sem, const struct timespec *abs_timeout)
{
	return sem_timedwait(sem, abs_timeout);
}

__weak
int __real_sem_getvalue(sem_t * sem, int *sval)
{
	return sem_getvalue(sem, sval);
}

/* rtdm */
__weak
int __real_open(const char *path, int oflag, ...)
{
	mode_t mode = 0;
	va_list ap;

	if ((oflag & O_CREAT) || (oflag & O_TMPFILE) == O_TMPFILE) {
		va_start(ap, oflag);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}

	return open(path, oflag, mode);
}

#if open64 != open
__weak
int __real_open64(const char *path, int oflag, ...)
{
	mode_t mode = 0;
	va_list ap;

	if ((oflag & O_CREAT) || (oflag & O_TMPFILE) == O_TMPFILE) {
		va_start(ap, oflag);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}

	return open64(path, oflag, mode);
}
#endif

__weak
int __real_socket(int protocol_family, int socket_type, int protocol)
{
	return socket(protocol_family, socket_type, protocol);
}

__weak
int __real_close(int fd)
{
	return close(fd);
}

__weak
int __real_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	int arg;

	va_start(ap, cmd);
	arg = va_arg(ap, int);
	va_end(ap);

	return fcntl(fd, cmd, arg);
}

__weak
int __real_ioctl(int fd, unsigned int request, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	return ioctl(fd, request, arg);
}

__weak
ssize_t __real_read(int fd, void *buf, size_t nbyte)
{
	return read(fd, buf, nbyte);
}

__weak
ssize_t __real_write(int fd, const void *buf, size_t nbyte)
{
	return write(fd, buf, nbyte);
}

__weak
ssize_t __real_recvmsg(int fd, struct msghdr * msg, int flags)
{
	return recvmsg(fd, msg, flags);
}

__weak
ssize_t __real_sendmsg(int fd, const struct msghdr * msg, int flags)
{
	return sendmsg(fd, msg, flags);
}

__weak
ssize_t __real_recvfrom(int fd, void *buf, size_t len, int flags,
			struct sockaddr * from, socklen_t * fromlen)
{
	return recvfrom(fd, buf, len, flags, from, fromlen);
}

__weak
ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags,
		      const struct sockaddr * to, socklen_t tolen)
{
	return sendto(fd, buf, len, flags, to, tolen);
}

__weak
ssize_t __real_recv(int fd, void *buf, size_t len, int flags)
{
	return recv(fd, buf, len, flags);
}

__weak
ssize_t __real_send(int fd, const void *buf, size_t len, int flags)
{
	return send(fd, buf, len, flags);
}

__weak
int __real_getsockopt(int fd, int level, int optname, void *optval,
		      socklen_t * optlen)
{
	return getsockopt(fd, level, optname, optval, optlen);
}

__weak
int __real_setsockopt(int fd, int level, int optname, const void *optval,
		      socklen_t optlen)
{
	return setsockopt(fd, level, optname, optval, optlen);
}

__weak
int __real_bind(int fd, const struct sockaddr *my_addr, socklen_t addrlen)
{
	return bind(fd, my_addr, addrlen);
}

__weak
int __real_connect(int fd, const struct sockaddr *serv_addr, socklen_t addrlen)
{
	return connect(fd, serv_addr, addrlen);
}

__weak
int __real_listen(int fd, int backlog)
{
	return listen(fd, backlog);
}

__weak
int __real_accept(int fd, struct sockaddr *addr, socklen_t * addrlen)
{
	return accept(fd, addr, addrlen);
}

__weak
int __real_getsockname(int fd, struct sockaddr *name, socklen_t * namelen)
{
	return getsockname(fd, name, namelen);
}

__weak
int __real_getpeername(int fd, struct sockaddr *name, socklen_t * namelen)
{
	return getpeername(fd, name, namelen);
}

__weak
int __real_shutdown(int fd, int how)
{
	return shutdown(fd, how);
}

__weak
int __real_select (int __nfds, fd_set *__restrict __readfds,
		   fd_set *__restrict __writefds,
		   fd_set *__restrict __exceptfds,
		   struct timeval *__restrict __timeout)
{
	return select(__nfds, __readfds, __writefds, __exceptfds, __timeout);
}

__weak
void *__real_mmap(void *addr, size_t length, int prot, int flags,
		  int fd, off_t offset)
{
	return mmap(addr, length, prot, flags, fd, offset);
}

#if mmap64 != mmap
__weak
void *__real_mmap64(void *addr, size_t length, int prot, int flags,
		  int fd, off64_t offset)
{
	return mmap64(addr, length, prot, flags, fd, offset);
}
#endif

__weak
int __real_vfprintf(FILE *stream, const char *fmt, va_list args)
{
	return vfprintf(stream, fmt, args);
}

__weak
int __real_vprintf(const char *fmt, va_list args)
{
	return vprintf(fmt, args);
}

__weak
int __real_fprintf(FILE *stream, const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = vfprintf(stream, fmt, args);
	va_end(args);

	return rc;
}

__weak
int __real_printf(const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = vprintf(fmt, args);
	va_end(args);

	return rc;
}

#ifdef CONFIG_XENO_FORTIFY

__weak
int __real___vfprintf_chk(FILE *stream, int level, const char *fmt, va_list ap)
{
	return __vfprintf_chk(stream, level, fmt, ap);
}

__weak
void __real___vsyslog_chk(int priority, int level, const char *fmt, va_list ap)
{
	extern void __vsyslog_chk(int, int, const char *, va_list);

	__vsyslog_chk(priority, level, fmt, ap);
}

#endif

__weak
int __real_puts(const char *s)
{
	return puts(s);
}

__weak
int __real_fputs(const char *s, FILE *stream)
{
	return fputs(s, stream);
}

#ifndef fputc
__weak
int __real_fputc(int c, FILE *stream)
{
	return fputc(c, stream);
}
#endif

#ifndef putchar
__weak
int __real_putchar(int c)
{
	return putchar(c);
}
#endif

__weak
size_t __real_fwrite(const void *ptr, size_t sz, size_t nmemb, FILE *stream)
{
	return fwrite(ptr, sz, nmemb, stream);
}

__weak
int __real_fclose(FILE *stream)
{
	return fclose(stream);
}

__weak
void __real_syslog(int priority, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsyslog(priority, fmt, args);
	va_end(args);
}

__weak
void __real_vsyslog(int priority, const char *fmt, va_list ap)
{
	vsyslog(priority, fmt, ap);
}

__weak
int __real_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return gettimeofday(tv, tz);
}

__weak
int __real_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	return clock_gettime(clk_id, tp);
}

__weak
int __real_sigwait(const sigset_t *set, int *sig)
{
	return sigwait(set, sig);
}

__weak
int __real_sigwaitinfo(const sigset_t *set, siginfo_t *si)
{
	return sigwaitinfo(set, si);
}

__weak
int __real_sigtimedwait(const sigset_t *set, siginfo_t *si,
			const struct timespec *timeout)
{
	return sigtimedwait(set, si, timeout);
}

__weak
int __real_sigpending(sigset_t *set)
{
	return sigpending(set);
}

__weak
int __real_kill(pid_t pid, int sig)
{
	return kill(pid, sig);
}

__weak
unsigned int __real_sleep(unsigned int seconds)
{
	return sleep(seconds);
}
