/* smp.h: PPC specific SMP stuff.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 */
#ifdef __KERNEL__
#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/errno.h>

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

struct cpuinfo_PPC {
	unsigned long loops_per_jiffy;
	unsigned long pvr;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};

extern struct cpuinfo_PPC cpu_data[];
extern unsigned long cpu_online_map;
extern unsigned long cpu_possible_map;
extern unsigned long smp_proc_in_lock[];
extern volatile unsigned long cpu_callin_map[];
extern int smp_tb_synchronized;

extern void smp_send_tlb_invalidate(int);
extern void smp_send_xmon_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);
extern void smp_local_timer_interrupt(struct pt_regs *);

#define NO_PROC_ID		0xFF            /* No processor magic marker */
#define PROC_CHANGE_PENALTY	20

#define smp_processor_id() (current_thread_info()->cpu)

#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))
#define cpu_possible(cpu) (cpu_possible_map & (1<<(cpu)))

extern inline unsigned int num_online_cpus(void)
{
	return hweight32(cpu_online_map);
}

extern inline unsigned int any_online_cpu(unsigned int mask)
{
	if (mask & cpu_online_map)
		return __ffs(mask & cpu_online_map);

	return NR_CPUS;
}

extern int __cpu_up(unsigned int cpu);

extern int smp_hw_index[];
#define hard_smp_processor_id() (smp_hw_index[smp_processor_id()])

struct klock_info_struct {
	unsigned long kernel_flag;
	unsigned char akp;
};

extern struct klock_info_struct klock_info;
#define KLOCK_HELD       0xffffffff
#define KLOCK_CLEAR      0x0

#endif /* __ASSEMBLY__ */

#else /* !(CONFIG_SMP) */

#endif /* !(CONFIG_SMP) */

#endif /* !(_PPC_SMP_H) */
#endif /* __KERNEL__ */
