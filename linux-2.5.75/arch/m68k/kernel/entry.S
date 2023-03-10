/* -*- mode: asm -*-
 *
 *  linux/arch/m68k/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 *
 */

/*
 * entry.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 */

/*
 * 12/03/96 Jes: Currently we only support m68k single-cpu systems, so
 *               all pointers that used to be 'current' are now entry
 *               number 0 in the 'current_set' list.
 *
 *  6/05/00 RZ:	 addedd writeback completion after return from sighandler
 *		 for 68040
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/traps.h>
#include <asm/unistd.h>

#include "m68k_defs.h"

.globl system_call, buserr, trap
.globl resume, ret_from_exception
.globl ret_from_signal
.globl inthandler, sys_call_table
.globl sys_fork, sys_clone, sys_vfork
.globl ret_from_interrupt, bad_interrupt

.text
ENTRY(buserr)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@- 		| stack frame pointer argument
	bsrl	buserr_c
	addql	#4,%sp
	jra	ret_from_exception

ENTRY(trap)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@- 		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	jra	ret_from_exception

	| schedule_tail is only used with CONFIG_SMP
ENTRY(ret_from_fork)
#ifdef CONFIG_SMP
	jsr	schedule_tail
#endif
	jra	ret_from_exception

badsys:
	movel	#-ENOSYS,%sp@(PT_D0)
	jra	ret_from_exception

do_trace:
	movel	#-ENOSYS,%sp@(PT_D0)	| needed for strace
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%sp@(PT_ORIG_D0),%d1
	movel	#-ENOSYS,%d0
	cmpl	#NR_syscalls,%d1
	jcc	1f
	jbsr	@(sys_call_table,%d1:l:4)@(0)
1:	movel	%d0,%sp@(PT_D0)		| save the return value
	subql	#4,%sp			| dummy return address
	SAVE_SWITCH_STACK
	jbsr	syscall_trace

ret_from_signal:
	RESTORE_SWITCH_STACK 
	addql	#4,%sp
/* on 68040 complete pending writebacks if any */	
#ifdef CONFIG_M68040
	bfextu	%sp@(PT_VECTOR){#0,#4},%d0  
	subql	#7,%d0				| bus error frame ?
	jbne	1f
	movel	%sp,%sp@-
	jbsr	berr_040cleanup
	addql	#4,%sp
1:	
#endif	
	jra	ret_from_exception

ENTRY(system_call)
	SAVE_ALL_SYS

	GET_CURRENT(%d1)
	| save top of frame
	movel	%sp,%curptr@(TASK_THREAD+THREAD_ESP0)

	tstb	%curptr@(TASK_SYSCALL_TRACE)
	jne	do_trace
	cmpl	#NR_syscalls,%d0
	jcc	badsys
	jbsr	@(sys_call_table,%d0:l:4)@(0)
	movel	%d0,%sp@(PT_D0)		| save the return value

	|oriw	#0x0700,%sr
	movel	%curptr@(TASK_WORK),%d0
	jne	syscall_exit_work
1:	RESTORE_ALL

syscall_exit_work:
	btst	#5,%sp@(PT_SR)		| check if returning to kernel
	bnes	1b			| if so, skip resched, signals
	tstw	%d0
	jeq	do_signal_return
	tstb	%d0
	jne	do_delayed_trace

	pea	resume_userspace
	jmp	schedule

ret_from_exception:
	btst	#5,%sp@(PT_SR)		| check if returning to kernel
	bnes	1f			| if so, skip resched, signals
	| only allow interrupts when we are really the last one on the
	| kernel stack, otherwise stack overflow can occur during
	| heavy interrupt load
	andw	#ALLOWINT,%sr

resume_userspace:
	movel	%curptr@(TASK_WORK),%d0
	lsrl	#8,%d0
	jne	exit_work
1:	RESTORE_ALL

exit_work:
	| save top of frame
	movel	%sp,%curptr@(TASK_THREAD+THREAD_ESP0)
	tstb	%d0
	jeq	do_signal_return

	pea	resume_userspace
	jmp	schedule

do_signal_return:
	|andw	#ALLOWINT,%sr
	subql	#4,%sp			| dummy return address
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	clrl	%sp@-
	bsrl	do_signal
	addql	#8,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jbra	resume_userspace

do_delayed_trace:
	bclr	#7,%sp@(PT_SR)		| clear trace bit in SR
	pea	1			| send SIGTRAP
	movel	%curptr,%sp@-
	pea	LSIGTRAP
	jbsr	send_sig
	addql	#8,%sp
	addql	#4,%sp
	jbra	resume_userspace


#if 0
#ifdef CONFIG_AMIGA
ami_inthandler:
	addql	#1,irq_stat+CPUSTAT_LOCAL_IRQ_COUNT
	SAVE_ALL_INT
	GET_CURRENT(%d0)

	bfextu	%sp@(PT_VECTOR){#4,#12},%d0
	movel	%d0,%a0
	addql	#1,%a0@(kstat+STAT_IRQ-VECOFF(VEC_SPUR))
	movel	%a0@(autoirq_list-VECOFF(VEC_SPUR)),%a0

| amiga vector int handler get the req mask instead of irq vector
	lea	CUSTOMBASE,%a1
	movew	%a1@(C_INTREQR),%d0
	andw	%a1@(C_INTENAR),%d0

| prepare stack (push frame pointer, dev_id & req mask)
	pea	%sp@
	movel	%a0@(IRQ_DEVID),%sp@-
	movel	%d0,%sp@-
	pea	%pc@(ret_from_interrupt:w)
	jbra	@(IRQ_HANDLER,%a0)@(0)

ENTRY(nmi_handler)
	rte
#endif
#endif

/*
** This is the main interrupt handler, responsible for calling process_int()
*/
inthandler:
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	addqb	#1,%curptr@(TASK_INFO+TINFO_PREEMPT+2)
					|  put exception # in d0
	bfextu %sp@(PT_VECTOR){#4,#10},%d0

	movel	%sp,%sp@-
	movel	%d0,%sp@- 		|  put vector # on stack
#if defined(MACH_Q40_ONLY) && defined(CONFIG_BLK_DEV_FD)
	btstb	#4,0xff000000		| Q40 floppy needs very special treatment ...
	jbeq	1f
	btstb	#3,0xff000004		
	jbeq	1f
	jbsr	floppy_hardint
	jbra	3f
1:
#endif		
	jbsr	process_int		|  process the IRQ
3:     	addql	#8,%sp			|  pop parameters off stack

ret_from_interrupt:
	subqb	#1,%curptr@(TASK_INFO+TINFO_PREEMPT+2)
	jeq	1f
2:
	RESTORE_ALL
1:
	moveq	#(~ALLOWINT>>8)&0xff,%d0
	andb	%sp@(PT_SR),%d0
	jne	2b

	/* check if we need to do software interrupts */
	tstl	irq_stat+CPUSTAT_SOFTIRQ_PENDING
	jeq	ret_from_exception
	pea	ret_from_exception
	jra	do_softirq


/* Handler for uninitialized and spurious interrupts */

bad_interrupt:
	addql	#1,num_spurious
	rte

ENTRY(sys_fork)
	SAVE_SWITCH_STACK	
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_fork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_clone)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_clone
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_vfork)
	SAVE_SWITCH_STACK	
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_vfork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigsuspend)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	do_sigsuspend
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_rt_sigsuspend)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	do_rt_sigsuspend
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_sigreturn
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	rts

