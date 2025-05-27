// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation
 * RTnet port 2022 Hongzhan Chen <hongzhan.chen@intel.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/if_vlan.h>
#include <linux/aer.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/pm_runtime.h>
#include <net/pkt_sched.h>

#include <net/ipv6.h>
#include <rtnet_port.h>

#include "igc.h"
#include "igc_hw.h"

// RTNET redefines
#ifdef NETIF_F_TSO
#undef NETIF_F_TSO
#define NETIF_F_TSO 0
#endif

#ifdef NETIF_F_TSO6
#undef NETIF_F_TSO6
#define NETIF_F_TSO6 0
#endif

#ifdef NETIF_F_HW_VLAN_TX
#undef NETIF_F_HW_VLAN_TX
#define NETIF_F_HW_VLAN_TX 0
#endif

#ifdef NETIF_F_HW_VLAN_RX
#undef NETIF_F_HW_VLAN_RX
#define NETIF_F_HW_VLAN_RX 0
#endif

#ifdef NETIF_F_HW_VLAN_FILTER
#undef NETIF_F_HW_VLAN_FILTER
#define NETIF_F_HW_VLAN_FILTER 0
#endif

#ifdef IGC_MAX_TX_QUEUES
#undef IGC_MAX_TX_QUEUES
#define IGC_MAX_TX_QUEUES 1
#endif

#ifdef IGC_MAX_RX_QUEUES
#undef IGC_MAX_RX_QUEUES
#define IGC_MAX_RX_QUEUES 1
#endif

#ifdef CONFIG_IGC_NAPI
#undef CONFIG_IGC_NAPI
#endif

#ifdef IGC_HAVE_TX_TIMEOUT
#undef IGC_HAVE_TX_TIMEOUT
#endif

#ifdef ETHTOOL_GPERMADDR
#undef ETHTOOL_GPERMADDR
#endif

#ifdef CONFIG_PM
#undef CONFIG_PM
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
#undef CONFIG_NET_POLL_CONTROLLER
#endif

#ifdef MAX_SKB_FRAGS
#undef MAX_SKB_FRAGS
#define MAX_SKB_FRAGS 1
#endif

#ifdef IGC_FRAMES_SUPPORT
#undef IGC_FRAMES_SUPPORT
#endif

#define DRV_SUMMARY	"Intel(R) 2.5G Ethernet Linux Driver"

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

static int debug = -1;

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL v2");
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

char igc_driver_name[] = "rt_igc";
static const char igc_driver_string[] = DRV_SUMMARY;
static const char igc_copyright[] =
	"Copyright(c) 2018 Intel Corporation.";

#define MAX_UNITS 8
static int InterruptThrottle;
module_param(InterruptThrottle, uint, 0);
MODULE_PARM_DESC(InterruptThrottle, "Throttle interrupts (boolean, false by default)");

static const struct igc_info *igc_info_tbl[] = {
	[board_base] = &igc_base_info,
};

static const struct pci_device_id igc_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_LM), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_V), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_I), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I220_V), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_K), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_K2), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_LMVP), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_IT), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I226_LM), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I226_V), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I226_IT), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I221_V), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I226_BLANK_NVM), board_base },
	{ PCI_VDEVICE(INTEL, IGC_DEV_ID_I225_BLANK_NVM), board_base },
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, igc_pci_tbl);

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

void igc_reset(struct igc_adapter *adapter)
{
	struct rtnet_device *dev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;
	struct igc_fc_info *fc = &hw->fc;
	u32 pba, hwm;

	/* Repartition PBA for greater than 9k MTU if required */
	pba = IGC_PBA_34K;

	/* flow control settings
	 * The high water mark must be low enough to fit one full frame
	 * after transmitting the pause frame.  As such we must have enough
	 * space to allow for us to complete our current transmit and then
	 * receive the frame that is in progress from the link partner.
	 * Set it to:
	 * - the full Rx FIFO size minus one full Tx plus one full Rx frame
	 */
	hwm = (pba << 10) - (adapter->max_frame_size + MAX_JUMBO_FRAME_SIZE);

	fc->high_water = hwm & 0xFFFFFFF0;	/* 16-byte granularity */
	fc->low_water = fc->high_water - 16;
	fc->pause_time = 0xFFFF;
	fc->send_xon = 1;
	fc->current_mode = fc->requested_mode;

	hw->mac.ops.reset_hw(hw);

	if (hw->mac.ops.init_hw(hw))
		rtdev_err(dev, "Error on hardware initialization\n");

	/* Re-establish EEE setting */
	igc_set_eee_i225(hw, true, true, true);

	if (!rtnetif_running(adapter->netdev))
		igc_power_down_phy_copper_base(&adapter->hw);

	igc_get_phy_info(hw);
}

/**
 * igc_power_up_link - Power up the phy link
 * @adapter: address of board private structure
 */
static void igc_power_up_link(struct igc_adapter *adapter)
{
	igc_reset_phy(&adapter->hw);

	igc_power_up_phy_copper(&adapter->hw);

	igc_setup_link(&adapter->hw);
}

/**
 * igc_release_hw_control - release control of the h/w to f/w
 * @adapter: address of board private structure
 *
 * igc_release_hw_control resets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that the
 * driver is no longer loaded.
 */
static void igc_release_hw_control(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware take over control of h/w */
	ctrl_ext = rd32(IGC_CTRL_EXT);
	wr32(IGC_CTRL_EXT,
	     ctrl_ext & ~IGC_CTRL_EXT_DRV_LOAD);
}

/**
 * igc_get_hw_control - get control of the h/w from f/w
 * @adapter: address of board private structure
 *
 * igc_get_hw_control sets CTRL_EXT:DRV_LOAD bit.
 * For ASF and Pass Through versions of f/w this means that
 * the driver is loaded.
 */
static void igc_get_hw_control(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 ctrl_ext;

	/* Let firmware know the driver has taken over */
	ctrl_ext = rd32(IGC_CTRL_EXT);
	wr32(IGC_CTRL_EXT,
	     ctrl_ext | IGC_CTRL_EXT_DRV_LOAD);
}

static void igc_unmap_and_free_tx_resource(struct igc_ring *ring,
					   struct igc_tx_buffer *tx_buffer)
{
	if (tx_buffer->skb) {
		kfree_rtskb(tx_buffer->skb);
		tx_buffer->skb = NULL;
	}
	tx_buffer->next_to_watch = NULL;
}
/**
 * igc_clean_tx_ring - Free Tx Buffers
 * @tx_ring: ring to be cleaned
 */
static void igc_clean_tx_ring(struct igc_ring *tx_ring)
{

	struct igc_tx_buffer *buffer_info;
	unsigned long size;
	u16 i;

	if (!tx_ring->tx_buffer_info)
		return;
	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->tx_buffer_info[i];
		igc_unmap_and_free_tx_resource(tx_ring, buffer_info);
	}

	size = sizeof(struct igc_tx_buffer) * tx_ring->count;
	memset(tx_ring->tx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(tx_ring->desc, 0, tx_ring->size);

	/* reset next_to_use and next_to_clean */
	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
}

