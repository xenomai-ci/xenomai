/*
	drivers/net/tulip/media.c

	Maintained by Jeff Garzik <jgarzik@mandrakesoft.com>
	Copyright 2000,2001  The Linux Kernel Team
	Written/copyright 1994-2001 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Please refer to Documentation/DocBook/tulip.{pdf,ps,html}
	for more information on this driver, or visit the project
	Web page at http://sourceforge.net/projects/tulip/

*/
/* Ported to RTnet by Wittawat Yamwong <wittawat@web.de> */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/init.h>
#include <linux/delay.h>
#include "tulip.h"


/* This is a mysterious value that can be written to CSR11 in the 21040 (only)
   to support a pre-NWay full-duplex signaling mechanism using short frames.
   No one knows what it should be, but if left at its default value some
   10base2(!) packets trigger a full-duplex-request interrupt. */
#define FULL_DUPLEX_MAGIC	0x6969

/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() inl(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK		0x10000
#define MDIO_DATA_WRITE0	0x00000
#define MDIO_DATA_WRITE1	0x20000
#define MDIO_ENB		0x00000 /* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ		0x80000

static const unsigned char comet_miireg2offset[32] = {
	0xB4, 0xB8, 0xBC, 0xC0,  0xC4, 0xC8, 0xCC, 0,  0,0,0,0,  0,0,0,0,
	0,0xD0,0,0,  0,0,0,0,  0,0,0,0, 0, 0xD4, 0xD8, 0xDC, };


/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

int tulip_mdio_read(struct rtnet_device *rtdev, int phy_id, int location)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	int i;
	int read_cmd = (0xf6 << 10) | ((phy_id & 0x1f) << 5) | location;
	int retval = 0;
	long ioaddr = rtdev->base_addr;
	long mdio_addr = ioaddr + CSR9;
	unsigned long flags;

	if (location & ~0x1f)
		return 0xffff;

	if (tp->chip_id == COMET  &&  phy_id == 30) {
		if (comet_miireg2offset[location])
			return inl(ioaddr + comet_miireg2offset[location]);
		return 0xffff;
	}

	spin_lock_irqsave(&tp->mii_lock, flags);
	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(0x60020000 + (phy_id<<23) + (location<<18), ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		while (--i > 0) {
			barrier();
			if ( ! ((retval = inl(ioaddr + 0xA0)) & 0x80000000))
				break;
		}
		spin_unlock_irqrestore(&tp->mii_lock, flags);
		return retval & 0xffff;
	}

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}

	spin_unlock_irqrestore(&tp->mii_lock, flags);
	return (retval>>1) & 0xffff;
}

void tulip_mdio_write(struct rtnet_device *rtdev, int phy_id, int location, int val)
{
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	int i;
	int cmd = (0x5002 << 16) | ((phy_id & 0x1f) << 23) | (location<<18) | (val & 0xffff);
	long ioaddr = rtdev->base_addr;
	long mdio_addr = ioaddr + CSR9;
	unsigned long flags;

	if (location & ~0x1f)
		return;

	if (tp->chip_id == COMET && phy_id == 30) {
		if (comet_miireg2offset[location])
			outl(val, ioaddr + comet_miireg2offset[location]);
		return;
	}

	spin_lock_irqsave(&tp->mii_lock, flags);
	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(cmd, ioaddr + 0xA0);
		do {
			barrier();
			if ( ! (inl(ioaddr + 0xA0) & 0x80000000))
				break;
		} while (--i > 0);
		spin_unlock_irqrestore(&tp->mii_lock, flags);
		return;
	}

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}

	spin_unlock_irqrestore(&tp->mii_lock, flags);
}


