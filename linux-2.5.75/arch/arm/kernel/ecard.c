/*
 *  linux/arch/arm/kernel/ecard.c
 *
 *  Copyright 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Find all installed expansion cards, and handle interrupts from them.
 *
 *  Created from information from Acorns RiscOS3 PRMs
 *
 *  08-Dec-1996	RMK	Added code for the 9'th expansion card - the ether
 *			podule slot.
 *  06-May-1997	RMK	Added blacklist for cards whose loader doesn't work.
 *  12-Sep-1997	RMK	Created new handling of interrupt enables/disables
 *			- cards can now register their own routine to control
 *			interrupts (recommended).
 *  29-Sep-1997	RMK	Expansion card interrupt hardware not being re-enabled
 *			on reset from Linux. (Caused cards not to respond
 *			under RiscOS without hard reset).
 *  15-Feb-1998	RMK	Added DMA support
 *  12-Sep-1998	RMK	Added EASI support
 *  10-Jan-1999	RMK	Run loaders in a simulated RISC OS environment.
 *  17-Apr-1999	RMK	Support for EASI Type C cycles.
 */
#define ECARD_C

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/mach/irq.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_ARCH_RPC
#define HAVE_EXPMASK
#endif

enum req {
	req_readbytes,
	req_reset
};

struct ecard_request {
	enum req	req;
	ecard_t		*ec;
	unsigned int	address;
	unsigned int	length;
	unsigned int	use_loader;
	void		*buffer;
};

struct expcard_blacklist {
	unsigned short	 manufacturer;
	unsigned short	 product;
	const char	*type;
};

static ecard_t *cards;
static ecard_t *slot_to_expcard[MAX_ECARDS];
static unsigned int ectcr;
#ifdef HAS_EXPMASK
static unsigned int have_expmask;
#endif

/* List of descriptions of cards which don't have an extended
 * identification, or chunk directories containing a description.
 */
static struct expcard_blacklist __initdata blacklist[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1, "Acorn Ether1" }
};

asmlinkage extern int
ecard_loader_reset(volatile unsigned char *pa, loader_t loader);
asmlinkage extern int
ecard_loader_read(int off, volatile unsigned char *pa, loader_t loader);

static const struct ecard_id *
ecard_match_device(const struct ecard_id *ids, struct expansion_card *ec);

static inline unsigned short
ecard_getu16(unsigned char *v)
{
	return v[0] | v[1] << 8;
}

static inline signed long
ecard_gets24(unsigned char *v)
{
	return v[0] | v[1] << 8 | v[2] << 16 | ((v[2] & 0x80) ? 0xff000000 : 0);
}

static inline ecard_t *
slot_to_ecard(unsigned int slot)
{
	return slot < MAX_ECARDS ? slot_to_expcard[slot] : NULL;
}

/* ===================== Expansion card daemon ======================== */
/*
 * Since the loader programs on the expansion cards need to be run
 * in a specific environment, create a separate task with this
 * environment up, and pass requests to this task as and when we
 * need to.
 *
 * This should allow 99% of loaders to be called from Linux.
 *
 * From a security standpoint, we trust the card vendors.  This
 * may be a misplaced trust.
 */
#define BUS_ADDR(x) ((((unsigned long)(x)) << 2) + IO_BASE)
#define POD_INT_ADDR(x)	((volatile unsigned char *)\
			 ((BUS_ADDR((x)) - IO_BASE) + IO_START))

static inline void ecard_task_reset(struct ecard_request *req)
{
	struct expansion_card *ec = req->ec;
	if (ec->loader)
		ecard_loader_reset(POD_INT_ADDR(ec->podaddr), ec->loader);
}

