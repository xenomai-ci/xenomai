/*
 * comedi/drivers/ni_660x.c
 * Hardware driver for NI 660x devices
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * Driver: ni_660x
 * Description: National Instruments 660x counter/timer boards
 * Devices:
 * [National Instruments] PCI-6601 (ni_660x), PCI-6602, PXI-6602,
 * PXI-6608
 * Author: J.P. Mellor <jpmellor@rose-hulman.edu>,
 * Herman.Bruyninckx@mech.kuleuven.ac.be,
 * Wim.Meeussen@mech.kuleuven.ac.be,
 * Klaas.Gadeyne@mech.kuleuven.ac.be,
 * Frank Mori Hess <fmhess@users.sourceforge.net>
 * Updated: Thu Oct 18 12:56:06 EDT 2007
 * Status: experimental

 * Encoders work.  PulseGeneration (both single pulse and pulse train)
 * works. Buffered commands work for input but not output.

 * References:
 * DAQ 660x Register-Level Programmer Manual  (NI 370505A-01)
 * DAQ 6601/6602 User Manual (NI 322137B-01)
 */

/*
 * Integration with Xenomai/Analogy layer based on the
 * comedi driver. Adaptation made by
 *   Julien Delange <julien.delange@esa.int>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/interrupt.h>

#include <linux/module.h>
#include <rtdm/analogy/device.h>

#include "../intel/8255.h"
#include "ni_stc.h"
#include "ni_mio.h"
#include "ni_tio.h"
#include "mite.h"

enum io_direction {
       DIRECTION_INPUT = 0,
       DIRECTION_OUTPUT = 1,
       DIRECTION_OPENDRAIN = 2
};


enum ni_660x_constants {
	min_counter_pfi_chan = 8,
	max_dio_pfi_chan = 31,
	counters_per_chip = 4
};

struct ni_660x_subd_priv {
   int                     io_bits;
   unsigned int            state;
   uint16_t                readback[2];
   uint16_t                config;
   struct ni_gpct*         counter;
};

#define NUM_PFI_CHANNELS 40
/* Really there are only up to 3 dma channels, but the register layout
   allows for 4 */
#define MAX_DMA_CHANNEL 4

static struct a4l_channels_desc chandesc_ni660x = {
	.mode = A4L_CHAN_GLOBAL_CHANDESC,
	.length = NUM_PFI_CHANNELS,
	.chans = {
		{A4L_CHAN_AREF_GROUND, sizeof(sampl_t)},
	},
};

#define subdev_priv ((struct ni_660x_subd_priv*)s->priv)

/* See Register-Level Programmer Manual page 3.1 */
enum NI_660x_Register {
	G0InterruptAcknowledge,
	G0StatusRegister,
	G1InterruptAcknowledge,
	G1StatusRegister,
	G01StatusRegister,
	G0CommandRegister,
	STCDIOParallelInput,
	G1CommandRegister,
	G0HWSaveRegister,
	G1HWSaveRegister,
	STCDIOOutput,
	STCDIOControl,
	G0SWSaveRegister,
	G1SWSaveRegister,
	G0ModeRegister,
	G01JointStatus1Register,
	G1ModeRegister,
	STCDIOSerialInput,
	G0LoadARegister,
	G01JointStatus2Register,
	G0LoadBRegister,
	G1LoadARegister,
	G1LoadBRegister,
	G0InputSelectRegister,
	G1InputSelectRegister,
	G0AutoincrementRegister,
	G1AutoincrementRegister,
	G01JointResetRegister,
	G0InterruptEnable,
	G1InterruptEnable,
	G0CountingModeRegister,
	G1CountingModeRegister,
	G0SecondGateRegister,
	G1SecondGateRegister,
	G0DMAConfigRegister,
	G0DMAStatusRegister,
	G1DMAConfigRegister,
	G1DMAStatusRegister,
	G2InterruptAcknowledge,
	G2StatusRegister,
	G3InterruptAcknowledge,
	G3StatusRegister,
	G23StatusRegister,
	G2CommandRegister,
	G3CommandRegister,
	G2HWSaveRegister,
	G3HWSaveRegister,
	G2SWSaveRegister,
	G3SWSaveRegister,
	G2ModeRegister,
	G23JointStatus1Register,
	G3ModeRegister,
	G2LoadARegister,
	G23JointStatus2Register,
	G2LoadBRegister,
	G3LoadARegister,
	G3LoadBRegister,
	G2InputSelectRegister,
	G3InputSelectRegister,
	G2AutoincrementRegister,
	G3AutoincrementRegister,
	G23JointResetRegister,
	G2InterruptEnable,
	G3InterruptEnable,
	G2CountingModeRegister,
	G3CountingModeRegister,
	G3SecondGateRegister,
	G2SecondGateRegister,
	G2DMAConfigRegister,
	G2DMAStatusRegister,
	G3DMAConfigRegister,
	G3DMAStatusRegister,
	DIO32Input,
	DIO32Output,
	ClockConfigRegister,
	GlobalInterruptStatusRegister,
	DMAConfigRegister,
	GlobalInterruptConfigRegister,
	IOConfigReg0_1,
	IOConfigReg2_3,
	IOConfigReg4_5,
	IOConfigReg6_7,
	IOConfigReg8_9,
	IOConfigReg10_11,
	IOConfigReg12_13,
	IOConfigReg14_15,
	IOConfigReg16_17,
	IOConfigReg18_19,
	IOConfigReg20_21,
	IOConfigReg22_23,
	IOConfigReg24_25,
	IOConfigReg26_27,
	IOConfigReg28_29,
	IOConfigReg30_31,
	IOConfigReg32_33,
	IOConfigReg34_35,
	IOConfigReg36_37,
	IOConfigReg38_39,
	NumRegisters,
};

static inline unsigned IOConfigReg(unsigned pfi_channel)
{
	unsigned reg = IOConfigReg0_1 + pfi_channel / 2;
	BUG_ON(reg > IOConfigReg38_39);
	return reg;
}

enum ni_660x_register_width {
	DATA_1B,
	DATA_2B,
	DATA_4B
};

enum ni_660x_register_direction {
	NI_660x_READ,
	NI_660x_WRITE,
	NI_660x_READ_WRITE
};

enum ni_660x_pfi_output_select {
	pfi_output_select_high_Z = 0,
	pfi_output_select_counter = 1,
	pfi_output_select_do = 2,
	num_pfi_output_selects
};

enum ni_660x_subdevices {
	NI_660X_DIO_SUBDEV = 1,
	NI_660X_GPCT_SUBDEV_0 = 2
};

static inline unsigned NI_660X_GPCT_SUBDEV(unsigned index)
{
	return NI_660X_GPCT_SUBDEV_0 + index;
}

struct NI_660xRegisterData {

	const char *name; /*  Register Name */
	int offset; /*  Offset from base address from GPCT chip */
	enum ni_660x_register_direction direction;
	enum ni_660x_register_width size; /*  1 byte, 2 bytes, or 4 bytes */
};

