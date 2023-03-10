/*
 *  linux/arch/arm/mm/proc-arm720.S: MMU functions for ARM720
 *
 *  Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *                     Rob Scott (rscott@mtrob.fdns.net)
 *  Copyright (C) 2000 ARM Limited, Deep Blue Solutions Ltd.
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
 * functions on the ARM720T.  The ARM720T has a writethrough IDC
 * cache, so we don't need to clean it.
 *
 *  Changelog:
 *   05-09-2000 SJH	Created by moving 720 specific functions
 *			out of 'proc-arm6,7.S' per RMK discussion
 *   07-25-2000 SJH	Added idle function.
 *   08-25-2000	DBS	Updated for integration of ARM Ltd version.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>
#include <asm/constants.h>
#include <asm/procinfo.h>
#include <asm/hardware.h>

/*
 * Function: arm720_proc_init (void)
 *	   : arm720_proc_fin (void)
 *
 * Notes   : This processor does not require these
 */
ENTRY(cpu_arm720_dcache_clean_area)
ENTRY(cpu_arm720_proc_init)
		mov	pc, lr

ENTRY(cpu_arm720_proc_fin)
		stmfd	sp!, {lr}
		mov	ip, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
		msr	cpsr_c, ip
		mrc	p15, 0, r0, c1, c0, 0
		bic	r0, r0, #0x1000			@ ...i............
		bic	r0, r0, #0x000e			@ ............wca.
		mcr	p15, 0, r0, c1, c0, 0		@ disable caches
		mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
		ldmfd	sp!, {pc}

/*
 * Function: arm720_proc_do_idle(void)
 * Params  : r0 = unused
 * Purpose : put the processer in proper idle mode
 */
ENTRY(cpu_arm720_do_idle)
		mov	pc, lr

/*
 * Function: arm720_switch_mm(unsigned long pgd_phys)
 * Params  : pgd_phys	Physical address of page table
 * Purpose : Perform a task switch, saving the old process' state and restoring
 *	     the new.
 */
ENTRY(cpu_arm720_switch_mm)
		mov	r1, #0
		mcr	p15, 0, r1, c7, c7, 0		@ invalidate cache
		mcr	p15, 0, r0, c2, c0, 0		@ update page table ptr
		mcr	p15, 0, r1, c8, c7, 0		@ flush TLB (v4)
		mov	pc, lr

/*
 * Function: arm720_set_pte(pte_t *ptep, pte_t pte)
 * Params  : r0 = Address to set
 *	   : r1 = value to set
 * Purpose : Set a PTE and flush it out of any WB cache
 */
		.align	5
ENTRY(cpu_arm720_set_pte)
		str	r1, [r0], #-2048		@ linux version

		eor	r1, r1, #LPTE_PRESENT | LPTE_YOUNG | LPTE_WRITE | LPTE_DIRTY

		bic	r2, r1, #0xff0
		bic	r2, r2, #3
		orr	r2, r2, #HPTE_TYPE_SMALL

		tst	r1, #LPTE_USER			@ User?
		orrne	r2, r2, #HPTE_AP_READ

		tst	r1, #LPTE_WRITE | LPTE_DIRTY	@ Write and Dirty?
		orreq	r2, r2, #HPTE_AP_WRITE

		tst	r1, #LPTE_PRESENT | LPTE_YOUNG	@ Present and Young
		movne	r2, #0

		str	r2, [r0]			@ hardware version
		mov	pc, lr

/*
 * Function: arm720_reset
 * Params  : r0 = address to jump to
 * Notes   : This sets up everything for a reset
 */
ENTRY(cpu_arm720_reset)
		mov	ip, #0
		mcr	p15, 0, ip, c7, c7, 0		@ invalidate cache
		mcr	p15, 0, ip, c8, c7, 0		@ flush TLB (v4)
		mrc	p15, 0, ip, c1, c0, 0		@ get ctrl register
		bic	ip, ip, #0x000f			@ ............wcam
		bic	ip, ip, #0x2100			@ ..v....s........
		mcr	p15, 0, ip, c1, c0, 0		@ ctrl register
		mov	pc, r0

	
cpu_arm720_name:
		.asciz	"ARM720T"
		.align

		__INIT

__arm720_setup:	mov	r0, #0
		mcr	p15, 0, r0, c7, c7, 0		@ invalidate caches
		mcr	p15, 0, r0, c8, c7, 0		@ flush TLB (v4)
		mcr	p15, 0, r4, c2, c0		@ load page table pointer
		mov	r0, #0x1f			@ Domains 0, 1 = client
		mcr	p15, 0, r0, c3, c0		@ load domain access register

		mrc	p15, 0, r0, c1, c0		@ get control register
		bic	r0, r0, #0x0e00			@ ..V. ..RS BLDP WCAM
		orr	r0, r0, #0x2100			@ .... .... .111 .... (old)
		orr	r0, r0, #0x003d			@ ..1. ..01 ..11 1101 (new)
		mov	pc, lr				@ __ret (head-armv.S)

/*
 * Purpose : Function pointers used to access above functions - all calls
 *	     come through these
 */
		.type	arm720_processor_functions, #object
ENTRY(arm720_processor_functions)
		.word	v4t_late_abort
		.word	cpu_arm720_proc_init
		.word	cpu_arm720_proc_fin
		.word	cpu_arm720_reset
		.word	cpu_arm720_do_idle
		.word	cpu_arm720_dcache_clean_area
		.word	cpu_arm720_switch_mm
		.word	cpu_arm720_set_pte
		.size	arm720_processor_functions, . - arm720_processor_functions

		.type	cpu_arch_name, #object
cpu_arch_name:	.asciz	"armv4t"
		.size	cpu_arch_name, . - cpu_arch_name

		.type	cpu_elf_name, #object
cpu_elf_name:	.asciz	"v4"
		.size	cpu_elf_name, . - cpu_elf_name
		.align

/*
 * See linux/include/asm-arm/procinfo.h for a definition of this structure.
 */
	
		.section ".proc.info", #alloc, #execinstr

		.type	__arm720_proc_info, #object
__arm720_proc_info:
		.long	0x41807200				@ cpu_val
		.long	0xffffff00				@ cpu_mask
		.long	0x00000c1e				@ section_mmu_flags
		b	__arm720_setup				@ cpu_flush
		.long	cpu_arch_name				@ arch_name
		.long	cpu_elf_name				@ elf_name
		.long	HWCAP_SWP | HWCAP_HALF | HWCAP_THUMB	@ elf_hwcap
		.long	cpu_arm720_name				@ name
		.long	arm720_processor_functions
		.long	v4_tlb_fns
		.long	v4wt_user_fns
		.long	v4_cache_fns
		.size	__arm720_proc_info, . - __arm720_proc_info
