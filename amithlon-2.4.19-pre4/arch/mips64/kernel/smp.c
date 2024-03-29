/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001 Kanoj Sarcar
 * Copyright (C) 2000, 2001 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/cache.h>

#include <asm/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/softirq.h>
#include <asm/mmu_context.h>
#include <asm/irq.h>

/* The 'big kernel lock' */
spinlock_t kernel_flag __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
int smp_threads_ready;	/* Not used */
atomic_t smp_commenced = ATOMIC_INIT(0);
struct cpuinfo_mips cpu_data[NR_CPUS];
void (*volatile smp_cpu0_finalize)(void);

// static atomic_t cpus_booted = ATOMIC_INIT(0);
atomic_t cpus_booted = ATOMIC_INIT(0);

int smp_num_cpus = 1;			/* Number that came online.  */
cpumask_t cpu_online_map;		/* Bitmask of currently online CPUs */
int __cpu_number_map[NR_CPUS];
int __cpu_logical_map[NR_CPUS];
cycles_t cacheflush_time;

// static void smp_tune_scheduling (void)
void smp_tune_scheduling (void)
{
}

void __init smp_callin(void)
{
#if 0
	calibrate_delay();
	smp_store_cpu_info(cpuid);
#endif
}

#ifndef CONFIG_SGI_IP27
/*
 * Hook for doing final board-specific setup after the generic smp setup
 * is done
 */
asmlinkage void start_secondary(void)
{
	unsigned int cpu = smp_processor_id();

	prom_init_secondary();
	per_cpu_trap_init();

	/*
	 * XXX parity protection should be folded in here when it's converted
	 * to an option instead of something based on .cputype
	 */
	pgd_current[cpu] = init_mm.pgd;
	cpu_data[cpu].udelay_val = loops_per_jiffy;
	prom_smp_finish();
	printk("Slave cpu booted successfully\n");
	CPUMASK_SETB(cpu_online_map, cpu);
	atomic_inc(&cpus_booted);
	cpu_idle();
}
#endif /* CONFIG_SGI_IP27 */

void __init smp_commence(void)
{
	wmb();
	atomic_set(&smp_commenced, 1);
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
void smp_send_reschedule(int cpu)
{
	core_send_ipi(cpu, SMP_RESCHEDULE_YOURSELF);
}

/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

static spinlock_t call_lock = SPIN_LOCK_UNLOCKED;

struct call_data_struct *call_data;

/*
 * Run a function on all other CPUs.
 *  <func>      The function to run. This must be fast and non-blocking.
 *  <info>      An arbitrary pointer to pass to the function.
 *  <retry>     If true, keep retrying until ready.
 *  <wait>      If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func>
 * or are or have executed.
 */

int smp_call_function (void (*func) (void *info), void *info, int retry, 
								int wait)
{
	struct call_data_struct data;
	int i, cpus = smp_num_cpus - 1;
	int cpu = smp_processor_id();

	if (!cpus)
		return 0;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&call_lock);
	call_data = &data;

	/* Send a message to all other CPUs and wait for them to respond */
	for (i = 0; i < smp_num_cpus; i++)
		if (i != cpu)
			core_send_ipi(i, SMP_CALL_FUNCTION);

	/* Wait for response */
	/* FIXME: lock-up detection, backtrace on lock-up */
	while (atomic_read(&data.started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	spin_unlock_bh(&call_lock);

	return 0;
}

void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;
	int cpu = smp_processor_id();

	irq_enter(cpu, 0);	/* XXX choose an irq number? */
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function.
	 */
	mb();
	atomic_inc(&call_data->started);

	/*
	 * At this point the info structure may be out of scope unless wait==1.
	 */
	(*func)(info);
	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
	irq_exit(cpu, 0);	/* XXX choose an irq number? */
}

static void stop_this_cpu(void *dummy)
{
	int cpu = smp_processor_id();
	if (cpu)
		for (;;);		/* XXX Use halt like i386 */

	/* XXXKW this isn't quite there yet */
	while (!smp_cpu0_finalize) ;
	smp_cpu0_finalize();
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
	smp_num_cpus = 1;
}

static void flush_tlb_all_ipi(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	smp_call_function(flush_tlb_all_ipi, 0, 1, 1);
	local_flush_tlb_all();
}

static void flush_tlb_mm_ipi(void *mm)
{
	local_flush_tlb_mm((struct mm_struct *)mm);
}

/*
 * The following tlb flush calls are invoked when old translations are 
 * being torn down, or pte attributes are changing. For single threaded
 * address spaces, a new context is obtained on the current cpu, and tlb
 * context on other cpus are invalidated to force a new context allocation
 * at switch_mm time, should the mm ever be used on other cpus. For 
 * multithreaded address spaces, intercpu interrupts have to be sent.
 * Another case where intercpu interrupts are required is when the target
 * mm might be active on another cpu (eg debuggers doing the flushes on
 * behalf of debugees, kswapd stealing pages from another process etc).
 * Kanoj 07/00.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		smp_call_function(flush_tlb_mm_ipi, (void *)mm, 1, 1);
	} else {
		int i;
		for (i = 0; i < smp_num_cpus; i++)
			if (smp_processor_id() != i)
				CPU_CONTEXT(i, mm) = 0;
	}
	local_flush_tlb_mm(mm);
}

struct flush_tlb_data {
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long addr1;
	unsigned long addr2;
};

static void flush_tlb_range_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_range(fd->mm, fd->addr1, fd->addr2);
}

void flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		struct flush_tlb_data fd;

		fd.mm = mm;
		fd.addr1 = start;
		fd.addr2 = end;
		smp_call_function(flush_tlb_range_ipi, (void *)&fd, 1, 1);
	} else {
		int i;
		for (i = 0; i < smp_num_cpus; i++)
			if (smp_processor_id() != i)
				CPU_CONTEXT(i, mm) = 0;
	}
	local_flush_tlb_range(mm, start, end);
}

static void flush_tlb_page_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_page(fd->vma, fd->addr1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if ((atomic_read(&vma->vm_mm->mm_users) != 1) || (current->mm != vma->vm_mm)) {
		struct flush_tlb_data fd;

		fd.vma = vma;
		fd.addr1 = page;
		smp_call_function(flush_tlb_page_ipi, (void *)&fd, 1, 1);
	} else {
		int i;
		for (i = 0; i < smp_num_cpus; i++)
			if (smp_processor_id() != i)
				CPU_CONTEXT(i, vma->vm_mm) = 0;
	}
	local_flush_tlb_page(vma, page);
}

EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(cpu_data);
EXPORT_SYMBOL(synchronize_irq);
EXPORT_SYMBOL(kernel_flag);
EXPORT_SYMBOL(__global_sti);
EXPORT_SYMBOL(__global_cli);
EXPORT_SYMBOL(__global_save_flags);
EXPORT_SYMBOL(__global_restore_flags);
