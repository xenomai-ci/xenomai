// SPDX-License-Identifier: GPL-2.0-only
/*
 * CAN driver PCI interface.
 *
 * Copyright (C) 2001-2021 PEAK System-Technik GmbH
 * Copyright (C) 2019-2021 Stephane Grosjean <s.grosjean@peak-system.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>

#include <rtdm/driver.h>

#include <rtdm/can.h>

#include "rtcan_dev.h"
#include "rtcan_raw.h"
#include "rtcan_peak_canfd_user.h"

#ifdef CONFIG_PCI_MSI
#define PCIEFD_USES_MSI
#endif

#ifndef struct_size
#define struct_size(p, member, n)	((n)*sizeof(*(p)->member) + \
					 sizeof(*(p)))
#endif

#define DRV_NAME			"xeno_peak_pciefd"

static char *pciefd_board_name = "PEAK-PCIe FD";

MODULE_AUTHOR("Stephane Grosjean <s.grosjean@peak-system.com>");
MODULE_DESCRIPTION("RTCAN driver for PEAK PCAN PCIe/M.2 FD family cards");
MODULE_LICENSE("GPL v2");

#define PEAK_PCI_VENDOR_ID	0x001c	/* The PCI device and vendor IDs */
#define PEAK_PCIEFD_ID		0x0013	/* for PCIe slot cards */
#define PCAN_CPCIEFD_ID		0x0014	/* for Compact-PCI Serial slot cards */
#define PCAN_PCIE104FD_ID	0x0017	/* for PCIe-104 Express slot cards */
#define PCAN_MINIPCIEFD_ID	0x0018	/* for mini-PCIe slot cards */
#define PCAN_PCIEFD_OEM_ID	0x0019	/* for PCIe slot OEM cards */
#define PCAN_M2_ID		0x001a	/* for M2 slot cards */

/* supported device ids. */
static const struct pci_device_id peak_pciefd_tbl[] = {
	{PEAK_PCI_VENDOR_ID, PEAK_PCIEFD_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PEAK_PCI_VENDOR_ID, PCAN_CPCIEFD_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PEAK_PCI_VENDOR_ID, PCAN_PCIE104FD_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PEAK_PCI_VENDOR_ID, PCAN_MINIPCIEFD_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PEAK_PCI_VENDOR_ID, PCAN_PCIEFD_OEM_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{PEAK_PCI_VENDOR_ID, PCAN_M2_ID, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};

MODULE_DEVICE_TABLE(pci, peak_pciefd_tbl);

/* PEAK PCIe board access description */
#define PCIEFD_BAR0_SIZE		(64 * 1024)
#define PCIEFD_RX_DMA_SIZE		(4 * 1024)
#define PCIEFD_TX_DMA_SIZE		(4 * 1024)

#define PCIEFD_TX_PAGE_SIZE		(2 * 1024)

/* System Control Registers */
#define PCIEFD_REG_SYS_CTL_SET		0x0000	/* set bits */
#define PCIEFD_REG_SYS_CTL_CLR		0x0004	/* clear bits */

/* Version info registers */
#define PCIEFD_REG_SYS_VER1		0x0040	/* version reg #1 */
#define PCIEFD_REG_SYS_VER2		0x0044	/* version reg #2 */

#define PCIEFD_FW_VERSION(x, y, z)	(((u32)(x) << 24) | \
					 ((u32)(y) << 16) | \
					 ((u32)(z) << 8))

/* System Control Registers Bits */
#define PCIEFD_SYS_CTL_TS_RST		0x00000001	/* timestamp clock */
#define PCIEFD_SYS_CTL_CLK_EN		0x00000002	/* system clock */

/* CAN-FD channel addresses */
#define PCIEFD_CANX_OFF(c)		(((c) + 1) * 0x1000)

#define PCIEFD_ECHO_SKB_MAX		PCANFD_ECHO_SKB_DEF

/* CAN-FD channel registers */
#define PCIEFD_REG_CAN_MISC		0x0000	/* Misc. control */
#define PCIEFD_REG_CAN_CLK_SEL		0x0008	/* Clock selector */
#define PCIEFD_REG_CAN_CMD_PORT_L	0x0010	/* 64-bits command port */
#define PCIEFD_REG_CAN_CMD_PORT_H	0x0014
#define PCIEFD_REG_CAN_TX_REQ_ACC	0x0020	/* Tx request accumulator */
#define PCIEFD_REG_CAN_TX_CTL_SET	0x0030	/* Tx control set register */
#define PCIEFD_REG_CAN_TX_CTL_CLR	0x0038	/* Tx control clear register */
#define PCIEFD_REG_CAN_TX_DMA_ADDR_L	0x0040	/* 64-bits addr for Tx DMA */
#define PCIEFD_REG_CAN_TX_DMA_ADDR_H	0x0044
#define PCIEFD_REG_CAN_RX_CTL_SET	0x0050	/* Rx control set register */
#define PCIEFD_REG_CAN_RX_CTL_CLR	0x0058	/* Rx control clear register */
#define PCIEFD_REG_CAN_RX_CTL_WRT	0x0060	/* Rx control write register */
#define PCIEFD_REG_CAN_RX_CTL_ACK	0x0068	/* Rx control ACK register */
#define PCIEFD_REG_CAN_RX_DMA_ADDR_L	0x0070	/* 64-bits addr for Rx DMA */
#define PCIEFD_REG_CAN_RX_DMA_ADDR_H	0x0074

/* CAN-FD channel misc register bits */
#define CANFD_MISC_TS_RST		0x00000001	/* timestamp cnt rst */

/* CAN-FD channel Clock SELector Source & DIVider */
#define CANFD_CLK_SEL_DIV_MASK		0x00000007
#define CANFD_CLK_SEL_DIV_60MHZ		0x00000000	/* SRC=240MHz only */
#define CANFD_CLK_SEL_DIV_40MHZ		0x00000001	/* SRC=240MHz only */
#define CANFD_CLK_SEL_DIV_30MHZ		0x00000002	/* SRC=240MHz only */
#define CANFD_CLK_SEL_DIV_24MHZ		0x00000003	/* SRC=240MHz only */
#define CANFD_CLK_SEL_DIV_20MHZ		0x00000004	/* SRC=240MHz only */

#define CANFD_CLK_SEL_SRC_MASK		0x00000008	/* 0=80MHz, 1=240MHz */
#define CANFD_CLK_SEL_SRC_240MHZ	0x00000008
#define CANFD_CLK_SEL_SRC_80MHZ		(~CANFD_CLK_SEL_SRC_240MHZ & \
					 CANFD_CLK_SEL_SRC_MASK)

