#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <linux/config.h>
#include <asm/irq.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

struct cpu_usage_stat {
	unsigned int user;
	unsigned int nice;
	unsigned int system;
	unsigned int idle;
	unsigned int iowait;
};

struct kernel_stat {
	struct cpu_usage_stat	cpustat;
#if !defined(CONFIG_ARCH_S390)
	unsigned int irqs[NR_IRQS];
#endif
};

DECLARE_PER_CPU(struct kernel_stat, kstat);

#define kstat_cpu(cpu)	per_cpu(kstat, cpu)
/* Must have preemption disabled for this to be meaningful. */
#define kstat_this_cpu	__get_cpu_var(kstat)

extern unsigned long nr_context_switches(void);

#if !defined(CONFIG_ARCH_S390)
/*
 * Number of interrupts per specific IRQ source, since bootup
 */
static inline int kstat_irqs(int irq)
{
	int i, sum=0;

	for (i = 0; i < NR_CPUS; i++)
		if (cpu_possible(i))
			sum += kstat_cpu(i).irqs[irq];

	return sum;
}
#endif

#endif /* _LINUX_KERNEL_STAT_H */
