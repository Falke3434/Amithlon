/*
 * BK Id: SCCS/s.m8260_setup.c 1.26 09/22/01 11:33:22 trini
 */
/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx and 8260 by Dan.
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>
#include <linux/ide.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/mpc8260.h>
#include <asm/immap_8260.h>
#include <asm/machdep.h>

#include <asm/time.h>
#include "ppc8260_pic.h"

static int m8260_set_rtc_time(unsigned long time);
unsigned long m8260_get_rtc_time(void);
void m8260_calibrate_decr(void);

extern unsigned long loops_per_jiffy;

unsigned char __res[sizeof(bd_t)];

extern char saved_command_line[256];

extern unsigned long find_available_memory(void);
extern void m8260_cpm_reset(void);

void __init
m8260_setup_arch(void)
{
	/* Reset the Communication Processor Module.
	*/
	m8260_cpm_reset();
}

void
abort(void)
{
#ifdef CONFIG_XMON
	extern void xmon(void *);
	xmon(0);
#endif
	machine_restart(NULL);
}

/* The decrementer counts at the system (internal) clock frequency
 * divided by four.
 */
void __init m8260_calibrate_decr(void)
{
	bd_t	*binfo = (bd_t *)__res;
	int freq, divisor;

	freq = binfo->bi_busfreq;
        divisor = 4;
        tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

/* The 8260 has an internal 1-second timer update register that
 * we should use for this purpose.
 */
static uint rtc_time;

static int
m8260_set_rtc_time(unsigned long time)
{
	rtc_time = time;
	return(0);
}

unsigned long
m8260_get_rtc_time(void)
{

	/* Get time from the RTC.
	*/
	return((unsigned long)rtc_time);
}

void
m8260_restart(char *cmd)
{
	extern void m8260_gorom(bd_t *bi, uint addr);
	uint	startaddr;

	/* Most boot roms have a warmstart as the second instruction
	 * of the reset vector.  If that doesn't work for you, change this
	 * or the reboot program to send a proper address.
	 */
	startaddr = 0xff000104;

	if (cmd != NULL) {
		if (!strncmp(cmd, "startaddr=", 10))
			startaddr = simple_strtoul(&cmd[10], NULL, 0);
	}

	m8260_gorom((unsigned int)__pa(__res), startaddr);
}

void
m8260_power_off(void)
{
   m8260_restart(NULL);
}

void
m8260_halt(void)
{
   m8260_restart(NULL);
}


int m8260_setup_residual(char *buffer)
{
        int     len = 0;
	bd_t	*bp;

	bp = (bd_t *)__res;
			
	len += sprintf(len+buffer,"core clock\t: %d MHz\n"
		       "CPM  clock\t: %d MHz\n"
		       "bus  clock\t: %d MHz\n",
		       bp->bi_intfreq / 1000000,
		       bp->bi_cpmfreq / 1000000,
		       bp->bi_busfreq / 1000000);

	return len;
}

/* Initialize the internal interrupt controller.  The number of
 * interrupts supported can vary with the processor type, and the
 * 8260 family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
void __init
m8260_init_IRQ(void)
{
	int i;
	void cpm_interrupt_init(void);

#if 0
        ppc8260_pic.irq_offset = 0;
#endif
        for ( i = 0 ; i < NR_SIU_INTS ; i++ )
                irq_desc[i].handler = &ppc8260_pic;
	
	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	immr->im_intctl.ic_sicr = 0;
	immr->im_intctl.ic_siprr = 0x05309770;
	immr->im_intctl.ic_scprrh = 0x05309770;
	immr->im_intctl.ic_scprrl = 0x05309770;

}

/*
 * Same hack as 8xx
 */
unsigned long __init m8260_find_end_of_memory(void)
{
	bd_t	*binfo;
	extern unsigned char __res[];
	
	binfo = (bd_t *)__res;

	return binfo->bi_memsize;
}

/* Map the IMMR, plus anything else we can cover
 * in that upper space according to the memory controller
 * chip select mapping.  Grab another bunch of space
 * below that for stuff we can't cover in the upper.
 */
static void __init
m8260_map_io(void)
{
	io_block_mapping(0xf0000000, 0xf0000000, 0x10000000, _PAGE_IO);
	io_block_mapping(0xe0000000, 0xe0000000, 0x10000000, _PAGE_IO);
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{

	if ( r3 )
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
	
#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 )
	{
		
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

	ppc_md.setup_arch     = m8260_setup_arch;
	ppc_md.setup_residual = m8260_setup_residual;
	ppc_md.get_cpuinfo    = NULL;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = m8260_init_IRQ;
	ppc_md.get_irq	      = m8260_get_irq;
	ppc_md.init           = NULL;

	ppc_md.restart        = m8260_restart;
	ppc_md.power_off      = m8260_power_off;
	ppc_md.halt           = m8260_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = m8260_set_rtc_time;
	ppc_md.get_rtc_time   = m8260_get_rtc_time;
	ppc_md.calibrate_decr = m8260_calibrate_decr;

	ppc_md.find_end_of_memory = m8260_find_end_of_memory;
	ppc_md.setup_io_mappings = m8260_map_io;

	ppc_md.kbd_setkeycode    = NULL;
	ppc_md.kbd_getkeycode    = NULL;
	ppc_md.kbd_translate     = NULL;
	ppc_md.kbd_unexpected_up = NULL;
	ppc_md.kbd_leds          = NULL;
	ppc_md.kbd_init_hw       = NULL;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = NULL;
#endif
}

/* Mainly for ksyms.
*/
int
request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flag, const char *naem, void *dev)
{
	panic("request IRQ\n");
}
