/*
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _COBALT_KERNEL_COMPAT_H
#define _COBALT_KERNEL_COMPAT_H

#ifdef CONFIG_XENO_ARCH_SYS3264

#include <linux/compat.h>
#include <net/compat.h>
#include <asm/xenomai/wrappers.h>
#include <cobalt/uapi/sched.h>

struct mq_attr;

struct compat_mq_attr {
	compat_long_t mq_flags;
	compat_long_t mq_maxmsg;
	compat_long_t mq_msgsize;
	compat_long_t mq_curmsgs;
};

typedef struct {
	compat_ulong_t fds_bits[__FD_SETSIZE / (8 * sizeof(compat_long_t))];
} compat_fd_set;

struct compat_rtdm_mmap_request {
	u64 offset;
	compat_size_t length;
	int prot;
	int flags;
};

int sys32_get_timespec(struct timespec64 *ts,
		       const struct old_timespec32 __user *cts);

int sys32_put_timespec(struct old_timespec32 __user *cts,
		       const struct timespec64 *ts);

int sys32_get_itimerspec(struct itimerspec64 *its,
			 const struct old_itimerspec32 __user *cits);

int sys32_put_itimerspec(struct old_itimerspec32 __user *cits,
			 const struct itimerspec64 *its);

int sys32_get_timeval(struct __kernel_old_timeval *tv,
		      const struct old_timeval32 __user *ctv);

int sys32_put_timeval(struct old_timeval32 __user *ctv,
		      const struct __kernel_old_timeval *tv);

int sys32_get_timex(struct __kernel_timex *tx,
		    const struct old_timex32 __user *ctx);

int sys32_put_timex(struct old_timex32 __user *ctx,
		    const struct __kernel_timex *tx);

int sys32_get_fdset(fd_set *fds, const compat_fd_set __user *cfds,
		    size_t cfdsize);

int sys32_put_fdset(compat_fd_set __user *cfds, const fd_set *fds,
		    size_t fdsize);

int sys32_get_mqattr(struct mq_attr *ap,
		     const struct compat_mq_attr __user *u_cap);

int sys32_put_mqattr(struct compat_mq_attr __user *u_cap,
		     const struct mq_attr *ap);

int sys32_get_sigevent(struct sigevent *ev,
		       const struct compat_sigevent *__user u_cev);

int sys32_get_sigset(sigset_t *set, const compat_sigset_t *u_cset);

int sys32_put_sigset(compat_sigset_t *u_cset, const sigset_t *set);

int sys32_get_sigval(union sigval *val, const union compat_sigval *u_cval);

int sys32_put_siginfo(void __user *u_si, const struct siginfo *si,
		      int overrun);

int sys32_get_msghdr(struct user_msghdr *msg,
		     const struct compat_msghdr __user *u_cmsg);

int sys32_get_mmsghdr(struct mmsghdr *mmsg,
		      const struct compat_mmsghdr __user *u_cmmsg);

int sys32_put_msghdr(struct compat_msghdr __user *u_cmsg,
		     const struct user_msghdr *msg);

int sys32_put_mmsghdr(struct compat_mmsghdr __user *u_cmmsg,
		     const struct mmsghdr *mmsg);

int sys32_get_iovec(struct iovec *iov,
		    const struct compat_iovec __user *ciov,
		    int ciovlen);

int sys32_put_iovec(struct compat_iovec __user *u_ciov,
		    const struct iovec *iov,
		    int iovlen);

#endif /* CONFIG_XENO_ARCH_SYS3264 */

#endif /* !_COBALT_KERNEL_COMPAT_H */