/**
 * igc_free_tx_resources - Free Tx Resources per Queue
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 */
void igc_free_tx_resources(struct igc_ring *tx_ring)
{
	igc_clean_tx_ring(tx_ring);

	vfree(tx_ring->tx_buffer_info);
	tx_ring->tx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!tx_ring->desc)
		return;

	dma_free_coherent(tx_ring->dev, tx_ring->size,
			  tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * igc_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 */
static void igc_free_all_tx_resources(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igc_free_tx_resources(adapter->tx_ring[i]);
}

/**
 * igc_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 */
static void igc_clean_all_tx_rings(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		if (adapter->tx_ring[i])
			igc_clean_tx_ring(adapter->tx_ring[i]);
}

/**
 * igc_setup_tx_resources - allocate Tx resources (Descriptors)
 * @tx_ring: tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 */
int igc_setup_tx_resources(struct igc_ring *tx_ring)
{
	struct device *dev = tx_ring->dev;
	int size = 0;

	size = sizeof(struct igc_tx_buffer) * tx_ring->count;
	tx_ring->tx_buffer_info = vzalloc(size);
	if (!tx_ring->tx_buffer_info)
		goto err;

	/* round up to nearest 4K */
	tx_ring->size = tx_ring->count * sizeof(union igc_adv_tx_desc);
	tx_ring->size = ALIGN(tx_ring->size, 4096);

	tx_ring->desc = dma_alloc_coherent(dev, tx_ring->size,
					   &tx_ring->dma, GFP_KERNEL);

	if (!tx_ring->desc)
		goto err;

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;

	return 0;

err:
	vfree(tx_ring->tx_buffer_info);
	dev_err(dev, "Unable to allocate memory for Tx descriptor ring\n");
	return -ENOMEM;
}

/**
 * igc_setup_all_tx_resources - wrapper to allocate Tx resources for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static int igc_setup_all_tx_resources(struct igc_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = igc_setup_tx_resources(adapter->tx_ring[i]);
		if (err) {
			dev_err(&pdev->dev, "Error on Tx queue %u setup\n", i);
			for (i--; i >= 0; i--)
				igc_free_tx_resources(adapter->tx_ring[i]);
			break;
		}
	}

	return err;
}

static void igc_clean_rx_ring_page_shared(struct igc_ring *rx_ring)
{
	unsigned long size;
	u16 i;

	if (!rx_ring->rx_buffer_info)
		return;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		struct igc_rx_buffer *buffer_info = &rx_ring->rx_buffer_info[i];

		if (buffer_info->dma)
			buffer_info->dma = 0;

		if (buffer_info->skb) {
			kfree_rtskb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	size = sizeof(struct igc_rx_buffer) * rx_ring->count;
	memset(rx_ring->rx_buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);
}

/**
 * igc_clean_rx_ring - Free Rx Buffers per Queue
 * @ring: ring to free buffers from
 */
static void igc_clean_rx_ring(struct igc_ring *ring)
{
	igc_clean_rx_ring_page_shared(ring);

	//clear_ring_uses_large_buffer(ring);

	ring->next_to_alloc = 0;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;
}

/**
 * igc_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 */
static void igc_clean_all_rx_rings(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		if (adapter->rx_ring[i])
			igc_clean_rx_ring(adapter->rx_ring[i]);
}

/**
 * igc_free_rx_resources - Free Rx Resources
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 */
void igc_free_rx_resources(struct igc_ring *rx_ring)
{
	igc_clean_rx_ring(rx_ring);

	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;

	/* if not set, then don't free */
	if (!rx_ring->desc)
		return;

	dma_free_coherent(rx_ring->dev, rx_ring->size,
			  rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * igc_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 */
static void igc_free_all_rx_resources(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		igc_free_rx_resources(adapter->rx_ring[i]);
}

/**
 * igc_setup_rx_resources - allocate Rx resources (Descriptors)
 * @rx_ring:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 */
int igc_setup_rx_resources(struct igc_ring *rx_ring)
{
	struct device *dev = rx_ring->dev;
	int size, desc_len;

	size = sizeof(struct igc_rx_buffer) * rx_ring->count;
	rx_ring->rx_buffer_info = vzalloc(size);
	if (!rx_ring->rx_buffer_info)
		goto err;

	desc_len = sizeof(union igc_adv_rx_desc);

	/* Round up to nearest 4K */
	rx_ring->size = rx_ring->count * desc_len;
	rx_ring->size = ALIGN(rx_ring->size, 4096);

	rx_ring->desc = dma_alloc_coherent(dev, rx_ring->size,
					   &rx_ring->dma, GFP_KERNEL);

	if (!rx_ring->desc)
		goto err;

	rx_ring->next_to_alloc = 0;
	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	return 0;

err:
	vfree(rx_ring->rx_buffer_info);
	rx_ring->rx_buffer_info = NULL;
	dev_err(dev, "Unable to allocate memory for Rx descriptor ring\n");
	return -ENOMEM;
}

/**
 * igc_setup_all_rx_resources - wrapper to allocate Rx resources
 *                                (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 */
static int igc_setup_all_rx_resources(struct igc_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = igc_setup_rx_resources(adapter->rx_ring[i]);
		if (err) {
			dev_err(&pdev->dev,
					"Error on Rx queue %u setup\n", i);
			for (i--; i >= 0; i--)
				igc_free_rx_resources(adapter->rx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * igc_configure_rx_ring - Configure a receive ring after Reset
 * @adapter: board private structure
 * @ring: receive ring to be configured
 *
 * Configure the Rx unit of the MAC after a reset.
 */
static void igc_configure_rx_ring(struct igc_adapter *adapter,
				  struct igc_ring *ring)
{
	struct igc_hw *hw = &adapter->hw;
	int reg_idx = ring->reg_idx;
	u32 srrctl = 0, rxdctl = 0;
	u64 rdba = ring->dma;

	/* disable the queue */
	wr32(IGC_RXDCTL(reg_idx), 0);

	/* Set DMA base address registers */
	wr32(IGC_RDBAL(reg_idx),
	     rdba & 0x00000000ffffffffULL);
	wr32(IGC_RDBAH(reg_idx), rdba >> 32);
	wr32(IGC_RDLEN(reg_idx),
	     ring->count * sizeof(union igc_adv_rx_desc));

	/* initialize head and tail */
	ring->tail =  hw->hw_addr + IGC_RDT(reg_idx);
	wr32(IGC_RDH(reg_idx), 0);
	writel(0, ring->tail);

	/* reset next-to- use/clean to place SW in sync with hardware */
	ring->next_to_clean = 0;
	ring->next_to_use = 0;

	/* set descriptor configuration */
	srrctl = IGC_RX_HDR_LEN << IGC_SRRCTL_BSIZEHDRSIZE_SHIFT;
	srrctl |= IGC_RX_BUFSZ >> IGC_SRRCTL_BSIZEPKT_SHIFT;
	srrctl |= IGC_SRRCTL_DESCTYPE_ADV_ONEBUF;

	wr32(IGC_SRRCTL(reg_idx), srrctl);

	rxdctl |= IGC_RX_PTHRESH;
	rxdctl |= IGC_RX_HTHRESH << 8;
	rxdctl |= IGC_RX_WTHRESH << 16;

	/* enable receive descriptor fetching */
	rxdctl |= IGC_RXDCTL_QUEUE_ENABLE;

	wr32(IGC_RXDCTL(reg_idx), rxdctl);
}

/**
 * igc_configure_rx - Configure receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 */
static void igc_configure_rx(struct igc_adapter *adapter)
{
	int i;

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring
	 */
	for (i = 0; i < adapter->num_rx_queues; i++)
		igc_configure_rx_ring(adapter, adapter->rx_ring[i]);
}

/**
 * igc_configure_tx_ring - Configure transmit ring after Reset
 * @adapter: board private structure
 * @ring: tx ring to configure
 *
 * Configure a transmit ring after a reset.
 */
static void igc_configure_tx_ring(struct igc_adapter *adapter,
				  struct igc_ring *ring)
{
	struct igc_hw *hw = &adapter->hw;
	int reg_idx = ring->reg_idx;
	u64 tdba = ring->dma;
	u32 txdctl = 0;

	/* disable the queue */
	wr32(IGC_TXDCTL(reg_idx), 0);
	wrfl();
	mdelay(10);

	wr32(IGC_TDLEN(reg_idx),
	     ring->count * sizeof(union igc_adv_tx_desc));
	wr32(IGC_TDBAL(reg_idx),
	     tdba & 0x00000000ffffffffULL);
	wr32(IGC_TDBAH(reg_idx), tdba >> 32);

	ring->tail = hw->hw_addr + IGC_TDT(reg_idx);
	wr32(IGC_TDH(reg_idx), 0);
	writel(0, ring->tail);

	txdctl |= IGC_TX_PTHRESH;
	txdctl |= IGC_TX_HTHRESH << 8;
	txdctl |= IGC_TX_WTHRESH << 16;

	txdctl |= IGC_TXDCTL_QUEUE_ENABLE;
	wr32(IGC_TXDCTL(reg_idx), txdctl);
}

/**
 * igc_configure_tx - Configure transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 */
static void igc_configure_tx(struct igc_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		igc_configure_tx_ring(adapter, adapter->tx_ring[i]);
}

/**
 * igc_setup_mrqc - configure the multiple receive queue control registers
 * @adapter: Board private structure
 */
static void igc_setup_mrqc(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 j, num_rx_queues;
	u32 mrqc, rxcsum;
	u32 rss_key[10];

	get_random_bytes(rss_key, sizeof(rss_key));
	for (j = 0; j < 10; j++)
		wr32(IGC_RSSRK(j), rss_key[j]);

	num_rx_queues = adapter->rss_queues;

	if (adapter->rss_indir_tbl_init != num_rx_queues) {
		for (j = 0; j < IGC_RETA_SIZE; j++)
			adapter->rss_indir_tbl[j] =
			(j * num_rx_queues) / IGC_RETA_SIZE;
		adapter->rss_indir_tbl_init = num_rx_queues;
	}

	/* Disable raw packet checksumming so that RSS hash is placed in
	 * descriptor on writeback.  No need to enable TCP/UDP/IP checksum
	 * offloads as they are enabled by default
	 */
	rxcsum = rd32(IGC_RXCSUM);
	rxcsum |= IGC_RXCSUM_PCSD;

	/* Enable Receive Checksum Offload for SCTP */
	rxcsum |= IGC_RXCSUM_CRCOFL;

	/* Don't need to set TUOFL or IPOFL, they default to 1 */
	wr32(IGC_RXCSUM, rxcsum);

	/* Generate RSS hash based on packet types, TCP/UDP
	 * port numbers and/or IPv4/v6 src and dst addresses
	 */
	mrqc = IGC_MRQC_RSS_FIELD_IPV4 |
	       IGC_MRQC_RSS_FIELD_IPV4_TCP |
	       IGC_MRQC_RSS_FIELD_IPV6 |
	       IGC_MRQC_RSS_FIELD_IPV6_TCP |
	       IGC_MRQC_RSS_FIELD_IPV6_TCP_EX;

	if (adapter->flags & IGC_FLAG_RSS_FIELD_IPV4_UDP)
		mrqc |= IGC_MRQC_RSS_FIELD_IPV4_UDP;
	if (adapter->flags & IGC_FLAG_RSS_FIELD_IPV6_UDP)
		mrqc |= IGC_MRQC_RSS_FIELD_IPV6_UDP;

	mrqc |= IGC_MRQC_ENABLE_RSS_MQ;

	wr32(IGC_MRQC, mrqc);
}

/**
 * igc_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 */
static void igc_setup_rctl(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 rctl;

	rctl = rd32(IGC_RCTL);

	rctl &= ~(3 << IGC_RCTL_MO_SHIFT);
	rctl &= ~(IGC_RCTL_LBM_TCVR | IGC_RCTL_LBM_MAC);

	rctl |= IGC_RCTL_EN | IGC_RCTL_BAM | IGC_RCTL_RDMTS_HALF |
		(hw->mac.mc_filter_type << IGC_RCTL_MO_SHIFT);

	/* enable stripping of CRC. Newer features require
	 * that the HW strips the CRC.
	 */
	rctl |= IGC_RCTL_SECRC;

	/* disable store bad packets and clear size bits. */
	rctl &= ~(IGC_RCTL_SBP | IGC_RCTL_SZ_256);

	/* enable LPE to allow for reception of jumbo frames */
	rctl |= IGC_RCTL_LPE;

	/* disable queue 0 to prevent tail write w/o re-config */
	wr32(IGC_RXDCTL(0), 0);

	/* This is useful for sniffing bad packets. */
	if (adapter->netdev->features & NETIF_F_RXALL) {
		/* UPE and MPE will be handled by normal PROMISC logic
		 * in set_rx_mode
		 */
		rctl |= (IGC_RCTL_SBP | /* Receive bad packets */
			 IGC_RCTL_BAM | /* RX All Bcast Pkts */
			 IGC_RCTL_PMCF); /* RX All MAC Ctrl Pkts */

		rctl &= ~(IGC_RCTL_DPF | /* Allow filtered pause */
			  IGC_RCTL_CFIEN); /* Disable VLAN CFIEN Filter */
	}

	wr32(IGC_RCTL, rctl);
}

/**
 * igc_setup_tctl - configure the transmit control registers
 * @adapter: Board private structure
 */
static void igc_setup_tctl(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 tctl;

	/* disable queue 0 which icould be enabled by default */
	wr32(IGC_TXDCTL(0), 0);

	/* Program the Transmit Control Register */
	tctl = rd32(IGC_TCTL);
	tctl &= ~IGC_TCTL_CT;
	tctl |= IGC_TCTL_PSP | IGC_TCTL_RTLC |
		(IGC_COLLISION_THRESHOLD << IGC_CT_SHIFT);

	/* Enable transmits */
	tctl |= IGC_TCTL_EN;

	wr32(IGC_TCTL, tctl);
}


/**
 *  igc_write_mc_addr_list - write multicast addresses to MTA
 *  @netdev: network interface device structure
 *
 *  Writes multicast address list to the MTA hash table.
 *  Returns: -ENOMEM on failure
 *           0 on no addresses written
 *           X on writing X addresses to MTA
 **/
static int igc_write_mc_addr_list(struct rtnet_device *netdev)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;

	igc_update_mc_addr_list(hw, NULL, 0);

	return 0;
}

static int __igc_maybe_stop_tx(struct igc_ring *tx_ring, const u16 size)
{
	struct rtnet_device *netdev = tx_ring->netdev;

	rtnetif_stop_queue(netdev);

	/* memory barriier comment */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available.
	 */
	if (igc_desc_unused(tx_ring) < size)
		return -EBUSY;

	/* A reprieve! */
	rtnetif_wake_queue(netdev);

	tx_ring->tx_stats.restart_queue2++;

	return 0;
}

static inline int igc_maybe_stop_tx(struct igc_ring *tx_ring, const u16 size)
{
	if (igc_desc_unused(tx_ring) >= size)
		return 0;
	return __igc_maybe_stop_tx(tx_ring, size);
}

#define IGC_SET_FLAG(_input, _flag, _result) \
	(((_flag) <= (_result)) ?				\
	 ((u32)((_input) & (_flag)) * ((_result) / (_flag))) :	\
	 ((u32)((_input) & (_flag)) / ((_flag) / (_result))))

static u32 igc_tx_cmd_type(struct rtskb *skb, u32 tx_flags)
{
	/* set type for advanced descriptor with frame checksum insertion */
	u32 cmd_type = IGC_ADVTXD_DTYP_DATA |
		       IGC_ADVTXD_DCMD_DEXT |
		       IGC_ADVTXD_DCMD_IFCS;

	return cmd_type;
}

static void igc_tx_olinfo_status(struct igc_ring *tx_ring,
				 union igc_adv_tx_desc *tx_desc,
				 u32 tx_flags, unsigned int paylen)
{
	u32 olinfo_status = paylen << IGC_ADVTXD_PAYLEN_SHIFT;

	tx_desc->read.olinfo_status = cpu_to_le32(olinfo_status);
}

static int igc_tx_map(struct igc_ring *tx_ring,
		      struct igc_tx_buffer *first,
		      const u8 hdr_len)
{
	struct rtskb *skb = first->skb;
	struct igc_tx_buffer *tx_buffer;
	union igc_adv_tx_desc *tx_desc;
	u32 tx_flags = first->tx_flags;
	u16 i = tx_ring->next_to_use;
	unsigned int size;
	dma_addr_t dma;
	u32 cmd_type = igc_tx_cmd_type(skb, tx_flags);

	/* first descriptor is also last, set RS and EOP bits */
	cmd_type |= IGC_TXD_DCMD;

	tx_desc = IGC_TX_DESC(tx_ring, i);

	igc_tx_olinfo_status(tx_ring, tx_desc, tx_flags, skb->len - hdr_len);

	size = skb->len;

	dma = rtskb_data_dma_addr(skb, 0);

	tx_buffer = first;

	tx_desc->read.buffer_addr = cpu_to_le64(dma);
	/* write last descriptor with RS and EOP bits */
	cmd_type |= size | IGC_TXD_DCMD;
	tx_desc->read.cmd_type_len = cpu_to_le32(cmd_type);

	/* set the timestamp */
	first->time_stamp = jiffies;
	first->next_to_watch = tx_desc;

	i++;
	tx_desc++;
	if (i == tx_ring->count) {
		tx_desc = IGC_TX_DESC(tx_ring, 0);
		i = 0;
	}

	/* Force memory writes to complete before letting h/w know there
	 * are new descriptors to fetch.  (Only applicable for weak-ordered
	 * memory model archs, such as IA-64).
	 *
	 * We also need this memory barrier to make certain all of the
	 * status bits have been updated before next_to_watch is written.
	 */
	wmb();

	if (skb->xmit_stamp)
		*skb->xmit_stamp =
			cpu_to_be64(rtdm_clock_read() + *skb->xmit_stamp);

	tx_ring->next_to_use = i;

	/* Make sure there is space in the ring for the next send. */
	igc_maybe_stop_tx(tx_ring, DESC_NEEDED);

	writel(i, tx_ring->tail);

	return 0;
}

static netdev_tx_t igc_xmit_frame_ring(struct rtskb *skb,
				       struct igc_ring *tx_ring)
{
	u16 count = 2;
	struct igc_tx_buffer *first;
	u32 tx_flags = 0;
	u8 hdr_len = 0;

	/* need: 1 descriptor per page * PAGE_SIZE/IGC_MAX_DATA_PER_TXD,
	 *	+ 1 desc for skb_headlen/IGC_MAX_DATA_PER_TXD,
	 *	+ 2 desc gap to keep tail from touching head,
	 *	+ 1 desc for context descriptor,
	 * otherwise try next time
	 */

	if (igc_maybe_stop_tx(tx_ring, count + 3)) {
		/* this is a hard error */
		return NETDEV_TX_BUSY;
	}

	if (skb->protocol == htons(ETH_P_IP))
		tx_flags |= IGC_TX_FLAGS_IPV4;

	/* record the location of the first descriptor for this packet */
	first = &tx_ring->tx_buffer_info[tx_ring->next_to_use];
	first->skb = skb;
	first->bytecount = skb->len;
	first->gso_segs = 1;

	/* record initial flags and protocol */
	first->tx_flags = tx_flags;
	first->protocol = skb->protocol;

	igc_tx_map(tx_ring, first, hdr_len);

	return NETDEV_TX_OK;
}

static inline struct igc_ring *igc_tx_queue_mapping(struct igc_adapter *adapter,
						    struct rtskb *skb)
{
	return adapter->tx_ring[0];
}

static netdev_tx_t igc_xmit_frame(struct rtskb *skb,
				  struct rtnet_device *netdev)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);

	if (test_bit(__IGC_DOWN, &adapter->state)) {
		kfree_rtskb(skb);
		return NETDEV_TX_OK;
	}

	if (skb->len <= 0) {
		kfree_rtskb(skb);
		return NETDEV_TX_OK;
	}

	/* The minimum packet size with TCTL.PSP set is 17 so pad the skb
	 * in order to meet this minimum size requirement.
	 */
	if (skb->len < 17) {
		if (rtskb_padto(skb, 17))
			return NETDEV_TX_OK;
		skb->len = 17;
	}

	return igc_xmit_frame_ring(skb, igc_tx_queue_mapping(adapter, skb));
}

static void igc_rx_checksum(struct igc_ring *ring,
			    union igc_adv_rx_desc *rx_desc,
			    struct rtskb *skb)
{
	skb->ip_summed = CHECKSUM_NONE;

	/* Ignore Checksum bit is set */
	if (igc_test_staterr(rx_desc, IGC_RXD_STAT_IXSM))
		return;

	/* Rx checksum disabled via ethtool */
	if (!(ring->netdev->features & NETIF_F_RXCSUM))
		return;

	/* TCP/UDP checksum error bit is set */
	if (igc_test_staterr(rx_desc,
			     IGC_RXDEXT_STATERR_L4E |
			     IGC_RXDEXT_STATERR_IPE)) {
		/* work around errata with sctp packets where the TCPE aka
		 * L4E bit is set incorrectly on 64 byte (60 byte w/o crc)
		 * packets (aka let the stack check the crc32c)
		 */
		if (!(skb->len == 60 &&
		      test_bit(IGC_RING_FLAG_RX_SCTP_CSUM, &ring->flags))) {
			u64_stats_update_begin(&ring->rx_syncp);
			ring->rx_stats.csum_err++;
			u64_stats_update_end(&ring->rx_syncp);
		}
		/* let the stack verify checksum errors */
		return;
	}
	/* It must be a TCP or UDP packet with a valid checksum */
	if (igc_test_staterr(rx_desc, IGC_RXD_STAT_TCPCS |
				      IGC_RXD_STAT_UDPCS))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	rtdev_dbg(ring->netdev, "cksum success: bits %08X\n",
		   le32_to_cpu(rx_desc->wb.upper.status_error));
}

/**
 * igc_process_skb_fields - Populate skb header fields from Rx descriptor
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being populated
 *
 * This function checks the ring, descriptor, and packet information in order
 * to populate the hash, checksum, VLAN, protocol, and other fields within the
 * skb.
 */
static void igc_process_skb_fields(struct igc_ring *rx_ring,
				   union igc_adv_rx_desc *rx_desc,
				   struct rtskb *skb)
{
	igc_rx_checksum(rx_ring, rx_desc, skb);

	skb->protocol = rt_eth_type_trans(skb, rx_ring->netdev);
}

static inline bool igc_page_is_reserved(struct page *page)
{
	return (page_to_nid(page) != numa_mem_id()) || page_is_pfmemalloc(page);
}

/**
 * igc_is_non_eop - process handling of non-EOP buffers
 * @rx_ring: Rx ring being processed
 * @rx_desc: Rx descriptor for current buffer
 *
 * This function updates next to clean.  If the buffer is an EOP buffer
 * this function exits returning false, otherwise it will place the
 * sk_buff in the next buffer to be chained and return true indicating
 * that this is in fact a non-EOP buffer.
 */
static bool igc_is_non_eop(struct igc_ring *rx_ring,
			   union igc_adv_rx_desc *rx_desc)
{
	u32 ntc = rx_ring->next_to_clean + 1;

	/* fetch, update, and store next to clean */
	ntc = (ntc < rx_ring->count) ? ntc : 0;
	rx_ring->next_to_clean = ntc;

	prefetch(IGC_RX_DESC(rx_ring, ntc));

	if (likely(igc_test_staterr(rx_desc, IGC_RXD_STAT_EOP)))
		return false;

	return true;
}

/**
 * igc_cleanup_headers - Correct corrupted or empty headers
 * @rx_ring: rx descriptor ring packet is being transacted on
 * @rx_desc: pointer to the EOP Rx descriptor
 * @skb: pointer to current skb being fixed
 *
 * Address the case where we are pulling data in on pages only
 * and as such no data is present in the skb header.
 *
 * In addition if skb is not at least 60 bytes we need to pad it so that
 * it is large enough to qualify as a valid Ethernet frame.
 *
 * Returns true if an error was encountered and skb was freed.
 */
static bool igc_cleanup_headers(struct igc_ring *rx_ring,
				union igc_adv_rx_desc *rx_desc,
				struct rtskb *skb)
{
	if (unlikely(igc_test_staterr(rx_desc, IGC_RXDEXT_STATERR_RXE))) {
		struct rtnet_device *netdev = rx_ring->netdev;

		if (!(netdev->features & NETIF_F_RXALL)) {
			kfree_rtskb(skb);
			return true;
		}
	}

	return false;
}

static inline unsigned int igc_rx_offset(struct igc_ring *rx_ring)
{
	return ring_uses_build_skb(rx_ring) ? IGC_SKB_PAD : 0;
}

static bool igc_alloc_mapped_skb(struct igc_ring *rx_ring,
				  struct igc_rx_buffer *bi)
{
	struct igc_adapter *adapter = rx_ring->q_vector->adapter;
	struct rtskb *skb = bi->skb;
	dma_addr_t dma = bi->dma;

	/* since we are recycling buffers we should seldom need to alloc */
	if (dma)
		return true;

	if (likely(!skb)) {
		skb = rtnetdev_alloc_rtskb(adapter->netdev,
					rx_ring->rx_buffer_len + NET_IP_ALIGN);
		if (!skb) {
			rx_ring->rx_stats.alloc_failed++;
			return false;
		}

		rtskb_reserve(skb, NET_IP_ALIGN);
		skb->rtdev = adapter->netdev;

		bi->skb = skb;
		bi->dma = rtskb_data_dma_addr(skb, 0);
	}

	return true;
}

/**
 * igc_alloc_rx_buffers - Replace used receive buffers; packet split
 * @rx_ring: rx descriptor ring
 * @cleaned_count: number of buffers to clean
 */
static void igc_alloc_rx_buffers(struct igc_ring *rx_ring, u16 cleaned_count)
{
	union igc_adv_rx_desc *rx_desc;
	u16 i = rx_ring->next_to_use;
	struct igc_rx_buffer *bi;

	/* nothing to do */
	if (!cleaned_count)
		return;

	rx_desc = IGC_RX_DESC(rx_ring, i);
	bi = &rx_ring->rx_buffer_info[i];
	i -= rx_ring->count;

	do {
		if (!igc_alloc_mapped_skb(rx_ring, bi))
			break;

		/* Refresh the desc even if buffer_addrs didn't change
		 * because each write-back erases this info.
		 */
		rx_desc->read.pkt_addr = cpu_to_le64(bi->dma);

		rx_desc++;
		bi++;
		i++;
		if (unlikely(!i)) {
			rx_desc = IGC_RX_DESC(rx_ring, 0);
			bi = rx_ring->rx_buffer_info;
			i -= rx_ring->count;
		}

		/* clear the status bits for the next_to_use descriptor */
		rx_desc->wb.upper.status_error = 0;

		cleaned_count--;
	} while (cleaned_count);

	i += rx_ring->count;

	if (rx_ring->next_to_use != i) {
		/* record the next descriptor to use */
		rx_ring->next_to_use = i;

		/* Force memory writes to complete before letting h/w
		 * know there are new descriptors to fetch.  (Only
		 * applicable for weak-ordered memory model archs,
		 * such as IA-64).
		 */
		wmb();
		writel(i, rx_ring->tail);
	}
}

static struct rtskb *igc_fetch_rx_buffer(struct igc_ring *rx_ring,
					   union igc_adv_rx_desc *rx_desc)
{
	struct igc_rx_buffer *rx_buffer;
	struct rtskb *skb;

	rx_buffer = &rx_ring->rx_buffer_info[rx_ring->next_to_clean];
	skb = rx_buffer->skb;
	prefetchw(skb->data);

	/* pull the header of the skb in */
	rtskb_put(skb, le16_to_cpu(rx_desc->wb.upper.length));
	rx_buffer->skb = NULL;
	rx_buffer->dma = 0;

	return skb;
}

static bool igc_clean_rx_irq(struct igc_q_vector *q_vector, const int budget)
{
	unsigned int total_bytes = 0, total_packets = 0;
	struct igc_ring *rx_ring = q_vector->rx.ring;
	u16 cleaned_count = igc_desc_unused(rx_ring);
	nanosecs_abs_t time_stamp = rtdm_clock_read();
	struct rtskb *skb =  rx_ring->skb;

	while (likely(total_packets < budget)) {
		union igc_adv_rx_desc *rx_desc;

		/* return some buffers to hardware, one at a time is too slow */
		if (cleaned_count >= IGC_RX_BUFFER_WRITE) {
			igc_alloc_rx_buffers(rx_ring, cleaned_count);
			cleaned_count = 0;
		}

		rx_desc = IGC_RX_DESC(rx_ring, rx_ring->next_to_clean);

		if (!rx_desc->wb.upper.status_error)
			break;

		/* This memory barrier is needed to keep us from reading
		 * any other fields out of the rx_desc until we know the
		 * descriptor has been written back
		 */
		rmb();

		skb = igc_fetch_rx_buffer(rx_ring, rx_desc);
		skb->time_stamp = time_stamp;

		cleaned_count++;

		/* fetch next buffer in frame if non-eop */
		if (igc_is_non_eop(rx_ring, rx_desc)) {
			kfree_rtskb(skb);
			continue;
		}

		/* verify the packet layout is correct */
		if (igc_cleanup_headers(rx_ring, rx_desc, skb)) {
			skb = NULL;
			continue;
		}

		/* probably a little skewed due to removing CRC */
		total_bytes += skb->len;

		/* populate checksum, VLAN, and protocol */
		igc_process_skb_fields(rx_ring, rx_desc, skb);

		rtnetif_rx(skb);

		/* reset skb pointer */
		skb = NULL;

		/* update budget accounting */
		total_packets++;
	}

	rx_ring->rx_stats.packets += total_packets;
	rx_ring->rx_stats.bytes += total_bytes;

	q_vector->rx.total_packets += total_packets;
	q_vector->rx.total_bytes += total_bytes;

	if (cleaned_count)
		igc_alloc_rx_buffers(rx_ring, cleaned_count);

	if (total_packets)
		rt_mark_stack_mgr(q_vector->adapter->netdev);

	return total_packets < budget;
}

/**
 * igc_clean_tx_irq - Reclaim resources after transmit completes
 * @q_vector: pointer to q_vector containing needed info
 *
 * returns true if ring is completely cleaned
 */
static bool igc_clean_tx_irq(struct igc_q_vector *q_vector)
{
	struct igc_adapter *adapter = q_vector->adapter;
	unsigned int total_bytes = 0, total_packets = 0;
	unsigned int budget = q_vector->tx.work_limit;
	struct igc_ring *tx_ring = q_vector->tx.ring;
	unsigned int i = tx_ring->next_to_clean;
	struct igc_tx_buffer *tx_buffer;
	union igc_adv_tx_desc *tx_desc;

	if (test_bit(__IGC_DOWN, &adapter->state))
		return true;

	tx_buffer = &tx_ring->tx_buffer_info[i];
	tx_desc = IGC_TX_DESC(tx_ring, i);
	i -= tx_ring->count;

	do {
		union igc_adv_tx_desc *eop_desc = tx_buffer->next_to_watch;

		/* if next_to_watch is not set then there is no work pending */
		if (!eop_desc)
			break;

		/* prevent any other reads prior to eop_desc */
		smp_rmb();

		/* if DD is not set pending work has not been completed */
		if (!(eop_desc->wb.status & cpu_to_le32(IGC_TXD_STAT_DD)))
			break;

		/* clear next_to_watch to prevent false hangs */
		tx_buffer->next_to_watch = NULL;

		/* update the statistics for this packet */
		total_bytes += tx_buffer->bytecount;
		total_packets += tx_buffer->gso_segs;

		/* free the skb */
		kfree_rtskb(tx_buffer->skb);

		/* clear tx_buffer data */
		tx_buffer->skb = NULL;

		/* clear last DMA location and unmap remaining buffers */
		while (tx_desc != eop_desc) {
			tx_buffer++;
			tx_desc++;
			i++;
			if (unlikely(!i)) {
				i -= tx_ring->count;
				tx_buffer = tx_ring->tx_buffer_info;
				tx_desc = IGC_TX_DESC(tx_ring, 0);
			}

		}

		/* move us one more past the eop_desc for start of next pkt */
		tx_buffer++;
		tx_desc++;
		i++;
		if (unlikely(!i)) {
			i -= tx_ring->count;
			tx_buffer = tx_ring->tx_buffer_info;
			tx_desc = IGC_TX_DESC(tx_ring, 0);
		}

		/* issue prefetch for next Tx descriptor */
		prefetch(tx_desc);

		/* update budget accounting */
		budget--;
	} while (likely(budget));

	i += tx_ring->count;
	tx_ring->next_to_clean = i;
	tx_ring->tx_stats.bytes += total_bytes;
	tx_ring->tx_stats.packets += total_packets;
	q_vector->tx.total_bytes += total_bytes;
	q_vector->tx.total_packets += total_packets;

	if (test_bit(IGC_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags)) {
		struct igc_hw *hw = &adapter->hw;

		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i
		 */
		clear_bit(IGC_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
		if (tx_buffer->next_to_watch &&
		    time_after(jiffies, tx_buffer->time_stamp +
		    (adapter->tx_timeout_factor * HZ)) &&
		    !(rd32(IGC_STATUS) & IGC_STATUS_TXOFF)) {
			/* detected Tx unit hang */
			rtdev_err(tx_ring->netdev,
				   "Detected Tx Unit Hang\n"
				   "  Tx Queue             <%d>\n"
				   "  TDH                  <%x>\n"
				   "  TDT                  <%x>\n"
				   "  next_to_use          <%x>\n"
				   "  next_to_clean        <%x>\n"
				   "buffer_info[next_to_clean]\n"
				   "  time_stamp           <%lx>\n"
				   "  next_to_watch        <%p>\n"
				   "  jiffies              <%lx>\n"
				   "  desc.status          <%x>\n",
				   tx_ring->queue_index,
				   rd32(IGC_TDH(tx_ring->reg_idx)),
				   readl(tx_ring->tail),
				   tx_ring->next_to_use,
				   tx_ring->next_to_clean,
				   tx_buffer->time_stamp,
				   tx_buffer->next_to_watch,
				   jiffies,
				   tx_buffer->next_to_watch->wb.status);
			rtnetif_stop_queue(tx_ring->netdev);

			/* we are about to reset, no point in enabling stuff */
			return true;
		}
	}

#define TX_WAKE_THRESHOLD (DESC_NEEDED * 2)
	if (unlikely(total_packets &&
		     rtnetif_carrier_ok(tx_ring->netdev) &&
		     igc_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();
		if (rtnetif_queue_stopped(tx_ring->netdev) &&
		    !(test_bit(__IGC_DOWN, &adapter->state))) {
			rtnetif_wake_queue(tx_ring->netdev);

			tx_ring->tx_stats.restart_queue++;
		}
	}

	return !!budget;
}

/**
 *  igc_write_uc_addr_list - write unicast addresses to RAR table
 *  @netdev: network interface device structure
 *
 *  Writes unicast address list to the RAR table.
 *  Returns: -ENOMEM on failure/insufficient address space
 *           0 on no addresses written
 *           X on writing X addresses to the RAR table
 **/
static int igc_write_uc_addr_list(struct rtnet_device *netdev)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	unsigned int vfn = 0;
	unsigned int rar_entries = hw->mac.rar_entry_count - (vfn + 1);
	int count = 0;


	/* write the addresses in reverse order to avoid write combining */
	for (; rar_entries > 0 ; rar_entries--) {
		wr32(IGC_RAH(rar_entries), 0);
		wr32(IGC_RAL(rar_entries), 0);
	}

	wrfl();

	return count;
}


/**
 * igc_set_rx_mode - Secondary Unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_rx_mode entry point is called whenever the unicast or multicast
 * address lists or the network interface flags are updated.  This routine is
 * responsible for configuring the hardware for proper unicast, multicast,
 * promiscuous mode, and all-multi behavior.
 */
static void igc_set_rx_mode(struct rtnet_device *netdev)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 rctl = 0, rlpml = MAX_JUMBO_FRAME_SIZE;
	int count = 0;

	/* Check for Promiscuous and All Multicast modes */
	if (netdev->flags & IFF_PROMISC) {
		rctl |= IGC_RCTL_UPE | IGC_RCTL_MPE;
	} else {
		if (netdev->flags & IFF_ALLMULTI) {
			rctl |= IGC_RCTL_MPE;
		} else {
			/* Write addresses to the MTA, if the attempt fails
			 * then we should just turn on promiscuous mode so
			 * that we can at least receive multicast traffic
			 */
			count = igc_write_mc_addr_list(netdev);
			if (count < 0)
				rctl |= IGC_RCTL_MPE;
		}
	}

	/* Write addresses to available RAR registers, if there is not
	 * sufficient space to store all the addresses then enable
	 * unicast promiscuous mode
	 */
	count = igc_write_uc_addr_list(netdev);

	if (count < 0)
		rctl |= IGC_RCTL_UPE;

	/* update state of unicast and multicast */
	rctl |= rd32(IGC_RCTL) & ~(IGC_RCTL_UPE | IGC_RCTL_MPE);
	wr32(IGC_RCTL, rctl);

	wr32(IGC_RLPML, rlpml);
}

/**
 * igc_configure - configure the hardware for RX and TX
 * @adapter: private board structure
 */
static void igc_configure(struct igc_adapter *adapter)
{
	struct rtnet_device *netdev = adapter->netdev;
	int i = 0;

	igc_get_hw_control(adapter);
	igc_set_rx_mode(netdev);

	igc_setup_tctl(adapter);
	igc_setup_mrqc(adapter);
	igc_setup_rctl(adapter);

	igc_configure_tx(adapter);
	igc_configure_rx(adapter);

	igc_rx_fifo_flush_base(&adapter->hw);

	/* call igc_desc_unused which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean
	 */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igc_ring *ring = adapter->rx_ring[i];

		igc_alloc_rx_buffers(ring, igc_desc_unused(ring));
	}
}

/**
 * igc_write_ivar - configure ivar for given MSI-X vector
 * @hw: pointer to the HW structure
 * @msix_vector: vector number we are allocating to a given ring
 * @index: row index of IVAR register to write within IVAR table
 * @offset: column offset of in IVAR, should be multiple of 8
 *
 * The IVAR table consists of 2 columns,
 * each containing an cause allocation for an Rx and Tx ring, and a
 * variable number of rows depending on the number of queues supported.
 */
static void igc_write_ivar(struct igc_hw *hw, int msix_vector,
			   int index, int offset)
{
	u32 ivar = array_rd32(IGC_IVAR0, index);

	/* clear any bits that are currently set */
	ivar &= ~((u32)0xFF << offset);

	/* write vector and valid bit */
	ivar |= (msix_vector | IGC_IVAR_VALID) << offset;

	array_wr32(IGC_IVAR0, index, ivar);
}

static void igc_assign_vector(struct igc_q_vector *q_vector, int msix_vector)
{
	struct igc_adapter *adapter = q_vector->adapter;
	struct igc_hw *hw = &adapter->hw;
	int rx_queue = IGC_N0_QUEUE;
	int tx_queue = IGC_N0_QUEUE;

	if (q_vector->rx.ring)
		rx_queue = q_vector->rx.ring->reg_idx;
	if (q_vector->tx.ring)
		tx_queue = q_vector->tx.ring->reg_idx;

	switch (hw->mac.type) {
	case igc_i225:
		if (rx_queue > IGC_N0_QUEUE)
			igc_write_ivar(hw, msix_vector,
				       rx_queue >> 1,
				       (rx_queue & 0x1) << 4);
		if (tx_queue > IGC_N0_QUEUE)
			igc_write_ivar(hw, msix_vector,
				       tx_queue >> 1,
				       ((tx_queue & 0x1) << 4) + 8);
		q_vector->eims_value = BIT(msix_vector);
		break;
	default:
		WARN_ONCE(hw->mac.type != igc_i225, "Wrong MAC type\n");
		break;
	}

	/* add q_vector eims value to global eims_enable_mask */
	adapter->eims_enable_mask |= q_vector->eims_value;

	/* configure q_vector to set itr on first interrupt */
	q_vector->set_itr = 1;
}

/**
 * igc_configure_msix - Configure MSI-X hardware
 * @adapter: Pointer to adapter structure
 *
 * igc_configure_msix sets up the hardware to properly
 * generate MSI-X interrupts.
 */
static void igc_configure_msix(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	int i, vector = 0;
	u32 tmp;

	adapter->eims_enable_mask = 0;

	/* set vector for other causes, i.e. link changes */
	switch (hw->mac.type) {
	case igc_i225:
		/* Turn on MSI-X capability first, or our settings
		 * won't stick.  And it will take days to debug.
		 */
		wr32(IGC_GPIE, IGC_GPIE_MSIX_MODE |
		     IGC_GPIE_PBA | IGC_GPIE_EIAME |
		     IGC_GPIE_NSICR);

		/* enable msix_other interrupt */
		adapter->eims_other = BIT(vector);
		tmp = (vector++ | IGC_IVAR_VALID) << 8;

		wr32(IGC_IVAR_MISC, tmp);
		break;
	default:
		/* do nothing, since nothing else supports MSI-X */
		break;
	} /* switch (hw->mac.type) */

	adapter->eims_enable_mask |= adapter->eims_other;

	for (i = 0; i < adapter->num_q_vectors; i++)
		igc_assign_vector(adapter->q_vector[i], vector++);

	wrfl();
}

/**
 * igc_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 */
static void igc_irq_enable(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		u32 ims = IGC_IMS_LSC | IGC_IMS_DOUTSYNC | IGC_IMS_DRSTA;
		u32 regval = rd32(IGC_EIAC);

		wr32(IGC_EIAC, regval | adapter->eims_enable_mask);
		regval = rd32(IGC_EIAM);
		wr32(IGC_EIAM, regval | adapter->eims_enable_mask);
		wr32(IGC_EIMS, adapter->eims_enable_mask);
		wr32(IGC_IMS, ims);
	} else {
		wr32(IGC_IMS, IMS_ENABLE_MASK | IGC_IMS_DRSTA);
		wr32(IGC_IAM, IMS_ENABLE_MASK | IGC_IMS_DRSTA);
	}
}

/**
 * igc_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 */
static void igc_irq_disable(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	if (adapter->msix_entries) {
		u32 regval = rd32(IGC_EIAM);

		wr32(IGC_EIAM, regval & ~adapter->eims_enable_mask);
		wr32(IGC_EIMC, adapter->eims_enable_mask);
		regval = rd32(IGC_EIAC);
		wr32(IGC_EIAC, regval & ~adapter->eims_enable_mask);
	}

	wr32(IGC_IAM, 0);
	wr32(IGC_IMC, ~0);
	wrfl();

	msleep(10);

}

void igc_set_flag_queue_pairs(struct igc_adapter *adapter,
			      const u32 max_rss_queues)
{
	/* Determine if we need to pair queues. */
	/* If rss_queues > half of max_rss_queues, pair the queues in
	 * order to conserve interrupts due to limited supply.
	 */
	if (adapter->rss_queues > (max_rss_queues / 2))
		adapter->flags |= IGC_FLAG_QUEUE_PAIRS;
	else
		adapter->flags &= ~IGC_FLAG_QUEUE_PAIRS;
}

unsigned int igc_get_max_rss_queues(struct igc_adapter *adapter)
{
	return IGC_MAX_RX_QUEUES;
}

static void igc_init_queue_configuration(struct igc_adapter *adapter)
{
	u32 max_rss_queues;

	max_rss_queues = igc_get_max_rss_queues(adapter);
	adapter->rss_queues = min_t(u32, max_rss_queues, num_online_cpus());

	igc_set_flag_queue_pairs(adapter, max_rss_queues);
}

/**
 * igc_reset_q_vector - Reset config for interrupt vector
 * @adapter: board private structure to initialize
 * @v_idx: Index of vector to be reset
 *
 * If NAPI is enabled it will delete any references to the
 * NAPI struct. This is preparation for igc_free_q_vector.
 */
static void igc_reset_q_vector(struct igc_adapter *adapter, int v_idx)
{
	struct igc_q_vector *q_vector = adapter->q_vector[v_idx];

	/* if we're coming from igc_set_interrupt_capability, the vectors are
	 * not yet allocated
	 */
	if (!q_vector)
		return;

	if (q_vector->tx.ring)
		adapter->tx_ring[q_vector->tx.ring->queue_index] = NULL;

	if (q_vector->rx.ring)
		adapter->rx_ring[q_vector->rx.ring->queue_index] = NULL;
}

/**
 * igc_free_q_vector - Free memory allocated for specific interrupt vector
 * @adapter: board private structure to initialize
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.
 */
static void igc_free_q_vector(struct igc_adapter *adapter, int v_idx)
{
	struct igc_q_vector *q_vector = adapter->q_vector[v_idx];

	adapter->q_vector[v_idx] = NULL;

	/* igc_get_stats64() might access the rings on this vector,
	 * we must wait a grace period before freeing it.
	 */
	if (q_vector)
		kfree_rcu(q_vector, rcu);
}

/**
 * igc_free_q_vectors - Free memory allocated for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * This function frees the memory allocated to the q_vectors.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 */
static void igc_free_q_vectors(struct igc_adapter *adapter)
{
	int v_idx = adapter->num_q_vectors;

	adapter->num_tx_queues = 0;
	adapter->num_rx_queues = 0;
	adapter->num_q_vectors = 0;

	while (v_idx--) {
		igc_reset_q_vector(adapter, v_idx);
		igc_free_q_vector(adapter, v_idx);
	}
}

/**
 * igc_update_itr - update the dynamic ITR value based on statistics
 * @q_vector: pointer to q_vector
 * @ring_container: ring info to update the itr for
 *
 * Stores a new ITR value based on packets and byte
 * counts during the last interrupt.  The advantage of per interrupt
 * computation is faster updates and more accurate ITR for the current
 * traffic pattern.  Constants in this function were computed
 * based on theoretical maximum wire speed and thresholds were set based
 * on testing data as well as attempting to minimize response time
 * while increasing bulk throughput.
 * NOTE: These calculations are only valid when operating in a single-
 * queue environment.
 */
static void igc_update_itr(struct igc_q_vector *q_vector,
			   struct igc_ring_container *ring_container)
{
	unsigned int packets = ring_container->total_packets;
	unsigned int bytes = ring_container->total_bytes;
	u8 itrval = ring_container->itr;

	/* no packets, exit with status unchanged */
	if (packets == 0)
		return;

	switch (itrval) {
	case lowest_latency:
		/* handle TSO and jumbo frames */
		if (bytes / packets > 8000)
			itrval = bulk_latency;
		else if ((packets < 5) && (bytes > 512))
			itrval = low_latency;
		break;
	case low_latency:  /* 50 usec aka 20000 ints/s */
		if (bytes > 10000) {
			/* this if handles the TSO accounting */
			if (bytes / packets > 8000)
				itrval = bulk_latency;
			else if ((packets < 10) || ((bytes / packets) > 1200))
				itrval = bulk_latency;
			else if ((packets > 35))
				itrval = lowest_latency;
		} else if (bytes / packets > 2000) {
			itrval = bulk_latency;
		} else if (packets <= 2 && bytes < 512) {
			itrval = lowest_latency;
		}
		break;
	case bulk_latency: /* 250 usec aka 4000 ints/s */
		if (bytes > 25000) {
			if (packets > 35)
				itrval = low_latency;
		} else if (bytes < 1500) {
			itrval = low_latency;
		}
		break;
	}

	/* clear work counters since we have the values we need */
	ring_container->total_bytes = 0;
	ring_container->total_packets = 0;

	/* write updated itr to ring container */
	ring_container->itr = itrval;
}

static void igc_set_itr(struct igc_q_vector *q_vector)
{
	struct igc_adapter *adapter = q_vector->adapter;
	u32 new_itr = q_vector->itr_val;
	u8 current_itr = 0;

	if (!InterruptThrottle)
		return;

	/* for non-gigabit speeds, just fix the interrupt rate at 4000 */
	switch (adapter->link_speed) {
	case SPEED_10:
	case SPEED_100:
		current_itr = 0;
		new_itr = IGC_4K_ITR;
		goto set_itr_now;
	default:
		break;
	}

	igc_update_itr(q_vector, &q_vector->tx);
	igc_update_itr(q_vector, &q_vector->rx);

	current_itr = max(q_vector->rx.itr, q_vector->tx.itr);

	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (current_itr == lowest_latency &&
	    ((q_vector->rx.ring && adapter->rx_itr_setting == 3) ||
	    (!q_vector->rx.ring && adapter->tx_itr_setting == 3)))
		current_itr = low_latency;

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = IGC_70K_ITR; /* 70,000 ints/sec */
		break;
	case low_latency:
		new_itr = IGC_20K_ITR; /* 20,000 ints/sec */
		break;
	case bulk_latency:
		new_itr = IGC_4K_ITR;  /* 4,000 ints/sec */
		break;
	default:
		break;
	}

set_itr_now:
	if (new_itr != q_vector->itr_val) {
		/* this attempts to bias the interrupt rate towards Bulk
		 * by adding intermediate steps when interrupt rate is
		 * increasing
		 */
		new_itr = new_itr > q_vector->itr_val ?
			  max((new_itr * q_vector->itr_val) /
			  (new_itr + (q_vector->itr_val >> 2)),
			  new_itr) : new_itr;
		/* Don't write the value here; it resets the adapter's
		 * internal timer, and causes us to delay far longer than
		 * we should between interrupts.  Instead, we write the ITR
		 * value at the beginning of the next interrupt so the timing
		 * ends up being correct.
		 */
		q_vector->itr_val = new_itr;
		q_vector->set_itr = 1;
	}
}