static void
ecard_task_readbytes(struct ecard_request *req)
{
	unsigned char *buf = (unsigned char *)req->buffer;
	volatile unsigned char *base_addr =
		(volatile unsigned char *)POD_INT_ADDR(req->ec->podaddr);
	unsigned int len = req->length;
	unsigned int off = req->address;

	if (req->ec->slot_no == 8) {
		/*
		 * The card maintains an index which increments the address
		 * into a 4096-byte page on each access.  We need to keep
		 * track of the counter.
		 */
		static unsigned int index;
		unsigned int page;

		page = (off >> 12) * 4;
		if (page > 256 * 4)
			return;

		off &= 4095;

		/*
		 * If we are reading offset 0, or our current index is
		 * greater than the offset, reset the hardware index counter.
		 */
		if (off == 0 || index > off) {
			*base_addr = 0;
			index = 0;
		}

		/*
		 * Increment the hardware index counter until we get to the
		 * required offset.  The read bytes are discarded.
		 */
		while (index < off) {
			unsigned char byte;
			byte = base_addr[page];
			index += 1;
		}

		while (len--) {
			*buf++ = base_addr[page];
			index += 1;
		}
	} else {

		if (!req->use_loader || !req->ec->loader) {
			off *= 4;
			while (len--) {
				*buf++ = base_addr[off];
				off += 4;
			}
		} else {
			while(len--) {
				/*
				 * The following is required by some
				 * expansion card loader programs.
				 */
				*(unsigned long *)0x108 = 0;
				*buf++ = ecard_loader_read(off++, base_addr,
							   req->ec->loader);
			}
		}
	}

}

static void ecard_do_request(struct ecard_request *req)
{
	switch (req->req) {
	case req_readbytes:
		ecard_task_readbytes(req);
		break;

	case req_reset:
		ecard_task_reset(req);
		break;
	}
}

#include <linux/completion.h>

static pid_t ecard_pid;
static wait_queue_head_t ecard_wait;
static struct ecard_request *ecard_req;

static DECLARE_COMPLETION(ecard_completion);

/*
 * Set up the expansion card daemon's page tables.
 */
static void ecard_init_pgtables(struct mm_struct *mm)
{
	struct vm_area_struct vma;

	/* We want to set up the page tables for the following mapping:
	 *  Virtual	Physical
	 *  0x03000000	0x03000000
	 *  0x03010000	unmapped
	 *  0x03210000	0x03210000
	 *  0x03400000	unmapped
	 *  0x08000000	0x08000000
	 *  0x10000000	unmapped
	 *
	 * FIXME: we don't follow this 100% yet.
	 */
	pgd_t *src_pgd, *dst_pgd;

	src_pgd = pgd_offset(mm, IO_BASE);
	dst_pgd = pgd_offset(mm, IO_START);

	memcpy(dst_pgd, src_pgd, sizeof(pgd_t) * (IO_SIZE / PGDIR_SIZE));

	src_pgd = pgd_offset(mm, EASI_BASE);
	dst_pgd = pgd_offset(mm, EASI_START);

	memcpy(dst_pgd, src_pgd, sizeof(pgd_t) * (EASI_SIZE / PGDIR_SIZE));

	vma.vm_mm = mm;

	flush_tlb_range(&vma, IO_START, IO_START + IO_SIZE);
	flush_tlb_range(&vma, EASI_START, EASI_START + EASI_SIZE);
}

static int ecard_init_mm(void)
{
	struct mm_struct * mm = mm_alloc();
	struct mm_struct *active_mm = current->active_mm;

	if (!mm)
		return -ENOMEM;

	current->mm = mm;
	current->active_mm = mm;
	activate_mm(active_mm, mm);
	mmdrop(active_mm);
	ecard_init_pgtables(mm);
	return 0;
}

static int
ecard_task(void * unused)
{
	struct task_struct *tsk = current;

	daemonize("kecardd");

	/*
	 * Allocate a mm.  We're not a lazy-TLB kernel task since we need
	 * to set page table entries where the user space would be.  Note
	 * that this also creates the page tables.  Failure is not an
	 * option here.
	 */
	if (ecard_init_mm())
		panic("kecardd: unable to alloc mm\n");

	while (1) {
		struct ecard_request *req;

		do {
			req = xchg(&ecard_req, NULL);

			if (req == NULL) {
				sigemptyset(&tsk->pending.signal);
				interruptible_sleep_on(&ecard_wait);
			}
		} while (req == NULL);

		ecard_do_request(req);
		complete(&ecard_completion);
	}
}

