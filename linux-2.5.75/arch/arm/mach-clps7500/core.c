/*
 *  linux/arch/arm/mach-clps7500/core.c
 *
 *  Copyright (C) 1998 Russell King
 *  Copyright (C) 1999 Nexus Electronics Ltd
 *
 * Extra MM routines for CL7500 architecture
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

static void cl7500_ack_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
	iomd_writeb(mask, IOMD_IRQCLRA);
}

static void cl7500_mask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val & ~mask, IOMD_IRQMASKA);
}

static void cl7500_unmask_irq_a(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << irq;
	val = iomd_readb(IOMD_IRQMASKA);
	iomd_writeb(val | mask, IOMD_IRQMASKA);
}

static struct irqchip clps7500_a_chip = {
	.ack	= cl7500_ack_irq_a,
	.mask	= cl7500_mask_irq_a,
	.unmask	= cl7500_unmask_irq_a,
};

static void cl7500_mask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val & ~mask, IOMD_IRQMASKB);
}

static void cl7500_unmask_irq_b(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKB);
	iomd_writeb(val | mask, IOMD_IRQMASKB);
}

static struct irqchip clps7500_b_chip = {
	.ack	= cl7500_mask_irq_b,
	.mask	= cl7500_mask_irq_b,
	.unmask	= cl7500_unmask_irq_b,
};

static void cl7500_mask_irq_c(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKC);
	iomd_writeb(val & ~mask, IOMD_IRQMASKC);
}

static void cl7500_unmask_irq_c(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKC);
	iomd_writeb(val | mask, IOMD_IRQMASKC);
}

static struct irqchip clps7500_c_chip = {
	.ack	= cl7500_mask_irq_c,
	.mask	= cl7500_mask_irq_c,
	.unmask	= cl7500_unmask_irq_c,
};

static void cl7500_mask_irq_d(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKD);
	iomd_writeb(val & ~mask, IOMD_IRQMASKD);
}

static void cl7500_unmask_irq_d(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_IRQMASKD);
	iomd_writeb(val | mask, IOMD_IRQMASKD);
}

static struct irqchip clps7500_d_chip = {
	.ack	= cl7500_mask_irq_d,
	.mask	= cl7500_mask_irq_d,
	.unmask	= cl7500_unmask_irq_d,
};

static void cl7500_mask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val & ~mask, IOMD_DMAMASK);
}

static void cl7500_unmask_irq_dma(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_DMAMASK);
	iomd_writeb(val | mask, IOMD_DMAMASK);
}

static struct irqchip clps7500_dma_chip = {
	.ack	= cl7500_mask_irq_dma,
	.mask	= cl7500_mask_irq_dma,
	.unmask	= cl7500_unmask_irq_dma,
};

static void cl7500_mask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val & ~mask, IOMD_FIQMASK);
}

static void cl7500_unmask_irq_fiq(unsigned int irq)
{
	unsigned int val, mask;

	mask = 1 << (irq & 7);
	val = iomd_readb(IOMD_FIQMASK);
	iomd_writeb(val | mask, IOMD_FIQMASK);
}

static struct irqchip clps7500_fiq_chip = {
	.ack	= cl7500_mask_irq_fiq,
	.mask	= cl7500_mask_irq_fiq,
	.unmask	= cl7500_unmask_irq_fiq,
};

static void cl7500_no_action(unsigned int irq)
{
}

static struct irqchip clps7500_no_chip = {
	.ack	= cl7500_no_action,
	.mask	= cl7500_no_action,
	.unmask	= cl7500_no_action,
};

static void no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}

static struct irqaction irq_isa = { no_action, 0, 0, "isa", NULL, NULL };

static void __init clps7500_init_irq(void)
{
	unsigned int irq, flags;

	iomd_writeb(0, IOMD_IRQMASKA);
	iomd_writeb(0, IOMD_IRQMASKB);
	iomd_writeb(0, IOMD_FIQMASK);
	iomd_writeb(0, IOMD_DMAMASK);

	for (irq = 0; irq < NR_IRQS; irq++) {
		flags = IRQF_VALID;

		if (irq <= 6 || (irq >= 9 && irq <= 15) ||
		    (irq >= 48 && irq <= 55))
			flags |= IRQF_PROBE;

		switch (irq) {
		case 0 ... 7:
			set_irq_chip(irq, &clps7500_a_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 8 ... 15:
			set_irq_chip(irq, &clps7500_b_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 16 ... 22:
			set_irq_chip(irq, &clps7500_dma_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 24 ... 31:
			set_irq_chip(irq, &clps7500_c_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 40 ... 47:
			set_irq_chip(irq, &clps7500_d_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 48 ... 55:
			set_irq_chip(irq, &clps7500_no_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;

		case 64 ... 72:
			set_irq_chip(irq, &clps7500_fiq_chip);
			set_irq_handler(irq, do_level_IRQ);
			set_irq_flags(irq, flags);
			break;
		}
	}

	setup_irq(IRQ_ISA, &irq_isa);
}

static struct map_desc cl7500_io_desc[] __initdata = {
	{ IO_BASE,	IO_START,	IO_SIZE,    MT_DEVICE },	/* IO space	*/
	{ ISA_BASE,	ISA_START,	ISA_SIZE,   MT_DEVICE },	/* ISA space	*/
	{ FLASH_BASE,	FLASH_START,	FLASH_SIZE, MT_DEVICE },	/* Flash	*/
	{ LED_BASE,	LED_START,	LED_SIZE,   MT_DEVICE } 	/* LED		*/
};

static void __init clps7500_map_io(void)
{
	iotable_init(cl7500_io_desc, ARRAY_SIZE(cl7500_io_desc));
}

MACHINE_START(CLPS7500, "CL-PS7500")
	MAINTAINER("Philip Blundell")
	BOOT_MEM(0x10000000, 0x03000000, 0xe0000000)
	MAPIO(clps7500_map_io)
	INITIRQ(clps7500_init_irq)
MACHINE_END

