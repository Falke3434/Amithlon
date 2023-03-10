/*
 *	IDE tuning and bus mastering support for the CS5510/CS5520
 *	chipsets
 *
 *	The CS5510/CS5520 are slightly unusual devices. Unlike the 
 *	typical IDE controllers they do bus mastering with the drive in
 *	PIO mode and smarter silicon.
 *
 *	The practical upshot of this is that we must always tune the
 *	drive for the right PIO mode. We must also ignore all the blacklists
 *	and the drive bus mastering DMA information.
 *
 *	*** This driver is strictly experimental ***
 *
 *	(c) Copyright Red Hat Inc 2002
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * For the avoidance of doubt the "preferred form" of this code is one which
 * is in an open non patent encumbered format. Where cryptographic key signing
 * forms part of the process of creating an executable the information
 * including keys needed to generate an equivalently functional executable
 * are deemed to be part of the source code.
 *
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
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide_modes.h"
#include "cs5520.h"

#if defined(DISPLAY_CS5520_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 cs5520_proc = 0;
static struct pci_dev *bmide_dev;

static int cs5520_get_info(char *buffer, char **addr, off_t offset, int count)
{
	char *p = buffer;
	unsigned long bmiba = pci_resource_start(bmide_dev, 2);
	int len;
	u8 c0 = 0, c1 = 0;
	u16 reg16;
	u32 reg32;

	/*
	 * at that point bibma+0x2 et bibma+0xa are byte registers
	 * to investigate:
	 */
	c0 = inb(bmiba + 0x02);
	c1 = inb(bmiba + 0x0a);
	
	p += sprintf(p, "\nCyrix CS55x0 IDE\n");
	p += sprintf(p, "--------------- Primary Channel "
			"---------------- Secondary Channel "
			"-------------\n");
	p += sprintf(p, "                %sabled "
			"                        %sabled\n",
			(c0&0x80) ? "dis" : " en",
			(c1&0x80) ? "dis" : " en");
			
	p += sprintf(p, "\n\nTimings: \n");
	
	pci_read_config_word(bmide_dev, 0x62, &reg16);
	p += sprintf(p, "8bit CAT/CRT   : %04x\n", reg16);
	pci_read_config_dword(bmide_dev, 0x64, &reg32);
	p += sprintf(p, "16bit Primary  : %08x\n", reg32);
	pci_read_config_dword(bmide_dev, 0x68, &reg32);
	p += sprintf(p, "16bit Secondary: %08x\n", reg32);
	
	len = (p - buffer) - offset;
	*addr = buffer + offset;
	
	return len > count ? count : len;
}

#endif

struct pio_clocks
{
	int address;
	int assert;
	int recovery;
};

struct pio_clocks cs5520_pio_clocks[]={
	{3, 6, 11},
	{2, 5, 6},
	{1, 4, 3},
	{1, 3, 2},
	{1, 2, 1}
};

static int cs5520_tune_chipset(ide_drive_t *drive, u8 xferspeed)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *pdev = hwif->pci_dev;
	u8 speed = min((u8)XFER_PIO_4, xferspeed);
	int pio = speed;
	u8 reg;
	int controller = drive->dn > 1 ? 1 : 0;
	int error;
	
	switch(speed)
	{
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pio -= XFER_PIO_0;
			break;
		default:
			pio = 0;
			printk(KERN_ERR "cs55x0: bad ide timing.\n");
	}
	
	printk("PIO clocking = %d\n", pio);
	
	/* FIXME: if DMA = 1 do we need to set the DMA bit here ? */
	
	/* 8bit command timing for channel */
	pci_write_config_byte(pdev, 0x62 + controller, 
		(cs5520_pio_clocks[pio].recovery << 4) |
		(cs5520_pio_clocks[pio].assert));
		
	/* FIXME: should these use address ? */
	/* Data read timing */
	pci_write_config_byte(pdev, 0x64 + 4*controller + (drive->dn&1),
		(cs5520_pio_clocks[pio].recovery << 4) |
		(cs5520_pio_clocks[pio].assert));
	/* Write command timing */
	pci_write_config_byte(pdev, 0x66 + 4*controller + (drive->dn&1),
		(cs5520_pio_clocks[pio].recovery << 4) |
		(cs5520_pio_clocks[pio].assert));
		
	/* Set the DMA enable/disable flag */
	reg = inb(hwif->dma_base + 0x02 + 8*controller);
	reg |= 1<<((drive->dn&1)+5);
	outb(reg, hwif->dma_base + 0x02 + 8*controller);
		
	error = ide_config_drive_speed(drive, speed);
	/* ATAPI is harder so leave it for now */
	if(!error && drive->media == ide_disk)
		error = hwif->ide_dma_on(drive);

	return error;
}	
	