#define CANFD_CLK_SEL_20MHZ		(CANFD_CLK_SEL_SRC_240MHZ |\
					 CANFD_CLK_SEL_DIV_20MHZ)
#define CANFD_CLK_SEL_24MHZ		(CANFD_CLK_SEL_SRC_240MHZ |\
					 CANFD_CLK_SEL_DIV_24MHZ)
#define CANFD_CLK_SEL_30MHZ		(CANFD_CLK_SEL_SRC_240MHZ |\
					 CANFD_CLK_SEL_DIV_30MHZ)
#define CANFD_CLK_SEL_40MHZ		(CANFD_CLK_SEL_SRC_240MHZ |\
					 CANFD_CLK_SEL_DIV_40MHZ)
#define CANFD_CLK_SEL_60MHZ		(CANFD_CLK_SEL_SRC_240MHZ |\
					 CANFD_CLK_SEL_DIV_60MHZ)
#define CANFD_CLK_SEL_80MHZ		(CANFD_CLK_SEL_SRC_80MHZ)

/* CAN-FD channel Rx/Tx control register bits */
#define CANFD_CTL_UNC_BIT		0x00010000	/* Uncached DMA mem */
#define CANFD_CTL_RST_BIT		0x00020000	/* reset DMA action */
#define CANFD_CTL_IEN_BIT		0x00040000	/* IRQ enable */

/* Rx IRQ Count and Time Limits */
#define CANFD_CTL_IRQ_CL_DEF	8	/* Rx msg max nb per IRQ in Rx DMA */
#define CANFD_CTL_IRQ_TL_DEF	5	/* Time before IRQ if < CL (x100 us) */

#define CANFD_OPTIONS_SET	(CANFD_OPTION_ERROR | CANFD_OPTION_BUSLOAD)

/* Tx anticipation window (link logical address should be aligned on 2K
 * boundary)
 */
#define PCIEFD_TX_PAGE_COUNT	(PCIEFD_TX_DMA_SIZE / PCIEFD_TX_PAGE_SIZE)

#define CANFD_MSG_LNK_TX	0x1001	/* Tx msgs link */

/* 32-bit IRQ status fields, heading Rx DMA area */
static inline int pciefd_irq_tag(u32 irq_status)
{
	return irq_status & 0x0000000f;
}

static inline int pciefd_irq_rx_cnt(u32 irq_status)
{
	return (irq_status & 0x000007f0) >> 4;
}

static inline int pciefd_irq_is_lnk(u32 irq_status)
{
	return irq_status & 0x00010000;
}

/* Rx record */
struct pciefd_rx_dma {
	__le32 irq_status;
	__le32 sys_time_low;
	__le32 sys_time_high;
	struct pucan_rx_msg msg[0];
} __packed __aligned(4);

/* Tx Link record */
struct pciefd_tx_link {
	__le16 size;
	__le16 type;
	__le32 laddr_lo;
	__le32 laddr_hi;
} __packed __aligned(4);

/* Tx page descriptor */
struct pciefd_page {
	void *vbase;			/* page virtual address */
	dma_addr_t lbase;		/* page logical address */
	u32 offset;
	u32 size;
};