/* Set up the transceiver control registers for the selected media type. */
void tulip_select_media(struct rtnet_device *rtdev, int startup)
{
	long ioaddr = rtdev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)rtdev->priv;
	struct mediatable *mtable = tp->mtable;
	u32 new_csr6;
	int i;

	if (mtable) {
		struct medialeaf *mleaf = &mtable->mleaf[tp->cur_index];
		unsigned char *p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0:					/* 21140 non-MII xcvr. */
			if (tulip_debug > 1)
				/*RTnet*/pr_debug("%s: Using a 21140 non-MII transceiver"
					   " with control setting %2.2x.\n",
					   rtdev->name, p[1]);
			rtdev->if_port = p[0];
			if (startup)
				outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
			outl(p[1], ioaddr + CSR12);
			new_csr6 = 0x02000000 | ((p[2] & 0x71) << 18);
			break;
		case 2: case 4: {
			u16 setup[5];
			u32 csr13val, csr14val, csr15dir, csr15val;
			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			rtdev->if_port = p[0] & MEDIA_MASK;
			if (tulip_media_cap[rtdev->if_port] & MediaAlwaysFD)
				tp->full_duplex = 1;

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					/*RTnet*/pr_debug("%s: Resetting the transceiver.\n",
						   rtdev->name);
				for (i = 0; i < rst[0]; i++)
					outl(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}
			if (tulip_debug > 1)
				/*RTnet*/pr_debug("%s: 21143 non-MII %s transceiver control "
					   "%4.4x/%4.4x.\n",
					   rtdev->name, medianame[rtdev->if_port], setup[0], setup[1]);
			if (p[0] & 0x40) {	/* SIA (CSR13-15) setup values are provided. */
				csr13val = setup[0];
				csr14val = setup[1];
				csr15dir = (setup[3]<<16) | setup[2];
				csr15val = (setup[4]<<16) | setup[2];
				outl(0, ioaddr + CSR13);
				outl(csr14val, ioaddr + CSR14);
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				outl(csr13val, ioaddr + CSR13);
			} else {
				csr13val = 1;
				csr14val = 0;
				csr15dir = (setup[0]<<16) | 0x0008;
				csr15val = (setup[1]<<16) | 0x0008;
				if (rtdev->if_port <= 4)
					csr14val = t21142_csr14[rtdev->if_port];
				if (startup) {
					outl(0, ioaddr + CSR13);
					outl(csr14val, ioaddr + CSR14);
				}
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				if (startup) outl(csr13val, ioaddr + CSR13);
			}
			if (tulip_debug > 1)
				/*RTnet*/pr_debug("%s:  Setting CSR15 to %8.8x/%8.8x.\n",
					   rtdev->name, csr15dir, csr15val);
			if (mleaf->type == 4)
				new_csr6 = 0x82020000 | ((setup[2] & 0x71) << 18);
			else
				new_csr6 = 0x82420000;
			break;
		}
		case 1: case 3: {
			int phy_num = p[0];
			int init_length = p[1];
			u16 *misc_info, tmp_info;

			rtdev->if_port = 11;
			new_csr6 = 0x020E0000;
			if (mleaf->type == 3) {	/* 21142 */
				u16 *init_sequence = (u16*)(p+2);
				u16 *reset_sequence = &((u16*)(p+3))[init_length];
				int reset_length = p[2 + init_length*2];
				misc_info = reset_sequence + reset_length;
				if (startup)
					for (i = 0; i < reset_length; i++)
						outl(get_u16(&reset_sequence[i]) << 16, ioaddr + CSR15);
				for (i = 0; i < init_length; i++)
					outl(get_u16(&init_sequence[i]) << 16, ioaddr + CSR15);
			} else {
				u8 *init_sequence = p + 2;
				u8 *reset_sequence = p + 3 + init_length;
				int reset_length = p[2 + init_length];
				misc_info = (u16*)(reset_sequence + reset_length);
				if (startup) {
					outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
					for (i = 0; i < reset_length; i++)
						outl(reset_sequence[i], ioaddr + CSR12);
				}
				for (i = 0; i < init_length; i++)
					outl(init_sequence[i], ioaddr + CSR12);
			}
			tmp_info = get_u16(&misc_info[1]);
			if (tmp_info)
				tp->advertising[phy_num] = tmp_info | 1;
			if (tmp_info && startup < 2) {
				if (tp->mii_advertise == 0)
					tp->mii_advertise = tp->advertising[phy_num];
				if (tulip_debug > 1)
					/*RTnet*/pr_debug("%s:  Advertising %4.4x on MII %d.\n",
					       rtdev->name, tp->mii_advertise, tp->phys[phy_num]);
				tulip_mdio_write(rtdev, tp->phys[phy_num], 4, tp->mii_advertise);
			}
			break;
		}
		case 5: case 6: {
			u16 setup[5];

			new_csr6 = 0; /* FIXME */

			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					/*RTnet*/pr_debug("%s: Resetting the transceiver.\n",
						   rtdev->name);
				for (i = 0; i < rst[0]; i++)
					outl(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}

			break;
		}
		default:
			/*RTnet*/pr_debug("%s:  Invalid media table selection %d.\n",
					   rtdev->name, mleaf->type);
			new_csr6 = 0x020E0000;
		}
		if (tulip_debug > 1)
			/*RTnet*/pr_debug("%s: Using media type %s, CSR12 is %2.2x.\n",
				   rtdev->name, medianame[rtdev->if_port],
				   inl(ioaddr + CSR12) & 0xff);
	} else if (tp->chip_id == DC21041) {
		int port = rtdev->if_port <= 4 ? rtdev->if_port : 0;
		if (tulip_debug > 1)
			/*RTnet*/pr_debug("%s: 21041 using media %s, CSR12 is %4.4x.\n",
				   rtdev->name, medianame[port == 3 ? 12: port],
				   inl(ioaddr + CSR12));
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		outl(t21041_csr14[port], ioaddr + CSR14);
		outl(t21041_csr15[port], ioaddr + CSR15);
		outl(t21041_csr13[port], ioaddr + CSR13);
		new_csr6 = 0x80020000;
	} else if (tp->chip_id == LC82C168) {
		if (startup && ! tp->medialock)
			rtdev->if_port = tp->mii_cnt ? 11 : 0;
		if (tulip_debug > 1)
			/*RTnet*/pr_debug("%s: PNIC PHY status is %3.3x, media %s.\n",
				   rtdev->name, inl(ioaddr + 0xB8), medianame[rtdev->if_port]);
		if (tp->mii_cnt) {
			new_csr6 = 0x810C0000;
			outl(0x0001, ioaddr + CSR15);
			outl(0x0201B07A, ioaddr + 0xB8);
		} else if (startup) {
			/* Start with 10mbps to do autonegotiation. */
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x0001B078, ioaddr + 0xB8);
			outl(0x0201B078, ioaddr + 0xB8);
		} else if (rtdev->if_port == 3  ||  rtdev->if_port == 5) {
			outl(0x33, ioaddr + CSR12);
			new_csr6 = 0x01860000;
			/* Trigger autonegotiation. */
			outl(startup ? 0x0201F868 : 0x0001F868, ioaddr + 0xB8);
		} else {
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x1F078, ioaddr + 0xB8);
		}
	} else if (tp->chip_id == DC21040) {					/* 21040 */
		/* Turn on the xcvr interface. */
		int csr12 = inl(ioaddr + CSR12);
		if (tulip_debug > 1)
			/*RTnet*/pr_debug("%s: 21040 media type is %s, CSR12 is %2.2x.\n",
				   rtdev->name, medianame[rtdev->if_port], csr12);
		if (tulip_media_cap[rtdev->if_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
		new_csr6 = 0x20000;
		/* Set the full duplux match frame. */
		outl(FULL_DUPLEX_MAGIC, ioaddr + CSR11);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		if (t21040_csr13[rtdev->if_port] & 8) {
			outl(0x0705, ioaddr + CSR14);
			outl(0x0006, ioaddr + CSR15);
		} else {
			outl(0xffff, ioaddr + CSR14);
			outl(0x0000, ioaddr + CSR15);
		}
		outl(0x8f01 | t21040_csr13[rtdev->if_port], ioaddr + CSR13);
	} else {					/* Unknown chip type with no media table. */
		if (tp->default_port == 0)
			rtdev->if_port = tp->mii_cnt ? 11 : 3;
		if (tulip_media_cap[rtdev->if_port] & MediaIsMII) {
			new_csr6 = 0x020E0000;
		} else if (tulip_media_cap[rtdev->if_port] & MediaIsFx) {
			new_csr6 = 0x02860000;
		} else
			new_csr6 = 0x03860000;
		if (tulip_debug > 1)
			/*RTnet*/pr_debug("%s: No media description table, assuming "
				   "%s transceiver, CSR12 %2.2x.\n",
				   rtdev->name, medianame[rtdev->if_port],
				   inl(ioaddr + CSR12));
	}

	tp->csr6 = new_csr6 | (tp->csr6 & 0xfdff) | (tp->full_duplex ? 0x0200 : 0);

	mdelay(1);

	return;
}

/*
  Check the MII negotiated duplex and change the CSR6 setting if
  required.
  Return 0 if everything is OK.
  Return < 0 if the transceiver is missing or has no link beat.
  */
int tulip_check_duplex(struct rtnet_device *rtdev)
{
	struct tulip_private *tp = rtdev->priv;
	unsigned int bmsr, lpa, negotiated, new_csr6;

	bmsr = tulip_mdio_read(rtdev, tp->phys[0], MII_BMSR);
	lpa = tulip_mdio_read(rtdev, tp->phys[0], MII_LPA);
	if (tulip_debug > 1)
		/*RTnet*/pr_info("%s: MII status %4.4x, Link partner report "
			   "%4.4x.\n", rtdev->name, bmsr, lpa);
	if (bmsr == 0xffff)
		return -2;
	if ((bmsr & BMSR_LSTATUS) == 0) {
		int new_bmsr = tulip_mdio_read(rtdev, tp->phys[0], MII_BMSR);
		if ((new_bmsr & BMSR_LSTATUS) == 0) {
			if (tulip_debug  > 1)
				/*RTnet*/pr_info("%s: No link beat on the MII interface,"
					   " status %4.4x.\n", rtdev->name, new_bmsr);
			return -1;
		}
	}
	negotiated = lpa & tp->advertising[0];
	tp->full_duplex = mii_duplex(tp->full_duplex_lock, negotiated);

	new_csr6 = tp->csr6;

	if (negotiated & LPA_100) new_csr6 &= ~TxThreshold;
	else			  new_csr6 |= TxThreshold;
	if (tp->full_duplex) new_csr6 |= FullDuplex;
	else		     new_csr6 &= ~FullDuplex;

	if (new_csr6 != tp->csr6) {
		tp->csr6 = new_csr6;
		tulip_restart_rxtx(tp);

		if (tulip_debug > 0)
			/*RTnet*/pr_info("%s: Setting %s-duplex based on MII"
				   "#%d link partner capability of %4.4x.\n",
				   rtdev->name, tp->full_duplex ? "full" : "half",
				   tp->phys[0], lpa);
		return 1;
	}

	return 0;
}

void tulip_find_mii (struct rtnet_device *rtdev, int board_idx)
{
	struct tulip_private *tp = rtdev->priv;
	int phyn, phy_idx = 0;
	int mii_reg0;
	int mii_advert;
	unsigned int to_advert, new_bmcr, ane_switch;

	/* Find the connected MII xcvrs.
	   Doing this in open() would allow detecting external xcvrs later,
	   but takes much time. */
	for (phyn = 1; phyn <= 32 && phy_idx < sizeof (tp->phys); phyn++) {
		int phy = phyn & 0x1f;
		int mii_status = tulip_mdio_read (rtdev, phy, MII_BMSR);
		if ((mii_status & 0x8301) == 0x8001 ||
		    ((mii_status & BMSR_100BASE4) == 0
		     && (mii_status & 0x7800) != 0)) {
			/* preserve Becker logic, gain indentation level */
		} else {
			continue;
		}

		mii_reg0 = tulip_mdio_read (rtdev, phy, MII_BMCR);
		mii_advert = tulip_mdio_read (rtdev, phy, MII_ADVERTISE);
		ane_switch = 0;

		/* if not advertising at all, gen an
		 * advertising value from the capability
		 * bits in BMSR
		 */
		if ((mii_advert & ADVERTISE_ALL) == 0) {
			unsigned int tmpadv = tulip_mdio_read (rtdev, phy, MII_BMSR);
			mii_advert = ((tmpadv >> 6) & 0x3e0) | 1;
		}

		if (tp->mii_advertise) {
			tp->advertising[phy_idx] =
			to_advert = tp->mii_advertise;
		} else if (tp->advertising[phy_idx]) {
			to_advert = tp->advertising[phy_idx];
		} else {
			tp->advertising[phy_idx] =
			tp->mii_advertise =
			to_advert = mii_advert;
		}

		tp->phys[phy_idx++] = phy;

		/*RTnet*/pr_info("tulip%d:  MII transceiver #%d "
			"config %4.4x status %4.4x advertising %4.4x.\n",
			board_idx, phy, mii_reg0, mii_status, mii_advert);

		/* Fixup for DLink with miswired PHY. */
		if (mii_advert != to_advert) {
			/*RTnet*/pr_debug("tulip%d:  Advertising %4.4x on PHY %d,"
				" previously advertising %4.4x.\n",
				board_idx, to_advert, phy, mii_advert);
			tulip_mdio_write (rtdev, phy, 4, to_advert);
		}

		/* Enable autonegotiation: some boards default to off. */
		if (tp->default_port == 0) {
			new_bmcr = mii_reg0 | BMCR_ANENABLE;
			if (new_bmcr != mii_reg0) {
				new_bmcr |= BMCR_ANRESTART;
				ane_switch = 1;
			}
		}
		/* ...or disable nway, if forcing media */
		else {
			new_bmcr = mii_reg0 & ~BMCR_ANENABLE;
			if (new_bmcr != mii_reg0)
				ane_switch = 1;
		}

		/* clear out bits we never want at this point */
		new_bmcr &= ~(BMCR_CTST | BMCR_FULLDPLX | BMCR_ISOLATE |
			      BMCR_PDOWN | BMCR_SPEED100 | BMCR_LOOPBACK |
			      BMCR_RESET);

		if (tp->full_duplex)
			new_bmcr |= BMCR_FULLDPLX;
		if (tulip_media_cap[tp->default_port] & MediaIs100)
			new_bmcr |= BMCR_SPEED100;

		if (new_bmcr != mii_reg0) {
			/* some phys need the ANE switch to
			 * happen before forced media settings
			 * will "take."  However, we write the
			 * same value twice in order not to
			 * confuse the sane phys.
			 */
			if (ane_switch) {
				tulip_mdio_write (rtdev, phy, MII_BMCR, new_bmcr);
				udelay (10);
			}
			tulip_mdio_write (rtdev, phy, MII_BMCR, new_bmcr);
		}
	}
	tp->mii_cnt = phy_idx;
	if (tp->mtable && tp->mtable->has_mii && phy_idx == 0) {
		/*RTnet*/pr_info("tulip%d: ***WARNING***: No MII transceiver found!\n",
			board_idx);
		tp->phys[0] = 1;
	}
}
