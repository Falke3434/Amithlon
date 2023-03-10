/*
 *  skisa.c: A network driver for SK-NET TMS380-based ISA token ring cards.
 *
 *  Based on tmspci written 1999 by Adam Fritzler
 *  
 *  Written 2000 by Jochen Friedrich
 *  Dedicated to my girlfriend Steffi Bopp
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *	- SysKonnect TR4/16(+) ISA	(SK-4190)
 *
 *  Maintainer(s):
 *    AF        Adam Fritzler           mid@auk.cx
 *    JF	Jochen Friedrich	jochen@scram.de
 *
 *  Modification History:
 *	14-Jan-01	JF	Created
 *	28-Oct-02	JF	Fixed probe of card for static compilation.
 *				Fixed module init to not make hotplug go wild.
 *	09-Nov-02	JF	Fixed early bail out on out of memory
 *				situations if multiple cards are found.
 *				Cleaned up some unnecessary console SPAM.
 *	09-Dec-02	JF	Fixed module reference counting.
 *	02-Jan-03	JF	Renamed to skisa.c
 *
 */
static const char version[] = "skisa.c: v1.03 09/12/2002 by Jochen Friedrich\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/trdevice.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pci.h>
#include <asm/dma.h>

#include "tms380tr.h"

#define SK_ISA_IO_EXTENT 32

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int portlist[] __initdata = {
	0x0A20, 0x1A20, 0x0B20, 0x1B20, 0x0980, 0x1980, 0x0900, 0x1900,// SK
	0
};

/* A zero-terminated list of IRQs to be probed. 
 * Used again after initial probe for sktr_chipset_init, called from sktr_open.
 */
static unsigned short irqlist[] = {
	3, 5, 9, 10, 11, 12, 15,
	0
};

/* A zero-terminated list of DMAs to be probed. */
static int dmalist[] __initdata = {
	5, 6, 7,
	0
};

static char isa_cardname[] = "SK NET TR 4/16 ISA\0";

int sk_isa_probe(struct net_device *dev);
static int sk_isa_open(struct net_device *dev);
static int sk_isa_close(struct net_device *dev);
static void sk_isa_read_eeprom(struct net_device *dev);
static unsigned short sk_isa_setnselout_pins(struct net_device *dev);

static unsigned short sk_isa_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short sk_isa_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void sk_isa_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void sk_isa_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}

struct sk_isa_card {
	struct net_device *dev;
	struct sk_isa_card *next;
};

static struct sk_isa_card *sk_isa_card_list;

static int __init sk_isa_probe1(int ioaddr)
{
	unsigned char old, chk1, chk2;

	old = inb(ioaddr + SIFADR);	/* Get the old SIFADR value */

	chk1 = 0;	/* Begin with check value 0 */
	do {
		/* Write new SIFADR value */
		outb(chk1, ioaddr + SIFADR);

		/* Read, invert and write */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;
		outb(chk2, ioaddr + SIFADR);

		/* Read, invert and compare */
		chk2 = inb(ioaddr + SIFADD);
		chk2 ^= 0x0FE;

		if(chk1 != chk2)
			return (-1);	/* No adapter */

		chk1 -= 2;
	} while(chk1 != 0);	/* Repeat 128 times (all byte values) */

    	/* Restore the SIFADR value */
	outb(old, ioaddr + SIFADR);

	return (0);
}

