/* 
 * arch/ppc64/kernel/xics.c
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/naca.h>
#include <asm/rtas.h>
#include <asm/xics.h>
#include <asm/ppcdebug.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>

#include "i8259.h"

void xics_enable_irq(u_int irq);
void xics_disable_irq(u_int irq);
void xics_mask_and_ack_irq(u_int irq);
void xics_end_irq(u_int irq);
void xics_set_affinity(unsigned int irq_nr, unsigned long cpumask);

struct hw_interrupt_type xics_pic = {
	" XICS     ",
	NULL,
	NULL,
	xics_enable_irq,
	xics_disable_irq,
	xics_mask_and_ack_irq,
	xics_end_irq,
	xics_set_affinity
};

struct hw_interrupt_type xics_8259_pic = {
	" XICS/8259",
	NULL,
	NULL,
	NULL,
	NULL,
	xics_mask_and_ack_irq,
	NULL
};

#define XICS_IPI		2
#define XICS_IRQ_OFFSET		0x10
#define XICS_IRQ_SPURIOUS	0

/* Want a priority other than 0.  Various HW issues require this. */
#define	DEFAULT_PRIORITY	5

/* 
 * Mark IPIs as higher priority so we can take them inside interrupts that
 * arent marked SA_INTERRUPT
 */
#define IPI_PRIORITY		4

struct xics_ipl {
	union {
		u32 word;
		u8 bytes[4];
	} xirr_poll;
	union {
		u32 word;
		u8 bytes[4];
	} xirr;
	u32 dummy;
	union {
		u32 word;
		u8 bytes[4];
	} qirr;
};

static struct xics_ipl *xics_per_cpu[NR_CPUS];

static int xics_irq_8259_cascade = 0;
static int xics_irq_8259_cascade_real = 0;
static unsigned int default_server = 0xFF;
static unsigned int default_distrib_server = 0;

/*
 * XICS only has a single IPI, so encode the messages per CPU
 */
struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

/* RTAS service tokens */
int ibm_get_xive;
int ibm_set_xive;
int ibm_int_on;
int ibm_int_off;

typedef struct {
	int (*xirr_info_get)(int cpu);
	void (*xirr_info_set)(int cpu, int val);
	void (*cppr_info)(int cpu, u8 val);
	void (*qirr_info)(int cpu, u8 val);
} xics_ops;


/* SMP */

static int pSeries_xirr_info_get(int n_cpu)
{
	return xics_per_cpu[n_cpu]->xirr.word;
}

static void pSeries_xirr_info_set(int n_cpu, int value)
{
	xics_per_cpu[n_cpu]->xirr.word = value;
}

static void pSeries_cppr_info(int n_cpu, u8 value)
{
	xics_per_cpu[n_cpu]->xirr.bytes[0] = value;
}

static void pSeries_qirr_info(int n_cpu, u8 value)
{
	xics_per_cpu[n_cpu]->qirr.bytes[0] = value;
}

static xics_ops pSeries_ops = {
	pSeries_xirr_info_get,
	pSeries_xirr_info_set,
	pSeries_cppr_info,
	pSeries_qirr_info
};

static xics_ops *ops = &pSeries_ops;


/* LPAR */

static inline long plpar_eoi(unsigned long xirr)
{
	return plpar_hcall_norets(H_EOI, xirr);
}

static inline long plpar_cppr(unsigned long cppr)
{
	return plpar_hcall_norets(H_CPPR, cppr);
}

static inline long plpar_ipi(unsigned long servernum, unsigned long mfrr)
{
	return plpar_hcall_norets(H_IPI, servernum, mfrr);
}

static inline long plpar_xirr(unsigned long *xirr_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_XIRR, 0, 0, 0, 0, xirr_ret, &dummy, &dummy);
}

static int pSeriesLP_xirr_info_get(int n_cpu)
{
	unsigned long lpar_rc;
	unsigned long return_value; 

	lpar_rc = plpar_xirr(&return_value);
	if (lpar_rc != H_Success)
		panic(" bad return code xirr - rc = %lx \n", lpar_rc); 
	return (int)return_value;
}

