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
#define TRACE_SYSTEM cobalt_core

#if !defined(_TRACE_COBALT_CORE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COBALT_CORE_H

#include <linux/tracepoint.h>
#include <linux/math64.h>
#include <cobalt/kernel/timer.h>
#include <cobalt/kernel/registry.h>
#include <cobalt/uapi/kernel/types.h>

struct xnsched;
struct xnthread;
struct xnsynch;
struct xnsched_class;
struct xnsched_quota_group;
struct xnthread_init_attr;

DECLARE_EVENT_CLASS(thread_event,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(unsigned long, state)
		__field(unsigned long, info)
	),

	TP_fast_assign(
		__entry->state = thread->state;
		__entry->info = thread->info;
		__entry->pid = xnthread_host_pid(thread);
	),

	TP_printk("pid=%d state=0x%lx info=0x%lx",
		  __entry->pid, __entry->state, __entry->info)
);

DECLARE_EVENT_CLASS(curr_thread_event,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread),

	TP_STRUCT__entry(
		__field(struct xnthread *, thread)
		__field(unsigned long, state)
		__field(unsigned long, info)
	),

	TP_fast_assign(
		__entry->state = thread->state;
		__entry->info = thread->info;
	),

	TP_printk("state=0x%lx info=0x%lx",
		  __entry->state, __entry->info)
);

DECLARE_EVENT_CLASS(synch_wait_event,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch),

	TP_STRUCT__entry(
		__field(struct xnsynch *, synch)
	),

	TP_fast_assign(
		__entry->synch = synch;
	),

	TP_printk("synch=%p", __entry->synch)
);

DECLARE_EVENT_CLASS(synch_post_event,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch),

	TP_STRUCT__entry(
		__field(struct xnsynch *, synch)
	),

	TP_fast_assign(
		__entry->synch = synch;
	),

	TP_printk("synch=%p", __entry->synch)
);

DECLARE_EVENT_CLASS(irq_event,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq),

	TP_STRUCT__entry(
		__field(unsigned int, irq)
	),

	TP_fast_assign(
		__entry->irq = irq;
	),

	TP_printk("irq=%u", __entry->irq)
);

DECLARE_EVENT_CLASS(clock_event,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq),

	TP_STRUCT__entry(
		__field(unsigned int, irq)
	),

	TP_fast_assign(
		__entry->irq = irq;
	),

	TP_printk("clock_irq=%u", __entry->irq)
);

DECLARE_EVENT_CLASS(timer_event,
	TP_PROTO(struct xntimer *timer),
	TP_ARGS(timer),

	TP_STRUCT__entry(
		__field(struct xntimer *, timer)
	),

	TP_fast_assign(
		__entry->timer = timer;
	),

	TP_printk("timer=%p", __entry->timer)
);

DECLARE_EVENT_CLASS(registry_event,
	TP_PROTO(const char *key, void *addr),
	TP_ARGS(key, addr),

	TP_STRUCT__entry(
		__string(key, key ?: "(anon)")
		__field(void *, addr)
	),

	TP_fast_assign(
		__wrap_assign_str(key, key ?: "(anon)");
		__entry->addr = addr;
	),

	TP_printk("key=%s, addr=%p", __get_str(key), __entry->addr)
);

TRACE_EVENT(cobalt_schedule,
	TP_PROTO(struct xnsched *sched),
	TP_ARGS(sched),

	TP_STRUCT__entry(
		__field(unsigned long, status)
	),

	TP_fast_assign(
		__entry->status = sched->status;
	),

	TP_printk("status=0x%lx", __entry->status)
);

TRACE_EVENT(cobalt_schedule_remote,
	TP_PROTO(struct xnsched *sched),
	TP_ARGS(sched),

	TP_STRUCT__entry(
		__field(unsigned long, status)
	),

	TP_fast_assign(
		__entry->status = sched->status;
	),

	TP_printk("status=0x%lx", __entry->status)
);

