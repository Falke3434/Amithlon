/*
 *  linux/drivers/acorn/scsi/powertec.c
 *
 *  Copyright (C) 1997-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver is based on experimentation.  Hence, it may have made
 * assumptions about the particular card that I have available, and
 * may not be reliable!
 *
 * Changelog:
 *  01-10-1997	RMK	Created, READONLY version.
 *  15-02-1998	RMK	Added DMA support and hardware definitions.
 *  15-04-1998	RMK	Only do PIO if FAS216 will allow it.
 *  02-05-1998	RMK	Moved DMA sg list into per-interface structure.
 *  27-06-1998	RMK	Changed asm/delay.h to linux/delay.h
 *  02-04-2000	RMK	Updated for new error handling code.
 */
#include <linux/module.h>
#include <linux/blk.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/unistd.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>

#include "../../scsi/sd.h"
#include "../../scsi/hosts.h"
#include "fas216.h"

#include <scsi/scsicam.h>

/* Configuration */
#define POWERTEC_XTALFREQ	40
#define POWERTEC_ASYNC_PERIOD	200
#define POWERTEC_SYNC_DEPTH	7

/*
 * List of devices that the driver will recognise
 */
#define POWERTECSCSI_LIST	{ MANU_ALSYSTEMS, PROD_ALSYS_SCSIATAPI }

#define POWERTEC_FAS216_OFFSET	0xc00
#define POWERTEC_FAS216_SHIFT	4

#define POWERTEC_INTR_STATUS	0x800
#define POWERTEC_INTR_BIT	0x80

#define POWERTEC_RESET_CONTROL	0x406
#define POWERTEC_RESET_BIT	1

#define POWERTEC_TERM_CONTROL	0x806
#define POWERTEC_TERM_ENABLE	1

#define POWERTEC_INTR_CONTROL	0x407
#define POWERTEC_INTR_ENABLE	1
#define POWERTEC_INTR_DISABLE	0

/*
 * Version
 */
#define VER_MAJOR	0
#define VER_MINOR	0
#define VER_PATCH	5

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("Powertec SCSI driver");
MODULE_PARM(term, "1-8i");
MODULE_PARM_DESC(term, "SCSI bus termination");
MODULE_LICENSE("GPL");

static struct expansion_card *ecs[MAX_ECARDS];

/*
 * Use term=0,1,0,0,0 to turn terminators on/off
 */
static int term[MAX_ECARDS] = { 1, 1, 1, 1, 1, 1, 1, 1 };

#define NR_SG	256

typedef struct {
	FAS216_Info info;

	struct {
		unsigned int term_port;
		unsigned int terms;
	} control;

	/* other info... */
	struct scatterlist sg[NR_SG];	/* Scatter DMA list	*/
} PowerTecScsi_Info;

/* Prototype: void powertecscsi_irqenable(ec, irqnr)
 * Purpose  : Enable interrupts on Powertec SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
powertecscsi_irqenable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_ENABLE, port);
}

/* Prototype: void powertecscsi_irqdisable(ec, irqnr)
 * Purpose  : Disable interrupts on Powertec SCSI card
 * Params   : ec    - expansion card structure
 *          : irqnr - interrupt number
 */
static void
powertecscsi_irqdisable(struct expansion_card *ec, int irqnr)
{
	unsigned int port = (unsigned int)ec->irq_data;
	outb(POWERTEC_INTR_DISABLE, port);
}

static const expansioncard_ops_t powertecscsi_ops = {
	powertecscsi_irqenable,
	powertecscsi_irqdisable,
	NULL,
	NULL,
	NULL,
	NULL
};

/* Prototype: void powertecscsi_terminator_ctl(host, on_off)
 * Purpose  : Turn the Powertec SCSI terminators on or off
 * Params   : host   - card to turn on/off
 *          : on_off - !0 to turn on, 0 to turn off
 */
static void
powertecscsi_terminator_ctl(struct Scsi_Host *host, int on_off)
{
	PowerTecScsi_Info *info = (PowerTecScsi_Info *)host->hostdata;

	if (on_off)
		info->control.terms = POWERTEC_TERM_ENABLE;
	else
		info->control.terms = 0;

	outb(info->control.terms, info->control.term_port);
}

