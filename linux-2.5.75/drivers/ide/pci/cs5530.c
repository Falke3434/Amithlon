/*
 * linux/drivers/ide/pci/cs5530.c		Version 0.7	Sept 10, 2002
 *
 * Copyright (C) 2000			Andre Hedrick <andre@linux-ide.org>
 * Ditto of GNU General Public License.
 *
 * Copyright (C) 2000			Mark Lord <mlord@pobox.com>
 * May be copied or modified under the terms of the GNU General Public License
 *
 * Development of this chipset driver was funded
 * by the nice folks at National Semiconductor.
 *
 * Documentation:
 *	CS5530 documentation available from National Semiconductor.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"
#include "cs5530.h"

#if defined(DISPLAY_CS5530_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 cs5530_proc = 0;

static struct pci_dev *bmide_dev;

static int cs5530_get_info (char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	unsigned long bibma = pci_resource_start(bmide_dev, 4);
	u8  c0 = 0, c1 = 0;

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */

	c0 = inb_p((u16)bibma + 0x02);
	c1 = inb_p((u16)bibma + 0x0a);

	p += sprintf(p, "\n                                "
			"Cyrix 5530 Chipset.\n");
	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled "
			"                        %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
	p += sprintf(p, "--------------- drive0 --------- drive1 "
			"-------- drive0 ---------- drive1 ------\n");
	p += sprintf(p, "DMA enabled:    %s              %s "
			"            %s               %s\n",
			(c0&0x20) ? "yes" : "no ",
			(c0&0x40) ? "yes" : "no ",
			(c1&0x20) ? "yes" : "no ",
			(c1&0x40) ? "yes" : "no " );

	p += sprintf(p, "UDMA\n");
	p += sprintf(p, "DMA\n");
	p += sprintf(p, "PIO\n");

	return p-buffer;
}
#endif /* DISPLAY_CS5530_TIMINGS && CONFIG_PROC_FS */

/**
 *	cs5530_xfer_set_mode	-	set a new transfer mode at the drive
 *	@drive: drive to tune
 *	@mode: new mode
 *
 *	Logging wrapper to the IDE driver speed configuration. This can
 *	probably go away now.
 */
 
static int cs5530_set_xfer_mode (ide_drive_t *drive, u8 mode)
{
	printk(KERN_DEBUG "%s: cs5530_set_xfer_mode(%s)\n",
		drive->name, ide_xfer_verbose(mode));
	return (ide_config_drive_speed(drive, mode));
}

/*
 * Here are the standard PIO mode 0-4 timings for each "format".
 * Format-0 uses fast data reg timings, with slower command reg timings.
 * Format-1 uses fast timings for all registers, but won't work with all drives.
 */