/*
 * Wake the expansion card daemon to action our request.
 *
 * FIXME: The test here is not sufficient to detect if the
 * kcardd is running.
 */
static void
ecard_call(struct ecard_request *req)
{
	/*
	 * Make sure we have a context that is able to sleep.
	 */
	if (current == &init_task || in_interrupt())
		BUG();

	if (ecard_pid <= 0)
		ecard_pid = kernel_thread(ecard_task, NULL,
				CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

	ecard_req = req;
	wake_up(&ecard_wait);

	/*
	 * Now wait for kecardd to run.
	 */
	wait_for_completion(&ecard_completion);
}

/* ======================= Mid-level card control ===================== */

static void
ecard_readbytes(void *addr, ecard_t *ec, int off, int len, int useld)
{
	struct ecard_request req;

	req.req		= req_readbytes;
	req.ec		= ec;
	req.address	= off;
	req.length	= len;
	req.use_loader	= useld;
	req.buffer	= addr;

	ecard_call(&req);
}

int ecard_readchunk(struct in_chunk_dir *cd, ecard_t *ec, int id, int num)
{
	struct ex_chunk_dir excd;
	int index = 16;
	int useld = 0;

	if (!ec->cid.cd)
		return 0;

	while(1) {
		ecard_readbytes(&excd, ec, index, 8, useld);
		index += 8;
		if (c_id(&excd) == 0) {
			if (!useld && ec->loader) {
				useld = 1;
				index = 0;
				continue;
			}
			return 0;
		}
		if (c_id(&excd) == 0xf0) { /* link */
			index = c_start(&excd);
			continue;
		}
		if (c_id(&excd) == 0x80) { /* loader */
			if (!ec->loader) {
				ec->loader = (loader_t)kmalloc(c_len(&excd),
							       GFP_KERNEL);
				if (ec->loader)
					ecard_readbytes(ec->loader, ec,
							(int)c_start(&excd),
							c_len(&excd), useld);
				else
					return 0;
			}
			continue;
		}
		if (c_id(&excd) == id && num-- == 0)
			break;
	}

	if (c_id(&excd) & 0x80) {
		switch (c_id(&excd) & 0x70) {
		case 0x70:
			ecard_readbytes((unsigned char *)excd.d.string, ec,
					(int)c_start(&excd), c_len(&excd),
					useld);
			break;
		case 0x00:
			break;
		}
	}
	cd->start_offset = c_start(&excd);
	memcpy(cd->d.string, excd.d.string, 256);
	return 1;
}

/* ======================= Interrupt control ============================ */

static void ecard_def_irq_enable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask |= 1 << irqnr;
		__raw_writeb(have_expmask, EXPMASK_ENABLE);
	}
#endif
}

static void ecard_def_irq_disable(ecard_t *ec, int irqnr)
{
#ifdef HAS_EXPMASK
	if (irqnr < 4 && have_expmask) {
		have_expmask &= ~(1 << irqnr);
		__raw_writeb(have_expmask, EXPMASK_ENABLE);
	}
#endif
}

static int ecard_def_irq_pending(ecard_t *ec)
{
	return !ec->irqmask || ec->irqaddr[0] & ec->irqmask;
}

static void ecard_def_fiq_enable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_enable called - impossible");
}

static void ecard_def_fiq_disable(ecard_t *ec, int fiqnr)
{
	panic("ecard_def_fiq_disable called - impossible");
}

static int ecard_def_fiq_pending(ecard_t *ec)
{
	return !ec->fiqmask || ec->fiqaddr[0] & ec->fiqmask;
}

