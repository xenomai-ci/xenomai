/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
 * Copyright (C) 2008 Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>
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
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/err.h>
#include <linux/fs.h>
#include <cobalt/kernel/compat.h>
#include <cobalt/kernel/ppd.h>
#include <cobalt/kernel/time.h>
#include <xenomai/rtdm/internal.h>
#include "process.h"
#include "internal.h"
#include "clock.h"
#include "io.h"

COBALT_SYSCALL(open, lostage,
	       (const char __user *u_path, int oflag))
{
	struct filename *filename;
	int ufd;

	filename = getname(u_path);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	ufd = __rtdm_dev_open(filename->name, oflag);
	putname(filename);

	return ufd;
}

COBALT_SYSCALL(socket, lostage,
	       (int protocol_family, int socket_type, int protocol))
{
	return __rtdm_dev_socket(protocol_family, socket_type, protocol);
}

COBALT_SYSCALL(close, lostage, (int fd))
{
	return rtdm_fd_close(fd, 0);
}

COBALT_SYSCALL(fcntl, current, (int fd, int cmd, long arg))
{
	return rtdm_fd_fcntl(fd, cmd, arg);
}

COBALT_SYSCALL(ioctl, handover,
	       (int fd, unsigned int request, void __user *arg))
{
	return rtdm_fd_ioctl(fd, request, arg);
}

COBALT_SYSCALL(read, handover,
	       (int fd, void __user *buf, size_t size))
{
	return rtdm_fd_read(fd, buf, size);
}

COBALT_SYSCALL(write, handover,
	       (int fd, const void __user *buf, size_t size))
{
	return rtdm_fd_write(fd, buf, size);
}

COBALT_SYSCALL(recvmsg, handover,
	       (int fd, struct user_msghdr __user *umsg, int flags))
{
	struct user_msghdr m;
	ssize_t ret;

	ret = cobalt_copy_from_user(&m, umsg, sizeof(m));
	if (ret)
		return ret;

	ret = rtdm_fd_recvmsg(fd, &m, flags);
	if (ret < 0)
		return ret;

	return cobalt_copy_to_user(umsg, &m, sizeof(*umsg)) ?: ret;
}

static int get_timespec(struct timespec64 *ts,
			const void __user *u_ts)
{
	return cobalt_get_old_timespec(ts, u_ts);
}

static int get_mmsg(struct mmsghdr *mmsg, void __user *u_mmsg)
{
	return cobalt_copy_from_user(mmsg, u_mmsg, sizeof(*mmsg));
}

static int put_mmsg(void __user **u_mmsg_p, const struct mmsghdr *mmsg)
{
	struct mmsghdr __user **p = (struct mmsghdr **)u_mmsg_p,
		*q __user = (*p)++;

	return cobalt_copy_to_user(q, mmsg, sizeof(*q));
}

COBALT_SYSCALL(recvmmsg, primary,
	       (int fd, struct mmsghdr __user *u_msgvec, unsigned int vlen,
		unsigned int flags, struct __kernel_old_timespec __user *u_timeout))
{
	return __rtdm_fd_recvmmsg(fd, u_msgvec, vlen, flags, u_timeout,
				  get_mmsg, put_mmsg, get_timespec);
}

COBALT_SYSCALL(recvmmsg64, primary,
	       (int fd, struct mmsghdr __user *u_msgvec, unsigned int vlen,
		unsigned int flags, struct __kernel_timespec __user *u_timeout))
{
	return __rtdm_fd_recvmmsg64(fd, u_msgvec, vlen, flags, u_timeout,
				    get_mmsg, put_mmsg);
}

COBALT_SYSCALL(sendmsg, handover,
	       (int fd, struct user_msghdr __user *umsg, int flags))
{
	struct user_msghdr m;
	int ret;

	ret = cobalt_copy_from_user(&m, umsg, sizeof(m));

	return ret ?: rtdm_fd_sendmsg(fd, &m, flags);
}

static int put_mmsglen(void __user **u_mmsg_p, const struct mmsghdr *mmsg)
{
	struct mmsghdr __user **p = (struct mmsghdr **)u_mmsg_p,
		*q __user = (*p)++;

	if (!access_ok(&q->msg_len, sizeof(q->msg_len)))
		return -EFAULT;
	return __xn_put_user(mmsg->msg_len, &q->msg_len);
}

