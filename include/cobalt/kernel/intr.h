/*
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * \ingroup intr
 */

#ifndef _COBALT_KERNEL_INTR_H
#define _COBALT_KERNEL_INTR_H

/* Possible return values of ISR. */
#define XN_ISR_NONE	 0x1
#define XN_ISR_HANDLED	 0x2
/* Additional bits. */
#define XN_ISR_PROPAGATE 0x100
#define XN_ISR_NOENABLE  0x200
#define XN_ISR_BITMASK	 (~0xff)

/* Creation flags. */
#define XN_ISR_SHARED	 0x1
#define XN_ISR_EDGE	 0x2

/* Operational flags. */
#define XN_ISR_ATTACHED	 0x10000

#ifdef __KERNEL__

#include <cobalt/kernel/types.h>
#include <cobalt/kernel/stat.h>

struct xnsched;

typedef struct xnintr {

#ifdef CONFIG_XENO_OPT_SHIRQ
    struct xnintr *next; /* !< Next object in the IRQ-sharing chain. */
#endif /* CONFIG_XENO_OPT_SHIRQ */

    unsigned unhandled;	/* !< Number of consequent unhandled interrupts */

    xnisr_t isr;	/* !< Interrupt service routine. */

    void *cookie;	/* !< User-defined cookie value. */

    int flags;		/* !< Creation flags. */

    unsigned irq;	/* !< IRQ number. */

    xniack_t iack;	/* !< Interrupt acknowledge routine. */

    const char *name;	/* !< Symbolic name. */

    struct {
	xnstat_counter_t hits;	  /* !< Number of handled receipts since attachment. */
	xnstat_exectime_t account; /* !< Runtime accounting entity */
	xnstat_exectime_t sum; /* !< Accumulated accounting entity */
    } stat[NR_CPUS];

} xnintr_t;

typedef struct xnintr_iterator {

    int cpu;		/* !< Current CPU in iteration. */

    unsigned long hits;	/* !< Current hit counter. */

    xnticks_t exectime_period;	/* !< Used CPU time in current accounting period. */

    xnticks_t account_period; /* !< Length of accounting period. */

    xnticks_t exectime_total;	/* !< Overall CPU time consumed. */

    int list_rev;	/* !< System-wide xnintr list revision (internal use). */

    xnintr_t *prev;	/* !< Previously visited xnintr object (internal use). */

} xnintr_iterator_t;

extern xnintr_t nktimer;

int xnintr_mount(void);

void xnintr_clock_handler(void);

void xnintr_host_tick(struct xnsched *sched);

void xnintr_init_proc(void);

void xnintr_cleanup_proc(void);

    /* Public interface. */

int xnintr_init(xnintr_t *intr,
		const char *name,
		unsigned irq,
		xnisr_t isr,
		xniack_t iack,
		int flags);

int xnintr_destroy(xnintr_t *intr);

int xnintr_attach(xnintr_t *intr,
		  void *cookie);

int xnintr_detach(xnintr_t *intr);

void xnintr_enable(xnintr_t *intr);

void xnintr_disable(xnintr_t *intr);

void xnintr_affinity(xnintr_t *intr,
		     cpumask_t cpumask);

int xnintr_query_init(xnintr_iterator_t *iterator);

int xnintr_query_next(int irq, xnintr_iterator_t *iterator,
		      char *name_buf);

#endif /* __KERNEL__ */

#endif /* !_COBALT_KERNEL_INTR_H */