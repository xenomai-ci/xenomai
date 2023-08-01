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
#include <linux/types.h>
#include <linux/err.h>
#include <cobalt/uapi/syscall.h>
#include <cobalt/kernel/time.h>
#include <xenomai/rtdm/internal.h>
#include "internal.h"
#include "syscall32.h"
#include "thread.h"
#include "mutex.h"
#include "cond.h"
#include "sem.h"
#include "sched.h"
#include "clock.h"
#include "timer.h"
#include "timerfd.h"
#include "signal.h"
#include "monitor.h"
#include "event.h"
#include "mqueue.h"
#include "io.h"
#include "../debug.h"

COBALT_SYSCALL32emu(thread_setschedprio, conforming,
		    (compat_ulong_t pth,
		     int prio,
		     __u32 __user *u_winoff,
		     int __user *u_promoted))
{
	return cobalt_thread_setschedprio(pth, prio, u_winoff, u_promoted);
}

static inline int sys32_fetch_timeout(struct timespec64 *ts,
				      const void __user *u_ts)
{
	return u_ts == NULL ? -EFAULT :
		sys32_get_timespec(ts, u_ts);
}

COBALT_SYSCALL32emu(sem_open, lostage,
		    (compat_uptr_t __user *u_addrp,
		     const char __user *u_name,
		     int oflags, mode_t mode, unsigned int value))
{
	struct cobalt_sem_shadow __user *usm;
	compat_uptr_t cusm;

	if (!access_ok(u_addrp, sizeof(*u_addrp)) ||
	    __xn_get_user(cusm, u_addrp))
		return -EFAULT;

	usm = __cobalt_sem_open(compat_ptr(cusm), u_name, oflags, mode, value);
	if (IS_ERR(usm))
		return PTR_ERR(usm);

	return __xn_put_user(ptr_to_compat(usm), u_addrp) ? -EFAULT : 0;
}

COBALT_SYSCALL32emu(sem_timedwait, primary,
		    (struct cobalt_sem_shadow __user *u_sem,
		     const struct old_timespec32 __user *u_ts))
{
	int ret = 1;
	struct timespec64 ts64;

	if (u_ts)
		ret = sys32_fetch_timeout(&ts64, u_ts);

	return __cobalt_sem_timedwait(u_sem, ret ? NULL : &ts64);
}

