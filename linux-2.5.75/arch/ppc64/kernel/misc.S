/*
 *  arch/ppc/kernel/misc.S
 *
 *  
 *
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com) 
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/ppc_asm.h>
#include <asm/offsets.h>
#include <asm/cputable.h>

	.text

/*
 * Returns (address we're running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	LOADADDR(r4,1b)
	sub	r3,r4,r3
	mtlr	r0
	blr

_GLOBAL(get_msr)
	mfmsr	r3
	blr

_GLOBAL(get_dar)
	mfdar	r3
	blr

_GLOBAL(get_srr0)
	mfsrr0  r3
	blr

_GLOBAL(get_srr1)
	mfsrr1  r3
	blr
	
_GLOBAL(get_sp)
	mr	r3,r1
	blr
		
#ifdef CONFIG_PPC_ISERIES
/* unsigned long __no_use_save_flags(void) */
_GLOBAL(__no_use_save_flags)
#warning FIX ISERIES
	mfspr	r4,SPRG3
	lbz	r3,PACAPROCENABLED(r4)
	blr

/* void __no_use_restore_flags(unsigned long flags) */	
_GLOBAL(__no_use_restore_flags)
/*
 * Just set/clear the MSR_EE bit through restore/flags but do not
 * change anything else.  This is needed by the RT system and makes
 * sense anyway.
 *    -- Cort
 */
#warning FIX ISERIES
	mfspr	r6,SPRG3
	lbz	r5,PACAPROCENABLED(r6)
	 /* Check if things are setup the way we want _already_. */
	cmpw	0,r3,r5
	beqlr
	/* are we enabling interrupts? */
	cmpi	0,r3,0
	stb	r3,PACAPROCENABLED(r6)
	beqlr
	/* Check pending interrupts */
	CHECKANYINT(r4,r5)
	beqlr

	/* 
	 * Handle pending interrupts in interrupt context
	 */
	li	r0,0x5555
	sc
	blr

_GLOBAL(__no_use_cli)
#warning FIX ISERIES
	mfspr	r5,SPRG3
	lbz	r3,PACAPROCENABLED(r5)
	li	r4,0
	stb	r4,PACAPROCENABLED(r5)
	blr			/* Done */

_GLOBAL(__no_use_sti)
#warning FIX ISERIES
	mfspr	r6,SPRG3
	li	r3,1
	stb	r3,PACAPROCENABLED(r6)

	/* Check for pending interrupts
	 *   A decrementer, IPI or PMC interrupt may have occurred
	 *   while we were in the hypervisor (which enables)
	 */
	CHECKANYINT(r4,r5)
	beqlr

	/* 
	 * Handle pending interrupts in interrupt context
	 */
	li	r0,0x5555
	sc	
	blr
#endif
/*
 * Flush instruction cache.
 */
_GLOBAL(flush_instruction_cache)

/*
 * This is called by kgdb code
 * and should probably go away
 * to be replaced by invalidating
 * the cache lines that are actually
 * modified
 */
	/* use invalidate-all bit in HID0
	 *  - is this consistent across all 64-bit cpus?  -- paulus */
	mfspr	r3,HID0
	ori	r3,r3,HID0_ICFI
	mtspr	HID0,r3
	sync
	isync
	blr

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 *
 *   flush all bytes from start through stop-1 inclusive
 */

_GLOBAL(flush_icache_range)

/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 * and in some cases i-cache and d-cache line sizes differ from
 * each other.
 */
	LOADADDR(r10,naca)		/* Get Naca address */
	ld	r10,0(r10)
	LOADADDR(r11,systemcfg)		/* Get systemcfg address */
	ld	r11,0(r11)
	lwz	r7,DCACHEL1LINESIZE(r11)/* Get cache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)	/* Get log-2 of cache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
1:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	1b
	sync

/* Now invalidate the instruction cache */
	
	lwz	r7,ICACHEL1LINESIZE(r11)	/* Get Icache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5
	lwz	r9,ICACHEL1LOGLINESIZE(r10)	/* Get log-2 of Icache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
2:	icbi	0,r6
	add	r6,r6,r7
	bdnz	2b
	isync
	blr
	
/*
 * Like above, but only do the D-cache.
 *
 * flush_dcache_range(unsigned long start, unsigned long stop)
 *
 *    flush all bytes from start to stop-1 inclusive
 */