static void igc_reset_interrupt_capability(struct igc_adapter *adapter)
{
	int v_idx = adapter->num_q_vectors;

	if (adapter->msix_entries) {
		pci_disable_msix(adapter->pdev);
		kfree(adapter->msix_entries);
		adapter->msix_entries = NULL;
	} else if (adapter->flags & IGC_FLAG_HAS_MSI) {
		pci_disable_msi(adapter->pdev);
	}

	while (v_idx--)
		igc_reset_q_vector(adapter, v_idx);
}

/**
 * igc_set_interrupt_capability - set MSI or MSI-X if supported
 * @adapter: Pointer to adapter structure
 * @msix: boolean value for MSI-X capability
 *
 * Attempt to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 */
static void igc_set_interrupt_capability(struct igc_adapter *adapter,
					 bool msix)
{
	int numvecs, i;
	int err;

	if (!msix)
		goto msi_only;
	adapter->flags |= IGC_FLAG_HAS_MSIX;

	/* Number of supported queues. */
	adapter->num_rx_queues = adapter->rss_queues;

	adapter->num_tx_queues = adapter->rss_queues;

	/* start with one vector for every Rx queue */
	numvecs = adapter->num_rx_queues;

	/* if Tx handler is separate add 1 for every Tx queue */
	if (!(adapter->flags & IGC_FLAG_QUEUE_PAIRS))
		numvecs += adapter->num_tx_queues;

	/* store the number of vectors reserved for queues */
	adapter->num_q_vectors = numvecs;

	/* add 1 vector for link status interrupts */
	numvecs++;

	adapter->msix_entries = kcalloc(numvecs, sizeof(struct msix_entry),
					GFP_KERNEL);

	if (!adapter->msix_entries)
		return;

	/* populate entry values */
	for (i = 0; i < numvecs; i++)
		adapter->msix_entries[i].entry = i;

	err = pci_enable_msix_range(adapter->pdev,
				    adapter->msix_entries,
				    numvecs,
				    numvecs);
	if (err > 0)
		return;

	kfree(adapter->msix_entries);
	adapter->msix_entries = NULL;

	igc_reset_interrupt_capability(adapter);

msi_only:
	adapter->flags &= ~IGC_FLAG_HAS_MSIX;

	adapter->rss_queues = 1;
	adapter->flags |= IGC_FLAG_QUEUE_PAIRS;
	adapter->num_rx_queues = 1;
	adapter->num_tx_queues = 1;
	adapter->num_q_vectors = 1;
	if (!pci_enable_msi(adapter->pdev))
		adapter->flags |= IGC_FLAG_HAS_MSI;
}