TRACE_EVENT(cobalt_switch_context,
	TP_PROTO(struct xnthread *prev, struct xnthread *next),
	TP_ARGS(prev, next),

	TP_STRUCT__entry(
		__field(struct xnthread *, prev)
		__string(prev_name, prev->name)
		__field(pid_t, prev_pid)
		__field(int, prev_prio)
		__field(unsigned long, prev_state)
		__field(struct xnthread *, next)
		__string(next_name, next->name)
		__field(pid_t, next_pid)
		__field(int, next_prio)
	),

	TP_fast_assign(
		__entry->prev = prev;
		__wrap_assign_str(prev_name, prev->name);
		__entry->prev_pid = xnthread_host_pid(prev);
		__entry->prev_prio = xnthread_current_priority(prev);
		__entry->prev_state = prev->state;
		__entry->next = next;
		__wrap_assign_str(next_name, next->name);
		__entry->next_pid = xnthread_host_pid(next);
		__entry->next_prio = xnthread_current_priority(next);
	),

	TP_printk("prev_name=%s prev_pid=%d prev_prio=%d prev_state=0x%lx ==> next_name=%s next_pid=%d next_prio=%d",
		  __get_str(prev_name), __entry->prev_pid,
		  __entry->prev_prio, __entry->prev_state,
		  __get_str(next_name), __entry->next_pid, __entry->next_prio)
);

#ifdef CONFIG_XENO_OPT_SCHED_QUOTA

TRACE_EVENT(cobalt_schedquota_refill,
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

DECLARE_EVENT_CLASS(schedquota_group_event,
	TP_PROTO(struct xnsched_quota_group *tg),
	TP_ARGS(tg),

	TP_STRUCT__entry(
		__field(int, tgid)
	),

	TP_fast_assign(
		__entry->tgid = tg->tgid;
	),

	TP_printk("tgid=%d",
		  __entry->tgid)
);

DEFINE_EVENT(schedquota_group_event, cobalt_schedquota_create_group,
	TP_PROTO(struct xnsched_quota_group *tg),
	TP_ARGS(tg)
);

DEFINE_EVENT(schedquota_group_event, cobalt_schedquota_destroy_group,
	TP_PROTO(struct xnsched_quota_group *tg),
	TP_ARGS(tg)
);

TRACE_EVENT(cobalt_schedquota_set_limit,
	TP_PROTO(struct xnsched_quota_group *tg,
		 int percent,
		 int peak_percent),
	TP_ARGS(tg, percent, peak_percent),

	TP_STRUCT__entry(
		__field(int, tgid)
		__field(int, percent)
		__field(int, peak_percent)
	),

	TP_fast_assign(
		__entry->tgid = tg->tgid;
		__entry->percent = percent;
		__entry->peak_percent = peak_percent;
	),

	TP_printk("tgid=%d percent=%d peak_percent=%d",
		  __entry->tgid, __entry->percent, __entry->peak_percent)
);

DECLARE_EVENT_CLASS(schedquota_thread_event,
	TP_PROTO(struct xnsched_quota_group *tg,
		 struct xnthread *thread),
	TP_ARGS(tg, thread),

	TP_STRUCT__entry(
		__field(int, tgid)
		__field(struct xnthread *, thread)
		__field(pid_t, pid)
	),

	TP_fast_assign(
		__entry->tgid = tg->tgid;
		__entry->thread = thread;
		__entry->pid = xnthread_host_pid(thread);
	),

	TP_printk("tgid=%d thread=%p pid=%d",
		  __entry->tgid, __entry->thread, __entry->pid)
);

DEFINE_EVENT(schedquota_thread_event, cobalt_schedquota_add_thread,
	TP_PROTO(struct xnsched_quota_group *tg,
		 struct xnthread *thread),
	TP_ARGS(tg, thread)
);

DEFINE_EVENT(schedquota_thread_event, cobalt_schedquota_remove_thread,
	TP_PROTO(struct xnsched_quota_group *tg,
		 struct xnthread *thread),
	TP_ARGS(tg, thread)
);

#endif /* CONFIG_XENO_OPT_SCHED_QUOTA */

TRACE_EVENT(cobalt_thread_init,
	TP_PROTO(struct xnthread *thread,
		 const struct xnthread_init_attr *attr,
		 struct xnsched_class *sched_class),
	TP_ARGS(thread, attr, sched_class),

	TP_STRUCT__entry(
		__field(struct xnthread *, thread)
		__string(thread_name, thread->name)
		__string(class_name, sched_class->name)
		__field(unsigned long, flags)
		__field(int, cprio)
	),

	TP_fast_assign(
		__entry->thread = thread;
		__wrap_assign_str(thread_name, thread->name);
		__entry->flags = attr->flags;
		__wrap_assign_str(class_name, sched_class->name);
		__entry->cprio = thread->cprio;
	),

	TP_printk("thread=%p name=%s flags=0x%lx class=%s prio=%d",
		   __entry->thread, __get_str(thread_name), __entry->flags,
		   __get_str(class_name), __entry->cprio)
);

TRACE_EVENT(cobalt_thread_suspend,
	TP_PROTO(struct xnthread *thread, unsigned long mask, xnticks_t timeout,
		 xntmode_t timeout_mode, struct xnsynch *wchan),
	TP_ARGS(thread, mask, timeout, timeout_mode, wchan),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(unsigned long, mask)
		__field(xnticks_t, timeout)
		__field(xntmode_t, timeout_mode)
		__field(struct xnsynch *, wchan)
	),

	TP_fast_assign(
		__entry->pid = xnthread_host_pid(thread);
		__entry->mask = mask;
		__entry->timeout = timeout;
		__entry->timeout_mode = timeout_mode;
		__entry->wchan = wchan;
	),

	TP_printk("pid=%d mask=0x%lx timeout=%Lu timeout_mode=%d wchan=%p",
		  __entry->pid, __entry->mask,
		  __entry->timeout, __entry->timeout_mode, __entry->wchan)
);