COBALT_SYSCALL(sendmmsg, primary,
	       (int fd, struct mmsghdr __user *u_msgvec,
		unsigned int vlen, unsigned int flags))
{
	return __rtdm_fd_sendmmsg(fd, u_msgvec, vlen, flags,
				  get_mmsg, put_mmsglen);
}

COBALT_SYSCALL(mmap, lostage,
	       (int fd, struct _rtdm_mmap_request __user *u_rma,
	        void __user **u_addrp))
{
	struct _rtdm_mmap_request rma;
	void *u_addr = NULL;
	int ret;

	ret = cobalt_copy_from_user(&rma, u_rma, sizeof(rma));
	if (ret)
		return ret;

	ret = rtdm_fd_mmap(fd, &rma, &u_addr);
	if (ret)
		return ret;

	return cobalt_copy_to_user(u_addrp, &u_addr, sizeof(u_addr));
}

static int __cobalt_first_fd_valid_p(fd_set *fds[XNSELECT_MAX_TYPES], int nfds)
{
	int i, fd;

	for (i = 0; i < XNSELECT_MAX_TYPES; i++)
		if (fds[i]
		    && (fd = find_first_bit(fds[i]->fds_bits, nfds)) < nfds)
			return rtdm_fd_valid_p(fd);

	/* All empty is correct, used as a "sleep" mechanism by strange
	   applications. */
	return 1;
}

static int __cobalt_select_bind_all(struct xnselector *selector,
				    fd_set *fds[XNSELECT_MAX_TYPES], int nfds)
{
	bool first_fd = true;
	unsigned fd, type;
	int err;

	for (type = 0; type < XNSELECT_MAX_TYPES; type++) {
		fd_set *set = fds[type];
		if (set)
			for (fd = find_first_bit(set->fds_bits, nfds);
			     fd < nfds;
			     fd = find_next_bit(set->fds_bits, nfds, fd + 1)) {
				err = rtdm_fd_select(fd, selector, type);
				if (err) {
					/*
					 * Do not needlessly signal "retry
					 * under Linux" for mixed fd sets.
					 */
					if (err == -EADV && !first_fd)
						return -EBADF;
					return err;
				}
				first_fd = false;
			}
	}

	return 0;
}

int __cobalt_select(int nfds, void __user *u_rfds, void __user *u_wfds,
		    void __user *u_xfds, struct timespec64 *to, bool compat)
{
	void __user *ufd_sets[XNSELECT_MAX_TYPES] = {
		[XNSELECT_READ] = u_rfds,
		[XNSELECT_WRITE] = u_wfds,
		[XNSELECT_EXCEPT] = u_xfds
	};
	fd_set *in_fds[XNSELECT_MAX_TYPES] = {NULL, NULL, NULL};
	fd_set *out_fds[XNSELECT_MAX_TYPES] = {NULL, NULL, NULL};
	fd_set in_fds_storage[XNSELECT_MAX_TYPES],
		out_fds_storage[XNSELECT_MAX_TYPES];
	xnticks_t timeout = XN_INFINITE;
	struct restart_block *restart;
	xntmode_t mode = XN_RELATIVE;
	struct xnselector *selector;
	struct xnthread *curr;
	size_t fds_size;
	int i, err;

	curr = xnthread_current();

	if (to) {
		if (xnthread_test_localinfo(curr, XNSYSRST)) {
			xnthread_clear_localinfo(curr, XNSYSRST);

			restart = &current->restart_block;
			timeout = restart->nanosleep.expires;

			if (restart->fn != cobalt_restart_syscall_placeholder) {
				err = -EINTR;
				goto out;
			}
		} else {

			if (!timespec64_valid(to))
				return -EINVAL;

			timeout = clock_get_ticks(CLOCK_MONOTONIC) + ts2ns(to);
		}

		mode = XN_ABSOLUTE;
	}

	if ((unsigned)nfds > __FD_SETSIZE)
		return -EINVAL;

	fds_size = __FDELT__(nfds + __NFDBITS__ - 1) * sizeof(long);

	for (i = 0; i < XNSELECT_MAX_TYPES; i++)
		if (ufd_sets[i]) {
			in_fds[i] = &in_fds_storage[i];
			out_fds[i] = &out_fds_storage[i];
#ifdef CONFIG_XENO_ARCH_SYS3264
			if (compat) {
				if (sys32_get_fdset(in_fds[i], ufd_sets[i],
						    fds_size))
					return -EFAULT;
			} else
#endif
			{
				if (!access_ok((void __user *) ufd_sets[i],
					       sizeof(fd_set))
				    || cobalt_copy_from_user(in_fds[i],
							     (void __user *)ufd_sets[i],
							     fds_size))
					return -EFAULT;
			}
		}

	selector = curr->selector;
	if (!selector) {
		/* This function may be called from pure Linux fd_sets, we want
		   to avoid the xnselector allocation in this case, so, we do a
		   simple test: test if the first file descriptor we find in the
		   fd_set is an RTDM descriptor or a message queue descriptor. */
		if (!__cobalt_first_fd_valid_p(in_fds, nfds))
			return -EADV;

		selector = xnmalloc(sizeof(*curr->selector));
		if (selector == NULL)
			return -ENOMEM;
		xnselector_init(selector);
		curr->selector = selector;

		/* Bind directly the file descriptors, we do not need to go
		   through xnselect returning -ECHRNG */
		err = __cobalt_select_bind_all(selector, in_fds, nfds);
		if (err)
			return err;
	}

	do {
		err = xnselect(selector, out_fds, in_fds, nfds, timeout, mode);
		if (err == -ECHRNG) {
			int bind_err = __cobalt_select_bind_all(selector,
								out_fds, nfds);
			if (bind_err)
				return bind_err;
		}
	} while (err == -ECHRNG);

out:
	if (to && (err > 0 || err == -EINTR)) {
		xnsticks_t diff = timeout - clock_get_ticks(CLOCK_MONOTONIC);
		if (diff > 0)
			ticks2ts64(to, diff);
		else
			to->tv_sec = to->tv_nsec = 0;
	}

	if (err == -EINTR && signal_pending(current)) {
		xnthread_set_localinfo(curr, XNSYSRST);

		restart = &current->restart_block;
		restart->fn = cobalt_restart_syscall_placeholder;
		restart->nanosleep.expires = timeout;

		return -ERESTARTSYS;
	}

	if (err >= 0)
		for (i = 0; i < XNSELECT_MAX_TYPES; i++) {
			if (!ufd_sets[i])
				continue;
#ifdef CONFIG_XENO_ARCH_SYS3264
			if (compat) {
				if (sys32_put_fdset(ufd_sets[i], out_fds[i],
						    sizeof(fd_set)))
					return -EFAULT;
			} else
#endif
			{
				if (cobalt_copy_to_user((void __user *)ufd_sets[i],
							out_fds[i], sizeof(fd_set)))
					return -EFAULT;
			}
		}
	return err;
}

