/* thread_info.h: PPC low-level thread information
 * adapted from the i386 version by Paul Mackerras
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <linux/stringify.h>

/*
 * low level task data.
 */
struct thread_info {
	struct task_struct *task;		/* main task structure */
	struct exec_domain *exec_domain;	/* execution domain */
	unsigned long	flags;			/* low level flags */
	int		cpu;			/* cpu we're on */
	int		preempt_count;		/* not used at present */
	struct restart_block restart_block;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task =		&tsk,			\
	.exec_domain =	&default_exec_domain,	\
	.flags =	0,			\
	.cpu =		0,			\
	.preempt_count = 1,			\
	.restart_block = {			\
		.fn = do_no_restart_syscall,	\
	},					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* thread information allocation */

#define THREAD_ORDER		2
#define THREAD_SIZE		(PAGE_SIZE << THREAD_ORDER)
#define THREAD_SHIFT		(PAGE_SHIFT + THREAD_ORDER)

#define alloc_thread_info(tsk) ((struct thread_info *) \
				__get_free_pages(GFP_KERNEL, THREAD_ORDER))
#define free_thread_info(ti)	free_pages((unsigned long) (ti), THREAD_ORDER)
#define get_thread_info(ti)	get_task_struct((ti)->task)
#define put_thread_info(ti)	put_task_struct((ti)->task)

#if THREAD_SIZE != (4*PAGE_SIZE)
#error update vmlinux.lds and current_thread_info to match
#endif

/* how to get the thread information struct from C */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;
	__asm__("clrrdi %0,1,14" : "=r"(ti));
	return ti;
}

#endif /* __ASSEMBLY__ */

#define PREEMPT_ACTIVE		0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* resumption notification requested */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_POLLING_NRFLAG	4	/* true if poll_idle() is polling
					   TIF_NEED_RESCHED */
#define TIF_32BIT		5	/* 32 bit binary */
#define TIF_RUN_LIGHT		6	/* iSeries run light */
#define TIF_ABI_PENDING		7	/* 32/64 bit switch needed */

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_32BIT		(1<<TIF_32BIT)
#define _TIF_RUN_LIGHT		(1<<TIF_RUN_LIGHT)
#define _TIF_ABI_PENDING	(1<<TIF_ABI_PENDING)

#define _TIF_USER_WORK_MASK	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING | \
				 _TIF_NEED_RESCHED)

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