/**
 * igc_update_ring_itr - update the dynamic ITR value based on packet size
 * @q_vector: pointer to q_vector
 *
 * Stores a new ITR value based on strictly on packet size.  This
 * algorithm is less sophisticated than that used in igc_update_itr,
 * due to the difficulty of synchronizing statistics across multiple
 * receive rings.  The divisors and thresholds used by this function
 * were determined based on theoretical maximum wire speed and testing
 * data, in order to minimize response time while increasing bulk
 * throughput.
 * NOTE: This function is called only when operating in a multiqueue
 * receive environment.
 */
static void igc_update_ring_itr(struct igc_q_vector *q_vector)
{
	struct igc_adapter *adapter = q_vector->adapter;
	int new_val = q_vector->itr_val;
	int avg_wire_size = 0;
	unsigned int packets;

	/* For non-gigabit speeds, just fix the interrupt rate at 4000
	 * ints/sec - ITR timer value of 120 ticks.
	 */
	switch (adapter->link_speed) {
	case SPEED_10:
	case SPEED_100:
		new_val = IGC_4K_ITR;
		goto set_itr_val;
	default:
		break;
	}

	packets = q_vector->rx.total_packets;
	if (packets)
		avg_wire_size = q_vector->rx.total_bytes / packets;

	packets = q_vector->tx.total_packets;
	if (packets)
		avg_wire_size = max_t(u32, avg_wire_size,
				      q_vector->tx.total_bytes / packets);

	/* if avg_wire_size isn't set no work was done */
	if (!avg_wire_size)
		goto clear_counts;

	/* Add 24 bytes to size to account for CRC, preamble, and gap */
	avg_wire_size += 24;

	/* Don't starve jumbo frames */
	avg_wire_size = min(avg_wire_size, 3000);

	/* Give a little boost to mid-size frames */
	if (avg_wire_size > 300 && avg_wire_size < 1200)
		new_val = avg_wire_size / 3;
	else
		new_val = avg_wire_size / 2;

	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (new_val < IGC_20K_ITR &&
	    ((q_vector->rx.ring && adapter->rx_itr_setting == 3) ||
	    (!q_vector->rx.ring && adapter->tx_itr_setting == 3)))
		new_val = IGC_20K_ITR;

set_itr_val:
	if (new_val != q_vector->itr_val) {
		q_vector->itr_val = new_val;
		q_vector->set_itr = 1;
	}
clear_counts:
	q_vector->rx.total_bytes = 0;
	q_vector->rx.total_packets = 0;
	q_vector->tx.total_bytes = 0;
	q_vector->tx.total_packets = 0;
}

