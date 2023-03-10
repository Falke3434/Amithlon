/*
 * Defines for the TJSYS JMR-TX3927/JMI-3927IO2/JMY-1394IF.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Toshiba Corporation
 */
#ifndef __ASM_TX3927_JMR3927_H
#define __ASM_TX3927_JMR3927_H

#include <asm/jmr3927/tx3927.h>
#include <asm/addrspace.h>
#include <asm/jmr3927/irq.h>
#ifndef __ASSEMBLY__
#include <asm/system.h>
#endif

/* CS */
#define JMR3927_ROMCE0	0x1fc00000	/* 4M */
#define JMR3927_ROMCE1	0x1e000000	/* 4M */
#define JMR3927_ROMCE2	0x14000000	/* 16M */
#define JMR3927_ROMCE3	0x10000000	/* 64M */
#define JMR3927_ROMCE5	0x1d000000	/* 4M */
#define JMR3927_SDCS0	0x00000000	/* 32M */
#define JMR3927_SDCS1	0x02000000	/* 32M */
/* PCI Direct Mappings */

#define JMR3927_PCIMEM	0x08000000
#define JMR3927_PCIMEM_SIZE	0x08000000	/* 128M */
#define JMR3927_PCIIO	0x15000000
#define JMR3927_PCIIO_SIZE	0x01000000	/* 16M */

#define JMR3927_SDRAM_SIZE	0x02000000	/* 32M */
#define JMR3927_PORT_BASE	KSEG1

/* select indirect initiator access per errata */
#define JMR3927_INIT_INDIRECT_PCI
#define PCI_ISTAT_IDICC           0x1000
#define PCI_IPCIBE_IBE_LONG       0
#define PCI_IPCIBE_ICMD_IOREAD    2
#define PCI_IPCIBE_ICMD_IOWRITE   3
#define PCI_IPCIBE_ICMD_MEMREAD   6
#define PCI_IPCIBE_ICMD_MEMWRITE  7
#define PCI_IPCIBE_ICMD_SHIFT     4

/* Address map (virtual address) */
#define JMR3927_ROM0_BASE	(KSEG1 + JMR3927_ROMCE0)
#define JMR3927_ROM1_BASE	(KSEG1 + JMR3927_ROMCE1)
#define JMR3927_IOC_BASE	(KSEG1 + JMR3927_ROMCE2)
#define JMR3927_IOB_BASE	(KSEG1 + JMR3927_ROMCE3)
#define JMR3927_ISAMEM_BASE	(JMR3927_IOB_BASE)
#define JMR3927_ISAIO_BASE	(JMR3927_IOB_BASE + 0x01000000)
#define JMR3927_ISAC_BASE	(JMR3927_IOB_BASE + 0x02000000)
#define JMR3927_LCDVGA_REG_BASE	(JMR3927_IOB_BASE + 0x03000000)
#define JMR3927_LCDVGA_MEM_BASE	(JMR3927_IOB_BASE + 0x03800000)
#define JMR3927_JMY1394_BASE	(KSEG1 + JMR3927_ROMCE5)
#define JMR3927_PREMIER3_BASE	(JMR3927_JMY1394_BASE + 0x00100000)
#define JMR3927_PCIMEM_BASE	(KSEG1 + JMR3927_PCIMEM)
#define JMR3927_PCIIO_BASE	(KSEG1 + JMR3927_PCIIO)

#define JMR3927_IOC_REV_ADDR	(JMR3927_IOC_BASE + 0x00000000)
#define JMR3927_IOC_NVRAMB_ADDR	(JMR3927_IOC_BASE + 0x00010000)
#define JMR3927_IOC_LED_ADDR	(JMR3927_IOC_BASE + 0x00020000)
#define JMR3927_IOC_DIPSW_ADDR	(JMR3927_IOC_BASE + 0x00030000)
#define JMR3927_IOC_BREV_ADDR	(JMR3927_IOC_BASE + 0x00040000)
#define JMR3927_IOC_DTR_ADDR	(JMR3927_IOC_BASE + 0x00050000)
#define JMR3927_IOC_INTS1_ADDR	(JMR3927_IOC_BASE + 0x00080000)
#define JMR3927_IOC_INTS2_ADDR	(JMR3927_IOC_BASE + 0x00090000)
#define JMR3927_IOC_INTM_ADDR	(JMR3927_IOC_BASE + 0x000a0000)
#define JMR3927_IOC_INTP_ADDR	(JMR3927_IOC_BASE + 0x000b0000)
#define JMR3927_IOC_RESET_ADDR	(JMR3927_IOC_BASE + 0x000f0000)

