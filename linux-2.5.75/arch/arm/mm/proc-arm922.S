/*
 *  linux/arch/arm/mm/arm922.S: MMU functions for ARM922
 *
 *  Copyright (C) 1999,2000 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * These are the low level assembler for performing cache and TLB
 * functions on the arm922.
 *
 *  CONFIG_CPU_ARM922_CPU_IDLE -> nohlt
 */
#include <linux/linkage.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>
#include <asm/page.h>
#include "proc-macros.S"

/*
 * The size of one data cache line.
 */
#define CACHE_DLINESIZE	32

/*
 * The number of data cache segments.
 */
#define CACHE_DSEGMENTS	4

/*
 * The number of lines in a cache segment.
 */
#define CACHE_DENTRIES	64

/*
 * This is the size at which it becomes more efficient to
 * clean the whole cache, rather than using the individual
 * cache line maintainence instructions.  (I think this should
 * be 32768).
 */
#define CACHE_DLIMIT	8192


	.text
/*
 * cpu_arm922_proc_init()
 */
ENTRY(cpu_arm922_proc_init)
	mov	pc, lr

/*
 * cpu_arm922_proc_fin()
 */
ENTRY(cpu_arm922_proc_fin)
	stmfd	sp!, {lr}
	mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, ip
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	bl	arm922_flush_kern_cache_all
#else
	bl	v4wt_flush_kern_cache_all
#endif
	mrc	p15, 0, r0, c1, c0, 0		@ ctrl register
	bic	r0, r0, #0x1000			@ ...i............
	bic	r0, r0, #0x000e			@ ............wca.
	mcr	p15, 0, r0, c1, c0, 0		@ disable caches
	ldmfd	sp!, {pc}

/*
 * cpu_arm922_reset(loc)
 *
 * Perform a soft reset of the system.  Put the CPU into the
 * same state as it would be if it had been reset, and branch
 * to what would be the reset vector.
 *
 * loc: location to jump to for soft reset
 */
	.align	5
ENTRY(cpu_arm922_reset)
	mov	ip, #0
	mcr	p15, 0, ip, c7, c7, 0		@ invalidate I,D caches
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mrc	p15, 0, ip, c1, c0, 0		@ ctrl register
	bic	ip, ip, #0x000f			@ ............wcam
	bic	ip, ip, #0x1100			@ ...i...s........
	mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
	mov	pc, r0

/*
 * cpu_arm922_do_idle()
 */
	.align	5
ENTRY(cpu_arm922_do_idle)
	mcr	p15, 0, r0, c7, c0, 4		@ Wait for interrupt
	mov	pc, lr


#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH

/*
 *	flush_user_cache_all()
 *
 *	Clean and invalidate all cache entries in a particular
 *	address space.
 */
ENTRY(arm922_flush_user_cache_all)
	/* FALLTHROUGH */

/*
 *	flush_kern_cache_all()
 *
 *	Clean and invalidate the entire cache.
 */
ENTRY(arm922_flush_kern_cache_all)
	mov	r2, #VM_EXEC
	mov	ip, #0
__flush_whole_cache:
	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 8 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean+invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 7 to 0
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_user_cache_range(start, end, flags)
 *
 *	Clean and invalidate a range of cache entries in the
 *	specified address range.
 *
 *	- start	- start address (inclusive)
 *	- end	- end address (exclusive)
 *	- flags	- vm_flags describing address space
 */
ENTRY(arm922_flush_user_cache_range)
	mov	ip, #0
	sub	r3, r1, r0			@ calculate total size
	cmp	r3, #CACHE_DLIMIT
	bhs	__flush_whole_cache

1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	tst	r2, #VM_EXEC
	mcrne	p15, 0, ip, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	coherent_kern_range(start, end)
 *
 *	Ensure coherency between the Icache and the Dcache in the
 *	region described by start, end.  If you have non-snooping
 *	Harvard caches, you need to implement this function.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm922_coherent_kern_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c5, 1		@ invalidate I entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	flush_kern_dcache_page(void *page)
 *
 *	Ensure no D cache aliasing occurs, either with itself or
 *	the I cache
 *
 *	- addr	- page aligned address
 */
