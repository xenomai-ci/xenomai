// SPDX-License-Identifier: GPL-2.0-only
/*
 * CANFD firmware interface.
 *
 * Copyright (C) 2001-2021 PEAK System-Technik GmbH
 * Copyright (C) 2019-2021 Stephane Grosjean <s.grosjean@peak-system.com>
 */
#include "rtcan_dev.h"
#include "rtcan_raw.h"
#include "rtcan_peak_canfd_user.h"

#define DRV_NAME		"xeno_peak_canfd"

#define RTCAN_DEV_NAME		"rtcan%d"
#define RTCAN_CTRLR_NAME	"peak_canfd"

/* bittiming ranges of the PEAK-System PC CAN-FD interfaces */
static const struct can_bittiming_const peak_canfd_nominal_const = {
	.name = RTCAN_CTRLR_NAME,
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

/* initialize the command area */
static struct peak_canfd_priv *pucan_init_cmd(struct peak_canfd_priv *priv)
{
	priv->cmd_len = 0;
	return priv;
}

/* add command 'cmd_op' to the command area */
static void *pucan_add_cmd(struct peak_canfd_priv *priv, int cmd_op)
{
	struct pucan_command *cmd;

	if (priv->cmd_len + sizeof(*cmd) > priv->cmd_maxlen)
		return NULL;

	cmd = priv->cmd_buffer + priv->cmd_len;

	/* reset all unused bit to default */
	memset(cmd, 0, sizeof(*cmd));

	cmd->opcode_channel = pucan_cmd_opcode_channel(priv->index, cmd_op);
	priv->cmd_len += sizeof(*cmd);

	return cmd;
}

/* send the command(s) to the IP core through the host-device interface */
static int pucan_write_cmd(struct peak_canfd_priv *priv)
{
	int err;

	/* prepare environment before writing the command */
	if (priv->pre_cmd) {
		err = priv->pre_cmd(priv);
		if (err)
			return err;
	}

	err = priv->write_cmd(priv);
	if (err)
		return err;

	/* update environment after writing the command */
	if (priv->post_cmd)
		err = priv->post_cmd(priv);

	return err;
}

/* set the device in RESET mode */
static int pucan_set_reset_mode(struct peak_canfd_priv *priv)
{
	int err;

	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_RESET_MODE);
	err = pucan_write_cmd(priv);
	if (!err)
		priv->rdev->state = CAN_STATE_STOPPED;

	return err;
}

/* set the device in NORMAL mode */
static int pucan_set_normal_mode(struct peak_canfd_priv *priv)
{
	int err;

	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_NORMAL_MODE);
	err = pucan_write_cmd(priv);
	if (!err)
		priv->rdev->state = CAN_STATE_ERROR_ACTIVE;

	return err;
}

/* set the device in LISTEN_ONLY mode */
static int pucan_set_listen_only_mode(struct peak_canfd_priv *priv)
{
	int err;

	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_LISTEN_ONLY_MODE);
	err = pucan_write_cmd(priv);
	if (!err)
		priv->rdev->state = CAN_STATE_ERROR_ACTIVE;

	return err;
}

/* set acceptance filters */
static int pucan_set_std_filter(struct peak_canfd_priv *priv, u8 row, u32 mask)
{
	struct pucan_std_filter *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_SET_STD_FILTER);

	/* All the 11-bit CAN ID values are represented by one bit in a
	 * 64 rows array of 32 columns: the upper 6 bit of the CAN ID select
	 * the row while the lowest 5 bit select the column in that row.
	 *
	 * bit  filter
	 * 1    passed
	 * 0    discarded
	 */

	/* select the row */
	cmd->idx = row;

	/* set/unset bits in the row */
	cmd->mask = cpu_to_le32(mask);

	return pucan_write_cmd(priv);
}

/* request the device to stop transmission */
static int pucan_tx_abort(struct peak_canfd_priv *priv, u16 flags)
{
	struct pucan_tx_abort *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_TX_ABORT);

	cmd->flags = cpu_to_le16(flags);

	return pucan_write_cmd(priv);
}