#define JMR3927_ISAC_REV_ADDR	(JMR3927_ISAC_BASE + 0x00000000)
#define JMR3927_ISAC_EINTS_ADDR	(JMR3927_ISAC_BASE + 0x00200000)
#define JMR3927_ISAC_EINTM_ADDR	(JMR3927_ISAC_BASE + 0x00300000)
#define JMR3927_ISAC_NMI_ADDR	(JMR3927_ISAC_BASE + 0x00400000)
#define JMR3927_ISAC_LED_ADDR	(JMR3927_ISAC_BASE + 0x00500000)
#define JMR3927_ISAC_INTP_ADDR	(JMR3927_ISAC_BASE + 0x00800000)
#define JMR3927_ISAC_INTS1_ADDR	(JMR3927_ISAC_BASE + 0x00900000)
#define JMR3927_ISAC_INTS2_ADDR	(JMR3927_ISAC_BASE + 0x00a00000)
#define JMR3927_ISAC_INTM_ADDR	(JMR3927_ISAC_BASE + 0x00b00000)

/* Flash ROM */
#define JMR3927_FLASH_BASE	(JMR3927_ROM0_BASE)
#define JMR3927_FLASH_SIZE	0x00400000

/* bits for IOC_REV/IOC_BREV/ISAC_REV (high byte) */
#define JMR3927_IDT_MASK	0xfc
#define JMR3927_REV_MASK	0x03
#define JMR3927_IOC_IDT		0xe0
#define JMR3927_ISAC_IDT	0x20

/* bits for IOC_INTS1/IOC_INTS2/IOC_INTM/IOC_INTP (high byte) */
#define JMR3927_IOC_INTB_PCIA	0
#define JMR3927_IOC_INTB_PCIB	1
#define JMR3927_IOC_INTB_PCIC	2
#define JMR3927_IOC_INTB_PCID	3
#define JMR3927_IOC_INTB_MODEM	4
#define JMR3927_IOC_INTB_INT6	5
#define JMR3927_IOC_INTB_INT7	6
#define JMR3927_IOC_INTB_SOFT	7
#define JMR3927_IOC_INTF_PCIA	(1 << JMR3927_IOC_INTF_PCIA)
#define JMR3927_IOC_INTF_PCIB	(1 << JMR3927_IOC_INTB_PCIB)
#define JMR3927_IOC_INTF_PCIC	(1 << JMR3927_IOC_INTB_PCIC)
#define JMR3927_IOC_INTF_PCID	(1 << JMR3927_IOC_INTB_PCID)
#define JMR3927_IOC_INTF_MODEM	(1 << JMR3927_IOC_INTB_MODEM)
#define JMR3927_IOC_INTF_INT6	(1 << JMR3927_IOC_INTB_INT6)
#define JMR3927_IOC_INTF_INT7	(1 << JMR3927_IOC_INTB_INT7)
#define JMR3927_IOC_INTF_SOFT	(1 << JMR3927_IOC_INTB_SOFT)

/* bits for IOC_RESET (high byte) */
#define JMR3927_IOC_RESET_CPU	1
#define JMR3927_IOC_RESET_PCI	2

/* bits for ISAC_EINTS/ISAC_EINTM (high byte) */
#define JMR3927_ISAC_EINTB_IOCHK	2
#define JMR3927_ISAC_EINTB_BWTH	4
#define JMR3927_ISAC_EINTF_IOCHK	(1 << JMR3927_ISAC_EINTB_IOCHK)
#define JMR3927_ISAC_EINTF_BWTH	(1 << JMR3927_ISAC_EINTB_BWTH)

/* bits for ISAC_LED (high byte) */
#define JMR3927_ISAC_LED_ISALED	0x01
#define JMR3927_ISAC_LED_USRLED	0x02

