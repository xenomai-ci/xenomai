/*******************************************************************************


  Copyright(c) 1999 - 2006 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/


/* Linux PRO/1000 Ethernet Driver main header file */

#ifndef _E1000_H_
#define _E1000_H_

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/capability.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <net/pkt_sched.h>
#include <linux/list.h>
#include <linux/reboot.h>
#ifdef NETIF_F_ISO
#undef NETIF_F_ISO
#endif

#ifdef NETIF_F_TSO
#include <net/checksum.h>
#endif
#ifdef SIOCGMIIPHY
#include <linux/mii.h>
#endif
#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif

#ifdef NETIF_F_HW_VLAN_TX
#undef NETIF_F_HW_VLAN_TX
#endif

#ifdef NETIF_F_HW_VLAN_TX
#include <linux/if_vlan.h>
#endif

// RTNET
#include <rtnet_port.h>


#define BAR_0		0
#define BAR_1		1
#define BAR_5		5

#include "kcompat.h"
#define INTEL_E1000_ETHERNET_DEVICE(device_id) {\
	PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

struct e1000_adapter;

#include "e1000_hw.h"

#ifdef DBG
#define E1000_DBG(args...) pr_debug(args)
#else
#define E1000_DBG(args...)
#endif

#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DPRINTK(nlevel, klevel, fmt, args...)                           \
	(void)((NETIF_MSG_##nlevel & adapter->msg_enable) &&            \
	       printk(KERN_##klevel "%s: %s: %s: " fmt, KBUILD_MODNAME, \
		      adapter->netdev->name, __FUNCTION__, ##args))

#define E1000_ERR(args...) pr_err(args)

#define E1000_MAX_INTR 10

/* TX/RX descriptor defines */
#define E1000_DEFAULT_TXD                  256
#define E1000_MAX_TXD                      256
#define E1000_MIN_TXD                       80
#define E1000_MAX_82544_TXD               4096

#define E1000_DEFAULT_RXD                  256
#define E1000_MAX_RXD                      256
#define E1000_MIN_RXD                       80
#define E1000_MAX_82544_RXD               4096

/* Supported Rx Buffer Sizes */
#define E1000_RXBUFFER_128   128    /* Used for packet split */
#define E1000_RXBUFFER_256   256    /* Used for packet split */
#define E1000_RXBUFFER_512   512
#define E1000_RXBUFFER_1024  1024
#define E1000_RXBUFFER_2048  2048
#define E1000_RXBUFFER_4096  4096
#define E1000_RXBUFFER_8192  8192
#define E1000_RXBUFFER_16384 16384

/* SmartSpeed delimiters */
#define E1000_SMARTSPEED_DOWNSHIFT 3
#define E1000_SMARTSPEED_MAX       15

/* Packet Buffer allocations */
#define E1000_PBA_BYTES_SHIFT 0xA
#define E1000_TX_HEAD_ADDR_SHIFT 7
#define E1000_PBA_TX_MASK 0xFFFF0000

/* Flow Control Watermarks */
#define E1000_FC_HIGH_DIFF 0x1638  /* High: 5688 bytes below Rx FIFO size */
#define E1000_FC_LOW_DIFF 0x1640   /* Low:  5696 bytes below Rx FIFO size */

#define E1000_FC_PAUSE_TIME 0x0680 /* 858 usec */

/* How many Tx Descriptors do we need to call netif_wake_queue ? */
#define E1000_TX_QUEUE_WAKE	16
/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define E1000_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define AUTO_ALL_MODES            0
#define E1000_EEPROM_82544_APM    0x0004
#define E1000_EEPROM_ICH8_APME    0x0004
#define E1000_EEPROM_APME         0x0400

#ifndef E1000_MASTER_SLAVE
/* Switch to override PHY master/slave setting */
#define E1000_MASTER_SLAVE	e1000_ms_hw_default
#endif

#ifdef NETIF_F_HW_VLAN_TX
#define E1000_MNG_VLAN_NONE -1
#endif
/* Number of packet split data buffers (not including the header buffer) */
#define PS_PAGE_BUFFERS MAX_PS_BUFFERS-1

/* only works for sizes that are powers of 2 */
#define E1000_ROUNDUP(i, size) ((i) = (((i) + (size) - 1) & ~((size) - 1)))

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct e1000_buffer {
	struct rtskb *skb;
	dma_addr_t dma;
	unsigned long time_stamp;
	uint16_t length;
	uint16_t next_to_watch;
};


struct e1000_ps_page { struct page *ps_page[PS_PAGE_BUFFERS]; };
struct e1000_ps_page_dma { uint64_t ps_page_dma[PS_PAGE_BUFFERS]; };

struct e1000_tx_ring {
	/* pointer to the descriptor ring memory */
	void *desc;
	/* physical address of the descriptor ring */
	dma_addr_t dma;
	/* length of descriptor ring in bytes */
	unsigned int size;
	/* number of descriptors in the ring */
	unsigned int count;
	/* next descriptor to associate a buffer with */
	unsigned int next_to_use;
	/* next descriptor to check for DD status bit */
	unsigned int next_to_clean;
	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;

	rtdm_lock_t tx_lock;
	uint16_t tdh;
	uint16_t tdt;
	boolean_t last_tx_tso;
};

struct e1000_rx_ring {
	/* pointer to the descriptor ring memory */
	void *desc;
	/* physical address of the descriptor ring */
	dma_addr_t dma;
	/* length of descriptor ring in bytes */
	unsigned int size;
	/* number of descriptors in the ring */
	unsigned int count;
	/* next descriptor to associate a buffer with */
	unsigned int next_to_use;
	/* next descriptor to check for DD status bit */
	unsigned int next_to_clean;
	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;
	/* arrays of page information for packet split */
	struct e1000_ps_page *ps_page;
	struct e1000_ps_page_dma *ps_page_dma;

	/* cpu for rx queue */
	int cpu;

	uint16_t rdh;
	uint16_t rdt;
};

#define E1000_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define E1000_RX_DESC_PS(R, i)	    \
	(&(((union e1000_rx_desc_packet_split *)((R).desc))[i]))
#define E1000_RX_DESC_EXT(R, i)	    \
	(&(((union e1000_rx_desc_extended *)((R).desc))[i]))
#define E1000_GET_DESC(R, i, type)	(&(((struct type *)((R).desc))[i]))
#define E1000_RX_DESC(R, i)		E1000_GET_DESC(R, i, e1000_rx_desc)
#define E1000_TX_DESC(R, i)		E1000_GET_DESC(R, i, e1000_tx_desc)
#define E1000_CONTEXT_DESC(R, i)	E1000_GET_DESC(R, i, e1000_context_desc)

/* board specific private data structure */

struct e1000_adapter {
#ifdef NETIF_F_HW_VLAN_TX
	struct vlan_group *vlgrp;
	uint16_t mng_vlan_id;
#endif
	uint32_t bd_number;
	uint32_t rx_buffer_len;
	uint32_t part_num;
	uint32_t wol;
	uint32_t ksp3_port_a;
	uint32_t smartspeed;
	uint32_t en_mng_pt;
	uint16_t link_speed;
	uint16_t link_duplex;
#ifdef CONFIG_E1000_NAPI
	spinlock_t tx_queue_lock;
#endif
	atomic_t irq_sem;
	struct work_struct reset_task;
	uint8_t fc_autoneg;

#ifdef ETHTOOL_PHYS_ID
	struct timer_list blink_timer;
	unsigned long led_status;
#endif

	/* TX */
	struct e1000_tx_ring *tx_ring;      /* One per active queue */
	unsigned long tx_queue_len;
	uint32_t txd_cmd;
	uint32_t tx_int_delay;
	uint32_t tx_abs_int_delay;
	uint32_t gotcl;
	uint64_t gotcl_old;
	uint64_t tpt_old;
	uint64_t colc_old;
	uint32_t tx_timeout_count;
	uint32_t tx_fifo_head;
	uint32_t tx_head_addr;
	uint32_t tx_fifo_size;
	uint8_t  tx_timeout_factor;
	atomic_t tx_fifo_stall;
	boolean_t pcix_82544;
	boolean_t detect_tx_hung;

	/* RX */
#ifdef CONFIG_E1000_NAPI
	boolean_t (*clean_rx) (struct e1000_adapter *adapter,
			       struct e1000_rx_ring *rx_ring,
			       int *work_done, int work_to_do);
#else
	boolean_t (*clean_rx) (struct e1000_adapter *adapter,
			       struct e1000_rx_ring *rx_ring);
#endif
	void (*alloc_rx_buf) (struct e1000_adapter *adapter,
			      struct e1000_rx_ring *rx_ring,
				int cleaned_count);
	struct e1000_rx_ring *rx_ring;      /* One per active queue */
#ifdef CONFIG_E1000_NAPI
	struct net_device *polling_netdev;  /* One per active queue */
#endif
	int num_tx_queues;
	int num_rx_queues;

	uint64_t hw_csum_err;
	uint64_t hw_csum_good;
	uint64_t rx_hdr_split;
	uint32_t alloc_rx_buff_failed;
	uint32_t rx_int_delay;
	uint32_t rx_abs_int_delay;
	boolean_t rx_csum;
	unsigned int rx_ps_pages;
	uint32_t gorcl;
	uint64_t gorcl_old;
	uint16_t rx_ps_bsize0;

	/* Interrupt Throttle Rate */
	uint32_t itr;

	/* OS defined structs */
	struct rtnet_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;

	rtdm_irq_t irq_handle;
	boolean_t data_received;

	/* structs defined in e1000_hw.h */
	struct e1000_hw hw;
	struct e1000_hw_stats stats;
	struct e1000_phy_info phy_info;
	struct e1000_phy_stats phy_stats;

#ifdef ETHTOOL_TEST
	uint32_t test_icr;
	struct e1000_tx_ring test_tx_ring;
	struct e1000_rx_ring test_rx_ring;
#endif

#ifdef E1000_COUNT_ICR
	uint64_t icr_txdw;
	uint64_t icr_txqe;
	uint64_t icr_lsc;
	uint64_t icr_rxseq;
	uint64_t icr_rxdmt;
	uint64_t icr_rxo;
	uint64_t icr_rxt;
	uint64_t icr_mdac;
	uint64_t icr_rxcfg;
	uint64_t icr_gpi;
#endif

	uint32_t *config_space;
	int msg_enable;
#ifdef CONFIG_PCI_MSI
	boolean_t have_msi;
#endif
	/* to not mess up cache alignment, always add to the bottom */
#ifdef NETIF_F_TSO
	boolean_t tso_force;
#endif
	boolean_t smart_power_down;	/* phy smart power down */
	unsigned long flags;

	struct delayed_work watchdog_task;
	struct delayed_work fifo_stall_task;
	struct delayed_work phy_info_task;
};

enum e1000_state_t {
	__E1000_DRIVER_TESTING,
	__E1000_RESETTING,
};

void e1000_check_options(struct e1000_adapter *adapter);
#endif /* _E1000_H_ */
