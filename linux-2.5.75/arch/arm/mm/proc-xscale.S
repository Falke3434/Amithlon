/*
 *  linux/arch/arm/mm/proc-xscale.S
 *
 *  Author:	Nicolas Pitre
 *  Created:	November 2000
 *  Copyright:	(C) 2000, 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMU functions for the Intel XScale CPUs
 *
 * 2001 Aug 21:
 *	some contributions by Brett Gaines <brett.w.gaines@intel.com>
 *	Copyright 2001 by Intel Corp.
 *
 * 2001 Sep 08:
 *	Completely revisited, many important fixes
 *	Nicolas Pitre <nico@cam.org>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include <asm/proc/pgtable.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 * This is the maximum size of an area which will be flushed.  If the area
 * is larger than this, then we flush the whole cache
 */
#define MAX_AREA_SIZE	32768

/*
 * the cache line size of the I and D cache
 */
#define CACHELINESIZE	32

/*
 * the size of the data cache
 */
#define CACHESIZE	32768

/*
 * Virtual address used to allocate the cache when flushed
 *
 * This must be an address range which is _never_ used.  It should
 * apparently have a mapping in the corresponding page table for
 * compatibility with future CPUs that _could_ require it.  For instance we
 * don't care.
 *
 * This must be aligned on a 2*CACHESIZE boundary.  The code selects one of
 * the 2 areas in alternance each time the clean_d_cache macro is used.
 * Without this the XScale core exhibits cache eviction problems and no one
 * knows why.
 *
 * Reminder: the vector table is located at 0xffff0000-0xffff0fff.
 */
#define CLEAN_ADDR	0xfffe0000

/*
 * This macro is used to wait for a CP15 write and is needed
 * when we have to ensure that the last operation to the co-pro
 * was completed before continuing with operation.
 */
	.macro	cpwait, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	mov	\rd, \rd			@ wait for completion
	sub 	pc, pc, #4			@ flush instruction pipeline
	.endm

	.macro	cpwait_ret, lr, rd
	mrc	p15, 0, \rd, c2, c0, 0		@ arbitrary read of cp15
	sub	pc, \lr, \rd, LSR #32		@ wait for completion and
						@ flush instruction pipeline
	.endm

/*
 * This macro cleans the entire dcache using line allocate.
 * The main loop has been unrolled to reduce loop overhead.
 * rd and rs are two scratch registers.
 */
	.macro  clean_d_cache, rd, rs
	ldr	\rs, =clean_addr
	ldr	\rd, [\rs]
	eor	\rd, \rd, #CACHESIZE
	str	\rd, [\rs]
	add	\rs, \rd, #CACHESIZE
1:	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	mcr	p15, 0, \rd, c7, c2, 5		@ allocate D cache line
	add	\rd, \rd, #CACHELINESIZE
	teq	\rd, \rs
	bne	1b
	.endm

	.data
clean_addr:	.word	CLEAN_ADDR

	.text

/*
 * cpu_xscale_proc_init()
 *
 * Nothing too exciting at the moment
 */
ENTRY(cpu_xscale_proc_init)
	mov	pc, lr

/*
 * cpu_xscale_proc_fin()
 */
