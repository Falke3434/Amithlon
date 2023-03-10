/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 * Copyright (c) 2001 Patrick Mochel <mochel@osdl.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sysrq.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <linux/acpi.h>
#include <asm/uaccess.h>
#include <asm/acpi.h>
#include <asm/tlbflush.h>

static struct saved_context saved_context;
static void fix_processor_context(void);

extern void enable_sep_cpu(void *);

void save_processor_state(void)
{
	kernel_fpu_begin();

	/*
	 * descriptor tables
	 */
	asm volatile ("sgdt %0" : "=m" (saved_context.gdt_limit));
	asm volatile ("sidt %0" : "=m" (saved_context.idt_limit));
	asm volatile ("sldt %0" : "=m" (saved_context.ldt));
	asm volatile ("str %0"  : "=m" (saved_context.tr));

	/*
	 * segment registers
	 */
	asm volatile ("movw %%es, %0" : "=m" (saved_context.es));
	asm volatile ("movw %%fs, %0" : "=m" (saved_context.fs));
	asm volatile ("movw %%gs, %0" : "=m" (saved_context.gs));
	asm volatile ("movw %%ss, %0" : "=m" (saved_context.ss));

	/*
	 * control registers 
	 */
	asm volatile ("movl %%cr0, %0" : "=r" (saved_context.cr0));
	asm volatile ("movl %%cr2, %0" : "=r" (saved_context.cr2));
	asm volatile ("movl %%cr3, %0" : "=r" (saved_context.cr3));
	asm volatile ("movl %%cr4, %0" : "=r" (saved_context.cr4));
}

static void
do_fpu_end(void)
{
        /* restore FPU regs if necessary */
	/* Do it out of line so that gcc does not move cr0 load to some stupid place */
        kernel_fpu_end();
}

void restore_processor_state(void)
{

	/*
	 * control registers
	 */
	asm volatile ("movl %0, %%cr4" :: "r" (saved_context.cr4));
	asm volatile ("movl %0, %%cr3" :: "r" (saved_context.cr3));
	asm volatile ("movl %0, %%cr2" :: "r" (saved_context.cr2));
	asm volatile ("movl %0, %%cr0" :: "r" (saved_context.cr0));

	/*
	 * segment registers
	 */
	asm volatile ("movw %0, %%es" :: "r" (saved_context.es));
	asm volatile ("movw %0, %%fs" :: "r" (saved_context.fs));
	asm volatile ("movw %0, %%gs" :: "r" (saved_context.gs));
	asm volatile ("movw %0, %%ss" :: "r" (saved_context.ss));

	/*
	 * now restore the descriptor tables to their proper values
	 * ltr is done i fix_processor_context().
	 */
	asm volatile ("lgdt %0" :: "m" (saved_context.gdt_limit));
	asm volatile ("lidt %0" :: "m" (saved_context.idt_limit));
	asm volatile ("lldt %0" :: "m" (saved_context.ldt));

	/*
	 * sysenter MSRs
	 */
	if (boot_cpu_has(X86_FEATURE_SEP))
		enable_sep_cpu(NULL);

	fix_processor_context();
	do_fpu_end();
}

static void fix_processor_context(void)
{
	int cpu = smp_processor_id();
	struct tss_struct * t = init_tss + cpu;

	set_tss_desc(cpu,t);	/* This just modifies memory; should not be necessary. But... This is necessary, because 386 hardware has concept of busy TSS or some similar stupidity. */
        cpu_gdt_table[cpu][GDT_ENTRY_TSS].b &= 0xfffffdff;

	load_TR_desc();				/* This does ltr */
	load_LDT(&current->active_mm->context);	/* This does lldt */

	/*
	 * Now maybe reload the debug registers
	 */
	if (current->thread.debugreg[7]){
                loaddebug(&current->thread, 0);
                loaddebug(&current->thread, 1);
                loaddebug(&current->thread, 2);
                loaddebug(&current->thread, 3);
                /* no 4 and 5 */
                loaddebug(&current->thread, 6);
                loaddebug(&current->thread, 7);
	}

}

EXPORT_SYMBOL(save_processor_state);
EXPORT_SYMBOL(restore_processor_state);