_GLOBAL(flush_dcache_range)

/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 */
	LOADADDR(r10,naca)		/* Get Naca address */
	ld	r10,0(r10)
	LOADADDR(r11,systemcfg)		/* Get systemcfg address */
	ld	r11,0(r11)
	lwz	r7,DCACHEL1LINESIZE(r11)	/* Get dcache line size */
	addi	r5,r7,-1
	andc	r6,r3,r5		/* round low to line bdy */
	subf	r8,r6,r4		/* compute length */
	add	r8,r8,r5		/* ensure we get enough */
	lwz	r9,DCACHEL1LOGLINESIZE(r10)	/* Get log-2 of dcache line size */
	srw.	r8,r8,r9		/* compute line count */
	beqlr				/* nothing to do? */
	mtctr	r8
0:	dcbst	0,r6
	add	r6,r6,r7
	bdnz	0b
	sync
	blr

/*
 * Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *
 *	void __flush_dcache_icache(void *page)
 */
_GLOBAL(__flush_dcache_icache)
/*
 * Flush the data cache to memory 
 * 
 * Different systems have different cache line sizes
 */

/* Flush the dcache */
	LOADADDR(r7,naca)
	ld	r7,0(r7)
	LOADADDR(r8,systemcfg)			/* Get systemcfg address */
	ld	r8,0(r8)
	clrrdi	r3,r3,12           	    /* Page align */
	lwz	r4,DCACHEL1LINESPERPAGE(r7)	/* Get # dcache lines per page */
	lwz	r5,DCACHEL1LINESIZE(r8)		/* Get dcache line size */
	mr	r6,r3
	mtctr	r4
0:	dcbst	0,r6
	add	r6,r6,r5
	bdnz	0b
	sync

/* Now invalidate the icache */	

	lwz	r4,ICACHEL1LINESPERPAGE(r7)	/* Get # icache lines per page */
	lwz	r5,ICACHEL1LINESIZE(r8)		/* Get icache line size */
	mtctr	r4
1:	icbi	0,r3
	add	r3,r3,r5
	bdnz	1b
	isync
	blr
	
/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
	eieio
	stbu	r5,1(r4)
	bdnz	00b
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbzu	r5,1(r4)
	stb	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	eieio
	sthbrx	r5,0,r3	
	bdnz	00b
	blr	