static const struct NI_660xRegisterData registerData[NumRegisters] = {
	{"G0 Interrupt Acknowledge", 0x004, NI_660x_WRITE, DATA_2B},
	{"G0 Status Register", 0x004, NI_660x_READ, DATA_2B},
	{"G1 Interrupt Acknowledge", 0x006, NI_660x_WRITE, DATA_2B},
	{"G1 Status Register", 0x006, NI_660x_READ, DATA_2B},
	{"G01 Status Register ", 0x008, NI_660x_READ, DATA_2B},
	{"G0 Command Register", 0x00C, NI_660x_WRITE, DATA_2B},
	{"STC DIO Parallel Input", 0x00E, NI_660x_READ, DATA_2B},
	{"G1 Command Register", 0x00E, NI_660x_WRITE, DATA_2B},
	{"G0 HW Save Register", 0x010, NI_660x_READ, DATA_4B},
	{"G1 HW Save Register", 0x014, NI_660x_READ, DATA_4B},
	{"STC DIO Output", 0x014, NI_660x_WRITE, DATA_2B},
	{"STC DIO Control", 0x016, NI_660x_WRITE, DATA_2B},
	{"G0 SW Save Register", 0x018, NI_660x_READ, DATA_4B},
	{"G1 SW Save Register", 0x01C, NI_660x_READ, DATA_4B},
	{"G0 Mode Register", 0x034, NI_660x_WRITE, DATA_2B},
	{"G01 Joint Status 1 Register", 0x036, NI_660x_READ, DATA_2B},
	{"G1 Mode Register", 0x036, NI_660x_WRITE, DATA_2B},
	{"STC DIO Serial Input", 0x038, NI_660x_READ, DATA_2B},
	{"G0 Load A Register", 0x038, NI_660x_WRITE, DATA_4B},
	{"G01 Joint Status 2 Register", 0x03A, NI_660x_READ, DATA_2B},
	{"G0 Load B Register", 0x03C, NI_660x_WRITE, DATA_4B},
	{"G1 Load A Register", 0x040, NI_660x_WRITE, DATA_4B},
	{"G1 Load B Register", 0x044, NI_660x_WRITE, DATA_4B},
	{"G0 Input Select Register", 0x048, NI_660x_WRITE, DATA_2B},
	{"G1 Input Select Register", 0x04A, NI_660x_WRITE, DATA_2B},
	{"G0 Autoincrement Register", 0x088, NI_660x_WRITE, DATA_2B},
	{"G1 Autoincrement Register", 0x08A, NI_660x_WRITE, DATA_2B},
	{"G01 Joint Reset Register", 0x090, NI_660x_WRITE, DATA_2B},
	{"G0 Interrupt Enable", 0x092, NI_660x_WRITE, DATA_2B},
	{"G1 Interrupt Enable", 0x096, NI_660x_WRITE, DATA_2B},
	{"G0 Counting Mode Register", 0x0B0, NI_660x_WRITE, DATA_2B},
	{"G1 Counting Mode Register", 0x0B2, NI_660x_WRITE, DATA_2B},
	{"G0 Second Gate Register", 0x0B4, NI_660x_WRITE, DATA_2B},
	{"G1 Second Gate Register", 0x0B6, NI_660x_WRITE, DATA_2B},
	{"G0 DMA Config Register", 0x0B8, NI_660x_WRITE, DATA_2B},
	{"G0 DMA Status Register", 0x0B8, NI_660x_READ, DATA_2B},
	{"G1 DMA Config Register", 0x0BA, NI_660x_WRITE, DATA_2B},
	{"G1 DMA Status Register", 0x0BA, NI_660x_READ, DATA_2B},
	{"G2 Interrupt Acknowledge", 0x104, NI_660x_WRITE, DATA_2B},
	{"G2 Status Register", 0x104, NI_660x_READ, DATA_2B},
	{"G3 Interrupt Acknowledge", 0x106, NI_660x_WRITE, DATA_2B},
	{"G3 Status Register", 0x106, NI_660x_READ, DATA_2B},
	{"G23 Status Register", 0x108, NI_660x_READ, DATA_2B},
	{"G2 Command Register", 0x10C, NI_660x_WRITE, DATA_2B},
	{"G3 Command Register", 0x10E, NI_660x_WRITE, DATA_2B},
	{"G2 HW Save Register", 0x110, NI_660x_READ, DATA_4B},
	{"G3 HW Save Register", 0x114, NI_660x_READ, DATA_4B},
	{"G2 SW Save Register", 0x118, NI_660x_READ, DATA_4B},
	{"G3 SW Save Register", 0x11C, NI_660x_READ, DATA_4B},
	{"G2 Mode Register", 0x134, NI_660x_WRITE, DATA_2B},
	{"G23 Joint Status 1 Register", 0x136, NI_660x_READ, DATA_2B},
	{"G3 Mode Register", 0x136, NI_660x_WRITE, DATA_2B},
	{"G2 Load A Register", 0x138, NI_660x_WRITE, DATA_4B},
	{"G23 Joint Status 2 Register", 0x13A, NI_660x_READ, DATA_2B},
	{"G2 Load B Register", 0x13C, NI_660x_WRITE, DATA_4B},
	{"G3 Load A Register", 0x140, NI_660x_WRITE, DATA_4B},
	{"G3 Load B Register", 0x144, NI_660x_WRITE, DATA_4B},
	{"G2 Input Select Register", 0x148, NI_660x_WRITE, DATA_2B},
	{"G3 Input Select Register", 0x14A, NI_660x_WRITE, DATA_2B},
	{"G2 Autoincrement Register", 0x188, NI_660x_WRITE, DATA_2B},
	{"G3 Autoincrement Register", 0x18A, NI_660x_WRITE, DATA_2B},
	{"G23 Joint Reset Register", 0x190, NI_660x_WRITE, DATA_2B},
	{"G2 Interrupt Enable", 0x192, NI_660x_WRITE, DATA_2B},
	{"G3 Interrupt Enable", 0x196, NI_660x_WRITE, DATA_2B},
	{"G2 Counting Mode Register", 0x1B0, NI_660x_WRITE, DATA_2B},
	{"G3 Counting Mode Register", 0x1B2, NI_660x_WRITE, DATA_2B},
	{"G3 Second Gate Register", 0x1B6, NI_660x_WRITE, DATA_2B},
	{"G2 Second Gate Register", 0x1B4, NI_660x_WRITE, DATA_2B},
	{"G2 DMA Config Register", 0x1B8, NI_660x_WRITE, DATA_2B},
	{"G2 DMA Status Register", 0x1B8, NI_660x_READ, DATA_2B},
	{"G3 DMA Config Register", 0x1BA, NI_660x_WRITE, DATA_2B},
	{"G3 DMA Status Register", 0x1BA, NI_660x_READ, DATA_2B},
	{"32 bit Digital Input", 0x414, NI_660x_READ, DATA_4B},
	{"32 bit Digital Output", 0x510, NI_660x_WRITE, DATA_4B},
	{"Clock Config Register", 0x73C, NI_660x_WRITE, DATA_4B},
	{"Global Interrupt Status Register", 0x754, NI_660x_READ, DATA_4B},
	{"DMA Configuration Register", 0x76C, NI_660x_WRITE, DATA_4B},
	{"Global Interrupt Config Register", 0x770, NI_660x_WRITE, DATA_4B},
	{"IO Config Register 0-1", 0x77C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 2-3", 0x77E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 4-5", 0x780, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 6-7", 0x782, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 8-9", 0x784, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 10-11", 0x786, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 12-13", 0x788, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 14-15", 0x78A, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 16-17", 0x78C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 18-19", 0x78E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 20-21", 0x790, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 22-23", 0x792, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 24-25", 0x794, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 26-27", 0x796, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 28-29", 0x798, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 30-31", 0x79A, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 32-33", 0x79C, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 34-35", 0x79E, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 36-37", 0x7A0, NI_660x_READ_WRITE, DATA_2B},
	{"IO Config Register 38-39", 0x7A2, NI_660x_READ_WRITE, DATA_2B}
};