TRACE_EVENT(cobalt_thread_resume,
	TP_PROTO(struct xnthread *thread, unsigned long mask),
	TP_ARGS(thread, mask),

	TP_STRUCT__entry(
		__string(name, thread->name)
		__field(pid_t, pid)
		__field(unsigned long, mask)
	),

	TP_fast_assign(
		__wrap_assign_str(name, thread->name);
		__entry->pid = xnthread_host_pid(thread);
		__entry->mask = mask;
	),

	TP_printk("name=%s pid=%d mask=0x%lx",
		  __get_str(name), __entry->pid, __entry->mask)
);

TRACE_EVENT(cobalt_thread_fault,
	TP_PROTO(unsigned long ip, unsigned int type),
	TP_ARGS(ip, type),

	TP_STRUCT__entry(
		__field(unsigned long, ip)
		__field(unsigned int, type)
	),

	TP_fast_assign(
		__entry->ip = ip;
		__entry->type = type;
	),

	TP_printk("ip=%#lx type=%#x",
		  __entry->ip, __entry->type)
);

TRACE_EVENT(cobalt_thread_set_current_prio,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread),

	TP_STRUCT__entry(
		__field(struct xnthread *, thread)
		__field(pid_t, pid)
		__field(int, cprio)
	),

	TP_fast_assign(
		__entry->thread = thread;
		__entry->pid = xnthread_host_pid(thread);
		__entry->cprio = xnthread_current_priority(thread);
	),

	TP_printk("thread=%p pid=%d prio=%d",
		  __entry->thread, __entry->pid, __entry->cprio)
);