ENTRY(arm922_flush_kern_dcache_page)
	add	r1, r0, #PAGE_SZ
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_inv_range(start, end)
 *
 *	Invalidate (discard) the specified virtual address range.
 *	May not write back any entries.  If 'start' or 'end'
 *	are not cache line aligned, those lines must be written
 *	back.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as v4wb)
 */
ENTRY(arm922_dma_inv_range)
	tst	r0, #CACHE_DLINESIZE - 1
	bic	r0, r0, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r0, c7, c10, 1		@ clean D entry
	tst	r1, #CACHE_DLINESIZE - 1
	mcrne	p15, 0, r1, c7, c10, 1		@ clean D entry
1:	mcr	p15, 0, r0, c7, c6, 1		@ invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_clean_range(start, end)
 *
 *	Clean the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 *
 * (same as v4wb)
 */
ENTRY(arm922_dma_clean_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

/*
 *	dma_flush_range(start, end)
 *
 *	Clean and invalidate the specified virtual address range.
 *
 *	- start	- virtual start address
 *	- end	- virtual end address
 */
ENTRY(arm922_dma_flush_range)
	bic	r0, r0, #CACHE_DLINESIZE - 1
1:	mcr	p15, 0, r0, c7, c14, 1		@ clean+invalidate D entry
	add	r0, r0, #CACHE_DLINESIZE
	cmp	r0, r1
	blo	1b
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr

ENTRY(arm922_cache_fns)
	.long	arm922_flush_kern_cache_all
	.long	arm922_flush_user_cache_all
	.long	arm922_flush_user_cache_range
	.long	arm922_coherent_kern_range
	.long	arm922_flush_kern_dcache_page
	.long	arm922_dma_inv_range
	.long	arm922_dma_clean_range
	.long	arm922_dma_flush_range

#endif


ENTRY(cpu_arm922_dcache_clean_area)
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
1:	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	add	r0, r0, #CACHE_DLINESIZE
	subs	r1, r1, #CACHE_DLINESIZE
	bhi	1b
#endif
	mov	pc, lr

/* =============================== PageTable ============================== */

/*
 * cpu_arm922_switch_mm(pgd)
 *
 * Set the translation base pointer to be as described by pgd.
 *
 * pgd: new page tables
 */
	.align	5
ENTRY(cpu_arm922_switch_mm)
	mov	ip, #0
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	mcr	p15, 0, ip, c7, c6, 0		@ invalidate D cache
#else
@ && 'Clean & Invalidate whole DCache'
@ && Re-written to use Index Ops.
@ && Uses registers r1, r3 and ip

	mov	r1, #(CACHE_DSEGMENTS - 1) << 5	@ 4 segments
1:	orr	r3, r1, #(CACHE_DENTRIES - 1) << 26 @ 64 entries
2:	mcr	p15, 0, r3, c7, c14, 2		@ clean & invalidate D index
	subs	r3, r3, #1 << 26
	bcs	2b				@ entries 63 to 0
	subs	r1, r1, #1 << 5
	bcs	1b				@ segments 7 to 0
#endif
	mcr	p15, 0, ip, c7, c5, 0		@ invalidate I cache
	mcr	p15, 0, ip, c7, c10, 4		@ drain WB
	mcr	p15, 0, r0, c2, c0, 0		@ load page table pointer
	mcr	p15, 0, ip, c8, c7, 0		@ invalidate I & D TLBs
	mov	pc, lr

/*
 * cpu_arm922_set_pte(ptep, pte)
 *
 * Set a PTE and flush it out
 */
	.align	5
ENTRY(cpu_arm922_set_pte)
	str	r1, [r0], #-2048		@ linux version

	eor	r1, r1, #LPTE_PRESENT | LPTE_YOUNG | LPTE_WRITE | LPTE_DIRTY

	bic	r2, r1, #0xff0
	bic	r2, r2, #3
	orr	r2, r2, #HPTE_TYPE_SMALL

	tst	r1, #LPTE_USER			@ User?
	orrne	r2, r2, #HPTE_AP_READ

	tst	r1, #LPTE_WRITE | LPTE_DIRTY	@ Write and Dirty?
	orreq	r2, r2, #HPTE_AP_WRITE

	tst	r1, #LPTE_PRESENT | LPTE_YOUNG	@ Present and Young?
	movne	r2, #0

#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	eor	r3, r2, #0x0a			@ C & small page?
	tst	r3, #0x0b
	biceq	r2, r2, #4
#endif
	str	r2, [r0]			@ hardware version
	mov	r0, r0
	mcr	p15, 0, r0, c7, c10, 1		@ clean D entry
	mcr	p15, 0, r0, c7, c10, 4		@ drain WB
	mov	pc, lr


ENTRY(cpu_arm922_name)
	.ascii	"Arm922T"
#ifndef CONFIG_CPU_ICACHE_DISABLE
	.ascii	"i"
#endif
#ifndef CONFIG_CPU_DCACHE_DISABLE
	.ascii	"d"
#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
	.ascii	"(wt)"
#else
	.ascii	"(wb)"
#endif
#endif
	.ascii	"\0"
	.align

	__INIT

__arm922_setup:
	mov	r0, #0
	mcr	p15, 0, r0, c7, c7		@ invalidate I,D caches on v4
	mcr	p15, 0, r0, c7, c10, 4		@ drain write buffer on v4
	mcr	p15, 0, r0, c8, c7		@ invalidate I,D TLBs on v4
	mcr	p15, 0, r4, c2, c0		@ load page table pointer
	mov	r0, #0x1f			@ Domains 0, 1 = client
	mcr	p15, 0, r0, c3, c0		@ load domain access register
	mrc	p15, 0, r0, c1, c0		@ get control register v4
/*
 * Clear out 'unwanted' bits (then put them in if we need them)
 */
						@   VI ZFRS BLDP WCAM
	bic	r0, r0, #0x0e00
	bic	r0, r0, #0x0002
	bic	r0, r0, #0x000c
	bic	r0, r0, #0x1000			@ ...0 000. .... 000.
/*
 * Turn on what we want
 */
	orr	r0, r0, #0x0031
	orr	r0, r0, #0x2100			@ ..1. ...1 ..11 ...1

#ifndef CONFIG_CPU_DCACHE_DISABLE
	orr	r0, r0, #0x0004			@ .... .... .... .1..
#endif
#ifndef CONFIG_CPU_ICACHE_DISABLE
	orr	r0, r0, #0x1000			@ ...1 .... .... ....
#endif
	mov	pc, lr

	.text

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
	.type	arm922_processor_functions, #object
arm922_processor_functions:
	.word	v4t_early_abort
	.word	cpu_arm922_proc_init
	.word	cpu_arm922_proc_fin
	.word	cpu_arm922_reset
	.word   cpu_arm922_do_idle
	.word	cpu_arm922_dcache_clean_area
	.word	cpu_arm922_switch_mm
	.word	cpu_arm922_set_pte
	.size	arm922_processor_functions, . - arm922_processor_functions

	.type	cpu_arch_name, #object
cpu_arch_name:
	.asciz	"armv4t"
	.size	cpu_arch_name, . - cpu_arch_name

	.type	cpu_elf_name, #object
cpu_elf_name:
	.asciz	"v4"
	.size	cpu_elf_name, . - cpu_elf_name
	.align

	.section ".proc.info", #alloc, #execinstr

	.type	__arm922_proc_info,#object
__arm922_proc_info:
	.long	0x41009220
	.long	0xff00fff0
	.long	0x00000c1e			@ mmuflags
	b	__arm922_setup
	.long	cpu_arch_name
	.long	cpu_elf_name
	.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB
	.long	cpu_arm922_name
	.long	arm922_processor_functions
	.long	v4wbi_tlb_fns
	.long	v4wb_user_fns
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	.long	arm922_cache_fns
#else
	.long	v4wt_cache_fns
#endif
	.size	__arm922_proc_info, . - __arm922_proc_info
