// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021, Dario Binacchi <dariobin@libero.it>
 */

#include <linux/platform_device.h>

/* CAN device profile */
#include <rtdm/can.h>

#include "rtcan_dev.h"
#include "rtcan_raw.h"
#include "rtcan_internal.h"
#include "rtcan_ethtool.h"
#include "rtcan_c_can.h"

static void rtcan_c_can_get_drvinfo(struct rtcan_device *dev,
				    struct ethtool_drvinfo *info)
{
	struct c_can_priv *priv = rtcan_priv(dev);
	struct platform_device *pdev = to_platform_device(priv->device);

	strscpy(info->driver, "c_can", sizeof(info->driver));
	strscpy(info->version, "1.0", sizeof(info->version));
	strscpy(info->bus_info, pdev->name, sizeof(info->bus_info));
}

static void rtcan_c_can_get_ringparam(struct rtcan_device *dev,
				      struct ethtool_ringparam *ring)
{
	struct c_can_priv *priv = rtcan_priv(dev);

	ring->rx_max_pending = priv->msg_obj_num;
	ring->tx_max_pending = priv->msg_obj_num;
	ring->rx_pending = priv->msg_obj_rx_num;
	ring->tx_pending = priv->msg_obj_tx_num;
}

static const struct rtcan_ethtool_ops rtcan_c_can_ethtool_ops = {
	.get_drvinfo = rtcan_c_can_get_drvinfo,
	.get_ringparam = rtcan_c_can_get_ringparam,
};

void rtcan_c_can_set_ethtool_ops(struct rtcan_device *dev)
{
	dev->ethtool_ops = &rtcan_c_can_ethtool_ops;
}