static void igc_ring_irq_enable(struct igc_q_vector *q_vector)
{
	struct igc_adapter *adapter = q_vector->adapter;
	struct igc_hw *hw = &adapter->hw;

	if ((q_vector->rx.ring && (adapter->rx_itr_setting & 3)) ||
	    (!q_vector->rx.ring && (adapter->tx_itr_setting & 3))) {
		if (adapter->num_q_vectors == 1)
			igc_set_itr(q_vector);
		else
			igc_update_ring_itr(q_vector);
	}

	if (!test_bit(__IGC_DOWN, &adapter->state)) {
		if (adapter->msix_entries)
			wr32(IGC_EIMS, q_vector->eims_value);
		else
			igc_irq_enable(adapter);
	}
}

static void igc_add_ring(struct igc_ring *ring,
			 struct igc_ring_container *head)
{
	head->ring = ring;
	head->count++;
}

/**
 * igc_cache_ring_register - Descriptor ring to register mapping
 * @adapter: board private structure to initialize
 *
 * Once we know the feature-set enabled for the device, we'll cache
 * the register offset the descriptor ring is assigned to.
 */
static void igc_cache_ring_register(struct igc_adapter *adapter)
{
	int i = 0, j = 0;

	switch (adapter->hw.mac.type) {
	case igc_i225:
	default:
		for (; i < adapter->num_rx_queues; i++)
			adapter->rx_ring[i]->reg_idx = i;
		for (; j < adapter->num_tx_queues; j++)
			adapter->tx_ring[j]->reg_idx = j;
		break;
	}
}

/**
 * igc_alloc_q_vector - Allocate memory for a single interrupt vector
 * @adapter: board private structure to initialize
 * @v_count: q_vectors allocated on adapter, used for ring interleaving
 * @v_idx: index of vector in adapter struct
 * @txr_count: total number of Tx rings to allocate
 * @txr_idx: index of first Tx ring to allocate
 * @rxr_count: total number of Rx rings to allocate
 * @rxr_idx: index of first Rx ring to allocate
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 */
static int igc_alloc_q_vector(struct igc_adapter *adapter,
			      unsigned int v_count, unsigned int v_idx,
			      unsigned int txr_count, unsigned int txr_idx,
			      unsigned int rxr_count, unsigned int rxr_idx)
{
	struct igc_q_vector *q_vector;
	struct igc_ring *ring;
	int ring_count, size;

	/* igc only supports 1 Tx and/or 1 Rx queue per vector */
	if (txr_count > 1 || rxr_count > 1)
		return -ENOMEM;

	ring_count = txr_count + rxr_count;

	/* allocate q_vector and rings */
	q_vector = adapter->q_vector[v_idx];
	size = sizeof(struct igc_q_vector) +
		(sizeof(struct igc_ring) * ring_count);

	if (!q_vector)
		q_vector = kzalloc(size,
				   GFP_KERNEL);
	else
		memset(q_vector, 0, size);

	if (!q_vector)
		return -ENOMEM;

	/* tie q_vector and adapter together */
	adapter->q_vector[v_idx] = q_vector;
	q_vector->adapter = adapter;

	/* initialize work limits */
	q_vector->tx.work_limit = adapter->tx_work_limit;

	/* initialize ITR configuration */
	q_vector->itr_register = adapter->io_addr + IGC_EITR(0);
	q_vector->itr_val = IGC_START_ITR;

	/* initialize pointer to rings */
	ring = q_vector->ring;

	/* initialize ITR */
	if (rxr_count) {
		/* rx or rx/tx vector */
		if (!adapter->rx_itr_setting || adapter->rx_itr_setting > 3)
			q_vector->itr_val = adapter->rx_itr_setting;
	} else {
		/* tx only vector */
		if (!adapter->tx_itr_setting || adapter->tx_itr_setting > 3)
			q_vector->itr_val = adapter->tx_itr_setting;
	}

	if (txr_count) {
		/* assign generic ring traits */
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Tx values */
		igc_add_ring(ring, &q_vector->tx);

		/* apply Tx specific ring traits */
		ring->count = adapter->tx_ring_count;
		ring->queue_index = txr_idx;

		/* assign ring to adapter */
		adapter->tx_ring[txr_idx] = ring;

		/* push pointer to next ring */
		ring++;
	}

	if (rxr_count) {
		/* assign generic ring traits */
		ring->dev = &adapter->pdev->dev;
		ring->netdev = adapter->netdev;

		/* configure backlink on ring */
		ring->q_vector = q_vector;

		/* update q_vector Rx values */
		igc_add_ring(ring, &q_vector->rx);

		/* apply Rx specific ring traits */
		ring->count = adapter->rx_ring_count;
		ring->queue_index = rxr_idx;

		/* assign ring to adapter */
		adapter->rx_ring[rxr_idx] = ring;
	}

	return 0;
}

/**
 * igc_alloc_q_vectors - Allocate memory for interrupt vectors
 * @adapter: board private structure to initialize
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 */
static int igc_alloc_q_vectors(struct igc_adapter *adapter)
{
	int rxr_remaining = adapter->num_rx_queues;
	int txr_remaining = adapter->num_tx_queues;
	int rxr_idx = 0, txr_idx = 0, v_idx = 0;
	int q_vectors = adapter->num_q_vectors;
	int err;

	if (q_vectors >= (rxr_remaining + txr_remaining)) {
		for (; rxr_remaining; v_idx++) {
			err = igc_alloc_q_vector(adapter, q_vectors, v_idx,
						 0, 0, 1, rxr_idx);

			if (err)
				goto err_out;

			/* update counts and index */
			rxr_remaining--;
			rxr_idx++;
		}
	}

	for (; v_idx < q_vectors; v_idx++) {
		int rqpv = DIV_ROUND_UP(rxr_remaining, q_vectors - v_idx);
		int tqpv = DIV_ROUND_UP(txr_remaining, q_vectors - v_idx);

		err = igc_alloc_q_vector(adapter, q_vectors, v_idx,
					 tqpv, txr_idx, rqpv, rxr_idx);

		if (err)
			goto err_out;

		/* update counts and index */
		rxr_remaining -= rqpv;
		txr_remaining -= tqpv;
		rxr_idx++;
		txr_idx++;
	}

	return 0;

err_out:
	adapter->num_tx_queues = 0;
	adapter->num_rx_queues = 0;
	adapter->num_q_vectors = 0;

	while (v_idx--)
		igc_free_q_vector(adapter, v_idx);

	return -ENOMEM;
}

/**
 * igc_init_interrupt_scheme - initialize interrupts, allocate queues/vectors
 * @adapter: Pointer to adapter structure
 * @msix: boolean for MSI-X capability
 *
 * This function initializes the interrupts and allocates all of the queues.
 */
static int igc_init_interrupt_scheme(struct igc_adapter *adapter, bool msix)
{
	struct rtnet_device *dev = adapter->netdev;
	int err = 0;

	igc_set_interrupt_capability(adapter, msix);

	err = igc_alloc_q_vectors(adapter);
	if (err) {
		rtdev_err(dev, "Unable to allocate memory for vectors\n");
		goto err_alloc_q_vectors;
	}

	igc_cache_ring_register(adapter);

	return 0;

err_alloc_q_vectors:
	igc_reset_interrupt_capability(adapter);
	return err;
}

/**
 * igc_sw_init - Initialize general software structures (struct igc_adapter)
 * @adapter: board private structure to initialize
 *
 * igc_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 */