resume:
	/*
	 * Beware - when entering resume, prev (the current task) is
	 * in a0, next (the new task) is in a1,so don't change these
	 * registers until their contents are no longer needed.
	 */

	/* save sr */
	movew	%sr,%a0@(TASK_THREAD+THREAD_SR)

	/* save fs (sfc,%dfc) (may be pointing to kernel memory) */
	movec	%sfc,%d0
	movew	%d0,%a0@(TASK_THREAD+THREAD_FS)

	/* save usp */
	/* it is better to use a movel here instead of a movew 8*) */
	movec	%usp,%d0
	movel	%d0,%a0@(TASK_THREAD+THREAD_USP)

	/* save non-scratch registers on stack */
	SAVE_SWITCH_STACK

	/* save current kernel stack pointer */
	movel	%sp,%a0@(TASK_THREAD+THREAD_KSP)

	/* save floating point context */
#ifndef CONFIG_M68KFPU_EMU_ONLY
#ifdef CONFIG_M68KFPU_EMU
	tstl	m68k_fputype
	jeq	3f
#endif
	fsave	%a0@(TASK_THREAD+THREAD_FPSTATE)

#if defined(CONFIG_M68060)
#if !defined(CPU_M68060_ONLY)
	btst	#3,m68k_cputype+3
	beqs	1f
#endif
	/* The 060 FPU keeps status in bits 15-8 of the first longword */
	tstb	%a0@(TASK_THREAD+THREAD_FPSTATE+2)
	jeq	3f
#if !defined(CPU_M68060_ONLY)
	jra	2f
#endif
#endif /* CONFIG_M68060 */
#if !defined(CPU_M68060_ONLY)
1:	tstb	%a0@(TASK_THREAD+THREAD_FPSTATE)
	jeq	3f