/* CAN channel object */
struct pciefd_board;
struct pciefd_can {
	struct peak_canfd_priv ucan;	/* must be the first member */
	void __iomem *reg_base;		/* channel config base addr */
	struct pciefd_board *board;	/* reverse link */

	struct pucan_command pucan_cmd;	/* command buffer */

	dma_addr_t rx_dma_laddr;	/* DMA virtual and logical addr */
	void *rx_dma_vaddr;		/* for Rx and Tx areas */
	dma_addr_t tx_dma_laddr;
	void *tx_dma_vaddr;

	struct pciefd_page tx_pages[PCIEFD_TX_PAGE_COUNT];
	u16 tx_pages_free;		/* free Tx pages counter */
	u16 tx_page_index;		/* current page used for Tx */
	rtdm_lock_t tx_lock;
	u32 irq_status;
	u32 irq_tag;			/* next irq tag */
	int irq;

	u32 flags;
};

/* PEAK-PCIe FD board object */
struct pciefd_board {
	void __iomem *reg_base;
	struct pci_dev *pci_dev;
	int can_count;
	int irq_flags;			/* RTDM_IRQTYPE_SHARED or 0 */
	rtdm_lock_t cmd_lock;		/* 64-bits cmds must be atomic */
	struct pciefd_can *can[0];	/* array of network devices */
};

#define CANFD_CTL_IRQ_CL_MIN	1
#define CANFD_CTL_IRQ_CL_MAX	127	/* 7-bit field */

#define CANFD_CTL_IRQ_TL_MIN	1
#define CANFD_CTL_IRQ_TL_MAX	15	/* 4-bit field */

static uint irqcl = CANFD_CTL_IRQ_CL_DEF;
module_param(irqcl, uint, 0644);
MODULE_PARM_DESC(irqcl,
" PCIe FD IRQ Count Limit (default=" __stringify(CANFD_CTL_IRQ_CL_DEF) ")");

static uint irqtl = CANFD_CTL_IRQ_TL_DEF;
module_param(irqtl, uint, 0644);
MODULE_PARM_DESC(irqtl,
" PCIe FD IRQ Time Limit (default=" __stringify(CANFD_CTL_IRQ_TL_DEF) ")");

#ifdef PCIEFD_USES_MSI

/* default behaviour: run in MSI mode (one IRQ per channel) */
#define PCIEFD_USEMSI_DEFAULT	1

static uint usemsi = PCIEFD_USEMSI_DEFAULT;
module_param(usemsi, uint, 0644);
MODULE_PARM_DESC(usemsi,
" 0=INTA; 1=MSI (def=" __stringify(PCIEFD_USEMSI_DEFAULT) ")");
#endif

/* read a 32 bit value from a SYS block register */
static inline u32 pciefd_sys_readreg(const struct pciefd_board *priv, u16 reg)
{
	return readl(priv->reg_base + reg);
}

/* write a 32 bit value into a SYS block register */
static inline void pciefd_sys_writereg(const struct pciefd_board *priv,
				       u32 val, u16 reg)
{
	writel(val, priv->reg_base + reg);
}

/* read a 32 bits value from CAN-FD block register */
static inline u32 pciefd_can_readreg(const struct pciefd_can *priv, u16 reg)
{
	return readl(priv->reg_base + reg);
}

/* write a 32 bits value into a CAN-FD block register */
static inline void pciefd_can_writereg(const struct pciefd_can *priv,
				       u32 val, u16 reg)
{
	writel(val, priv->reg_base + reg);
}

/* give a channel logical Rx DMA address to the board */
static void pciefd_can_setup_rx_dma(struct pciefd_can *priv)
{
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	const u32 dma_addr_h = (u32)(priv->rx_dma_laddr >> 32);
#else
	const u32 dma_addr_h = 0;
#endif

	/* (DMA must be reset for Rx) */
	pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
			    PCIEFD_REG_CAN_RX_CTL_SET);

	/* write the logical address of the Rx DMA area for this channel */
	pciefd_can_writereg(priv, (u32)priv->rx_dma_laddr,
			    PCIEFD_REG_CAN_RX_DMA_ADDR_L);
	pciefd_can_writereg(priv, dma_addr_h, PCIEFD_REG_CAN_RX_DMA_ADDR_H);

	/* also indicates that Rx DMA is cacheable */
	pciefd_can_writereg(priv, CANFD_CTL_UNC_BIT,
			    PCIEFD_REG_CAN_RX_CTL_CLR);
}

/* clear channel logical Rx DMA address from the board */
static void pciefd_can_clear_rx_dma(struct pciefd_can *priv)
{
	/* DMA must be reset for Rx */
	pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
			    PCIEFD_REG_CAN_RX_CTL_SET);

	/* clear the logical address of the Rx DMA area for this channel */
	pciefd_can_writereg(priv, 0, PCIEFD_REG_CAN_RX_DMA_ADDR_L);
	pciefd_can_writereg(priv, 0, PCIEFD_REG_CAN_RX_DMA_ADDR_H);
}