static int igc_sw_init(struct igc_adapter *adapter)
{
	struct rtnet_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	struct igc_hw *hw = &adapter->hw;

	pci_read_config_word(pdev, PCI_COMMAND, &hw->bus.pci_cmd_word);

	/* set default ring sizes */
	adapter->tx_ring_count = IGC_DEFAULT_TXD;
	adapter->rx_ring_count = IGC_DEFAULT_RXD;

	/* set default ITR values */
	if (InterruptThrottle) {
		adapter->rx_itr_setting = IGC_DEFAULT_ITR;
		adapter->tx_itr_setting = IGC_DEFAULT_ITR;
	} else {
		adapter->rx_itr_setting = IGC_MIN_ITR_USECS;
		adapter->tx_itr_setting = IGC_MIN_ITR_USECS;
	}


	/* set default work limits */
	adapter->tx_work_limit = IGC_DEFAULT_TX_WORK;

	/* adjust max frame to be at least the size of a standard frame */
	adapter->max_frame_size = netdev->mtu + ETH_HLEN + ETH_FCS_LEN +
				VLAN_HLEN;
	adapter->min_frame_size = ETH_ZLEN + ETH_FCS_LEN;

	mutex_init(&adapter->nfc_rule_lock);
	INIT_LIST_HEAD(&adapter->nfc_rule_list);
	adapter->nfc_rule_count = 0;

	spin_lock_init(&adapter->stats64_lock);
	/* Assume MSI-X interrupts, will be checked during IRQ allocation */
	adapter->flags |= IGC_FLAG_HAS_MSIX;

	igc_init_queue_configuration(adapter);

	/* This call may decrease the number of queues */
	if (igc_init_interrupt_scheme(adapter, true)) {
		rtdev_err(netdev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	/* Explicitly disable IRQ since the NIC can be in any state. */
	igc_irq_disable(adapter);

	set_bit(__IGC_DOWN, &adapter->state);

	return 0;
}

/**
 * igc_up - Open the interface and prepare it to handle traffic
 * @adapter: board private structure
 */
void igc_up(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	/* hardware has been reset, we need to reload some things */
	igc_configure(adapter);

	clear_bit(__IGC_DOWN, &adapter->state);

	if (adapter->msix_entries)
		igc_configure_msix(adapter);
	else
		igc_assign_vector(adapter->q_vector[0], 0);

	/* Clear any pending interrupts. */
	rd32(IGC_ICR);
	igc_irq_enable(adapter);

	rtnetif_start_queue(adapter->netdev);

	/* start the watchdog. */
	hw->mac.get_link_status = 1;
	schedule_work(&adapter->watchdog_task);
}

/**
 * igc_update_stats - Update the board statistics counters
 * @adapter: board private structure
 */
void igc_update_stats(struct igc_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct igc_hw *hw = &adapter->hw;
	struct net_device_stats *net_stats;
	u64 bytes, packets;
	u32 mpc;
	int i;

	/* Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pci_channel_offline(pdev))
		return;

	net_stats = &adapter->net_stats;
	packets = 0;
	bytes = 0;

	rcu_read_lock();
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct igc_ring *ring = adapter->rx_ring[i];
		u32 rqdpc = rd32(IGC_RQDPC(i));

		if (hw->mac.type >= igc_i225)
			wr32(IGC_RQDPC(i), 0);

		if (rqdpc)
			ring->rx_stats.drops += rqdpc;

		bytes += ring->rx_stats.bytes;
		packets += ring->rx_stats.packets;
	}

	net_stats->rx_bytes = bytes;
	net_stats->rx_packets = packets;

	packets = 0;
	bytes = 0;
	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *ring = adapter->tx_ring[i];

		bytes += ring->tx_stats.bytes;
		packets += ring->tx_stats.packets;
	}

	net_stats->tx_bytes = bytes;
	net_stats->tx_packets = packets;

	rcu_read_unlock();

	/* read stats registers */
	adapter->stats.crcerrs += rd32(IGC_CRCERRS);
	adapter->stats.gprc += rd32(IGC_GPRC);
	adapter->stats.gorc += rd32(IGC_GORCL);
	rd32(IGC_GORCH); /* clear GORCL */
	adapter->stats.bprc += rd32(IGC_BPRC);
	adapter->stats.mprc += rd32(IGC_MPRC);
	adapter->stats.roc += rd32(IGC_ROC);

	adapter->stats.prc64 += rd32(IGC_PRC64);
	adapter->stats.prc127 += rd32(IGC_PRC127);
	adapter->stats.prc255 += rd32(IGC_PRC255);
	adapter->stats.prc511 += rd32(IGC_PRC511);
	adapter->stats.prc1023 += rd32(IGC_PRC1023);
	adapter->stats.prc1522 += rd32(IGC_PRC1522);
	adapter->stats.tlpic += rd32(IGC_TLPIC);
	adapter->stats.rlpic += rd32(IGC_RLPIC);

	mpc = rd32(IGC_MPC);
	adapter->stats.mpc += mpc;
	net_stats->rx_fifo_errors += mpc;
	adapter->stats.scc += rd32(IGC_SCC);
	adapter->stats.ecol += rd32(IGC_ECOL);
	adapter->stats.mcc += rd32(IGC_MCC);
	adapter->stats.latecol += rd32(IGC_LATECOL);
	adapter->stats.dc += rd32(IGC_DC);
	adapter->stats.rlec += rd32(IGC_RLEC);
	adapter->stats.xonrxc += rd32(IGC_XONRXC);
	adapter->stats.xontxc += rd32(IGC_XONTXC);
	adapter->stats.xoffrxc += rd32(IGC_XOFFRXC);
	adapter->stats.xofftxc += rd32(IGC_XOFFTXC);
	adapter->stats.fcruc += rd32(IGC_FCRUC);
	adapter->stats.gptc += rd32(IGC_GPTC);
	adapter->stats.gotc += rd32(IGC_GOTCL);
	rd32(IGC_GOTCH); /* clear GOTCL */
	adapter->stats.rnbc += rd32(IGC_RNBC);
	adapter->stats.ruc += rd32(IGC_RUC);
	adapter->stats.rfc += rd32(IGC_RFC);
	adapter->stats.rjc += rd32(IGC_RJC);
	adapter->stats.tor += rd32(IGC_TORH);
	adapter->stats.tot += rd32(IGC_TOTH);
	adapter->stats.tpr += rd32(IGC_TPR);

	adapter->stats.ptc64 += rd32(IGC_PTC64);
	adapter->stats.ptc127 += rd32(IGC_PTC127);
	adapter->stats.ptc255 += rd32(IGC_PTC255);
	adapter->stats.ptc511 += rd32(IGC_PTC511);
	adapter->stats.ptc1023 += rd32(IGC_PTC1023);
	adapter->stats.ptc1522 += rd32(IGC_PTC1522);

	adapter->stats.mptc += rd32(IGC_MPTC);
	adapter->stats.bptc += rd32(IGC_BPTC);

	adapter->stats.tpt += rd32(IGC_TPT);
	adapter->stats.colc += rd32(IGC_COLC);
	adapter->stats.colc += rd32(IGC_RERC);

	adapter->stats.algnerrc += rd32(IGC_ALGNERRC);

	adapter->stats.tsctc += rd32(IGC_TSCTC);

	adapter->stats.iac += rd32(IGC_IAC);

	/* Fill out the OS statistics structure */
	net_stats->multicast = adapter->stats.mprc;
	net_stats->collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	 * our own version based on RUC and ROC
	 */
	net_stats->rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.ruc + adapter->stats.roc +
		adapter->stats.cexterr;
	net_stats->rx_length_errors = adapter->stats.ruc +
					adapter->stats.roc;
	net_stats->rx_crc_errors = adapter->stats.crcerrs;
	net_stats->rx_frame_errors = adapter->stats.algnerrc;
	net_stats->rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	net_stats->tx_errors = adapter->stats.ecol +
				adapter->stats.latecol;
	net_stats->tx_aborted_errors = adapter->stats.ecol;
	net_stats->tx_window_errors = adapter->stats.latecol;
	net_stats->tx_carrier_errors = adapter->stats.tncrs;


	/* Tx Dropped needs to be maintained elsewhere */

	/* Management Stats */
	adapter->stats.mgptc += rd32(IGC_MGTPTC);
	adapter->stats.mgprc += rd32(IGC_MGTPRC);
	adapter->stats.mgpdc += rd32(IGC_MGTPDC);
}

/**
 * igc_down - Close the interface
 * @adapter: board private structure
 */
void igc_down(struct igc_adapter *adapter)
{
	struct rtnet_device *netdev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;
	u32 tctl, rctl;

	set_bit(__IGC_DOWN, &adapter->state);

	/* disable receives in the hardware */
	rctl = rd32(IGC_RCTL);
	wr32(IGC_RCTL, rctl & ~IGC_RCTL_EN);

	rtnetif_stop_queue(netdev);

	/* disable transmits in the hardware */
	tctl = rd32(IGC_TCTL);
	tctl &= ~IGC_TCTL_EN;
	wr32(IGC_TCTL, tctl);
	/* flush both disables and wait for them to finish */
	wrfl();
	usleep_range(10000, 20000);

	igc_irq_disable(adapter);

	adapter->flags &= ~IGC_FLAG_NEED_LINK_UPDATE;

	timer_delete_sync(&adapter->watchdog_timer);
	timer_delete_sync(&adapter->phy_info_timer);

	/* record the stats before reset*/
	spin_lock(&adapter->stats64_lock);
	igc_update_stats(adapter);
	spin_unlock(&adapter->stats64_lock);

	rtnetif_carrier_off(netdev);

	adapter->link_speed = 0;
	adapter->link_duplex = 0;

	if (!pci_channel_offline(adapter->pdev))
		igc_reset(adapter);

	/* clear VLAN promisc flag so VFTA will be updated if necessary */
	adapter->flags &= ~IGC_FLAG_VLAN_PROMISC;

	igc_clean_all_tx_rings(adapter);
	igc_clean_all_rx_rings(adapter);
}

void igc_reinit_locked(struct igc_adapter *adapter)
{
	while (test_and_set_bit(__IGC_RESETTING, &adapter->state))
		usleep_range(1000, 2000);
	igc_down(adapter);
	igc_up(adapter);
	clear_bit(__IGC_RESETTING, &adapter->state);
}

static void igc_reset_task(struct work_struct *work)
{
	struct igc_adapter *adapter;

	adapter = container_of(work, struct igc_adapter, reset_task);

	rtnl_lock();
	/* If we're already down or resetting, just bail */
	if (test_bit(__IGC_DOWN, &adapter->state) ||
	    test_bit(__IGC_RESETTING, &adapter->state)) {
		rtnl_unlock();
		return;
	}

	igc_rings_dump(adapter);
	igc_regs_dump(adapter);
	rtdev_err(adapter->netdev, "Reset adapter\n");
	igc_reinit_locked(adapter);
	rtnl_unlock();
}

static void igc_other_handler(struct igc_adapter *adapter, u32 icr, bool root)
{
	struct igc_hw *hw = &adapter->hw;

	/* reading ICR causes bit 31 of EICR to be cleared */
	if (icr & IGC_ICR_DRSTA)
		rtdm_schedule_nrt_work(&adapter->reset_task);

	if (icr & IGC_ICR_DOUTSYNC) {
		/* HW is reporting DMA is out of sync */
		adapter->stats.doosync++;
	}

	if (icr & IGC_ICR_LSC) {
		hw->mac.get_link_status = 1;
		/* guard against interrupt when we're going down */
		if (!test_bit(__IGC_DOWN, &adapter->state)) {
			if (root)
				mod_timer(&adapter->watchdog_timer, jiffies + 1);
			else
				rtdm_nrtsig_pend(&adapter->watchdog_nrtsig);
		}
	}

}

/**
 * igc_msix_other - msix other interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 */
static irqreturn_t igc_msix_other(int irq, void *data)
{
	struct igc_adapter *adapter = data;
	struct igc_hw *hw = &adapter->hw;
	u32 icr = rd32(IGC_ICR);

	igc_other_handler(adapter, icr, true);

	wr32(IGC_EIMS, adapter->eims_other);

	return IRQ_HANDLED;
}

static void igc_write_itr(struct igc_q_vector *q_vector)
{
	u32 itr_val = q_vector->itr_val & IGC_QVECTOR_MASK;

	if (!q_vector->set_itr)
		return;

	if (!itr_val)
		itr_val = IGC_ITR_VAL_MASK;

	itr_val |= IGC_EITR_CNT_IGNR;

	writel(itr_val, q_vector->itr_register);
	q_vector->set_itr = 0;
}


/**
 *  igc_poll - NAPI Rx polling callback
 *  @napi: napi polling structure
 *  @budget: count of how many packets we should handle
 **/
static void igc_poll(struct igc_q_vector *q_vector)
{
	if (q_vector->tx.ring)
		igc_clean_tx_irq(q_vector);

	if (q_vector->rx.ring)
		igc_clean_rx_irq(q_vector, 64);

	igc_ring_irq_enable(q_vector);
}

static int igc_msix_ring(rtdm_irq_t *ih)
{
	struct igc_q_vector *q_vector =
		rtdm_irq_get_arg(ih, struct igc_q_vector);

	/* Write the ITR value calculated from the previous interrupt. */
	igc_write_itr(q_vector);

	igc_poll(q_vector);

	return RTDM_IRQ_HANDLED;
}

/**
 * igc_request_msix - Initialize MSI-X interrupts
 * @adapter: Pointer to adapter structure
 *
 * igc_request_msix allocates MSI-X vectors and requests interrupts from the
 * kernel.
 */
static int igc_request_msix(struct igc_adapter *adapter)
{
	int i = 0, err = 0, vector = 0, free_vector = 0;
	struct rtnet_device *netdev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;

	err = request_irq(adapter->msix_entries[vector].vector,
			  &igc_msix_other, 0, netdev->name, adapter);
	if (err)
		goto err_out;

	for (i = 0; i < adapter->num_q_vectors; i++) {
		struct igc_q_vector *q_vector = adapter->q_vector[i];

		vector++;

		q_vector->itr_register = hw->hw_addr  + IGC_EITR(vector);

		if (q_vector->rx.ring && q_vector->tx.ring)
			sprintf(q_vector->name, "%s-TxRx-%u", netdev->name,
				q_vector->rx.ring->queue_index);
		else if (q_vector->tx.ring)
			sprintf(q_vector->name, "%s-tx-%u", netdev->name,
				q_vector->tx.ring->queue_index);
		else if (q_vector->rx.ring)
			sprintf(q_vector->name, "%s-rx-%u", netdev->name,
				q_vector->rx.ring->queue_index);
		else
			sprintf(q_vector->name, "%s-unused", netdev->name);

		err = rtdm_irq_request(&adapter->msix_irq_handle[vector],
				adapter->msix_entries[vector].vector,
				igc_msix_ring, 0, q_vector->name,
				q_vector);
		if (err)
			goto err_free;
	}

	igc_configure_msix(adapter);
	return 0;

err_free:
	/* free already assigned IRQs */
	free_irq(adapter->msix_entries[free_vector++].vector, adapter);

	vector--;
	for (i = 0; i < vector; i++)
		rtdm_irq_free(&adapter->msix_irq_handle[free_vector++]);
err_out:
	return err;
}

/**
 * igc_clear_interrupt_scheme - reset the device to a state of no interrupts
 * @adapter: Pointer to adapter structure
 *
 * This function resets the device so that it has 0 rx queues, tx queues, and
 * MSI-X interrupts allocated.
 */
static void igc_clear_interrupt_scheme(struct igc_adapter *adapter)
{
	igc_free_q_vectors(adapter);
	igc_reset_interrupt_capability(adapter);
}

/* Need to wait a few seconds after link up to get diagnostic information from
 * the phy
 */
static void igc_update_phy_info(struct timer_list *t)
{
	struct igc_adapter *adapter = from_timer(adapter, t, phy_info_timer);

	igc_get_phy_info(&adapter->hw);
}

/**
 * igc_has_link - check shared code for link and determine up/down
 * @adapter: pointer to driver private info
 */
bool igc_has_link(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	bool link_active = false;

	/* get_link_status is set on LSC (link status) interrupt or
	 * rx sequence error interrupt.  get_link_status will stay
	 * false until the igc_check_for_link establishes link
	 * for copper adapters ONLY
	 */
	switch (hw->phy.media_type) {
	case igc_media_type_copper:
		if (!hw->mac.get_link_status)
			return true;
		hw->mac.ops.check_for_link(hw);
		link_active = !hw->mac.get_link_status;
		break;
	default:
	case igc_media_type_unknown:
		break;
	}

	if (hw->mac.type == igc_i225) {
		if (!rtnetif_carrier_ok(adapter->netdev)) {
			adapter->flags &= ~IGC_FLAG_NEED_LINK_UPDATE;
		} else if (!(adapter->flags & IGC_FLAG_NEED_LINK_UPDATE)) {
			adapter->flags |= IGC_FLAG_NEED_LINK_UPDATE;
			adapter->link_check_timeout = jiffies;
		}
	}

	return link_active;
}

/**
 * igc_watchdog - Timer Call-back
 * @t: timer for the watchdog
 */
static void igc_watchdog(struct timer_list *t)
{
	struct igc_adapter *adapter = from_timer(adapter, t, watchdog_timer);
	/* Do the rest outside of interrupt context */
	schedule_work(&adapter->watchdog_task);
}

static void igc_watchdog_task(struct work_struct *work)
{
	struct igc_adapter *adapter = container_of(work,
						   struct igc_adapter,
						   watchdog_task);
	struct rtnet_device *netdev = adapter->netdev;
	struct igc_hw *hw = &adapter->hw;
	struct igc_phy_info *phy = &hw->phy;
	u32 link;
	int i;

	link = igc_has_link(adapter);

	if (adapter->flags & IGC_FLAG_NEED_LINK_UPDATE) {
		if (time_after(jiffies, (adapter->link_check_timeout + HZ)))
			adapter->flags &= ~IGC_FLAG_NEED_LINK_UPDATE;
		else
			link = false;
	}

	if (link) {
		/* Cancel scheduled suspend requests. */
		pm_runtime_resume(adapter->pdev->dev.parent);

		if (!rtnetif_carrier_ok(netdev)) {
			u32 ctrl;

			hw->mac.ops.get_speed_and_duplex(hw,
							 &adapter->link_speed,
							 &adapter->link_duplex);

			ctrl = rd32(IGC_CTRL);
			/* Link status message must follow this format */
			rtdev_info(netdev,
				    "NIC Link is Up %d Mbps %s Duplex, Flow Control: %s\n",
				    adapter->link_speed,
				    adapter->link_duplex == FULL_DUPLEX ?
				    "Full" : "Half",
				    (ctrl & IGC_CTRL_TFCE) &&
				    (ctrl & IGC_CTRL_RFCE) ? "RX/TX" :
				    (ctrl & IGC_CTRL_RFCE) ?  "RX" :
				    (ctrl & IGC_CTRL_TFCE) ?  "TX" : "None");

			/* disable EEE if enabled */
			if ((adapter->flags & IGC_FLAG_EEE) &&
			    adapter->link_duplex == HALF_DUPLEX) {
				rtdev_info(netdev,
					    "EEE Disabled: unsupported at half duplex. Re-enable using ethtool when at full duplex\n");
				adapter->hw.dev_spec._base.eee_enable = false;
				adapter->flags &= ~IGC_FLAG_EEE;
			}

			/* check if SmartSpeed worked */
			igc_check_downshift(hw);
			if (phy->speed_downgraded)
				rtdev_warn(netdev, "Link Speed was downgraded by SmartSpeed\n");

			/* adjust timeout factor according to speed/duplex */
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				adapter->tx_timeout_factor = 14;
				break;
			case SPEED_100:
				/* maybe add some timeout factor ? */
				break;
			}

			rtnetif_carrier_on(netdev);

			/* link state has changed, schedule phy info update */
			if (!test_bit(__IGC_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));
		}
	} else {
		if (rtnetif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;

			/* Links status message must follow this format */
			rtdev_info(netdev, "NIC Link is Down\n");
			rtnetif_carrier_off(netdev);

			/* link state has changed, schedule phy info update */
			if (!test_bit(__IGC_DOWN, &adapter->state))
				mod_timer(&adapter->phy_info_timer,
					  round_jiffies(jiffies + 2 * HZ));

			/* link is down, time to check for alternate media */
			if (adapter->flags & IGC_FLAG_MAS_ENABLE) {
				if (adapter->flags & IGC_FLAG_MEDIA_RESET) {
					schedule_work(&adapter->reset_task);
					/* return immediately */
					return;
				}
			}
			pm_schedule_suspend(adapter->pdev->dev.parent,
					    MSEC_PER_SEC * 5);

		/* also check for alternate media here */
		} else if (!rtnetif_carrier_ok(netdev) &&
			   (adapter->flags & IGC_FLAG_MAS_ENABLE)) {
			if (adapter->flags & IGC_FLAG_MEDIA_RESET) {
				schedule_work(&adapter->reset_task);
				/* return immediately */
				return;
			}
		}
	}

	spin_lock(&adapter->stats64_lock);
	igc_update_stats(adapter);
	spin_unlock(&adapter->stats64_lock);

	for (i = 0; i < adapter->num_tx_queues; i++) {
		struct igc_ring *tx_ring = adapter->tx_ring[i];

		if (!rtnetif_carrier_ok(netdev)) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context).
			 */
			if (igc_desc_unused(tx_ring) + 1 < tx_ring->count) {
				adapter->tx_timeout_count++;
				schedule_work(&adapter->reset_task);
				/* return immediately since reset is imminent */
				return;
			}
		}

		/* Force detection of hung controller every watchdog period */
		set_bit(IGC_RING_FLAG_TX_DETECT_HANG, &tx_ring->flags);
	}

	/* Cause software interrupt to ensure Rx ring is cleaned */
	if (adapter->flags & IGC_FLAG_HAS_MSIX) {
		u32 eics = 0;

		for (i = 0; i < adapter->num_q_vectors; i++)
			eics |= adapter->q_vector[i]->eims_value;
		wr32(IGC_EICS, eics);
	} else {
		wr32(IGC_ICS, IGC_ICS_RXDMT0);
	}

	/* Reset the timer */
	if (!test_bit(__IGC_DOWN, &adapter->state)) {
		if (adapter->flags & IGC_FLAG_NEED_LINK_UPDATE)
			mod_timer(&adapter->watchdog_timer,
				  round_jiffies(jiffies +  HZ));
		else
			mod_timer(&adapter->watchdog_timer,
				  round_jiffies(jiffies + 2 * HZ));
	}
}

