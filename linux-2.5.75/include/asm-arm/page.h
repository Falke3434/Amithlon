#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

#include <linux/config.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <asm/glue.h>

/*
 *	User Space Model
 *	================
 *
 *	This section selects the correct set of functions for dealing with
 *	page-based copying and clearing for user space for the particular
 *	processor(s) we're building for.
 *
 *	We have the following to choose from:
 *	  v3		- ARMv3
 *	  v4wt		- ARMv4 with writethrough cache, without minicache
 *	  v4wb		- ARMv4 with writeback cache, without minicache
 *	  v4_mc		- ARMv4 with minicache
 *	  xscale	- Xscale
 */
#undef _USER
#undef MULTI_USER

#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v3
# endif
#endif

#if defined(CONFIG_CPU_ARM720T)
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wt
# endif
#endif

#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM926T) || defined(CONFIG_CPU_SA110)   || \
    defined(CONFIG_CPU_ARM1020)
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wb
# endif
#endif

#if defined(CONFIG_CPU_SA1100)
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4_mc
# endif
#endif

#if defined(CONFIG_CPU_XSCALE)
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER xscale_mc
# endif
#endif

#ifndef _USER
#error Unknown user operations model
#endif

struct cpu_user_fns {
	void (*cpu_clear_user_page)(void *p, unsigned long user);
	void (*cpu_copy_user_page)(void *to, const void *from,
				   unsigned long user);
};

#ifdef MULTI_USER
extern struct cpu_user_fns cpu_user;

#define __cpu_clear_user_page	cpu_user.cpu_clear_user_page
#define __cpu_copy_user_page	cpu_user.cpu_copy_user_page

#else

#define __cpu_clear_user_page	__glue(_USER,_clear_user_page)
#define __cpu_copy_user_page	__glue(_USER,_copy_user_page)

extern void __cpu_clear_user_page(void *p, unsigned long user);
extern void __cpu_copy_user_page(void *to, const void *from,
				 unsigned long user);
#endif

#define clear_user_page(addr,vaddr,pg)			\
	do {						\
		preempt_disable();			\
		__cpu_clear_user_page(addr, vaddr);	\
		preempt_enable();			\
	} while (0)

#define copy_user_page(to,from,vaddr,pg)		\
	do {						\
		preempt_disable();			\
		__cpu_copy_user_page(to, from, vaddr);	\
		preempt_enable();			\
	} while (0)

#define clear_page(page)	memzero((void *)(page), PAGE_SIZE)
extern void copy_page(void *to, void *from);

#undef STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgprot(x)     (x)

#endif /* STRICT_MM_TYPECHECKS */
#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */

#include <asm/proc/page.h>

#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

/* Pure 2^n version of get_order */
static inline int get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif
