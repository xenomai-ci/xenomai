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
#ifndef _COBALT_POSIX_SYSCALL32_H
#define _COBALT_POSIX_SYSCALL32_H

#include <cobalt/kernel/compat.h>

struct cobalt_mutex_shadow;
struct cobalt_event_shadow;
struct cobalt_cond_shadow;
struct cobalt_sem_shadow;
struct cobalt_monitor_shadow;

COBALT_SYSCALL32emu_DECL(thread_setschedprio,
			 (compat_ulong_t pth,
			  int prio,
			  __u32 __user *u_winoff,
			  int __user *u_promoted));

COBALT_SYSCALL32emu_DECL(clock_getres,
			 (clockid_t clock_id,
			  struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_gettime,
			 (clockid_t clock_id,
			  struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_settime,
			 (clockid_t clock_id,
			  const struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(clock_adjtime,
			 (clockid_t clock_id,
			  struct old_timex32 __user *u_tx));

COBALT_SYSCALL32emu_DECL(clock_nanosleep,
			 (clockid_t clock_id, int flags,
			  const struct old_timespec32 __user *u_rqt,
			  struct old_timespec32 __user *u_rmt));

COBALT_SYSCALL32emu_DECL(mutex_timedlock,
			 (struct cobalt_mutex_shadow __user *u_mx,
			  const struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(cond_wait_prologue,
			 (struct cobalt_cond_shadow __user *u_cnd,
			  struct cobalt_mutex_shadow __user *u_mx,
			  int __user *u_err,
			  unsigned int timed,
			  struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_open,
			 (const char __user *u_name, int oflags,
			  mode_t mode, struct compat_mq_attr __user *u_attr));

COBALT_SYSCALL32emu_DECL(mq_getattr,
			 (mqd_t uqd, struct compat_mq_attr __user *u_attr));

COBALT_SYSCALL32emu_DECL(mq_timedsend,
			 (mqd_t uqd, const void __user *u_buf, size_t len,
			  unsigned int prio,
			  const struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_timedreceive,
			 (mqd_t uqd, void __user *u_buf,
			  compat_ssize_t __user *u_len,
			  unsigned int __user *u_prio,
			  const struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_timedreceive64,
			 (mqd_t uqd, void __user *u_buf,
			  compat_ssize_t __user *u_len,
			  unsigned int __user *u_prio,
			  const struct __kernel_timespec __user *u_ts));

COBALT_SYSCALL32emu_DECL(mq_notify,
			 (mqd_t fd, const struct compat_sigevent *__user u_cev));

COBALT_SYSCALL32emu_DECL(timer_create,
			 (clockid_t clock,
			  const struct compat_sigevent __user *u_sev,
			  timer_t __user *u_tm));

COBALT_SYSCALL32emu_DECL(timer_settime,
			 (timer_t tm, int flags,
			  const struct old_itimerspec32 __user *u_newval,
			  struct old_itimerspec32 __user *u_oldval));

COBALT_SYSCALL32emu_DECL(timer_gettime,
			 (timer_t tm,
			  struct old_itimerspec32 __user *u_val));

COBALT_SYSCALL32emu_DECL(timerfd_settime,
			 (int fd, int flags,
			  const struct old_itimerspec32 __user *new_value,
			  struct old_itimerspec32 __user *old_value));

COBALT_SYSCALL32emu_DECL(timerfd_gettime,
			 (int fd, struct old_itimerspec32 __user *value));

COBALT_SYSCALL32emu_DECL(sigwait,
			 (const compat_sigset_t __user *u_set,
			  int __user *u_sig));

COBALT_SYSCALL32emu_DECL(sigtimedwait,
			 (const compat_sigset_t __user *u_set,
			  struct compat_siginfo __user *u_si,
			  const struct old_timespec32 __user *u_timeout));

COBALT_SYSCALL32emu_DECL(sigtimedwait64,
			 (const compat_sigset_t __user *u_set,
			  struct compat_siginfo __user *u_si,
			  const struct __kernel_timespec __user *u_timeout));

COBALT_SYSCALL32emu_DECL(sigwaitinfo,
			 (const compat_sigset_t __user *u_set,
			  struct compat_siginfo __user *u_si));

COBALT_SYSCALL32emu_DECL(sigpending,
			 (compat_old_sigset_t __user *u_set));

COBALT_SYSCALL32emu_DECL(sigqueue,
			 (pid_t pid, int sig,
			  const union compat_sigval __user *u_value));

COBALT_SYSCALL32emu_DECL(monitor_wait,
			 (struct cobalt_monitor_shadow __user *u_mon,
			  int event, const struct old_timespec32 __user *u_ts,
			  int __user *u_ret));

COBALT_SYSCALL32emu_DECL(event_wait,
			 (struct cobalt_event_shadow __user *u_event,
			  unsigned int bits,
			  unsigned int __user *u_bits_r,
			  int mode, const struct old_timespec32 __user *u_ts));

COBALT_SYSCALL32emu_DECL(select,
			 (int nfds,
			  compat_fd_set __user *u_rfds,
			  compat_fd_set __user *u_wfds,
			  compat_fd_set __user *u_xfds,
			  struct old_timeval32 __user *u_tv));

COBALT_SYSCALL32emu_DECL(recvmsg,
			 (int fd, struct compat_msghdr __user *umsg,
			  int flags));

COBALT_SYSCALL32emu_DECL(recvmmsg,
			 (int fd, struct compat_mmsghdr __user *u_msgvec,
			  unsigned int vlen,
			  unsigned int flags, struct old_timespec32 *u_timeout));

COBALT_SYSCALL32emu_DECL(recvmmsg64,
			 (int fd, struct compat_mmsghdr __user *u_msgvec,
			  unsigned int vlen,
			  unsigned int flags,
			  struct __kernel_timespec *u_timeout));

COBALT_SYSCALL32emu_DECL(sendmsg,
			 (int fd, struct compat_msghdr __user *umsg,
			  int flags));

COBALT_SYSCALL32emu_DECL(sendmmsg,
			 (int fd, struct compat_mmsghdr __user *u_msgvec, unsigned int vlen,
			  unsigned int flags));

COBALT_SYSCALL32emu_DECL(mmap,
			 (int fd,
			  struct compat_rtdm_mmap_request __user *u_rma,
			  compat_uptr_t __user *u_addrp));

COBALT_SYSCALL32emu_DECL(backtrace,
			 (int nr, compat_ulong_t __user *u_backtrace,
			  int reason));

COBALT_SYSCALL32emu_DECL(sem_open,
			 (compat_uptr_t __user *u_addrp,
			  const char __user *u_name,
			  int oflags, mode_t mode, unsigned int value));

COBALT_SYSCALL32emu_DECL(sem_timedwait,
			 (struct cobalt_sem_shadow __user *u_sem,
			  const struct old_timespec32 __user *u_ts));

#endif /* !_COBALT_POSIX_SYSCALL32_H */