static expansioncard_ops_t ecard_default_ops = {
	ecard_def_irq_enable,
	ecard_def_irq_disable,
	ecard_def_irq_pending,
	ecard_def_fiq_enable,
	ecard_def_fiq_disable,
	ecard_def_fiq_pending
};

/*
 * Enable and disable interrupts from expansion cards.
 * (interrupts are disabled for these functions).
 *
 * They are not meant to be called directly, but via enable/disable_irq.
 */
static void ecard_irq_unmask(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->irqenable)
			ec->ops->irqenable(ec, irqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable IRQs for %d\n", irqnr);
	}
}

static void ecard_irq_mask(unsigned int irqnr)
{
	ecard_t *ec = slot_to_ecard(irqnr - 32);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops && ec->ops->irqdisable)
			ec->ops->irqdisable(ec, irqnr);
	}
}

static struct irqchip ecard_chip = {
	.ack	= ecard_irq_mask,
	.mask	= ecard_irq_mask,
	.unmask = ecard_irq_unmask,
};

void ecard_enablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->claimed && ec->ops->fiqenable)
			ec->ops->fiqenable(ec, fiqnr);
		else
			printk(KERN_ERR "ecard: rejecting request to "
				"enable FIQs for %d\n", fiqnr);
	}
}

void ecard_disablefiq(unsigned int fiqnr)
{
	ecard_t *ec = slot_to_ecard(fiqnr);

	if (ec) {
		if (!ec->ops)
			ec->ops = &ecard_default_ops;

		if (ec->ops->fiqdisable)
			ec->ops->fiqdisable(ec, fiqnr);
	}
}

static void
ecard_dump_irq_state(ecard_t *ec)
{
	printk("  %d: %sclaimed, ",
	       ec->slot_no,
	       ec->claimed ? "" : "not ");

	if (ec->ops && ec->ops->irqpending &&
	    ec->ops != &ecard_default_ops)
		printk("irq %spending\n",
		       ec->ops->irqpending(ec) ? "" : "not ");
	else
		printk("irqaddr %p, mask = %02X, status = %02X\n",
		       ec->irqaddr, ec->irqmask, *ec->irqaddr);
}

static void ecard_check_lockup(struct irqdesc *desc)
{
	static unsigned long last;
	static int lockup;
	ecard_t *ec;

	/*
	 * If the timer interrupt has not run since the last million
	 * unrecognised expansion card interrupts, then there is
	 * something seriously wrong.  Disable the expansion card
	 * interrupts so at least we can continue.
	 *
	 * Maybe we ought to start a timer to re-enable them some time
	 * later?
	 */
	if (last == jiffies) {
		lockup += 1;
		if (lockup > 1000000) {
			printk(KERN_ERR "\nInterrupt lockup detected - "
			       "disabling all expansion card interrupts\n");

			desc->chip->mask(IRQ_EXPANSIONCARD);

			printk("Expansion card IRQ state:\n");

			for (ec = cards; ec; ec = ec->next)
				ecard_dump_irq_state(ec);
		}
	} else
		lockup = 0;

	/*
	 * If we did not recognise the source of this interrupt,
	 * warn the user, but don't flood the user with these messages.
	 */
	if (!last || time_after(jiffies, last + 5*HZ)) {
		last = jiffies;
		printk(KERN_WARNING "Unrecognised interrupt from backplane\n");
	}
}

static void
ecard_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	ecard_t *ec;
	int called = 0;

	desc->chip->mask(irq);
	for (ec = cards; ec; ec = ec->next) {
		int pending;

		if (!ec->claimed || ec->irq == NO_IRQ || ec->slot_no == 8)
			continue;

		if (ec->ops && ec->ops->irqpending)
			pending = ec->ops->irqpending(ec);
		else
			pending = ecard_default_ops.irqpending(ec);

		if (pending) {
			struct irqdesc *d = irq_desc + ec->irq;
			d->handle(ec->irq, d, regs);
			called ++;
		}
	}
	desc->chip->unmask(irq);

	if (called == 0)
		ecard_check_lockup(desc);
}

