/*
 * Copyright (C) 2005-2012 Philippe Gerum <rpm@xenomai.org>.
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
#ifndef _COBALT_ASM_GENERIC_WRAPPERS_H

#include <linux/version.h>

#define COBALT_BACKPORT(__sym) __cobalt_backport_ ##__sym

/*
 * To keep the #ifdefery as readable as possible, please:
 *
 * - keep the conditional structure flat, no nesting (e.g. do not fold
 *   the pre-3.11 conditions into the pre-3.14 ones).
 * - group all wrappers for a single kernel revision.
 * - list conditional blocks in order of kernel release, latest first
 * - identify the first kernel release for which the wrapper should
 *   be defined, instead of testing the existence of a preprocessor
 *   symbol, so that obsolete wrappers can be spotted.
 */

#define DEFINE_PROC_OPS(__name, __open, __release, __read, __write)	\
	struct proc_ops __name = {					\
		.proc_open = (__open),					\
		.proc_release = (__release),				\
		.proc_read = (__read),					\
		.proc_write = (__write),				\
		.proc_lseek = seq_lseek,				\
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,11,0)
#define IRQ_WORK_INIT(_func) (struct irq_work) {	\
	.flags = ATOMIC_INIT(0),			\
	.func = (_func),				\
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,14,0)
#define close_fd(__ufd)	__close_fd(current->files, __ufd)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,15,0) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(5,11,0) || \
     LINUX_VERSION_CODE < KERNEL_VERSION(5,10,188))
#define dev_addr_set(dev, addr)		memcpy((dev)->dev_addr, addr, MAX_ADDR_LEN)
#define eth_hw_addr_set(dev, addr)	memcpy((dev)->dev_addr, addr, ETH_ALEN)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,17,0)
#define pde_data(i)	PDE_DATA(i)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
#define spi_get_chipselect(spi, idx)	((spi)->chip_select)
#define spi_get_csgpiod(spi, idx)	((spi)->cs_gpiod)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0)
#define MAX_PAGE_ORDER	MAX_ORDER
#endif

#endif /* _COBALT_ASM_GENERIC_WRAPPERS_H */