/* kind of ENABLE for the second counter */
enum clock_config_register_bits {
	CounterSwap = 0x1 << 21
};

/* ioconfigreg */
static inline unsigned ioconfig_bitshift(unsigned pfi_channel)
{
	if (pfi_channel % 2)
		return 0;
	else
		return 8;
}

static inline unsigned pfi_output_select_mask(unsigned pfi_channel)
{
	return 0x3 << ioconfig_bitshift(pfi_channel);
}

static inline unsigned pfi_output_select_bits(unsigned pfi_channel,
					      unsigned output_select)
{
	return (output_select & 0x3) << ioconfig_bitshift(pfi_channel);
}

static inline unsigned pfi_input_select_mask(unsigned pfi_channel)
{
	return 0x7 << (4 + ioconfig_bitshift(pfi_channel));
}

static inline unsigned pfi_input_select_bits(unsigned pfi_channel,
					     unsigned input_select)
{
	return (input_select & 0x7) << (4 + ioconfig_bitshift(pfi_channel));
}

/* Dma configuration register bits */
static inline unsigned dma_select_mask(unsigned dma_channel)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return 0x1f << (8 * dma_channel);
}

enum dma_selection {
	dma_selection_none = 0x1f,
};

static inline unsigned dma_selection_counter(unsigned counter_index)
{
	BUG_ON(counter_index >= counters_per_chip);
	return counter_index;
}

static inline unsigned dma_select_bits(unsigned dma_channel, unsigned selection)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return (selection << (8 * dma_channel)) & dma_select_mask(dma_channel);
}

static inline unsigned dma_reset_bit(unsigned dma_channel)
{
	BUG_ON(dma_channel >= MAX_DMA_CHANNEL);
	return 0x80 << (8 * dma_channel);
}

enum global_interrupt_status_register_bits {
	Counter_0_Int_Bit = 0x100,
	Counter_1_Int_Bit = 0x200,
	Counter_2_Int_Bit = 0x400,
	Counter_3_Int_Bit = 0x800,
	Cascade_Int_Bit = 0x20000000,
	Global_Int_Bit = 0x80000000
};

enum global_interrupt_config_register_bits {
	Cascade_Int_Enable_Bit = 0x20000000,
	Global_Int_Polarity_Bit = 0x40000000,
	Global_Int_Enable_Bit = 0x80000000
};

/* Offset of the GPCT chips from the base-adress of the card:
   First chip is at base-address +0x00, etc. */
static const unsigned GPCT_OFFSET[2] = { 0x0, 0x800 };

/* Board description */
struct ni_660x_board {
	unsigned short dev_id;	/* `lspci` will show you this */
	const char *name;
	unsigned n_chips;	/* total number of TIO chips */
};

static const struct ni_660x_board ni_660x_boards[] = {
	{
	 .dev_id = 0x2c60,
	 .name = "PCI-6601",
	 .n_chips = 1,
	 },
	{
	 .dev_id = 0x1310,
	 .name = "PCI-6602",
	 .n_chips = 2,
	 },
	{
	 .dev_id = 0x1360,
	 .name = "PXI-6602",
	 .n_chips = 2,
	 },
	{
	 .dev_id = 0x2cc0,
	 .name = "PXI-6608",
	 .n_chips = 2,
	 },
};

#define NI_660X_MAX_NUM_CHIPS 2
#define NI_660X_MAX_NUM_COUNTERS (NI_660X_MAX_NUM_CHIPS * counters_per_chip)

static const struct pci_device_id ni_660x_pci_table[] = {
	{
	PCI_VENDOR_ID_NATINST, 0x2c60, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	PCI_VENDOR_ID_NATINST, 0x1310, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	PCI_VENDOR_ID_NATINST, 0x1360, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	PCI_VENDOR_ID_NATINST, 0x2cc0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	0}
};

MODULE_DEVICE_TABLE(pci, ni_660x_pci_table);

struct ni_660x_private {
	struct mite_struct *mite;
	struct ni_gpct_device *counter_dev;
	uint64_t pfi_direction_bits;

	struct mite_dma_descriptor_ring
	  *mite_rings[NI_660X_MAX_NUM_CHIPS][counters_per_chip];

	rtdm_lock_t mite_channel_lock;
	/* Interrupt_lock prevents races between interrupt and
	   comedi_poll */
	rtdm_lock_t interrupt_lock;
	unsigned int dma_configuration_soft_copies[NI_660X_MAX_NUM_CHIPS];
	rtdm_lock_t soft_reg_copy_lock;
	unsigned short pfi_output_selects[NUM_PFI_CHANNELS];

	struct ni_660x_board *board_ptr;
};

#undef devpriv
#define devpriv ((struct ni_660x_private *)dev->priv)

static inline struct ni_660x_private *private(struct a4l_device *dev)
{
	return (struct ni_660x_private*) dev->priv;
}

/* Initialized in ni_660x_find_device() */
static inline const struct ni_660x_board *board(struct a4l_device *dev)
{
	return ((struct ni_660x_private*)dev->priv)->board_ptr;
}

#define n_ni_660x_boards ARRAY_SIZE(ni_660x_boards)

static int ni_660x_attach(struct a4l_device *dev,
					 a4l_lnkdesc_t *arg);
static int ni_660x_detach(struct a4l_device *dev);
static void init_tio_chip(struct a4l_device *dev, int chipset);
static void ni_660x_select_pfi_output(struct a4l_device *dev,
				      unsigned pfi_channel,
				      unsigned output_select);