static void pSeriesLP_xirr_info_set(int n_cpu, int value)
{
	unsigned long lpar_rc;
	unsigned long val64 = value & 0xffffffff;

	lpar_rc = plpar_eoi(val64);
	if (lpar_rc != H_Success)
		panic("bad return code EOI - rc = %ld, value=%lx\n", lpar_rc,
		      val64); 
}

static void pSeriesLP_cppr_info(int n_cpu, u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_cppr(value);
	if (lpar_rc != H_Success)
		panic("bad return code cppr - rc = %lx\n", lpar_rc); 
}

static void pSeriesLP_qirr_info(int n_cpu , u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_ipi(n_cpu, value);
	if (lpar_rc != H_Success)
		panic("bad return code qirr - rc = %lx\n", lpar_rc); 
}

xics_ops pSeriesLP_ops = {
	pSeriesLP_xirr_info_get,
	pSeriesLP_xirr_info_set,
	pSeriesLP_cppr_info,
	pSeriesLP_qirr_info
};

void xics_enable_irq(u_int virq)
{
	u_int irq;
	long call_status;
	unsigned int server;

	virq -= XICS_IRQ_OFFSET;
	irq = virt_irq_to_real(virq);
	if (irq == XICS_IPI)
		return;

#ifdef CONFIG_IRQ_ALL_CPUS
	if (smp_threads_ready)
		server = default_distrib_server;
	else
		server = default_server;
#else
	server = default_server;
#endif

	call_status = rtas_call(ibm_set_xive, 3, 1, NULL, irq, server,
				DEFAULT_PRIORITY);
	if (call_status != 0) {
		printk("xics_enable_irq: irq=%x: ibm_set_xive returned %lx\n",
		       irq, call_status);
		return;
	}

	/* Now unmask the interrupt (often a no-op) */
	call_status = rtas_call(ibm_int_on, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk("xics_enable_irq: irq=%x: ibm_int_on returned %lx\n",
		       irq, call_status);
		return;
	}
}

void xics_disable_irq(u_int virq)
{
	u_int irq;
	long call_status;

	virq -= XICS_IRQ_OFFSET;
	irq = virt_irq_to_real(virq);
	if (irq == XICS_IPI)
		return;

	call_status = rtas_call(ibm_int_off, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk("xics_disable_irq: irq=%x: ibm_int_off returned %lx\n",
		       irq, call_status);
		return;
	}
}

void xics_end_irq(u_int	irq)
{
	int cpu = smp_processor_id();

	iosync();
	ops->xirr_info_set(cpu, ((0xff<<24) |
				 (virt_irq_to_real(irq-XICS_IRQ_OFFSET))));
}

void xics_mask_and_ack_irq(u_int irq)
{
	int cpu = smp_processor_id();

	if (irq < XICS_IRQ_OFFSET) {
		i8259_pic.ack(irq);
		iosync();
		ops->xirr_info_set(cpu, ((0xff<<24) |
					 xics_irq_8259_cascade_real));
		iosync();
	}
}

int xics_get_irq(struct pt_regs *regs)
{
	u_int cpu = smp_processor_id();
	u_int vec;
	int irq;

	vec = ops->xirr_info_get(cpu);
	/*  (vec >> 24) == old priority */
	vec &= 0x00ffffff;

	/* for sanity, this had better be < NR_IRQS - 16 */
	if (vec == xics_irq_8259_cascade_real) {
		irq = i8259_irq(cpu);
		if (irq == -1) {
			/* Spurious cascaded interrupt.  Still must ack xics */
                        xics_end_irq(XICS_IRQ_OFFSET + xics_irq_8259_cascade);
			irq = -1;
		}
	} else if (vec == XICS_IRQ_SPURIOUS) {
		irq = -1;
	} else {
		irq = real_irq_to_virt(vec) + XICS_IRQ_OFFSET;
	}
	return irq;
}