DEFINE_EVENT(thread_event, cobalt_thread_start,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(thread_event, cobalt_thread_cancel,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(thread_event, cobalt_thread_join,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(thread_event, cobalt_thread_unblock,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_thread_wait_period,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_thread_missed_period,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_thread_set_mode,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

TRACE_EVENT(cobalt_thread_migrate,
	TP_PROTO(unsigned int cpu),
	TP_ARGS(cpu),

	TP_STRUCT__entry(
		__field(unsigned int, cpu)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
	),

	TP_printk("cpu=%u", __entry->cpu)
);

TRACE_EVENT(cobalt_thread_migrate_passive,
	TP_PROTO(struct xnthread *thread, unsigned int cpu),
	TP_ARGS(thread, cpu),

	TP_STRUCT__entry(
		__field(struct xnthread *, thread)
		__field(pid_t, pid)
		__field(unsigned int, cpu)
	),

	TP_fast_assign(
		__entry->thread = thread;
		__entry->pid = xnthread_host_pid(thread);
		__entry->cpu = cpu;
	),

	TP_printk("thread=%p pid=%d cpu=%u",
		  __entry->thread, __entry->pid, __entry->cpu)
);

DEFINE_EVENT(curr_thread_event, cobalt_shadow_gohard,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_watchdog_signal,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_shadow_hardened,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

#define cobalt_print_relax_reason(reason)				\
	__print_symbolic(reason,					\
			 { SIGDEBUG_UNDEFINED,		"undefined" },	\
			 { SIGDEBUG_MIGRATE_SIGNAL,	"signal" },	\
			 { SIGDEBUG_MIGRATE_SYSCALL,	"syscall" },	\
			 { SIGDEBUG_MIGRATE_FAULT,	"fault" })

TRACE_EVENT(cobalt_shadow_gorelax,
	TP_PROTO(int reason),
	TP_ARGS(reason),

	TP_STRUCT__entry(
		__field(int, reason)
	),

	TP_fast_assign(
		__entry->reason = reason;
	),

	TP_printk("reason=%s", cobalt_print_relax_reason(__entry->reason))
);

DEFINE_EVENT(curr_thread_event, cobalt_shadow_relaxed,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

DEFINE_EVENT(curr_thread_event, cobalt_shadow_entry,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

TRACE_EVENT(cobalt_shadow_map,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread),

	TP_STRUCT__entry(
		__field(struct xnthread *, thread)
		__field(pid_t, pid)
		__field(int, prio)
	),

	TP_fast_assign(
		__entry->thread = thread;
		__entry->pid = xnthread_host_pid(thread);
		__entry->prio = xnthread_base_priority(thread);
	),

	TP_printk("thread=%p pid=%d prio=%d",
		  __entry->thread, __entry->pid, __entry->prio)
);

DEFINE_EVENT(curr_thread_event, cobalt_shadow_unmap,
	TP_PROTO(struct xnthread *thread),
	TP_ARGS(thread)
);

TRACE_EVENT(cobalt_lostage_request,
        TP_PROTO(const char *type, struct task_struct *task),
	TP_ARGS(type, task),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__field(const char *, type)
	),

	TP_fast_assign(
		__entry->type = type;
		__entry->pid = task_pid_nr(task);
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
	),

	TP_printk("request=%s pid=%d comm=%s",
		  __entry->type, __entry->pid, __entry->comm)
);

TRACE_EVENT(cobalt_lostage_wakeup,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
	),

	TP_fast_assign(
		__entry->pid = task_pid_nr(task);
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
	),

	TP_printk("pid=%d comm=%s",
		  __entry->pid, __entry->comm)
);

TRACE_EVENT(cobalt_lostage_signal,
	TP_PROTO(struct task_struct *task, int sig),
	TP_ARGS(task, sig),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__field(int, sig)
	),

	TP_fast_assign(
		__entry->pid = task_pid_nr(task);
		__entry->sig = sig;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
	),

	TP_printk("pid=%d comm=%s sig=%d",
		  __entry->pid, __entry->comm, __entry->sig)
);

DEFINE_EVENT(irq_event, cobalt_irq_entry,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(irq_event, cobalt_irq_exit,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(irq_event, cobalt_irq_attach,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(irq_event, cobalt_irq_detach,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(irq_event, cobalt_irq_enable,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(irq_event, cobalt_irq_disable,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(clock_event, cobalt_clock_entry,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(clock_event, cobalt_clock_exit,
	TP_PROTO(unsigned int irq),
	TP_ARGS(irq)
);

DEFINE_EVENT(timer_event, cobalt_timer_stop,
	TP_PROTO(struct xntimer *timer),
	TP_ARGS(timer)
);

DEFINE_EVENT(timer_event, cobalt_timer_expire,
	TP_PROTO(struct xntimer *timer),
	TP_ARGS(timer)
);

#define cobalt_print_timer_mode(mode)			\
	__print_symbolic(mode,				\
			 { XN_RELATIVE, "rel" },	\
			 { XN_ABSOLUTE, "abs" },	\
			 { XN_REALTIME, "rt" })

TRACE_EVENT(cobalt_timer_start,
	TP_PROTO(struct xntimer *timer, xnticks_t value, xnticks_t interval,
		 xntmode_t mode),
	TP_ARGS(timer, value, interval, mode),

	TP_STRUCT__entry(
		__field(struct xntimer *, timer)
#ifdef CONFIG_XENO_OPT_STATS
		__string(name, timer->name)
#endif
		__field(xnticks_t, value)
		__field(xnticks_t, interval)
		__field(xntmode_t, mode)
	),

	TP_fast_assign(
		__entry->timer = timer;
#ifdef CONFIG_XENO_OPT_STATS
		__wrap_assign_str(name, timer->name);
#endif
		__entry->value = value;
		__entry->interval = interval;
		__entry->mode = mode;
	),

	TP_printk("timer=%p(%s) value=%Lu interval=%Lu mode=%s",
		  __entry->timer,
#ifdef CONFIG_XENO_OPT_STATS
		  __get_str(name),
#else
		  "(anon)",
#endif
		  __entry->value, __entry->interval,
		  cobalt_print_timer_mode(__entry->mode))
);

#ifdef CONFIG_SMP

TRACE_EVENT(cobalt_timer_migrate,
	TP_PROTO(struct xntimer *timer, unsigned int cpu),
	TP_ARGS(timer, cpu),

	TP_STRUCT__entry(
		__field(struct xntimer *, timer)
		__field(unsigned int, cpu)
	),

	TP_fast_assign(
		__entry->timer = timer;
		__entry->cpu = cpu;
	),

	TP_printk("timer=%p cpu=%u",
		  __entry->timer, __entry->cpu)
);

#endif /* CONFIG_SMP */

DEFINE_EVENT(synch_wait_event, cobalt_synch_sleepon,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_wait_event, cobalt_synch_try_acquire,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_wait_event, cobalt_synch_acquire,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_post_event, cobalt_synch_release,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_post_event, cobalt_synch_wakeup,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_post_event, cobalt_synch_wakeup_many,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_post_event, cobalt_synch_flush,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(synch_post_event, cobalt_synch_forget,
	TP_PROTO(struct xnsynch *synch),
	TP_ARGS(synch)
);

DEFINE_EVENT(registry_event, cobalt_registry_enter,
	TP_PROTO(const char *key, void *addr),
	TP_ARGS(key, addr)
);

DEFINE_EVENT(registry_event, cobalt_registry_remove,
	TP_PROTO(const char *key, void *addr),
	TP_ARGS(key, addr)
);

DEFINE_EVENT(registry_event, cobalt_registry_unlink,
	TP_PROTO(const char *key, void *addr),
	TP_ARGS(key, addr)
);

TRACE_EVENT(cobalt_tick_shot,
	TP_PROTO(s64 delta),
	TP_ARGS(delta),

	TP_STRUCT__entry(
		__field(u64, secs)
		__field(u32, nsecs)
		__field(s64, delta)
	),

	TP_fast_assign(
		__entry->delta = div_s64(delta, 1000);
		__entry->secs = div_u64_rem(trace_clock_local() + delta,
					    NSEC_PER_SEC, &__entry->nsecs);
	),

	TP_printk("next tick at %Lu.%06u (delay: %Ld us)",
		  (unsigned long long)__entry->secs,
		  __entry->nsecs / 1000, __entry->delta)
);

TRACE_EVENT(cobalt_trace,
	TP_PROTO(const char *msg),
	TP_ARGS(msg),
	TP_STRUCT__entry(
		__string(msg, msg)
	),
	TP_fast_assign(
		__wrap_assign_str(msg, msg);
	),
	TP_printk("%s", __get_str(msg))
);

TRACE_EVENT(cobalt_trace_longval,
	TP_PROTO(int id, u64 val),
	TP_ARGS(id, val),
	TP_STRUCT__entry(
		__field(int, id)
		__field(u64, val)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->val = val;
	),
	TP_printk("id=%#x, v=%llu", __entry->id, __entry->val)
);

TRACE_EVENT(cobalt_trace_pid,
	TP_PROTO(pid_t pid, int prio),
	TP_ARGS(pid, prio),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(int, prio)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->prio = prio;
	),
	TP_printk("pid=%d, prio=%d", __entry->pid, __entry->prio)
);

TRACE_EVENT(cobalt_latpeak,
	TP_PROTO(int latmax_ns),
	TP_ARGS(latmax_ns),
	TP_STRUCT__entry(
		 __field(int, latmax_ns)
	),
	TP_fast_assign(
		__entry->latmax_ns = latmax_ns;
	),
	TP_printk("** latency peak: %d.%.3d us **",
		  __entry->latmax_ns / 1000,
		  __entry->latmax_ns % 1000)
);

/* Basically cobalt_trace() + trigger point */
TRACE_EVENT(cobalt_trigger,
	TP_PROTO(const char *issuer),
	TP_ARGS(issuer),
	TP_STRUCT__entry(
		__string(issuer, issuer)
	),
	TP_fast_assign(
		__wrap_assign_str(issuer, issuer);
	),
	TP_printk("%s", __get_str(issuer))
);

#endif /* _TRACE_COBALT_CORE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE cobalt-core
#include <trace/define_trace.h>