/* give a channel logical Tx DMA address to the board */
static void pciefd_can_setup_tx_dma(struct pciefd_can *priv)
{
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	const u32 dma_addr_h = (u32)(priv->tx_dma_laddr >> 32);
#else
	const u32 dma_addr_h = 0;
#endif

	/* (DMA must be reset for Tx) */
	pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
			    PCIEFD_REG_CAN_TX_CTL_SET);

	/* write the logical address of the Tx DMA area for this channel */
	pciefd_can_writereg(priv, (u32)priv->tx_dma_laddr,
			    PCIEFD_REG_CAN_TX_DMA_ADDR_L);
	pciefd_can_writereg(priv, dma_addr_h, PCIEFD_REG_CAN_TX_DMA_ADDR_H);

	/* also indicates that Tx DMA is cacheable */
	pciefd_can_writereg(priv, CANFD_CTL_UNC_BIT,
			    PCIEFD_REG_CAN_TX_CTL_CLR);
}

/* clear channel logical Tx DMA address from the board */
static void pciefd_can_clear_tx_dma(struct pciefd_can *priv)
{
	/* DMA must be reset for Tx */
	pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
			    PCIEFD_REG_CAN_TX_CTL_SET);

	/* clear the logical address of the Tx DMA area for this channel */
	pciefd_can_writereg(priv, 0, PCIEFD_REG_CAN_TX_DMA_ADDR_L);
	pciefd_can_writereg(priv, 0, PCIEFD_REG_CAN_TX_DMA_ADDR_H);
}

/* acknowledge interrupt to the device */
static void pciefd_can_ack_rx_dma(struct pciefd_can *priv)
{
	/* read value of current IRQ tag and inc it for next one */
	priv->irq_tag = le32_to_cpu(*(__le32 *)priv->rx_dma_vaddr);
	priv->irq_tag++;
	priv->irq_tag &= 0xf;

	/* write the next IRQ tag for this CAN */
	pciefd_can_writereg(priv, priv->irq_tag, PCIEFD_REG_CAN_RX_CTL_ACK);
}

/* IRQ handler */
static int pciefd_irq_handler(rtdm_irq_t *irq_handle)
{
	struct pciefd_can *priv = rtdm_irq_get_arg(irq_handle, void);
	struct pciefd_rx_dma *rx_dma = priv->rx_dma_vaddr;

	/* INTA mode only, dummy read to sync with PCIe transaction */
	if (!pci_dev_msi_enabled(priv->board->pci_dev))
		(void)pciefd_sys_readreg(priv->board, PCIEFD_REG_SYS_VER1);

	/* read IRQ status from the first 32-bit of the Rx DMA area */
	priv->irq_status = le32_to_cpu(rx_dma->irq_status);

	/* check if this (shared) IRQ is for this CAN */
	if (pciefd_irq_tag(priv->irq_status) != priv->irq_tag)
		return RTDM_IRQ_NONE;

	/* handle rx messages (if any) */
	peak_canfd_handle_msgs_list(&priv->ucan,
				    rx_dma->msg,
				    pciefd_irq_rx_cnt(priv->irq_status));

	/* handle tx link interrupt (if any) */
	if (pciefd_irq_is_lnk(priv->irq_status)) {
		rtdm_lock_get(&priv->tx_lock);
		priv->tx_pages_free++;
		rtdm_lock_put(&priv->tx_lock);

		/* Wake up a sender */
		rtdm_sem_up(&priv->ucan.rdev->tx_sem);
	}

	/* re-enable Rx DMA transfer for this CAN */
	pciefd_can_ack_rx_dma(priv);

	return RTDM_IRQ_HANDLED;
}

/* initialize structures used for sending CAN frames */
static int pciefd_enable_tx_path(struct peak_canfd_priv *ucan)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	int i;

	/* initialize the Tx pages descriptors */
	priv->tx_pages_free = PCIEFD_TX_PAGE_COUNT - 1;
	priv->tx_page_index = 0;

	priv->tx_pages[0].vbase = priv->tx_dma_vaddr;
	priv->tx_pages[0].lbase = priv->tx_dma_laddr;

	for (i = 0; i < PCIEFD_TX_PAGE_COUNT; i++) {
		priv->tx_pages[i].offset = 0;
		priv->tx_pages[i].size = PCIEFD_TX_PAGE_SIZE -
					 sizeof(struct pciefd_tx_link);
		if (i) {
			priv->tx_pages[i].vbase =
					  priv->tx_pages[i - 1].vbase +
					  PCIEFD_TX_PAGE_SIZE;
			priv->tx_pages[i].lbase =
					  priv->tx_pages[i - 1].lbase +
					  PCIEFD_TX_PAGE_SIZE;
		}
	}

	/* setup Tx DMA addresses into IP core */
	pciefd_can_setup_tx_dma(priv);

	/* start (TX_RST=0) Tx Path */
	pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
			    PCIEFD_REG_CAN_TX_CTL_CLR);

	return 0;
}