static struct a4l_driver ni_660x_drv = {
	.board_name = "analogy_ni_660x",
	.driver_name = "ni_660x",
	.owner = THIS_MODULE,
	.attach = ni_660x_attach,
	.detach = ni_660x_detach,
   .privdata_size = sizeof(struct ni_660x_private),
};

static int ni_660x_set_pfi_routing(struct a4l_device *dev, unsigned chan,
				   unsigned source);

/* Possible instructions for a GPCT */
static int ni_660x_GPCT_rinsn(
			      struct a4l_subdevice *s,
			      struct a4l_kernel_instruction *insn);
static int ni_660x_GPCT_insn_config(
				    struct a4l_subdevice *s,
				    struct a4l_kernel_instruction *insn);
static int ni_660x_GPCT_winsn(
			      struct a4l_subdevice *s,
			      struct a4l_kernel_instruction *insn);

/* Possible instructions for Digital IO */
static int ni_660x_dio_insn_config(
	       struct a4l_subdevice *s,
	       struct a4l_kernel_instruction *insn);
static int ni_660x_dio_insn_bits(
	     struct a4l_subdevice *s,
	     struct a4l_kernel_instruction *insn);

static inline unsigned ni_660x_num_counters(struct a4l_device *dev)
{
	return board(dev)->n_chips * counters_per_chip;
}

static enum NI_660x_Register ni_gpct_to_660x_register(enum ni_gpct_register reg)
{

	enum NI_660x_Register ni_660x_register;
	switch (reg) {
	case NITIO_G0_Autoincrement_Reg:
		ni_660x_register = G0AutoincrementRegister;
		break;
	case NITIO_G1_Autoincrement_Reg:
		ni_660x_register = G1AutoincrementRegister;
		break;
	case NITIO_G2_Autoincrement_Reg:
		ni_660x_register = G2AutoincrementRegister;
		break;
	case NITIO_G3_Autoincrement_Reg:
		ni_660x_register = G3AutoincrementRegister;
		break;
	case NITIO_G0_Command_Reg:
		ni_660x_register = G0CommandRegister;
		break;
	case NITIO_G1_Command_Reg:
		ni_660x_register = G1CommandRegister;
		break;
	case NITIO_G2_Command_Reg:
		ni_660x_register = G2CommandRegister;
		break;
	case NITIO_G3_Command_Reg:
		ni_660x_register = G3CommandRegister;
		break;
	case NITIO_G0_HW_Save_Reg:
		ni_660x_register = G0HWSaveRegister;
		break;
	case NITIO_G1_HW_Save_Reg:
		ni_660x_register = G1HWSaveRegister;
		break;
	case NITIO_G2_HW_Save_Reg:
		ni_660x_register = G2HWSaveRegister;
		break;
	case NITIO_G3_HW_Save_Reg:
		ni_660x_register = G3HWSaveRegister;
		break;
	case NITIO_G0_SW_Save_Reg:
		ni_660x_register = G0SWSaveRegister;
		break;
	case NITIO_G1_SW_Save_Reg:
		ni_660x_register = G1SWSaveRegister;
		break;
	case NITIO_G2_SW_Save_Reg:
		ni_660x_register = G2SWSaveRegister;
		break;
	case NITIO_G3_SW_Save_Reg:
		ni_660x_register = G3SWSaveRegister;
		break;
	case NITIO_G0_Mode_Reg:
		ni_660x_register = G0ModeRegister;
		break;
	case NITIO_G1_Mode_Reg:
		ni_660x_register = G1ModeRegister;
		break;
	case NITIO_G2_Mode_Reg:
		ni_660x_register = G2ModeRegister;
		break;
	case NITIO_G3_Mode_Reg:
		ni_660x_register = G3ModeRegister;
		break;
	case NITIO_G0_LoadA_Reg:
		ni_660x_register = G0LoadARegister;
		break;
	case NITIO_G1_LoadA_Reg:
		ni_660x_register = G1LoadARegister;
		break;
	case NITIO_G2_LoadA_Reg:
		ni_660x_register = G2LoadARegister;
		break;
	case NITIO_G3_LoadA_Reg:
		ni_660x_register = G3LoadARegister;
		break;
	case NITIO_G0_LoadB_Reg:
		ni_660x_register = G0LoadBRegister;
		break;
	case NITIO_G1_LoadB_Reg:
		ni_660x_register = G1LoadBRegister;
		break;
	case NITIO_G2_LoadB_Reg:
		ni_660x_register = G2LoadBRegister;
		break;
	case NITIO_G3_LoadB_Reg:
		ni_660x_register = G3LoadBRegister;
		break;
	case NITIO_G0_Input_Select_Reg:
		ni_660x_register = G0InputSelectRegister;
		break;
	case NITIO_G1_Input_Select_Reg:
		ni_660x_register = G1InputSelectRegister;
		break;
	case NITIO_G2_Input_Select_Reg:
		ni_660x_register = G2InputSelectRegister;
		break;
	case NITIO_G3_Input_Select_Reg:
		ni_660x_register = G3InputSelectRegister;
		break;
	case NITIO_G01_Status_Reg:
		ni_660x_register = G01StatusRegister;
		break;
	case NITIO_G23_Status_Reg:
		ni_660x_register = G23StatusRegister;
		break;
	case NITIO_G01_Joint_Reset_Reg:
		ni_660x_register = G01JointResetRegister;
		break;
	case NITIO_G23_Joint_Reset_Reg:
		ni_660x_register = G23JointResetRegister;
		break;
	case NITIO_G01_Joint_Status1_Reg:
		ni_660x_register = G01JointStatus1Register;
		break;
	case NITIO_G23_Joint_Status1_Reg:
		ni_660x_register = G23JointStatus1Register;
		break;
	case NITIO_G01_Joint_Status2_Reg:
		ni_660x_register = G01JointStatus2Register;
		break;
	case NITIO_G23_Joint_Status2_Reg:
		ni_660x_register = G23JointStatus2Register;
		break;
	case NITIO_G0_Counting_Mode_Reg:
		ni_660x_register = G0CountingModeRegister;
		break;
	case NITIO_G1_Counting_Mode_Reg:
		ni_660x_register = G1CountingModeRegister;
		break;
	case NITIO_G2_Counting_Mode_Reg:
		ni_660x_register = G2CountingModeRegister;
		break;
	case NITIO_G3_Counting_Mode_Reg:
		ni_660x_register = G3CountingModeRegister;
		break;
	case NITIO_G0_Second_Gate_Reg:
		ni_660x_register = G0SecondGateRegister;
		break;
	case NITIO_G1_Second_Gate_Reg:
		ni_660x_register = G1SecondGateRegister;
		break;
	case NITIO_G2_Second_Gate_Reg:
		ni_660x_register = G2SecondGateRegister;
		break;
	case NITIO_G3_Second_Gate_Reg:
		ni_660x_register = G3SecondGateRegister;
		break;
	case NITIO_G0_DMA_Config_Reg:
		ni_660x_register = G0DMAConfigRegister;
		break;
	case NITIO_G0_DMA_Status_Reg:
		ni_660x_register = G0DMAStatusRegister;
		break;
	case NITIO_G1_DMA_Config_Reg:
		ni_660x_register = G1DMAConfigRegister;
		break;
	case NITIO_G1_DMA_Status_Reg:
		ni_660x_register = G1DMAStatusRegister;
		break;
	case NITIO_G2_DMA_Config_Reg:
		ni_660x_register = G2DMAConfigRegister;
		break;
	case NITIO_G2_DMA_Status_Reg:
		ni_660x_register = G2DMAStatusRegister;
		break;
	case NITIO_G3_DMA_Config_Reg:
		ni_660x_register = G3DMAConfigRegister;
		break;
	case NITIO_G3_DMA_Status_Reg:
		ni_660x_register = G3DMAStatusRegister;
		break;
	case NITIO_G0_Interrupt_Acknowledge_Reg:
		ni_660x_register = G0InterruptAcknowledge;
		break;
	case NITIO_G1_Interrupt_Acknowledge_Reg:
		ni_660x_register = G1InterruptAcknowledge;
		break;
	case NITIO_G2_Interrupt_Acknowledge_Reg:
		ni_660x_register = G2InterruptAcknowledge;
		break;
	case NITIO_G3_Interrupt_Acknowledge_Reg:
		ni_660x_register = G3InterruptAcknowledge;
		break;
	case NITIO_G0_Status_Reg:
		ni_660x_register = G0StatusRegister;
		break;
	case NITIO_G1_Status_Reg:
		ni_660x_register = G0StatusRegister;
		break;
	case NITIO_G2_Status_Reg:
		ni_660x_register = G0StatusRegister;
		break;
	case NITIO_G3_Status_Reg:
		ni_660x_register = G0StatusRegister;
		break;
	case NITIO_G0_Interrupt_Enable_Reg:
		ni_660x_register = G0InterruptEnable;
		break;
	case NITIO_G1_Interrupt_Enable_Reg:
		ni_660x_register = G1InterruptEnable;
		break;
	case NITIO_G2_Interrupt_Enable_Reg:
		ni_660x_register = G2InterruptEnable;
		break;
	case NITIO_G3_Interrupt_Enable_Reg:
		ni_660x_register = G3InterruptEnable;
		break;
	default:
		__a4l_err("%s: unhandled register 0x%x in switch.\n",
			  __FUNCTION__, reg);
		BUG();
		return 0;
		break;
	}
	return ni_660x_register;
}

