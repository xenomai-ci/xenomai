// SPDX-License-Identifier: GPL-2.0
#ifndef __RTNET_CHECKSUM_H_
#define __RTNET_CHECKSUM_H_

#include <linux/string.h>
#include <net/checksum.h>

#define rtnet_csum(__buf, __len, __csum)				\
	({								\
		csum_partial(__buf, __len, (__force __wsum)__csum);	\
	})

#define rtnet_csum_copy(__src, __dst, __len, __csum)			\
	({								\
		memcpy(__dst, __src, __len);				\
		csum_partial(__dst, __len, (__force __wsum)__csum);	\
	})

#endif /* !__RTNET_CHECKSUM_H_ */
