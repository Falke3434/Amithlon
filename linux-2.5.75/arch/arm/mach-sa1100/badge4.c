/*
 * linux/arch/arm/mach-sa1100/badge4.c
 *
 * BadgePAD 4 specific initialization
 *
 *   Tim Connors <connors@hpl.hp.com>
 *   Christopher Hoover <ch@hpl.hp.com>
 *
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/errno.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/arch/irqs.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/hardware/sa1111.h>
#include <asm/mach/serial_sa1100.h>

#include <asm/arch/badge4.h>

#include "generic.h"

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= BADGE4_SA1111_BASE,
		.end		= BADGE4_SA1111_BASE + 0x00001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= BADGE4_IRQ_GPIO_SA1111,
		.end		= BADGE4_IRQ_GPIO_SA1111,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.name	= "Intel Corporation SA1111",
		.dma_mask = &sa1111_dmamask;
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
};

static int __init badge4_sa1111_init(void)
{
	/*
	 * Ensure that the memory bus request/grant signals are setup,
	 * and the grant is held in its inactive state
	 */
	sa1110_mb_disable();

	/*
	 * Probe for SA1111.
	 */
	return platform_add_devices(devices, ARRAY_SIZE(devices));
}


static int five_v_on __initdata = 0;

static int __init five_v_on_setup(char *ignore)
{
        five_v_on = 1;
	return 1;
}
__setup("five_v_on", five_v_on_setup);


static int __init badge4_init(void)
{
	int ret;

	if (!machine_is_badge4())
		return -ENODEV;

	/* LCD */
	GPCR  = (BADGE4_GPIO_LGP2 | BADGE4_GPIO_LGP3 |
		 BADGE4_GPIO_LGP4 | BADGE4_GPIO_LGP5 |
		 BADGE4_GPIO_LGP6 | BADGE4_GPIO_LGP7 |
		 BADGE4_GPIO_LGP8 | BADGE4_GPIO_LGP9 |
		 BADGE4_GPIO_GPA_VID | BADGE4_GPIO_GPB_VID |
		 BADGE4_GPIO_GPC_VID);
	GPDR &= ~BADGE4_GPIO_INT_VID;
	GPDR |= (BADGE4_GPIO_LGP2 | BADGE4_GPIO_LGP3 |
		 BADGE4_GPIO_LGP4 | BADGE4_GPIO_LGP5 |
		 BADGE4_GPIO_LGP6 | BADGE4_GPIO_LGP7 |
		 BADGE4_GPIO_LGP8 | BADGE4_GPIO_LGP9 |
		 BADGE4_GPIO_GPA_VID | BADGE4_GPIO_GPB_VID |
		 BADGE4_GPIO_GPC_VID);

	/* SDRAM SPD i2c */
	GPCR  = (BADGE4_GPIO_SDSDA | BADGE4_GPIO_SDSCL);
	GPDR |= (BADGE4_GPIO_SDSDA | BADGE4_GPIO_SDSCL);

	/* uart */
	GPCR  = (BADGE4_GPIO_UART_HS1 | BADGE4_GPIO_UART_HS2);
	GPDR |= (BADGE4_GPIO_UART_HS1 | BADGE4_GPIO_UART_HS2);

	/* CPLD muxsel0 input for mux/adc chip select */
	GPCR  = BADGE4_GPIO_MUXSEL0;
	GPDR |= BADGE4_GPIO_MUXSEL0;

	/* test points: J5, J6 as inputs, J7 outputs */
	GPDR &= ~(BADGE4_GPIO_TESTPT_J5 | BADGE4_GPIO_TESTPT_J6);
	GPCR  = BADGE4_GPIO_TESTPT_J7;
	GPDR |= BADGE4_GPIO_TESTPT_J7;

 	/* 5V supply rail. */
 	GPCR  = BADGE4_GPIO_PCMEN5V;		/* initially off */
  	GPDR |= BADGE4_GPIO_PCMEN5V;

	/* CPLD sdram type inputs; set up by blob */
	//GPDR |= (BADGE4_GPIO_SDTYP1 | BADGE4_GPIO_SDTYP0);
	printk(KERN_DEBUG __FILE__ ": SDRAM CPLD typ1=%d typ0=%d\n",
	       !!(GPLR & BADGE4_GPIO_SDTYP1),
	       !!(GPLR & BADGE4_GPIO_SDTYP0));

	/* SA1111 reset pin; set up by blob */
	//GPSR  = BADGE4_GPIO_SA1111_NRST;
	//GPDR |= BADGE4_GPIO_SA1111_NRST;


	/* power management cruft */
	PGSR = 0;
	PWER = 0;
	PCFR = 0;
	PSDR = 0;

	PWER |= PWER_GPIO26;	/* wake up on an edge from TESTPT_J5 */
	PWER |= PWER_RTC;	/* wake up if rtc fires */

	/* drive sa1111_nrst during sleep */
	PGSR |= BADGE4_GPIO_SA1111_NRST;
	/* drive CPLD as is during sleep */
	PGSR |= (GPLR & (BADGE4_GPIO_SDTYP0|BADGE4_GPIO_SDTYP1));


	/* Now bring up the SA-1111. */
	ret = badge4_sa1111_init();
	if (ret < 0)
		printk(KERN_ERR
		       "%s: SA-1111 initialization failed (%d)\n",
		       __FUNCTION__, ret);


	/* maybe turn on 5v0 from the start */
	badge4_set_5V(BADGE4_5V_INITIALLY, five_v_on);

	return 0;
}