int __init sk_isa_probe(struct net_device *dev)
{
        static int versionprinted;
	struct net_local *tp;
	int i,j;
	struct sk_isa_card *card;

#ifndef MODULE
	netdev_boot_setup_check(dev);
	tr_setup(dev);
#endif

	SET_MODULE_OWNER(dev);
	if (!dev->base_addr)
	{
		for(i = 0; portlist[i]; i++)
		{
			if (!request_region(portlist[i], SK_ISA_IO_EXTENT, isa_cardname))
				continue;

			if(sk_isa_probe1(portlist[i]))
			{
				release_region(dev->base_addr, SK_ISA_IO_EXTENT); 
				continue;
			}

			dev->base_addr = portlist[i];
			break;
		}
		if(!dev->base_addr)
			return -1;
	}
	else
	{
		if (!request_region(dev->base_addr, SK_ISA_IO_EXTENT, isa_cardname))
			return -1;

		if(sk_isa_probe1(dev->base_addr))
		{
			release_region(dev->base_addr, SK_ISA_IO_EXTENT); 
			return -1;
  		}
	} 

	/* At this point we have found a valid card. */

	if (versionprinted++ == 0)
		printk(KERN_DEBUG "%s", version);

	if (tmsdev_init(dev, ISA_MAX_ADDRESS, NULL))
		goto out4;

	dev->base_addr &= ~3; 
		
	sk_isa_read_eeprom(dev);

	printk(KERN_DEBUG "%s:    Ring Station Address: ", dev->name);
	printk("%2.2x", dev->dev_addr[0]);
	for (j = 1; j < 6; j++)
		printk(":%2.2x", dev->dev_addr[j]);
	printk("\n");
		
	tp = (struct net_local *)dev->priv;
	tp->setnselout = sk_isa_setnselout_pins;
		
	tp->sifreadb = sk_isa_sifreadb;
	tp->sifreadw = sk_isa_sifreadw;
	tp->sifwriteb = sk_isa_sifwriteb;
	tp->sifwritew = sk_isa_sifwritew;
	
	memcpy(tp->ProductID, isa_cardname, PROD_ID_SIZE + 1);

	tp->tmspriv = NULL;

	dev->open = sk_isa_open;
	dev->stop = sk_isa_close;

	if (dev->irq == 0)
	{
		for(j = 0; irqlist[j] != 0; j++)
		{
			dev->irq = irqlist[j];
			if (!request_irq(dev->irq, tms380tr_interrupt, 0, 
				isa_cardname, dev))
				break;
                }
		
                if(irqlist[j] == 0)
                {
                        printk(KERN_INFO "%s: AutoSelect no IRQ available\n", dev->name);
			goto out3;
		}
	}
	else
	{
		for(j = 0; irqlist[j] != 0; j++)
			if (irqlist[j] == dev->irq)
				break;
		if (irqlist[j] == 0)
		{
			printk(KERN_INFO "%s: Illegal IRQ %d specified\n",
				dev->name, dev->irq);
			goto out3;
		}
		if (request_irq(dev->irq, tms380tr_interrupt, 0, 
			isa_cardname, dev))
		{
                        printk(KERN_INFO "%s: Selected IRQ %d not available\n", 
				dev->name, dev->irq);
			goto out3;
		}
	}

	if (dev->dma == 0)
	{
		for(j = 0; dmalist[j] != 0; j++)
		{
			dev->dma = dmalist[j];
                        if (!request_dma(dev->dma, isa_cardname))
				break;
		}

		if(dmalist[j] == 0)
		{
			printk(KERN_INFO "%s: AutoSelect no DMA available\n", dev->name);
			goto out2;
		}
	}
	else
	{
		for(j = 0; dmalist[j] != 0; j++)
			if (dmalist[j] == dev->dma)
				break;
		if (dmalist[j] == 0)
		{
                        printk(KERN_INFO "%s: Illegal DMA %d specified\n", 
				dev->name, dev->dma);
			goto out2;
		}
		if (request_dma(dev->dma, isa_cardname))
		{
                        printk(KERN_INFO "%s: Selected DMA %d not available\n", 
				dev->name, dev->dma);
			goto out2;
		}
	}

	printk(KERN_DEBUG "%s:    IO: %#4lx  IRQ: %d  DMA: %d\n",
	       dev->name, dev->base_addr, dev->irq, dev->dma);
		
	/* Enlist in the card list */
	card = kmalloc(sizeof(struct sk_isa_card), GFP_KERNEL);
	if (!card)
		goto out;
	card->next = sk_isa_card_list;
	sk_isa_card_list = card;
	card->dev = dev;
	return 0;
out:
	free_dma(dev->dma);
out2:
	free_irq(dev->irq, dev);
out3:
	tmsdev_term(dev);
out4:
	release_region(dev->base_addr, SK_ISA_IO_EXTENT); 
	return -1;
}

/*
 * Reads MAC address from adapter RAM, which should've read it from
 * the onboard ROM.  
 *
 * Calling this on a board that does not support it can be a very
 * dangerous thing.  The Madge board, for instance, will lock your
 * machine hard when this is called.  Luckily, its supported in a
 * separate driver.  --ASF
 */
