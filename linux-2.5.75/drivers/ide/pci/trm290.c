/*
 *  linux/drivers/ide/pci/trm290.c		Version 1.02	Mar. 18, 2000
 *
 *  Copyright (c) 1997-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA function
 * of the Tekram TRM290 chip, used on a variety of PCI IDE add-on boards,
 * including a "Precision Instruments" board.  The TRM290 pre-dates
 * the sff-8038 standard (ide-dma.c) by a few months, and differs
 * significantly enough to warrant separate routines for some functions,
 * while re-using others from ide-dma.c.
 *
 * EXPERIMENTAL!  It works for me (a sample of one).
 *
 * Works reliably for me in DMA mode (READs only),
 * DMA WRITEs are disabled by default (see #define below);
 *
 * DMA is not enabled automatically for this chipset,
 * but can be turned on manually (with "hdparm -d1") at run time.
 *
 * I need volunteers with "spare" drives for further testing
 * and development, and maybe to help figure out the peculiarities.
 * Even knowing the registers (below), some things behave strangely.
 */

#define TRM290_NO_DMA_WRITES	/* DMA writes seem unreliable sometimes */

/*
 * TRM-290 PCI-IDE2 Bus Master Chip
 * ================================
 * The configuration registers are addressed in normal I/O port space
 * and are used as follows:
 *
 * trm290_base depends on jumper settings, and is probed for by ide-dma.c
 *
 * trm290_base+2 when WRITTEN: chiptest register (byte, write-only)
 *	bit7 must always be written as "1"
 *	bits6-2 undefined
 *	bit1 1=legacy_compatible_mode, 0=native_pci_mode
 *	bit0 1=test_mode, 0=normal(default)
 *
 * trm290_base+2 when READ: status register (byte, read-only)
 *	bits7-2 undefined
 *	bit1 channel0 busmaster interrupt status 0=none, 1=asserted
 *	bit0 channel0 interrupt status 0=none, 1=asserted
 *
 * trm290_base+3 Interrupt mask register
 *	bits7-5 undefined
 *	bit4 legacy_header: 1=present, 0=absent
 *	bit3 channel1 busmaster interrupt status 0=none, 1=asserted (read only)
 *	bit2 channel1 interrupt status 0=none, 1=asserted (read only)
 *	bit1 channel1 interrupt mask: 1=masked, 0=unmasked(default)
 *	bit0 channel0 interrupt mask: 1=masked, 0=unmasked(default)
 *
 * trm290_base+1 "CPR" Config Pointer Register (byte)
 *	bit7 1=autoincrement CPR bits 2-0 after each access of CDR
 *	bit6 1=min. 1 wait-state posted write cycle (default), 0=0 wait-state
 *	bit5 0=enabled master burst access (default), 1=disable  (write only)
 *	bit4 PCI DEVSEL# timing select: 1=medium(default), 0=fast
 *	bit3 0=primary IDE channel, 1=secondary IDE channel
 *	bits2-0 register index for accesses through CDR port
 *
 * trm290_base+0 "CDR" Config Data Register (word)
 *	two sets of seven config registers,
 *	selected by CPR bit 3 (channel) and CPR bits 2-0 (index 0 to 6),
 *	each index defined below:
 *
 * Index-0 Base address register for command block (word)
 *	defaults: 0x1f0 for primary, 0x170 for secondary
 *
 * Index-1 general config register (byte)
 *	bit7 1=DMA enable, 0=DMA disable
 *	bit6 1=activate IDE_RESET, 0=no action (default)
 *	bit5 1=enable IORDY, 0=disable IORDY (default)
 *	bit4 0=16-bit data port(default), 1=8-bit (XT) data port
 *	bit3 interrupt polarity: 1=active_low, 0=active_high(default)
 *	bit2 power-saving-mode(?): 1=enable, 0=disable(default) (write only)
 *	bit1 bus_master_mode(?): 1=enable, 0=disable(default)
 *	bit0 enable_io_ports: 1=enable(default), 0=disable
 *
 * Index-2 read-ahead counter preload bits 0-7 (byte, write only)
 *	bits7-0 bits7-0 of readahead count
 *
 * Index-3 read-ahead config register (byte, write only)
 *	bit7 1=enable_readahead, 0=disable_readahead(default)
 *	bit6 1=clear_FIFO, 0=no_action
 *	bit5 undefined
 *	bit4 mode4 timing control: 1=enable, 0=disable(default)
 *	bit3 undefined
 *	bit2 undefined
 *	bits1-0 bits9-8 of read-ahead count
 *
 * Index-4 base address register for control block (word)
 *	defaults: 0x3f6 for primary, 0x376 for secondary
 *
 * Index-5 data port timings (shared by both drives) (byte)
 *	standard PCI "clk" (clock) counts, default value = 0xf5
 *
 *	bits7-6 setup time:  00=1clk, 01=2clk, 10=3clk, 11=4clk
 *	bits5-3 hold time:	000=1clk, 001=2clk, 010=3clk,
 *				011=4clk, 100=5clk, 101=6clk,
 *				110=8clk, 111=12clk
 *	bits2-0 active time:	000=2clk, 001=3clk, 010=4clk,
 *				011=5clk, 100=6clk, 101=8clk,
 *				110=12clk, 111=16clk
 *
 * Index-6 command/control port timings (shared by both drives) (byte)
 *	same layout as Index-5, default value = 0xde
 *
 * Suggested CDR programming for PIO mode0 (600ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0xf5,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0xf5,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode3 (180ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x09,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x09,0xde	; secondary
 *
 * Suggested CDR programming for PIO mode4 (120ns):
 *	0x01f0,0x21,0xff,0x80,0x03f6,0x00,0xde	; primary
 *	0x0170,0x21,0xff,0x80,0x0376,0x00,0xde	; secondary
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "trm290.h"

static void trm290_prepare_drive (ide_drive_t *drive, unsigned int use_dma)
{
	ide_hwif_t *hwif = HWIF(drive);
	u16 reg = 0;
	unsigned long flags;

	/* select PIO or DMA */
	reg = use_dma ? (0x21 | 0x82) : (0x21 & ~0x82);

	local_irq_save(flags);

	if (reg != hwif->select_data) {
		hwif->select_data = reg;
		/* set PIO/DMA */
		hwif->OUTB(0x51|(hwif->channel<<3), hwif->config_data+1);
		hwif->OUTW(reg & 0xff, hwif->config_data);
	}

	/* enable IRQ if not probing */
	if (drive->present) {
		reg = hwif->INW(hwif->config_data + 3);
		reg &= 0x13;
		reg &= ~(1 << hwif->channel);
		hwif->OUTW(reg, hwif->config_data+3);
	}

	local_irq_restore(flags);
}

