/* pcnet32.c: An AMD PCnet32 ethernet driver for linux. */
/*
 *	Copyright 1996-1999 Thomas Bogendoerfer
 *
 *	Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 *
 *	Copyright 1993 United States Government as represented by the
 *	Director, National Security Agency.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	This driver is for PCnet32 and PCnetPCI based ethercards
 */
/**************************************************************************
 *  23 Oct, 2000.
 *  Fixed a few bugs, related to running the controller in 32bit mode.
 *
 *  Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  Ported to RTnet: September 2003, Jan Kiszka <Jan.Kiszka@web.de>
 *************************************************************************/

#define DRV_NAME "pcnet32-rt"
#define DRV_VERSION "1.27a-RTnet-0.2"
#define DRV_RELDATE "2003-09-24"

#define pr_fmt(fmt) DRV_NAME ": " fmt

static const char *version =
	DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE " Jan.Kiszka@web.de\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

/*** RTnet ***/
#include <rtnet_port.h>

#define MAX_UNITS 8 /* More are supported, limit only on options */
#define DEFAULT_RX_POOL_SIZE 16

static int cards[MAX_UNITS] = { [0 ...(MAX_UNITS - 1)] = 1 };
module_param_array(cards, int, NULL, 0444);
MODULE_PARM_DESC(cards, "array of cards to be supported (e.g. 1,0,1)");
/*** RTnet ***/

/*
 * PCI device identifiers for "new style" Linux PCI Device Drivers
 */
static struct pci_device_id pcnet32_pci_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE_HOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LANCE, PCI_ANY_ID, PCI_ANY_ID, 0,
	  0, 0 },
	{
		0,
	}
};

MODULE_DEVICE_TABLE(pci, pcnet32_pci_tbl);

static int cards_found = -1;
static int pcnet32_have_pci;

/*
 * VLB I/O addresses
 */
static unsigned int pcnet32_portlist[] = { 0x300, 0x320, 0x340, 0x360, 0 };

static int pcnet32_debug = 1;
static int tx_start =
	1; /* Mapping -- 0:20, 1:64, 2:128, 3:~220 (depends on chip vers) */
static int pcnet32vlb; /* check for VLB cards ? */

static struct rtnet_device *pcnet32_dev; /*** RTnet ***/

static int max_interrupt_work = 80;
/*** RTnet ***
static int rx_copybreak = 200;
 *** RTnet ***/

#define PCNET32_PORT_AUI 0x00
#define PCNET32_PORT_10BT 0x01
#define PCNET32_PORT_GPSI 0x02
#define PCNET32_PORT_MII 0x03

#define PCNET32_PORT_PORTSEL 0x03
#define PCNET32_PORT_ASEL 0x04
#define PCNET32_PORT_100 0x40
#define PCNET32_PORT_FD 0x80

#define PCNET32_DMA_MASK 0xffffffff

/*
 * table to translate option values from tulip
 * to internal options
 */
static unsigned char options_mapping[] = {
	PCNET32_PORT_ASEL, /*  0 Auto-select	  */
	PCNET32_PORT_AUI, /*  1 BNC/AUI	  */
	PCNET32_PORT_AUI, /*  2 AUI/BNC	  */
	PCNET32_PORT_ASEL, /*  3 not supported	  */
	PCNET32_PORT_10BT | PCNET32_PORT_FD, /*  4 10baseT-FD	  */
	PCNET32_PORT_ASEL, /*  5 not supported	  */
	PCNET32_PORT_ASEL, /*  6 not supported	  */
	PCNET32_PORT_ASEL, /*  7 not supported	  */
	PCNET32_PORT_ASEL, /*  8 not supported	  */
	PCNET32_PORT_MII, /*  9 MII 10baseT	  */
	PCNET32_PORT_MII | PCNET32_PORT_FD, /* 10 MII 10baseT-FD	  */
	PCNET32_PORT_MII, /* 11 MII (autosel)	  */
	PCNET32_PORT_10BT, /* 12 10BaseT	  */
	PCNET32_PORT_MII | PCNET32_PORT_100, /* 13 MII 100BaseTx	  */
	PCNET32_PORT_MII | PCNET32_PORT_100 |
		PCNET32_PORT_FD, /* 14 MII 100BaseTx-FD */
	PCNET32_PORT_ASEL /* 15 not supported	  */
};

static int options[MAX_UNITS];
static int full_duplex[MAX_UNITS];

/*
 *				Theory of Operation
 *
 * This driver uses the same software structure as the normal lance
 * driver. So look for a verbose description in lance.c. The differences
 * to the normal lance driver is the use of the 32bit mode of PCnet32
 * and PCnetPCI chips. Because these chips are 32bit chips, there is no
 * 16MB limitation and we don't need bounce buffers.
 */

/*
 * History:
 * v0.01:  Initial version
 *	   only tested on Alpha Noname Board
 * v0.02:  changed IRQ handling for new interrupt scheme (dev_id)
 *	   tested on a ASUS SP3G
 * v0.10:  fixed an odd problem with the 79C974 in a Compaq Deskpro XL
 *	   looks like the 974 doesn't like stopping and restarting in a
 *	   short period of time; now we do a reinit of the lance; the
 *	   bug was triggered by doing ifconfig eth0 <ip> broadcast <addr>
 *	   and hangs the machine (thanks to Klaus Liedl for debugging)
 * v0.12:  by suggestion from Donald Becker: Renamed driver to pcnet32,
 *	   made it standalone (no need for lance.c)
 * v0.13:  added additional PCI detecting for special PCI devices (Compaq)
 * v0.14:  stripped down additional PCI probe (thanks to David C Niemi
 *	   and sveneric@xs4all.nl for testing this on their Compaq boxes)
 * v0.15:  added 79C965 (VLB) probe
 *	   added interrupt sharing for PCI chips
 * v0.16:  fixed set_multicast_list on Alpha machines
 * v0.17:  removed hack from dev.c; now pcnet32 uses ethif_probe in Space.c
 * v0.19:  changed setting of autoselect bit
 * v0.20:  removed additional Compaq PCI probe; there is now a working one
 *	   in arch/i386/bios32.c
 * v0.21:  added endian conversion for ppc, from work by cort@cs.nmt.edu
 * v0.22:  added printing of status to ring dump
 * v0.23:  changed enet_statistics to net_devive_stats
 * v0.90:  added multicast filter
 *	   added module support
 *	   changed irq probe to new style
 *	   added PCnetFast chip id
 *	   added fix for receive stalls with Intel saturn chipsets
 *	   added in-place rx skbs like in the tulip driver
 *	   minor cleanups
 * v0.91:  added PCnetFast+ chip id
 *	   back port to 2.0.x
 * v1.00:  added some stuff from Donald Becker's 2.0.34 version
 *	   added support for byte counters in net_dev_stats
 * v1.01:  do ring dumps, only when debugging the driver
 *	   increased the transmit timeout
 * v1.02:  fixed memory leak in pcnet32_init_ring()
 * v1.10:  workaround for stopped transmitter
 *	   added port selection for modules
 *	   detect special T1/E1 WAN card and setup port selection
 * v1.11:  fixed wrong checking of Tx errors
 * v1.20:  added check of return value kmalloc (cpeterso@cs.washington.edu)
 *	   added save original kmalloc addr for freeing (mcr@solidum.com)
 *	   added support for PCnetHome chip (joe@MIT.EDU)
 *	   rewritten PCI card detection
 *	   added dwio mode to get driver working on some PPC machines
 * v1.21:  added mii selection and mii ioctl
 * v1.22:  changed pci scanning code to make PPC people happy
 *	   fixed switching to 32bit mode in pcnet32_open() (thanks
 *	   to Michael Richard <mcr@solidum.com> for noticing this one)
 *	   added sub vendor/device id matching (thanks again to
 *	   Michael Richard <mcr@solidum.com>)
 *	   added chip id for 79c973/975 (thanks to Zach Brown <zab@zabbo.net>)
 * v1.23   fixed small bug, when manual selecting MII speed/duplex
 * v1.24   Applied Thomas' patch to use TxStartPoint and thus decrease TxFIFO
 *	   underflows.	Added tx_start_pt module parameter. Increased
 *	   TX_RING_SIZE from 16 to 32.	Added #ifdef'd code to use DXSUFLO
 *	   for FAST[+] chipsets. <kaf@fc.hp.com>
 * v1.24ac Added SMP spinlocking - Alan Cox <alan@redhat.com>
 * v1.25kf Added No Interrupt on successful Tx for some Tx's <kaf@fc.hp.com>
 * v1.26   Converted to pci_alloc_consistent, Jamey Hicks / George France
 *                                           <jamey@crl.dec.com>
 * -	   Fixed a few bugs, related to running the controller in 32bit mode.
 *	   23 Oct, 2000.  Carsten Langgaard, carstenl@mips.com
 *	   Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 * v1.26p  Fix oops on rmmod+insmod; plug i/o resource leak - Paul Gortmaker
 * v1.27   improved CSR/PROM address detection, lots of cleanups,
 *	   new pcnet32vlb module option, HP-PARISC support,
 *	   added module parameter descriptions,
 *	   initial ethtool support - Helge Deller <deller@gmx.de>
 * v1.27a  Sun Feb 10 2002 Go Taniguchi <go@turbolinux.co.jp>
 *	   use alloc_etherdev and register_netdev
 *	   fix pci probe not increment cards_found
 *	   FD auto negotiate error workaround for xSeries250
 *	   clean up and using new mii module
 */