/* bits for ISAC_INTS/ISAC_INTM/ISAC_INTP (high byte) */
#define JMR3927_ISAC_INTB_IRQ5	0
#define JMR3927_ISAC_INTB_IRQKB	1
#define JMR3927_ISAC_INTB_IRQMOUSE	2
#define JMR3927_ISAC_INTB_IRQ4	3
#define JMR3927_ISAC_INTB_IRQ12	4
#define JMR3927_ISAC_INTB_IRQ3	5
#define JMR3927_ISAC_INTB_IRQ10	6
#define JMR3927_ISAC_INTB_ISAER	7
#define JMR3927_ISAC_INTF_IRQ5	(1 << JMR3927_ISAC_INTB_IRQ5)
#define JMR3927_ISAC_INTF_IRQKB	(1 << JMR3927_ISAC_INTB_IRQKB)
#define JMR3927_ISAC_INTF_IRQMOUSE	(1 << JMR3927_ISAC_INTB_IRQMOUSE)
#define JMR3927_ISAC_INTF_IRQ4	(1 << JMR3927_ISAC_INTB_IRQ4)
#define JMR3927_ISAC_INTF_IRQ12	(1 << JMR3927_ISAC_INTB_IRQ12)
#define JMR3927_ISAC_INTF_IRQ3	(1 << JMR3927_ISAC_INTB_IRQ3)
#define JMR3927_ISAC_INTF_IRQ10	(1 << JMR3927_ISAC_INTB_IRQ10)
#define JMR3927_ISAC_INTF_ISAER	(1 << JMR3927_ISAC_INTB_ISAER)

#ifndef __ASSEMBLY__

#if 0
#define jmr3927_ioc_reg_out(d, a)	((*(volatile unsigned short *)(a)) = (d) << 8)
#define jmr3927_ioc_reg_in(a)		(((*(volatile unsigned short *)(a)) >> 8) & 0xff)
#else
#if defined(__BIG_ENDIAN)
#define jmr3927_ioc_reg_out(d, a)	((*(volatile unsigned char *)(a)) = (d))
#define jmr3927_ioc_reg_in(a)		(*(volatile unsigned char *)(a))
#elif defined(__LITTLE_ENDIAN)
#define jmr3927_ioc_reg_out(d, a)	((*(volatile unsigned char *)((a)^1)) = (d))
#define jmr3927_ioc_reg_in(a)		(*(volatile unsigned char *)((a)^1))
#else
#error "No Endian"
#endif
#endif
#define jmr3927_isac_reg_out(d, a)	((*(volatile unsigned char *)(a)) = (d))
#define jmr3927_isac_reg_in(a)		(*(volatile unsigned char *)(a))

extern inline int jmr3927_have_isac(void)
{
	unsigned char idt;
	unsigned long flags;
	unsigned long romcr3;

	local_irq_save(flags);
	romcr3 = tx3927_romcptr->cr[3];
	tx3927_romcptr->cr[3] &= 0xffffefff;	/* do not wait infinitely */
	idt = jmr3927_isac_reg_in(JMR3927_ISAC_REV_ADDR) & JMR3927_IDT_MASK;
	tx3927_romcptr->cr[3] = romcr3;
	local_irq_restore(flags);

	return idt == JMR3927_ISAC_IDT;
}
#define jmr3927_have_nvram() \
	((jmr3927_ioc_reg_in(JMR3927_IOC_REV_ADDR) & JMR3927_IDT_MASK) == JMR3927_IOC_IDT)

/* NVRAM macro */
#define jmr3927_nvram_in(ofs) \
	jmr3927_ioc_reg_in(JMR3927_IOC_NVRAMB_ADDR + ((ofs) << 1))
#define jmr3927_nvram_out(d, ofs) \
	jmr3927_ioc_reg_out(d, JMR3927_IOC_NVRAMB_ADDR + ((ofs) << 1))

/* LED macro */
#define jmr3927_led_set(n/*0-16*/)	jmr3927_ioc_reg_out(~(n), JMR3927_IOC_LED_ADDR)
#define jmr3927_io_led_set(n/*0-3*/)	jmr3927_isac_reg_out((n), JMR3927_ISAC_LED_ADDR)

#define jmr3927_led_and_set(n/*0-16*/)	jmr3927_ioc_reg_out((~(n)) & jmr3927_ioc_reg_in(JMR3927_IOC_LED_ADDR), JMR3927_IOC_LED_ADDR)

/* DIPSW4 macro */
#define jmr3927_dipsw1()	((tx3927_pioptr->din & (1 << 11)) == 0)
#define jmr3927_dipsw2()	((tx3927_pioptr->din & (1 << 10)) == 0)
#define jmr3927_dipsw3()	((jmr3927_ioc_reg_in(JMR3927_IOC_DIPSW_ADDR) & 2) == 0)
#define jmr3927_dipsw4()	((jmr3927_ioc_reg_in(JMR3927_IOC_DIPSW_ADDR) & 1) == 0)
#define jmr3927_io_dipsw()	(jmr3927_isac_reg_in(JMR3927_ISAC_LED_ADDR) >> 4)