static void trm290_selectproc (ide_drive_t *drive)
{
	trm290_prepare_drive(drive, drive->using_dma);
}

#ifdef CONFIG_BLK_DEV_IDEDMA
static int trm290_ide_dma_write (ide_drive_t *drive /*, struct request *rq */)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= HWGROUP(drive)->rq;
//	ide_task_t *args	= rq->special;
	task_ioreg_t command	= WIN_NOP;
	unsigned int count, reading = 2, writing = 0;

	reading = 0;
	writing = 1;
#ifdef TRM290_NO_DMA_WRITES
	/* always use PIO for writes */
	trm290_prepare_drive(drive, 0);	/* select PIO xfer */
	return 1;
#endif
	if (!(count = ide_build_dmatable(drive, rq))) {
		/* try PIO instead of DMA */
		trm290_prepare_drive(drive, 0); /* select PIO xfer */
		return 1;
	}
	/* select DMA xfer */
	trm290_prepare_drive(drive, 1);
	hwif->OUTL(hwif->dmatable_dma|reading|writing, hwif->dma_command);
	drive->waiting_for_dma = 1;
	/* start DMA */
	hwif->OUTW((count * 2) - 1, hwif->dma_status);
	if (drive->media != ide_disk)
		return 0;
	if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
		BUG();
	ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);
	/*
	 * FIX ME to use only ACB ide_task_t args Struct
	 */
#if 0
	{
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#else
	command = /* (lba48) ? WIN_READDMA_EXT : */ WIN_READDMA;
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#endif
	/* issue cmd to drive */
	hwif->OUTB(command, IDE_COMMAND_REG);
	return HWIF(drive)->ide_dma_count(drive);
}

static int trm290_ide_dma_read (ide_drive_t *drive  /*, struct request *rq */)
{
	ide_hwif_t *hwif	= HWIF(drive);
	struct request *rq	= HWGROUP(drive)->rq;
//	ide_task_t *args	= rq->special;
	task_ioreg_t command	= WIN_NOP;
	unsigned int count, reading = 2, writing = 0;

	if (!(count = ide_build_dmatable(drive, rq))) {
		/* try PIO instead of DMA */
		trm290_prepare_drive(drive, 0); /* select PIO xfer */
		return 1;
	}
	/* select DMA xfer */
	trm290_prepare_drive(drive, 1);
	hwif->OUTL(hwif->dmatable_dma|reading|writing, hwif->dma_command);
	drive->waiting_for_dma = 1;
	/* start DMA */
	hwif->OUTW((count * 2) - 1, hwif->dma_status);
	if (drive->media != ide_disk)
		return 0;
	if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
		BUG();
	ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);
	/*
	 * FIX ME to use only ACB ide_task_t args Struct
	 */
#if 0
	{
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#else
	command = /* (lba48) ? WIN_WRITEDMA_EXT : */ WIN_WRITEDMA;
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#endif
	/* issue cmd to drive */
	hwif->OUTB(command, IDE_COMMAND_REG);
	return HWIF(drive)->ide_dma_count(drive);
}