#endif
2:	fmovemx	%fp0-%fp7,%a0@(TASK_THREAD+THREAD_FPREG)
	fmoveml	%fpcr/%fpsr/%fpiar,%a0@(TASK_THREAD+THREAD_FPCNTL)
3:
#endif	/* CONFIG_M68KFPU_EMU_ONLY */
	/* Return previous task in %d1 */
	movel	%curptr,%d1

	/* switch to new task (a1 contains new task) */
	movel	%a1,%curptr

	/* restore floating point context */
#ifndef CONFIG_M68KFPU_EMU_ONLY
#ifdef CONFIG_M68KFPU_EMU
	tstl	m68k_fputype
	jeq	4f
#endif
#if defined(CONFIG_M68060)
#if !defined(CPU_M68060_ONLY)
	btst	#3,m68k_cputype+3
	beqs	1f
#endif
	/* The 060 FPU keeps status in bits 15-8 of the first longword */
	tstb	%a1@(TASK_THREAD+THREAD_FPSTATE+2)
	jeq	3f
#if !defined(CPU_M68060_ONLY)
	jra	2f
#endif
#endif /* CONFIG_M68060 */
#if !defined(CPU_M68060_ONLY)
1:	tstb	%a1@(TASK_THREAD+THREAD_FPSTATE)
	jeq	3f
#endif	
2:	fmovemx	%a1@(TASK_THREAD+THREAD_FPREG),%fp0-%fp7
	fmoveml	%a1@(TASK_THREAD+THREAD_FPCNTL),%fpcr/%fpsr/%fpiar
3:	frestore %a1@(TASK_THREAD+THREAD_FPSTATE)
4:
#endif	/* CONFIG_M68KFPU_EMU_ONLY */

	/* restore the kernel stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_KSP),%sp

	/* restore non-scratch registers */
	RESTORE_SWITCH_STACK

	/* restore user stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_USP),%a0
	movel	%a0,%usp

	/* restore fs (sfc,%dfc) */
	movew	%a1@(TASK_THREAD+THREAD_FS),%a0
	movec	%a0,%sfc
	movec	%a0,%dfc

	/* restore status register */
	movew	%a1@(TASK_THREAD+THREAD_SR),%sr

	rts

.data
ALIGN
sys_call_table:
	.long sys_ni_syscall	/* 0  -  old "setup()" system call*/
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_chown16
	.long sys_ni_syscall				/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid16
	.long sys_getuid16
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall				/* old stty syscall holder */
	.long sys_ni_syscall				/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall				/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid16
	.long sys_getgid16
	.long sys_signal
	.long sys_geteuid16
	.long sys_getegid16	/* 50 */
	.long sys_acct
	.long sys_umount				/* recycled never used phys() */
	.long sys_ni_syscall				/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall				/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall				/* old ulimit syscall holder */
	.long sys_ni_syscall
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid16	/* 70 */
	.long sys_setregid16
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_old_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups16	/* 80 */
	.long sys_setgroups16
	.long old_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir
	.long old_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown16	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall				/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ioperm
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall
	.long sys_ni_syscall	/* 110 */	/* iopl for i386 */
	.long sys_vhangup
	.long sys_ni_syscall				/* obsolete idle() syscall */
	.long sys_ni_syscall				/* vm86old for i386 */
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_cacheflush				/* modify_ldt for i386 */
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_ni_syscall		/* old "create_module" */ 
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall	/* 130 - old "get_kernel_syms" */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall				/* for afs_syscall */
	.long sys_setfsuid16
	.long sys_setfsgid16
	.long sys_llseek	/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid16
	.long sys_getresuid16	/* 165 */
	.long sys_getpagesize
	.long sys_ni_syscall		/* old sys_query_module */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid16	/* 170 */
	.long sys_getresgid16
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64	/* 180 */
	.long sys_pwrite64
	.long sys_lchown16;
	.long sys_getcwd
	.long sys_capget
	.long sys_capset	/* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall				/* streams1 */
	.long sys_ni_syscall				/* streams2 */
	.long sys_vfork		/* 190 */
	.long sys_getrlimit
	.long sys_mmap2
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64	/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_chown
	.long sys_getuid
	.long sys_getgid	/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups	/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid	/* 210 */
	.long sys_getresgid
	.long sys_lchown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid	/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_ni_syscall
	.long sys_ni_syscall
	.long sys_getdents64	/* 220 */
	.long sys_gettid
	.long sys_tkill
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr	/* 225 */
	.long sys_getxattr
	.long sys_lgetxattr
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr	/* 230 */
	.long sys_flistxattr
	.long sys_removexattr
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_futex		/* 235 */