#ifdef HAS_EXPMASK
static unsigned char priority_masks[] =
{
	0xf0, 0xf1, 0xf3, 0xf7, 0xff, 0xff, 0xff, 0xff
};

static unsigned char first_set[] =
{
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00,
	0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00
};

static void
ecard_irqexp_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	const unsigned int statusmask = 15;
	unsigned int status;

	status = __raw_readb(EXPMASK_STATUS) & statusmask;
	if (status) {
		unsigned int slot = first_set[status];
		ecard_t *ec = slot_to_ecard(slot);

		if (ec->claimed) {
			struct irqdesc *d = irqdesc + ec->irq;
			/*
			 * this ugly code is so that we can operate a
			 * prioritorising system:
			 *
			 * Card 0 	highest priority
			 * Card 1
			 * Card 2
			 * Card 3	lowest priority
			 *
			 * Serial cards should go in 0/1, ethernet/scsi in 2/3
			 * otherwise you will lose serial data at high speeds!
			 */
			d->handle(ec->irq, d, regs);
		} else {
			printk(KERN_WARNING "card%d: interrupt from unclaimed "
			       "card???\n", slot);
			have_expmask &= ~(1 << slot);
			__raw_writeb(have_expmask, EXPMASK_ENABLE);
		}
	} else
		printk(KERN_WARNING "Wild interrupt from backplane (masks)\n");
}

static int __init ecard_probeirqhw(void)
{
	ecard_t *ec;
	int found;

	__raw_writeb(0x00, EXPMASK_ENABLE);
	__raw_writeb(0xff, EXPMASK_STATUS);
	found = (__raw_readb(EXPMASK_STATUS) & 15) == 0;
	__raw_writeb(0xff, EXPMASK_ENABLE);

	if (found) {
		printk(KERN_DEBUG "Expansion card interrupt "
		       "management hardware found\n");

		/* for each card present, set a bit to '1' */
		have_expmask = 0x80000000;

		for (ec = cards; ec; ec = ec->next)
			have_expmask |= 1 << ec->slot_no;

		__raw_writeb(have_expmask, EXPMASK_ENABLE);
	}

	return found;
}
#else
#define ecard_irqexp_handler NULL
#define ecard_probeirqhw() (0)
#endif

#ifndef IO_EC_MEMC8_BASE
#define IO_EC_MEMC8_BASE 0
#endif

unsigned int ecard_address(ecard_t *ec, card_type_t type, card_speed_t speed)
{
	unsigned long address = 0;
	int slot = ec->slot_no;

	if (ec->slot_no == 8)
		return IO_EC_MEMC8_BASE;

	ectcr &= ~(1 << slot);

	switch (type) {
	case ECARD_MEMC:
		if (slot < 4)
			address = IO_EC_MEMC_BASE + (slot << 12);
		break;

	case ECARD_IOC:
		if (slot < 4)
			address = IO_EC_IOC_BASE + (slot << 12);
#ifdef IO_EC_IOC4_BASE
		else
			address = IO_EC_IOC4_BASE + ((slot - 4) << 12);
#endif
		if (address)
			address +=  speed << 17;
		break;

#ifdef IO_EC_EASI_BASE
	case ECARD_EASI:
		address = IO_EC_EASI_BASE + (slot << 22);
		if (speed == ECARD_FAST)
			ectcr |= 1 << slot;
		break;
#endif
	default:
		break;
	}

#ifdef IOMD_ECTCR
	iomd_writeb(ectcr, IOMD_ECTCR);
#endif
	return address;
}