static unsigned int cs5530_pio_timings[2][5] = {
	{0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
	{0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}
};

/*
 * After chip reset, the PIO timings are set to 0x0000e132, which is not valid.
 */
#define CS5530_BAD_PIO(timings) (((timings)&~0x80000000)==0x0000e132)
#define CS5530_BASEREG(hwif)	(((hwif)->dma_base & ~0xf) + ((hwif)->channel ? 0x30 : 0x20))

/**
 *	cs5530_tuneproc		-	select/set PIO modes
 *
 *	cs5530_tuneproc() handles selection/setting of PIO modes
 *	for both the chipset and drive.
 *
 *	The ide_init_cs5530() routine guarantees that all drives
 *	will have valid default PIO timings set up before we get here.
 */

static void cs5530_tuneproc (ide_drive_t *drive, u8 pio)	/* pio=255 means "autotune" */
{
	ide_hwif_t	*hwif = HWIF(drive);
	unsigned int	format;
	unsigned long basereg = CS5530_BASEREG(hwif);
	static u8	modes[5] = { XFER_PIO_0, XFER_PIO_1, XFER_PIO_2, XFER_PIO_3, XFER_PIO_4};

	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	if (!cs5530_set_xfer_mode(drive, modes[pio])) {
		format = (hwif->INL(basereg+4) >> 31) & 1;
		hwif->OUTL(cs5530_pio_timings[format][pio],
			basereg+(drive->select.b.unit<<3));
	}
}

/**
 *	cs5530_config_dma	-	select/set DMA and UDMA modes
 *	@drive: drive to tune
 *
 *	cs5530_config_dma() handles selection/setting of DMA/UDMA modes
 *	for both the chipset and drive. The CS5530 has limitations about
 *	mixing DMA/UDMA on the same cable.
 */
 
static int cs5530_config_dma (ide_drive_t *drive)
{
	int			udma_ok = 1, mode = 0;
	ide_hwif_t		*hwif = HWIF(drive);
	int			unit = drive->select.b.unit;
	ide_drive_t		*mate = &hwif->drives[unit^1];
	struct hd_driveid	*id = drive->id;
	unsigned int		reg, timings;
	unsigned long		basereg;

	/*
	 * Default to DMA-off in case we run into trouble here.
	 */
	hwif->ide_dma_off_quietly(drive);
	/* turn off DMA while we fiddle */
	hwif->ide_dma_host_off(drive);
	/* clear DMA_capable bit */

	/*
	 * The CS5530 specifies that two drives sharing a cable cannot
	 * mix UDMA/MDMA.  It has to be one or the other, for the pair,
	 * though different timings can still be chosen for each drive.
	 * We could set the appropriate timing bits on the fly,
	 * but that might be a bit confusing.  So, for now we statically
	 * handle this requirement by looking at our mate drive to see
	 * what it is capable of, before choosing a mode for our own drive.
	 *
	 * Note: This relies on the fact we never fail from UDMA to MWDMA_2
	 * but instead drop to PIO
	 */
	if (mate->present) {
		struct hd_driveid *mateid = mate->id;
		if (mateid && (mateid->capability & 1) &&
		    !hwif->ide_dma_bad_drive(mate)) {
			if ((mateid->field_valid & 4) &&
			    (mateid->dma_ultra & 7))
				udma_ok = 1;
			else if ((mateid->field_valid & 2) &&
				 (mateid->dma_mword & 7))
				udma_ok = 0;
			else
				udma_ok = 1;
		}
	}

	/*
	 * Now see what the current drive is capable of,
	 * selecting UDMA only if the mate said it was ok.
	 */
	if (id && (id->capability & 1) && drive->autodma &&
	    !hwif->ide_dma_bad_drive(drive)) {
		if (udma_ok && (id->field_valid & 4) && (id->dma_ultra & 7)) {
			if      (id->dma_ultra & 4)
				mode = XFER_UDMA_2;
			else if (id->dma_ultra & 2)
				mode = XFER_UDMA_1;
			else if (id->dma_ultra & 1)
				mode = XFER_UDMA_0;
		}
		if (!mode && (id->field_valid & 2) && (id->dma_mword & 7)) {
			if      (id->dma_mword & 4)
				mode = XFER_MW_DMA_2;
			else if (id->dma_mword & 2)
				mode = XFER_MW_DMA_1;
			else if (id->dma_mword & 1)
				mode = XFER_MW_DMA_0;
		}
	}

	/*
	 * Tell the drive to switch to the new mode; abort on failure.
	 */
	if (!mode || cs5530_set_xfer_mode(drive, mode))
		return 1;	/* failure */

	/*
	 * Now tune the chipset to match the drive:
	 */
	switch (mode) {
		case XFER_UDMA_0:	timings = 0x00921250; break;
		case XFER_UDMA_1:	timings = 0x00911140; break;
		case XFER_UDMA_2:	timings = 0x00911030; break;
		case XFER_MW_DMA_0:	timings = 0x00077771; break;
		case XFER_MW_DMA_1:	timings = 0x00012121; break;
		case XFER_MW_DMA_2:	timings = 0x00002020; break;
		default:
			printk(KERN_ERR "%s: cs5530_config_dma: huh? mode=%02x\n",
				drive->name, mode);
			return 1;	/* failure */
	}
	basereg = CS5530_BASEREG(hwif);
	reg = hwif->INL(basereg+4);		/* get drive0 config register */
	timings |= reg & 0x80000000;		/* preserve PIO format bit */
	if (unit == 0) {			/* are we configuring drive0? */
		hwif->OUTL(timings, basereg+4);	/* write drive0 config register */
	} else {
		if (timings & 0x00100000)
			reg |=  0x00100000;	/* enable UDMA timings for both drives */
		else
			reg &= ~0x00100000;	/* disable UDMA timings for both drives */
		hwif->OUTL(reg,     basereg+4);	/* write drive0 config register */
		hwif->OUTL(timings, basereg+12);	/* write drive1 config register */
	}
	(void) hwif->ide_dma_host_on(drive);
	/* set DMA_capable bit */

	/*
	 * Finally, turn DMA on in software, and exit.
	 */
	return hwif->ide_dma_on(drive);	/* success */
}

/**
 *	init_chipset_5530	-	set up 5530 bridge
 *	@dev: PCI device
 *	@name: device name
 *
 *	Initialize the cs5530 bridge for reliable IDE DMA operation.
 */

static unsigned int __init init_chipset_cs5530 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *master_0 = NULL, *cs5530_0 = NULL;
	unsigned long flags;

#if defined(DISPLAY_CS5530_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!cs5530_proc) {
		cs5530_proc = 1;
		bmide_dev = dev;
		ide_pci_register_host_proc(&cs5530_procs[0]);
	}
