#ifndef _ASM_HW_IRQ_H
#define _ASM_HW_IRQ_H

/*
 *	linux/include/asm/hw_irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	moved some of the old arch/i386/kernel/irq.h to here. VY
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#include <linux/config.h>
#include <linux/profile.h>
#include <asm/atomic.h>
#include <asm/irq.h>

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

extern int irq_vector[NR_IRQS];
#define IO_APIC_VECTOR(irq)	irq_vector[irq]

extern void (*interrupt[NR_IRQS])(void);

#ifdef CONFIG_SMP
extern asmlinkage void reschedule_interrupt(void);
extern asmlinkage void invalidate_interrupt(void);
extern asmlinkage void call_function_interrupt(void);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
extern asmlinkage void apic_timer_interrupt(void);
extern asmlinkage void error_interrupt(void);
extern asmlinkage void spurious_interrupt(void);
extern asmlinkage void thermal_interrupt(struct pt_regs);
#endif

extern void mask_irq(unsigned int irq);
extern void unmask_irq(unsigned int irq);
extern void disable_8259A_irq(unsigned int irq);
extern void enable_8259A_irq(unsigned int irq);
extern int i8259A_irq_pending(unsigned int irq);
extern void make_8259A_irq(unsigned int irq);
extern void init_8259A(int aeoi);
extern void FASTCALL(send_IPI_self(int vector));
extern void init_VISWS_APIC_irqs(void);
extern void setup_IO_APIC(void);
extern void disable_IO_APIC(void);
extern void print_IO_APIC(void);
extern int IO_APIC_get_PCI_irq_vector(int bus, int slot, int fn);
extern void send_IPI(int dest, int vector);
extern void setup_ioapic_dest(unsigned long mask);

extern unsigned long io_apic_irqs;

extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

extern char _stext, _etext;

#define IO_APIC_IRQ(x) (((x) >= 16) || ((1<<(x)) & io_apic_irqs))

/*
 * The profiling function is SMP safe. (nothing can mess
 * around with "current", and the profiling counters are
 * updated with atomic operations). This is especially
 * useful with a profiling multiplier != 1
 */
static inline void x86_do_profile(struct pt_regs * regs)
{
	unsigned long eip;
	extern unsigned long prof_cpu_mask;
 
	profile_hook(regs);
 
	if (user_mode(regs))
		return;
 
	if (!prof_buffer)
		return;

	eip = regs->eip;
 
	/*
	 * Only measure the CPUs specified by /proc/irq/prof_cpu_mask.
	 * (default is all CPUs.)
	 */
	if (!((1<<smp_processor_id()) & prof_cpu_mask))
		return;

	eip -= (unsigned long) &_stext;
	eip >>= prof_shift;
	/*
	 * Don't ignore out-of-bounds EIP values silently,
	 * put them into the last histogram slot, so if
	 * present, they will show up as a sharp peak.
	 */
	if (eip > prof_len-1)
		eip = prof_len-1;
	atomic_inc((atomic_t *)&prof_buffer[eip]);
}
 
#if defined(CONFIG_X86_IO_APIC) && defined(CONFIG_SMP)
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i)
{
	if (IO_APIC_IRQ(i))
		send_IPI_self(IO_APIC_VECTOR(i));
}
#else
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}
#endif

#endif /* _ASM_HW_IRQ_H */