#endif /* !__ASSEMBLY__ */

/*
 * UART defines for serial.h
 */

/* use Pre-scaler T0 (1/2) */
#define JMR3927_BASE_BAUD (JMR3927_IMCLK / 2 / 16)

#define UART0_ADDR   0xfffef300
#define UART1_ADDR   0xfffef400
#define UART0_INT    JMR3927_IRQ_IRC_SIO0
#define UART1_INT    JMR3927_IRQ_IRC_SIO1
#define UART0_FLAGS  ASYNC_BOOT_AUTOCONF
#define UART1_FLAGS  0

/*
 * IRQ mappings
 */

/* These are the virtual IRQ numbers, we divide all IRQ's into
 * 'spaces', the 'space' determines where and how to enable/disable
 * that particular IRQ on an JMR machine.  Add new 'spaces' as new
 * IRQ hardware is supported.
 */
#define JMR3927_NR_IRQ_IRC	16	/* On-Chip IRC */
#define JMR3927_NR_IRQ_IOC	8	/* PCI/MODEM/INT[6:7] */
#define JMR3927_NR_IRQ_ISAC	8	/* ISA */


#define JMR3927_IRQ_IRC	NR_ISA_IRQS
#define JMR3927_IRQ_IOC	(JMR3927_IRQ_IRC + JMR3927_NR_IRQ_IRC)
#define JMR3927_IRQ_ISAC	(JMR3927_IRQ_IOC + JMR3927_NR_IRQ_IOC)
#define JMR3927_IRQ_END	(JMR3927_IRQ_ISAC + JMR3927_NR_IRQ_ISAC)
#define JMR3927_IRQ_IS_IRC(irq)	(JMR3927_IRQ_IRC <= (irq) && (irq) < JMR3927_IRQ_IOC)
#define JMR3927_IRQ_IS_IOC(irq)		(JMR3927_IRQ_IOC <= (irq) && (irq) < JMR3927_IRQ_ISAC)
#define JMR3927_IRQ_IS_ISAC(irq)	(JMR3927_IRQ_ISAC <= (irq) && (irq) < JMR3927_IRQ_END)

#define JMR3927_IRQ_IRC_INT0	(JMR3927_IRQ_IRC + TX3927_IR_INT0)
#define JMR3927_IRQ_IRC_INT1	(JMR3927_IRQ_IRC + TX3927_IR_INT1)
#define JMR3927_IRQ_IRC_INT2	(JMR3927_IRQ_IRC + TX3927_IR_INT2)
#define JMR3927_IRQ_IRC_INT3	(JMR3927_IRQ_IRC + TX3927_IR_INT3)
#define JMR3927_IRQ_IRC_INT4	(JMR3927_IRQ_IRC + TX3927_IR_INT4)
#define JMR3927_IRQ_IRC_INT5	(JMR3927_IRQ_IRC + TX3927_IR_INT5)
#define JMR3927_IRQ_IRC_SIO0	(JMR3927_IRQ_IRC + TX3927_IR_SIO0)
#define JMR3927_IRQ_IRC_SIO1	(JMR3927_IRQ_IRC + TX3927_IR_SIO1)
#define JMR3927_IRQ_IRC_SIO(ch)	(JMR3927_IRQ_IRC + TX3927_IR_SIO(ch))
#define JMR3927_IRQ_IRC_DMA	(JMR3927_IRQ_IRC + TX3927_IR_DMA)
#define JMR3927_IRQ_IRC_PIO	(JMR3927_IRQ_IRC + TX3927_IR_PIO)
#define JMR3927_IRQ_IRC_PCI	(JMR3927_IRQ_IRC + TX3927_IR_PCI)
#define JMR3927_IRQ_IRC_TMR0	(JMR3927_IRQ_IRC + TX3927_IR_TMR0)
#define JMR3927_IRQ_IRC_TMR1	(JMR3927_IRQ_IRC + TX3927_IR_TMR1)
#define JMR3927_IRQ_IRC_TMR2	(JMR3927_IRQ_IRC + TX3927_IR_TMR2)
#define JMR3927_IRQ_IOC_PCIA	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIA)
#define JMR3927_IRQ_IOC_PCIB	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIB)
#define JMR3927_IRQ_IOC_PCIC	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCIC)
#define JMR3927_IRQ_IOC_PCID	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_PCID)
#define JMR3927_IRQ_IOC_MODEM	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_MODEM)
#define JMR3927_IRQ_IOC_INT6	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_INT6)
#define JMR3927_IRQ_IOC_INT7	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_INT7)
#define JMR3927_IRQ_IOC_SOFT	(JMR3927_IRQ_IOC + JMR3927_IOC_INTB_SOFT)
#define JMR3927_IRQ_ISAC_IRQ5	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQ5)
#define JMR3927_IRQ_ISAC_IRQKB	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQKB)
#define JMR3927_IRQ_ISAC_IRQMOUSE	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQMOUSE)
#define JMR3927_IRQ_ISAC_IRQ4	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQ4)
#define JMR3927_IRQ_ISAC_IRQ12	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQ12)
#define JMR3927_IRQ_ISAC_IRQ3	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQ3)
#define JMR3927_IRQ_ISAC_IRQ10	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_IRQ10)
#define JMR3927_IRQ_ISAC_ISAER	(JMR3927_IRQ_ISAC + JMR3927_ISAC_INTB_ISAER)

