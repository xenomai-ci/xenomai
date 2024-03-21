// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021, Dario Binacchi <dariobin@libero.it>
 */

#include <asm/xenomai/syscall.h>
#include <xenomai/posix/corectl.h>
#include "corectl.h"

static int rtcan_corectl_call(struct notifier_block *self, unsigned long arg,
			      void *cookie)
{
	struct cobalt_config_vector *vec = cookie;
	int val, ret;

	if (arg != _CC_COBALT_GET_CAN_CONFIG)
		return NOTIFY_DONE;

	if (vec->u_bufsz < sizeof(ret))
		return notifier_from_errno(-EINVAL);

	val = _CC_COBALT_CAN;

	ret = cobalt_copy_to_user(vec->u_buf, &val, sizeof(val));

	return ret ? notifier_from_errno(-EFAULT) : NOTIFY_STOP;
}

static struct notifier_block rtcan_corectl_notifier = {
	.notifier_call = rtcan_corectl_call,
};

void rtcan_corectl_register(void)
{
	cobalt_add_config_chain(&rtcan_corectl_notifier);
}

void rtcan_corectl_unregister(void)
{
	cobalt_remove_config_chain(&rtcan_corectl_notifier);
}