ENTRY(cpu_xscale_proc_fin)
	str	lr, [sp, #-4]!
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	bl	xscale_flush_kern_cache_all	@ clean caches
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1800			@ ...IZ...........
	bic	r0, r0, #0x0006			@ .............CA.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldr	pc, [sp], #4

/*
 * cpu_xscale_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_xscale_reset)
	mov	r1, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r1			@ reset CPSR
	mrc	p15, 0, r1, c1, c0, 0		@ ctrl register
	bic	r1, r1, #0x0086			@ ........B....CA.
	bic	r1, r1, #0x3900			@ ..VIZ..S........
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches & BTB
	bic	r1, r1, #0x0001			@ ...............M
	mcr	p15, 0, r1, c1, c0, 0		@ ctrl register
	@ CAUTION: MMU turned off from this point. We count on the pipeline
	@ already containing those two last instructions to survive.
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, r0

/*
 * cpu_xscale_do_idle()
 *
 * Cause the processor to idle
 *
 * For now we do nothing but go to idle mode for every case
 *
 * XScale supports clock switching, but using idle mode support
 * allows external hardware to react to system state changes.
 */
	.align	5

ENTRY(cpu_xscale_do_idle)
	mov	r0, #1
	mcr	p14, 0, r0, c7, c0, 0		@ Go to IDLE
	mov	pc, lr

/* ================================= CACHE ================================ */

/*
 *	flush_user_cache_all()
 *
 *	Invalidate all cache entries in a particular address
 *	space.
 */
ENTRY(xscale_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(xscale_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	clean_d_cache r0, r1
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, vm_flags)
 *
 *	Invalidate a range of cache entries in the specified
 *	address space.
 *
 *	- start - start address (may not be aligned)
 *	- end	- end address (exclusive, may not be aligned)
 *	- vma	- vma_area_struct describing address space
 */
	.align	5
ENTRY(xscale_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #MAX_AREA_SIZE
	bhs	__flush_whole_cache

1:	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c7, c5, 1		@ Invalidate I cache line
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, r0, c7, c6, 1		@ Invalidate D cache line
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 6		@ Invalidate BTB
	mcrne	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_coherent_kern_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 */
ENTRY(xscale_flush_kern_dcache_page)
	add	r1, r0, #PAGE_SZ
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, r0, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_dma_inv_range)
	mrc	p15, 0, r2, c0, c0, 0		@ read ID
	eor	r2, r2, #0x69000000
	eor	r2, r2, #0x00052000
	bics	r2, r2, #1
	beq	xscale_dma_flush_range

	tst	r0, #CACHELINESIZE - 1
	bic	r0, r0, #CACHELINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHELINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 1		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_dma_clean_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 1		@ Drain Write (& Fill) Buffer
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start  - virtual start address
 *	- end	 - virtual end address
 */
ENTRY(xscale_dma_flush_range)
	bic	r0, r0, #CACHELINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHELINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 1		@ Drain Write (& Fill) Buffer
	mov	pc, lr

ENTRY(xscale_cache_fns)
	.long	xscale_flush_kern_cache_all
	.long	xscale_flush_user_cache_all
	.long	xscale_flush_user_cache_range
	.long	xscale_coherent_kern_range
	.long	xscale_flush_kern_dcache_page
	.long	xscale_dma_inv_range
	.long	xscale_dma_clean_range
	.long	xscale_dma_flush_range

ENTRY(cpu_xscale_dcache_clean_area)
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHELINESIZE
	subs	r1, r1, #CACHELINESIZE
	bhi	1b
	mov	pc, lr

/* ================================ CACHE LOCKING============================
 *
 * The XScale MicroArchitecture implements support for locking entries into
 * the data and instruction cache.  The following functions implement the core
 * low level instructions needed to accomplish the locking.  The developer's
 * manual states that the code that performs the locking must be in non-cached
 * memory.  To accomplish this, the code in xscale-cache-lock.c copies the
 * following functions from the cache into a non-cached memory region that
 * is allocated through consistent_alloc().
 *
 */
	.align	5
/*
 * xscale_icache_lock
 *
 * r0: starting address to lock
 * r1: end address to lock
 */
ENTRY(xscale_icache_lock)

iLockLoop:
	bic	r0, r0, #CACHELINESIZE - 1
	mcr	p15, 0, r0, c9, c1, 0	@ lock into cache
	cmp	r0, r1			@ are we done?
	add	r0, r0, #CACHELINESIZE	@ advance to next cache line
	bls	iLockLoop
	mov	pc, lr

/*
 * xscale_icache_unlock
 */
ENTRY(xscale_icache_unlock)
	mcr	p15, 0, r0, c9, c1, 1	@ Unlock icache
	mov	pc, lr

/*
 * xscale_dcache_lock
 *
 * r0: starting address to lock
 * r1: end address to lock
 */
ENTRY(xscale_dcache_lock)
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	r2, #1
	mcr	p15, 0, r2, c9, c2, 0	@ Put dcache in lock mode
	cpwait	ip			@ Wait for completion

	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
dLockLoop:
	msr	cpsr_c, r3
	mcr	p15, 0, r0, c7, c10, 1	@ Write back line if it is dirty
	mcr	p15, 0, r0, c7, c6, 1	@ Flush/invalidate line
	msr	cpsr_c, r2
	ldr	ip, [r0], #CACHELINESIZE @ Preload 32 bytes into cache from
					@ location [r0]. Post-increment
					@ r3 to next cache line
	cmp	r0, r1			@ Are we done?
	bls	dLockLoop

	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	r2, #0
	mcr	p15, 0, r2, c9, c2, 0	@ Get out of lock mode
	cpwait_ret lr, ip

/*
 * xscale_dcache_unlock
 */
ENTRY(xscale_dcache_unlock)
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, ip, c9, c2, 1	@ Unlock cache
	mov	pc, lr

/*
 * Needed to determine the length of the code that needs to be copied.
 */
	.align	5
ENTRY(xscale_cache_dummy)
	mov	pc, lr

/* ================================ TLB LOCKING==============================
 *
 * The XScale MicroArchitecture implements support for locking entries into
 * the Instruction and Data TLBs.  The following functions provide the
 * low level support for supporting these under Linux.  xscale-lock.c
 * implements some higher level management code.  Most of the following
 * is taken straight out of the Developer's Manual.
 */

/*
 * Lock I-TLB entry
 *
 * r0: Virtual address to translate and lock
 */
	.align	5
ENTRY(xscale_itlb_lock)
	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
	msr	cpsr_c, r3			@ Disable interrupts
	mcr	p15, 0, r0, c8, c5, 1		@ Invalidate I-TLB entry
	mcr	p15, 0, r0, c10, c4, 0		@ Translate and lock
	msr	cpsr_c, r2			@ Restore interrupts
	cpwait_ret lr, ip

/*
 * Lock D-TLB entry
 *
 * r0: Virtual address to translate and lock
 */
	.align	5
ENTRY(xscale_dtlb_lock)
	mrs	r2, cpsr
	orr	r3, r2, #PSR_F_BIT | PSR_I_BIT
	msr	cpsr_c, r3			@ Disable interrupts
	mcr	p15, 0, r0, c8, c6, 1		@ Invalidate D-TLB entry
	mcr	p15, 0, r0, c10, c8, 0		@ Translate and lock
	msr	cpsr_c, r2			@ Restore interrupts
	cpwait_ret lr, ip

/*
 * Unlock all I-TLB entries
 */
	.align	5
ENTRY(xscale_itlb_unlock)
	mcr	p15, 0, ip, c10, c4, 1		@ Unlock I-TLB
	mcr	p15, 0, ip, c8, c5, 0		@ Invalidate I-TLB
	cpwait_ret lr, ip

/*
 * Unlock all D-TLB entries
 */
ENTRY(xscale_dtlb_unlock)
	mcr	p15, 0, ip, c10, c8, 1		@ Unlock D-TBL
	mcr	p15, 0, ip, c8, c6, 0		@ Invalidate D-TLB
	cpwait_ret lr, ip

/* =============================== PageTable ============================== */

#define PTE_CACHE_WRITE_ALLOCATE 0

/*
 * cpu_xscale_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_xscale_switch_mm)
	clean_d_cache r1, r2
	mcr	p15, 0, ip, c7, c5, 0		@ Invalidate I cache & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	cpwait_ret lr, ip

/*
 * cpu_xscale_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 *
 * Errata 40: must set memory to write-through for user read-only pages.
 */
	.align	5
ENTRY(cpu_xscale_set_pte)
	str	r1, [r0], #-2048		@ linux version

	bic	r2, r1, #0xff0
	orr	r2, r2, #PTE_TYPE_EXT		@ extended page

	eor	r3, r1, #L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_WRITE | L_PTE_DIRTY

	tst	r3, #L_PTE_USER			@ User?
	orrne	r2, r2, #PTE_EXT_AP_URO_SRW	@ yes -> user r/o, system r/w

	tst	r3, #L_PTE_WRITE | L_PTE_DIRTY	@ Write and Dirty?
	orreq	r2, r2, #PTE_EXT_AP_UNO_SRW	@ yes -> user n/a, system r/w
						@ combined with user -> user r/w

	@
	@ Handle the X bit.  We want to set this bit for the minicache
	@ (U = E = B = W = 0, C = 1) or when write allocate is enabled,
	@ and we have a writeable, cacheable region.  If we ignore the
	@ U and E bits, we can allow user space to use the minicache as
	@ well.
	@
	@  X = (C & ~W & ~B) | (C & W & B & write_allocate)
	@
	eor	ip, r1, #L_PTE_CACHEABLE
	tst	ip, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
#if PTE_CACHE_WRITE_ALLOCATE
	eorne	ip, r1, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
	tstne	ip, #L_PTE_CACHEABLE | L_PTE_WRITE | L_PTE_BUFFERABLE
#endif
	orreq	r2, r2, #PTE_EXT_TEX(1)

	@
	@ Erratum 40: The B bit must be cleared for a user read-only
	@ cacheable page.
	@
	@  B = B & ~(U & C & ~W)
	@
	and	ip, r1, #L_PTE_USER | L_PTE_WRITE | L_PTE_CACHEABLE
	teq	ip, #L_PTE_USER | L_PTE_CACHEABLE
	biceq	r2, r2, #PTE_BUFFERABLE

	tst	r3, #L_PTE_PRESENT | L_PTE_YOUNG	@ Present and Young?
	movne	r2, #0				@ no -> fault

	str	r2, [r0]			@ hardware version
	mov	ip, #0
	mcr	p15, 0, r0, c7, c10, 1		@ Clean D cache line
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mov	pc, lr


	.ltorg

cpu_80200_name:
	.asciz	"XScale-80200"

cpu_80321_name:
	.asciz	"XScale-IOP80321"

cpu_pxa250_name:
	.asciz	"XScale-PXA250"

cpu_pxa210_name:
	.asciz	"XScale-PXA210"

	.align

	__INIT

__xscale_setup:
	mov	r0, #PSR_F_BIT|PSR_I_BIT|SVC_MODE
	msr	cpsr_c, r0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I, D caches & BTB
	mcr	p15, 0, ip, c7, c10, 4		@ Drain Write (& Fill) Buffer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I, D TLBs
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer
	mov	r0, #0x1f			@ Domains 0, 1 = client
	mcr	p15, 0, r0, c3, c0, 0		@ load domain access register
	mov	r0, #1				@ Allow access to CP0 and CP13
	orr	r0, r0, #1 << 13		@ Its undefined whether this
	mcr	p15, 0, r0, c15, c1, 0		@ affects USR or SVC modes
	mrc	p15, 0, r0, c1, c0, 0		@ get control register
	bic	r0, r0, #0x0200			@ .... ..R. .... ....
	bic	r0, r0, #0x0002			@ .... .... .... ..A.
	orr	r0, r0, #0x0005			@ .... .... .... .C.M
	orr	r0, r0, #0x3900			@ ..VI Z..S .... ....
	mov	pc, lr

	.text

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */

	.type	xscale_processor_functions, #object
ENTRY(xscale_processor_functions)
	.word	xscale_abort
	.word	cpu_xscale_proc_init
	.word	cpu_xscale_proc_fin
	.word	cpu_xscale_reset
	.word	cpu_xscale_do_idle
	.word	cpu_xscale_dcache_clean_area
	.word	cpu_xscale_switch_mm
	.word	cpu_xscale_set_pte
	.size	xscale_processor_functions, . - xscale_processor_functions

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv5te"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v5"
	.size	cpu_elf_name, . - cpu_elf_name
	.align

	.section ".proc.info", #alloc, #execinstr

	.type	__80200_proc_info,#object
__80200_proc_info:
	.long	0x69052000
	.long	0xfffffff0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_80200_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__80200_proc_info, . - __80200_proc_info

	.type	__80321_proc_info,#object
__80321_proc_info:
	.long	0x69052420
	.long	0xfffffff0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_80321_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__80321_proc_info, . - __80321_proc_info

	.type	__pxa250_proc_info,#object
__pxa250_proc_info:
	.long	0x69052100
	.long	0xfffff3f0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_pxa250_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__pxa250_proc_info, . - __pxa250_proc_info

	.type	__pxa210_proc_info,#object
__pxa210_proc_info:
	.long	0x69052120
	.long	0xfffff3f0
	.long	0x00000c0e
	b	__xscale_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP|HWCAP_HALF|HWCAP_THUMB|HWCAP_FAST_MULT|HWCAP_EDSP
	.long	cpu_pxa210_name
	.long	xscale_processor_functions
	.long	v4wbi_tlb_fns
	.long	xscale_mc_user_fns
	.long	xscale_cache_fns
	.size	__pxa210_proc_info, . - __pxa210_proc_info