/*
 * Set the number of Tx and Rx buffers, using Log_2(# buffers).
 * Reasonable default values are 4 Tx buffers, and 16 Rx buffers.
 * That translates to 2 (4 == 2^^2) and 4 (16 == 2^^4).
 */
#ifndef PCNET32_LOG_TX_BUFFERS
#define PCNET32_LOG_TX_BUFFERS 4
#define PCNET32_LOG_RX_BUFFERS 3 /*** RTnet ***/
#endif

#define TX_RING_SIZE (1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK (TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS ((PCNET32_LOG_TX_BUFFERS) << 12)

#define RX_RING_SIZE (1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK (RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS ((PCNET32_LOG_RX_BUFFERS) << 4)

#define PKT_BUF_SZ 1544

/* Offsets from base I/O address. */
#define PCNET32_WIO_RDP 0x10
#define PCNET32_WIO_RAP 0x12
#define PCNET32_WIO_RESET 0x14
#define PCNET32_WIO_BDP 0x16

#define PCNET32_DWIO_RDP 0x10
#define PCNET32_DWIO_RAP 0x14
#define PCNET32_DWIO_RESET 0x18
#define PCNET32_DWIO_BDP 0x1C

#define PCNET32_TOTAL_SIZE 0x20

/* The PCNET32 Rx and Tx ring descriptors. */
struct pcnet32_rx_head {
	u32 base;
	s16 buf_length;
	s16 status;
	u32 msg_length;
	u32 reserved;
};

struct pcnet32_tx_head {
	u32 base;
	s16 length;
	s16 status;
	u32 misc;
	u32 reserved;
};

/* The PCNET32 32-Bit initialization block, described in databook. */
struct pcnet32_init_block {
	u16 mode;
	u16 tlen_rlen;
	u8 phys_addr[6];
	u16 reserved;
	u32 filter[2];
	/* Receive and transmit ring base, along with extra bits. */
	u32 rx_ring;
	u32 tx_ring;
};

/* PCnet32 access functions */
struct pcnet32_access {
	u16 (*read_csr)(unsigned long, int);
	void (*write_csr)(unsigned long, int, u16);
	u16 (*read_bcr)(unsigned long, int);
	void (*write_bcr)(unsigned long, int, u16);
	u16 (*read_rap)(unsigned long);
	void (*write_rap)(unsigned long, u16);
	void (*reset)(unsigned long);
};

/*
 * The first three fields of pcnet32_private are read by the ethernet device
 * so we allocate the structure should be allocated by pci_alloc_consistent().
 */
struct pcnet32_private {
	/* The Tx and Rx ring entries must be aligned on 16-byte boundaries in 32bit mode. */
	struct pcnet32_rx_head rx_ring[RX_RING_SIZE];
	struct pcnet32_tx_head tx_ring[TX_RING_SIZE];
	struct pcnet32_init_block init_block;
	dma_addr_t dma_addr; /* DMA address of beginning of this object,
					   returned by pci_alloc_consistent */
	struct pci_dev
		*pci_dev; /* Pointer to the associated pci device structure */
	const char *name;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	/*** RTnet ***/
	struct rtskb *tx_skbuff[TX_RING_SIZE];
	struct rtskb *rx_skbuff[RX_RING_SIZE];
	/*** RTnet ***/
	dma_addr_t tx_dma_addr[TX_RING_SIZE];
	dma_addr_t rx_dma_addr[RX_RING_SIZE];
	struct pcnet32_access a;
	rtdm_lock_t lock; /* Guard lock */
	unsigned int cur_rx, cur_tx; /* The next free ring entry */
	unsigned int dirty_rx, dirty_tx; /* The ring entries to be free()ed. */
	struct net_device_stats stats;
	char tx_full;
	int options;
	int shared_irq : 1, /* shared irq possible */
		ltint : 1, /* enable TxDone-intr inhibitor */
		dxsuflo : 1, /* disable transmit stop on uflo */
		mii : 1; /* mii port available */
	struct rtnet_device *next; /*** RTnet ***/
	struct mii_if_info mii_if;
	rtdm_irq_t irq_handle;
};

static void pcnet32_probe_vlbus(void);
static int pcnet32_probe_pci(struct pci_dev *, const struct pci_device_id *);
static int pcnet32_probe1(unsigned long, unsigned int, int, struct pci_dev *);
/*** RTnet ***/
static int pcnet32_open(struct rtnet_device *);
static int pcnet32_init_ring(struct rtnet_device *);
static int pcnet32_start_xmit(struct rtskb *, struct rtnet_device *);
static int pcnet32_rx(struct rtnet_device *, nanosecs_abs_t *time_stamp);
//static void pcnet32_tx_timeout (struct net_device *dev);
static int pcnet32_interrupt(rtdm_irq_t *irq_handle);
static int pcnet32_close(struct rtnet_device *);
static struct net_device_stats *pcnet32_get_stats(struct rtnet_device *);
//static void pcnet32_set_multicast_list(struct net_device *);
//static int  pcnet32_ioctl(struct net_device *, struct ifreq *, int);
//static int mdio_read(struct net_device *dev, int phy_id, int reg_num);
//static void mdio_write(struct net_device *dev, int phy_id, int reg_num, int val);
/*** RTnet ***/

enum pci_flags_bit {
	PCI_USES_IO = 1,
	PCI_USES_MEM = 2,
	PCI_USES_MASTER = 4,
	PCI_ADDR0 = 0x10 << 0,
	PCI_ADDR1 = 0x10 << 1,
	PCI_ADDR2 = 0x10 << 2,
	PCI_ADDR3 = 0x10 << 3,
};

static u16 pcnet32_wio_read_csr(unsigned long addr, int index)
{
	outw(index, addr + PCNET32_WIO_RAP);
	return inw(addr + PCNET32_WIO_RDP);
}

static void pcnet32_wio_write_csr(unsigned long addr, int index, u16 val)
{
	outw(index, addr + PCNET32_WIO_RAP);
	outw(val, addr + PCNET32_WIO_RDP);
}

static u16 pcnet32_wio_read_bcr(unsigned long addr, int index)
{
	outw(index, addr + PCNET32_WIO_RAP);
	return inw(addr + PCNET32_WIO_BDP);
}