/* board specific command pre-processing */
static int pciefd_pre_cmd(struct peak_canfd_priv *ucan)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	u16 cmd = pucan_cmd_get_opcode(&priv->pucan_cmd);

	/* pre-process command */
	switch (cmd) {
	case PUCAN_CMD_NORMAL_MODE:
	case PUCAN_CMD_LISTEN_ONLY_MODE:

		if (ucan->rdev->state == CAN_STATE_BUS_OFF)
			break;

		/* setup Rx DMA address */
		pciefd_can_setup_rx_dma(priv);

		/* setup max count of msgs per IRQ */
		pciefd_can_writereg(priv, (irqtl << 8) | irqcl,
				    PCIEFD_REG_CAN_RX_CTL_WRT);

		/* clear DMA RST for Rx (Rx start) */
		pciefd_can_writereg(priv, CANFD_CTL_RST_BIT,
				    PCIEFD_REG_CAN_RX_CTL_CLR);

		/* reset timestamps */
		pciefd_can_writereg(priv, !CANFD_MISC_TS_RST,
				    PCIEFD_REG_CAN_MISC);

		/* do an initial ACK */
		pciefd_can_ack_rx_dma(priv);

		/* enable IRQ for this CAN after having set next irq_tag */
		pciefd_can_writereg(priv, CANFD_CTL_IEN_BIT,
				    PCIEFD_REG_CAN_RX_CTL_SET);

		/* Tx path will be setup as soon as RX_BARRIER is received */
		break;
	default:
		break;
	}

	return 0;
}

/* write a command */
static int pciefd_write_cmd(struct peak_canfd_priv *ucan)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	unsigned long flags;

	/* 64-bit command must be atomic */
	rtdm_lock_get_irqsave(&priv->board->cmd_lock, flags);

	pciefd_can_writereg(priv, *(u32 *)ucan->cmd_buffer,
			    PCIEFD_REG_CAN_CMD_PORT_L);
	pciefd_can_writereg(priv, *(u32 *)(ucan->cmd_buffer + 4),
			    PCIEFD_REG_CAN_CMD_PORT_H);

	rtdm_lock_put_irqrestore(&priv->board->cmd_lock, flags);

	return 0;
}

/* board specific command post-processing */
static int pciefd_post_cmd(struct peak_canfd_priv *ucan)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	u16 cmd = pucan_cmd_get_opcode(&priv->pucan_cmd);

	switch (cmd) {
	case PUCAN_CMD_RESET_MODE:

		if (ucan->rdev->state == CAN_STATE_STOPPED)
			break;

		/* controller now in reset mode: disable IRQ for this CAN */
		pciefd_can_writereg(priv, CANFD_CTL_IEN_BIT,
				    PCIEFD_REG_CAN_RX_CTL_CLR);

		/* stop and reset DMA addresses in Tx/Rx engines */
		pciefd_can_clear_tx_dma(priv);
		pciefd_can_clear_rx_dma(priv);

		/* wait for above commands to complete (read cycle) */
		(void)pciefd_sys_readreg(priv->board, PCIEFD_REG_SYS_VER1);

		ucan->rdev->state = CAN_STATE_STOPPED;

		break;
	}

	return 0;
}

/* allocate enough room into the Tx dma area to store a CAN message */
static void *pciefd_alloc_tx_msg(struct peak_canfd_priv *ucan, u16 msg_size,
				 int *room_left)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	struct pciefd_page *page = priv->tx_pages + priv->tx_page_index;
	unsigned long flags;
	void *msg;

	rtdm_lock_get_irqsave(&priv->tx_lock, flags);

	if (page->offset + msg_size > page->size) {
		struct pciefd_tx_link *lk;

		/* not enough space in this page: try another one */
		if (!priv->tx_pages_free) {
			rtdm_lock_put_irqrestore(&priv->tx_lock, flags);

			/* Tx overflow */
			return NULL;
		}

		priv->tx_pages_free--;

		/* keep address of the very last free slot of current page */
		lk = page->vbase + page->offset;

		/* next, move on a new free page */
		priv->tx_page_index = (priv->tx_page_index + 1) %
				      PCIEFD_TX_PAGE_COUNT;
		page = priv->tx_pages + priv->tx_page_index;

		/* put link record to this new page at the end of prev one */
		lk->size = cpu_to_le16(sizeof(*lk));
		lk->type = cpu_to_le16(CANFD_MSG_LNK_TX);
		lk->laddr_lo = cpu_to_le32(page->lbase);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		lk->laddr_hi = cpu_to_le32(page->lbase >> 32);
#else
		lk->laddr_hi = 0;
#endif
		/* next msgs will be put from the begininng of this new page */
		page->offset = 0;
	}

	*room_left = priv->tx_pages_free * page->size;

	rtdm_lock_put_irqrestore(&priv->tx_lock, flags);

	msg = page->vbase + page->offset;

	/* give back room left in the tx ring */
	*room_left += page->size - (page->offset + msg_size);

	return msg;
}