static inline void ni_660x_write_register(struct a4l_device *dev,
					  unsigned chip_index, unsigned bits,
					  enum NI_660x_Register reg)
{
	void *const write_address =
	    private(dev)->mite->daq_io_addr + GPCT_OFFSET[chip_index] +
	    registerData[reg].offset;

	switch (registerData[reg].size) {
	case DATA_2B:
		writew(bits, write_address);
		break;
	case DATA_4B:
		writel(bits, write_address);
		break;
	default:
		__a4l_err("%s: %s: bug! unhandled case (reg=0x%x) in switch.\n",
			  __FILE__, __FUNCTION__, reg);
		BUG();
		break;
	}
}

static inline unsigned ni_660x_read_register(struct a4l_device *dev,
					     unsigned chip_index,
					     enum NI_660x_Register reg)
{
	void *const read_address =
	    private(dev)->mite->daq_io_addr + GPCT_OFFSET[chip_index] +
	    registerData[reg].offset;

	switch (registerData[reg].size) {
	case DATA_2B:
		return readw(read_address);
		break;
	case DATA_4B:
		return readl(read_address);
		break;
	default:
		__a4l_err("%s: %s: bug! unhandled case (reg=0x%x) in switch.\n",
			  __FILE__, __FUNCTION__, reg);
		BUG();
		break;
	}
	return 0;
}

static void ni_gpct_write_register(struct ni_gpct *counter,
				   unsigned int bits, enum ni_gpct_register reg)
{
	struct a4l_device *dev = counter->counter_dev->dev;
	enum NI_660x_Register ni_660x_register = ni_gpct_to_660x_register(reg);

	ni_660x_write_register(dev, counter->chip_index, bits,
			       ni_660x_register);
}

static unsigned ni_gpct_read_register(struct ni_gpct *counter,
				      enum ni_gpct_register reg)
{
	struct a4l_device *dev = counter->counter_dev->dev;
	enum NI_660x_Register ni_660x_register = ni_gpct_to_660x_register(reg);

	return ni_660x_read_register(dev, counter->chip_index,
				     ni_660x_register);
}

static inline
struct mite_dma_descriptor_ring *mite_ring(struct ni_660x_private *priv,
					   struct ni_gpct *counter)
{

	return priv->mite_rings[counter->chip_index][counter->counter_index];
}

static inline
void ni_660x_set_dma_channel(struct a4l_device *dev,
			     unsigned int mite_channel, struct ni_gpct *counter)
{
	unsigned long flags;

	rtdm_lock_get_irqsave(&private(dev)->soft_reg_copy_lock, flags);
	private(dev)->dma_configuration_soft_copies[counter->chip_index] &=
	    ~dma_select_mask(mite_channel);
	private(dev)->dma_configuration_soft_copies[counter->chip_index] |=
	    dma_select_bits(mite_channel,
			    dma_selection_counter(counter->counter_index));
	ni_660x_write_register(dev, counter->chip_index,
			       private(dev)->
			       dma_configuration_soft_copies
			       [counter->chip_index] |
			       dma_reset_bit(mite_channel), DMAConfigRegister);
	rtdm_lock_put_irqrestore(&private(dev)->soft_reg_copy_lock, flags);
}

static inline
void ni_660x_unset_dma_channel(struct a4l_device *dev,
			       unsigned int mite_channel,
			       struct ni_gpct *counter)
{
	unsigned long flags;
	rtdm_lock_get_irqsave(&private(dev)->soft_reg_copy_lock, flags);
	private(dev)->dma_configuration_soft_copies[counter->chip_index] &=
	    ~dma_select_mask(mite_channel);
	private(dev)->dma_configuration_soft_copies[counter->chip_index] |=
	    dma_select_bits(mite_channel, dma_selection_none);
	ni_660x_write_register(dev, counter->chip_index,
			       private(dev)->
			       dma_configuration_soft_copies
			       [counter->chip_index], DMAConfigRegister);
	rtdm_lock_put_irqrestore(&private(dev)->soft_reg_copy_lock, flags);
}

static int ni_660x_request_mite_channel(struct a4l_device *dev,
					struct ni_gpct *counter,
					enum io_direction direction)
{
	unsigned long flags;
	struct mite_channel *mite_chan;

