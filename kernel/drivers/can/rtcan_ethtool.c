#include "rtcan_dev.h"

static int rtcan_ethtool_drvinfo(struct rtdm_fd *fd, struct rtcan_device *dev,
				 void __user *useraddr)
{
	const struct rtcan_ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_drvinfo info = {.cmd = ETHTOOL_GDRVINFO };

	if (!ops->get_drvinfo)
		return -EOPNOTSUPP;

	ops->get_drvinfo(dev, &info);

	if (!rtdm_fd_is_user(fd) ||
	    rtdm_copy_to_user(fd, useraddr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int rtcan_ethtool_get_ringparam(struct rtdm_fd *fd,
				       struct rtcan_device *dev,
				       void __user *useraddr)
{
	const struct rtcan_ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_ringparam ringparam = { .cmd = ETHTOOL_GRINGPARAM };

	if (!ops->get_ringparam)
		return -EOPNOTSUPP;

	ops->get_ringparam(dev, &ringparam);

	if (!rtdm_fd_is_user(fd) ||
	    rtdm_copy_to_user(fd, useraddr, &ringparam, sizeof(ringparam)))
		return -EFAULT;

	return 0;
}

int rtcan_ethtool(struct rtdm_fd *fd, struct rtcan_device *dev,
		  struct ifreq *ifr)
{
	void __user *useraddr = ifr->ifr_data;
	u32 ethcmd;
	int rc;

	if (!dev->ethtool_ops)
		return -EOPNOTSUPP;

	if (!rtdm_read_user_ok(fd, useraddr, sizeof(ethcmd)) ||
	    rtdm_copy_from_user(fd, &ethcmd, useraddr, sizeof(ethcmd)))
		return -EFAULT;

	if (dev->ethtool_ops->begin) {
		rc = dev->ethtool_ops->begin(dev);
		if (rc < 0)
			return rc;
	}

	switch (ethcmd) {
	case ETHTOOL_GDRVINFO:
		rc = rtcan_ethtool_drvinfo(fd, dev, useraddr);
		break;
	case ETHTOOL_GRINGPARAM:
		rc = rtcan_ethtool_get_ringparam(fd, dev, useraddr);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	if (dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);

	return rc;
}
