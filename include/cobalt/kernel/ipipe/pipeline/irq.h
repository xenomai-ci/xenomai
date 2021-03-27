/*
 * SPDX-License-Identifier: GPL-2.0
 */

#ifndef _COBALT_KERNEL_IPIPE_IRQ_H
#define _COBALT_KERNEL_IPIPE_IRQ_H

void xnintr_init_proc(void);

void xnintr_cleanup_proc(void);

int xnintr_mount(void);

#endif /* !_COBALT_KERNEL_IPIPE_IRQ_H */