/* tell the IP core tha a frame has been written into the Tx DMA area */
static int pciefd_write_tx_msg(struct peak_canfd_priv *ucan,
			       struct pucan_tx_msg *msg)
{
	struct pciefd_can *priv = (struct pciefd_can *)ucan;
	struct pciefd_page *page = priv->tx_pages + priv->tx_page_index;

	/* this slot is now reserved for writing the frame */
	page->offset += le16_to_cpu(msg->size);

	/* tell the board a frame has been written in Tx DMA area */
	pciefd_can_writereg(priv, 1, PCIEFD_REG_CAN_TX_REQ_ACC);

	return 0;
}

/* probe for CAN channel number #pciefd_board->can_count */
static int pciefd_can_probe(struct pciefd_board *pciefd)
{
	struct rtcan_device *rdev;
	struct pciefd_can *priv;
	u32 clk;
	int err;

	/* allocate the RTCAN object */
	rdev = alloc_peak_canfd_dev(sizeof(*priv), pciefd->can_count);
	if (!rdev) {
		dev_err(&pciefd->pci_dev->dev,
			"failed to alloc RTCAN device object\n");
		goto failure;
	}

	/* fill-in board specific parts */
	rdev->board_name = pciefd_board_name;

	/* fill-in rtcan private object */
	priv = rdev->priv;

	/* setup PCIe-FD own callbacks */
	priv->ucan.pre_cmd = pciefd_pre_cmd;
	priv->ucan.write_cmd = pciefd_write_cmd;
	priv->ucan.post_cmd = pciefd_post_cmd;
	priv->ucan.enable_tx_path = pciefd_enable_tx_path;
	priv->ucan.alloc_tx_msg = pciefd_alloc_tx_msg;
	priv->ucan.write_tx_msg = pciefd_write_tx_msg;

	/* setup PCIe-FD own command buffer */
	priv->ucan.cmd_buffer = &priv->pucan_cmd;
	priv->ucan.cmd_maxlen = sizeof(priv->pucan_cmd);

	priv->board = pciefd;

	/* CAN config regs block address */
	priv->reg_base = pciefd->reg_base + PCIEFD_CANX_OFF(priv->ucan.index);
	rdev->base_addr = (unsigned long)priv->reg_base;

	/* allocate non-cacheable DMA'able 4KB memory area for Rx */
	priv->rx_dma_vaddr = dmam_alloc_coherent(&pciefd->pci_dev->dev,
						 PCIEFD_RX_DMA_SIZE,
						 &priv->rx_dma_laddr,
						 GFP_KERNEL);
	if (!priv->rx_dma_vaddr) {
		dev_err(&pciefd->pci_dev->dev,
			"Rx dmam_alloc_coherent(%u) failure\n",
			PCIEFD_RX_DMA_SIZE);
		goto err_free_rtdev;
	}

	/* allocate non-cacheable DMA'able 4KB memory area for Tx */
	priv->tx_dma_vaddr = dmam_alloc_coherent(&pciefd->pci_dev->dev,
						 PCIEFD_TX_DMA_SIZE,
						 &priv->tx_dma_laddr,
						 GFP_KERNEL);
	if (!priv->tx_dma_vaddr) {
		dev_err(&pciefd->pci_dev->dev,
			"Tx dmam_alloc_coherent(%u) failure\n",
			PCIEFD_TX_DMA_SIZE);
		goto err_free_rtdev;
	}

	/* CAN clock in RST mode */
	pciefd_can_writereg(priv, CANFD_MISC_TS_RST, PCIEFD_REG_CAN_MISC);

	/* read current clock value */
	clk = pciefd_can_readreg(priv, PCIEFD_REG_CAN_CLK_SEL);
	switch (clk) {
	case CANFD_CLK_SEL_20MHZ:
		priv->ucan.rdev->can_sys_clock = 20 * 1000 * 1000;
		break;
	case CANFD_CLK_SEL_24MHZ:
		priv->ucan.rdev->can_sys_clock = 24 * 1000 * 1000;
		break;
	case CANFD_CLK_SEL_30MHZ:
		priv->ucan.rdev->can_sys_clock = 30 * 1000 * 1000;
		break;
	case CANFD_CLK_SEL_40MHZ:
		priv->ucan.rdev->can_sys_clock = 40 * 1000 * 1000;
		break;
	case CANFD_CLK_SEL_60MHZ:
		priv->ucan.rdev->can_sys_clock = 60 * 1000 * 1000;
		break;
	default:
		pciefd_can_writereg(priv, CANFD_CLK_SEL_80MHZ,
				    PCIEFD_REG_CAN_CLK_SEL);

		fallthrough;
	case CANFD_CLK_SEL_80MHZ:
		priv->ucan.rdev->can_sys_clock = 80 * 1000 * 1000;
		break;
	}

#ifdef PCIEFD_USES_MSI
	priv->irq = (pciefd->irq_flags & RTDM_IRQTYPE_SHARED) ?
		    pciefd->pci_dev->irq :
		    pci_irq_vector(pciefd->pci_dev, priv->ucan.index);
#else
	priv->irq = pciefd->pci_dev->irq;
#endif

	/* setup irq handler */
	err = rtdm_irq_request(&rdev->irq_handle,
			       priv->irq,
			       pciefd_irq_handler,
			       pciefd->irq_flags,
			       DRV_NAME,
			       priv);
	if (err) {
		dev_err(&pciefd->pci_dev->dev,
			"rtdm_irq_request(IRQ%u) failure err %d\n",
			priv->irq, err);
		goto err_free_rtdev;
	}

	err = rtcan_dev_register(rdev);
	if (err) {
		dev_err(&pciefd->pci_dev->dev,
			"couldn't register RTCAN device: %d\n", err);
		goto err_free_irq;
	}

	rtdm_lock_init(&priv->tx_lock);

	/* save the object address in the board structure */
	pciefd->can[pciefd->can_count] = priv;

	dev_info(&pciefd->pci_dev->dev, "%s at reg_base=0x%p irq=%d\n",
		 rdev->name, priv->reg_base, priv->irq);

	return 0;

err_free_irq:
	rtdm_irq_free(&rdev->irq_handle);

err_free_rtdev:
	rtcan_dev_free(rdev);

failure:
	return -ENOMEM;
}