#if 0	/* auto detect */
/* RTL8019AS 10M Ether (JMI-3927IO2:JPW2:1-2 Short) */
#define JMR3927_IRQ_ETHER1	JMR3927_IRQ_IRC_INT0
#endif
/* IOC (PCI, MODEM) */
#define JMR3927_IRQ_IOCINT	JMR3927_IRQ_IRC_INT1
/* ISAC (ISA, PCMCIA, KEYBOARD, MOUSE) */
#define JMR3927_IRQ_ISACINT	JMR3927_IRQ_IRC_INT2
/* TC35815 100M Ether (JMR-TX3912:JPW4:2-3 Short) */
#define JMR3927_IRQ_ETHER0	JMR3927_IRQ_IRC_INT3
/* Clock Tick (10ms) */
#define JMR3927_IRQ_TICK	JMR3927_IRQ_IRC_TMR0
#define JMR3927_IRQ_IDE		JMR3927_IRQ_ISAC_IRQ12

/* IEEE1394 (Note that this may conflicts with RTL8019AS 10M Ether...) */
#define JMR3927_IRQ_PREMIER3	JMR3927_IRQ_IRC_INT0

/* I/O Ports */
/* RTL8019AS 10M Ether */
#define JMR3927_ETHER1_PORT	(JMR3927_ISAIO_BASE - JMR3927_PORT_BASE + 0x280)
#define JMR3927_KBD_PORT	(JMR3927_ISAIO_BASE - JMR3927_PORT_BASE + 0x00800060)
#define JMR3927_IDE_PORT	(JMR3927_ISAIO_BASE - JMR3927_PORT_BASE + 0x001001f0)

/* Clocks */
#define JMR3927_CORECLK	132710400	/* 132.7MHz */
#define JMR3927_GBUSCLK	(JMR3927_CORECLK / 2)	/* 66.35MHz */
#define JMR3927_IMCLK	(JMR3927_CORECLK / 4)	/* 33.17MHz */

#define jmr3927_tmrptr		tx3927_tmrptr(0)	/* TMR0 */


/*
 * TX3927 Pin Configuration:
 *
 *	PCFG bits		Avail			Dead
 *	SELSIO[1:0]:11		RXD[1:0], TXD[1:0]	PIO[6:3]
 *	SELSIOC[0]:1		CTS[0], RTS[0]		INT[5:4]
 *	SELSIOC[1]:0,SELDSF:0,	GSDAO[0],GPCST[3]	CTS[1], RTS[1],DSF,
 *	  GDBGE*					  PIO[2:1]
 *	SELDMA[2]:1		DMAREQ[2],DMAACK[2]	PIO[13:12]
 *	SELTMR[2:0]:000					TIMER[1:0]
 *	SELCS:0,SELDMA[1]:0	PIO[11;10]		SDCS_CE[7:6],
 *							  DMAREQ[1],DMAACK[1]
 *	SELDMA[0]:1		DMAREQ[0],DMAACK[0]	PIO[9:8]
 *	SELDMA[3]:1		DMAREQ[3],DMAACK[3]	PIO[15:14]
 *	SELDONE:1		DMADONE			PIO[7]
 *
 * Usable pins are:
 *	RXD[1;0],TXD[1:0],CTS[0],RTS[0],
 *	DMAREQ[0,2,3],DMAACK[0,2,3],DMADONE,PIO[0,10,11]
 *	INT[3:0]
 */

#endif /* __ASM_TX3927_JMR3927_H */