/* Prototype: void powertecscsi_intr(irq, *dev_id, *regs)
 * Purpose  : handle interrupts from Powertec SCSI card
 * Params   : irq    - interrupt number
 *	      dev_id - user-defined (Scsi_Host structure)
 *	      regs   - processor registers at interrupt
 */
static void
powertecscsi_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct Scsi_Host *host = (struct Scsi_Host *)dev_id;

	fas216_intr(host);
}

/* Prototype: fasdmatype_t powertecscsi_dma_setup(host, SCpnt, direction, min_type)
 * Purpose  : initialises DMA/PIO
 * Params   : host      - host
 *	      SCpnt     - command
 *	      direction - DMA on to/off of card
 *	      min_type  - minimum DMA support that we must have for this transfer
 * Returns  : type of transfer to be performed
 */
static fasdmatype_t
powertecscsi_dma_setup(struct Scsi_Host *host, Scsi_Pointer *SCp,
		       fasdmadir_t direction, fasdmatype_t min_type)
{
	PowerTecScsi_Info *info = (PowerTecScsi_Info *)host->hostdata;
	int dmach = host->dma_channel;

	if (dmach != NO_DMA &&
	    (min_type == fasdma_real_all || SCp->this_residual >= 512)) {
		int bufs = SCp->buffers_residual;
		int pci_dir, dma_dir;

		if (bufs)
			memcpy(info->sg + 1, SCp->buffer + 1,
				sizeof(struct scatterlist) * bufs);
		info->sg[0].address = SCp->ptr;
		info->sg[0].length = SCp->this_residual;

		if (direction == DMA_OUT)
			pci_dir = PCI_DMA_TODEVICE,
			dma_dir = DMA_MODE_WRITE;
		else
			pci_dir = PCI_DMA_FROMDEVICE,
			dma_dir = DMA_MODE_READ;

		pci_map_sg(NULL, info->sg, bufs + 1, pci_dir);

		disable_dma(dmach);
		set_dma_sg(dmach, info->sg, bufs + 1);
		set_dma_mode(dmach, dma_dir);
		enable_dma(dmach);
		return fasdma_real_all;
	}

	/*
	 * If we're not doing DMA,
	 *  we'll do slow PIO
	 */
	return fasdma_pio;
}

/* Prototype: int powertecscsi_dma_stop(host, SCpnt)
 * Purpose  : stops DMA/PIO
 * Params   : host  - host
 *	      SCpnt - command
 */
static void
powertecscsi_dma_stop(struct Scsi_Host *host, Scsi_Pointer *SCp)
{
	if (host->dma_channel != NO_DMA)
		disable_dma(host->dma_channel);
}

/* Prototype: int powertecscsi_detect(Scsi_Host_Template * tpnt)
 * Purpose  : initialises PowerTec SCSI driver
 * Params   : tpnt - template for this SCSI adapter
 * Returns  : >0 if host found, 0 otherwise.
 */