/* wakeup all RT tasks that are blocked on read */
static void pciefd_can_unlock_recv_tasks(struct rtcan_device *rdev)
{
	struct rtcan_recv *recv_listener = rdev->recv_list;

	while (recv_listener) {
		struct rtcan_socket *sock = recv_listener->sock;

		/* wakeup any rx task */
		rtdm_sem_destroy(&sock->recv_sem);

		recv_listener = recv_listener->next;
	}
}

/* remove a CAN-FD channel by releasing all of its resources */
static void pciefd_can_remove(struct pciefd_can *priv)
{
	struct rtcan_device *rdev = priv->ucan.rdev;

	/* unlock any tasks that wait for read on a socket bound to this CAN */
	pciefd_can_unlock_recv_tasks(rdev);

	/* in case the driver is removed when the interface is UP
	 * (device MUST be closed before being unregistered)
	 */
	rdev->do_set_mode(rdev, CAN_MODE_STOP, NULL);

	rtcan_dev_unregister(rdev);
	rtdm_irq_disable(&rdev->irq_handle);
	rtdm_irq_free(&rdev->irq_handle);
	rtcan_dev_free(rdev);
}

/* remove all CAN-FD channels by releasing their own resources */
static void pciefd_can_remove_all(struct pciefd_board *pciefd)
{
	while (pciefd->can_count > 0)
		pciefd_can_remove(pciefd->can[--pciefd->can_count]);
}

/* probe for the entire device */
static int peak_pciefd_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	struct pciefd_board *pciefd;
	int err, can_count;
	u16 sub_sys_id;
	u8 hw_ver_major;
	u8 hw_ver_minor;
	u8 hw_ver_sub;
	u32 v2;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto err_disable_pci;

	/* the number of channels depends on sub-system id */
	err = pci_read_config_word(pdev, PCI_SUBSYSTEM_ID, &sub_sys_id);
	if (err)
		goto err_release_regions;

	dev_dbg(&pdev->dev, "probing device %04x:%04x:%04x\n",
		pdev->vendor, pdev->device, sub_sys_id);

	if (sub_sys_id >= 0x0012)
		can_count = 4;
	else if (sub_sys_id >= 0x0010)
		can_count = 3;
	else if (sub_sys_id >= 0x0004)
		can_count = 2;
	else
		can_count = 1;

	/* allocate board structure object */
	pciefd = devm_kzalloc(&pdev->dev, struct_size(pciefd, can, can_count),
			      GFP_KERNEL);
	if (!pciefd) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* initialize the board structure */
	pciefd->pci_dev = pdev;
	rtdm_lock_init(&pciefd->cmd_lock);

	/* save the PCI BAR0 virtual address for further system regs access */
	pciefd->reg_base = pci_iomap(pdev, 0, PCIEFD_BAR0_SIZE);
	if (!pciefd->reg_base) {
		dev_err(&pdev->dev, "failed to map PCI resource #0\n");
		err = -ENOMEM;
		goto err_release_regions;
	}

	/* read the firmware version number */
	v2 = pciefd_sys_readreg(pciefd, PCIEFD_REG_SYS_VER2);

	hw_ver_major = (v2 & 0x0000f000) >> 12;
	hw_ver_minor = (v2 & 0x00000f00) >> 8;
	hw_ver_sub = (v2 & 0x000000f0) >> 4;

	dev_info(&pdev->dev,
		 "%ux CAN-FD PCAN-PCIe FPGA v%u.%u.%u:\n", can_count,
		 hw_ver_major, hw_ver_minor, hw_ver_sub);

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	/* DMA logic doesn't handle mix of 32-bit and 64-bit logical addresses
	 * in fw <= 3.2.x
	 */
	if (PCIEFD_FW_VERSION(hw_ver_major, hw_ver_minor, hw_ver_sub) <
		PCIEFD_FW_VERSION(3, 3, 0)) {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err)
			dev_warn(&pdev->dev,
				"warning: can't set DMA mask %llxh (err %d)\n",
				DMA_BIT_MASK(32), err);
	}