static int ecard_prints(char *buffer, ecard_t *ec)
{
	char *start = buffer;

	buffer += sprintf(buffer, "  %d: %s ", ec->slot_no,
			  ec->type == ECARD_EASI ? "EASI" : "    ");

	if (ec->cid.id == 0) {
		struct in_chunk_dir incd;

		buffer += sprintf(buffer, "[%04X:%04X] ",
			ec->cid.manufacturer, ec->cid.product);

		if (!ec->card_desc && ec->cid.cd &&
		    ecard_readchunk(&incd, ec, 0xf5, 0)) {
			ec->card_desc = kmalloc(strlen(incd.d.string)+1, GFP_KERNEL);

			if (ec->card_desc)
				strcpy((char *)ec->card_desc, incd.d.string);
		}

		buffer += sprintf(buffer, "%s\n", ec->card_desc ? ec->card_desc : "*unknown*");
	} else
		buffer += sprintf(buffer, "Simple card %d\n", ec->cid.id);

	return buffer - start;
}

static int get_ecard_dev_info(char *buf, char **start, off_t pos, int count)
{
	ecard_t *ec = cards;
	off_t at = 0;
	int len, cnt;

	cnt = 0;
	while (ec && count > cnt) {
		len = ecard_prints(buf, ec);
		at += len;
		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else
				cnt += len;
			buf += len;
		}
		ec = ec->next;
	}
	return (count > cnt) ? cnt : count;
}

static struct proc_dir_entry *proc_bus_ecard_dir = NULL;

static void ecard_proc_init(void)
{
	proc_bus_ecard_dir = proc_mkdir("ecard", proc_bus);
	create_proc_info_entry("devices", 0, proc_bus_ecard_dir,
		get_ecard_dev_info);
}

#define ec_set_resource(ec,nr,st,sz,flg)			\
	do {							\
		(ec)->resource[nr].name = ec->dev.name;		\
		(ec)->resource[nr].start = st;			\
		(ec)->resource[nr].end = (st) + (sz) - 1;	\
		(ec)->resource[nr].flags = flg;			\
	} while (0)

static void __init ecard_init_resources(struct expansion_card *ec)
{
	unsigned long base = PODSLOT_IOC4_BASE;
	unsigned int slot = ec->slot_no;
	int i;

	if (slot < 4) {
		ec_set_resource(ec, ECARD_RES_MEMC,
				PODSLOT_MEMC_BASE + (slot << 14),
				PODSLOT_MEMC_SIZE, IORESOURCE_MEM);
		base = PODSLOT_IOC0_BASE;
	}

#ifdef CONFIG_ARCH_RPC
	if (slot < 8) {
		ec_set_resource(ec, ECARD_RES_EASI,
				PODSLOT_EASI_BASE + (slot << 24),
				PODSLOT_EASI_SIZE, IORESOURCE_MEM);
	}

	if (slot == 8) {
		ec_set_resource(ec, ECARD_RES_MEMC, NETSLOT_BASE,
				NETSLOT_SIZE, IORESOURCE_MEM);
	} else
#endif

	for (i = 0; i < ECARD_RES_IOCSYNC - ECARD_RES_IOCSLOW; i++) {
		ec_set_resource(ec, i + ECARD_RES_IOCSLOW,
				base + (slot << 14) + (i << 19),
				PODSLOT_IOC_SIZE, IORESOURCE_MEM);
	}

	for (i = 0; i < ECARD_NUM_RESOURCES; i++) {
		if (ec->resource[i].start &&
		    request_resource(&iomem_resource, &ec->resource[i])) {
			printk(KERN_ERR "%s: resource(s) not available\n",
				ec->dev.bus_id);
			ec->resource[i].end -= ec->resource[i].start;
			ec->resource[i].start = 0;
		}
	}
}

static ssize_t ecard_show_irq(struct device *dev, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->irq);
}

static DEVICE_ATTR(irq, S_IRUGO, ecard_show_irq, NULL);

static ssize_t ecard_show_dma(struct device *dev, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	return sprintf(buf, "%u\n", ec->dma);
}

static DEVICE_ATTR(dma, S_IRUGO, ecard_show_dma, NULL);

static ssize_t ecard_show_resources(struct device *dev, char *buf)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	char *str = buf;
	int i;

	for (i = 0; i < ECARD_NUM_RESOURCES; i++)
		str += sprintf(str, "%08lx %08lx %08lx\n",
				ec->resource[i].start,
				ec->resource[i].end,
				ec->resource[i].flags);

	return str - buf;
}

