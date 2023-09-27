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

#include <linux/xenomai/wrappers.h>

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)
#define raw_copy_to_user(__to, __from, __n)	__copy_to_user_inatomic(__to, __from, __n)
#define raw_copy_from_user(__to, __from, __n)	__copy_from_user_inatomic(__to, __from, __n)
#define raw_put_user(__from, __to)		__put_user_inatomic(__from, __to)
#define raw_get_user(__to, __from)		__get_user_inatomic(__to, __from)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0)
#define in_ia32_syscall() (current_thread_info()->status & TS_COMPAT)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,5,0)
#define cobalt_gpiochip_dev(__gc)	((__gc)->dev)
#else
#define cobalt_gpiochip_dev(__gc)	((__gc)->parent)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)
#define cobalt_get_restart_block(p)	(&task_thread_info(p)->restart_block)
#else
#define cobalt_get_restart_block(p)	(&(p)->restart_block)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)
#define user_msghdr msghdr
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
#include <linux/netdevice.h>

#undef alloc_netdev
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, setup, 1, 1)
 
#include <linux/trace_seq.h>

static inline unsigned char *
trace_seq_buffer_ptr(struct trace_seq *s)
{
	return s->buffer + s->len;
}

#endif /* < 3.17 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
#define smp_mb__before_atomic()  smp_mb()
#define smp_mb__after_atomic()   smp_mb()
#endif /* < 3.16 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
#define raw_cpu_ptr(v)	__this_cpu_ptr(v)
#endif /* < 3.15 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
#include <linux/pci.h>

#ifdef CONFIG_PCI
#define pci_enable_msix_range COBALT_BACKPORT(pci_enable_msix_range)
#ifdef CONFIG_PCI_MSI
int pci_enable_msix_range(struct pci_dev *dev,
			  struct msix_entry *entries,
			  int minvec, int maxvec);
#else /* !CONFIG_PCI_MSI */
static inline
int pci_enable_msix_range(struct pci_dev *dev,
			  struct msix_entry *entries,
			  int minvec, int maxvec)
{
	return -ENOSYS;
}
#endif /* !CONFIG_PCI_MSI */
#endif /* CONFIG_PCI */
#endif /* < 3.14 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#include <linux/dma-mapping.h>
#include <linux/hwmon.h>

#define dma_set_mask_and_coherent COBALT_BACKPORT(dma_set_mask_and_coherent)
static inline
int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}

#ifdef CONFIG_HWMON
#define hwmon_device_register_with_groups \
	COBALT_BACKPORT(hwmon_device_register_with_groups)
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				void *drvdata,
				const struct attribute_group **groups);

#define devm_hwmon_device_register_with_groups \
	COBALT_BACKPORT(devm_hwmon_device_register_with_groups)
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				void *drvdata,
				const struct attribute_group **groups);
#endif /* !CONFIG_HWMON */

#define reinit_completion(__x)	INIT_COMPLETION(*(__x))

#endif /* < 3.13 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
#define DEVICE_ATTR_RW(_name)	__ATTR_RW(_name)
#define DEVICE_ATTR_RO(_name)	__ATTR_RO(_name)
#define DEVICE_ATTR_WO(_name)	__ATTR_WO(_name)
#endif /* < 3.11 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
#error "Xenomai/cobalt requires Linux kernel 3.10 or above"
#endif /* < 3.10 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
#define __kernel_timex		timex
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,1,0)
#define old_timex32		compat_timex
#define SO_RCVTIMEO_OLD		SO_RCVTIMEO
#define SO_SNDTIMEO_OLD		SO_SNDTIMEO
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,2,0)
#define mmiowb()		do { } while (0)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#define __kernel_old_timeval	timeval
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,208) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0) && \
     LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0))
#define mmap_read_lock(__mm)	down_read(&mm->mmap_sem)
#define mmap_read_unlock(__mm)	up_read(&mm->mmap_sem)
#define mmap_write_lock(__mm)	down_write(&mm->mmap_sem)
#define mmap_write_unlock(__mm)	up_write(&mm->mmap_sem)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
#define DEFINE_PROC_OPS(__name, __open, __release, __read, __write) \
	struct file_operations __name = {			    \
		.open = (__open),				    \
		.release = (__release),				    \
		.read = (__read),				    \
		.write = (__write),				    \
		.llseek = seq_lseek,				    \
}
#else
#define DEFINE_PROC_OPS(__name, __open, __release, __read, __write)	\
	struct proc_ops __name = {					\
		.proc_open = (__open),					\
		.proc_release = (__release),				\
		.proc_read = (__read),					\
		.proc_write = (__write),				\
		.proc_lseek = seq_lseek,				\
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0)
#define vmalloc_kernel(__size, __flags)	__vmalloc(__size, GFP_KERNEL|__flags, PAGE_KERNEL)
#else
#define vmalloc_kernel(__size, __flags)	__vmalloc(__size, GFP_KERNEL|__flags)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,7,0)
#define pci_aer_clear_nonfatal_status	pci_cleanup_aer_uncorrect_error_status
#define old_timespec32    compat_timespec
#define old_itimerspec32  compat_itimerspec
#define old_timeval32     compat_timeval
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,8,0)
#define vmalloc_kernel(__size, __flags)	__vmalloc(__size, GFP_KERNEL|__flags, PAGE_KERNEL)
#else
#define vmalloc_kernel(__size, __flags)	__vmalloc(__size, GFP_KERNEL|__flags)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,9,0)
#define read_file_from_kernel(__file, __buf, __buf_size, __file_size, __id) \
	({								\
		loff_t ___file_size;					\
		int __ret;						\
		__ret = kernel_read_file(__file, __buf, &___file_size,	\
				__buf_size, __id);			\
		(*__file_size) = ___file_size;				\
		__ret;							\
	})
#else
#define read_file_from_kernel(__file, __buf, __buf_size, __file_size, __id) \
	kernel_read_file(__file, 0, __buf, __buf_size, __file_size, __id)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
#if __has_attribute(__fallthrough__)
# define fallthrough			__attribute__((__fallthrough__))
#else
# define fallthrough			do {} while (0)  /* fallthrough */
#endif
#endif

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
     LINUX_VERSION_CODE < KERNEL_VERSION(5,10,188)) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(5,5,0) || \
     LINUX_VERSION_CODE < KERNEL_VERSION(5,4,251)) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(4,20,0) || \
     LINUX_VERSION_CODE < KERNEL_VERSION(4,19,255) || \
     (LINUX_VERSION_CODE == KERNEL_VERSION(4,19,255) && \
      CONFIG_KVER_SUBLEVEL < 291))
#define dev_addr_set(dev, addr)		memcpy((dev)->dev_addr, addr, MAX_ADDR_LEN)
#define eth_hw_addr_set(dev, addr)	memcpy((dev)->dev_addr, addr, ETH_ALEN)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,17,0)
#define pde_data(i)	PDE_DATA(i)
#endif

#endif /* _COBALT_ASM_GENERIC_WRAPPERS_H */