#ifdef CONFIG_SMP

extern struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

irqreturn_t xics_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	ops->qirr_info(cpu, 0xff);
	while (xics_ipi_message[cpu].value) {
		if (test_and_clear_bit(PPC_MSG_CALL_FUNCTION,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_CALL_FUNCTION, regs);
		}
		if (test_and_clear_bit(PPC_MSG_RESCHEDULE,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_RESCHEDULE, regs);
		}
#if 0
		if (test_and_clear_bit(PPC_MSG_MIGRATE_TASK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_MIGRATE_TASK, regs);
		}
#endif
#ifdef CONFIG_XMON
		if (test_and_clear_bit(PPC_MSG_XMON_BREAK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_XMON_BREAK, regs);
		}
#endif
	}
	return IRQ_HANDLED;
}

void xics_cause_IPI(int cpu)
{
	ops->qirr_info(cpu, IPI_PRIORITY);
}

void xics_setup_cpu(void)
{
	int cpu = smp_processor_id();

	ops->cppr_info(cpu, 0xff);
	iosync();
}

#endif /* CONFIG_SMP */

void xics_init_IRQ(void)
{
	int i;
	unsigned long intr_size = 0;
	struct device_node *np;
	uint *ireg, ilen, indx=0;
	unsigned long intr_base = 0;
	struct xics_interrupt_node {
		unsigned long long addr;
		unsigned long long size;
	} inodes[NR_CPUS*2]; 

	ppc64_boot_msg(0x20, "XICS Init");

	ibm_get_xive = rtas_token("ibm,get-xive");
	ibm_set_xive = rtas_token("ibm,set-xive");
	ibm_int_on  = rtas_token("ibm,int-on");
	ibm_int_off = rtas_token("ibm,int-off");

	np = find_type_devices("PowerPC-External-Interrupt-Presentation");
	if (!np) {
		printk(KERN_WARNING "Can't find Interrupt Presentation\n");
		udbg_printf("Can't find Interrupt Presentation\n");
		while (1);
	}
nextnode:
	ireg = (uint *)get_property(np, "ibm,interrupt-server-ranges", 0);
	if (ireg) {
		/*
		 * set node starting index for this node
		 */
		indx = *ireg;
	}

	ireg = (uint *)get_property(np, "reg", &ilen);
	if (!ireg) {
		printk(KERN_WARNING "Can't find Interrupt Reg Property\n");
		udbg_printf("Can't find Interrupt Reg Property\n");
		while (1);
	}
	
	while (ilen) {
		inodes[indx].addr = (unsigned long long)*ireg++ << 32;
		ilen -= sizeof(uint);
		inodes[indx].addr |= *ireg++;
		ilen -= sizeof(uint);
		inodes[indx].size = (unsigned long long)*ireg++ << 32;
		ilen -= sizeof(uint);
		inodes[indx].size |= *ireg++;
		ilen -= sizeof(uint);
		indx++;
		if (indx >= NR_CPUS) break;
	}

	np = np->next;
	if ((indx < NR_CPUS) && np) goto nextnode;

	/* Find the server numbers for the boot cpu. */
	for (np = find_type_devices("cpu"); np; np = np->next) {
		ireg = (uint *)get_property(np, "reg", &ilen);
		if (ireg && ireg[0] == smp_processor_id()) {
			ireg = (uint *)get_property(np, "ibm,ppc-interrupt-gserver#s", &ilen);
			i = ilen / sizeof(int);
			if (ireg && i > 0) {
				default_server = ireg[0];
				default_distrib_server = ireg[i-1]; /* take last element */
			}
			break;
		}
	}

	intr_base = inodes[0].addr;
	intr_size = (ulong)inodes[0].size;

	np = find_type_devices("interrupt-controller");
	if (!np) {
		printk(KERN_WARNING "xics:  no ISA Interrupt Controller\n");
		xics_irq_8259_cascade_real = -1;
		xics_irq_8259_cascade = -1;
	} else {
		ireg = (uint *) get_property(np, "interrupts", 0);
		if (!ireg) {
			printk(KERN_WARNING "Can't find ISA Interrupts Property\n");
			udbg_printf("Can't find ISA Interrupts Property\n");
			while (1);
		}
		xics_irq_8259_cascade_real = *ireg;
		xics_irq_8259_cascade = virt_irq_create_mapping(xics_irq_8259_cascade_real);
	}

	if (systemcfg->platform == PLATFORM_PSERIES) {
#ifdef CONFIG_SMP
		for (i = 0; i < NR_CPUS; ++i) {
			if (!cpu_possible(i))
				continue;
			xics_per_cpu[i] = __ioremap((ulong)inodes[i].addr, 
						    (ulong)inodes[i].size,
						    _PAGE_NO_CACHE);
		}
#else
		xics_per_cpu[0] = __ioremap((ulong)intr_base, intr_size,
					    _PAGE_NO_CACHE);
#endif /* CONFIG_SMP */
#ifdef CONFIG_PPC_PSERIES
	/* actually iSeries does not use any of xics...but it has link dependencies
	 * for now, except this new one...
	 */
	} else if (systemcfg->platform == PLATFORM_PSERIES_LPAR) {
		ops = &pSeriesLP_ops;
#endif
	}

	xics_8259_pic.enable = i8259_pic.enable;
	xics_8259_pic.disable = i8259_pic.disable;
	for (i = 0; i < 16; ++i)
		irq_desc[i].handler = &xics_8259_pic;
	for (; i < NR_IRQS; ++i)
		irq_desc[i].handler = &xics_pic;

	ops->cppr_info(boot_cpuid, 0xff);
	iosync();
	if (xics_irq_8259_cascade != -1) {
		if (request_irq(xics_irq_8259_cascade + XICS_IRQ_OFFSET,
				no_action, 0, "8259 cascade", 0))
			printk(KERN_ERR "xics_init_IRQ: couldn't get 8259 cascade\n");
		i8259_init();
	}

#ifdef CONFIG_SMP
	real_irq_to_virt_map[XICS_IPI] = virt_irq_to_real_map[XICS_IPI] =
		XICS_IPI;
	/* IPIs are marked SA_INTERRUPT as they must run with irqs disabled */
	request_irq(XICS_IPI + XICS_IRQ_OFFSET, xics_ipi_action, SA_INTERRUPT,
		    "IPI", 0);
	irq_desc[XICS_IPI+XICS_IRQ_OFFSET].status |= IRQ_PER_CPU;
#endif
	ppc64_boot_msg(0x21, "XICS Done");
}