int
powertecscsi_detect(Scsi_Host_Template *tpnt)
{
	static const card_ids powertecscsi_cids[] =
			{ POWERTECSCSI_LIST, { 0xffff, 0xffff} };
	int count = 0;
	struct Scsi_Host *host;
  
	tpnt->proc_name = "powertec";
	memset(ecs, 0, sizeof (ecs));

	ecard_startfind();

	while (1) {
	    	PowerTecScsi_Info *info;

		ecs[count] = ecard_find(0, powertecscsi_cids);
		if (!ecs[count])
			break;

		ecard_claim(ecs[count]);

		host = scsi_register(tpnt, sizeof (PowerTecScsi_Info));
		if (!host) {
			ecard_release(ecs[count]);
			break;
		}

		host->io_port = ecard_address(ecs[count], ECARD_IOC, ECARD_FAST);
		host->irq = ecs[count]->irq;
		host->dma_channel = ecs[count]->dma;
		info = (PowerTecScsi_Info *)host->hostdata;

		if (host->dma_channel != NO_DMA)
			set_dma_speed(host->dma_channel, 180);

		info->control.term_port = host->io_port + POWERTEC_TERM_CONTROL;
		info->control.terms = term[count] ? POWERTEC_TERM_ENABLE : 0;
		powertecscsi_terminator_ctl(host, info->control.terms);

		info->info.scsi.io_port	=
				host->io_port + POWERTEC_FAS216_OFFSET;
		info->info.scsi.io_shift= POWERTEC_FAS216_SHIFT;
		info->info.scsi.irq		= host->irq;
		info->info.ifcfg.clockrate	= POWERTEC_XTALFREQ;
		info->info.ifcfg.select_timeout	= 255;
		info->info.ifcfg.asyncperiod	= POWERTEC_ASYNC_PERIOD;
		info->info.ifcfg.sync_max_depth	= POWERTEC_SYNC_DEPTH;
		info->info.ifcfg.cntl3		= CNTL3_BS8 | CNTL3_FASTSCSI | CNTL3_FASTCLK;
		info->info.ifcfg.disconnect_ok	= 1;
		info->info.ifcfg.wide_max_size	= 0;
		info->info.dma.setup		= powertecscsi_dma_setup;
		info->info.dma.pseudo		= NULL;
		info->info.dma.stop		= powertecscsi_dma_stop;

		ecs[count]->irqaddr	= (unsigned char *)
			    ioaddr(host->io_port + POWERTEC_INTR_STATUS);
		ecs[count]->irqmask	= POWERTEC_INTR_BIT;
		ecs[count]->irq_data	= (void *)
			    (host->io_port + POWERTEC_INTR_CONTROL);
		ecs[count]->ops		= (expansioncard_ops_t *)&powertecscsi_ops;

		request_region(host->io_port + POWERTEC_FAS216_OFFSET,
			       16 << POWERTEC_FAS216_SHIFT, "powertec2-fas");

		if (host->irq != NO_IRQ &&
		    request_irq(host->irq, powertecscsi_intr,
				SA_INTERRUPT, "powertec", host)) {
			printk("scsi%d: IRQ%d not free, interrupts disabled\n",
			       host->host_no, host->irq);
			host->irq = NO_IRQ;
			info->info.scsi.irq = NO_IRQ;
		}

		if (host->dma_channel != NO_DMA &&
		    request_dma(host->dma_channel, "powertec")) {
			printk("scsi%d: DMA%d not free, DMA disabled\n",
			       host->host_no, host->dma_channel);
			host->dma_channel = NO_DMA;
		}

		fas216_init(host);
		++count;
	}
	return count;
}

/* Prototype: int powertecscsi_release(struct Scsi_Host * host)
 * Purpose  : releases all resources used by this adapter
 * Params   : host - driver host structure to return info for.
 */
int powertecscsi_release(struct Scsi_Host *host)
{
	int i;

	fas216_release(host);

	if (host->irq != NO_IRQ)
		free_irq(host->irq, host);
	if (host->dma_channel != NO_DMA)
		free_dma(host->dma_channel);
	release_region(host->io_port + POWERTEC_FAS216_OFFSET,
		       16 << POWERTEC_FAS216_SHIFT);

	for (i = 0; i < MAX_ECARDS; i++)
		if (ecs[i] &&
		    host->io_port == ecard_address(ecs[i], ECARD_IOC, ECARD_FAST))
			ecard_release(ecs[i]);
	return 0;
}

/* Prototype: const char *powertecscsi_info(struct Scsi_Host * host)
 * Purpose  : returns a descriptive string about this interface,
 * Params   : host - driver host structure to return info for.
 * Returns  : pointer to a static buffer containing null terminated string.
 */
const char *powertecscsi_info(struct Scsi_Host *host)
{
	PowerTecScsi_Info *info = (PowerTecScsi_Info *)host->hostdata;
	static char string[100], *p;

	p = string;
	p += sprintf(p, "%s ", host->hostt->name);
	p += fas216_info(&info->info, p);
	p += sprintf(p, "v%d.%d.%d terminators o%s",
		     VER_MAJOR, VER_MINOR, VER_PATCH,
		     info->control.terms ? "n" : "ff");

	return string;
}

/* Prototype: int powertecscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
 * Purpose  : Set a driver specific function
 * Params   : host   - host to setup
 *          : buffer - buffer containing string describing operation
 *          : length - length of string
 * Returns  : -EINVAL, or 0
 */