static DEVICE_ATTR(resource, S_IRUGO, ecard_show_resources, NULL);

/*
 * Probe for an expansion card.
 *
 * If bit 1 of the first byte of the card is set, then the
 * card does not exist.
 */
static int __init
ecard_probe(int slot, card_type_t type)
{
	ecard_t **ecp;
	ecard_t *ec;
	struct ex_ecid cid;
	int i, rc = -ENOMEM;

	ec = kmalloc(sizeof(ecard_t), GFP_KERNEL);
	if (!ec)
		goto nomem;

	memset(ec, 0, sizeof(ecard_t));

	ec->slot_no	= slot;
	ec->type	= type;
	ec->irq		= NO_IRQ;
	ec->fiq		= NO_IRQ;
	ec->dma		= NO_DMA;
	ec->card_desc	= NULL;
	ec->ops		= &ecard_default_ops;

	rc = -ENODEV;
	if ((ec->podaddr = ecard_address(ec, type, ECARD_SYNC)) == 0)
		goto nodev;

	cid.r_zero = 1;
	ecard_readbytes(&cid, ec, 0, 16, 0);
	if (cid.r_zero)
		goto nodev;

	ec->cid.id	= cid.r_id;
	ec->cid.cd	= cid.r_cd;
	ec->cid.is	= cid.r_is;
	ec->cid.w	= cid.r_w;
	ec->cid.manufacturer = ecard_getu16(cid.r_manu);
	ec->cid.product = ecard_getu16(cid.r_prod);
	ec->cid.country = cid.r_country;
	ec->cid.irqmask = cid.r_irqmask;
	ec->cid.irqoff  = ecard_gets24(cid.r_irqoff);
	ec->cid.fiqmask = cid.r_fiqmask;
	ec->cid.fiqoff  = ecard_gets24(cid.r_fiqoff);
	ec->fiqaddr	=
	ec->irqaddr	= (unsigned char *)ioaddr(ec->podaddr);

	if (ec->cid.is) {
		ec->irqmask = ec->cid.irqmask;
		ec->irqaddr += ec->cid.irqoff;
		ec->fiqmask = ec->cid.fiqmask;
		ec->fiqaddr += ec->cid.fiqoff;
	} else {
		ec->irqmask = 1;
		ec->fiqmask = 4;
	}

	for (i = 0; i < sizeof(blacklist) / sizeof(*blacklist); i++)
		if (blacklist[i].manufacturer == ec->cid.manufacturer &&
		    blacklist[i].product == ec->cid.product) {
			ec->card_desc = blacklist[i].type;
			break;
		}

	snprintf(ec->dev.bus_id, sizeof(ec->dev.bus_id), "ecard%d", slot);
	snprintf(ec->dev.name, sizeof(ec->dev.name), "ecard %04x:%04x",
		 ec->cid.manufacturer, ec->cid.product);
	ec->dev.parent = NULL;
	ec->dev.bus    = &ecard_bus_type;
	ec->dev.dma_mask = &ec->dma_mask;
	ec->dma_mask = (u64)0xffffffff;

	ecard_init_resources(ec);

	/*
	 * hook the interrupt handlers
	 */
	if (slot < 8) {
		ec->irq = 32 + slot;
		set_irq_chip(ec->irq, &ecard_chip);
		set_irq_handler(ec->irq, do_level_IRQ);
		set_irq_flags(ec->irq, IRQF_VALID);
	}

#ifdef IO_EC_MEMC8_BASE
	if (slot == 8)
		ec->irq = 11;
#endif
#ifdef CONFIG_ARCH_RPC
	/* On RiscPC, only first two slots have DMA capability */
	if (slot < 2)
		ec->dma = 2 + slot;
#endif

	for (ecp = &cards; *ecp; ecp = &(*ecp)->next);

	*ecp = ec;
	slot_to_expcard[slot] = ec;

	device_register(&ec->dev);
	device_create_file(&ec->dev, &dev_attr_dma);
	device_create_file(&ec->dev, &dev_attr_irq);
	device_create_file(&ec->dev, &dev_attr_resource);

	return 0;

nodev:
	kfree(ec);
nomem:
	return rc;
}