_GLOBAL(_insl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3
	eieio
	bdnz	00b
	blr	

_GLOBAL(ide_insw)
_GLOBAL(_insw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
	eieio
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(ide_outsw)
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
	sth	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(_insl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
	eieio
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
	stw	r5,0(r3)
	eieio
	bdnz	00b
	blr	

_GLOBAL(abs)
	cmpi	0,r3,0
	bge	10f
	neg	r3,r3
10:	blr

_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr
	
_GLOBAL(_get_PVR)
	mfspr	r3,PVR
	blr

_GLOBAL(_get_PIR)
	mfspr	r3,PIR
	blr

_GLOBAL(_get_HID0)
	mfspr	r3,HID0
	blr

_GLOBAL(cvt_fd)
	lfd	0,0(r5)		/* load up fpscr value */
	mtfsf	0xff,0
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,0(r5)
	blr

_GLOBAL(cvt_df)
	lfd	0,0(r5)		/* load up fpscr value */
	mtfsf	0xff,0
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0		/* save new fpscr value */
	stfd	0,0(r5)
	blr

/*
 * identify_cpu,
 * In:	r3 = base of the cpu_specs array
 *	r4 = address of cur_cpu_spec
 *	r5 = relocation offset
 */
_GLOBAL(identify_cpu)
	mfpvr	r7
1:
	lwz	r8,CPU_SPEC_PVR_MASK(r3)
	and	r8,r8,r7
	lwz	r9,CPU_SPEC_PVR_VALUE(r3)
	cmplw	0,r9,r8
	beq	1f
	addi	r3,r3,CPU_SPEC_ENTRY_SIZE
	b	1b
1:
	add	r3,r3,r5
	std	r3,0(r4)
	blr

/*
 * do_cpu_ftr_fixups - goes through the list of CPU feature fixups
 * and writes nop's over sections of code that don't apply for this cpu.
 * r3 = data offset (not changed)
 */
_GLOBAL(do_cpu_ftr_fixups)
	/* Get CPU 0 features */
	LOADADDR(r6,cur_cpu_spec)
	sub	r6,r6,r3
	ld	r4,0(r6)
	sub	r4,r4,r3
	ld	r4,CPU_SPEC_FEATURES(r4)
	/* Get the fixup table */
	LOADADDR(r6,__start___ftr_fixup)
	sub	r6,r6,r3
	LOADADDR(r7,__stop___ftr_fixup)
	sub	r7,r7,r3
	/* Do the fixup */
1:	cmpld	r6,r7
	bgelr
	addi	r6,r6,32
	ld	r8,-32(r6)	/* mask */
	and	r8,r8,r4
	ld	r9,-24(r6)	/* value */
	cmpld	r8,r9
	beq	1b
	ld	r8,-16(r6)	/* section begin */
	ld	r9,-8(r6)	/* section end */
	subf.	r9,r8,r9
	beq	1b
	/* write nops over the section of code */
	/* todo: if large section, add a branch at the start of it */
	srwi	r9,r9,2
	mtctr	r9
	sub	r8,r8,r3
	lis	r0,0x60000000@h	/* nop */
3:	stw	r0,0(r8)
	andi.	r10,r4,CPU_FTR_SPLIT_ID_CACHE@l
	beq	2f
	dcbst	0,r8		/* suboptimal, but simpler */
	sync
	icbi	0,r8
2:	addi	r8,r8,4
	bdnz	3b
	sync			/* additional sync needed on g4 */
	isync
	b	1b

/*
 * call_setup_cpu - call the setup_cpu function for this cpu
 * r3 = data offset
 *
 * Setup function is called with:
 *   r3 = data offset
 *   r4 = ptr to CPU spec (relocated)
 */
_GLOBAL(call_setup_cpu)
	LOADADDR(r4, cur_cpu_spec)
	sub	r4,r4,r3
	lwz	r4,0(r4)		# load pointer to cpu_spec
	sub	r4,r4,r3		# relocate
	lwz	r6,CPU_SPEC_SETUP(r4)	# load function pointer
	sub	r6,r6,r3
	mtctr	r6
	bctr



/*
 * Create a kernel thread
 *   kernel_thread(fn, arg, flags)
 */
_GLOBAL(kernel_thread)
	std	r29,-24(r1)
	std	r30,-16(r1)
	stdu	r1,-STACK_FRAME_OVERHEAD(r1)
	mr	r29,r3
	mr	r30,r4
	ori	r3,r5,CLONE_VM	/* flags */
	oris	r3,r3,(CLONE_UNTRACED>>16)
	li	r4,0		/* new sp (unused) */
	li	r0,__NR_clone
	sc
	cmpi	0,r3,0		/* parent or child? */
	bne	1f		/* return if parent */
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)
	ld	r2,8(r29)
	ld	r29,0(r29)
	mtlr	r29              /* fn addr in lr */
	mr	r3,r30	        /* load arg and call fn */
	blrl
	li	r0,__NR_exit	/* exit after child exits */
        li	r3,0
	sc
1:	addi	r1,r1,STACK_FRAME_OVERHEAD	
	ld	r29,-24(r1)
	ld	r30,-16(r1)
	blr

	.section	".toc","aw"
.SYSCALL_ERRNO:
	.tc errno[TC],errno

	.section	".text"
	.align 3
	
#define SYSCALL(name) \
_GLOBAL(name) \
	li	r0,__NR_##name; \
	sc; \
	bnslr; \
	ld	r4,.SYSCALL_ERRNO@toc(2); \
	std	r3,0(r4); \
	li	r3,-1; \
	blr

#define __NR__exit __NR_exit

SYSCALL(setsid)
SYSCALL(open)
SYSCALL(read)
SYSCALL(write)
SYSCALL(lseek)
SYSCALL(close)
SYSCALL(dup)
SYSCALL(execve)
SYSCALL(waitpid)

/* Why isn't this a) automatic, b) written in 'C'? */	
	.balign 8
_GLOBAL(sys_call_table32)
	.llong .sys_restart_syscall	/* 0 */
	.llong .sys_exit
	.llong .sys_fork
	.llong .sys_read
	.llong .sys_write
	.llong .sys32_open		/* 5 */
	.llong .sys_close
	.llong .sys32_waitpid
	.llong .sys32_creat
	.llong .sys_link
	.llong .sys_unlink      	/* 10 */
	.llong .sys32_execve
	.llong .sys_chdir
	.llong .sys32_time
	.llong .sys_mknod
	.llong .sys_chmod		/* 15 */
	.llong .sys_lchown
	.llong .sys_ni_syscall		/* old break syscall */
	.llong .sys_ni_syscall		/* old stat syscall */
	.llong .ppc32_lseek
	.llong .sys_getpid              /* 20 */
	.llong .sys32_mount
	.llong .sys_oldumount
	.llong .sys_setuid
	.llong .sys_getuid
	.llong .ppc64_sys32_stime       /* 25 */
	.llong .sys32_ptrace
	.llong .sys_alarm
	.llong .sys_ni_syscall		/* old fstat syscall */
	.llong .sys32_pause
	.llong .compat_sys_utime		/* 30 */
	.llong .sys_ni_syscall		/* old stty syscall */
	.llong .sys_ni_syscall		/* old gtty syscall */
	.llong .sys32_access
	.llong .sys32_nice
	.llong .sys_ni_syscall		/* 35 - old ftime syscall */
	.llong .sys_sync
	.llong .sys32_kill
	.llong .sys_rename
	.llong .sys32_mkdir
	.llong .sys_rmdir		/* 40 */
	.llong .sys_dup
	.llong .sys_pipe
	.llong .compat_sys_times
	.llong .sys_ni_syscall		/* old prof syscall */
	.llong .sys_brk			/* 45 */
	.llong .sys_setgid
	.llong .sys_getgid
	.llong .sys_signal
	.llong .sys_geteuid
	.llong .sys_getegid		/* 50 */
	.llong .sys_acct
	.llong .sys32_umount
	.llong .sys_ni_syscall		/* old lock syscall */
	.llong .compat_sys_ioctl
	.llong .compat_sys_fcntl		/* 55 */
	.llong .sys_ni_syscall		/* old mpx syscall */
	.llong .sys32_setpgid
	.llong .sys_ni_syscall		/* old ulimit syscall */
	.llong .sys32_olduname
	.llong .sys32_umask		/* 60 */
	.llong .sys_chroot
	.llong .sys_ustat
	.llong .sys_dup2
	.llong .sys_getppid
	.llong .sys_getpgrp	        /* 65 */
	.llong .sys_setsid
	.llong .sys32_sigaction
	.llong .sys_sgetmask
	.llong .sys32_ssetmask
	.llong .sys_setreuid	        /* 70 */
	.llong .sys_setregid
	.llong .sys32_sigsuspend
	.llong .compat_sys_sigpending
	.llong .sys32_sethostname
	.llong .compat_sys_setrlimit	        /* 75 */
	.llong .compat_sys_old_getrlimit
	.llong .compat_sys_getrusage
	.llong .sys32_gettimeofday
	.llong .sys32_settimeofday
	.llong .sys32_getgroups	        /* 80 */
	.llong .sys32_setgroups
	.llong .sys_ni_syscall		/* old select syscall */
	.llong .sys_symlink
	.llong .sys_ni_syscall		/* old lstat syscall */
	.llong .sys32_readlink	        /* 85 */
	.llong .sys_uselib
	.llong .sys_swapon
	.llong .sys_reboot
	.llong .old32_readdir
	.llong .sys_mmap		/* 90 */
	.llong .sys_munmap
	.llong .sys_truncate
	.llong .sys_ftruncate
	.llong .sys_fchmod
	.llong .sys_fchown              /* 95 */
	.llong .sys32_getpriority
	.llong .sys32_setpriority
	.llong .sys_ni_syscall		/* old profil syscall */
	.llong .compat_sys_statfs
	.llong .compat_sys_fstatfs		/* 100 */
	.llong .sys_ni_syscall		/* old ioperm syscall */
	.llong .compat_sys_socketcall
	.llong .sys32_syslog
	.llong .compat_sys_setitimer
	.llong .compat_sys_getitimer		/* 105 */
	.llong .compat_sys_newstat
	.llong .compat_sys_newlstat
	.llong .compat_sys_newfstat
	.llong .sys_uname
	.llong .sys_ni_syscall		/* 110 old iopl syscall */
	.llong .sys_vhangup
	.llong .sys_ni_syscall		/* old idle syscall */
	.llong .sys_ni_syscall		/* old vm86 syscall */
	.llong .compat_sys_wait4
	.llong .sys_swapoff		/* 115 */
	.llong .sys32_sysinfo
	.llong .sys32_ipc
	.llong .sys_fsync
	.llong .ppc32_sigreturn
	.llong .sys_clone		/* 120 */
	.llong .sys32_setdomainname
	.llong .ppc64_newuname
	.llong .sys_ni_syscall		/* old modify_ldt syscall */
	.llong .sys32_adjtimex
	.llong .sys_mprotect		/* 125 */
	.llong .compat_sys_sigprocmask
	.llong .sys_ni_syscall		/* old create_module syscall */
	.llong .sys_init_module
	.llong .sys_delete_module
	.llong .sys_ni_syscall		/* 130 old get_kernel_syms syscall */
	.llong .sys_quotactl
	.llong .sys32_getpgid
	.llong .sys_fchdir
	.llong .sys_bdflush
	.llong .sys32_sysfs		/* 135 */
	.llong .ppc64_personality
	.llong .sys_ni_syscall	        /* for afs_syscall */
	.llong .sys_setfsuid
	.llong .sys_setfsgid
	.llong .sys_llseek	        /* 140 */
        .llong .sys32_getdents
	.llong .ppc32_select
	.llong .sys_flock
	.llong .sys32_msync
	.llong .sys32_readv		/* 145 */
	.llong .sys32_writev
	.llong .sys32_getsid
	.llong .sys_fdatasync
	.llong .sys32_sysctl
	.llong .sys_mlock		/* 150 */
	.llong .sys_munlock
	.llong .sys32_mlockall
	.llong .sys_munlockall
	.llong .sys32_sched_setparam
	.llong .sys32_sched_getparam	/* 155 */
	.llong .sys32_sched_setscheduler
	.llong .sys32_sched_getscheduler
	.llong .sys_sched_yield
	.llong .sys32_sched_get_priority_max
	.llong .sys32_sched_get_priority_min  /* 160 */
	.llong .sys32_sched_rr_get_interval
	.llong .compat_sys_nanosleep
	.llong .sys_mremap
	.llong .sys_setresuid
	.llong .sys_getresuid	        /* 165 */
	.llong .sys_ni_syscall		/* old query_module syscall */
	.llong .sys_poll
	.llong .sys32_nfsservctl
	.llong .sys_setresgid
	.llong .sys_getresgid	        /* 170 */
	.llong .sys32_prctl
	.llong .ppc32_rt_sigreturn
	.llong .sys32_rt_sigaction
	.llong .sys32_rt_sigprocmask
	.llong .sys32_rt_sigpending     /* 175 */
	.llong .sys32_rt_sigtimedwait
	.llong .sys32_rt_sigqueueinfo
	.llong .sys32_rt_sigsuspend
	.llong .sys32_pread64
	.llong .sys32_pwrite64	        /* 180 */
	.llong .sys_chown
	.llong .sys_getcwd
	.llong .sys_capget
	.llong .sys_capset
	.llong .sys32_sigaltstack	/* 185 */
	.llong .sys32_sendfile
	.llong .sys_ni_syscall		/* reserved for streams1 */
	.llong .sys_ni_syscall		/* reserved for streams2 */
	.llong .sys_vfork
	.llong .compat_sys_getrlimit	        /* 190 */
	.llong .sys32_readahead
	.llong .sys32_mmap2
	.llong .sys32_truncate64
	.llong .sys32_ftruncate64
	.llong .sys_stat64      	/* 195 */
	.llong .sys_lstat64
	.llong .sys_fstat64
	.llong .sys32_pciconfig_read
	.llong .sys32_pciconfig_write
	.llong .sys_ni_syscall		/* 200 - old pciconfig_iobase */
	.llong .sys_ni_syscall		/* reserved for MacOnLinux */
	.llong .sys_getdents64
	.llong .sys_pivot_root
	.llong .compat_sys_fcntl64
	.llong .sys_madvise		/* 205 */
	.llong .sys_mincore
	.llong .sys_gettid
	.llong .sys_tkill
	.llong .sys_setxattr
	.llong .sys_lsetxattr		/* 210 */
	.llong .sys_fsetxattr
	.llong .sys_getxattr
	.llong .sys_lgetxattr
	.llong .sys_fgetxattr
	.llong .sys_listxattr		/* 215 */
	.llong .sys_llistxattr
	.llong .sys_flistxattr
	.llong .sys_removexattr
	.llong .sys_lremovexattr
	.llong .sys_fremovexattr	/* 220 */
	.llong .compat_sys_futex
	.llong .compat_sys_sched_setaffinity
	.llong .compat_sys_sched_getaffinity
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* 225 - reserved for tux */
	.llong .sys32_sendfile64
	.llong .sys32_io_setup
	.llong .sys_io_destroy
	.llong .sys32_io_getevents
	.llong .sys32_io_submit
	.llong .sys_io_cancel
	.llong .sys_set_tid_address
	.llong .ppc32_fadvise64
	.llong .sys_exit_group
	.llong .ppc32_lookup_dcookie	/* 235 */
	.llong .sys_epoll_create
	.llong .sys_epoll_ctl
	.llong .sys_epoll_wait
	.llong .sys_remap_file_pages

	.balign 8
_GLOBAL(sys_call_table)
	.llong .sys_restart_syscall	/* 0 */
	.llong .sys_exit
	.llong .sys_fork
	.llong .sys_read
	.llong .sys_write
	.llong .sys_open		/* 5 */
	.llong .sys_close
	.llong .sys_waitpid
	.llong .sys_creat
	.llong .sys_link
	.llong .sys_unlink		/* 10 */
	.llong .sys_execve
	.llong .sys_chdir
	.llong .sys64_time
	.llong .sys_mknod
	.llong .sys_chmod		/* 15 */
	.llong .sys_lchown
	.llong .sys_ni_syscall		/* old break syscall */
	.llong .sys_ni_syscall		/* old stat syscall */
	.llong .sys_lseek
	.llong .sys_getpid		/* 20 */
	.llong .sys_mount
	.llong .sys_ni_syscall		/* old umount syscall */
	.llong .sys_setuid
	.llong .sys_getuid
	.llong .ppc64_sys_stime		/* 25 */
	.llong .sys_ptrace
	.llong .sys_alarm
	.llong .sys_ni_syscall		/* old fstat syscall */
	.llong .sys_pause
	.llong .sys_utime		/* 30 */
	.llong .sys_ni_syscall		/* old stty syscall */
	.llong .sys_ni_syscall		/* old gtty syscall */
	.llong .sys_access
	.llong .sys_nice
	.llong .sys_ni_syscall		/* 35 - old ftime syscall */
	.llong .sys_sync
	.llong .sys_kill
	.llong .sys_rename
	.llong .sys_mkdir
	.llong .sys_rmdir		/* 40 */
	.llong .sys_dup
	.llong .sys_pipe
	.llong .sys_times
	.llong .sys_ni_syscall		/* old prof syscall */
	.llong .sys_brk			/* 45 */
	.llong .sys_setgid
	.llong .sys_getgid
	.llong .sys_signal
	.llong .sys_geteuid
	.llong .sys_getegid		/* 50 */
	.llong .sys_acct
	.llong .sys_umount
	.llong .sys_ni_syscall		/* old lock syscall */
	.llong .sys_ioctl
	.llong .sys_fcntl		/* 55 */
	.llong .sys_ni_syscall		/* old mpx syscall */
	.llong .sys_setpgid
	.llong .sys_ni_syscall		/* old ulimit syscall */
	.llong .sys_ni_syscall		/* old uname syscall */
	.llong .sys_umask		/* 60 */
	.llong .sys_chroot
	.llong .sys_ustat
	.llong .sys_dup2
	.llong .sys_getppid
	.llong .sys_getpgrp		/* 65 */
	.llong .sys_setsid
	.llong .sys_ni_syscall
	.llong .sys_sgetmask
	.llong .sys_ssetmask
	.llong .sys_setreuid		/* 70 */
	.llong .sys_setregid
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall
	.llong .sys_sethostname
	.llong .sys_setrlimit		/* 75 */
	.llong .sys_ni_syscall		/* old getrlimit syscall */
	.llong .sys_getrusage
	.llong .sys_gettimeofday
	.llong .sys_settimeofday
	.llong .sys_getgroups		/* 80 */
	.llong .sys_setgroups
	.llong .sys_ni_syscall		/* old select syscall */
	.llong .sys_symlink
	.llong .sys_ni_syscall		/* old lstat syscall */
	.llong .sys_readlink		/* 85 */
	.llong .sys_uselib
	.llong .sys_swapon
	.llong .sys_reboot
	.llong .sys_ni_syscall		/* old readdir syscall */
	.llong .sys_mmap		/* 90 */
	.llong .sys_munmap
	.llong .sys_truncate
	.llong .sys_ftruncate
	.llong .sys_fchmod
	.llong .sys_fchown		/* 95 */
	.llong .sys_getpriority
	.llong .sys_setpriority
	.llong .sys_ni_syscall		/* old profil syscall holder */
	.llong .sys_statfs
	.llong .sys_fstatfs		/* 100 */
	.llong .sys_ni_syscall		/* old ioperm syscall */
	.llong .sys_socketcall
	.llong .sys_syslog
	.llong .sys_setitimer
	.llong .sys_getitimer		/* 105 */
	.llong .sys_newstat
	.llong .sys_newlstat
	.llong .sys_newfstat
	.llong .sys_ni_syscall		/* old uname syscall */
	.llong .sys_ni_syscall		/* 110 old iopl syscall */
	.llong .sys_vhangup
	.llong .sys_ni_syscall		/* old idle syscall */
	.llong .sys_ni_syscall		/* old vm86 syscall */
	.llong .sys_wait4
	.llong .sys_swapoff		/* 115 */
	.llong .sys_sysinfo
	.llong .sys_ipc
	.llong .sys_fsync
	.llong .sys_ni_syscall
	.llong .sys_clone		/* 120 */
	.llong .sys_setdomainname
	.llong .ppc64_newuname
	.llong .sys_ni_syscall		/* old modify_ldt syscall */
	.llong .sys_adjtimex
	.llong .sys_mprotect		/* 125 */
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* old create_module syscall */
	.llong .sys_init_module
	.llong .sys_delete_module
	.llong .sys_ni_syscall		/* 130 old get_kernel_syms syscall */
	.llong .sys_quotactl
	.llong .sys_getpgid
	.llong .sys_fchdir
	.llong .sys_bdflush
	.llong .sys_sysfs		/* 135 */
	.llong .ppc64_personality
	.llong .sys_ni_syscall	        /* for afs_syscall */
	.llong .sys_setfsuid
	.llong .sys_setfsgid
	.llong .sys_llseek	        /* 140 */
        .llong .sys_getdents
	.llong .sys_select
	.llong .sys_flock
	.llong .sys_msync
	.llong .sys_readv		/* 145 */
	.llong .sys_writev
	.llong .sys_getsid
	.llong .sys_fdatasync
	.llong .sys_sysctl
	.llong .sys_mlock		/* 150 */
	.llong .sys_munlock
	.llong .sys_mlockall
	.llong .sys_munlockall
	.llong .sys_sched_setparam
	.llong .sys_sched_getparam	/* 155 */
	.llong .sys_sched_setscheduler
	.llong .sys_sched_getscheduler
	.llong .sys_sched_yield
	.llong .sys_sched_get_priority_max
	.llong .sys_sched_get_priority_min  /* 160 */
	.llong .sys_sched_rr_get_interval
	.llong .sys_nanosleep
	.llong .sys_mremap
	.llong .sys_setresuid
	.llong .sys_getresuid	        /* 165 */
	.llong .sys_ni_syscall		/* old query_module syscall */
	.llong .sys_poll
	.llong .sys_nfsservctl
	.llong .sys_setresgid
	.llong .sys_getresgid	        /* 170 */
	.llong .sys_prctl
	.llong .ppc64_rt_sigreturn
	.llong .sys_rt_sigaction
	.llong .sys_rt_sigprocmask	
	.llong .sys_rt_sigpending	/* 175 */
	.llong .sys_rt_sigtimedwait
	.llong .sys_rt_sigqueueinfo
	.llong .sys_rt_sigsuspend
	.llong .sys_pread64
	.llong .sys_pwrite64	        /* 180 */
	.llong .sys_chown
	.llong .sys_getcwd
	.llong .sys_capget
	.llong .sys_capset
	.llong .sys_sigaltstack	        /* 185 */
	.llong .sys_sendfile
	.llong .sys_ni_syscall		/* reserved for streams1 */
	.llong .sys_ni_syscall		/* reserved for streams2 */
	.llong .sys_vfork
	.llong .sys_getrlimit	        /* 190 */
	.llong .sys_readahead
	.llong .sys_ni_syscall		/* 32bit only mmap2 */
	.llong .sys_ni_syscall		/* 32bit only truncate64 */
	.llong .sys_ni_syscall		/* 32bit only ftruncate64 */
	.llong .sys_ni_syscall		/* 195 - 32bit only stat64 */
	.llong .sys_ni_syscall		/* 32bit only lstat64 */
	.llong .sys_ni_syscall		/* 32bit only fstat64 */
	.llong .sys_ni_syscall		/* 32bit only pciconfig_read */
	.llong .sys_ni_syscall		/* 32bit only pciconfig_write */
	.llong .sys_ni_syscall		/* 200 - old pciconfig_iobase */
	.llong .sys_ni_syscall		/* reserved for MacOnLinux */
	.llong .sys_getdents64
	.llong .sys_pivot_root
	.llong .sys_ni_syscall		/* 32bit only fcntl64 */
	.llong .sys_madvise		/* 205 */
	.llong .sys_mincore
	.llong .sys_gettid
	.llong .sys_tkill
	.llong .sys_setxattr
	.llong .sys_lsetxattr		/* 210 */
	.llong .sys_fsetxattr
	.llong .sys_getxattr
	.llong .sys_lgetxattr
	.llong .sys_fgetxattr
	.llong .sys_listxattr		/* 215 */
	.llong .sys_llistxattr
	.llong .sys_flistxattr
	.llong .sys_removexattr
	.llong .sys_lremovexattr
	.llong .sys_fremovexattr	/* 220 */
	.llong .sys_futex
	.llong .sys_sched_setaffinity
	.llong .sys_sched_getaffinity
	.llong .sys_ni_syscall
	.llong .sys_ni_syscall		/* 225 - reserved for tux */
	.llong .sys_ni_syscall		/* 32bit only sendfile64 */
	.llong .sys_io_setup
	.llong .sys_io_destroy
	.llong .sys_io_getevents
	.llong .sys_io_submit		/* 230 */
	.llong .sys_io_cancel
	.llong .sys_set_tid_address
	.llong .sys_fadvise64
	.llong .sys_exit_group
	.llong .sys_lookup_dcookie	/* 235 */
	.llong .sys_epoll_create
	.llong .sys_epoll_ctl
	.llong .sys_epoll_wait
	.llong .sys_remap_file_pages