static int
powertecscsi_set_proc_info(struct Scsi_Host *host, char *buffer, int length)
{
	int ret = length;

	if (length >= 12 && strncmp(buffer, "POWERTECSCSI", 12) == 0) {
		buffer += 12;
		length -= 12;

		if (length >= 5 && strncmp(buffer, "term=", 5) == 0) {
			if (buffer[5] == '1')
				powertecscsi_terminator_ctl(host, 1);
			else if (buffer[5] == '0')
				powertecscsi_terminator_ctl(host, 0);
			else
				ret = -EINVAL;
		} else
			ret = -EINVAL;
	} else
		ret = -EINVAL;

	return ret;
}

/* Prototype: int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
 *					int length, int host_no, int inout)
 * Purpose  : Return information about the driver to a user process accessing
 *	      the /proc filesystem.
 * Params   : buffer  - a buffer to write information to
 *	      start   - a pointer into this buffer set by this routine to the start
 *		        of the required information.
 *	      offset  - offset into information that we have read upto.
 *	      length  - length of buffer
 *	      host_no - host number to return information for
 *	      inout   - 0 for reading, 1 for writing.
 * Returns  : length of data written to buffer.
 */
int powertecscsi_proc_info(char *buffer, char **start, off_t offset,
			    int length, int host_no, int inout)
{
	int pos, begin;
	struct Scsi_Host *host = scsi_hostlist;
	PowerTecScsi_Info *info;
	Scsi_Device *scd;

	while (host) {
		if (host->host_no == host_no)
			break;
		host = host->next;
	}
	if (!host)
		return 0;

	if (inout == 1)
		return powertecscsi_set_proc_info(host, buffer, length);

	info = (PowerTecScsi_Info *)host->hostdata;

	begin = 0;
	pos = sprintf(buffer,
			"PowerTec SCSI driver version %d.%d.%d\n",
			VER_MAJOR, VER_MINOR, VER_PATCH);

	pos += fas216_print_host(&info->info, buffer + pos);
	pos += sprintf(buffer + pos, "Term    : o%s\n",
			info->control.terms ? "n" : "ff");

	pos += fas216_print_stats(&info->info, buffer + pos);

	pos += sprintf(buffer+pos, "\nAttached devices:\n");

	for (scd = host->host_queue; scd; scd = scd->next) {
		pos += fas216_print_device(&info->info, scd, buffer + pos);

		if (pos + begin < offset) {
			begin += pos;
			pos = 0;
		}
		if (pos + begin > offset + length)
			break;
	}

	*start = buffer + (offset - begin);
	pos -= offset - begin;
	if (pos > length)
		pos = length;

	return pos;
}

static Scsi_Host_Template powertecscsi_template = {
	module:				THIS_MODULE,
	proc_info:			powertecscsi_proc_info,
	name:				"PowerTec SCSI",
	detect:				powertecscsi_detect,
	release:			powertecscsi_release,
	info:				powertecscsi_info,
	bios_param:			scsicam_bios_param,
	can_queue:			1,
	this_id:			7,
	sg_tablesize:			SG_ALL,
	cmd_per_lun:			1,
	use_clustering:			ENABLE_CLUSTERING,
	command:			fas216_command,
	queuecommand:			fas216_queue_command,
	eh_host_reset_handler:		fas216_eh_host_reset,
	eh_bus_reset_handler:		fas216_eh_bus_reset,
	eh_device_reset_handler:	fas216_eh_device_reset,
	eh_abort_handler:		fas216_eh_abort,
	use_new_eh_code:		1
};

static int __init powertecscsi_init(void)
{
	scsi_register_module(MODULE_SCSI_HA, &powertecscsi_template);
	if (powertecscsi_template.present)
		return 0;

	scsi_unregister_module(MODULE_SCSI_HA, &powertecscsi_template);
	return -ENODEV;
}

static void __exit powertecscsi_exit(void)
{
	scsi_unregister_module(MODULE_SCSI_HA, &powertecscsi_template);
}

module_init(powertecscsi_init);
module_exit(powertecscsi_exit);
