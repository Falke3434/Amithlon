#ifndef _ASM_IA64_MMU_CONTEXT_H
#define _ASM_IA64_MMU_CONTEXT_H

/*
 * Copyright (C) 1998-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

/*
 * Routines to manage the allocation of task context numbers.  Task context numbers are
 * used to reduce or eliminate the need to perform TLB flushes due to context switches.
 * Context numbers are implemented using ia-64 region ids.  Since the IA-64 TLB does not
 * consider the region number when performing a TLB lookup, we need to assign a unique
 * region id to each region in a process.  We use the least significant three bits in a
 * region id for this purpose.
 */

#define IA64_REGION_ID_KERNEL	0 /* the kernel's region id (tlb.c depends on this being 0) */

#define ia64_rid(ctx,addr)	(((ctx) << 3) | (addr >> 61))

# ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <asm/processor.h>

#define MMU_CONTEXT_DEBUG	0

#if MMU_CONTEXT_DEBUG

#include <ia64intrin.h>

extern struct mmu_trace_entry {
	char op;
	u8 cpu;
	u32 context;
	void *mm;
} mmu_tbuf[1024];

extern volatile int mmu_tbuf_index;

# define MMU_TRACE(_op,_cpu,_mm,_ctx)							\
do {											\
	int i = __sync_fetch_and_add(&mmu_tbuf_index, 1) % ARRAY_SIZE(mmu_tbuf);	\
	struct mmu_trace_entry e;							\
	e.op = (_op);									\
	e.cpu = (_cpu);									\
	e.mm = (_mm);									\
	e.context = (_ctx);								\
	mmu_tbuf[i] = e;								\
} while (0)

#else
# define MMU_TRACE(op,cpu,mm,ctx)	do { ; } while (0)
#endif

struct ia64_ctx {
	spinlock_t lock;
	unsigned int next;	/* next context number to use */
	unsigned int limit;	/* next >= limit => must call wrap_mmu_context() */
	unsigned int max_ctx;	/* max. context value supported by all CPUs */
};

extern struct ia64_ctx ia64_ctx;
DECLARE_PER_CPU(u8, ia64_need_tlb_flush);

extern void wrap_mmu_context (struct mm_struct *mm);

static inline void
enter_lazy_tlb (struct mm_struct *mm, struct task_struct *tsk)
{
}

/*
 * When the context counter wraps around all TLBs need to be flushed because an old
 * context number might have been reused. This is signalled by the ia64_need_tlb_flush
 * per-CPU variable, which is checked in the routine below. Called by activate_mm().
 * <efocht@ess.nec.de>
 */
static inline void
delayed_tlb_flush (void)
{
	extern void local_flush_tlb_all (void);

	if (unlikely(__get_cpu_var(ia64_need_tlb_flush))) {
		local_flush_tlb_all();
		__get_cpu_var(ia64_need_tlb_flush) = 0;
	}
}

static inline mm_context_t
get_mmu_context (struct mm_struct *mm)
{
	mm_context_t context = mm->context;

	if (context)
		return context;

	spin_lock(&ia64_ctx.lock);
	{
		/* re-check, now that we've got the lock: */
		context = mm->context;
		if (context == 0) {
			if (ia64_ctx.next >= ia64_ctx.limit)
				wrap_mmu_context(mm);
			mm->context = context = ia64_ctx.next++;
		}
	}
	spin_unlock(&ia64_ctx.lock);
	return context;
}

/*
 * Initialize context number to some sane value.  MM is guaranteed to be a brand-new
 * address-space, so no TLB flushing is needed, ever.
 */
static inline int
init_new_context (struct task_struct *p, struct mm_struct *mm)
{
	MMU_TRACE('N', smp_processor_id(), mm, 0);
	mm->context = 0;
	return 0;
}

static inline void
destroy_context (struct mm_struct *mm)
{
	/* Nothing to do.  */
	MMU_TRACE('D', smp_processor_id(), mm, mm->context);
}

static inline void
reload_context (mm_context_t context)
{
	unsigned long rid;
	unsigned long rid_incr = 0;
	unsigned long rr0, rr1, rr2, rr3, rr4;

	rid = context << 3;	/* make space for encoding the region number */
	rid_incr = 1 << 8;

	/* encode the region id, preferred page size, and VHPT enable bit: */
	rr0 = (rid << 8) | (PAGE_SHIFT << 2) | 1;
	rr1 = rr0 + 1*rid_incr;
	rr2 = rr0 + 2*rid_incr;
	rr3 = rr0 + 3*rid_incr;
	rr4 = rr0 + 4*rid_incr;
#ifdef  CONFIG_HUGETLB_PAGE
	rr4 = (rr4 & (~(0xfcUL))) | (HPAGE_SHIFT << 2);
#endif

	ia64_set_rr(0x0000000000000000, rr0);
	ia64_set_rr(0x2000000000000000, rr1);
	ia64_set_rr(0x4000000000000000, rr2);
	ia64_set_rr(0x6000000000000000, rr3);
	ia64_set_rr(0x8000000000000000, rr4);
	ia64_insn_group_barrier();
	ia64_srlz_i();			/* srlz.i implies srlz.d */
	ia64_insn_group_barrier();
}

static inline void
activate_context (struct mm_struct *mm)
{
	mm_context_t context;

	do {
		context = get_mmu_context(mm);
		MMU_TRACE('A', smp_processor_id(), mm, context);
		reload_context(context);
		MMU_TRACE('a', smp_processor_id(), mm, context);
		/* in the unlikely event of a TLB-flush by another thread, redo the load: */
	} while (unlikely(context != mm->context));
}

#define deactivate_mm(tsk,mm)					\
do {								\
	MMU_TRACE('d', smp_processor_id(), mm, mm->context);	\
} while (0)

/*
 * Switch from address space PREV to address space NEXT.
 */
static inline void
activate_mm (struct mm_struct *prev, struct mm_struct *next)
{
	delayed_tlb_flush();

	/*
	 * We may get interrupts here, but that's OK because interrupt handlers cannot
	 * touch user-space.
	 */
	ia64_set_kr(IA64_KR_PT_BASE, __pa(next->pgd));
	activate_context(next);
}

#define switch_mm(prev_mm,next_mm,next_task)	activate_mm(prev_mm, next_mm)

# endif /* ! __ASSEMBLY__ */
#endif /* _ASM_IA64_MMU_CONTEXT_H */