/*
 * Initialise the expansion card system.
 * Locate all hardware - interrupt management and
 * actual cards.
 */
static int __init ecard_init(void)
{
	int slot, irqhw;

	init_waitqueue_head(&ecard_wait);

	printk("Probing expansion cards\n");

	for (slot = 0; slot < 8; slot ++) {
		if (ecard_probe(slot, ECARD_EASI) == -ENODEV)
			ecard_probe(slot, ECARD_IOC);
	}

#ifdef IO_EC_MEMC8_BASE
	ecard_probe(8, ECARD_IOC);
#endif

	irqhw = ecard_probeirqhw();

	set_irq_chained_handler(IRQ_EXPANSIONCARD,
				irqhw ? ecard_irqexp_handler : ecard_irq_handler);

	ecard_proc_init();

	return 0;
}

subsys_initcall(ecard_init);

/*
 *	ECARD "bus"
 */
static const struct ecard_id *
ecard_match_device(const struct ecard_id *ids, struct expansion_card *ec)
{
	int i;

	for (i = 0; ids[i].manufacturer != 65535; i++)
		if (ec->cid.manufacturer == ids[i].manufacturer &&
		    ec->cid.product == ids[i].product)
			return ids + i;

	return NULL;
}

static int ecard_drv_probe(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);
	const struct ecard_id *id;
	int ret;

	id = ecard_match_device(drv->id_table, ec);

	ecard_claim(ec);
	ret = drv->probe(ec, id);
	if (ret)
		ecard_release(ec);
	return ret;
}

static int ecard_drv_remove(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);

	drv->remove(ec);
	ecard_release(ec);

	return 0;
}

/*
 * Before rebooting, we must make sure that the expansion card is in a
 * sensible state, so it can be re-detected.  This means that the first
 * page of the ROM must be visible.  We call the expansion cards reset
 * handler, if any.
 */
static void ecard_drv_shutdown(struct device *dev)
{
	struct expansion_card *ec = ECARD_DEV(dev);
	struct ecard_driver *drv = ECARD_DRV(dev->driver);
	struct ecard_request req;

	if (drv->shutdown)
		drv->shutdown(ec);
	ecard_release(ec);
	req.req = req_reset;
	req.ec = ec;
	ecard_call(&req);
}

int ecard_register_driver(struct ecard_driver *drv)
{
	drv->drv.bus = &ecard_bus_type;
	drv->drv.probe = ecard_drv_probe;
	drv->drv.remove = ecard_drv_remove;
	drv->drv.shutdown = ecard_drv_shutdown;

	return driver_register(&drv->drv);
}

void ecard_remove_driver(struct ecard_driver *drv)
{
	driver_unregister(&drv->drv);
}

static int ecard_match(struct device *_dev, struct device_driver *_drv)
{
	struct expansion_card *ec = ECARD_DEV(_dev);
	struct ecard_driver *drv = ECARD_DRV(_drv);
	int ret;

	if (drv->id_table) {
		ret = ecard_match_device(drv->id_table, ec) != NULL;
	} else {
		ret = ec->cid.id == drv->id;
	}

	return ret;
}

struct bus_type ecard_bus_type = {
	.name	= "ecard",
	.match	= ecard_match,
};

static int ecard_bus_init(void)
{
	return bus_register(&ecard_bus_type);
}

postcore_initcall(ecard_bus_init);

EXPORT_SYMBOL(ecard_readchunk);
EXPORT_SYMBOL(ecard_address);
EXPORT_SYMBOL(ecard_register_driver);
EXPORT_SYMBOL(ecard_remove_driver);
EXPORT_SYMBOL(ecard_bus_type);
