#ifndef __RTCAN_ETHTOOL_H_
#define __RTCAN_ETHTOOL_H_

#ifdef __KERNEL__

#include <linux/ethtool.h>

struct rtcan_device;

struct rtcan_ethtool_ops {
	int (*begin)(struct rtcan_device *dev);
	int (*complete)(struct rtcan_device *dev);
	void (*get_drvinfo)(struct rtcan_device *dev,
			     struct ethtool_drvinfo *info);
	void (*get_ringparam)(struct rtcan_device *dev,
			      struct ethtool_ringparam *ring);
};

int rtcan_ethtool(struct rtdm_fd *fd, struct rtcan_device *dev,
		  struct ifreq *ifr);

#endif /* __KERNEL__ */

#endif /* __RTCAN_ETHTOOL_H_ */