/* request the device to clear rx/tx error counters */
static int pucan_clr_err_counters(struct peak_canfd_priv *priv)
{
	struct pucan_wr_err_cnt *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_WR_ERR_CNT);

	cmd->sel_mask = cpu_to_le16(PUCAN_WRERRCNT_TE | PUCAN_WRERRCNT_RE);

	/* write the counters new value */
	cmd->tx_counter = 0;
	cmd->rx_counter = 0;

	return pucan_write_cmd(priv);
}

/* set options to the device */
static int pucan_set_options(struct peak_canfd_priv *priv, u16 opt_mask)
{
	struct pucan_options *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_SET_EN_OPTION);

	cmd->options = cpu_to_le16(opt_mask);

	return pucan_write_cmd(priv);
}

/* request the device to notify the driver when Tx path is ready */
static int pucan_setup_rx_barrier(struct peak_canfd_priv *priv)
{
	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_RX_BARRIER);

	return pucan_write_cmd(priv);
}

/* handle the reception of one CAN frame */
static int pucan_handle_can_rx(struct peak_canfd_priv *priv,
			       struct pucan_rx_msg *msg)
{
	struct rtcan_skb skb = { .rb_frame_size = EMPTY_RB_FRAME_SIZE, };
	struct rtcan_rb_frame *cf = &skb.rb_frame;
	struct rtcan_device *rdev = priv->rdev;
	const u16 rx_msg_flags = le16_to_cpu(msg->flags);

	if (rx_msg_flags & PUCAN_MSG_EXT_DATA_LEN) {
		/* CAN-FD frames are silently discarded */
		return 0;
	}

	cf->can_id = le32_to_cpu(msg->can_id);
	cf->can_dlc = get_can_dlc(pucan_msg_get_dlc(msg));

	if (rx_msg_flags & PUCAN_MSG_EXT_ID)
		cf->can_id |= CAN_EFF_FLAG;

	if (rx_msg_flags & PUCAN_MSG_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	else {
		memcpy(cf->data, msg->d, cf->can_dlc);
		skb.rb_frame_size += cf->can_dlc;
	}

	cf->can_ifindex = rdev->ifindex;

	/* Pass received frame out to the sockets */
	rtcan_rcv(rdev, &skb);

	return 0;
}

/* handle rx/tx error counters notification */
static int pucan_handle_error(struct peak_canfd_priv *priv,
			      struct pucan_error_msg *msg)
{
	priv->bec.txerr = msg->tx_err_cnt;
	priv->bec.rxerr = msg->rx_err_cnt;

	return 0;
}

/* handle status notification */
static int pucan_handle_status(struct peak_canfd_priv *priv,
			       struct pucan_status_msg *msg)
{
	struct rtcan_skb skb = { .rb_frame_size = EMPTY_RB_FRAME_SIZE, };
	struct rtcan_rb_frame *cf = &skb.rb_frame;
	struct rtcan_device *rdev = priv->rdev;

	/* this STATUS is the CNF of the RX_BARRIER: Tx path can be setup */
	if (pucan_status_is_rx_barrier(msg)) {
		if (priv->enable_tx_path) {
			int err = priv->enable_tx_path(priv);

			if (err)
				return err;
		}

		/* unlock senders */
		rtdm_sem_up(&rdev->tx_sem);
		return 0;
	}

	/* otherwise, it's a BUS status */
	cf->can_id = CAN_ERR_FLAG;
	cf->can_dlc = CAN_ERR_DLC;