/**
 * igc_intr_msi - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 */
static int igc_intr_msi(rtdm_irq_t *ih)
{
	struct igc_adapter *adapter = rtdm_irq_get_arg(ih, struct igc_adapter);
	struct igc_q_vector *q_vector = adapter->q_vector[0];
	struct igc_hw *hw = &adapter->hw;
	/* read ICR disables interrupts using IAM */
	u32 icr = rd32(IGC_ICR);

	igc_write_itr(q_vector);

	igc_other_handler(adapter, icr, false);

	igc_poll(q_vector);

	return RTDM_IRQ_HANDLED;
}

/**
 * igc_intr - Legacy Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 */
static int igc_intr(rtdm_irq_t *ih)
{
	struct igc_adapter *adapter = rtdm_irq_get_arg(ih, struct igc_adapter);
	struct igc_q_vector *q_vector = adapter->q_vector[0];
	struct igc_hw *hw = &adapter->hw;
	/* Interrupt Auto-Mask...upon reading ICR, interrupts are masked.  No
	 * need for the IMC write
	 */
	u32 icr = rd32(IGC_ICR);

	/* IMS will not auto-mask if INT_ASSERTED is not set, and if it is
	 * not set, then the adapter didn't send an interrupt
	 */
	if (!(icr & IGC_ICR_INT_ASSERTED))
		return IRQ_NONE;

	igc_write_itr(q_vector);


	igc_other_handler(adapter, icr, false);

	igc_poll(q_vector);

	return RTDM_IRQ_HANDLED;
}

static void igc_free_irq(struct igc_adapter *adapter)
{
	if (adapter->msix_entries) {
		int vector = 0, i;

		free_irq(adapter->msix_entries[vector++].vector, adapter);

		for (i = 0; i < adapter->num_q_vectors; i++)
			rtdm_irq_free(&adapter->msix_irq_handle[vector++]);
	} else
		rtdm_irq_free(&adapter->irq_handle);

}

/**
 * igc_request_irq - initialize interrupts
 * @adapter: Pointer to adapter structure
 *
 * Attempts to configure interrupts using the best available
 * capabilities of the hardware and kernel.
 */
static int igc_request_irq(struct igc_adapter *adapter)
{
	struct rtnet_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;
	int err = 0;

	rt_stack_connect(netdev, &STACK_manager);

	if (adapter->flags & IGC_FLAG_HAS_MSIX) {
		err = igc_request_msix(adapter);
		if (!err)
			goto request_done;
		/* fall back to MSI */
		igc_free_all_tx_resources(adapter);
		igc_free_all_rx_resources(adapter);

		igc_clear_interrupt_scheme(adapter);
		err = igc_init_interrupt_scheme(adapter, false);
		if (err)
			goto request_done;
		igc_setup_all_tx_resources(adapter);
		igc_setup_all_rx_resources(adapter);
		igc_configure(adapter);
	}

	igc_assign_vector(adapter->q_vector[0], 0);

	if (adapter->flags & IGC_FLAG_HAS_MSI) {
		err = rtdm_irq_request(&adapter->irq_handle,
				pdev->irq, &igc_intr_msi, 0,
				netdev->name, adapter);
		if (!err)
			goto request_done;

		/* fall back to legacy interrupts */
		igc_reset_interrupt_capability(adapter);
		adapter->flags &= ~IGC_FLAG_HAS_MSI;
	}

	err = rtdm_irq_request(&adapter->irq_handle,
			pdev->irq, &igc_intr, IRQF_SHARED,
			netdev->name, adapter);

	if (err)
		rtdev_err(netdev, "Error %d getting interrupt\n", err);

request_done:
	return err;
}

/**
 * __igc_open - Called when a network interface is made active
 * @netdev: network interface device structure
 * @resuming: boolean indicating if the device is resuming
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 */
static int __igc_open(struct rtnet_device *netdev, bool resuming)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;
	struct igc_hw *hw = &adapter->hw;
	int err = 0;

	/* disallow open during test */

	if (test_bit(__IGC_TESTING, &adapter->state)) {
		WARN_ON(resuming);
		return -EBUSY;
	}

	if (!resuming)
		pm_runtime_get_sync(&pdev->dev);

	rtnetif_carrier_off(netdev);

	/* allocate transmit descriptors */
	err = igc_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = igc_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	igc_power_up_link(adapter);

	igc_configure(adapter);

	err = igc_request_irq(adapter);
	if (err)
		goto err_req_irq;

	clear_bit(__IGC_DOWN, &adapter->state);

	/* Clear any pending interrupts. */
	rd32(IGC_ICR);
	igc_irq_enable(adapter);

	if (!resuming)
		pm_runtime_put(&pdev->dev);

	//netif_tx_start_all_queues(netdev);
	rtnetif_start_queue(netdev);

	/* start the watchdog. */
	hw->mac.get_link_status = 1;
	schedule_work(&adapter->watchdog_task);

	return IGC_SUCCESS;

err_req_irq:
	igc_release_hw_control(adapter);
	igc_power_down_phy_copper_base(&adapter->hw);
	igc_free_all_rx_resources(adapter);
err_setup_rx:
	igc_free_all_tx_resources(adapter);
err_setup_tx:
	igc_reset(adapter);
	if (!resuming)
		pm_runtime_put(&pdev->dev);

	return err;
}

int igc_open(struct rtnet_device *netdev)
{
	return __igc_open(netdev, false);
}

/**
 * __igc_close - Disables a network interface
 * @netdev: network interface device structure
 * @suspending: boolean indicating the device is suspending
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the driver's control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 */
static int __igc_close(struct rtnet_device *netdev, bool suspending)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct pci_dev *pdev = adapter->pdev;

	WARN_ON(test_bit(__IGC_RESETTING, &adapter->state));

	if (!suspending)
		pm_runtime_get_sync(&pdev->dev);

	igc_down(adapter);

	igc_release_hw_control(adapter);

	igc_free_irq(adapter);

	rt_stack_disconnect(netdev);

	igc_free_all_tx_resources(adapter);
	igc_free_all_rx_resources(adapter);

	if (!suspending)
		pm_runtime_put_sync(&pdev->dev);

	return 0;
}

int igc_close(struct rtnet_device *netdev)
{
	return __igc_close(netdev, false);
}

static void igc_ptp_disable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	wr32(IGC_TSYNCTXCTL, 0);
}

static void igc_ptp_enable_tx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;

	wr32(IGC_TSYNCTXCTL, IGC_TSYNCTXCTL_ENABLED | IGC_TSYNCTXCTL_TXSYNSIG);

	/* Read TXSTMP registers to discard any timestamp previously stored. */
	rd32(IGC_TXSTMPL);
	rd32(IGC_TXSTMPH);
}

static void igc_ptp_disable_rx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 val;
	int i;

	wr32(IGC_TSYNCRXCTL, 0);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		val = rd32(IGC_SRRCTL(i));
		val &= ~IGC_SRRCTL_TIMESTAMP;
		wr32(IGC_SRRCTL(i), val);
	}

	val = rd32(IGC_RXPBS);
	val &= ~IGC_RXPBS_CFG_TS_EN;
	wr32(IGC_RXPBS, val);
}

static void igc_ptp_enable_rx_timestamp(struct igc_adapter *adapter)
{
	struct igc_hw *hw = &adapter->hw;
	u32 val;
	int i;

	val = rd32(IGC_RXPBS);
	val |= IGC_RXPBS_CFG_TS_EN;
	wr32(IGC_RXPBS, val);

	for (i = 0; i < adapter->num_rx_queues; i++) {
		val = rd32(IGC_SRRCTL(i));
		/* FIXME: For now, only support retrieving RX timestamps from
		 * timer 0.
		 */
		val |= IGC_SRRCTL_TIMER1SEL(0) | IGC_SRRCTL_TIMER0SEL(0) |
		       IGC_SRRCTL_TIMESTAMP;
		wr32(IGC_SRRCTL(i), val);
	}

	val = IGC_TSYNCRXCTL_ENABLED | IGC_TSYNCRXCTL_TYPE_ALL |
	      IGC_TSYNCRXCTL_RXSYNSIG;
	wr32(IGC_TSYNCRXCTL, val);
}

/**
 * igc_ptp_set_timestamp_mode - setup hardware for timestamping
 * @adapter: networking device structure
 * @config: hwtstamp configuration
 *
 * Return: 0 in case of success, negative errno code otherwise.
 */
static int igc_ptp_set_timestamp_mode(struct igc_adapter *adapter,
				      struct hwtstamp_config *config)
{
	switch (config->tx_type) {
	case HWTSTAMP_TX_OFF:
		igc_ptp_disable_tx_timestamp(adapter);
		break;
	case HWTSTAMP_TX_ON:
		igc_ptp_enable_tx_timestamp(adapter);
		break;
	default:
		return -ERANGE;
	}

	switch (config->rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		igc_ptp_disable_rx_timestamp(adapter);
		break;
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_NTP_ALL:
	case HWTSTAMP_FILTER_ALL:
		igc_ptp_enable_rx_timestamp(adapter);
		config->rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	return 0;
}

/**
 * igc_ptp_set_ts_config - set hardware time stamping config
 * @netdev: network interface device structure
 * @ifr: interface request data
 *
 **/
static int igc_ptp_set_ts_config(struct rtnet_device *netdev, struct ifreq *ifr)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct hwtstamp_config config;
	int err;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	err = igc_ptp_set_timestamp_mode(adapter, &config);
	if (err)
		return err;

	/* save these settings for future reference */
	memcpy(&adapter->tstamp_config, &config,
	       sizeof(adapter->tstamp_config));

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

/**
 * igc_ptp_get_ts_config - get hardware time stamping config
 * @netdev: network interface device structure
 * @ifr: interface request data
 *
 * Get the hwtstamp_config settings to return to the user. Rather than attempt
 * to deconstruct the settings from the registers, just return a shadow copy
 * of the last known settings.
 **/
static int igc_ptp_get_ts_config(struct rtnet_device *netdev, struct ifreq *ifr)
{
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct hwtstamp_config *config = &adapter->tstamp_config;

	return copy_to_user(ifr->ifr_data, config, sizeof(*config)) ?
		-EFAULT : 0;
}

/**
 * igc_ioctl - Access the hwtstamp interface
 * @netdev: network interface device structure
 * @ifr: interface request data
 * @cmd: ioctl command
 **/
static int igc_ioctl(struct rtnet_device *netdev, struct ifreq *ifr, int cmd)
{
	if (rtdm_in_rt_context())
		return -ENOSYS;

	switch (cmd) {
	case SIOCGHWTSTAMP:
		return igc_ptp_get_ts_config(netdev, ifr);
	case SIOCSHWTSTAMP:
		return igc_ptp_set_ts_config(netdev, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

/* PCIe configuration access */
void igc_read_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	pci_read_config_word(adapter->pdev, reg, value);
}

void igc_write_pci_cfg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	pci_write_config_word(adapter->pdev, reg, *value);
}

s32 igc_read_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	if (!pci_is_pcie(adapter->pdev))
		return -IGC_ERR_CONFIG;

	pcie_capability_read_word(adapter->pdev, reg, value);

	return IGC_SUCCESS;
}

s32 igc_write_pcie_cap_reg(struct igc_hw *hw, u32 reg, u16 *value)
{
	struct igc_adapter *adapter = hw->back;

	if (!pci_is_pcie(adapter->pdev))
		return -IGC_ERR_CONFIG;

	pcie_capability_write_word(adapter->pdev, reg, *value);

	return IGC_SUCCESS;
}

u32 igc_rd32(struct igc_hw *hw, u32 reg)
{
	struct igc_adapter *igc = container_of(hw, struct igc_adapter, hw);
	u8 __iomem *hw_addr = READ_ONCE(hw->hw_addr);
	u32 value = 0;

	value = readl(&hw_addr[reg]);

	/* reads should not return all F's */
	if (!(~value) && (!reg || !(~readl(hw_addr)))) {
		struct rtnet_device *netdev = igc->netdev;

		hw->hw_addr = NULL;
		rtnetif_device_detach(netdev);
		rtdev_err(netdev, "PCIe link lost, device now detached\n");
	}

	return value;
}

int igc_set_spd_dplx(struct igc_adapter *adapter, u32 spd, u8 dplx)
{
	struct igc_mac_info *mac = &adapter->hw.mac;

	mac->autoneg = 0;

	/* Make sure dplx is at most 1 bit and lsb of speed is not set
	 * for the switch() below to work
	 */
	if ((spd & 1) || (dplx & ~1))
		goto err_inval;

	switch (spd + dplx) {
	case SPEED_10 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_10_HALF;
		break;
	case SPEED_10 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_10_FULL;
		break;
	case SPEED_100 + DUPLEX_HALF:
		mac->forced_speed_duplex = ADVERTISE_100_HALF;
		break;
	case SPEED_100 + DUPLEX_FULL:
		mac->forced_speed_duplex = ADVERTISE_100_FULL;
		break;
	case SPEED_1000 + DUPLEX_FULL:
		mac->autoneg = 1;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + DUPLEX_HALF: /* not supported */
		goto err_inval;
	case SPEED_2500 + DUPLEX_FULL:
		mac->autoneg = 1;
		adapter->hw.phy.autoneg_advertised = ADVERTISE_2500_FULL;
		break;
	case SPEED_2500 + DUPLEX_HALF: /* not supported */
	default:
		goto err_inval;
	}

	/* clear MDI, MDI(-X) override is only allowed when autoneg enabled */
	adapter->hw.phy.mdix = AUTO_ALL_MODES;

	return 0;

err_inval:
	rtdev_err(adapter->netdev, "Unsupported Speed/Duplex configuration\n");
	return -EINVAL;
}

static void igc_nrtsig_watchdog(rtdm_nrtsig_t *sig, void *data)
{
	struct igc_adapter *adapter = data;

	mod_timer(&adapter->watchdog_timer, jiffies + 1);
}

/**
 * igc_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/
static struct net_device_stats *igc_get_stats(struct rtnet_device *netdev)
{
	struct igc_adapter *adapter = netdev->priv;

	/* only return the current stats */
	return &adapter->net_stats;
}

static dma_addr_t igc_map_rtskb(struct rtnet_device *netdev,
				struct rtskb *skb)
{
	struct igc_adapter *adapter = netdev->priv;
	struct device *dev = &adapter->pdev->dev;
	dma_addr_t addr;

	addr = dma_map_single(dev, skb->buf_start, RTSKB_SIZE,
			      DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, addr)) {
		dev_err(dev, "DMA map failed\n");
		return RTSKB_UNMAPPED;
	}
	return addr;
}

static void igc_unmap_rtskb(struct rtnet_device *netdev,
			      struct rtskb *skb)
{
	struct igc_adapter *adapter = netdev->priv;
	struct device *dev = &adapter->pdev->dev;

	dma_unmap_single(dev, skb->buf_dma_addr, RTSKB_SIZE,
			 DMA_BIDIRECTIONAL);
}

/**
 * igc_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in igc_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * igc_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring the adapter private structure,
 * and a hardware reset occur.
 */
