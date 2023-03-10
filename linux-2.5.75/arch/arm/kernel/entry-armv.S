/*
 *  linux/arch/arm/kernel/entry-armv.S
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *  ARM700 fix by Matthew Godbolt (linux-user@willothewisp.demon.co.uk)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Low-level vector interface routines
 *
 *  Note:  there is a StrongARM bug in the STMIA rn, {regs}^ instruction that causes
 *  it to save wrong values...  Be aware!
 */
#include <linux/config.h>
#include <linux/init.h>
#include "entry-header.S"
#include <asm/thread_info.h>
#include <asm/glue.h>


#ifdef IOC_BASE
/* IOC / IOMD based hardware */
#include <asm/hardware/iomd.h>

		.equ	ioc_base_high, IOC_BASE & 0xff000000
		.equ	ioc_base_low, IOC_BASE & 0x00ff0000
		.macro	disable_fiq
		mov	r12, #ioc_base_high
		.if	ioc_base_low
		orr	r12, r12, #ioc_base_low
		.endif
		strb	r12, [r12, #0x38]	@ Disable FIQ register
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #ioc_base_high		@ point at IOC
		.if	ioc_base_low
		orr	r4, r4, #ioc_base_low
		.endif
		ldrb	\irqstat, [r4, #IOMD_IRQREQB]	@ get high priority first
		ldr	\base, =irq_prio_h
		teq	\irqstat, #0
#ifdef IOMD_BASE
		ldreqb	\irqstat, [r4, #IOMD_DMAREQ]	@ get dma
		addeq	\base, \base, #256		@ irq_prio_h table size
		teqeq	\irqstat, #0
		bne	2406f
#endif
		ldreqb	\irqstat, [r4, #IOMD_IRQREQA]	@ get low priority
		addeq	\base, \base, #256		@ irq_prio_d table size
		teqeq	\irqstat, #0
#ifdef IOMD_IRQREQC
		ldreqb	\irqstat, [r4, #IOMD_IRQREQC]
		addeq	\base, \base, #256		@ irq_prio_l table size
		teqeq	\irqstat, #0
#endif
#ifdef IOMD_IRQREQD
		ldreqb	\irqstat, [r4, #IOMD_IRQREQD]
		addeq	\base, \base, #256		@ irq_prio_lc table size
		teqeq	\irqstat, #0
#endif
2406:		ldrneb	\irqnr, [\base, \irqstat]	@ get IRQ number
		.endm

/*
 * Interrupt table (incorporates priority).  Please note that we
 * rely on the order of these tables (see above code).
 */
		.macro	irq_prio_table
irq_prio_h:	.byte	 0, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	12, 8, 9, 8,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	14,14,14,14,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	15,15,15,15,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
		.byte	13,13,13,13,10,10,10,10,11,11,11,11,10,10,10,10
#ifdef IOMD_BASE
irq_prio_d:	.byte	 0,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	20,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	23,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	22,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
		.byte	21,16,17,16,18,16,17,16,19,16,17,16,18,16,17,16
#endif
irq_prio_l:	.byte	 0, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 4, 0, 1, 0, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 6, 6, 6, 6, 6, 6, 6, 6, 3, 3, 3, 3, 3, 3, 3, 3
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		.byte	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
#ifdef IOMD_IRQREQC
irq_prio_lc:	.byte	24,24,25,24,26,26,26,26,27,27,27,27,27,27,27,27
		.byte	28,24,25,24,26,26,26,26,27,27,27,27,27,27,27,27
		.byte	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
		.byte	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
		.byte	30,30,30,30,30,30,30,30,27,27,27,27,27,27,27,27
		.byte	30,30,30,30,30,30,30,30,27,27,27,27,27,27,27,27
		.byte	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
		.byte	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
		.byte	31,31,31,31,31,31,31,31,31,31,31,31,31,31,31,31
#endif
#ifdef IOMD_IRQREQD
irq_prio_ld:	.byte	40,40,41,40,42,42,42,42,43,43,43,43,43,43,43,43
		.byte	44,40,41,40,42,42,42,42,43,43,43,43,43,43,43,43
		.byte	45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45
		.byte	45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45
		.byte	46,46,46,46,46,46,46,46,43,43,43,43,43,43,43,43
		.byte	46,46,46,46,46,46,46,46,43,43,43,43,43,43,43,43
		.byte	45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45
		.byte	45,45,45,45,45,45,45,45,45,45,45,45,45,45,45,45
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
		.byte	47,47,47,47,47,47,47,47,47,47,47,47,47,47,47,47
#endif
		.endm

#elif defined(CONFIG_ARCH_EBSA110)

#define IRQ_STAT		0xff000000	/* read */

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, stat, base, tmp
		mov	\base, #IRQ_STAT
		ldrb	\stat, [\base]			@ get interrupts
		mov	\irqnr, #0
		tst	\stat, #15
		addeq	\irqnr, \irqnr, #4
		moveq	\stat, \stat, lsr #4
		tst	\stat, #3
		addeq	\irqnr, \irqnr, #2
		moveq	\stat, \stat, lsr #2
		tst	\stat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\stat, \stat, lsr #1
		tst	\stat, #1			@ bit 0 should be set
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_SHARK)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #0xe0000000

		mov	\irqstat, #0x0C
		strb	\irqstat, [r4, #0x20]		@outb(0x0C, 0x20) /* Poll command */
		ldrb	\irqnr, [r4, #0x20]		@irq = inb(0x20) & 7
		and	\irqstat, \irqnr, #0x80
		teq	\irqstat, #0
		beq	43f
		and	\irqnr, \irqnr, #7
		teq	\irqnr, #2
		bne	44f
43:		mov	\irqstat, #0x0C
		strb	\irqstat, [r4, #0xa0]		@outb(0x0C, 0xA0) /* Poll command */
		ldrb	\irqnr, [r4, #0xa0]		@irq = (inb(0xA0) & 7) + 8
		and	\irqstat, \irqnr, #0x80
		teq	\irqstat, #0
		beq	44f
		and	\irqnr, \irqnr, #7
		add	\irqnr, \irqnr, #8
44:		teq	\irqstat, #0
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_FOOTBRIDGE)
#include <asm/hardware/dec21285.h>

		.macro	disable_fiq
		.endm

		.equ	dc21285_high, ARMCSR_BASE & 0xff000000
		.equ	dc21285_low, ARMCSR_BASE & 0x00ffffff

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #dc21285_high
		.if	dc21285_low
		orr	r4, r4, #dc21285_low
		.endif
		ldr	\irqstat, [r4, #0x180]		@ get interrupts

		mov	\irqnr, #IRQ_SDRAMPARITY
		tst	\irqstat, #IRQ_MASK_SDRAMPARITY
		bne	1001f

		tst	\irqstat, #IRQ_MASK_UART_RX
		movne	\irqnr, #IRQ_CONRX
		bne	1001f

		tst	\irqstat, #IRQ_MASK_DMA1
		movne	\irqnr, #IRQ_DMA1
		bne	1001f

		tst	\irqstat, #IRQ_MASK_DMA2
		movne	\irqnr, #IRQ_DMA2
		bne	1001f

		tst	\irqstat, #IRQ_MASK_IN0
		movne	\irqnr, #IRQ_IN0
		bne	1001f

		tst	\irqstat, #IRQ_MASK_IN1
		movne	\irqnr, #IRQ_IN1
		bne	1001f

		tst	\irqstat, #IRQ_MASK_IN2
		movne	\irqnr, #IRQ_IN2
		bne	1001f

		tst	\irqstat, #IRQ_MASK_IN3
		movne	\irqnr, #IRQ_IN3
		bne	1001f

		tst	\irqstat, #IRQ_MASK_PCI
		movne	\irqnr, #IRQ_PCI
		bne	1001f

		tst	\irqstat, #IRQ_MASK_DOORBELLHOST
		movne	\irqnr, #IRQ_DOORBELLHOST
		bne     1001f
	
		tst	\irqstat, #IRQ_MASK_I2OINPOST
		movne	\irqnr, #IRQ_I2OINPOST
		bne	1001f

		tst	\irqstat, #IRQ_MASK_TIMER1
		movne	\irqnr, #IRQ_TIMER1
		bne	1001f

		tst	\irqstat, #IRQ_MASK_TIMER2
		movne	\irqnr, #IRQ_TIMER2
		bne	1001f

		tst	\irqstat, #IRQ_MASK_TIMER3
		movne	\irqnr, #IRQ_TIMER3
		bne	1001f

		tst	\irqstat, #IRQ_MASK_UART_TX
		movne	\irqnr, #IRQ_CONTX
		bne	1001f

		tst	\irqstat, #IRQ_MASK_PCI_ABORT
		movne	\irqnr, #IRQ_PCI_ABORT
		bne	1001f

		tst	\irqstat, #IRQ_MASK_PCI_SERR
		movne	\irqnr, #IRQ_PCI_SERR
		bne	1001f

		tst	\irqstat, #IRQ_MASK_DISCARD_TIMER
		movne	\irqnr, #IRQ_DISCARD_TIMER
		bne	1001f

		tst	\irqstat, #IRQ_MASK_PCI_DPERR
		movne	\irqnr, #IRQ_PCI_DPERR
		bne	1001f

		tst	\irqstat, #IRQ_MASK_PCI_PERR
		movne	\irqnr, #IRQ_PCI_PERR
1001:
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_NEXUSPCI)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, =INTCONT_BASE
		ldr	\base, =soft_irq_mask
		ldr	\irqstat, [\irqstat]		@ get interrupts
		ldr	\base, [\base]
		mov	\irqnr, #0
		and	\irqstat, \irqstat, \base	@ mask out disabled ones
1001:		tst	\irqstat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\irqstat, \irqstat, lsr #1
		tsteq	\irqnr, #32
		beq	1001b
		teq	\irqnr, #32
		.endm

		.macro	irq_prio_table
		.ltorg
		.bss
ENTRY(soft_irq_mask)
		.word	0
		.text
		.endm

#elif defined(CONFIG_ARCH_TBOX)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, =0xffff7000
		ldr	\irqstat, [\irqstat]		@ get interrupts
		ldr	\base, =soft_irq_mask
		ldr	\base, [\base]
		mov	\irqnr, #0
		and	\irqstat, \irqstat, \base	@ mask out disabled ones
1001:		tst	\irqstat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\irqstat, \irqstat, lsr #1
		tsteq	\irqnr, #32
		beq	1001b
		teq	\irqnr, #32
		.endm

		.macro	irq_prio_table
		.ltorg
		.bss
ENTRY(soft_irq_mask)
		.word	0
		.text
		.endm

#elif defined(CONFIG_ARCH_SA1100)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #0xfa000000			@ ICIP = 0xfa050000
		add	r4, r4, #0x00050000
		ldr	\irqstat, [r4]			@ get irqs
		ldr	\irqnr, [r4, #4]		@ ICMR = 0xfa050004
		ands	\irqstat, \irqstat, \irqnr
		mov	\irqnr, #0
		beq	1001f
		tst	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tsteq	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tsteq	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tst	\irqstat, #0x0f
		moveq	\irqstat, \irqstat, lsr #4
		addeq	\irqnr, \irqnr, #4
		tst	\irqstat, #0x03
		moveq	\irqstat, \irqstat, lsr #2
		addeq	\irqnr, \irqnr, #2
		tst	\irqstat, #0x01
		addeqs	\irqnr, \irqnr, #1
1001:
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_L7200)
#include <asm/hardware.h>
	
		.equ	irq_base_addr,	IO_BASE_2

		.macro  disable_fiq
		.endm
 
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov     \irqstat, #irq_base_addr		@ Virt addr IRQ regs
		add	\irqstat, \irqstat, #0x00001000		@ Status reg
		ldr     \irqstat, [\irqstat, #0]		@ get interrupts
		mov     \irqnr, #0
1001:		tst     \irqstat, #1
		addeq   \irqnr, \irqnr, #1
		moveq   \irqstat, \irqstat, lsr #1
		tsteq   \irqnr, #32
		beq     1001b
		teq     \irqnr, #32
		.endm

		.macro  irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_INTEGRATOR)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
/* FIXME: should not be using soo many LDRs here */
		ldr	\irqnr, =IO_ADDRESS(INTEGRATOR_IC_BASE)
		ldr	\irqstat, [\irqnr, #IRQ_STATUS]		@ get masked status
		ldr	\irqnr, =IO_ADDRESS(INTEGRATOR_HDR_BASE)
		ldr	\irqnr, [\irqnr, #(INTEGRATOR_HDR_IC_OFFSET+IRQ_STATUS)]
		orr	\irqstat, \irqstat, \irqnr, lsl #INTEGRATOR_CM_INT0

		mov	\irqnr, #0
1001:		tst	\irqstat, #1
		bne	1002f
		add	\irqnr, \irqnr, #1
		mov	\irqstat, \irqstat, lsr #1
		cmp	\irqnr, #22
		bcc	1001b
1002:		/* EQ will be set if we reach 22 */
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_CLPS711X)

#include <asm/hardware/clps7111.h>

		.macro	disable_fiq
		.endm

#if (INTSR2 - INTSR1) != (INTMR2 - INTMR1)
#error INTSR stride != INTMR stride
#endif

		.macro	get_irqnr_and_base, irqnr, stat, base, mask
		mov	\base, #CLPS7111_BASE
		ldr	\stat, [\base, #INTSR1]
		ldr	\mask, [\base, #INTMR1]
		mov	\irqnr, #4
		mov	\mask, \mask, lsl #16
		and	\stat, \stat, \mask, lsr #16
		movs	\stat, \stat, lsr #4
		bne	1001f

		add	\base, \base, #INTSR2 - INTSR1
		ldr	\stat, [\base, #INTSR1]
		ldr	\mask, [\base, #INTMR1]
		mov	\irqnr, #16
		mov	\mask, \mask, lsl #16
		and	\stat, \stat, \mask, lsr #16

1001:		tst	\stat, #255
		addeq	\irqnr, \irqnr, #8
		moveq	\stat, \stat, lsr #8
		tst	\stat, #15
		addeq	\irqnr, \irqnr, #4
		moveq	\stat, \stat, lsr #4
		tst	\stat, #3
		addeq	\irqnr, \irqnr, #2
		moveq	\stat, \stat, lsr #2
		tst	\stat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\stat, \stat, lsr #1
		tst	\stat, #1			@ bit 0 should be set
		.endm

		.macro	irq_prio_table
		.endm
	
#elif defined (CONFIG_ARCH_CAMELOT)
#include <asm/arch/platform.h>
#undef IRQ_MODE /* same name defined in asm/proc/ptrace.h */
#include <asm/arch/int_ctrl00.h>
	
		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	
		ldr	\irqstat, =INT_ID(IO_ADDRESS(EXC_INT_CTRL00_BASE))
		ldr	\irqnr,[\irqstat]		
		cmp	\irqnr,#0
		subne	\irqnr,\irqnr,#1

	
		.endm

		.macro	irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_ANAKIN)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	\base, #IO_BASE
		mov	\irqstat, #INTERRUPT_CONTROLLER
		ldr	\tmp, =anakin_irq_mask
		ldr	\irqstat, [\base, \irqstat]
		ldr	\tmp, [\tmp]
		ands	\irqstat, \irqstat, \tmp
		ldrne	\tmp, =anakin_active_irqs
		strne	\irqstat, [\tmp]
		movne	\irqnr, #IRQ_ANAKIN
		.endm

		.macro	irq_prio_table
		.ltorg
		.bss
ENTRY(anakin_irq_mask)
		.word	0
ENTRY(anakin_active_irqs)
		.space	4
		.text
		.endm

#elif defined(CONFIG_ARCH_IOP310) || defined(CONFIG_ARCH_ADIFCC)

		.macro	disable_fiq
		.endm

        	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
        	mrc p13, 0, \irqstat, c4, c0, 0 @ get INTSRC
        	mrc p13, 0, \base, c0, c0, 0    @ get INTCTL

        	tst \irqstat, #(1<<29)      @ if INTSRC_BI
        	tstne   \base, #(1<<3)          @ and INTCTL_BM
        	movne   \irqnr, #IRQ_XS80200_BCU
        	bne 1001f

        	tst \irqstat, #(1<<28)      @ if INTSRC_PI
        	tstne   \base, #(1<<2)          @ and INTCTL_PM
        	movne   \irqnr, #IRQ_XS80200_PMU
        	bne 1001f

        	tst \irqstat, #(1<<31)      @ if INTSRC_FI
        	tstne   \base, #(1<<0)          @ and INTCTL_FM
        	movne   \irqnr, #IRQ_XS80200_EXTFIQ
        	bne 1001f

        	tst \irqstat, #(1<<30)      @ if INTSRC_II
        	tstne   \base, #(1<<1)          @ and INTCTL_IM
        	movne   \irqnr, #IRQ_XS80200_EXTIRQ

1001:
  		.endm

  		.macro	irq_prio_table
 		.endm

#elif defined(CONFIG_ARCH_IOP321)
		.macro  disable_fiq
		.endm

		/*
		 * Note: only deal with normal interrupts, not FIQ
		 */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov     \irqnr, #0
		mrc     p6, 0, \irqstat, c8, c0, 0      @ Read IINTSRC
		cmp     \irqstat, #0
		beq     1001f
		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr,\base,\irqnr
		add     \irqnr,\irqnr,#IRQ_IOP321_DMA0_EOT
1001:
		.endm

		.macro  irq_prio_table
		.endm

#elif defined(CONFIG_ARCH_PXA)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	\base, #io_p2v(0x40000000)	@ IIR Ctl = 0x40d00000
		add	\base, \base, #0x00d00000
		ldr	\irqstat, [\base, #0]		@ ICIP
		ldr	\irqnr, [\base, #4]		@ ICMR
		ands	\irqnr, \irqstat, \irqnr
		beq	1001f
		rsb	\irqstat, \irqnr, #0
		and	\irqstat, \irqstat, \irqnr
		clz	\irqnr, \irqstat
		rsb	\irqnr, \irqnr, #(31 - PXA_IRQ_SKIP)
1001:
		.endm

		.macro	irq_prio_table
		.endm

#else
#error Unknown architecture
#endif

/*
 * Invalid mode handlers
 */
__pabt_invalid:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - lr}			@ Save XXX r0 - lr
		ldr	r4, .LCabt
		mov	r1, #BAD_PREFETCH
		b	1f

__dabt_invalid:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - lr}			@ Save SVC r0 - lr [lr *should* be intact]
		ldr	r4, .LCabt
		mov	r1, #BAD_DATA
		b	1f

__irq_invalid:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate space on stack for frame
		stmfd	sp, {r0 - lr}			@ Save r0 - lr
		ldr	r4, .LCirq
		mov	r1, #BAD_IRQ
		b	1f

__und_invalid:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - lr}
		ldr	r4, .LCund
		mov	r1, #BAD_UNDEFINSTR		@ int reason

1:		zero_fp
		ldmia	r4, {r5 - r7}			@ Get XXX pc, cpsr, old_r0
		add	r4, sp, #S_PC
		stmia	r4, {r5 - r7}			@ Save XXX pc, cpsr, old_r0
		mov	r0, sp
		and	r2, r6, #31			@ int mode
		b	bad_mode

#if 1 /* defined CONFIG_FPE_NWFPE || defined CONFIG_FPE_FASTFPE */
		/* The FPE is always present */
		.equ	fpe_not_present, fpundefinstr
#else
wfs_mask_data:	.word	0x0e200110			@ WFS/RFS
		.word	0x0fef0fff
		.word	0x0d000100			@ LDF [sp]/STF [sp]
		.word	0x0d000100			@ LDF [fp]/STF [fp]
		.word	0x0f000f00

/* We get here if an undefined instruction happens and the floating
 * point emulator is not present.  If the offending instruction was
 * a WFS, we just perform a normal return as if we had emulated the
 * operation.  This is a hack to allow some basic userland binaries
 * to run so that the emulator module proper can be loaded. --philb
 */
fpe_not_present:
		adr	r10, wfs_mask_data
		ldmia	r10, {r4, r5, r6, r7, r8}
		ldr	r10, [sp, #S_PC]		@ Load PC
		sub	r10, r10, #4
		mask_pc	r10, r10
		ldrt	r10, [r10]			@ get instruction
		and	r5, r10, r5
		teq	r5, r4				@ Is it WFS?
		moveq	pc, r9
		and	r5, r10, r8
		teq	r5, r6				@ Is it LDF/STF on sp or fp?
		teqne	r5, r7
		movne	pc, lr
		tst	r10, #0x00200000		@ Does it have WB
		moveq	pc, r9
		and	r4, r10, #255			@ get offset
		and	r6, r10, #0x000f0000
		tst	r10, #0x00800000		@ +/-
		ldr	r5, [sp, r6, lsr #14]		@ Load reg
		rsbeq	r4, r4, #0
		add	r5, r5, r4, lsl #2
		str	r5, [sp, r6, lsr #14]		@ Save reg
		mov	pc, r9
#endif

/*
 * SVC mode handlers
 */
		.align	5
__dabt_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r2, .LCabt
		add	r0, sp, #S_FRAME_SIZE
		ldmia	r2, {r2 - r4}			@ get pc, cpsr
		add	r5, sp, #S_SP
		mov	r1, lr
		stmia	r5, {r0 - r4}			@ save sp_SVC, lr_SVC, pc, cpsr, old_ro
		mrs	r9, cpsr			@ Enable interrupts if they were
		tst	r3, #PSR_I_BIT
		biceq	r9, r9, #PSR_I_BIT		@ previously
/*
 * This routine must not corrupt r9
 */
#ifdef MULTI_ABORT
		ldr	r4, .LCprocfns			@ pass r0, r3 to
		mov	lr, pc				@ processor code
		ldr	pc, [r4]			@ call processor specific code
#else
		bl	CPU_ABORT_HANDLER
#endif
		msr	cpsr_c, r9
		mov	r2, sp
		bl	do_DataAbort
		disable_irq r0
		ldr	r0, [sp, #S_PSR]
		msr	spsr, r0
		ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

		.align	5
__irq_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r7, .LCirq
		add	r5, sp, #S_FRAME_SIZE
		ldmia	r7, {r7 - r9}
		add	r4, sp, #S_SP
		mov	r6, lr
		stmia	r4, {r5, r6, r7, r8, r9}	@ save sp_SVC, lr_SVC, pc, cpsr, old_ro
#ifdef CONFIG_PREEMPT
		get_thread_info r8
		ldr	r9, [r8, #TI_PREEMPT]		@ get preempt count
		add	r7, r9, #1			@ increment it
		str	r7, [r8, #TI_PREEMPT]
#endif
1:		get_irqnr_and_base r0, r6, r5, lr
		movne	r1, sp
		@
		@ routine called with r0 = irq number, r1 = struct pt_regs *
		@
		adrsvc	ne, lr, 1b
		bne	asm_do_IRQ
#ifdef CONFIG_PREEMPT
		ldr	r0, [r8, #TI_FLAGS]		@ get flags
		tst	r0, #_TIF_NEED_RESCHED
		blne	svc_preempt
preempt_return:
		ldr	r0, [r8, #TI_PREEMPT]		@ read preempt value
		teq	r0, r7
		strne	r0, [r0, -r0]			@ bug()
		str	r9, [r8, #TI_PREEMPT]		@ restore preempt count
#endif
		ldr	r0, [sp, #S_PSR]		@ irqs are already disabled
		msr	spsr, r0
		ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

		.ltorg

#ifdef CONFIG_PREEMPT
svc_preempt:	teq	r9, #0				@ was preempt count = 0
		ldreq	r6, .LCirq_stat
		movne	pc, lr				@ no
		ldr	r0, [r6, #4]			@ local_irq_count
		ldr	r1, [r6, #8]			@ local_bh_count
		adds	r0, r0, r1
		movne	pc, lr
		mov	r7, #PREEMPT_ACTIVE
		str	r7, [r8, #TI_PREEMPT]		@ set PREEMPT_ACTIVE
1:		enable_irq r2				@ enable IRQs
		bl	schedule
		disable_irq r0				@ disable IRQs
		ldr	r0, [r8, #TI_FLAGS]		@ get new tasks TI_FLAGS
		tst	r0, #_TIF_NEED_RESCHED
		beq	preempt_return			@ go again
		b	1b
#endif

		.align	5
__und_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r3, .LCund
		mov	r4, lr
		ldmia	r3, {r5 - r7}
		add	r3, sp, #S_FRAME_SIZE
		add	r2, sp, #S_SP
		stmia	r2, {r3 - r7}			@ save sp_SVC, lr_SVC, pc, cpsr, old_ro

		ldr	r0, [r5, #-4]			@ r0 = instruction
		adrsvc	al, r9, 1f			@ r9 = normal FP return
		bl	call_fpe			@ lr = undefined instr return

		mov	r0, sp				@ struct pt_regs *regs
		bl	do_undefinstr

1:		disable_irq r0
		ldr	lr, [sp, #S_PSR]		@ Get SVC cpsr
		msr	spsr, lr
		ldmia	sp, {r0 - pc}^			@ Restore SVC registers

		.align	5
__pabt_svc:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r2, .LCabt
		add	r0, sp, #S_FRAME_SIZE
		ldmia	r2, {r2 - r4}			@ get pc, cpsr
		add	r5, sp, #S_SP
		mov	r1, lr
		stmia	r5, {r0 - r4}			@ save sp_SVC, lr_SVC, pc, cpsr, old_ro
		mrs	r9, cpsr			@ Enable interrupts if they were
		tst	r3, #PSR_I_BIT
		biceq	r9, r9, #PSR_I_BIT		@ previously
		msr	cpsr_c, r9
		mov	r0, r2				@ address (pc)
		mov	r1, sp				@ regs
		bl	do_PrefetchAbort		@ call abort handler
		disable_irq r0
		ldr	r0, [sp, #S_PSR]
		msr	spsr, r0
		ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr

		.align	5
.LCirq:		.word	__temp_irq
.LCund:		.word	__temp_und
.LCabt:		.word	__temp_abt
#ifdef MULTI_ABORT
.LCprocfns:	.word	processor
#endif
.LCfp:		.word	fp_enter
#ifdef CONFIG_PREEMPT
.LCirq_stat:	.word	irq_stat
#endif

		irq_prio_table

/*
 * User mode handlers
 */
		.align	5
__dabt_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r7, .LCabt
		add	r5, sp, #S_PC
		ldmia	r7, {r2 - r4}			@ Get USR pc, cpsr
		stmia	r5, {r2 - r4}			@ Save USR pc, cpsr, old_r0
		stmdb	r5, {sp, lr}^
		alignment_trap r7, r7, __temp_abt
		zero_fp
#ifdef MULTI_ABORT
		ldr	r4, .LCprocfns			@ pass r0, r3 to
		mov	lr, pc				@ processor code
		ldr	pc, [r4]			@ call processor specific code
#else
		bl	CPU_ABORT_HANDLER
#endif
		enable_irq r2				@ Enable interrupts
		mov	r2, sp
		adrsvc	al, lr, ret_from_exception
		b	do_DataAbort

		.align	5
__irq_usr:	sub	sp, sp, #S_FRAME_SIZE
		stmia	sp, {r0 - r12}			@ save r0 - r12
		ldr	r4, .LCirq
		add	r8, sp, #S_PC
		ldmia	r4, {r5 - r7}			@ get saved PC, SPSR
		stmia	r8, {r5 - r7}			@ save pc, psr, old_r0
		stmdb	r8, {sp, lr}^
		alignment_trap r4, r7, __temp_irq
		zero_fp
#ifdef CONFIG_PREEMPT
		get_thread_info r8
		ldr	r9, [r8, #TI_PREEMPT]		@ get preempt count
		add	r7, r9, #1			@ increment it
		str	r7, [r8, #TI_PREEMPT]
#endif
1:		get_irqnr_and_base r0, r6, r5, lr
		movne	r1, sp
		adrsvc	ne, lr, 1b
		@
		@ routine called with r0 = irq number, r1 = struct pt_regs *
		@
		bne	asm_do_IRQ
#ifdef CONFIG_PREEMPT
		ldr	r0, [r8, #TI_PREEMPT]
		teq	r0, r7
		strne	r0, [r0, -r0]
		str	r9, [r8, #TI_PREEMPT]
		mov	tsk, r8
#else
		get_thread_info tsk
#endif
		mov	why, #0
		b	ret_to_user

		.ltorg

		.align	5
__und_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ Save r0 - r12
		ldr	r4, .LCund
		add	r8, sp, #S_PC
		ldmia	r4, {r5 - r7}
		stmia	r8, {r5 - r7}			@ Save USR pc, cpsr, old_r0
		stmdb	r8, {sp, lr}^			@ Save user sp, lr
		alignment_trap r4, r7, __temp_und
		zero_fp
		tst	r6, #PSR_T_BIT			@ Thumb mode?
		bne	fpundefinstr			@ ignore FP
		sub	r4, r5, #4
1:		ldrt	r0, [r4]			@ r0  = instruction
		adrsvc	al, r9, ret_from_exception	@ r9  = normal FP return
		adrsvc	al, lr, fpundefinstr		@ lr  = undefined instr return

/*
 * The out of line fixup for the ldrt above.
 */
		.section .fixup, "ax"
2:		mov	pc, r9
		.previous
		.section __ex_table,"a"
		.long	1b, 2b
		.previous

/*
 * r0 = instruction.
 *
 * Check whether the instruction is a co-processor instruction.
 * If yes, we need to call the relevant co-processor handler.
 *
 * Note that we don't do a full check here for the co-processor
 * instructions; all instructions with bit 27 set are well
 * defined.  The only instructions that should fault are the
 * co-processor instructions.  However, we have to watch out
 * for the ARM6/ARM7 SWI bug.
 *
 * Emulators may wish to make use of the instruction value we
 * prepared for them in r0.
 */
call_fpe:	enable_irq r10				@ Enable interrupts
		tst	r0, #0x08000000			@ only CDP/CPRT/LDC/STC have bit 27
#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
		and	r10, r0, #0x0f000000		@ mask out op-code bits
		teqne	r10, #0x0f000000		@ SWI (ARM6/7 bug)?
#endif
		moveq	pc, lr
do_fpe:		get_thread_info r10			@ get current thread
		ldr	r4, [r10, #TI_TASK]		@ get current task
		mov	r8, #1
		strb	r8, [r4, #TSK_USED_MATH]	@ set current->used_math
		ldr	r4, .LCfp
		add	r10, r10, #TI_FPSTATE		@ r10 = workspace
		ldr	pc, [r4]			@ Call FP module USR entry point
/*
 * The FP module is called with these registers set:
 *  r0  = instruction
 *  r5  = PC
 *  r9  = normal "successful" return address
 *  r10 = FP workspace
 *  lr  = unrecognised FP instruction return address
 */

fpundefinstr:	mov	r0, sp
		adrsvc	al, lr, ret_from_exception
		b	do_undefinstr

		.align	5
__pabt_usr:	sub	sp, sp, #S_FRAME_SIZE		@ Allocate frame size in one go
		stmia	sp, {r0 - r12}			@ Save r0 - r12
		ldr	r4, .LCabt
		add	r8, sp, #S_PC
		ldmia	r4, {r5 - r7}			@ Get USR pc, cpsr
		stmia	r8, {r5 - r7}			@ Save USR pc, cpsr, old_r0
		stmdb	r8, {sp, lr}^			@ Save sp_usr lr_usr
		alignment_trap r4, r7, __temp_abt
		zero_fp
		enable_irq r0				@ Enable interrupts
		mov	r0, r5				@ address (pc)
		mov	r1, sp				@ regs
		bl	do_PrefetchAbort		@ call abort handler
		/* fall through */
/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
		get_thread_info tsk
		mov	why, #0
		b	ret_to_user

		.data
ENTRY(fp_enter)
		.word	fpe_not_present
		.text
/*
 * Register switch for ARMv3 and ARMv4 processors
 * r0 = previous thread_info, r1 = next thread_info
 * previous and next are guaranteed not to be the same.
 */
ENTRY(__switch_to)
		add	ip, r1, #TI_CPU_SAVE
		ldr	r3, [r2, #TI_CPU_DOMAIN]!
		stmia	ip, {r4 - sl, fp, sp, lr}	@ Store most regs on stack
		mcr	p15, 0, r3, c3, c0, 0		@ Set domain register
		ldmib	r2, {r4 - sl, fp, sp, pc}	@ Load all regs saved previously

		__INIT
/*
 * Vector stubs.  NOTE that we only align 'vector_IRQ' to a cache line boundary,
 * and we rely on each stub being exactly 48 (1.5 cache lines) in size.  This
 * means that we only ever load two cache lines for this code, or one if we're
 * lucky.  We also copy this code to 0x200 so that we can use branches in the
 * vectors, rather than ldr's.
 */
		.align	5
__stubs_start:
/*
 * Interrupt dispatcher
 * Enter in IRQ mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
vector_IRQ:	@
		@ save mode specific registers
		@
		ldr	r13, .LCsirq
		sub	lr, lr, #4
		str	lr, [r13]			@ save lr_IRQ
		mrs	lr, spsr
		str	lr, [r13, #4]			@ save spsr_IRQ
		@
		@ now branch to the relevant MODE handling routine
		@
		mrs	r13, cpsr
		bic	r13, r13, #MODE_MASK
		orr	r13, r13, #MODE_SVC
		msr	spsr, r13			@ switch to SVC_32 mode

		and	lr, lr, #15
		ldr	lr, [pc, lr, lsl #2]
		movs	pc, lr				@ Changes mode and branches

.LCtab_irq:	.word	__irq_usr			@  0  (USR_26 / USR_32)
		.word	__irq_invalid			@  1  (FIQ_26 / FIQ_32)
		.word	__irq_invalid			@  2  (IRQ_26 / IRQ_32)
		.word	__irq_svc			@  3  (SVC_26 / SVC_32)
		.word	__irq_invalid			@  4
		.word	__irq_invalid			@  5
		.word	__irq_invalid			@  6
		.word	__irq_invalid			@  7
		.word	__irq_invalid			@  8
		.word	__irq_invalid			@  9
		.word	__irq_invalid			@  a
		.word	__irq_invalid			@  b
		.word	__irq_invalid			@  c
		.word	__irq_invalid			@  d
		.word	__irq_invalid			@  e
		.word	__irq_invalid			@  f

		.align	5

/*
 * Data abort dispatcher - dispatches it to the correct handler for the processor mode
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
vector_data:	@
		@ save mode specific registers
		@
		ldr	r13, .LCsabt
		sub	lr, lr, #8
		str	lr, [r13]
		mrs	lr, spsr
		str	lr, [r13, #4]
		@
		@ now branch to the relevant MODE handling routine
		@
		mrs	r13, cpsr
		bic	r13, r13, #MODE_MASK
		orr	r13, r13, #MODE_SVC
		msr	spsr, r13			@ switch to SVC_32 mode

		and	lr, lr, #15
		ldr	lr, [pc, lr, lsl #2]
		movs	pc, lr				@ Changes mode and branches

.LCtab_dabt:	.word	__dabt_usr			@  0  (USR_26 / USR_32)
		.word	__dabt_invalid			@  1  (FIQ_26 / FIQ_32)
		.word	__dabt_invalid			@  2  (IRQ_26 / IRQ_32)
		.word	__dabt_svc			@  3  (SVC_26 / SVC_32)
		.word	__dabt_invalid			@  4
		.word	__dabt_invalid			@  5
		.word	__dabt_invalid			@  6
		.word	__dabt_invalid			@  7
		.word	__dabt_invalid			@  8
		.word	__dabt_invalid			@  9
		.word	__dabt_invalid			@  a
		.word	__dabt_invalid			@  b
		.word	__dabt_invalid			@  c
		.word	__dabt_invalid			@  d
		.word	__dabt_invalid			@  e
		.word	__dabt_invalid			@  f

		.align	5

/*
 * Prefetch abort dispatcher - dispatches it to the correct handler for the processor mode
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
vector_prefetch:
		@
		@ save mode specific registers
		@
		ldr	r13, .LCsabt
		sub	lr, lr, #4
		str	lr, [r13]			@ save lr_ABT
		mrs	lr, spsr
		str	lr, [r13, #4]			@ save spsr_ABT
		@
		@ now branch to the relevant MODE handling routine
		@
		mrs	r13, cpsr
		bic	r13, r13, #MODE_MASK
		orr	r13, r13, #MODE_SVC
		msr	spsr, r13			@ switch to SVC_32 mode

		ands	lr, lr, #15
		ldr	lr, [pc, lr, lsl #2]
		movs	pc, lr

.LCtab_pabt:	.word	__pabt_usr			@  0 (USR_26 / USR_32)
		.word	__pabt_invalid			@  1 (FIQ_26 / FIQ_32)
		.word	__pabt_invalid			@  2 (IRQ_26 / IRQ_32)
		.word	__pabt_svc			@  3 (SVC_26 / SVC_32)
		.word	__pabt_invalid			@  4
		.word	__pabt_invalid			@  5
		.word	__pabt_invalid			@  6
		.word	__pabt_invalid			@  7
		.word	__pabt_invalid			@  8
		.word	__pabt_invalid			@  9
		.word	__pabt_invalid			@  a
		.word	__pabt_invalid			@  b
		.word	__pabt_invalid			@  c
		.word	__pabt_invalid			@  d
		.word	__pabt_invalid			@  e
		.word	__pabt_invalid			@  f

		.align	5

/*
 * Undef instr entry dispatcher - dispatches it to the correct handler for the processor mode
 * Enter in UND mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
vector_undefinstr:
		@
		@ save mode specific registers
		@
		ldr	r13, .LCsund
		str	lr, [r13]			@ save lr_UND
		mrs	lr, spsr
		str	lr, [r13, #4]			@ save spsr_UND
		@
		@ now branch to the relevant MODE handling routine
		@
		mrs	r13, cpsr
		bic	r13, r13, #MODE_MASK
		orr	r13, r13, #MODE_SVC
		msr	spsr, r13			@ switch to SVC_32 mode

		and	lr, lr, #15
		ldr	lr, [pc, lr, lsl #2]
		movs	pc, lr				@ Changes mode and branches

.LCtab_und:	.word	__und_usr			@  0 (USR_26 / USR_32)
		.word	__und_invalid			@  1 (FIQ_26 / FIQ_32)
		.word	__und_invalid			@  2 (IRQ_26 / IRQ_32)
		.word	__und_svc			@  3 (SVC_26 / SVC_32)
		.word	__und_invalid			@  4
		.word	__und_invalid			@  5
		.word	__und_invalid			@  6
		.word	__und_invalid			@  7
		.word	__und_invalid			@  8
		.word	__und_invalid			@  9
		.word	__und_invalid			@  a
		.word	__und_invalid			@  b
		.word	__und_invalid			@  c
		.word	__und_invalid			@  d
		.word	__und_invalid			@  e
		.word	__und_invalid			@  f

		.align	5

/*=============================================================================
 * Undefined FIQs
 *-----------------------------------------------------------------------------
 * Enter in FIQ mode, spsr = ANY CPSR, lr = ANY PC
 * MUST PRESERVE SVC SPSR, but need to switch to SVC mode to show our msg.
 * Basically to switch modes, we *HAVE* to clobber one register...  brain
 * damage alert!  I don't think that we can execute any code in here in any
 * other mode than FIQ...  Ok you can switch to another mode, but you can't
 * get out of that mode without clobbering one register.
 */
vector_FIQ:	disable_fiq
		subs	pc, lr, #4

/*=============================================================================
 * Address exception handler
 *-----------------------------------------------------------------------------
 * These aren't too critical.
 * (they're not supposed to happen, and won't happen in 32-bit data mode).
 */

vector_addrexcptn:
		b	vector_addrexcptn

/*
 * We group all the following data together to optimise
 * for CPUs with separate I & D caches.
 */
		.align	5

.LCvswi:	.word	vector_swi

.LCsirq:	.word	__temp_irq
.LCsund:	.word	__temp_und
.LCsabt:	.word	__temp_abt

__stubs_end:

		.equ	__real_stubs_start, .LCvectors + 0x200

.LCvectors:	swi	SYS_ERROR0
		b	__real_stubs_start + (vector_undefinstr - __stubs_start)
		ldr	pc, __real_stubs_start + (.LCvswi - __stubs_start)
		b	__real_stubs_start + (vector_prefetch - __stubs_start)
		b	__real_stubs_start + (vector_data - __stubs_start)
		b	__real_stubs_start + (vector_addrexcptn - __stubs_start)
		b	__real_stubs_start + (vector_IRQ - __stubs_start)
		b	__real_stubs_start + (vector_FIQ - __stubs_start)

ENTRY(__trap_init)
		stmfd	sp!, {r4 - r6, lr}

		adr	r1, .LCvectors			@ set up the vectors
		ldmia	r1, {r1, r2, r3, r4, r5, r6, ip, lr}
		stmia	r0, {r1, r2, r3, r4, r5, r6, ip, lr}

		add	r2, r0, #0x200
		adr	r0, __stubs_start		@ copy stubs to 0x200
		adr	r1, __stubs_end
1:		ldr	r3, [r0], #4
		str	r3, [r2], #4
		cmp	r0, r1
		blt	1b
		LOADREGS(fd, sp!, {r4 - r6, pc})

		.data

/*
 * Do not reorder these, and do not insert extra data between...
 */

__temp_irq:	.word	0				@ saved lr_irq
		.word	0				@ saved spsr_irq
		.word	-1				@ old_r0
__temp_und:	.word	0				@ Saved lr_und
		.word	0				@ Saved spsr_und
		.word	-1				@ old_r0
__temp_abt:	.word	0				@ Saved lr_abt
		.word	0				@ Saved spsr_abt
		.word	-1				@ old_r0

		.globl	cr_alignment
		.globl	cr_no_alignment
cr_alignment:
		.space	4
cr_no_alignment:
		.space	4