COBALT_SYSCALL32emu(clock_getres, current,
		    (clockid_t clock_id,
		     struct old_timespec32 __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_getres(clock_id, &ts);
	if (ret)
		return ret;

	return u_ts ? sys32_put_timespec(u_ts, &ts) : 0;
}

COBALT_SYSCALL32emu(clock_gettime, current,
		    (clockid_t clock_id,
		     struct old_timespec32 __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = __cobalt_clock_gettime(clock_id, &ts);
	if (ret)
		return ret;

	return sys32_put_timespec(u_ts, &ts);
}

COBALT_SYSCALL32emu(clock_settime, current,
		    (clockid_t clock_id,
		     const struct old_timespec32 __user *u_ts))
{
	struct timespec64 ts;
	int ret;

	ret = sys32_get_timespec(&ts, u_ts);
	if (ret)
		return ret;

	return __cobalt_clock_settime(clock_id, &ts);
}

COBALT_SYSCALL32emu(clock_adjtime, current,
		    (clockid_t clock_id, struct old_timex32 __user *u_tx))
{
	struct __kernel_timex tx;
	int ret;

	ret = sys32_get_timex(&tx, u_tx);
	if (ret)
		return ret;

	ret = __cobalt_clock_adjtime(clock_id, &tx);
	if (ret)
		return ret;

	return sys32_put_timex(u_tx, &tx);
}

COBALT_SYSCALL32emu(clock_nanosleep, primary,
		    (clockid_t clock_id, int flags,
		     const struct old_timespec32 __user *u_rqt,
		     struct old_timespec32 __user *u_rmt))
{
	struct timespec64 rqt, rmt, *rmtp = NULL;
	int ret;

	if (u_rmt)
		rmtp = &rmt;

	ret = sys32_get_timespec(&rqt, u_rqt);
	if (ret)
		return ret;

	ret = __cobalt_clock_nanosleep(clock_id, flags, &rqt, rmtp);
	if (ret == -EINTR && flags == 0 && rmtp)
		ret = sys32_put_timespec(u_rmt, rmtp);

	return ret;
}

COBALT_SYSCALL32emu(mutex_timedlock, primary,
		    (struct cobalt_mutex_shadow __user *u_mx,
		     const struct old_timespec32 __user *u_ts))
{
	return __cobalt_mutex_timedlock_break(u_mx, u_ts, sys32_fetch_timeout);
}

COBALT_SYSCALL32emu(cond_wait_prologue, nonrestartable,
		    (struct cobalt_cond_shadow __user *u_cnd,
		     struct cobalt_mutex_shadow __user *u_mx,
		     int __user *u_err,
		     unsigned int timed,
		     struct old_timespec32 __user *u_ts))
{
	return __cobalt_cond_wait_prologue(u_cnd, u_mx, u_err, u_ts,
					   timed ? sys32_fetch_timeout : NULL);
}

COBALT_SYSCALL32emu(mq_open, lostage,
		    (const char __user *u_name, int oflags,
		     mode_t mode, struct compat_mq_attr __user *u_attr))
{
	struct mq_attr _attr, *attr = &_attr;
	int ret;

	if ((oflags & O_CREAT) && u_attr) {
		ret = sys32_get_mqattr(&_attr, u_attr);
		if (ret)
			return ret;
	} else
		attr = NULL;

	return __cobalt_mq_open(u_name, oflags, mode, attr);
}

COBALT_SYSCALL32emu(mq_getattr, current,
		    (mqd_t uqd, struct compat_mq_attr __user *u_attr))
{
	struct mq_attr attr;
	int ret;

	ret = __cobalt_mq_getattr(uqd, &attr);
	if (ret)
		return ret;

	return sys32_put_mqattr(u_attr, &attr);
}

COBALT_SYSCALL32emu(mq_timedsend, primary,
		    (mqd_t uqd, const void __user *u_buf, size_t len,
		     unsigned int prio,
		     const struct old_timespec32 __user *u_ts))
{
	return __cobalt_mq_timedsend(uqd, u_buf, len, prio,
				     u_ts, u_ts ? sys32_fetch_timeout : NULL);
}

COBALT_SYSCALL32emu(mq_timedreceive, primary,
		    (mqd_t uqd, void __user *u_buf,
		     compat_ssize_t __user *u_len,
		     unsigned int __user *u_prio,
		     const struct old_timespec32 __user *u_ts))
{
	compat_ssize_t clen;
	ssize_t len;
	int ret;

	ret = cobalt_copy_from_user(&clen, u_len, sizeof(*u_len));
	if (ret)
		return ret;

	len = clen;
	ret = __cobalt_mq_timedreceive(uqd, u_buf, &len, u_prio,
				       u_ts, u_ts ? sys32_fetch_timeout : NULL);
	clen = len;

	return ret ?: cobalt_copy_to_user(u_len, &clen, sizeof(*u_len));
}

COBALT_SYSCALL32emu(mq_timedreceive64, primary,
		    (mqd_t uqd, void __user *u_buf,
		     compat_ssize_t __user *u_len,
		     unsigned int __user *u_prio,
		     const struct __kernel_timespec __user *u_ts))
{
	compat_ssize_t clen;
	ssize_t len;
	int ret;

	ret = cobalt_copy_from_user(&clen, u_len, sizeof(*u_len));
	if (ret)
		return ret;

	len = clen;
	ret = __cobalt_mq_timedreceive64(uqd, u_buf, &len, u_prio, u_ts);
	clen = len;

	return ret ?: cobalt_copy_to_user(u_len, &clen, sizeof(*u_len));
}

COBALT_SYSCALL32emu(mq_notify, primary,
		    (mqd_t fd, const struct compat_sigevent *__user u_cev))
{
	struct sigevent sev;
	int ret;

	if (u_cev) {
		ret = sys32_get_sigevent(&sev, u_cev);
		if (ret)
			return ret;
	}

	return __cobalt_mq_notify(fd, u_cev ? &sev : NULL);
}

COBALT_SYSCALL32emu(timer_create, current,
		    (clockid_t clock,
		     const struct compat_sigevent __user *u_sev,
		     timer_t __user *u_tm))
{
	struct sigevent sev, *evp = NULL;
	int ret;

	if (u_sev) {
		evp = &sev;
		ret = sys32_get_sigevent(&sev, u_sev);
		if (ret)
			return ret;
	}

	return __cobalt_timer_create(clock, evp, u_tm);
}

COBALT_SYSCALL32emu(timer_settime, primary,
		    (timer_t tm, int flags,
		     const struct old_itimerspec32 __user *u_newval,
		     struct old_itimerspec32 __user *u_oldval))
{
	struct itimerspec64 newv, oldv, *oldvp = &oldv;
	int ret;

	if (u_oldval == NULL)
		oldvp = NULL;

	ret = sys32_get_itimerspec(&newv, u_newval);
	if (ret)
		return ret;

	ret = __cobalt_timer_settime(tm, flags, &newv, oldvp);
	if (ret)
		return ret;

	if (oldvp) {
		ret = sys32_put_itimerspec(u_oldval, oldvp);
		if (ret)
			__cobalt_timer_settime(tm, flags, oldvp, NULL);
	}

	return ret;
}

COBALT_SYSCALL32emu(timer_gettime, current,
		    (timer_t tm, struct old_itimerspec32 __user *u_val))
{
	struct itimerspec64 val;
	int ret;

	ret = __cobalt_timer_gettime(tm, &val);

	return ret ?: sys32_put_itimerspec(u_val, &val);
}

COBALT_SYSCALL32emu(timerfd_settime, primary,
		    (int fd, int flags,
		     const struct old_itimerspec32 __user *new_value,
		     struct old_itimerspec32 __user *old_value))
{
	struct itimerspec64 ovalue, value;
	int ret;

	ret = sys32_get_itimerspec(&value, new_value);
	if (ret)
		return ret;

	ret = __cobalt_timerfd_settime(fd, flags, &value, &ovalue);
	if (ret)
		return ret;

	if (old_value) {
		ret = sys32_put_itimerspec(old_value, &ovalue);
		value.it_value.tv_sec = 0;
		value.it_value.tv_nsec = 0;
		__cobalt_timerfd_settime(fd, flags, &value, NULL);
	}

	return ret;
}

COBALT_SYSCALL32emu(timerfd_gettime, current,
		    (int fd, struct old_itimerspec32 __user *curr_value))
{
	struct itimerspec64 value;
	int ret;

	ret = __cobalt_timerfd_gettime(fd, &value);

	return ret ?: sys32_put_itimerspec(curr_value, &value);
}

COBALT_SYSCALL32emu(sigwait, primary,
		    (const compat_sigset_t __user *u_set,
		     int __user *u_sig))
{
	sigset_t set;
	int ret, sig;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	sig = __cobalt_sigwait(&set);
	if (sig < 0)
		return sig;

	return cobalt_copy_to_user(u_sig, &sig, sizeof(*u_sig));
}

COBALT_SYSCALL32emu(sigtimedwait, nonrestartable,
		    (const compat_sigset_t __user *u_set,
		     struct compat_siginfo __user *u_si,
		     const struct old_timespec32 __user *u_timeout))
{
	struct timespec64 timeout;
	sigset_t set;
	int ret;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	ret = sys32_get_timespec(&timeout, u_timeout);
	if (ret)
		return ret;

	return __cobalt_sigtimedwait(&set, &timeout, u_si, true);
}

COBALT_SYSCALL32emu(sigtimedwait64, nonrestartable,
		    (const compat_sigset_t __user *u_set,
		     struct compat_siginfo __user *u_si,
		     const struct __kernel_timespec __user *u_timeout))
{
	struct timespec64 timeout;
	sigset_t set;
	int ret;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	ret = cobalt_get_timespec64(&timeout, u_timeout);
	if (ret)
		return ret;

	return __cobalt_sigtimedwait(&set, &timeout, u_si, true);
}

COBALT_SYSCALL32emu(sigwaitinfo, nonrestartable,
		    (const compat_sigset_t __user *u_set,
		     struct compat_siginfo __user *u_si))
{
	sigset_t set;
	int ret;

	ret = sys32_get_sigset(&set, u_set);
	if (ret)
		return ret;

	return __cobalt_sigwaitinfo(&set, u_si, true);
}

COBALT_SYSCALL32emu(sigpending, primary, (compat_old_sigset_t __user *u_set))
{
	struct cobalt_thread *curr = cobalt_current_thread();

	return sys32_put_sigset((compat_sigset_t *)u_set, &curr->sigpending);
}

COBALT_SYSCALL32emu(sigqueue, conforming,
		    (pid_t pid, int sig,
		     const union compat_sigval __user *u_value))
{
	union sigval val;
	int ret;

	ret = sys32_get_sigval(&val, u_value);

	return ret ?: __cobalt_sigqueue(pid, sig, &val);
}

COBALT_SYSCALL32emu(monitor_wait, nonrestartable,
		    (struct cobalt_monitor_shadow __user *u_mon,
		     int event, const struct old_timespec32 __user *u_ts,
		     int __user *u_ret))
{
	struct timespec64 ts, *tsp = NULL;
	int ret;

	if (u_ts) {
		tsp = &ts;
		ret = sys32_get_timespec(&ts, u_ts);
		if (ret)
			return ret;
	}

	return __cobalt_monitor_wait(u_mon, event, tsp, u_ret);
}

COBALT_SYSCALL32emu(event_wait, primary,
		    (struct cobalt_event_shadow __user *u_event,
		     unsigned int bits,
		     unsigned int __user *u_bits_r,
		     int mode, const struct old_timespec32 __user *u_ts))
{
	struct timespec64 ts, *tsp = NULL;
	int ret;

	if (u_ts) {
		tsp = &ts;
		ret = sys32_get_timespec(&ts, u_ts);
		if (ret)
			return ret;
	}

	return __cobalt_event_wait(u_event, bits, u_bits_r, mode, tsp);
}

COBALT_SYSCALL32emu(select, primary,
		    (int nfds,
		     compat_fd_set __user *u_rfds,
		     compat_fd_set __user *u_wfds,
		     compat_fd_set __user *u_xfds,
		     struct old_timeval32 __user *u_tv))
{
	struct timespec64 ts64, *to = NULL;
	struct __kernel_old_timeval tv;
	int ret;

	if (u_tv &&
	    (!access_ok(u_tv, sizeof(tv)) || sys32_get_timeval(&tv, u_tv)))
		return -EFAULT;

	if (u_tv) {
		ts64 = cobalt_timeval_to_timespec64(&tv);
		to = &ts64;
	}

	ret = __cobalt_select(nfds, u_rfds, u_wfds, u_xfds, to, true);

	if (u_tv) {
		tv = cobalt_timespec64_to_timeval(to);
		/* See CoBaLt_select() */
		sys32_put_timeval(u_tv, &tv);
	}

	return ret;
}

COBALT_SYSCALL32emu(recvmsg, handover,
		    (int fd, struct compat_msghdr __user *umsg,
		     int flags))
{
	struct user_msghdr m;
	ssize_t ret;

	ret = sys32_get_msghdr(&m, umsg);
	if (ret)
		return ret;

	ret = rtdm_fd_recvmsg(fd, &m, flags);
	if (ret < 0)
		return ret;

	return sys32_put_msghdr(umsg, &m) ?: ret;
}

static int get_timespec32(struct timespec64 *ts,
			  const void __user *u_ts)
{
	return sys32_get_timespec(ts, u_ts);
}

static int get_mmsg32(struct mmsghdr *mmsg, void __user *u_mmsg)
{
	return sys32_get_mmsghdr(mmsg, u_mmsg);
}

static int put_mmsg32(void __user **u_mmsg_p, const struct mmsghdr *mmsg)
{
	struct compat_mmsghdr __user **p = (struct compat_mmsghdr **)u_mmsg_p,
		*q __user = (*p)++;

	return sys32_put_mmsghdr(q, mmsg);
}

COBALT_SYSCALL32emu(recvmmsg, primary,
	       (int ufd, struct compat_mmsghdr __user *u_msgvec, unsigned int vlen,
		unsigned int flags, struct old_timespec32 *u_timeout))
{
	return __rtdm_fd_recvmmsg(ufd, u_msgvec, vlen, flags, u_timeout,
				  get_mmsg32, put_mmsg32,
				  get_timespec32);
}

COBALT_SYSCALL32emu(recvmmsg64, primary,
		    (int ufd, struct compat_mmsghdr __user *u_msgvec,
		     unsigned int vlen, unsigned int flags,
		     struct __kernel_timespec *u_timeout))
{
	return __rtdm_fd_recvmmsg64(ufd, u_msgvec, vlen, flags, u_timeout,
				    get_mmsg32, put_mmsg32);
}

COBALT_SYSCALL32emu(sendmsg, handover,
		    (int fd, struct compat_msghdr __user *umsg, int flags))
{
	struct user_msghdr m;
	int ret;

	ret = sys32_get_msghdr(&m, umsg);

	return ret ?: rtdm_fd_sendmsg(fd, &m, flags);
}

static int put_mmsglen32(void __user **u_mmsg_p, const struct mmsghdr *mmsg)
{
	struct compat_mmsghdr __user **p = (struct compat_mmsghdr **)u_mmsg_p,
		*q __user = (*p)++;

	return __xn_put_user(mmsg->msg_len, &q->msg_len);
}

COBALT_SYSCALL32emu(sendmmsg, primary,
		    (int fd, struct compat_mmsghdr __user *u_msgvec, unsigned int vlen,
		     unsigned int flags))
{
	return __rtdm_fd_sendmmsg(fd, u_msgvec, vlen, flags,
				  get_mmsg32, put_mmsglen32);
}

COBALT_SYSCALL32emu(mmap, lostage,
		    (int fd, struct compat_rtdm_mmap_request __user *u_crma,
		     compat_uptr_t __user *u_caddrp))
{
	struct _rtdm_mmap_request rma;
	compat_uptr_t u_caddr;
	void *u_addr = NULL;
	int ret;

	if (u_crma == NULL ||
	    !access_ok(u_crma, sizeof(*u_crma)) ||
	    __xn_get_user(rma.length, &u_crma->length) ||
	    __xn_get_user(rma.offset, &u_crma->offset) ||
	    __xn_get_user(rma.prot, &u_crma->prot) ||
	    __xn_get_user(rma.flags, &u_crma->flags))
	  return -EFAULT;

	ret = rtdm_fd_mmap(fd, &rma, &u_addr);
	if (ret)
		return ret;

	u_caddr = ptr_to_compat(u_addr);

	return cobalt_copy_to_user(u_caddrp, &u_caddr, sizeof(u_caddr));
}

COBALT_SYSCALL32emu(backtrace, current,
		    (int nr, compat_ulong_t __user *u_backtrace,
		     int reason))
{
	compat_ulong_t cbacktrace[SIGSHADOW_BACKTRACE_DEPTH];
	unsigned long backtrace[SIGSHADOW_BACKTRACE_DEPTH];
	int ret, n;

	if (nr <= 0)
		return 0;

	if (nr > SIGSHADOW_BACKTRACE_DEPTH)
		nr = SIGSHADOW_BACKTRACE_DEPTH;

	ret = cobalt_copy_from_user(cbacktrace, u_backtrace,
				       nr * sizeof(compat_ulong_t));
	if (ret)
		return ret;

	for (n = 0; n < nr; n++)
		backtrace [n] = cbacktrace[n];

	xndebug_trace_relax(nr, backtrace, reason);

	return 0;
}