	/* test state error bits according to their priority */
	if (pucan_status_is_busoff(msg)) {
		rtdm_printk(DRV_NAME " CAN%u: Bus-off entry status\n",
			    priv->index+1);
		rdev->state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;

		/* wakeup waiting senders */
		rtdm_sem_destroy(&rdev->tx_sem);

	} else if (pucan_status_is_passive(msg)) {
		rtdm_printk(DRV_NAME " CAN%u: Error passive status\n",
			    priv->index+1);
		rdev->state = CAN_STATE_ERROR_PASSIVE;
		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = (priv->bec.txerr > priv->bec.rxerr) ?
					CAN_ERR_CRTL_TX_PASSIVE :
					CAN_ERR_CRTL_RX_PASSIVE;
		cf->data[6] = priv->bec.txerr;
		cf->data[7] = priv->bec.rxerr;

	} else if (pucan_status_is_warning(msg)) {
		rtdm_printk(DRV_NAME " CAN%u: Error warning status\n",
			    priv->index+1);
		rdev->state = CAN_STATE_ERROR_WARNING;

		cf->can_id |= CAN_ERR_CRTL;
		cf->data[1] = (priv->bec.txerr > priv->bec.rxerr) ?
					CAN_ERR_CRTL_TX_WARNING :
					CAN_ERR_CRTL_RX_WARNING;
		cf->data[6] = priv->bec.txerr;
		cf->data[7] = priv->bec.rxerr;

	} else if (rdev->state != CAN_STATE_ERROR_ACTIVE) {
		/* back to ERROR_ACTIVE */
		rtdm_printk(DRV_NAME " CAN%u: Error active status\n",
			    priv->index+1);
		rdev->state = CAN_STATE_ERROR_ACTIVE;
	}

	skb.rb_frame_size += cf->can_dlc;
	cf->can_ifindex = rdev->ifindex;

	/* Pass received frame out to the sockets */
	rtcan_rcv(rdev, &skb);

	return 0;
}

/* handle IP core Rx overflow notification */
static int pucan_handle_cache_critical(struct peak_canfd_priv *priv)
{
	struct rtcan_skb skb = { .rb_frame_size = EMPTY_RB_FRAME_SIZE, };
	struct rtcan_rb_frame *cf = &skb.rb_frame;
	struct rtcan_device *rdev = priv->rdev;

	cf->can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
	cf->can_dlc = CAN_ERR_DLC;

	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	cf->data[6] = priv->bec.txerr;
	cf->data[7] = priv->bec.rxerr;

	skb.rb_frame_size += cf->can_dlc;
	cf->can_ifindex = rdev->ifindex;

	/* Pass received frame out to the sockets */
	rtcan_rcv(rdev, &skb);

	return 0;
}

/* handle a single uCAN message */
int peak_canfd_handle_msg(struct peak_canfd_priv *priv,
			  struct pucan_rx_msg *msg)
{
	u16 msg_type = le16_to_cpu(msg->type);
	int msg_size = le16_to_cpu(msg->size);
	int err;

	if (!msg_size || !msg_type) {
		/* null packet found: end of list */
		goto exit;
	}

	switch (msg_type) {
	case PUCAN_MSG_CAN_RX:
		err = pucan_handle_can_rx(priv, (struct pucan_rx_msg *)msg);
		break;
	case PUCAN_MSG_ERROR:
		err = pucan_handle_error(priv, (struct pucan_error_msg *)msg);
		break;
	case PUCAN_MSG_STATUS:
		err = pucan_handle_status(priv,
					  (struct pucan_status_msg *)msg);
		break;
	case PUCAN_MSG_CACHE_CRITICAL:
		err = pucan_handle_cache_critical(priv);
		break;
	default:
		err = 0;
	}

	if (err < 0)
		return err;

exit:
	return msg_size;
}

/* handle a list of rx_count messages from rx_msg memory address */
int peak_canfd_handle_msgs_list(struct peak_canfd_priv *priv,
				struct pucan_rx_msg *msg_list, int msg_count)
{
	void *msg_ptr = msg_list;
	int i, msg_size = 0;

	for (i = 0; i < msg_count; i++) {
		msg_size = peak_canfd_handle_msg(priv, msg_ptr);

		/* a null packet can be found at the end of a list */
		if (msg_size <= 0)
			break;

		msg_ptr += ALIGN(msg_size, 4);
	}

	if (msg_size < 0)
		return msg_size;