static void pcnet32_wio_write_bcr(unsigned long addr, int index, u16 val)
{
	outw(index, addr + PCNET32_WIO_RAP);
	outw(val, addr + PCNET32_WIO_BDP);
}

static u16 pcnet32_wio_read_rap(unsigned long addr)
{
	return inw(addr + PCNET32_WIO_RAP);
}

static void pcnet32_wio_write_rap(unsigned long addr, u16 val)
{
	outw(val, addr + PCNET32_WIO_RAP);
}

static void pcnet32_wio_reset(unsigned long addr)
{
	inw(addr + PCNET32_WIO_RESET);
}

static int pcnet32_wio_check(unsigned long addr)
{
	outw(88, addr + PCNET32_WIO_RAP);
	return (inw(addr + PCNET32_WIO_RAP) == 88);
}

static struct pcnet32_access pcnet32_wio = {
	read_csr: pcnet32_wio_read_csr,
	write_csr: pcnet32_wio_write_csr,
	read_bcr: pcnet32_wio_read_bcr,
	write_bcr: pcnet32_wio_write_bcr,
	read_rap: pcnet32_wio_read_rap,
	write_rap: pcnet32_wio_write_rap,
	reset: pcnet32_wio_reset
};

static u16 pcnet32_dwio_read_csr(unsigned long addr, int index)
{
	outl(index, addr + PCNET32_DWIO_RAP);
	return (inl(addr + PCNET32_DWIO_RDP) & 0xffff);
}

static void pcnet32_dwio_write_csr(unsigned long addr, int index, u16 val)
{
	outl(index, addr + PCNET32_DWIO_RAP);
	outl(val, addr + PCNET32_DWIO_RDP);
}

static u16 pcnet32_dwio_read_bcr(unsigned long addr, int index)
{
	outl(index, addr + PCNET32_DWIO_RAP);
	return (inl(addr + PCNET32_DWIO_BDP) & 0xffff);
}

static void pcnet32_dwio_write_bcr(unsigned long addr, int index, u16 val)
{
	outl(index, addr + PCNET32_DWIO_RAP);
	outl(val, addr + PCNET32_DWIO_BDP);
}

static u16 pcnet32_dwio_read_rap(unsigned long addr)
{
	return (inl(addr + PCNET32_DWIO_RAP) & 0xffff);
}

static void pcnet32_dwio_write_rap(unsigned long addr, u16 val)
{
	outl(val, addr + PCNET32_DWIO_RAP);
}

static void pcnet32_dwio_reset(unsigned long addr)
{
	inl(addr + PCNET32_DWIO_RESET);
}

static int pcnet32_dwio_check(unsigned long addr)
{
	outl(88, addr + PCNET32_DWIO_RAP);
	return ((inl(addr + PCNET32_DWIO_RAP) & 0xffff) == 88);
}

static struct pcnet32_access pcnet32_dwio = {
	read_csr: pcnet32_dwio_read_csr,
	write_csr: pcnet32_dwio_write_csr,
	read_bcr: pcnet32_dwio_read_bcr,
	write_bcr: pcnet32_dwio_write_bcr,
	read_rap: pcnet32_dwio_read_rap,
	write_rap: pcnet32_dwio_write_rap,
	reset: pcnet32_dwio_reset
};

/* only probes for non-PCI devices, the rest are handled by
 * pci_register_driver via pcnet32_probe_pci */

static void pcnet32_probe_vlbus(void)
{
	unsigned int *port, ioaddr;

	/* search for PCnet32 VLB cards at known addresses */
	for (port = pcnet32_portlist; (ioaddr = *port); port++) {
		if (!request_region(ioaddr, PCNET32_TOTAL_SIZE,
				    "pcnet32_probe_vlbus")) {
			/* check if there is really a pcnet chip on that ioaddr */
			if ((inb(ioaddr + 14) == 0x57) &&
			    (inb(ioaddr + 15) == 0x57)) {
				pcnet32_probe1(ioaddr, 0, 0, NULL);
			} else {
				release_region(ioaddr, PCNET32_TOTAL_SIZE);
			}
		}
	}
}

static int pcnet32_probe_pci(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	unsigned long ioaddr;
	int err;

	err = pci_enable_device(pdev);
	if (err < 0) {
		pr_err("failed to enable device -- err=%d\n", err);
		return err;
	}
	pci_set_master(pdev);

	ioaddr = pci_resource_start(pdev, 0);
	if (!ioaddr) {
		pr_err("card has no PCI IO resources, aborting\n");
		return -ENODEV;
	}

	err = dma_set_mask(&pdev->dev, PCNET32_DMA_MASK);
	if (err) {
		pr_err("architecture does not support 32bit PCI busmaster DMA\n");
		return err;
	}

	return pcnet32_probe1(ioaddr, pdev->irq, 1, pdev);
}

/* pcnet32_probe1
 *  Called from both pcnet32_probe_vlbus and pcnet_probe_pci.
 *  pdev will be NULL when called from pcnet32_probe_vlbus.
 */