static void cs5520_tune_drive(ide_drive_t *drive, u8 pio)
{
	pio = ide_get_best_pio_mode(drive, pio, 4, NULL);
	cs5520_tune_chipset(drive, (XFER_PIO_0 + pio));
}

static int cs5520_config_drive_xfer_rate(ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	/* Tune the drive for PIO modes up to PIO 4 */	
	cs5520_tune_drive(drive, 4);
	/* Then tell the core to use DMA operations */
	return hwif->ide_dma_on(drive);
}
	
	
static unsigned int __devinit init_chipset_cs5520(struct pci_dev *dev, const char *name)
{
#if defined(DISPLAY_CS5520_TIMINGS) && defined(CONFIG_PROC_FS)
	if (!cs5520_proc) {
		cs5520_proc = 1;
		bmide_dev = dev;
		ide_pci_register_host_proc(&cs5520_procs[0]);
	}
#endif /* DISPLAY_CS5520_TIMINGS && CONFIG_PROC_FS */
	return 0;
}

/*
 *	We provide a callback for our nonstandard DMA location
 */

static void __devinit cs5520_init_setup_dma(struct pci_dev *dev, ide_pci_device_t *d, ide_hwif_t *hwif)
{
	unsigned long bmide = pci_resource_start(dev, 2);	/* Not the usual 4 */
	if(hwif->mate && hwif->mate->dma_base)	/* Second channel at primary + 8 */
		bmide += 8;
	ide_setup_dma(hwif, bmide, 8);
}

/*
 *	We wrap the DMA activate to set the vdma flag. This is needed
 *	so that the IDE DMA layer issues PIO not DMA commands over the
 *	DMA channel
 */
 
static int cs5520_dma_on(ide_drive_t *drive)
{
	drive->vdma = 1;
	return 0;
}

static void __devinit init_hwif_cs5520(ide_hwif_t *hwif)
{
	hwif->tuneproc = &cs5520_tune_drive;
	hwif->speedproc = &cs5520_tune_chipset;
	hwif->ide_dma_check = &cs5520_config_drive_xfer_rate;
	hwif->ide_dma_on = &cs5520_dma_on;

	if(!noautodma)
		hwif->autodma = 1;
	
	if(!hwif->dma_base)
	{
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		return;
	}
	
	hwif->atapi_dma = 0;
	hwif->ultra_mask = 0;
	hwif->swdma_mask = 0;
	hwif->mwdma_mask = 0;
	
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
}
		
/*
 *	The 5510/5520 are a bit weird. They don't quite set up the way
 *	the PCI helper layer expects so we must do much of the set up 
 *	work longhand.
 */
 
static int __devinit cs5520_init_one(struct pci_dev *dev, const struct pci_device_id *id)
{
	ata_index_t index;
	ide_pci_device_t *d = &cyrix_chipsets[id->driver_data];

	ide_setup_pci_noise(dev, d);

	/* We must not grab the entire device, it has 'ISA' space in its
	   BARS too and we will freak out other bits of the kernel */
	if(pci_enable_device_bars(dev, 1<<2))
	{
		printk(KERN_WARNING "%s: Unable to enable 55x0.\n", d->name);
		return 1;
	}
	pci_set_master(dev);
	pci_set_dma_mask(dev, 0xFFFFFFFF);
	init_chipset_cs5520(dev, d->name);

	index.all = 0xf0f0;

	/*
	 *	Now the chipset is configured we can let the core
	 *	do all the device setup for us
	 */

	ide_pci_setup_ports(dev, d, 1, 14, &index);

	printk("Index.b %d %d\n", index.b.low, index.b.high);
	mdelay(2000);
	if((index.b.low & 0xf0) != 0xf0)
		probe_hwif_init(&ide_hwifs[index.b.low]);
	if((index.b.high & 0xf0) != 0xf0)
		probe_hwif_init(&ide_hwifs[index.b.high]);
	MOD_INC_USE_COUNT;
	return 0;
}

static struct pci_device_id cs5520_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5510, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_CYRIX, PCI_DEVICE_ID_CYRIX_5520, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ 0, },
};

static struct pci_driver driver = {
	.name		= "CyrixIDE",
	.id_table	= cs5520_pci_tbl,
	.probe		= cs5520_init_one,
};

static int cs5520_ide_init(void)
{
	return ide_pci_register_driver(&driver);
}

static void cs5520_ide_exit(void)
{
	return ide_pci_unregister_driver(&driver);
}

module_init(cs5520_ide_init);
module_exit(cs5520_ide_exit);

MODULE_AUTHOR("Alan Cox");
MODULE_DESCRIPTION("PCI driver module for Cyrix 5510/5520 IDE");
MODULE_LICENSE("GPL");