	return i;
}

/* start the device (set the IP core in NORMAL or LISTEN-ONLY mode) */
static int peak_canfd_start(struct rtcan_device *rdev,
			    rtdm_lockctx_t *lock_ctx)
{
	struct peak_canfd_priv *priv = rdev->priv;
	int i, err = 0;

	switch (rdev->state) {
	case CAN_STATE_BUS_OFF:
	case CAN_STATE_STOPPED:
		err = pucan_set_reset_mode(priv);
		if (err)
			break;

		/* set ineeded option: get rx/tx error counters */
		err = pucan_set_options(priv, PUCAN_OPTION_ERROR);
		if (err)
			break;

		/* accept all standard CAN ID */
		for (i = 0; i <= PUCAN_FLTSTD_ROW_IDX_MAX; i++)
			pucan_set_std_filter(priv, i, 0xffffffff);

		/* clear device rx/tx error counters */
		err = pucan_clr_err_counters(priv);
		if (err)
			break;

		/* set resquested mode */
		if (priv->rdev->ctrl_mode & CAN_CTRLMODE_LISTENONLY)
			err = pucan_set_listen_only_mode(priv);
		else
			err = pucan_set_normal_mode(priv);

		rtdm_sem_init(&rdev->tx_sem, 1);

		/* receiving the RB status says when Tx path is ready */
		err = pucan_setup_rx_barrier(priv);
		break;

	default:
		break;
	}

	return err;
}

/* stop the device (set the IP core in RESET mode) */
static int peak_canfd_stop(struct rtcan_device *rdev,
			   rtdm_lockctx_t *lock_ctx)
{
	struct peak_canfd_priv *priv = rdev->priv;
	int err = 0;

	switch (rdev->state) {
	case CAN_STATE_BUS_OFF:
	case CAN_STATE_STOPPED:
		break;

	default:
		/* go back to RESET mode */
		err = pucan_set_reset_mode(priv);
		if (err) {
			rtdm_printk(DRV_NAME " CAN%u: reset failed\n",
				    priv->index+1);
			break;
		}

		/* abort last Tx (MUST be done in RESET mode only!) */
		pucan_tx_abort(priv, PUCAN_TX_ABORT_FLUSH);

		rtdm_sem_destroy(&rdev->tx_sem);
		break;
	}

	return err;
}

/* RT-Socket-CAN driver interface */
static int peak_canfd_set_mode(struct rtcan_device *rdev, can_mode_t mode,
			       rtdm_lockctx_t *lock_ctx)
{
	int err = 0;

	switch (mode) {
	case CAN_MODE_STOP:
		err = peak_canfd_stop(rdev, lock_ctx);
		break;
	case CAN_MODE_START:
		err = peak_canfd_start(rdev, lock_ctx);
		break;
	case CAN_MODE_SLEEP:
		/* Controller must operate, otherwise go out */
		if (!CAN_STATE_OPERATING(rdev->state)) {
			err = -ENETDOWN;
			break;
		}
		if (rdev->state == CAN_STATE_SLEEPING)
			break;

		/* fallthrough */
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int peak_canfd_set_bittiming(struct rtcan_device *rdev,
				    struct can_bittime *pbt,
				    rtdm_lockctx_t *lock_ctx)
{
	struct peak_canfd_priv *priv = rdev->priv;
	struct pucan_timing_slow *cmd;

	/* can't support BTR0BTR1 mode with clock greater than 8 MHz */
	if (pbt->type != CAN_BITTIME_STD) {
		rtdm_printk(DRV_NAME
			    " CAN%u: unsupported bittiming mode %u\n",
			    priv->index+1, pbt->type);
		return -EINVAL;
	}

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_TIMING_SLOW);

	cmd->sjw_t = PUCAN_TSLOW_SJW_T(pbt->std.sjw - 1,
				       priv->rdev->ctrl_mode &
						CAN_CTRLMODE_3_SAMPLES);

	cmd->tseg1 = PUCAN_TSLOW_TSEG1(pbt->std.prop_seg +
				       pbt->std.phase_seg1 - 1);
	cmd->tseg2 = PUCAN_TSLOW_TSEG2(pbt->std.phase_seg2 - 1);
	cmd->brp = cpu_to_le16(PUCAN_TSLOW_BRP(pbt->std.brp - 1));

	cmd->ewl = 96;	/* default */

	rtdm_printk(DRV_NAME ": nominal: brp=%u tseg1=%u tseg2=%u sjw=%u\n",
		   le16_to_cpu(cmd->brp), cmd->tseg1, cmd->tseg2, cmd->sjw_t);

	return pucan_write_cmd(priv);
}

/* hard transmit callback: write the CAN frame to the device */
static netdev_tx_t peak_canfd_start_xmit(struct rtcan_device *rdev,
					 can_frame_t *cf)
{
	struct peak_canfd_priv *priv = rdev->priv;
	struct pucan_tx_msg *msg;
	u16 msg_size, msg_flags;
	int room_left;
	const u8 dlc = (cf->can_dlc > CAN_MAX_DLC) ? CAN_MAX_DLC : cf->can_dlc;

	msg_size = ALIGN(sizeof(*msg) + dlc, 4);
	msg = priv->alloc_tx_msg(priv, msg_size, &room_left);

	/* should never happen except under bus-off condition and
	 * (auto-)restart mechanism
	 */
	if (!msg) {
		rtdm_printk(DRV_NAME
			    " CAN%u: skb lost (No room left in tx buffer)\n",
			    priv->index+1);
		return 0;
	}

	msg->size = cpu_to_le16(msg_size);
	msg->type = cpu_to_le16(PUCAN_MSG_CAN_TX);
	msg_flags = 0;
	if (cf->can_id & CAN_EFF_FLAG) {
		msg_flags |= PUCAN_MSG_EXT_ID;
		msg->can_id = cpu_to_le32(cf->can_id & CAN_EFF_MASK);
	} else {
		msg->can_id = cpu_to_le32(cf->can_id & CAN_SFF_MASK);
	}

	if (cf->can_id & CAN_RTR_FLAG)
		msg_flags |= PUCAN_MSG_RTR;

	/* set driver specific bit to differentiate with application
	 * loopback
	 */
	if (rdev->ctrl_mode & CAN_CTRLMODE_LOOPBACK)
		msg_flags |= PUCAN_MSG_LOOPED_BACK;

	msg->flags = cpu_to_le16(msg_flags);
	msg->channel_dlc = PUCAN_MSG_CHANNEL_DLC(priv->index, dlc);
	memcpy(msg->d, cf->data, dlc);

	/* write the skb on the interface */
	priv->write_tx_msg(priv, msg);

	/* control senders flow */
	if (room_left > (sizeof(*msg) + CAN_MAX_DLC))
		rtdm_sem_up(&rdev->tx_sem);

	return 0;
}

/* allocate a rtcan device for channel #index, with enough space to store
 * private information.
 */
struct rtcan_device *alloc_peak_canfd_dev(int sizeof_priv, int index)
{
	struct rtcan_device *rdev;
	struct peak_canfd_priv *priv;

	/* allocate the candev object */
	rdev = rtcan_dev_alloc(sizeof_priv, 0);
	if (!rdev)
		return NULL;

	/* RTCAN part initialization */
	strncpy(rdev->name, RTCAN_DEV_NAME, IFNAMSIZ);
	rdev->ctrl_name = RTCAN_CTRLR_NAME;
	rdev->can_sys_clock = 80*1000*1000;	/* default */
	rdev->state = CAN_STATE_STOPPED;
	rdev->hard_start_xmit = peak_canfd_start_xmit;
	rdev->do_set_mode = peak_canfd_set_mode;
	rdev->do_set_bit_time = peak_canfd_set_bittiming;
	rdev->bittiming_const = &peak_canfd_nominal_const;

	priv = rdev->priv;

	/* private part initialization */
	priv->rdev = rdev;
	priv->index = index;
	priv->cmd_len = 0;
	priv->bec.txerr = 0;
	priv->bec.rxerr = 0;

	return rdev;
}