/* int select(int, fd_set *, fd_set *, fd_set *, struct __kernel_old_timeval *) */
COBALT_SYSCALL(select, primary,
	       (int nfds,
		fd_set __user *u_rfds,
		fd_set __user *u_wfds,
		fd_set __user *u_xfds,
		struct __kernel_old_timeval __user *u_tv))
{
	struct timespec64 ts64, *to = NULL;
	struct __kernel_old_timeval tv;
	int ret;

	if (u_tv && (!access_ok(u_tv, sizeof(tv)) ||
		     cobalt_copy_from_user(&tv, u_tv, sizeof(tv))))
		return -EFAULT;

	if (u_tv) {
		ts64 = cobalt_timeval_to_timespec64(&tv);
		to = &ts64;
	}

	ret = __cobalt_select(nfds, u_rfds, u_wfds, u_xfds, to, false);

	if (u_tv) {
		tv = cobalt_timespec64_to_timeval(to);
		/*
		 * Writing to u_tv might fail. We ignore a possible error here
		 * to report back the number of changed fds (or the error).
		 * This is aligned with the Linux select() implementation.
		 */
		cobalt_copy_to_user(u_tv, &tv, sizeof(tv));
	}

	return ret;
}

COBALT_SYSCALL(pselect64, primary,
	       (int nfds,
		fd_set __user *u_rfds,
		fd_set __user *u_wfds,
		fd_set __user *u_xfds,
		struct __kernel_timespec __user *u_ts))
{
	struct timespec64 ts64, *to = NULL;
	int ret = 0;

	if (u_ts) {
		ret = cobalt_get_timespec64(&ts64, u_ts);
		to = &ts64;
	}

	if (ret)
		return ret;

	ret = __cobalt_select(nfds, u_rfds, u_wfds, u_xfds, to, false);

	/*
	 * Normally pselect() would not write back the modified timeout. As we
	 * only use it for keeping select() y2038 safe we do it here as well.
	 *
	 * See CoBaLt_select() for some notes about error handling.
	 */
	if (u_ts)
		cobalt_put_timespec64(&ts64, u_ts);

	return ret;
}
