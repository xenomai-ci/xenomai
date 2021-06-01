// SPDX-License-Identifier: GPL-2.0

#include <asm-generic/xenomai/syscall.h>
#include <cobalt/kernel/time.h>
#include <linux/compat.h>

int cobalt_get_timespec64(struct timespec64 *ts,
			  const struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts;
	int ret;

	ret = cobalt_copy_from_user(&kts, uts, sizeof(kts));
	if (ret)
		return -EFAULT;

	ts->tv_sec = kts.tv_sec;

	/* Zero out the padding in compat mode */
	if (in_compat_syscall())
		kts.tv_nsec &= 0xFFFFFFFFUL;

	/* In 32-bit mode, this drops the padding */
	ts->tv_nsec = kts.tv_nsec;

	return 0;
}

int cobalt_put_timespec64(const struct timespec64 *ts,
		   struct __kernel_timespec __user *uts)
{
	struct __kernel_timespec kts = {
		.tv_sec = ts->tv_sec,
		.tv_nsec = ts->tv_nsec
	};

	return cobalt_copy_to_user(uts, &kts, sizeof(kts)) ? -EFAULT : 0;
}
