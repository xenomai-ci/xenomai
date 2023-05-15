/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _COBALT_KERNEL_TIME_H
#define _COBALT_KERNEL_TIME_H

#include <linux/time.h>
#include <linux/time64.h>

/**
 * Read struct __kernel_timespec from userspace and convert to
 * struct timespec64
 *
 * @param ts The destination, will be filled
 * @param uts The source, provided by an application
 * @return 0 on success, -EFAULT otherwise
 */
int cobalt_get_timespec64(struct timespec64 *ts,
			  const struct __kernel_timespec __user *uts);

/**
 * Covert struct timespec64 to struct __kernel_timespec
 * and copy to userspace
 *
 * @param ts The source, provided by kernel
 * @param uts The destination, will be filled
 * @return 0 on success, -EFAULT otherwise
 */
int cobalt_put_timespec64(const struct timespec64 *ts,
			  struct __kernel_timespec __user *uts);

/**
 * Read struct __kernel_itimerspec from userspace and convert to
 * struct itimerspec64
 *
 * @param dst The destination, will be filled
 * @param src The source, provided by an application
 * @return 0 on success, -EFAULT otherwise
 */
int cobalt_get_itimerspec64(struct itimerspec64 *dst,
			    const struct __kernel_itimerspec __user *src);

/**
 * Convert struct itimerspec64 to struct __kernel_itimerspec and copy to user
 * space
 * @param dst The destination, will be filled, provided by an application
 * @param src The source, provided by the kernel
 * @return 0 un success, -EFAULT otherwise
 */
int cobalt_put_itimerspec64(struct __kernel_itimerspec __user *dst,
			    const struct itimerspec64 *src);

#endif //_COBALT_KERNEL_TIME_H
