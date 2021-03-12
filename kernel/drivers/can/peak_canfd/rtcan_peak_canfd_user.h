/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CAN driver for PEAK System micro-CAN based adapters.
 *
 * Copyright (C) 2001-2021 PEAK System-Technik GmbH
 * Copyright (C) 2019-2021 Stephane Grosjean <s.grosjean@peak-system.com>
 */
#ifndef PEAK_CANFD_USER_H
#define PEAK_CANFD_USER_H

#include <linux/can/dev/peak_canfd.h>

#define CAN_MAX_DLC		8
#define get_can_dlc(i)		(min_t(__u8, (i), CAN_MAX_DLC))

struct peak_berr_counter {
	__u16 txerr;
	__u16 rxerr;
};

/* data structure private to each uCAN interface */
struct peak_canfd_priv {
	struct rtcan_device *rdev;	/* RTCAN device */
	int index;			/* channel index */

	struct peak_berr_counter bec;

	int cmd_len;
	void *cmd_buffer;
	int cmd_maxlen;

	int (*pre_cmd)(struct peak_canfd_priv *priv);
	int (*write_cmd)(struct peak_canfd_priv *priv);
	int (*post_cmd)(struct peak_canfd_priv *priv);

	int (*enable_tx_path)(struct peak_canfd_priv *priv);
	void *(*alloc_tx_msg)(struct peak_canfd_priv *priv, u16 msg_size,
			      int *room_left);
	int (*write_tx_msg)(struct peak_canfd_priv *priv,
			    struct pucan_tx_msg *msg);
};

struct rtcan_device *alloc_peak_canfd_dev(int sizeof_priv, int index);
void rtcan_peak_pciefd_remove_proc(struct rtcan_device *rdev);
int rtcan_peak_pciefd_create_proc(struct rtcan_device *rdev);

int peak_canfd_handle_msg(struct peak_canfd_priv *priv,
			  struct pucan_rx_msg *msg);
int peak_canfd_handle_msgs_list(struct peak_canfd_priv *priv,
				struct pucan_rx_msg *rx_msg, int rx_count);
#endif