	rtdm_lock_get_irqsave(&private(dev)->mite_channel_lock, flags);
	BUG_ON(counter->mite_chan);
	mite_chan = mite_request_channel(private(dev)->mite,
					 mite_ring(private(dev), counter));
	if (mite_chan == NULL) {
		rtdm_lock_put_irqrestore(&private(dev)->mite_channel_lock, flags);
		a4l_err(dev,
			"%s: failed to reserve mite dma channel for counter.\n",
			__FUNCTION__);
		return -EBUSY;
	}
	mite_chan->dir = direction;
	a4l_ni_tio_set_mite_channel(counter, mite_chan);
	ni_660x_set_dma_channel(dev, mite_chan->channel, counter);
	rtdm_lock_put_irqrestore(&private(dev)->mite_channel_lock, flags);
	return 0;
}

void ni_660x_release_mite_channel(struct a4l_device *dev,
				  struct ni_gpct *counter)
{
	unsigned long flags;

	rtdm_lock_get_irqsave(&private(dev)->mite_channel_lock, flags);
	if (counter->mite_chan) {
		struct mite_channel *mite_chan = counter->mite_chan;

		ni_660x_unset_dma_channel(dev, mite_chan->channel, counter);
		a4l_ni_tio_set_mite_channel(counter, NULL);
		a4l_mite_release_channel(mite_chan);
	}
	rtdm_lock_put_irqrestore(&private(dev)->mite_channel_lock, flags);
}

static int ni_660x_cmd(struct a4l_subdevice *s, struct a4l_cmd_desc* cmd)
{
	int retval;

	struct ni_gpct *counter = subdev_priv->counter;

	retval = ni_660x_request_mite_channel(s->dev, counter, A4L_INPUT);
	if (retval) {
		a4l_err(s->dev,
			"%s: no dma channel available for use by counter",
			__FUNCTION__);
		return retval;
	}

	a4l_ni_tio_acknowledge_and_confirm (counter, NULL, NULL, NULL, NULL);
	retval = a4l_ni_tio_cmd(counter, cmd);

	return retval;
}

static int ni_660x_cmdtest(struct a4l_subdevice *s, struct a4l_cmd_desc *cmd)
{
	struct ni_gpct *counter = subdev_priv->counter;
	return a4l_ni_tio_cmdtest(counter, cmd);
}

static int ni_660x_cancel(struct a4l_subdevice *s)
{
	struct ni_gpct *counter = subdev_priv->counter;
	int retval;

	retval = a4l_ni_tio_cancel(counter);
	ni_660x_release_mite_channel(s->dev, counter);
	return retval;
}

static void set_tio_counterswap(struct a4l_device *dev, int chipset)
{
	/* See P. 3.5 of the Register-Level Programming manual.  The
	   CounterSwap bit has to be set on the second chip, otherwise
	   it will try to use the same pins as the first chip.
	 */

	if (chipset)
		ni_660x_write_register(dev,
				       chipset,
				       CounterSwap, ClockConfigRegister);
	else
		ni_660x_write_register(dev,
				       chipset, 0, ClockConfigRegister);
}

static void ni_660x_handle_gpct_interrupt(struct a4l_device *dev,
					  struct a4l_subdevice *s)
{
   struct a4l_buffer *buf = s->buf;

   a4l_ni_tio_handle_interrupt(subdev_priv->counter, dev);
   if ( test_bit(A4L_BUF_EOA_NR, &buf->flags) &&
	test_bit(A4L_BUF_ERROR_NR, &buf->flags) &&
	test_bit(A4L_BUF_EOA_NR, &buf->flags))
	   ni_660x_cancel(s);
   else
	   a4l_buf_evt(s, 0);
}

static int ni_660x_interrupt(unsigned int irq, void *d)
{
	struct a4l_device *dev = d;
	unsigned long flags;

	if (test_bit(A4L_DEV_ATTACHED_NR, &dev->flags))
		return -ENOENT;

	/* Lock to avoid race with comedi_poll */
	rtdm_lock_get_irqsave(&private(dev)->interrupt_lock, flags);
	smp_mb();

	while (&dev->subdvsq != dev->subdvsq.next) {
		struct list_head *this = dev->subdvsq.next;
		struct a4l_subdevice *tmp = list_entry(this, struct a4l_subdevice, list);
		ni_660x_handle_gpct_interrupt(dev, tmp);
	}

	rtdm_lock_put_irqrestore(&private(dev)->interrupt_lock, flags);
	return 0;
}

static int ni_660x_alloc_mite_rings(struct a4l_device *dev)
{
	unsigned int i;
	unsigned int j;

	for (i = 0; i < board(dev)->n_chips; ++i) {
		for (j = 0; j < counters_per_chip; ++j) {
			private(dev)->mite_rings[i][j] =
				mite_alloc_ring(private(dev)->mite);
			if (private(dev)->mite_rings[i][j] == NULL)
				return -ENOMEM;
		}
	}

	return 0;
}

static void ni_660x_free_mite_rings(struct a4l_device *dev)
{
	unsigned int i;
	unsigned int j;

	for (i = 0; i < board(dev)->n_chips; ++i)
		for (j = 0; j < counters_per_chip; ++j)
			mite_free_ring(private(dev)->mite_rings[i][j]);
}


static int __init driver_ni_660x_init_module(void)
{
	return a4l_register_drv (&ni_660x_drv);
}

static void __exit driver_ni_660x_cleanup_module(void)
{
	a4l_unregister_drv (&ni_660x_drv);
}

module_init(driver_ni_660x_init_module);
module_exit(driver_ni_660x_cleanup_module);