#endif /* DISPLAY_CS5530_TIMINGS && CONFIG_PROC_FS */

	dev = NULL;
	while ((dev = pci_find_device(PCI_VENDOR_ID_CYRIX, PCI_ANY_ID, dev)) != NULL) {
		switch (dev->device) {
			case PCI_DEVICE_ID_CYRIX_PCI_MASTER:
				master_0 = dev;
				break;
			case PCI_DEVICE_ID_CYRIX_5530_LEGACY:
				cs5530_0 = dev;
				break;
		}
	}
	if (!master_0) {
		printk(KERN_ERR "%s: unable to locate PCI MASTER function\n", name);
		return 0;
	}
	if (!cs5530_0) {
		printk(KERN_ERR "%s: unable to locate CS5530 LEGACY function\n", name);
		return 0;
	}

	spin_lock_irqsave(&ide_lock, flags);
		/* all CPUs (there should only be one CPU with this chipset) */

	/*
	 * Enable BusMaster and MemoryWriteAndInvalidate for the cs5530:
	 * -->  OR 0x14 into 16-bit PCI COMMAND reg of function 0 of the cs5530
	 */

	pci_set_master(cs5530_0);
	pci_set_mwi(cs5530_0);

	/*
	 * Set PCI CacheLineSize to 16-bytes:
	 * --> Write 0x04 into 8-bit PCI CACHELINESIZE reg of function 0 of the cs5530
	 */

	pci_write_config_byte(cs5530_0, PCI_CACHE_LINE_SIZE, 0x04);

	/*
	 * Disable trapping of UDMA register accesses (Win98 hack):
	 * --> Write 0x5006 into 16-bit reg at offset 0xd0 of function 0 of the cs5530
	 */

	pci_write_config_word(cs5530_0, 0xd0, 0x5006);

	/*
	 * Bit-1 at 0x40 enables MemoryWriteAndInvalidate on internal X-bus:
	 * The other settings are what is necessary to get the register
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x40, 0x1e);

	/* 
	 * Set max PCI burst size (16-bytes seems to work best):
	 *	   16bytes: set bit-1 at 0x41 (reg value of 0x16)
	 *	all others: clear bit-1 at 0x41, and do:
	 *	  128bytes: OR 0x00 at 0x41
	 *	  256bytes: OR 0x04 at 0x41
	 *	  512bytes: OR 0x08 at 0x41
	 *	 1024bytes: OR 0x0c at 0x41
	 */

	pci_write_config_byte(master_0, 0x41, 0x14);

	/*
	 * These settings are necessary to get the chip
	 * into a sane state for IDE DMA operation.
	 */

	pci_write_config_byte(master_0, 0x42, 0x00);
	pci_write_config_byte(master_0, 0x43, 0xc1);

	spin_unlock_irqrestore(&ide_lock, flags);

	return 0;
}

/**
 *	init_hwif_cs5530	-	initialise an IDE channel
 *	@hwif: IDE to initialize
 *
 *	This gets invoked by the IDE driver once for each channel. It
 *	performs channel-specific pre-initialization before drive probing.
 */

static void __init init_hwif_cs5530 (ide_hwif_t *hwif)
{
	unsigned long basereg;
	u32 d0_timings;
	hwif->autodma = 0;

	if (hwif->mate)
		hwif->serialized = hwif->mate->serialized = 1;

	hwif->tuneproc = &cs5530_tuneproc;
	basereg = CS5530_BASEREG(hwif);
	d0_timings = hwif->INL(basereg+0);
	if (CS5530_BAD_PIO(d0_timings)) {
		/* PIO timings not initialized? */
		hwif->OUTL(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+0);
		if (!hwif->drives[0].autotune)
			hwif->drives[0].autotune = 1;
			/* needs autotuning later */
	}
	if (CS5530_BAD_PIO(hwif->INL(basereg+8))) {
	/* PIO timings not initialized? */
		hwif->OUTL(cs5530_pio_timings[(d0_timings>>31)&1][0], basereg+8);
		if (!hwif->drives[1].autotune)
			hwif->drives[1].autotune = 1;
			/* needs autotuning later */
	}

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x07;
	hwif->mwdma_mask = 0x07;

	hwif->ide_dma_check = &cs5530_config_dma;
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}

/**
 *	init_dma_cs5530		-	set up for DMA
 *	@hwif: interface
 *	@dmabase: DMA base address
 *
 *	FIXME: this can go away
 */
 
static void __init init_dma_cs5530 (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);


static int __devinit cs5530_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ide_pci_device_t *d = &cs5530_chipsets[id->driver_data];
	if (dev->device != d->device)
		BUG();
	ide_setup_pci_device(dev, d);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id cs5530_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5530_IDE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "CS5530 IDE",
	.id_table	= cs5530_pci_tbl,
	.probe		= cs5530_init_one,
};

static int cs5530_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void cs5530_ide_exit(void)
{
	ide_pci_unregister_driver(&driver);
}

module_init(cs5530_ide_init);
module_exit(cs5530_ide_exit);

MODULE_AUTHOR("Mark Lord");
MODULE_DESCRIPTION("PCI driver module for Cyrix/NS 5530 IDE");
MODULE_LICENSE("GPL");