static int pcnet32_probe1(unsigned long ioaddr, unsigned int irq_line,
			  int shared, struct pci_dev *pdev)
{
	struct pcnet32_private *lp;
	dma_addr_t lp_dma_addr;
	int i, media;
	int fdx, mii, fset, dxsuflo, ltint;
	int chip_version;
	char *chipname;
	struct rtnet_device *dev; /*** RTnet ***/
	struct pcnet32_access *a = NULL;
	u8 promaddr[6];

	// *** RTnet ***
	cards_found++;
	if (cards[cards_found] == 0)
		return -ENODEV;
	// *** RTnet ***

	/* reset the chip */
	pcnet32_wio_reset(ioaddr);

	/* NOTE: 16-bit check is first, otherwise some older PCnet chips fail */
	if (pcnet32_wio_read_csr(ioaddr, 0) == 4 && pcnet32_wio_check(ioaddr)) {
		a = &pcnet32_wio;
	} else {
		pcnet32_dwio_reset(ioaddr);
		if (pcnet32_dwio_read_csr(ioaddr, 0) == 4 &&
		    pcnet32_dwio_check(ioaddr)) {
			a = &pcnet32_dwio;
		} else
			return -ENODEV;
	}

	chip_version =
		a->read_csr(ioaddr, 88) | (a->read_csr(ioaddr, 89) << 16);
	if (pcnet32_debug > 2)
		pr_info("PCnet chip version is %#x.\n", chip_version);
	if ((chip_version & 0xfff) != 0x003)
		return -ENODEV;

	/* initialize variables */
	fdx = mii = fset = dxsuflo = ltint = 0;
	chip_version = (chip_version >> 12) & 0xffff;

	switch (chip_version) {
	case 0x2420:
		chipname = "PCnet/PCI 79C970"; /* PCI */
		break;
	case 0x2430:
		if (shared)
			chipname =
				"PCnet/PCI 79C970"; /* 970 gives the wrong chip id back */
		else
			chipname = "PCnet/32 79C965"; /* 486/VL bus */
		break;
	case 0x2621:
		chipname = "PCnet/PCI II 79C970A"; /* PCI */
		fdx = 1;
		break;
	case 0x2623:
		chipname = "PCnet/FAST 79C971"; /* PCI */
		fdx = 1;
		mii = 1;
		fset = 1;
		ltint = 1;
		break;
	case 0x2624:
		chipname = "PCnet/FAST+ 79C972"; /* PCI */
		fdx = 1;
		mii = 1;
		fset = 1;
		break;
	case 0x2625:
		chipname = "PCnet/FAST III 79C973"; /* PCI */
		fdx = 1;
		mii = 1;
		break;
	case 0x2626:
		chipname = "PCnet/Home 79C978"; /* PCI */
		fdx = 1;
		/*
	 * This is based on specs published at www.amd.com.  This section
	 * assumes that a card with a 79C978 wants to go into 1Mb HomePNA
	 * mode.  The 79C978 can also go into standard ethernet, and there
	 * probably should be some sort of module option to select the
	 * mode by which the card should operate
	 */
		/* switch to home wiring mode */
		media = a->read_bcr(ioaddr, 49);
		if (pcnet32_debug > 2)
			pr_debug("media reset to %#x.\n", media);
		a->write_bcr(ioaddr, 49, media);
		break;
	case 0x2627:
		chipname = "PCnet/FAST III 79C975"; /* PCI */
		fdx = 1;
		mii = 1;
		break;
	default:
		pr_info("PCnet version %#x, no PCnet32 chip.\n", chip_version);
		return -ENODEV;
	}

	/*
     *	On selected chips turn on the BCR18:NOUFLO bit. This stops transmit
     *	starting until the packet is loaded. Strike one for reliability, lose
     *	one for latency - although on PCI this isnt a big loss. Older chips
     *	have FIFO's smaller than a packet, so you can't do this.
     */

	if (fset) {
		a->write_bcr(ioaddr, 18, (a->read_bcr(ioaddr, 18) | 0x0800));
		a->write_csr(ioaddr, 80,
			     (a->read_csr(ioaddr, 80) & 0x0C00) | 0x0c00);
		dxsuflo = 1;
		ltint = 1;
	}

	/*** RTnet ***/
	dev = rt_alloc_etherdev(0, RX_RING_SIZE * 2 + TX_RING_SIZE);
	if (dev == NULL)
		return -ENOMEM;
	rtdev_alloc_name(dev, "rteth%d");
	rt_rtdev_connect(dev, &RTDEV_manager);
	dev->vers = RTDEV_VERS_2_0;
	dev->sysbind = &pdev->dev;
	/*** RTnet ***/

	pr_info("%s at %#3lx,", chipname, ioaddr);

	/* In most chips, after a chip reset, the ethernet address is read from the
     * station address PROM at the base address and programmed into the
     * "Physical Address Registers" CSR12-14.
     * As a precautionary measure, we read the PROM values and complain if
     * they disagree with the CSRs.  Either way, we use the CSR values, and
     * double check that they are valid.
     */
	for (i = 0; i < 3; i++) {
		unsigned int val;
		val = a->read_csr(ioaddr, i + 12) & 0x0ffff;
		/* There may be endianness issues here. */
		dev->dev_addr[2 * i] = val & 0x0ff;
		dev->dev_addr[2 * i + 1] = (val >> 8) & 0x0ff;
	}

	/* read PROM address and compare with CSR address */
	for (i = 0; i < 6; i++)
		promaddr[i] = inb(ioaddr + i);

	if (memcmp(promaddr, dev->dev_addr, 6) ||
	    !is_valid_ether_addr(dev->dev_addr)) {
#ifndef __powerpc__
		if (is_valid_ether_addr(promaddr)) {
#else
		if (!is_valid_ether_addr(dev->dev_addr) &&
		    is_valid_ether_addr(promaddr)) {
#endif
			pr_cont(" warning: CSR address invalid,\n");
			pr_info("    using instead PROM address of");
			memcpy(dev->dev_addr, promaddr, 6);
		}
	}

	/* if the ethernet address is not valid, force to 00:00:00:00:00:00 */
	if (!is_valid_ether_addr(dev->dev_addr))
		memset(dev->dev_addr, 0, sizeof(dev->dev_addr));

	for (i = 0; i < 6; i++)
		pr_cont(" %2.2x", dev->dev_addr[i]);

	if (((chip_version + 1) & 0xfffe) ==
	    0x2624) { /* Version 0x2623 or 0x2624 */
		i = a->read_csr(ioaddr, 80) & 0x0C00; /* Check tx_start_pt */
		pr_info("    tx_start_pt(0x%04x):", i);
		switch (i >> 10) {
		case 0:
			pr_cont("  20 bytes,");
			break;
		case 1:
			pr_cont("  64 bytes,");
			break;
		case 2:
			pr_cont(" 128 bytes,");
			break;
		case 3:
			pr_cont("~220 bytes,");
			break;
		}
		i = a->read_bcr(ioaddr, 18); /* Check Burst/Bus control */
		pr_cont(" BCR18(%x):", i & 0xffff);
		if (i & (1 << 5))
			pr_cont("BurstWrEn ");
		if (i & (1 << 6))
			pr_cont("BurstRdEn ");
		if (i & (1 << 7))
			pr_cont("DWordIO ");
		if (i & (1 << 11))
			pr_cont("NoUFlow ");
		i = a->read_bcr(ioaddr, 25);
		pr_info("    SRAMSIZE=0x%04x,", i << 8);
		i = a->read_bcr(ioaddr, 26);
		pr_cont(" SRAM_BND=0x%04x,", i << 8);
		i = a->read_bcr(ioaddr, 27);
		if (i & (1 << 14))
			pr_cont("LowLatRx");
	}

	dev->base_addr = ioaddr;
	if (request_region(ioaddr, PCNET32_TOTAL_SIZE, chipname) == NULL)
		return -EBUSY;

	/* dma_alloc_coherent returns page-aligned memory, so we do not have to check the alignment */
	if ((lp = dma_alloc_coherent(&pdev->dev, sizeof(*lp), &lp_dma_addr,
				     GFP_ATOMIC)) ==
	    NULL) {
		release_region(ioaddr, PCNET32_TOTAL_SIZE);
		return -ENOMEM;
	}

	memset(lp, 0, sizeof(*lp));
	lp->dma_addr = lp_dma_addr;
	lp->pci_dev = pdev;

	rtdm_lock_init(&lp->lock);

	dev->priv = lp;
	lp->name = chipname;
	lp->shared_irq = shared;
	lp->mii_if.full_duplex = fdx;
	lp->dxsuflo = dxsuflo;
	lp->ltint = ltint;
	lp->mii = mii;
	if ((cards_found >= MAX_UNITS) ||
	    (options[cards_found] > (int)sizeof(options_mapping)))
		lp->options = PCNET32_PORT_ASEL;
	else
		lp->options = options_mapping[options[cards_found]];
	/*** RTnet ***
    lp->mii_if.dev = dev;
    lp->mii_if.mdio_read = mdio_read;
    lp->mii_if.mdio_write = mdio_write;
 *** RTnet ***/

	if (fdx && !(lp->options & PCNET32_PORT_ASEL) &&
	    ((cards_found >= MAX_UNITS) || full_duplex[cards_found]))
		lp->options |= PCNET32_PORT_FD;

	if (!a) {
		pr_err("No access methods\n");
		dma_free_coherent(&lp->pci_dev->dev, sizeof(*lp), lp,
				  lp->dma_addr);
		release_region(ioaddr, PCNET32_TOTAL_SIZE);
		return -ENODEV;
	}
	lp->a = *a;

	/* detect special T1/E1 WAN card by checking for MAC address */
	if (dev->dev_addr[0] == 0x00 && dev->dev_addr[1] == 0xe0 &&
	    dev->dev_addr[2] == 0x75)
		lp->options = PCNET32_PORT_FD | PCNET32_PORT_GPSI;

	lp->init_block.mode = le16_to_cpu(0x0003); /* Disable Rx and Tx. */
	lp->init_block.tlen_rlen =
		le16_to_cpu(TX_RING_LEN_BITS | RX_RING_LEN_BITS);
	for (i = 0; i < 6; i++)
		lp->init_block.phys_addr[i] = dev->dev_addr[i];
	lp->init_block.filter[0] = 0x00000000;
	lp->init_block.filter[1] = 0x00000000;
	lp->init_block.rx_ring = (u32)le32_to_cpu(
		lp->dma_addr + offsetof(struct pcnet32_private, rx_ring));
	lp->init_block.tx_ring = (u32)le32_to_cpu(
		lp->dma_addr + offsetof(struct pcnet32_private, tx_ring));

	/* switch pcnet32 to 32bit mode */
	a->write_bcr(ioaddr, 20, 2);

	a->write_csr(
		ioaddr, 1,
		(lp->dma_addr + offsetof(struct pcnet32_private, init_block)) &
			0xffff);
	a->write_csr(
		ioaddr, 2,
		(lp->dma_addr + offsetof(struct pcnet32_private, init_block)) >>
			16);

	if (irq_line) {
		dev->irq = irq_line;
	}

	if (dev->irq >= 2)
		pr_info("assigned IRQ %d.\n", dev->irq);
	else {
		unsigned long irq_mask = probe_irq_on();

		/*
	 * To auto-IRQ we enable the initialization-done and DMA error
	 * interrupts. For ISA boards we get a DMA error, but VLB and PCI
	 * boards will work.
	 */
		/* Trigger an initialization just for the interrupt. */
		a->write_csr(ioaddr, 0, 0x41);
		mdelay(1);

		dev->irq = probe_irq_off(irq_mask);
		if (dev->irq)
			pr_info("probed IRQ %d.\n", dev->irq);
		else {
			pr_info("failed to detect IRQ line.\n");
			dma_free_coherent(&lp->pci_dev->dev, sizeof(*lp), lp,
					  lp->dma_addr);
			release_region(ioaddr, PCNET32_TOTAL_SIZE);
			return -ENODEV;
		}
	}

	/* The PCNET32-specific entries in the device structure. */
	dev->open = &pcnet32_open;
	dev->hard_start_xmit = &pcnet32_start_xmit;
	dev->stop = &pcnet32_close;
	dev->get_stats = &pcnet32_get_stats;
	/*** RTnet ***
    dev->set_multicast_list = &pcnet32_set_multicast_list;
    dev->do_ioctl = &pcnet32_ioctl;
    dev->tx_timeout = pcnet32_tx_timeout;
    dev->watchdog_timeo = (5*HZ);
 *** RTnet ***/

	lp->next = pcnet32_dev;
	pcnet32_dev = dev;

	/* Fill in the generic fields of the device structure. */
	/*** RTnet ***/
	if ((i = rt_register_rtnetdev(dev))) {
		dma_free_coherent(&lp->pci_dev->dev, sizeof(*lp), lp,
				   lp->dma_addr);
		release_region(ioaddr, PCNET32_TOTAL_SIZE);
		rtdev_free(dev);
		return i;
	}
	/*** RTnet ***/

	pr_info("%s: registered as %s\n", dev->name, lp->name);
	return 0;
}

static int pcnet32_open(struct rtnet_device *dev) /*** RTnet ***/
{
	struct pcnet32_private *lp = dev->priv;
	unsigned long ioaddr = dev->base_addr;
	u16 val;
	int i;

	/*** RTnet ***/
	if (dev->irq == 0)
		return -EAGAIN;

	rt_stack_connect(dev, &STACK_manager);

	i = rtdm_irq_request(&lp->irq_handle, dev->irq, pcnet32_interrupt,
			     RTDM_IRQTYPE_SHARED, "rt_pcnet32", dev);
	if (i)
		return i;
	/*** RTnet ***/

	/* Check for a valid station address */
	if (!is_valid_ether_addr(dev->dev_addr))
		return -EINVAL;

	/* Reset the PCNET32 */
	lp->a.reset(ioaddr);

	/* switch pcnet32 to 32bit mode */
	lp->a.write_bcr(ioaddr, 20, 2);

	if (pcnet32_debug > 1)
		pr_debug(
		       "%s: pcnet32_open() irq %d tx/rx rings %#x/%#x init %#x.\n",
		       dev->name, dev->irq,
		       (u32)(lp->dma_addr +
			     offsetof(struct pcnet32_private, tx_ring)),
		       (u32)(lp->dma_addr +
			     offsetof(struct pcnet32_private, rx_ring)),
		       (u32)(lp->dma_addr +
			     offsetof(struct pcnet32_private, init_block)));

	/* set/reset autoselect bit */
	val = lp->a.read_bcr(ioaddr, 2) & ~2;
	if (lp->options & PCNET32_PORT_ASEL)
		val |= 2;
	lp->a.write_bcr(ioaddr, 2, val);

	/* handle full duplex setting */
	if (lp->mii_if.full_duplex) {
		val = lp->a.read_bcr(ioaddr, 9) & ~3;
		if (lp->options & PCNET32_PORT_FD) {
			val |= 1;
			if (lp->options == (PCNET32_PORT_FD | PCNET32_PORT_AUI))
				val |= 2;
		} else if (lp->options & PCNET32_PORT_ASEL) {
			/* workaround of xSeries250, turn on for 79C975 only */
			i = ((lp->a.read_csr(ioaddr, 88) |
			      (lp->a.read_csr(ioaddr, 89) << 16)) >>
			     12) &
			    0xffff;
			if (i == 0x2627)
				val |= 3;
		}
		lp->a.write_bcr(ioaddr, 9, val);
	}

	/* set/reset GPSI bit in test register */
	val = lp->a.read_csr(ioaddr, 124) & ~0x10;
	if ((lp->options & PCNET32_PORT_PORTSEL) == PCNET32_PORT_GPSI)
		val |= 0x10;
	lp->a.write_csr(ioaddr, 124, val);

	if (lp->mii && !(lp->options & PCNET32_PORT_ASEL)) {
		val = lp->a.read_bcr(ioaddr, 32) &
		      ~0x38; /* disable Auto Negotiation, set 10Mpbs, HD */
		if (lp->options & PCNET32_PORT_FD)
			val |= 0x10;
		if (lp->options & PCNET32_PORT_100)
			val |= 0x08;
		lp->a.write_bcr(ioaddr, 32, val);
	} else {
		if (lp->options &
		    PCNET32_PORT_ASEL) { /* enable auto negotiate, setup, disable fd */
			val = lp->a.read_bcr(ioaddr, 32) & ~0x98;
			val |= 0x20;
			lp->a.write_bcr(ioaddr, 32, val);
		}
	}

#ifdef DO_DXSUFLO
	if (lp->dxsuflo) { /* Disable transmit stop on underflow */
		val = lp->a.read_csr(ioaddr, 3);
		val |= 0x40;
		lp->a.write_csr(ioaddr, 3, val);
	}
#endif

	if (lp->ltint) { /* Enable TxDone-intr inhibitor */
		val = lp->a.read_csr(ioaddr, 5);
		val |= (1 << 14);
		lp->a.write_csr(ioaddr, 5, val);
	}

	lp->init_block.mode =
		le16_to_cpu((lp->options & PCNET32_PORT_PORTSEL) << 7);
	lp->init_block.filter[0] = 0x00000000;
	lp->init_block.filter[1] = 0x00000000;
	if (pcnet32_init_ring(dev))
		return -ENOMEM;

	/* Re-initialize the PCNET32, and start it when done. */
	lp->a.write_csr(
		ioaddr, 1,
		(lp->dma_addr + offsetof(struct pcnet32_private, init_block)) &
			0xffff);
	lp->a.write_csr(
		ioaddr, 2,
		(lp->dma_addr + offsetof(struct pcnet32_private, init_block)) >>
			16);

	lp->a.write_csr(ioaddr, 4, 0x0915);
	lp->a.write_csr(ioaddr, 0, 0x0001);

	rtnetif_start_queue(dev); /*** RTnet ***/

	i = 0;
	while (i++ < 100)
		if (lp->a.read_csr(ioaddr, 0) & 0x0100)
			break;
	/*
     * We used to clear the InitDone bit, 0x0100, here but Mark Stockton
     * reports that doing so triggers a bug in the '974.
     */
	lp->a.write_csr(ioaddr, 0, 0x0042);

	if (pcnet32_debug > 2)
		pr_debug(
		       "%s: pcnet32 open after %d ticks, init block %#x csr0 %4.4x.\n",
		       dev->name, i,
		       (u32)(lp->dma_addr +
			     offsetof(struct pcnet32_private, init_block)),
		       lp->a.read_csr(ioaddr, 0));

	return 0; /* Always succeed */
}

/*
 * The LANCE has been halted for one reason or another (busmaster memory
 * arbitration error, Tx FIFO underflow, driver stopped it to reconfigure,
 * etc.).  Modern LANCE variants always reload their ring-buffer
 * configuration when restarted, so we must reinitialize our ring
 * context before restarting.  As part of this reinitialization,
 * find all packets still on the Tx ring and pretend that they had been
 * sent (in effect, drop the packets on the floor) - the higher-level
 * protocols will time out and retransmit.  It'd be better to shuffle
 * these skbs to a temp list and then actually re-Tx them after
 * restarting the chip, but I'm too lazy to do so right now.  dplatt@3do.com
 */

/*** RTnet ***
static void
pcnet32_purge_tx_ring(struct net_device *dev)
{
    struct pcnet32_private *lp = dev->priv;
    int i;

    for (i = 0; i < TX_RING_SIZE; i++) {
	if (lp->tx_skbuff[i]) {
	    pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[i], lp->tx_skbuff[i]->len, PCI_DMA_TODEVICE);
	    dev_kfree_skb(lp->tx_skbuff[i]);
	    lp->tx_skbuff[i] = NULL;
	    lp->tx_dma_addr[i] = 0;
	}
    }
}
 *** RTnet ***/

/* Initialize the PCNET32 Rx and Tx rings. */
static int pcnet32_init_ring(struct rtnet_device *dev) /*** RTnet ***/
{
	struct pcnet32_private *lp = dev->priv;
	int i;

	lp->tx_full = 0;
	lp->cur_rx = lp->cur_tx = 0;
	lp->dirty_rx = lp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		struct rtskb *rx_skbuff = lp->rx_skbuff[i]; /*** RTnet ***/
		if (rx_skbuff == NULL) {
			if (!(rx_skbuff = lp->rx_skbuff[i] =
				      rtnetdev_alloc_rtskb(
					      dev,
					      PKT_BUF_SZ))) { /*** RTnet ***/
				/* there is not much, we can do at this point */
				pr_err(
				       "%s: pcnet32_init_ring rtnetdev_alloc_rtskb failed.\n",
				       dev->name);
				return -1;
			}
			rtskb_reserve(rx_skbuff, 2); /*** RTnet ***/
		}
		lp->rx_dma_addr[i] =
			dma_map_single(&lp->pci_dev->dev, rx_skbuff->tail,
				       rx_skbuff->len, DMA_FROM_DEVICE);
		lp->rx_ring[i].base = (u32)le32_to_cpu(lp->rx_dma_addr[i]);
		lp->rx_ring[i].buf_length = le16_to_cpu(-PKT_BUF_SZ);
		lp->rx_ring[i].status = le16_to_cpu(0x8000);
	}
	/* The Tx buffer address is filled in as needed, but we do need to clear
       the upper ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		lp->tx_ring[i].base = 0;
		lp->tx_ring[i].status = 0;
		lp->tx_dma_addr[i] = 0;
	}

	lp->init_block.tlen_rlen =
		le16_to_cpu(TX_RING_LEN_BITS | RX_RING_LEN_BITS);
	for (i = 0; i < 6; i++)
		lp->init_block.phys_addr[i] = dev->dev_addr[i];
	lp->init_block.rx_ring = (u32)le32_to_cpu(
		lp->dma_addr + offsetof(struct pcnet32_private, rx_ring));
	lp->init_block.tx_ring = (u32)le32_to_cpu(
		lp->dma_addr + offsetof(struct pcnet32_private, tx_ring));
	return 0;
}

/*** RTnet ***/
/*** RTnet ***/

static int pcnet32_start_xmit(struct rtskb *skb,
			      struct rtnet_device *dev) /*** RTnet ***/
{
	struct pcnet32_private *lp = dev->priv;
	unsigned long ioaddr = dev->base_addr;
	u16 status;
	int entry;
	rtdm_lockctx_t context;

	if (pcnet32_debug > 3) {
		pr_debug("%s: pcnet32_start_xmit() called, csr0 %4.4x.\n",
			 dev->name, lp->a.read_csr(ioaddr, 0));
	}

	/*** RTnet ***/
	rtdm_lock_get_irqsave(&lp->lock, context);
	/*** RTnet ***/

	/* Default status -- will not enable Successful-TxDone
     * interrupt when that option is available to us.
     */
	status = 0x8300;
	if ((lp->ltint) && ((lp->cur_tx - lp->dirty_tx == TX_RING_SIZE / 2) ||
			    (lp->cur_tx - lp->dirty_tx >= TX_RING_SIZE - 2))) {
		/* Enable Successful-TxDone interrupt if we have
	 * 1/2 of, or nearly all of, our ring buffer Tx'd
	 * but not yet cleaned up.  Thus, most of the time,
	 * we will not enable Successful-TxDone interrupts.
	 */
		status = 0x9300;
	}

	/* Fill in a Tx ring entry */

	/* Mask to ring buffer boundary. */
	entry = lp->cur_tx & TX_RING_MOD_MASK;

	/* Caution: the write order is important here, set the base address
       with the "ownership" bits last. */

	lp->tx_ring[entry].length = le16_to_cpu(-skb->len);

	lp->tx_ring[entry].misc = 0x00000000;

	lp->tx_skbuff[entry] = skb;
	lp->tx_dma_addr[entry] = dma_map_single(&lp->pci_dev->dev, skb->data,
						skb->len, DMA_TO_DEVICE);
	lp->tx_ring[entry].base = (u32)le32_to_cpu(lp->tx_dma_addr[entry]);

	/*** RTnet ***/
	/* get and patch time stamp just before the transmission */
	if (skb->xmit_stamp)
		*skb->xmit_stamp =
			cpu_to_be64(rtdm_clock_read() + *skb->xmit_stamp);
	/*** RTnet ***/

	wmb();
	lp->tx_ring[entry].status = le16_to_cpu(status);

	lp->cur_tx++;
	lp->stats.tx_bytes += skb->len;

	/* Trigger an immediate send poll. */
	lp->a.write_csr(ioaddr, 0, 0x0048);

	//dev->trans_start = jiffies; /*** RTnet ***/

	if (lp->tx_ring[(entry + 1) & TX_RING_MOD_MASK].base == 0)
		rtnetif_start_queue(dev); /*** RTnet ***/
	else {
		lp->tx_full = 1;
		rtnetif_stop_queue(dev); /*** RTnet ***/
	}
	/*** RTnet ***/
	rtdm_lock_put_irqrestore(&lp->lock, context);
	/*** RTnet ***/
	return 0;
}

/* The PCNET32 interrupt handler. */
static int pcnet32_interrupt(rtdm_irq_t *irq_handle) /*** RTnet ***/
{
	nanosecs_abs_t time_stamp = rtdm_clock_read(); /*** RTnet ***/
	struct rtnet_device *dev = rtdm_irq_get_arg(
		irq_handle, struct rtnet_device); /*** RTnet ***/
	struct pcnet32_private *lp;
	unsigned long ioaddr;
	u16 csr0, rap;
	int boguscnt = max_interrupt_work;
	int must_restart;
	unsigned int old_packet_cnt; /*** RTnet ***/
	int ret = RTDM_IRQ_NONE;

	/*** RTnet ***
	if (!dev) {
		pr_debug("%s(): irq %d for unknown device\n",
		__FUNCTION__, irq);
	return;
	}
	*** RTnet ***/

	ioaddr = dev->base_addr;
	lp = dev->priv;
	old_packet_cnt = lp->stats.rx_packets; /*** RTnet ***/

	rtdm_lock_get(&lp->lock); /*** RTnet ***/

	rap = lp->a.read_rap(ioaddr);
	while ((csr0 = lp->a.read_csr(ioaddr, 0)) & 0x8600 && --boguscnt >= 0) {
		/* Acknowledge all of the current interrupt sources ASAP. */
		lp->a.write_csr(ioaddr, 0, csr0 & ~0x004f);

		ret = RTDM_IRQ_HANDLED;

		must_restart = 0;

		if (pcnet32_debug > 5)
			pr_debug("%s: interrupt  csr0=%#2.2x new csr=%#2.2x.\n",
				 dev->name, csr0, lp->a.read_csr(ioaddr, 0));

		if (csr0 & 0x0400) /* Rx interrupt */
			pcnet32_rx(dev, &time_stamp);

		if (csr0 & 0x0200) { /* Tx-done interrupt */
			unsigned int dirty_tx = lp->dirty_tx;

			while (dirty_tx < lp->cur_tx) {
				int entry = dirty_tx & TX_RING_MOD_MASK;
				int status = (short)le16_to_cpu(
					lp->tx_ring[entry].status);

				if (status < 0)
					break; /* It still hasn't been Txed */

				lp->tx_ring[entry].base = 0;

				if (status & 0x4000) {
					/* There was an major error, log it. */
					int err_status = le32_to_cpu(
						lp->tx_ring[entry].misc);
					lp->stats.tx_errors++;
					if (err_status & 0x04000000)
						lp->stats.tx_aborted_errors++;
					if (err_status & 0x08000000)
						lp->stats.tx_carrier_errors++;
					if (err_status & 0x10000000)
						lp->stats.tx_window_errors++;
#ifndef DO_DXSUFLO
					if (err_status & 0x40000000) {
						lp->stats.tx_fifo_errors++;
						/* Ackk!  On FIFO errors the Tx unit is turned off! */
						/* Remove this verbosity later! */
						pr_err("%s: Tx FIFO error! CSR0=%4.4x\n",
						       dev->name, csr0);
						must_restart = 1;
					}
#else
					if (err_status & 0x40000000) {
						lp->stats.tx_fifo_errors++;
						if (!lp->dxsuflo) { /* If controller doesn't recover ... */
							/* Ackk!  On FIFO errors the Tx unit is turned off! */
							/* Remove this verbosity later! */
							pr_err("%s: Tx FIFO error! CSR0=%4.4x\n",
							       dev->name, csr0);
							must_restart = 1;
						}
					}
#endif
				} else {
					if (status & 0x1800)
						lp->stats.collisions++;
					lp->stats.tx_packets++;
				}

				/* We must free the original skb */
				if (lp->tx_skbuff[entry]) {
					dma_unmap_single(
						&lp->pci_dev->dev,
						lp->tx_dma_addr[entry],
						lp->tx_skbuff[entry]->len,
						DMA_TO_DEVICE);
					dev_kfree_rtskb(
						lp->tx_skbuff[entry]); /*** RTnet ***/
					lp->tx_skbuff[entry] = 0;
					lp->tx_dma_addr[entry] = 0;
				}
				dirty_tx++;
			}

			if (lp->cur_tx - dirty_tx >= TX_RING_SIZE) {
				pr_err("%s: out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
				       dev->name, dirty_tx, lp->cur_tx,
				       lp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}

			if (lp->tx_full &&
			    rtnetif_queue_stopped(dev) && /*** RTnet ***/
			    dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				lp->tx_full = 0;
				rtnetif_wake_queue(dev); /*** RTnet ***/
			}
			lp->dirty_tx = dirty_tx;
		}

		/* Log misc errors. */
		if (csr0 & 0x4000)
			lp->stats.tx_errors++; /* Tx babble. */
		if (csr0 & 0x1000) {
			/*
	     * this happens when our receive ring is full. This shouldn't
	     * be a problem as we will see normal rx interrupts for the frames
	     * in the receive ring. But there are some PCI chipsets (I can reproduce
	     * this on SP3G with Intel saturn chipset) which have sometimes problems
	     * and will fill up the receive ring with error descriptors. In this
	     * situation we don't get a rx interrupt, but a missed frame interrupt sooner
	     * or later. So we try to clean up our receive ring here.
	     */
			pcnet32_rx(dev, &time_stamp);
			lp->stats.rx_errors++; /* Missed a Rx frame. */
		}
		if (csr0 & 0x0800) {
			pr_err("%s: Bus master arbitration failure, status %4.4x.\n",
			       dev->name, csr0);
			/* unlike for the lance, there is no restart needed */
		}

		/*** RTnet ***/
		/*** RTnet ***/
	}

	/* Clear any other interrupt, and set interrupt enable. */
	lp->a.write_csr(ioaddr, 0, 0x7940);
	lp->a.write_rap(ioaddr, rap);

	if (pcnet32_debug > 4)
		pr_debug("%s: exiting interrupt, csr0=%#4.4x.\n", dev->name,
			 lp->a.read_csr(ioaddr, 0));

	/*** RTnet ***/
	rtdm_lock_put(&lp->lock);

	if (old_packet_cnt != lp->stats.rx_packets)
		rt_mark_stack_mgr(dev);

	return ret;
	/*** RTnet ***/
}

static int pcnet32_rx(struct rtnet_device *dev,
		      nanosecs_abs_t *time_stamp) /*** RTnet ***/
{
	struct pcnet32_private *lp = dev->priv;
	int entry = lp->cur_rx & RX_RING_MOD_MASK;

	/* If we own the next entry, it's a new packet. Send it up. */
	while ((short)le16_to_cpu(lp->rx_ring[entry].status) >= 0) {
		int status = (short)le16_to_cpu(lp->rx_ring[entry].status) >> 8;

		if (status != 0x03) { /* There was an error. */
			/*
	     * There is a tricky error noted by John Murphy,
	     * <murf@perftech.com> to Russ Nelson: Even with full-sized
	     * buffers it's possible for a jabber packet to use two
	     * buffers, with only the last correctly noting the error.
	     */
			if (status &
			    0x01) /* Only count a general error at the */
				lp->stats.rx_errors++; /* end of a packet.*/
			if (status & 0x20)
				lp->stats.rx_frame_errors++;
			if (status & 0x10)
				lp->stats.rx_over_errors++;
			if (status & 0x08)
				lp->stats.rx_crc_errors++;
			if (status & 0x04)
				lp->stats.rx_fifo_errors++;
			lp->rx_ring[entry].status &= le16_to_cpu(0x03ff);
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			short pkt_len =
				(le32_to_cpu(lp->rx_ring[entry].msg_length) &
				 0xfff) -
				4;
			struct rtskb *skb; /*** RTnet ***/

			if (pkt_len < 60) {
				pr_err("%s: Runt packet!\n", dev->name);
				lp->stats.rx_errors++;
			} else {
				/*** RTnet ***/
				/*int rx_in_place = 0;*/

				/*if (pkt_len > rx_copybreak)*/ {
					struct rtskb *newskb;

					if ((newskb = rtnetdev_alloc_rtskb(
						     dev, PKT_BUF_SZ))) {
						rtskb_reserve(newskb, 2);
						skb = lp->rx_skbuff[entry];
						dma_unmap_single(
							&lp->pci_dev->dev,
							lp->rx_dma_addr[entry],
							skb->len,
							DMA_FROM_DEVICE);
						rtskb_put(skb, pkt_len);
						lp->rx_skbuff[entry] = newskb;
						lp->rx_dma_addr
							[entry] = dma_map_single(
							&lp->pci_dev->dev,
							newskb->tail,
							newskb->len,
							DMA_FROM_DEVICE);
						lp->rx_ring[entry]
							.base = le32_to_cpu(
							lp->rx_dma_addr[entry]);
						/*rx_in_place = 1;*/
					} else
						skb = NULL;
				} /*else {
		    skb = dev_alloc_skb(pkt_len+2);
		}*/
				/*** RTnet ***/

				if (skb == NULL) {
					int i;
					pr_err("%s: Memory squeeze, deferring packet.\n",
					       dev->name);
					for (i = 0; i < RX_RING_SIZE; i++)
						if ((short)le16_to_cpu(
							    lp->rx_ring[(entry +
									 i) &
									RX_RING_MOD_MASK]
								    .status) <
						    0)
							break;

					if (i > RX_RING_SIZE - 2) {
						lp->stats.rx_dropped++;
						lp->rx_ring[entry].status |=
							le16_to_cpu(0x8000);
						lp->cur_rx++;
					}
					break;
				}
				/*** RTnet ***/
				lp->stats.rx_bytes += skb->len;
				skb->protocol = rt_eth_type_trans(skb, dev);
				skb->time_stamp = *time_stamp;
				rtnetif_rx(skb);
				///dev->last_rx = jiffies;
				/*** RTnet ***/
				lp->stats.rx_packets++;
			}
		}
		/*
	 * The docs say that the buffer length isn't touched, but Andrew Boyd
	 * of QNX reports that some revs of the 79C965 clear it.
	 */
		lp->rx_ring[entry].buf_length = le16_to_cpu(-PKT_BUF_SZ);
		lp->rx_ring[entry].status |= le16_to_cpu(0x8000);
		entry = (++lp->cur_rx) & RX_RING_MOD_MASK;
	}

	return 0;
}

static int pcnet32_close(struct rtnet_device *dev) /*** RTnet ***/
{
	unsigned long ioaddr = dev->base_addr;
	struct pcnet32_private *lp = dev->priv;
	int i;

	rtnetif_stop_queue(dev); /*** RTnet ***/

	lp->stats.rx_missed_errors = lp->a.read_csr(ioaddr, 112);

	if (pcnet32_debug > 1)
		pr_debug("%s: Shutting down ethercard, status was %2.2x.\n",
			 dev->name, lp->a.read_csr(ioaddr, 0));

	/* We stop the PCNET32 here -- it occasionally polls memory if we don't. */
	lp->a.write_csr(ioaddr, 0, 0x0004);

	/*
     * Switch back to 16bit mode to avoid problems with dumb
     * DOS packet driver after a warm reboot
     */
	lp->a.write_bcr(ioaddr, 20, 4);

	/*** RTnet ***/
	if ((i = rtdm_irq_free(&lp->irq_handle)) < 0)
		return i;

	rt_stack_disconnect(dev);
	/*** RTnet ***/

	/* free all allocated skbuffs */
	for (i = 0; i < RX_RING_SIZE; i++) {
		lp->rx_ring[i].status = 0;
		if (lp->rx_skbuff[i]) {
			dma_unmap_single(&lp->pci_dev->dev, lp->rx_dma_addr[i],
					 lp->rx_skbuff[i]->len,
					 DMA_FROM_DEVICE);
			dev_kfree_rtskb(lp->rx_skbuff[i]); /*** RTnet ***/
		}
		lp->rx_skbuff[i] = NULL;
		lp->rx_dma_addr[i] = 0;
	}

	for (i = 0; i < TX_RING_SIZE; i++) {
		if (lp->tx_skbuff[i]) {
			dma_unmap_single(&lp->pci_dev->dev, lp->tx_dma_addr[i],
					 lp->tx_skbuff[i]->len,
					 DMA_TO_DEVICE);
			dev_kfree_rtskb(lp->tx_skbuff[i]); /*** RTnet ***/
		}
		lp->tx_skbuff[i] = NULL;
		lp->tx_dma_addr[i] = 0;
	}

	return 0;
}

/*** RTnet ***/
static struct net_device_stats *pcnet32_get_stats(struct rtnet_device *rtdev)
{
	struct pcnet32_private *lp = rtdev->priv;
	unsigned long ioaddr = rtdev->base_addr;
	rtdm_lockctx_t context;
	u16 saved_addr;

	rtdm_lock_get_irqsave(&lp->lock, context);
	saved_addr = lp->a.read_rap(ioaddr);
	lp->stats.rx_missed_errors = lp->a.read_csr(ioaddr, 112);
	lp->a.write_rap(ioaddr, saved_addr);
	rtdm_lock_put_irqrestore(&lp->lock, context);

	return &lp->stats;
}

/*** RTnet ***/

static struct pci_driver pcnet32_driver = {
	name: DRV_NAME,
	probe: pcnet32_probe_pci,
	id_table: pcnet32_pci_tbl,
};

/* An additional parameter that may be passed in... */
static int local_debug = -1;
static int tx_start_pt = -1;

module_param_named(debug, local_debug, int, 0444);
MODULE_PARM_DESC(debug, DRV_NAME " debug level (0-6)");
module_param(max_interrupt_work, int, 0444);
MODULE_PARM_DESC(max_interrupt_work,
		 DRV_NAME " maximum events handled per interrupt");
/*** RTnet ***
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM_DESC(rx_copybreak, DRV_NAME " copy breakpoint for copy-only-tiny-frames");
 *** RTnet ***/
module_param(tx_start_pt, int, 0444);
MODULE_PARM_DESC(tx_start_pt, DRV_NAME " transmit start point (0-3)");
module_param(pcnet32vlb, int, 0444);
MODULE_PARM_DESC(pcnet32vlb, DRV_NAME " Vesa local bus (VLB) support (0/1)");
module_param_array(options, int, NULL, 0444);
MODULE_PARM_DESC(options, DRV_NAME " initial option setting(s) (0-15)");
module_param_array(full_duplex, int, NULL, 0444);
MODULE_PARM_DESC(full_duplex, DRV_NAME " full duplex setting(s) (1)");

MODULE_AUTHOR("Jan Kiszka");
MODULE_DESCRIPTION("RTnet Driver for PCnet32 and PCnetPCI based ethercards");
MODULE_LICENSE("GPL");

static int __init pcnet32_init_module(void)
{
	pr_info("%s", version);

	if (local_debug > 0)
		pcnet32_debug = local_debug;

	if ((tx_start_pt >= 0) && (tx_start_pt <= 3))
		tx_start = tx_start_pt;

	/* find the PCI devices */
	if (!pci_register_driver(&pcnet32_driver))
		pcnet32_have_pci = 1;

	/* should we find any remaining VLbus devices ? */
	if (pcnet32vlb)
		pcnet32_probe_vlbus();

	if (cards_found)
		pr_info("%d cards_found.\n", cards_found);

	return (pcnet32_have_pci + cards_found) ? 0 : -ENODEV;
}

static void __exit pcnet32_cleanup_module(void)
{
	struct rtnet_device *next_dev; /*** RTnet ***/

	/* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	while (pcnet32_dev) {
		struct pcnet32_private *lp = pcnet32_dev->priv;
		next_dev = lp->next;
		/*** RTnet ***/
		rt_unregister_rtnetdev(pcnet32_dev);
		rt_rtdev_disconnect(pcnet32_dev);
		/*** RTnet ***/
		release_region(pcnet32_dev->base_addr, PCNET32_TOTAL_SIZE);
		dma_free_coherent(&lp->pci_dev->dev, sizeof(*lp), lp,
				  lp->dma_addr);
		/*** RTnet ***/
		rtdev_free(pcnet32_dev);
		/*** RTnet ***/
		pcnet32_dev = next_dev;
	}

	if (pcnet32_have_pci)
		pci_unregister_driver(&pcnet32_driver);
}

module_init(pcnet32_init_module);
module_exit(pcnet32_cleanup_module);