static int igc_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	struct igc_adapter *adapter;
	struct rtnet_device *netdev;
	struct igc_hw *hw;
	const struct igc_info *ei = igc_info_tbl[ent->driver_data];
	int err, pci_using_dac;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	pci_using_dac = 0;
	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (!err) {
		pci_using_dac = 1;
	} else {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"No usable DMA configuration, aborting\n");
			goto err_dma;
		}
	}

	err = pci_request_mem_regions(pdev, igc_driver_name);
	if (err)
		goto err_pci_reg;

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,0,0)
	pci_enable_pcie_error_reporting(pdev);
#endif

	pci_set_master(pdev);

	err = -ENOMEM;
	netdev = rt_alloc_etherdev(sizeof(*adapter),
				2 * IGC_DEFAULT_RXD + IGC_DEFAULT_TXD);

	if (!netdev)
		goto err_alloc_etherdev;

	rtdev_alloc_name(netdev, "rteth%d");
	rt_rtdev_connect(netdev, &RTDEV_manager);

	netdev->vers = RTDEV_VERS_2_0;
	netdev->sysbind = &pdev->dev;

	pci_set_drvdata(pdev, netdev);
	adapter = rtnetdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	hw->back = adapter;
	adapter->port_num = hw->bus.func;
	adapter->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	err = pci_save_state(pdev);
	if (err)
		goto err_ioremap;

	err = -EIO;
	hw->hw_addr = ioremap(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
	if (!hw->hw_addr)
		goto err_ioremap;

	netdev->open = igc_open;
	netdev->stop = igc_close;
	netdev->hard_start_xmit = igc_xmit_frame;
	netdev->get_stats = igc_get_stats;
	netdev->map_rtskb = igc_map_rtskb;
	netdev->unmap_rtskb = igc_unmap_rtskb;
	netdev->do_ioctl = igc_ioctl;

	netdev->mem_start = pci_resource_start(pdev, 0);
	netdev->mem_end = pci_resource_end(pdev, 0);

	/* PCI config space info */
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->revision_id = pdev->revision;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;

	/* Copy the default MAC and PHY function pointers */
	memcpy(&hw->mac.ops, ei->mac_ops, sizeof(hw->mac.ops));
	memcpy(&hw->phy.ops, ei->phy_ops, sizeof(hw->phy.ops));

	/* Initialize skew-specific constants */
	err = ei->get_invariants(hw);
	if (err)
		goto err_sw_init;

	/* Add supported features to the features list*/
	netdev->features |= NETIF_F_SG;
	netdev->features |= NETIF_F_TSO;
	netdev->features |= NETIF_F_TSO6;
	netdev->features |= NETIF_F_TSO_ECN;
	netdev->features |= NETIF_F_RXCSUM;
	netdev->features |= NETIF_F_SCTP_CRC;
	netdev->features |= NETIF_F_HW_TC;
	netdev->features |= NETIF_F_IP_CSUM;
	netdev->features |= NETIF_F_IPV6_CSUM;

	netdev->priv_flags |= IFF_SUPP_NOFCS;
	/* setup the private structure */
	err = igc_sw_init(adapter);
	if (err)
		goto err_sw_init;

	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	/* before reading the NVM, reset the controller to put the device in a
	 * known good starting state
	 */
	hw->mac.ops.reset_hw(hw);

	if (igc_get_flash_presence_i225(hw)) {
		if (hw->nvm.ops.validate(hw) < 0) {
			dev_err(&pdev->dev, "The NVM Checksum Is Not Valid\n");
			err = -EIO;
			goto err_eeprom;
		}
	}

	/* copy the MAC address out of the NVM */
	if (hw->mac.ops.read_mac_addr(hw))
		dev_err(&pdev->dev, "NVM Read Error\n");

	memcpy(netdev->dev_addr, hw->mac.addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		dev_err(&pdev->dev, "Invalid MAC Address\n");
		err = -EIO;
		goto err_eeprom;
	}

	/* configure RXPBSIZE and TXPBSIZE */
	wr32(IGC_RXPBS, I225_RXPBSIZE_DEFAULT);
	wr32(IGC_TXPBS, I225_TXPBSIZE_DEFAULT);

	timer_setup(&adapter->watchdog_timer, igc_watchdog, 0);
	timer_setup(&adapter->phy_info_timer, igc_update_phy_info, 0);

	INIT_WORK(&adapter->reset_task, igc_reset_task);
	INIT_WORK(&adapter->watchdog_task, igc_watchdog_task);
	rtdm_nrtsig_init(&adapter->watchdog_nrtsig,
			igc_nrtsig_watchdog, adapter);

	/* Initialize link properties that are user-changeable */
	adapter->fc_autoneg = true;
	hw->mac.autoneg = true;
	hw->phy.autoneg_advertised = 0xaf;

	hw->fc.requested_mode = igc_fc_default;
	hw->fc.current_mode = igc_fc_default;

	/* By default, support wake on port A */
	adapter->flags |= IGC_FLAG_WOL_SUPPORTED;

	/* initialize the wol settings based on the eeprom settings */
	if (adapter->flags & IGC_FLAG_WOL_SUPPORTED)
		adapter->wol |= IGC_WUFC_MAG;

	device_set_wakeup_enable(&adapter->pdev->dev,
				 adapter->flags & IGC_FLAG_WOL_SUPPORTED);

	/* reset the hardware with the new settings */
	igc_reset(adapter);

	/* let the f/w know that the h/w is now under the control of the
	 * driver.
	 */
	igc_get_hw_control(adapter);

	strncpy(netdev->name, "rteth%d", IFNAMSIZ);
	err = rt_register_rtnetdev(netdev);
	if (err)
		goto err_register;

	 /* carrier off reporting is important to ethtool even BEFORE open */
	rtnetif_carrier_off(netdev);

	/* Check if Media Autosense is enabled */
	adapter->ei = *ei;

	/* print pcie link status and MAC address */
	pcie_print_link_status(pdev);
	rtdev_info(netdev, "MAC: %pM\n", netdev->dev_addr);

	//dev_pm_set_driver_flags(&pdev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);
	/* Disable EEE for internal PHY devices */
	hw->dev_spec._base.eee_enable = false;
	adapter->flags &= ~IGC_FLAG_EEE;
	igc_set_eee_i225(hw, false, false, false);

	pm_runtime_put_noidle(&pdev->dev);

	//igc_led_setup(adapter);

	return 0;

err_register:
	igc_release_hw_control(adapter);
err_eeprom:
	if (!igc_check_reset_block(hw))
		igc_reset_phy(hw);
err_sw_init:
	igc_clear_interrupt_scheme(adapter);
	iounmap(hw->hw_addr);
err_ioremap:
	rtdev_free(netdev);
err_alloc_etherdev:
	pci_release_mem_regions(pdev);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * igc_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * igc_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 */
static void igc_remove(struct pci_dev *pdev)
{
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;

	rtdev_down(netdev);
	igc_down(adapter);

	pm_runtime_get_noresume(&pdev->dev);

	timer_delete_sync(&adapter->watchdog_timer);
	timer_delete_sync(&adapter->phy_info_timer);

	cancel_work_sync(&adapter->reset_task);
	cancel_work_sync(&adapter->watchdog_task);

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant.
	 */
	igc_release_hw_control(adapter);
	rt_rtdev_disconnect(netdev);
	rt_unregister_rtnetdev(netdev);

	igc_clear_interrupt_scheme(adapter);
	pci_iounmap(pdev, hw->hw_addr);
	pci_release_mem_regions(pdev);

	rtdev_free(netdev);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(6,0,0)
	pci_disable_pcie_error_reporting(pdev);
#endif

	pci_disable_device(pdev);
}

static int __igc_shutdown(struct pci_dev *pdev, bool *enable_wake,
			  bool runtime)
{
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	u32 wufc = runtime ? IGC_WUFC_LNKC : adapter->wol;
	struct igc_hw *hw = &adapter->hw;
	u32 ctrl, rctl, status;
	bool wake;

	rtnetif_device_detach(netdev);

	if (rtnetif_running(netdev))
		__igc_close(netdev, true);

	igc_clear_interrupt_scheme(adapter);

	status = rd32(IGC_STATUS);
	if (status & IGC_STATUS_LU)
		wufc &= ~IGC_WUFC_LNKC;

	if (wufc) {
		igc_setup_rctl(adapter);
		igc_set_rx_mode(netdev);

		/* turn on all-multi mode if wake on multicast is enabled */
		if (wufc & IGC_WUFC_MC) {
			rctl = rd32(IGC_RCTL);
			rctl |= IGC_RCTL_MPE;
			wr32(IGC_RCTL, rctl);
		}

		ctrl = rd32(IGC_CTRL);
		ctrl |= IGC_CTRL_ADVD3WUC;
		wr32(IGC_CTRL, ctrl);

		/* Allow time for pending master requests to run */
		igc_disable_pcie_master(hw);

		wr32(IGC_WUC, IGC_WUC_PME_EN);
		wr32(IGC_WUFC, wufc);
	} else {
		wr32(IGC_WUC, 0);
		wr32(IGC_WUFC, 0);
	}

	wake = wufc || adapter->en_mng_pt;
	if (!wake)
		igc_power_down_phy_copper_base(&adapter->hw);
	else
		igc_power_up_link(adapter);

	if (enable_wake)
		*enable_wake = wake;

	/* Release control of h/w to f/w.  If f/w is AMT enabled, this
	 * would have already happened in close and is redundant.
	 */
	igc_release_hw_control(adapter);

	pci_disable_device(pdev);

	return 0;
}

#ifdef CONFIG_PM
static int __maybe_unused igc_runtime_suspend(struct device *dev)
{
	return __igc_shutdown(to_pci_dev(dev), NULL, 1);
}

static int __maybe_unused igc_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	u32 err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (!pci_device_is_present(pdev))
		return -ENODEV;
	err = pci_enable_device_mem(pdev);
	if (err) {
		rtdev_err(netdev, "Cannot enable PCI device from suspend\n");
		return err;
	}
	pci_set_master(pdev);

	pci_enable_wake(pdev, PCI_D3hot, 0);
	pci_enable_wake(pdev, PCI_D3cold, 0);

	if (igc_init_interrupt_scheme(adapter, true)) {
		rtdev_err(netdev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	igc_reset(adapter);

	/* let the f/w know that the h/w is now under the control of the
	 * driver.
	 */
	igc_get_hw_control(adapter);

	wr32(IGC_WUS, ~0);

	if (netdev->flags & IFF_UP) {
		rtnl_lock();
		err = __igc_open(netdev, true);

		if (err)
			return err;
	}

	rtnetif_device_attach(netdev);
	return 0;
}

static int __maybe_unused igc_runtime_resume(struct device *dev)
{
	return igc_resume(dev);
}

static int __maybe_unused igc_suspend(struct device *dev)
{
	return __igc_shutdown(to_pci_dev(dev), NULL, 0);
}

static int __maybe_unused igc_runtime_idle(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);

	if (!igc_has_link(adapter))
		pm_schedule_suspend(dev, MSEC_PER_SEC * 5);

	return -EBUSY;
}
#endif /* CONFIG_PM */

static void igc_shutdown(struct pci_dev *pdev)
{
	bool wake;

	__igc_shutdown(pdev, &wake, 0);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 *  igc_io_error_detected - called when PCI error is detected
 *  @pdev: Pointer to PCI device
 *  @state: The current PCI connection state
 *
 *  This function is called after a PCI bus error affecting
 *  this device has been detected.
 **/
static pci_ers_result_t igc_io_error_detected(struct pci_dev *pdev,
					      pci_channel_state_t state)
{
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);

	rtnetif_device_detach(netdev);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	if (rtnetif_running(netdev))
		igc_down(adapter);
	pci_disable_device(pdev);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 *  igc_io_slot_reset - called after the PCI bus has been reset.
 *  @pdev: Pointer to PCI device
 *
 *  Restart the card from scratch, as if from a cold-boot. Implementation
 *  resembles the first-half of the igc_resume routine.
 **/
static pci_ers_result_t igc_io_slot_reset(struct pci_dev *pdev)
{
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);
	struct igc_hw *hw = &adapter->hw;
	pci_ers_result_t result;

	if (pci_enable_device_mem(pdev)) {
		rtdev_err(netdev, "Could not re-enable PCI device after reset\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);

		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_enable_wake(pdev, PCI_D3cold, 0);

		igc_reset(adapter);
		wr32(IGC_WUS, ~0);
		result = PCI_ERS_RESULT_RECOVERED;
	}

	return result;
}

/**
 *  igc_io_resume - called when traffic can start to flow again.
 *  @pdev: Pointer to PCI device
 *
 *  This callback is called when the error recovery driver tells us that
 *  its OK to resume normal operation. Implementation resembles the
 *  second-half of the igc_resume routine.
 */
static void igc_io_resume(struct pci_dev *pdev)
{
	struct rtnet_device *netdev = pci_get_drvdata(pdev);
	struct igc_adapter *adapter = rtnetdev_priv(netdev);

	rtnl_lock();
	if (rtnetif_running(netdev)) {
		if (igc_open(netdev)) {
			rtdev_err(netdev, "igc_open failed after reset\n");
			return;
		}
	}

	rtnetif_device_attach(netdev);

	/* let the f/w know that the h/w is now under the control of the
	 * driver.
	 */
	igc_get_hw_control(adapter);
	rtnl_unlock();
}

static const struct pci_error_handlers igc_err_handler = {
	.error_detected = igc_io_error_detected,
	.slot_reset = igc_io_slot_reset,
	.resume = igc_io_resume,
};

#ifdef CONFIG_PM
static const struct dev_pm_ops igc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(igc_suspend, igc_resume)
	SET_RUNTIME_PM_OPS(igc_runtime_suspend, igc_runtime_resume,
			   igc_runtime_idle)
};
#endif

static struct pci_driver igc_driver = {
	.name     = igc_driver_name,
	.id_table = igc_pci_tbl,
	.probe    = igc_probe,
	.remove   = igc_remove,
#ifdef CONFIG_PM
	.driver.pm = &igc_pm_ops,
#endif
	.shutdown = igc_shutdown,
	.err_handler = &igc_err_handler,
};

/**
 * igc_reinit_queues - return error
 * @adapter: pointer to adapter structure
 */
int igc_reinit_queues(struct igc_adapter *adapter)
{
	struct rtnet_device *netdev = adapter->netdev;
	int err = 0;

	if (rtnetif_running(netdev))
		igc_close(netdev);

	igc_reset_interrupt_capability(adapter);

	if (igc_init_interrupt_scheme(adapter, true)) {
		rtdev_err(netdev, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	if (rtnetif_running(netdev))
		err = igc_open(netdev);

	return err;
}

/**
 * igc_get_hw_dev - return device
 * @hw: pointer to hardware structure
 *
 * used by hardware layer to print debugging information
 */
struct rtnet_device *igc_get_hw_dev(struct igc_hw *hw)
{
	struct igc_adapter *adapter = hw->back;

	return adapter->netdev;
}

/**
 * igc_init_module - Driver Registration Routine
 *
 * igc_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init igc_init_module(void)
{
	int ret;

	pr_info("%s\n", igc_driver_string);
	pr_info("%s\n", igc_copyright);

	ret = pci_register_driver(&igc_driver);
	return ret;
}

module_init(igc_init_module);

/**
 * igc_exit_module - Driver Exit Cleanup Routine
 *
 * igc_exit_module is called just before the driver is removed
 * from memory.
 */
static void __exit igc_exit_module(void)
{
	pci_unregister_driver(&igc_driver);
}

module_exit(igc_exit_module);
/* igc_main.c */
