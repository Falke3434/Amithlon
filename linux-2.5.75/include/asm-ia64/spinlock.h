#ifndef _ASM_IA64_SPINLOCK_H
#define _ASM_IA64_SPINLOCK_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * This file is used for SMP configurations only.
 */

#include <linux/kernel.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/atomic.h>

typedef struct {
	volatile unsigned int lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED			(spinlock_t) { 0 }
#define spin_lock_init(x)			((x)->lock = 0)

#define NEW_LOCK
#ifdef NEW_LOCK

/*
 * Try to get the lock.  If we fail to get the lock, make a non-standard call to
 * ia64_spinlock_contention().  We do not use a normal call because that would force all
 * callers of spin_lock() to be non-leaf routines.  Instead, ia64_spinlock_contention() is
 * carefully coded to touch only those registers that spin_lock() marks "clobbered".
 */

#define IA64_SPINLOCK_CLOBBERS "ar.ccv", "ar.pfs", "p14", "r28", "r29", "r30", "b6", "memory"

static inline void
_raw_spin_lock (spinlock_t *lock)
{
	register volatile unsigned int *ptr asm ("r31") = &lock->lock;

#if __GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 4)
# ifdef CONFIG_ITANIUM
	/* don't use brl on Itanium... */
	asm volatile ("{\n\t"
		      "  mov ar.ccv = r0\n\t"
		      "  mov r28 = ip\n\t"
		      "  mov r30 = 1;;\n\t"
		      "}\n\t"
		      "cmpxchg4.acq r30 = [%1], r30, ar.ccv\n\t"
		      "movl r29 = ia64_spinlock_contention_pre3_4;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "mov b6 = r29;;\n"
		      "(p14) br.cond.spnt.many b6"
		      : "=r"(ptr) : "r"(ptr) : IA64_SPINLOCK_CLOBBERS);
# else
	asm volatile ("{\n\t"
		      "  mov ar.ccv = r0\n\t"
		      "  mov r28 = ip\n\t"
		      "  mov r30 = 1;;\n\t"
		      "}\n\t"
		      "cmpxchg4.acq r30 = [%1], r30, ar.ccv;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n"
		      "(p14) brl.cond.spnt.many ia64_spinlock_contention_pre3_4"
		      : "=r"(ptr) : "r"(ptr) : IA64_SPINLOCK_CLOBBERS);
# endif /* CONFIG_MCKINLEY */
#else
# ifdef CONFIG_ITANIUM
	/* don't use brl on Itanium... */
	/* mis-declare, so we get the entry-point, not it's function descriptor: */
	asm volatile ("mov r30 = 1\n\t"
		      "mov ar.ccv = r0;;\n\t"
		      "cmpxchg4.acq r30 = [%0], r30, ar.ccv\n\t"
		      "movl r29 = ia64_spinlock_contention;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "mov b6 = r29;;\n"
		      "(p14) br.call.spnt.many b6 = b6"
		      : "=r"(ptr) : "r"(ptr) : IA64_SPINLOCK_CLOBBERS);
# else
	asm volatile ("mov r30 = 1\n\t"
		      "mov ar.ccv = r0;;\n\t"
		      "cmpxchg4.acq r30 = [%0], r30, ar.ccv;;\n\t"
		      "cmp4.ne p14, p0 = r30, r0\n\t"
		      "(p14) brl.call.spnt.many b6=ia64_spinlock_contention"
		      : "=r"(ptr) : "r"(ptr) : IA64_SPINLOCK_CLOBBERS);
# endif /* CONFIG_MCKINLEY */
#endif
}

#else /* !NEW_LOCK */

/*
 * Streamlined test_and_set_bit(0, (x)).  We use test-and-test-and-set
 * rather than a simple xchg to avoid writing the cache-line when
 * there is contention.
 */
#define _raw_spin_lock(x) __asm__ __volatile__ (		\
	"mov ar.ccv = r0\n"					\
	"mov r29 = 1\n"						\
	";;\n"							\
	"1:\n"							\
	"ld4.bias r2 = [%0]\n"					\
	";;\n"							\
	"cmp4.eq p0,p7 = r0,r2\n"				\
	"(p7) br.cond.spnt.few 1b \n"				\
	"cmpxchg4.acq r2 = [%0], r29, ar.ccv\n"			\
	";;\n"							\
	"cmp4.eq p0,p7 = r0, r2\n"				\
	"(p7) br.cond.spnt.few 1b\n"				\
	";;\n"							\
	:: "r"(&(x)->lock) : "ar.ccv", "p7", "r2", "r29", "memory")

#endif /* !NEW_LOCK */

#define spin_is_locked(x)	((x)->lock != 0)
#define _raw_spin_unlock(x)	do { barrier(); ((spinlock_t *) x)->lock = 0; } while (0)
#define _raw_spin_trylock(x)	(cmpxchg_acq(&(x)->lock, 0, 1) == 0)
#define spin_unlock_wait(x)	do { barrier(); } while ((x)->lock)

typedef struct {
	volatile int read_counter	: 31;
	volatile int write_lock		:  1;
} rwlock_t;
#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

#define rwlock_init(x)		do { *(x) = RW_LOCK_UNLOCKED; } while(0)
#define rwlock_is_locked(x)	(*(volatile int *) (x) != 0)

#define _raw_read_lock(rw)								\
do {											\
	rwlock_t *__read_lock_ptr = (rw);						\
											\
	while (unlikely(ia64_fetchadd(1, (int *) __read_lock_ptr, "acq") < 0)) {	\
		ia64_fetchadd(-1, (int *) __read_lock_ptr, "rel");			\
		while (*(volatile int *)__read_lock_ptr < 0)				\
			cpu_relax();							\
	}										\
} while (0)

#define _raw_read_unlock(rw)					\
do {								\
	rwlock_t *__read_lock_ptr = (rw);			\
	ia64_fetchadd(-1, (int *) __read_lock_ptr, "rel");	\
} while (0)

#define _raw_write_lock(rw)							\
do {										\
 	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1\n"					\
		";;\n"								\
		"1:\n"								\
		"ld4 r2 = [%0]\n"						\
		";;\n"								\
		"cmp4.eq p0,p7 = r0,r2\n"					\
		"(p7) br.cond.spnt.few 1b \n"					\
		"cmpxchg4.acq r2 = [%0], r29, ar.ccv\n"				\
		";;\n"								\
		"cmp4.eq p0,p7 = r0, r2\n"					\
		"(p7) br.cond.spnt.few 1b\n"					\
		";;\n"								\
		:: "r"(rw) : "ar.ccv", "p7", "r2", "r29", "memory");		\
} while(0)

#define _raw_write_trylock(rw)							\
({										\
	register long result;							\
										\
	__asm__ __volatile__ (							\
		"mov ar.ccv = r0\n"						\
		"dep r29 = -1, r0, 31, 1\n"					\
		";;\n"								\
		"cmpxchg4.acq %0 = [%1], r29, ar.ccv\n"				\
		: "=r"(result) : "r"(rw) : "ar.ccv", "r29", "memory");		\
	(result == 0);								\
})

#define _raw_write_unlock(x)								\
({											\
	smp_mb__before_clear_bit();	/* need barrier before releasing lock... */	\
	clear_bit(31, (x));								\
})

#endif /*  _ASM_IA64_SPINLOCK_H */
