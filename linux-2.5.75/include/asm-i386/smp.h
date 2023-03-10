#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#endif
#endif

#define BAD_APICID 0xFFu
#ifdef CONFIG_SMP
#ifndef __ASSEMBLY__

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern unsigned long phys_cpu_present_map;
extern unsigned long cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern int smp_num_siblings;
extern int cpu_sibling_map[];

extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);

#define MAX_APICID 256

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */
#define smp_processor_id() (current_thread_info()->cpu)

extern volatile unsigned long cpu_callout_map;

#define cpu_possible(cpu) (cpu_callout_map & (1<<(cpu)))
#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))

#define for_each_cpu(cpu, mask) \
	for(mask = cpu_online_map; \
	    cpu = __ffs(mask), mask != 0; \
	    mask &= ~(1<<cpu))

extern inline unsigned int num_online_cpus(void)
{
	return hweight32(cpu_online_map);
}

/* We don't mark CPUs online until __cpu_up(), so we need another measure */
static inline int num_booting_cpus(void)
{
	return hweight32(cpu_callout_map);
}

extern void map_cpu_to_logical_apicid(void);
extern void unmap_cpu_to_logical_apicid(int cpu);

extern inline unsigned int any_online_cpu(unsigned int mask)
{
	if (mask & cpu_online_map)
		return __ffs(mask & cpu_online_map);

	return NR_CPUS;
}
#ifdef CONFIG_X86_LOCAL_APIC

#ifdef APIC_DEFINITION
extern int hard_smp_processor_id(void);
#else
#include <mach_apicdef.h>
static inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned long *)(APIC_BASE+APIC_ID));
}
#endif

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(unsigned long *)(APIC_BASE+APIC_LDR));
}

#endif
#endif /* !__ASSEMBLY__ */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif
#endif