void xics_set_affinity(unsigned int virq, unsigned long cpumask)
{
        irq_desc_t *desc = irq_desc + virq;
	unsigned int irq;
	unsigned long flags;
	long status;
	unsigned long xics_status[2];
	unsigned long newmask;

	virq -= XICS_IRQ_OFFSET;
	irq = virt_irq_to_real(virq);
	if (irq == XICS_IPI)
		return;

        spin_lock_irqsave(&desc->lock, flags);

	status = rtas_call(ibm_get_xive, 1, 3, (void *)&xics_status, irq);

	if (status) {
		printk("xics_set_affinity: irq=%d ibm,get-xive returns %ld\n",
			irq, status);
		goto out;
	}

	/* For the moment only implement delivery to all cpus or one cpu */
	if (cpumask == -1UL) {
		newmask = default_distrib_server;
	} else {
		if (!(cpumask & cpu_online_map))
			goto out;
		newmask = find_first_bit(&cpumask, 8*sizeof(unsigned long));
	}

	status = rtas_call(ibm_set_xive, 3, 1, NULL,
				irq, newmask, xics_status[1]);

	if (status) {
		printk("xics_set_affinity irq=%d ibm,set-xive returns %ld\n",
			irq, status);
		goto out;
	}

out:
        spin_unlock_irqrestore(&desc->lock, flags);
}