#endif

	/* default interrupt mode is: shared INTx */
	pciefd->irq_flags = RTDM_IRQTYPE_SHARED;

#ifdef PCIEFD_USES_MSI
	if (usemsi) {
		err = pci_msi_vec_count(pdev);
		if (err > 0) {
			int msi_maxvec = err;

			err = pci_alloc_irq_vectors_affinity(pdev, can_count,
							     msi_maxvec,
							     PCI_IRQ_MSI,
							     NULL);
			dev_info(&pdev->dev,
				 "MSI[%u..%u] enabling status: %d\n",
				 can_count, msi_maxvec, err);

			/* if didn't get the requested count of MSI, fall back
			 * to INTx
			 */
			if (err >= can_count)
				pciefd->irq_flags &= ~RTDM_IRQTYPE_SHARED;
			else if (err >= 0)
				pci_free_irq_vectors(pdev);
		}
	}
#endif

	/* stop system clock */
	pciefd_sys_writereg(pciefd, PCIEFD_SYS_CTL_CLK_EN,
			    PCIEFD_REG_SYS_CTL_CLR);

	pci_set_master(pdev);

	/* create now the corresponding channels objects */
	while (pciefd->can_count < can_count) {
		err = pciefd_can_probe(pciefd);
		if (err)
			goto err_free_canfd;

		pciefd->can_count++;
	}

	/* set system timestamps counter in RST mode */
	pciefd_sys_writereg(pciefd, PCIEFD_SYS_CTL_TS_RST,
			    PCIEFD_REG_SYS_CTL_SET);

	/* wait a bit (read cycle) */
	(void)pciefd_sys_readreg(pciefd, PCIEFD_REG_SYS_VER1);

	/* free all clocks */
	pciefd_sys_writereg(pciefd, PCIEFD_SYS_CTL_TS_RST,
			    PCIEFD_REG_SYS_CTL_CLR);

	/* start system clock */
	pciefd_sys_writereg(pciefd, PCIEFD_SYS_CTL_CLK_EN,
			    PCIEFD_REG_SYS_CTL_SET);

	/* remember the board structure address in the device user data */
	pci_set_drvdata(pdev, pciefd);

	return 0;

err_free_canfd:
	pciefd_can_remove_all(pciefd);

#ifdef PCIEFD_USES_MSI
	pci_free_irq_vectors(pdev);
#endif
	pci_iounmap(pdev, pciefd->reg_base);

err_release_regions:
	pci_release_regions(pdev);

err_disable_pci:
	pci_disable_device(pdev);

	/* pci_xxx_config_word() return positive PCIBIOS_xxx error codes while
	 * the probe() function must return a negative errno in case of failure
	 * (err is unchanged if negative)
	 */
	return pcibios_err_to_errno(err);
}

/* free the board structure object, as well as its resources: */
static void peak_pciefd_remove(struct pci_dev *pdev)
{
	struct pciefd_board *pciefd = pci_get_drvdata(pdev);

	/* release CAN-FD channels resources */
	pciefd_can_remove_all(pciefd);

#ifdef PCIEFD_USES_MSI
	pci_free_irq_vectors(pdev);
#endif
	pci_iounmap(pdev, pciefd->reg_base);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver rtcan_peak_pciefd_driver = {
	.name = DRV_NAME,
	.id_table = peak_pciefd_tbl,
	.probe = peak_pciefd_probe,
	.remove = peak_pciefd_remove,
};

static int __init rtcan_peak_pciefd_init(void)
{
	if (!realtime_core_enabled())
		return 0;

	return pci_register_driver(&rtcan_peak_pciefd_driver);
}

static void __exit rtcan_peak_pciefd_exit(void)
{
	if (realtime_core_enabled())
		pci_unregister_driver(&rtcan_peak_pciefd_driver);
}

module_init(rtcan_peak_pciefd_init);
module_exit(rtcan_peak_pciefd_exit);