static int ni_660x_attach(struct a4l_device *dev, a4l_lnkdesc_t *arg)
{
	struct a4l_subdevice *s;
	int ret;
	int err;
	int bus, slot;
	unsigned i;
	int nsubdev = 0;
	unsigned global_interrupt_config_bits;
	struct mite_struct *mitedev;
	struct ni_660x_board* boardptr = NULL;

	ret = 0;
	bus = slot = 0;
	mitedev = NULL;
	nsubdev = 0;

	if(arg->opts == NULL || arg->opts_size == 0)
		bus = slot = 0;
	else {
		bus = arg->opts_size >= sizeof(unsigned long) ?
			((unsigned long *)arg->opts)[0] : 0;
		slot = arg->opts_size >= sizeof(unsigned long) * 2 ?
			((unsigned long *)arg->opts)[1] : 0;
	}

	for (i = 0; ( i < n_ni_660x_boards ) && ( mitedev == NULL ); i++) {
		mitedev  = a4l_mite_find_device(bus, slot,
						ni_660x_boards[i].dev_id);
		boardptr = (struct ni_660x_board*) &ni_660x_boards[i];
	}


	if(mitedev == NULL) {
		a4l_info(dev, "mite device not found\n");
		return -ENOENT;
	}

	a4l_info(dev, "Board found (name=%s), continue initialization ...",
		 boardptr->name);

	private(dev)->mite      = mitedev;
	private(dev)->board_ptr = boardptr;

	rtdm_lock_init(&private(dev)->mite_channel_lock);
	rtdm_lock_init(&private(dev)->interrupt_lock);
	rtdm_lock_init(&private(dev)->soft_reg_copy_lock);
	for (i = 0; i < NUM_PFI_CHANNELS; ++i) {
		private(dev)->pfi_output_selects[i] = pfi_output_select_counter;
	}

	ret = a4l_mite_setup(private(dev)->mite, 1);
	if (ret < 0) {
		a4l_err(dev, "%s: error setting up mite\n", __FUNCTION__);
		return ret;
	}

	ret = ni_660x_alloc_mite_rings(dev);
	if (ret < 0) {
		a4l_err(dev, "%s: error setting up mite rings\n", __FUNCTION__);
		return ret;
	}

	/* Setup first subdevice */
	s = a4l_alloc_subd(sizeof(struct ni_660x_subd_priv), NULL);
	if (s == NULL)
		return -ENOMEM;

	s->flags = A4L_SUBD_UNUSED;

	err = a4l_add_subd(dev, s);
	if (err != nsubdev) {
		a4l_info(dev, "cannot add first subdevice, returns %d, expect %d\n", err, i);
		return err;
	}

	nsubdev++;

	/* Setup second subdevice */
	s = a4l_alloc_subd(sizeof(struct ni_660x_subd_priv), NULL);
	if (s == NULL) {
		a4l_info(dev, "cannot allocate second subdevice\n");
		return -ENOMEM;
	}

	s->flags          = A4L_SUBD_DIO;
	s->flags         |= A4L_SUBD_CMD;
	s->chan_desc      = &chandesc_ni660x;
	s->rng_desc       = &range_digital;
	s->insn_bits      = ni_660x_dio_insn_bits;
	s->insn_config    = ni_660x_dio_insn_config;
	s->dev            = dev;
	subdev_priv->io_bits = 0;
	ni_660x_write_register(dev, 0, 0, STCDIOControl);

	err = a4l_add_subd(dev, s);
	if (err != nsubdev)
		return err;

	nsubdev++;

	private(dev)->counter_dev =
		a4l_ni_gpct_device_construct(dev,
					     &ni_gpct_write_register,
					     &ni_gpct_read_register,
					     ni_gpct_variant_660x,
					     ni_660x_num_counters (dev));
	if (private(dev)->counter_dev == NULL)
		return -ENOMEM;

	for (i = 0; i < ni_660x_num_counters(dev); ++i) {
		/* TODO: check why there are kmalloc here... and in pcimio */
		private(dev)->counter_dev->counters[i] =
			kmalloc(sizeof(struct ni_gpct), GFP_KERNEL);
		private(dev)->counter_dev->counters[i]->counter_dev =
			private(dev)->counter_dev;
		rtdm_lock_init(&(private(dev)->counter_dev->counters[i]->lock));
	}

	for (i = 0; i < NI_660X_MAX_NUM_COUNTERS; ++i) {
		if (i < ni_660x_num_counters(dev)) {
			/* Setup other subdevice */
			s = a4l_alloc_subd(sizeof(struct ni_660x_subd_priv), NULL);

			if (s == NULL)
				return -ENOMEM;

			s->flags             = A4L_SUBD_COUNTER;
			s->chan_desc         = rtdm_malloc (sizeof (struct a4l_channels_desc));
			s->chan_desc->length = 3;
			s->insn_read         = ni_660x_GPCT_rinsn;
			s->insn_write        = ni_660x_GPCT_winsn;
			s->insn_config       = ni_660x_GPCT_insn_config;
			s->do_cmd            = &ni_660x_cmd;
			s->do_cmdtest        = &ni_660x_cmdtest;
			s->cancel            = &ni_660x_cancel;

			subdev_priv->counter = private(dev)->counter_dev->counters[i];

			private(dev)->counter_dev->counters[i]->chip_index =
				i / counters_per_chip;
			private(dev)->counter_dev->counters[i]->counter_index =
				i % counters_per_chip;
		} else {
			s = a4l_alloc_subd(sizeof(struct ni_660x_subd_priv), NULL);
			if (s == NULL)
				return -ENOMEM;
			s->flags = A4L_SUBD_UNUSED;
		}

		err = a4l_add_subd(dev, s);

		if (err != nsubdev)
			return err;

		nsubdev++;
	}

	for (i = 0; i < board(dev)->n_chips; ++i)
		init_tio_chip(dev, i);

	for (i = 0; i < ni_660x_num_counters(dev); ++i)
		a4l_ni_tio_init_counter(private(dev)->counter_dev->counters[i]);

	for (i = 0; i < NUM_PFI_CHANNELS; ++i) {
		if (i < min_counter_pfi_chan)
			ni_660x_set_pfi_routing(dev, i, pfi_output_select_do);
		else
			ni_660x_set_pfi_routing(dev, i,
						pfi_output_select_counter);
		ni_660x_select_pfi_output(dev, i, pfi_output_select_high_Z);
	}


	/* To be safe, set counterswap bits on tio chips after all the
	   counter outputs have been set to high impedance mode */

	for (i = 0; i < board(dev)->n_chips; ++i)
		set_tio_counterswap(dev, i);

	ret = a4l_request_irq(dev,
			      mite_irq(private(dev)->mite),
			      ni_660x_interrupt, RTDM_IRQTYPE_SHARED, dev);

	if (ret < 0) {
		a4l_err(dev, "%s: IRQ not available\n", __FUNCTION__);
		return ret;
	}

	global_interrupt_config_bits = Global_Int_Enable_Bit;
	if (board(dev)->n_chips > 1)
		global_interrupt_config_bits |= Cascade_Int_Enable_Bit;

	ni_660x_write_register(dev, 0, global_interrupt_config_bits,
			       GlobalInterruptConfigRegister);

	a4l_info(dev, "attach succeed, ready to be used\n");

	return 0;
}

static int ni_660x_detach(struct a4l_device *dev)
{
	int i;

	a4l_info(dev, "begin to detach the driver ...");

	/* Free irq */
	if(a4l_get_irq(dev)!=A4L_IRQ_UNUSED)
		a4l_free_irq(dev,a4l_get_irq(dev));

	if (dev->priv) {

		if (private(dev)->counter_dev) {

			for (i = 0; i < ni_660x_num_counters(dev); ++i)
				if ((private(dev)->counter_dev->counters[i]) != NULL)
					kfree (private(dev)->counter_dev->counters[i]);

			a4l_ni_gpct_device_destroy(private(dev)->counter_dev);
		}

		if (private(dev)->mite) {
			ni_660x_free_mite_rings(dev);
			a4l_mite_unsetup(private(dev)->mite);
		}
	}

	a4l_info(dev, "driver detached !\n");

	return 0;
}

static int ni_660x_GPCT_rinsn(struct a4l_subdevice *s, struct a4l_kernel_instruction *insn)
{
	return a4l_ni_tio_rinsn(subdev_priv->counter, insn);
}