arch_initcall(badge4_init);


static unsigned badge4_5V_bitmap = 0;

void badge4_set_5V(unsigned subsystem, int on)
{
	unsigned long flags;
	unsigned old_5V_bitmap;

	local_irq_save(flags);

	old_5V_bitmap = badge4_5V_bitmap;

	if (on) {
		badge4_5V_bitmap |= subsystem;
	} else {
		badge4_5V_bitmap &= ~subsystem;
	}

	/* detect on->off and off->on transitions */
	if ((!old_5V_bitmap) && (badge4_5V_bitmap)) {
		/* was off, now on */
		printk(KERN_INFO "%s: enabling 5V supply rail\n", __FUNCTION__);
		GPSR = BADGE4_GPIO_PCMEN5V;
	} else if ((old_5V_bitmap) && (!badge4_5V_bitmap)) {
		/* was on, now off */
		printk(KERN_INFO "%s: disabling 5V supply rail\n", __FUNCTION__);
		GPCR = BADGE4_GPIO_PCMEN5V;
	}

	local_irq_restore(flags);
}
EXPORT_SYMBOL(badge4_set_5V);


static struct map_desc badge4_io_desc[] __initdata = {
  /*  virtual    physical    length    type */
  {0xf1000000, 0x08000000, 0x00100000, MT_DEVICE },/* SRAM  bank 1 */
  {0xf2000000, 0x10000000, 0x00100000, MT_DEVICE },/* SRAM  bank 2 */
  {0xf4000000, 0x48000000, 0x00100000, MT_DEVICE } /* SA-1111      */
};

static void
badge4_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (!state) {
		Ser1SDCR0 |= SDCR0_UART;
	}
}

static struct sa1100_port_fns badge4_port_fns __initdata = {
	//.get_mctrl	= badge4_get_mctrl,
	//.set_mctrl	= badge4_set_mctrl,
	.pm		= badge4_uart_pm,
};

static void __init badge4_map_io(void)
{
	sa1100_map_io();
	iotable_init(badge4_io_desc, ARRAY_SIZE(badge4_io_desc));

	sa1100_register_uart_fns(&badge4_port_fns);
	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

MACHINE_START(BADGE4, "Hewlett-Packard Laboratories BadgePAD 4")
	BOOT_MEM(0xc0000000, 0x80000000, 0xf8000000)
	BOOT_PARAMS(0xc0000100)
	MAPIO(badge4_map_io)
	INITIRQ(sa1100_init_irq)
MACHINE_END