static void sk_isa_read_eeprom(struct net_device *dev)
{
	int i;
	
	/* Address: 0000:0000 */
	sk_isa_sifwritew(dev, 0, SIFADX);
	sk_isa_sifwritew(dev, 0, SIFADR);	
	
	/* Read six byte MAC address data */
	dev->addr_len = 6;
	for(i = 0; i < 6; i++)
		dev->dev_addr[i] = sk_isa_sifreadw(dev, SIFINC) >> 8;
}

unsigned short sk_isa_setnselout_pins(struct net_device *dev)
{
	return 0;
}

static int sk_isa_open(struct net_device *dev)
{  
	struct net_local *tp = (struct net_local *)dev->priv;
	unsigned short val = 0;
	unsigned short oldval;
	int i;

	val = 0;
	for(i = 0; irqlist[i] != 0; i++)
	{
		if(irqlist[i] == dev->irq)
			break;
	}

	val |= CYCLE_TIME << 2;
	val |= i << 4;
	i = dev->dma - 5;
	val |= i;
	if(tp->DataRate == SPEED_4)
		val |= LINE_SPEED_BIT;
	else
		val &= ~LINE_SPEED_BIT;
	oldval = sk_isa_sifreadb(dev, POSREG);
	/* Leave cycle bits alone */
	oldval |= 0xf3;
	val &= oldval;
	sk_isa_sifwriteb(dev, val, POSREG);

	tms380tr_open(dev);
	return 0;
}

static int sk_isa_close(struct net_device *dev)
{
	tms380tr_close(dev);
	return 0;
}

#ifdef MODULE

#define ISATR_MAX_ADAPTERS 3

static int io[ISATR_MAX_ADAPTERS];
static int irq[ISATR_MAX_ADAPTERS];
static int dma[ISATR_MAX_ADAPTERS];

MODULE_LICENSE("GPL");

MODULE_PARM(io, "1-" __MODULE_STRING(ISATR_MAX_ADAPTERS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(ISATR_MAX_ADAPTERS) "i");
MODULE_PARM(dma, "1-" __MODULE_STRING(ISATR_MAX_ADAPTERS) "i");

static int __init setup_card(unsigned long io, unsigned irq, unsigned char dma)
{
	int res = -ENOMEM;
	struct sk_isa_card *this_card;
	struct net_device *dev = alloc_trdev(0);

	if (dev) {
		dev->base_addr = io;
		dev->irq       = irq;
		dev->dma       = dma;
		res = -ENODEV;
		if (sk_isa_probe(dev) == 0) {
			res = register_netdev(dev);
			if (!res)
				return 0;
			release_region(dev->base_addr, SK_ISA_IO_EXTENT);
			free_irq(dev->irq, dev);
			free_dma(dev->dma);
			tmsdev_term(dev);
			this_card = sk_isa_card_list;
			sk_isa_card_list = this_card->next;
			kfree(this_card);
		}
		kfree(dev);
	}
	return res;
}

int init_module(void)
{
	int i, num;
	struct net_device *dev;

	num = 0;
	if (io[0]) { /* Only probe addresses from command line */
		for (i = 0; i < ISATR_MAX_ADAPTERS ; i++) {
			if (io[i] && setup_card(io[i], irq[i], dma[i]) == 0)
				num++;
		}
	} else {
		for(i = 0; num < ISATR_MAX_ADAPTERS && portlist[i]; i++) {
			if (setup_card(portlist[i], irq[i], dma[i]))
				num++;
		}
	}
	printk(KERN_NOTICE "skisa.c: %d cards found.\n", num);
	/* Probe for cards. */
	if (num == 0) {
		printk(KERN_NOTICE "skisa.c: No cards found.\n");
		return (-ENODEV);
	}
	return (0);
}

void cleanup_module(void)
{
	struct net_device *dev;
	struct sk_isa_card *this_card;

	while (sk_isa_card_list) {
		dev = sk_isa_card_list->dev;
		
		unregister_netdev(dev);
		release_region(dev->base_addr, SK_ISA_IO_EXTENT);
		free_irq(dev->irq, dev);
		free_dma(dev->dma);
		tmsdev_term(dev);
		kfree(dev);
		this_card = sk_isa_card_list;
		sk_isa_card_list = this_card->next;
		kfree(this_card);
	}
}
#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c skisa.c"
 *  alt-compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c skisa.c"
 *  c-set-style "K&R"
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