static int trm290_ide_dma_begin (ide_drive_t *drive)
{
	return 0;
}

static int trm290_ide_dma_end (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u16 status = 0;;

	drive->waiting_for_dma = 0;
	/* purge DMA mappings */
	ide_destroy_dmatable(drive);
	status = hwif->INW(hwif->dma_status);
	return (status != 0x00ff);
}

static int trm290_ide_dma_test_irq (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	u16 status = 0;

	status = hwif->INW(hwif->dma_status);
	return (status == 0x00ff);
}
#endif /* CONFIG_BLK_DEV_IDEDMA */

/*
 * Invoked from ide-dma.c at boot time.
 */
void __init init_hwif_trm290 (ide_hwif_t *hwif)
{
	unsigned int cfgbase = 0;
	unsigned long flags;
	u8 reg = 0;
	struct pci_dev *dev = hwif->pci_dev;

	hwif->addressing = 1;
	hwif->chipset = ide_trm290;
	cfgbase = pci_resource_start(dev, 4);
	if ((dev->class & 5) && cfgbase) {
		hwif->config_data = cfgbase;
		printk(KERN_INFO "TRM290: chip config base at 0x%04lx\n",
			hwif->config_data);
	} else {
		hwif->config_data = 0x3df0;
		printk(KERN_INFO "TRM290: using default config base at 0x%04lx\n",
			hwif->config_data);
	}

	local_irq_save(flags);
	/* put config reg into first byte of hwif->select_data */
	hwif->OUTB(0x51|(hwif->channel<<3), hwif->config_data+1);
	/* select PIO as default */
	hwif->select_data = 0x21;
	hwif->OUTB(hwif->select_data, hwif->config_data);
	/* get IRQ info */
	reg = hwif->INB(hwif->config_data+3);
	/* mask IRQs for both ports */
	reg = (reg & 0x10) | 0x03;
	hwif->OUTB(reg, hwif->config_data+3);
	local_irq_restore(flags);

	if ((reg & 0x10))
		/* legacy mode */
		hwif->irq = hwif->channel ? 15 : 14;
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		/* sharing IRQ with mate */
		hwif->irq = hwif->mate->irq;

	ide_setup_dma(hwif, (hwif->config_data + 4) ^ (hwif->channel ? 0x0080 : 0x0000), 3);

#ifdef CONFIG_BLK_DEV_IDEDMA
	hwif->ide_dma_write = &trm290_ide_dma_write;
	hwif->ide_dma_read = &trm290_ide_dma_read;
	hwif->ide_dma_begin = &trm290_ide_dma_begin;
	hwif->ide_dma_end = &trm290_ide_dma_end;
	hwif->ide_dma_test_irq = &trm290_ide_dma_test_irq;
#endif /* CONFIG_BLK_DEV_IDEDMA */

	hwif->selectproc = &trm290_selectproc;
	hwif->autodma = 0;		/* play it safe for now */
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#if 1
	{
	/*
	 * My trm290-based card doesn't seem to work with all possible values
	 * for the control basereg, so this kludge ensures that we use only
	 * values that are known to work.  Ugh.		-ml
	 */
		u16 new, old, compat = hwif->channel ? 0x374 : 0x3f4;
		static u16 next_offset = 0;
		u8 old_mask;

		hwif->OUTB(0x54|(hwif->channel<<3), hwif->config_data+1);
		old = hwif->INW(hwif->config_data);
		old &= ~1;
		old_mask = hwif->INB(old+2);
		if (old != compat && old_mask == 0xff) {
			/* leave lower 10 bits untouched */
			compat += (next_offset += 0x400);
#  if 1
			if (check_region(compat + 2, 1))
				printk(KERN_ERR "%s: check_region failure at 0x%04x\n",
					hwif->name, (compat + 2));
			/*
			 * The region check is not needed; however.........
			 * Since this is the checked in ide-probe.c,
			 * this is only an assignment.
			 */
#  endif
			hwif->io_ports[IDE_CONTROL_OFFSET] = compat + 2;
			hwif->OUTW(compat|1, hwif->config_data);
			new = hwif->INW(hwif->config_data);
			printk(KERN_INFO "%s: control basereg workaround: "
				"old=0x%04x, new=0x%04x\n",
				hwif->name, old, new & ~1);
		}
	}
#endif
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static int __devinit trm290_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &trm290_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id trm290_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_TEKRAM, PCI_DEVICE_ID_TEKRAM_DC290, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "TRM290 IDE",
	.id_table	= trm290_pci_tbl,
	.probe		= trm290_init_one,
};

static int trm290_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void trm290_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(trm290_ide_init);
module_exit(trm290_ide_exit);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("PCI driver module for Tekram TRM290 IDE");
MODULE_LICENSE("GPL");
