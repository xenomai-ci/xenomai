/*
 * Copyright (C) 2014 Jan Kiszka <jan.kiszka@siemens.com>.
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cobalt_posix

#if !defined(_TRACE_COBALT_POSIX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COBALT_POSIX_H

#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include <xenomai/posix/cond.h>
#include <xenomai/posix/mqueue.h>
#include <xenomai/posix/event.h>

#define __timespec_fields(__name)				\
	__field(time64_t, tv_sec_##__name)			\
	__field(long, tv_nsec_##__name)

#define __assign_timespec(__to, __from)				\
	do {							\
		__entry->tv_sec_##__to = (__from)->tv_sec;	\
		__entry->tv_nsec_##__to = (__from)->tv_nsec;	\
	} while (0)

#define __timespec_args(__name)					\
	(long long)__entry->tv_sec_##__name, __entry->tv_nsec_##__name

#ifdef CONFIG_XENO_ARCH_SYS3264
#define __sc_compat(__name)	, { sc_cobalt_##__name + __COBALT_COMPAT32_BASE, "compat-" #__name }
#else
#define __sc_compat(__name)
#endif

#define __cobalt_symbolic_syscall(__name)				\
	{ sc_cobalt_##__name, #__name }					\
	__sc_compat(__name)						\

#define __cobalt_syscall_name(__nr)					\
	__print_symbolic((__nr),					\
		__cobalt_symbolic_syscall(bind),			\
		__cobalt_symbolic_syscall(thread_create),		\
		__cobalt_symbolic_syscall(thread_getpid),		\
		__cobalt_symbolic_syscall(thread_setmode),		\
		__cobalt_symbolic_syscall(thread_setname),		\
		__cobalt_symbolic_syscall(thread_join),			\
		__cobalt_symbolic_syscall(thread_kill),			\
		__cobalt_symbolic_syscall(thread_setschedparam_ex),	\
		__cobalt_symbolic_syscall(thread_getschedparam_ex),	\
		__cobalt_symbolic_syscall(thread_setschedprio),		\
		__cobalt_symbolic_syscall(thread_getstat),		\
		__cobalt_symbolic_syscall(sem_init),			\
		__cobalt_symbolic_syscall(sem_destroy),			\
		__cobalt_symbolic_syscall(sem_post),			\
		__cobalt_symbolic_syscall(sem_wait),			\
		__cobalt_symbolic_syscall(sem_trywait),			\
		__cobalt_symbolic_syscall(sem_getvalue),		\
		__cobalt_symbolic_syscall(sem_open),			\
		__cobalt_symbolic_syscall(sem_close),			\
		__cobalt_symbolic_syscall(sem_unlink),			\
		__cobalt_symbolic_syscall(sem_timedwait),		\
		__cobalt_symbolic_syscall(sem_inquire),			\
		__cobalt_symbolic_syscall(sem_broadcast_np),		\
		__cobalt_symbolic_syscall(clock_getres),		\
		__cobalt_symbolic_syscall(clock_gettime),		\
		__cobalt_symbolic_syscall(clock_settime),		\
		__cobalt_symbolic_syscall(clock_nanosleep),		\
		__cobalt_symbolic_syscall(mutex_init),			\
		__cobalt_symbolic_syscall(mutex_check_init),		\
		__cobalt_symbolic_syscall(mutex_destroy),		\
		__cobalt_symbolic_syscall(mutex_lock),			\
		__cobalt_symbolic_syscall(mutex_timedlock),		\
		__cobalt_symbolic_syscall(mutex_trylock),		\
		__cobalt_symbolic_syscall(mutex_unlock),		\
		__cobalt_symbolic_syscall(cond_init),			\
		__cobalt_symbolic_syscall(cond_destroy),		\
		__cobalt_symbolic_syscall(cond_wait_prologue),		\
		__cobalt_symbolic_syscall(cond_wait_epilogue),		\
		__cobalt_symbolic_syscall(mq_open),			\
		__cobalt_symbolic_syscall(mq_close),			\
		__cobalt_symbolic_syscall(mq_unlink),			\
		__cobalt_symbolic_syscall(mq_getattr),			\
		__cobalt_symbolic_syscall(mq_timedsend),		\
		__cobalt_symbolic_syscall(mq_timedreceive),		\
		__cobalt_symbolic_syscall(mq_notify),			\
		__cobalt_symbolic_syscall(sched_minprio),		\
		__cobalt_symbolic_syscall(sched_maxprio),		\
		__cobalt_symbolic_syscall(sched_weightprio),		\
		__cobalt_symbolic_syscall(sched_yield),			\
		__cobalt_symbolic_syscall(sched_setscheduler_ex),	\
		__cobalt_symbolic_syscall(sched_getscheduler_ex),	\
		__cobalt_symbolic_syscall(sched_setconfig_np),		\
		__cobalt_symbolic_syscall(sched_getconfig_np),		\
		__cobalt_symbolic_syscall(timer_create),		\
		__cobalt_symbolic_syscall(timer_delete),		\
		__cobalt_symbolic_syscall(timer_settime),		\
		__cobalt_symbolic_syscall(timer_gettime),		\
		__cobalt_symbolic_syscall(timer_getoverrun),		\
		__cobalt_symbolic_syscall(timerfd_create),		\
		__cobalt_symbolic_syscall(timerfd_settime),		\
		__cobalt_symbolic_syscall(timerfd_gettime),		\
		__cobalt_symbolic_syscall(sigwait),			\
		__cobalt_symbolic_syscall(sigwaitinfo),			\
		__cobalt_symbolic_syscall(sigtimedwait),		\
		__cobalt_symbolic_syscall(sigpending),			\
		__cobalt_symbolic_syscall(kill),			\
		__cobalt_symbolic_syscall(sigqueue),			\
		__cobalt_symbolic_syscall(monitor_init),		\
		__cobalt_symbolic_syscall(monitor_destroy),		\
		__cobalt_symbolic_syscall(monitor_enter),		\
		__cobalt_symbolic_syscall(monitor_wait),		\
		__cobalt_symbolic_syscall(monitor_sync),		\
		__cobalt_symbolic_syscall(monitor_exit),		\
		__cobalt_symbolic_syscall(event_init),			\
		__cobalt_symbolic_syscall(event_wait),			\
		__cobalt_symbolic_syscall(event_sync),			\
		__cobalt_symbolic_syscall(event_destroy),		\
		__cobalt_symbolic_syscall(event_inquire),		\
		__cobalt_symbolic_syscall(open),			\
		__cobalt_symbolic_syscall(socket),			\
		__cobalt_symbolic_syscall(close),			\
		__cobalt_symbolic_syscall(ioctl),			\
		__cobalt_symbolic_syscall(read),			\
		__cobalt_symbolic_syscall(write),			\
		__cobalt_symbolic_syscall(recvmsg),			\
		__cobalt_symbolic_syscall(sendmsg),			\
		__cobalt_symbolic_syscall(mmap),			\
		__cobalt_symbolic_syscall(select),			\
		__cobalt_symbolic_syscall(fcntl),			\
		__cobalt_symbolic_syscall(migrate),			\
		__cobalt_symbolic_syscall(trace),			\
		__cobalt_symbolic_syscall(corectl),			\
		__cobalt_symbolic_syscall(get_current),			\
		__cobalt_symbolic_syscall(backtrace),			\
		__cobalt_symbolic_syscall(serialdbg),			\
		__cobalt_symbolic_syscall(extend),			\
		__cobalt_symbolic_syscall(ftrace_puts),			\
		__cobalt_symbolic_syscall(recvmmsg),			\
		__cobalt_symbolic_syscall(sendmmsg),			\
		__cobalt_symbolic_syscall(clock_adjtime),		\
		__cobalt_symbolic_syscall(sem_timedwait64),		\
		__cobalt_symbolic_syscall(clock_gettime64),		\
		__cobalt_symbolic_syscall(clock_settime64),		\
		__cobalt_symbolic_syscall(clock_nanosleep64),		\
		__cobalt_symbolic_syscall(clock_getres64),		\
		__cobalt_symbolic_syscall(clock_adjtime64),		\
		__cobalt_symbolic_syscall(mutex_timedlock64),		\
		__cobalt_symbolic_syscall(mq_timedsend64),  		\
		__cobalt_symbolic_syscall(mq_timedreceive64),		\
		__cobalt_symbolic_syscall(sigtimedwait64),		\
		__cobalt_symbolic_syscall(monitor_wait64),		\
		__cobalt_symbolic_syscall(event_wait64),		\
		__cobalt_symbolic_syscall(recvmmsg64),			\
		__cobalt_symbolic_syscall(cond_wait_prologue64),	\
		__cobalt_symbolic_syscall(timer_settime64),		\
		__cobalt_symbolic_syscall(timer_gettime64),		\
		__cobalt_symbolic_syscall(timerfd_settime64),		\
		__cobalt_symbolic_syscall(timerfd_gettime64),		\
		__cobalt_symbolic_syscall(pselect64))

DECLARE_EVENT_CLASS(cobalt_syscall_entry,
	TP_PROTO(unsigned int nr),
	TP_ARGS(nr),

	TP_STRUCT__entry(
		__field(unsigned int, nr)
	),

	TP_fast_assign(
		__entry->nr = nr;
	),

	TP_printk("syscall=%s", __cobalt_syscall_name(__entry->nr))
);

DECLARE_EVENT_CLASS(cobalt_syscall_exit,
	TP_PROTO(long result),
	TP_ARGS(result),

	TP_STRUCT__entry(
		__field(long, result)
	),

	TP_fast_assign(
		__entry->result = result;
	),

	TP_printk("result=%ld", __entry->result)
);

#define cobalt_print_sched_policy(__policy)			\
	__print_symbolic(__policy,				\
			 {SCHED_NORMAL, "normal"},		\
			 {SCHED_FIFO, "fifo"},			\
			 {SCHED_RR, "rr"},			\
			 {SCHED_TP, "tp"},			\
			 {SCHED_QUOTA, "quota"},		\
			 {SCHED_SPORADIC, "sporadic"},		\
			 {SCHED_COBALT, "cobalt"},		\
			 {SCHED_WEAK, "weak"})

const char *cobalt_trace_parse_sched_params(struct trace_seq *, int,
					    struct sched_param_ex *);

#define __parse_sched_params(policy, params)			\
	cobalt_trace_parse_sched_params(p, policy,		\
					(struct sched_param_ex *)(params))

DECLARE_EVENT_CLASS(cobalt_posix_schedparam,
	TP_PROTO(unsigned long pth, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex),

	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(int, policy)
		__dynamic_array(char, param_ex, sizeof(struct sched_param_ex))
	),

	TP_fast_assign(
		__entry->pth = pth;
		__entry->policy = policy;
		memcpy(__get_dynamic_array(param_ex), param_ex, sizeof(*param_ex));
	),

	TP_printk("pth=%p policy=%s param={ %s }",
		  (void *)__entry->pth,
		  cobalt_print_sched_policy(__entry->policy),
		  __parse_sched_params(__entry->policy,
				       __get_dynamic_array(param_ex))
	)
);

DECLARE_EVENT_CLASS(cobalt_posix_scheduler,
	TP_PROTO(pid_t pid, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pid, policy, param_ex),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(int, policy)
		__dynamic_array(char, param_ex, sizeof(struct sched_param_ex))
	),

	TP_fast_assign(
		__entry->pid = pid;
		__entry->policy = policy;
		memcpy(__get_dynamic_array(param_ex), param_ex, sizeof(*param_ex));
	),

	TP_printk("pid=%d policy=%s param={ %s }",
		  __entry->pid,
		  cobalt_print_sched_policy(__entry->policy),
		  __parse_sched_params(__entry->policy,
				       __get_dynamic_array(param_ex))
	)
);

DECLARE_EVENT_CLASS(cobalt_void,
	TP_PROTO(int dummy),
	TP_ARGS(dummy),
	TP_STRUCT__entry(
		__field(int, dummy)
	),
	TP_fast_assign(
		(void)dummy;
	),
	TP_printk("%s", "")
);

DEFINE_EVENT(cobalt_syscall_entry, cobalt_head_sysentry,
	TP_PROTO(unsigned int nr),
	TP_ARGS(nr)
);

DEFINE_EVENT(cobalt_syscall_exit, cobalt_head_sysexit,
	TP_PROTO(long result),
	TP_ARGS(result)
);

DEFINE_EVENT(cobalt_syscall_entry, cobalt_root_sysentry,
	TP_PROTO(unsigned int nr),
	TP_ARGS(nr)
);

DEFINE_EVENT(cobalt_syscall_exit, cobalt_root_sysexit,
	TP_PROTO(long result),
	TP_ARGS(result)
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_create,
	TP_PROTO(unsigned long pth, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_setschedparam,
	TP_PROTO(unsigned long pth, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

DEFINE_EVENT(cobalt_posix_schedparam, cobalt_pthread_getschedparam,
	TP_PROTO(unsigned long pth, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pth, policy, param_ex)
);

TRACE_EVENT(cobalt_pthread_setschedprio,
	TP_PROTO(unsigned long pth, int prio),
	TP_ARGS(pth, prio),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(int, prio)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__entry->prio = prio;
	),
	TP_printk("pth=%p prio=%d", (void *)__entry->pth, __entry->prio)
);

#define cobalt_print_thread_mode(__mode)			\
	__print_flags(__mode, "|",				\
		      {PTHREAD_WARNSW, "warnsw"},		\
		      {PTHREAD_LOCK_SCHED, "lock"},		\
		      {PTHREAD_DISABLE_LOCKBREAK, "nolockbreak"})

TRACE_EVENT(cobalt_pthread_setmode,
	TP_PROTO(int clrmask, int setmask),
	TP_ARGS(clrmask, setmask),
	TP_STRUCT__entry(
		__field(int, clrmask)
		__field(int, setmask)
	),
	TP_fast_assign(
		__entry->clrmask = clrmask;
		__entry->setmask = setmask;
	),
	TP_printk("clrmask=%#x(%s) setmask=%#x(%s)",
		  __entry->clrmask, cobalt_print_thread_mode(__entry->clrmask),
		  __entry->setmask, cobalt_print_thread_mode(__entry->setmask))
);

TRACE_EVENT(cobalt_pthread_setname,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__wrap_assign_str(name, name);
	),
	TP_printk("pth=%p name=%s", (void *)__entry->pth, __get_str(name))
);

DECLARE_EVENT_CLASS(cobalt_posix_pid,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid),
	TP_STRUCT__entry(
		__field(pid_t, pid)
	),
	TP_fast_assign(
		__entry->pid = pid;
	),
	TP_printk("pid=%d", __entry->pid)
);

DEFINE_EVENT(cobalt_posix_pid, cobalt_pthread_stat,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid)
);

TRACE_EVENT(cobalt_pthread_kill,
	TP_PROTO(unsigned long pth, int sig),
	TP_ARGS(pth, sig),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__field(int, sig)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__entry->sig = sig;
	),
	TP_printk("pth=%p sig=%d", (void *)__entry->pth, __entry->sig)
);

TRACE_EVENT(cobalt_pthread_join,
	TP_PROTO(unsigned long pth),
	TP_ARGS(pth),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
	),
	TP_fast_assign(
		__entry->pth = pth;
	),
	TP_printk("pth=%p", (void *)__entry->pth)
);

TRACE_EVENT(cobalt_pthread_pid,
	TP_PROTO(unsigned long pth),
	TP_ARGS(pth),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
	),
	TP_fast_assign(
		__entry->pth = pth;
	),
	TP_printk("pth=%p", (void *)__entry->pth)
);

TRACE_EVENT(cobalt_pthread_extend,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__wrap_assign_str(name, name);
	),
	TP_printk("pth=%p +personality=%s", (void *)__entry->pth, __get_str(name))
);

TRACE_EVENT(cobalt_pthread_restrict,
	TP_PROTO(unsigned long pth, const char *name),
	TP_ARGS(pth, name),
	TP_STRUCT__entry(
		__field(unsigned long, pth)
		__string(name, name)
	),
	TP_fast_assign(
		__entry->pth = pth;
		__wrap_assign_str(name, name);
	),
	TP_printk("pth=%p -personality=%s", (void *)__entry->pth, __get_str(name))
);

DEFINE_EVENT(cobalt_void, cobalt_pthread_yield,
	TP_PROTO(int dummy),
	TP_ARGS(dummy)
);

TRACE_EVENT(cobalt_sched_setconfig,
	TP_PROTO(int cpu, int policy, size_t len),
	TP_ARGS(cpu, policy, len),
	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, policy)
		__field(size_t, len)
	),
	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->policy = policy;
		__entry->len = len;
	),
	TP_printk("cpu=%d policy=%d(%s) len=%zu",
		  __entry->cpu, __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->len)
);

TRACE_EVENT(cobalt_sched_get_config,
	TP_PROTO(int cpu, int policy, size_t rlen),
	TP_ARGS(cpu, policy, rlen),
	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, policy)
		__field(ssize_t, rlen)
	),
	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->policy = policy;
		__entry->rlen = rlen;
	),
	TP_printk("cpu=%d policy=%d(%s) rlen=%Zd",
		  __entry->cpu, __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->rlen)
);

DEFINE_EVENT(cobalt_posix_scheduler, cobalt_sched_setscheduler,
	TP_PROTO(pid_t pid, int policy,
		 const struct sched_param_ex *param_ex),
	TP_ARGS(pid, policy, param_ex)
);

DEFINE_EVENT(cobalt_posix_pid, cobalt_sched_getscheduler,
	TP_PROTO(pid_t pid),
	TP_ARGS(pid)
);

DECLARE_EVENT_CLASS(cobalt_posix_prio_bound,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio),
	TP_STRUCT__entry(
		__field(int, policy)
		__field(int, prio)
	),
	TP_fast_assign(
		__entry->policy = policy;
		__entry->prio = prio;
	),
	TP_printk("policy=%d(%s) prio=%d",
		  __entry->policy,
		  cobalt_print_sched_policy(__entry->policy),
		  __entry->prio)
);

DEFINE_EVENT(cobalt_posix_prio_bound, cobalt_sched_min_prio,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio)
);

DEFINE_EVENT(cobalt_posix_prio_bound, cobalt_sched_max_prio,
	TP_PROTO(int policy, int prio),
	TP_ARGS(policy, prio)
);

DECLARE_EVENT_CLASS(cobalt_posix_sem,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle),
	TP_STRUCT__entry(
		__field(xnhandle_t, handle)
	),
	TP_fast_assign(
		__entry->handle = handle;
	),
	TP_printk("sem=%#x", __entry->handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_wait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_trywait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_timedwait,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_post,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_destroy,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_broadcast,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_inquire,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

TRACE_EVENT(cobalt_psem_getvalue,
	TP_PROTO(xnhandle_t handle, int value),
	TP_ARGS(handle, value),
	TP_STRUCT__entry(
		__field(xnhandle_t, handle)
		__field(int, value)
	),
	TP_fast_assign(
		__entry->handle = handle;
		__entry->value = value;
	),
	TP_printk("sem=%#x value=%d", __entry->handle, __entry->value)
);

#define cobalt_print_sem_flags(__flags)				\
  	__print_flags(__flags, "|",				\
			 {SEM_FIFO, "fifo"},			\
			 {SEM_PULSE, "pulse"},			\
			 {SEM_PSHARED, "pshared"},		\
			 {SEM_REPORT, "report"},		\
			 {SEM_WARNDEL, "warndel"},		\
			 {SEM_RAWCLOCK, "rawclock"},		\
			 {SEM_NOBUSYDEL, "nobusydel"})

TRACE_EVENT(cobalt_psem_init,
	TP_PROTO(const char *name, xnhandle_t handle,
		 int flags, unsigned int value),
	TP_ARGS(name, handle, flags, value),
	TP_STRUCT__entry(
		__string(name, name)
		__field(xnhandle_t, handle)
		__field(int, flags)
		__field(unsigned int, value)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->handle = handle;
		__entry->flags = flags;
		__entry->value = value;
	),
	TP_printk("sem=%#x(%s) flags=%#x(%s) value=%u",
		  __entry->handle,
		  __get_str(name),
		  __entry->flags,
		  cobalt_print_sem_flags(__entry->flags),
		  __entry->value)
);

TRACE_EVENT(cobalt_psem_init_failed,
	TP_PROTO(const char *name, int flags, unsigned int value, int status),
	TP_ARGS(name, flags, value, status),
	TP_STRUCT__entry(
		__string(name, name)
		__field(int, flags)
		__field(unsigned int, value)
		__field(int, status)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->flags = flags;
		__entry->value = value;
		__entry->status = status;
	),
	TP_printk("name=%s flags=%#x(%s) value=%u error=%d",
		  __get_str(name),
		  __entry->flags,
		  cobalt_print_sem_flags(__entry->flags),
		  __entry->value, __entry->status)
);

#define cobalt_print_oflags(__flags)		\
	__print_flags(__flags,  "|", 		\
		      {O_RDONLY, "rdonly"},	\
		      {O_WRONLY, "wronly"},	\
		      {O_RDWR, "rdwr"},		\
		      {O_CREAT, "creat"},	\
		      {O_EXCL, "excl"},		\
		      {O_DIRECT, "direct"},	\
		      {O_NONBLOCK, "nonblock"},	\
		      {O_TRUNC, "trunc"})

TRACE_EVENT(cobalt_psem_open,
	TP_PROTO(const char *name, xnhandle_t handle,
		 int oflags, mode_t mode, unsigned int value),
	TP_ARGS(name, handle, oflags, mode, value),
	TP_STRUCT__entry(
		__string(name, name)
		__field(xnhandle_t, handle)
		__field(int, oflags)
		__field(mode_t, mode)
		__field(unsigned int, value)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->handle = handle;
		__entry->oflags = oflags;
		if (oflags & O_CREAT) {
			__entry->mode = mode;
			__entry->value = value;
		} else {
			__entry->mode = 0;
			__entry->value = 0;
		}
	),
	TP_printk("named_sem=%#x=(%s) oflags=%#x(%s) mode=%o value=%u",
		  __entry->handle, __get_str(name),
		  __entry->oflags, cobalt_print_oflags(__entry->oflags),
		  __entry->mode, __entry->value)
);

TRACE_EVENT(cobalt_psem_open_failed,
	TP_PROTO(const char *name, int oflags, mode_t mode,
		 unsigned int value, int status),
	TP_ARGS(name, oflags, mode, value, status),
	TP_STRUCT__entry(
		__string(name, name)
		__field(int, oflags)
		__field(mode_t, mode)
		__field(unsigned int, value)
		__field(int, status)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->oflags = oflags;
		__entry->status = status;
		if (oflags & O_CREAT) {
			__entry->mode = mode;
			__entry->value = value;
		} else {
			__entry->mode = 0;
			__entry->value = 0;
		}
	),
	TP_printk("named_sem=%s oflags=%#x(%s) mode=%o value=%u error=%d",
		  __get_str(name),
		  __entry->oflags, cobalt_print_oflags(__entry->oflags),
		  __entry->mode, __entry->value, __entry->status)
);

DEFINE_EVENT(cobalt_posix_sem, cobalt_psem_close,
	TP_PROTO(xnhandle_t handle),
	TP_ARGS(handle)
);

TRACE_EVENT(cobalt_psem_unlink,
	TP_PROTO(const char *name),
	TP_ARGS(name),
	TP_STRUCT__entry(
		__string(name, name)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
	),
	TP_printk("name=%s", __get_str(name))
);

DECLARE_EVENT_CLASS(cobalt_clock_timespec,
	TP_PROTO(clockid_t clk_id, const struct timespec64 *val),
	TP_ARGS(clk_id, val),

	TP_STRUCT__entry(
		__field(clockid_t, clk_id)
		__timespec_fields(val)
	),

	TP_fast_assign(
		__entry->clk_id = clk_id;
		__assign_timespec(val, val);
	),

	TP_printk("clock_id=%d timeval=(%lld.%09ld)",
		  __entry->clk_id,
		  __timespec_args(val)
	)
);

DEFINE_EVENT(cobalt_clock_timespec, cobalt_clock_getres,
	TP_PROTO(clockid_t clk_id, const struct timespec64 *res),
	TP_ARGS(clk_id, res)
);

DEFINE_EVENT(cobalt_clock_timespec, cobalt_clock_gettime,
	TP_PROTO(clockid_t clk_id, const struct timespec64 *time),
	TP_ARGS(clk_id, time)
);

DEFINE_EVENT(cobalt_clock_timespec, cobalt_clock_settime,
	TP_PROTO(clockid_t clk_id, const struct timespec64 *time),
	TP_ARGS(clk_id, time)
);

TRACE_EVENT(cobalt_clock_adjtime,
	TP_PROTO(clockid_t clk_id, struct __kernel_timex *tx),
	TP_ARGS(clk_id, tx),

	TP_STRUCT__entry(
		__field(clockid_t, clk_id)
		__field(struct __kernel_timex *, tx)
	),

	TP_fast_assign(
		__entry->clk_id = clk_id;
		__entry->tx = tx;
	),

	TP_printk("clock_id=%d timex=%p",
		  __entry->clk_id,
		  __entry->tx
	)
);

#define cobalt_print_timer_flags(__flags)			\
	__print_flags(__flags, "|",				\
		      {TIMER_ABSTIME, "TIMER_ABSTIME"})

TRACE_EVENT(cobalt_clock_nanosleep,
	TP_PROTO(clockid_t clk_id, int flags, const struct timespec64 *time),
	TP_ARGS(clk_id, flags, time),

	TP_STRUCT__entry(
		__field(clockid_t, clk_id)
		__field(int, flags)
		__timespec_fields(time)
	),

	TP_fast_assign(
		__entry->clk_id = clk_id;
		__entry->flags = flags;
		__assign_timespec(time, time);
	),

	TP_printk("clock_id=%d flags=%#x(%s) rqt=(%lld.%09ld)",
		  __entry->clk_id,
		  __entry->flags, cobalt_print_timer_flags(__entry->flags),
		  __timespec_args(time)
	)
);

DECLARE_EVENT_CLASS(cobalt_clock_ident,
	TP_PROTO(const char *name, clockid_t clk_id),
	TP_ARGS(name, clk_id),
	TP_STRUCT__entry(
		__string(name, name)
		__field(clockid_t, clk_id)
	),
	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->clk_id = clk_id;
	),
	TP_printk("name=%s, id=%#x", __get_str(name), __entry->clk_id)
);

DEFINE_EVENT(cobalt_clock_ident, cobalt_clock_register,
	TP_PROTO(const char *name, clockid_t clk_id),
	TP_ARGS(name, clk_id)
);

DEFINE_EVENT(cobalt_clock_ident, cobalt_clock_deregister,
	TP_PROTO(const char *name, clockid_t clk_id),
	TP_ARGS(name, clk_id)
);

#define cobalt_print_clock(__clk_id)					\
	__print_symbolic(__clk_id,					\
			 {CLOCK_MONOTONIC, "CLOCK_MONOTONIC"},		\
			 {CLOCK_MONOTONIC_RAW, "CLOCK_MONOTONIC_RAW"},	\
			 {CLOCK_REALTIME, "CLOCK_REALTIME"})

TRACE_EVENT(cobalt_cond_init,
	TP_PROTO(const struct cobalt_cond_shadow __user *u_cnd,
		 const struct cobalt_condattr *attr),
	TP_ARGS(u_cnd, attr),
	TP_STRUCT__entry(
		__field(const struct cobalt_cond_shadow __user *, u_cnd)
		__field(clockid_t, clk_id)
		__field(int, pshared)
	),
	TP_fast_assign(
		__entry->u_cnd = u_cnd;
		__entry->clk_id = attr->clock;
		__entry->pshared = attr->pshared;
	),
	TP_printk("cond=%p attr={ .clock=%s, .pshared=%d }",
		  __entry->u_cnd,
		  cobalt_print_clock(__entry->clk_id),
		  __entry->pshared)
);

TRACE_EVENT(cobalt_cond_destroy,
	TP_PROTO(const struct cobalt_cond_shadow __user *u_cnd),
	TP_ARGS(u_cnd),
	TP_STRUCT__entry(
		__field(const struct cobalt_cond_shadow __user *, u_cnd)
	),
	TP_fast_assign(
		__entry->u_cnd = u_cnd;
	),
	TP_printk("cond=%p", __entry->u_cnd)
);

TRACE_EVENT(cobalt_cond_timedwait,
	TP_PROTO(const struct cobalt_cond_shadow __user *u_cnd,
		 const struct cobalt_mutex_shadow __user *u_mx,
		 const struct timespec64 *timeout),
	TP_ARGS(u_cnd, u_mx, timeout),
	TP_STRUCT__entry(
		__field(const struct cobalt_cond_shadow __user *, u_cnd)
		__field(const struct cobalt_mutex_shadow __user *, u_mx)
		__timespec_fields(timeout)
	),
	TP_fast_assign(
		__entry->u_cnd = u_cnd;
		__entry->u_mx = u_mx;
		__assign_timespec(timeout, timeout);
	),
	TP_printk("cond=%p, mutex=%p, timeout=(%lld.%09ld)",
		  __entry->u_cnd, __entry->u_mx, __timespec_args(timeout))
);

TRACE_EVENT(cobalt_cond_wait,
	TP_PROTO(const struct cobalt_cond_shadow __user *u_cnd,
		 const struct cobalt_mutex_shadow __user *u_mx),
	TP_ARGS(u_cnd, u_mx),
	TP_STRUCT__entry(
		__field(const struct cobalt_cond_shadow __user *, u_cnd)
		__field(const struct cobalt_mutex_shadow __user *, u_mx)
	),
	TP_fast_assign(
		__entry->u_cnd = u_cnd;
		__entry->u_mx = u_mx;
	),
	TP_printk("cond=%p, mutex=%p",
		  __entry->u_cnd, __entry->u_mx)
);

TRACE_EVENT(cobalt_mq_open,
	TP_PROTO(const char *name, int oflags, mode_t mode),
	TP_ARGS(name, oflags, mode),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, oflags)
		__field(mode_t, mode)
	),

	TP_fast_assign(
		__wrap_assign_str(name, name);
		__entry->oflags = oflags;
		__entry->mode = (oflags & O_CREAT) ? mode : 0;
	),

	TP_printk("name=%s oflags=%#x(%s) mode=%o",
		  __get_str(name),
		  __entry->oflags, cobalt_print_oflags(__entry->oflags),
		  __entry->mode)
);

TRACE_EVENT(cobalt_mq_notify,
	TP_PROTO(mqd_t mqd, const struct sigevent *sev),
	TP_ARGS(mqd, sev),

	TP_STRUCT__entry(
		__field(mqd_t, mqd)
		__field(int, signo)
	),

	TP_fast_assign(
		__entry->mqd = mqd;
		__entry->signo = sev && sev->sigev_notify != SIGEV_NONE ?
			sev->sigev_signo : 0;
	),

	TP_printk("mqd=%d signo=%d",
		  __entry->mqd, __entry->signo)
);

TRACE_EVENT(cobalt_mq_close,
	TP_PROTO(mqd_t mqd),
	TP_ARGS(mqd),

	TP_STRUCT__entry(
		__field(mqd_t, mqd)
	),

	TP_fast_assign(
		__entry->mqd = mqd;
	),

	TP_printk("mqd=%d", __entry->mqd)
);

TRACE_EVENT(cobalt_mq_unlink,
	TP_PROTO(const char *name),
	TP_ARGS(name),

	TP_STRUCT__entry(
		__string(name, name)
	),

	TP_fast_assign(
		__wrap_assign_str(name, name);
	),

	TP_printk("name=%s", __get_str(name))
);

TRACE_EVENT(cobalt_mq_send,
	TP_PROTO(mqd_t mqd, const void __user *u_buf, size_t len,
		 unsigned int prio),
	TP_ARGS(mqd, u_buf, len, prio),
	TP_STRUCT__entry(
		__field(mqd_t, mqd)
		__field(const void __user *, u_buf)
		__field(size_t, len)
		__field(unsigned int, prio)
	),
	TP_fast_assign(
		__entry->mqd = mqd;
		__entry->u_buf = u_buf;
		__entry->len = len;
		__entry->prio = prio;
	),
	TP_printk("mqd=%d buf=%p len=%zu prio=%u",
		  __entry->mqd, __entry->u_buf, __entry->len,
		  __entry->prio)
);

TRACE_EVENT(cobalt_mq_timedreceive,
	TP_PROTO(mqd_t mqd, const void __user *u_buf, size_t len,
		 const struct timespec64 *timeout),
	TP_ARGS(mqd, u_buf, len, timeout),
	TP_STRUCT__entry(
		__field(mqd_t, mqd)
		__field(const void __user *, u_buf)
		__field(size_t, len)
		__timespec_fields(timeout)
	),
	TP_fast_assign(
		__entry->mqd = mqd;
		__entry->u_buf = u_buf;
		__entry->len = len;
		__assign_timespec(timeout, timeout);
	),
	TP_printk("mqd=%d buf=%p len=%zu timeout=(%lld.%09ld)",
		  __entry->mqd, __entry->u_buf, __entry->len,
		  __timespec_args(timeout))
);

TRACE_EVENT(cobalt_mq_receive,
	TP_PROTO(mqd_t mqd, const void __user *u_buf, size_t len),
	TP_ARGS(mqd, u_buf, len),
	TP_STRUCT__entry(
		__field(mqd_t, mqd)
		__field(const void __user *, u_buf)
		__field(size_t, len)
	),
	TP_fast_assign(
		__entry->mqd = mqd;
		__entry->u_buf = u_buf;
		__entry->len = len;
	),
	TP_printk("mqd=%d buf=%p len=%zu",
		  __entry->mqd, __entry->u_buf, __entry->len)
);

DECLARE_EVENT_CLASS(cobalt_posix_mqattr,
	TP_PROTO(mqd_t mqd, const struct mq_attr *attr),
	TP_ARGS(mqd, attr),
	TP_STRUCT__entry(
		__field(mqd_t, mqd)
		__field(long, flags)
		__field(long, curmsgs)
		__field(long, msgsize)
		__field(long, maxmsg)
	),
	TP_fast_assign(
		__entry->mqd = mqd;
		__entry->flags = attr->mq_flags;
		__entry->curmsgs = attr->mq_curmsgs;
		__entry->msgsize = attr->mq_msgsize;
		__entry->maxmsg = attr->mq_maxmsg;
	),
	TP_printk("mqd=%d flags=%#lx(%s) curmsgs=%ld msgsize=%ld maxmsg=%ld",
		  __entry->mqd,
		  __entry->flags, cobalt_print_oflags(__entry->flags),
		  __entry->curmsgs,
		  __entry->msgsize,
		  __entry->maxmsg
	)
);

DEFINE_EVENT(cobalt_posix_mqattr, cobalt_mq_getattr,
	TP_PROTO(mqd_t mqd, const struct mq_attr *attr),
	TP_ARGS(mqd, attr)
);

DEFINE_EVENT(cobalt_posix_mqattr, cobalt_mq_setattr,
	TP_PROTO(mqd_t mqd, const struct mq_attr *attr),
	TP_ARGS(mqd, attr)
);

#define cobalt_print_evflags(__flags)			\
	__print_flags(__flags,  "|",			\
		      {COBALT_EVENT_SHARED, "shared"},	\
		      {COBALT_EVENT_PRIO, "prio"})

TRACE_EVENT(cobalt_event_init,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event,
		 unsigned long value, int flags),
	TP_ARGS(u_event, value, flags),
	TP_STRUCT__entry(
		__field(const struct cobalt_event_shadow __user *, u_event)
		__field(unsigned long, value)
		__field(int, flags)
	),
	TP_fast_assign(
		__entry->u_event = u_event;
		__entry->value = value;
		__entry->flags = flags;
	),
	TP_printk("event=%p value=%lu flags=%#x(%s)",
		  __entry->u_event, __entry->value,
		  __entry->flags, cobalt_print_evflags(__entry->flags))
);

#define cobalt_print_evmode(__mode)			\
	__print_symbolic(__mode,			\
			 {COBALT_EVENT_ANY, "any"},	\
			 {COBALT_EVENT_ALL, "all"})

TRACE_EVENT(cobalt_event_timedwait,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event,
		 unsigned long bits, int mode,
		 const struct timespec64 *timeout),
	TP_ARGS(u_event, bits, mode, timeout),
	TP_STRUCT__entry(
		__field(const struct cobalt_event_shadow __user *, u_event)
		__field(unsigned long, bits)
		__field(int, mode)
		__timespec_fields(timeout)
	),
	TP_fast_assign(
		__entry->u_event = u_event;
		__entry->bits = bits;
		__entry->mode = mode;
		__assign_timespec(timeout, timeout);
	),
	TP_printk("event=%p bits=%#lx mode=%#x(%s) timeout=(%lld.%09ld)",
		  __entry->u_event, __entry->bits, __entry->mode,
		  cobalt_print_evmode(__entry->mode),
		  __timespec_args(timeout))
);

TRACE_EVENT(cobalt_event_wait,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event,
		 unsigned long bits, int mode),
	TP_ARGS(u_event, bits, mode),
	TP_STRUCT__entry(
		__field(const struct cobalt_event_shadow __user *, u_event)
		__field(unsigned long, bits)
		__field(int, mode)
	),
	TP_fast_assign(
		__entry->u_event = u_event;
		__entry->bits = bits;
		__entry->mode = mode;
	),
	TP_printk("event=%p bits=%#lx mode=%#x(%s)",
		  __entry->u_event, __entry->bits, __entry->mode,
		  cobalt_print_evmode(__entry->mode))
);

DECLARE_EVENT_CLASS(cobalt_event_ident,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event),
	TP_ARGS(u_event),
	TP_STRUCT__entry(
		__field(const struct cobalt_event_shadow __user *, u_event)
	),
	TP_fast_assign(
		__entry->u_event = u_event;
	),
	TP_printk("event=%p", __entry->u_event)
);

DEFINE_EVENT(cobalt_event_ident, cobalt_event_destroy,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event),
	TP_ARGS(u_event)
);

DEFINE_EVENT(cobalt_event_ident, cobalt_event_sync,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event),
	TP_ARGS(u_event)
);

DEFINE_EVENT(cobalt_event_ident, cobalt_event_inquire,
	TP_PROTO(const struct cobalt_event_shadow __user *u_event),
	TP_ARGS(u_event)
);

#endif /* _TRACE_COBALT_POSIX_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cobalt-posix
#include <trace/define_trace.h>