static void init_tio_chip(struct a4l_device *dev, int chipset)
{
	unsigned int i;

	/*  Init dma configuration register */
	private(dev)->dma_configuration_soft_copies[chipset] = 0;
	for (i = 0; i < MAX_DMA_CHANNEL; ++i) {
		private(dev)->dma_configuration_soft_copies[chipset] |=
		    dma_select_bits(i, dma_selection_none) & dma_select_mask(i);
	}

	ni_660x_write_register(dev, chipset,
			       private(dev)->
			       dma_configuration_soft_copies[chipset],
			       DMAConfigRegister);

	for (i = 0; i < NUM_PFI_CHANNELS; ++i)
		ni_660x_write_register(dev, chipset, 0, IOConfigReg(i));
}

static int ni_660x_GPCT_insn_config(struct a4l_subdevice *s, struct a4l_kernel_instruction *insn)
{
	return a4l_ni_tio_insn_config (subdev_priv->counter, insn);
}

static int ni_660x_GPCT_winsn(struct a4l_subdevice *s, struct a4l_kernel_instruction *insn)
{
	return a4l_ni_tio_winsn(subdev_priv->counter, insn);
}

static int ni_660x_dio_insn_bits(struct a4l_subdevice *s, struct a4l_kernel_instruction *insn)
{
	unsigned int* data = (unsigned int*) insn->data;
	unsigned int base_bitfield_channel = CR_CHAN(insn->chan_desc);

	/*  Check if we have to write some bits */
	if (data[0]) {
		subdev_priv->state &= ~(data[0] << base_bitfield_channel);
		subdev_priv->state |= (data[0] & data[1]) << base_bitfield_channel;
		/* Write out the new digital output lines */
		ni_660x_write_register(s->dev, 0, subdev_priv->state, DIO32Output);
	}

	/* On return, data[1] contains the value of the digital input
	   and output lines. */
	data[1] = ni_660x_read_register(s->dev, 0,DIO32Input) >>
		base_bitfield_channel;

	return 0;
}

static void ni_660x_select_pfi_output(struct a4l_device *dev,
				      unsigned pfi_channel,
				      unsigned output_select)
{
	static const unsigned counter_4_7_first_pfi = 8;
	static const unsigned counter_4_7_last_pfi = 23;
	unsigned active_chipset = 0;
	unsigned idle_chipset = 0;
	unsigned active_bits;
	unsigned idle_bits;

	if (board(dev)->n_chips > 1) {
		if (output_select == pfi_output_select_counter &&
		    pfi_channel >= counter_4_7_first_pfi &&
		    pfi_channel <= counter_4_7_last_pfi) {
			active_chipset = 1;
			idle_chipset = 0;
		} else {
			active_chipset = 0;
			idle_chipset = 1;
		}
	}

	if (idle_chipset != active_chipset) {

		idle_bits =ni_660x_read_register(dev, idle_chipset,
						 IOConfigReg(pfi_channel));
		idle_bits &= ~pfi_output_select_mask(pfi_channel);
		idle_bits |=
		    pfi_output_select_bits(pfi_channel,
					   pfi_output_select_high_Z);
		ni_660x_write_register(dev, idle_chipset, idle_bits,
				       IOConfigReg(pfi_channel));
	}

	active_bits =
	    ni_660x_read_register(dev, active_chipset,
				  IOConfigReg(pfi_channel));
	active_bits &= ~pfi_output_select_mask(pfi_channel);
	active_bits |= pfi_output_select_bits(pfi_channel, output_select);
	ni_660x_write_register(dev, active_chipset, active_bits,
			       IOConfigReg(pfi_channel));
}

static int ni_660x_set_pfi_routing(struct a4l_device *dev, unsigned chan,
				   unsigned source)
{
	BUG_ON(chan >= NUM_PFI_CHANNELS);

	if (source > num_pfi_output_selects)
		return -EINVAL;
	if (source == pfi_output_select_high_Z)
		return -EINVAL;
	if (chan < min_counter_pfi_chan) {
		if (source == pfi_output_select_counter)
			return -EINVAL;
	} else if (chan > max_dio_pfi_chan) {
		if (source == pfi_output_select_do)
			return -EINVAL;
	}
	BUG_ON(chan >= NUM_PFI_CHANNELS);

	private(dev)->pfi_output_selects[chan] = source;
	if (private(dev)->pfi_direction_bits & (((uint64_t) 1) << chan))
		ni_660x_select_pfi_output(dev, chan,
					  private(dev)->
					  pfi_output_selects[chan]);
	return 0;
}

static unsigned ni_660x_get_pfi_routing(struct a4l_device *dev,
					unsigned chan)
{
	BUG_ON(chan >= NUM_PFI_CHANNELS);
	return private(dev)->pfi_output_selects[chan];
}

static void ni660x_config_filter(struct a4l_device *dev,
				 unsigned pfi_channel,
				 int filter)
{
	unsigned int bits;

	bits = ni_660x_read_register(dev, 0, IOConfigReg(pfi_channel));
	bits &= ~pfi_input_select_mask(pfi_channel);
	bits |= pfi_input_select_bits(pfi_channel, filter);
	ni_660x_write_register(dev, 0, bits, IOConfigReg(pfi_channel));
}

static int ni_660x_dio_insn_config(struct a4l_subdevice *s, struct a4l_kernel_instruction *insn)
{
	unsigned int* data = insn->data;
	int chan = CR_CHAN(insn->chan_desc);
	struct a4l_device* dev = s->dev;

	if (data == NULL)
		return -EINVAL;

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */

	switch (data[0]) {
	case A4L_INSN_CONFIG_DIO_OUTPUT:
		private(dev)->pfi_direction_bits |= ((uint64_t) 1) << chan;
		ni_660x_select_pfi_output(dev, chan,
					  private(dev)->
					  pfi_output_selects[chan]);
		break;
	case A4L_INSN_CONFIG_DIO_INPUT:
		private(dev)->pfi_direction_bits &= ~(((uint64_t) 1) << chan);
		ni_660x_select_pfi_output(dev, chan, pfi_output_select_high_Z);
		break;
	case A4L_INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (private(dev)->pfi_direction_bits &
		     (((uint64_t) 1) << chan)) ? A4L_OUTPUT : A4L_INPUT;
		return 0;
	case A4L_INSN_CONFIG_SET_ROUTING:
		return ni_660x_set_pfi_routing(dev, chan, data[1]);
		break;
	case A4L_INSN_CONFIG_GET_ROUTING:
		data[1] = ni_660x_get_pfi_routing(dev, chan);
		break;
	case A4L_INSN_CONFIG_FILTER:
		ni660x_config_filter(dev, chan, data[1]);
		break;
	default:
		return -EINVAL;
		break;
	};

	return 0;
}


MODULE_DESCRIPTION("Analogy driver for NI660x series cards");
MODULE_LICENSE("GPL");
