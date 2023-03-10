/*
 *  linux/arch/arm/mach-pxa/lubbock.c
 *
 *  Support for the Intel DBPXA250 Development Platform.
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/irq.h>
#include <asm/hardware/sa1111.h>

#include "generic.h"


static unsigned long lubbock_irq_enabled;

static void lubbock_mask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	LUB_IRQ_MASK_EN = (lubbock_irq_enabled &= ~(1 << lubbock_irq));
}

static void lubbock_unmask_irq(unsigned int irq)
{
	int lubbock_irq = (irq - LUBBOCK_IRQ(0));
	/* the irq can be acknowledged only if deasserted, so it's done here */
	LUB_IRQ_SET_CLR &= ~(1 << lubbock_irq);
	LUB_IRQ_MASK_EN = (lubbock_irq_enabled |= (1 << lubbock_irq));
}

static struct irqchip lubbock_irq_chip = {
	.ack		= lubbock_mask_irq,
	.mask		= lubbock_mask_irq,
	.unmask		= lubbock_unmask_irq,
};

static void lubbock_irq_handler(unsigned int irq, struct irqdesc *desc,
				struct pt_regs *regs)
{
	unsigned long pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	do {
		GEDR(0) = GPIO_bit(0);	/* clear our parent irq */
		if (likely(pending)) {
			irq = LUBBOCK_IRQ(0) + __ffs(pending);
			desc = irq_desc + irq;
			desc->handle(irq, desc, regs);
		}
		pending = LUB_IRQ_SET_CLR & lubbock_irq_enabled;
	} while (pending);
}

static void __init lubbock_init_irq(void)
{
	int irq;

	pxa_init_irq();

	/* setup extra lubbock irqs */
	for (irq = LUBBOCK_IRQ(0); irq <= LUBBOCK_IRQ(5); irq++) {
		set_irq_chip(irq, &lubbock_irq_chip);
		set_irq_handler(irq, do_level_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	set_irq_chained_handler(IRQ_GPIO(0), lubbock_irq_handler);
	set_irq_type(IRQ_GPIO(0), IRQT_FALLING);
}

static struct resource sa1111_resources[] = {
	[0] = {
		.start	= 0x10000000,
		.end	= 0x10001fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= LUBBOCK_SA1111_IRQ,
		.end	= LUBBOCK_SA1111_IRQ,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.name	= "Intel Corporation SA1111",
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init lubbock_init(void)
{
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}

subsys_initcall(lubbock_init);

static struct map_desc lubbock_io_desc[] __initdata = {
 /* virtual     physical    length      type */
  { 0xf0000000, 0x08000000, 0x00100000, MT_DEVICE }, /* CPLD */
  { 0xf1000000, 0x0c000000, 0x00100000, MT_DEVICE }, /* LAN91C96 IO */
  { 0xf1100000, 0x0e000000, 0x00100000, MT_DEVICE }, /* LAN91C96 Attr */
};

static void __init lubbock_map_io(void)
{
	pxa_map_io();
	iotable_init(lubbock_io_desc, ARRAY_SIZE(lubbock_io_desc));

	/* This enables the BTUART */
	CKEN |= CKEN7_BTUART;
	pxa_gpio_mode(GPIO42_BTRXD_MD);
	pxa_gpio_mode(GPIO43_BTTXD_MD);
	pxa_gpio_mode(GPIO44_BTCTS_MD);
	pxa_gpio_mode(GPIO45_BTRTS_MD);

	/* This is for the SMC chip select */
	pxa_gpio_mode(GPIO79_nCS_3_MD);

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00008000;
	PGSR1 = 0x003F0202;
	PGSR2 = 0x0001C000;
	PCFR |= PCFR_OPDE;
}

MACHINE_START(LUBBOCK, "Intel DBPXA250 Development Platform")
	MAINTAINER("MontaVista Software Inc.")
	BOOT_MEM(0xa0000000, 0x40000000, io_p2v(0x40000000))
	MAPIO(lubbock_map_io)
	INITIRQ(lubbock_init_irq)
MACHINE_END
