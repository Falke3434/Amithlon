/*
 * ia64/kernel/entry.S
 *
 * Kernel entry points.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999, 2002-2003
 *	Asit Mallick <Asit.K.Mallick@intel.com>
 * 	Don Dugger <Don.Dugger@intel.com>
 *	Suresh Siddha <suresh.b.siddha@intel.com>
 *	Fenghua Yu <fenghua.yu@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 */
/*
 * ia64_switch_to now places correct virtual mapping in in TR2 for
 * kernel stack. This allows us to handle interrupts without changing
 * to physical mode.
 *
 * Jonathan Nicklin	<nicklin@missioncriticallinux.com>
 * Patrick O'Rourke	<orourke@missioncriticallinux.com>
 * 11/07/2000
 /
/*
 * Global (preserved) predicate usage on syscall entry/exit path:
 *
 *	pKStk:		See entry.h.
 *	pUStk:		See entry.h.
 *	pSys:		See entry.h.
 *	pNonSys:	!pSys
 */

#include <linux/config.h>

#include <asm/asmmacro.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/kregs.h>
#include <asm/offsets.h>
#include <asm/pgtable.h>
#include <asm/percpu.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

#include "minstate.h"

	/*
	 * execve() is special because in case of success, we need to
	 * setup a null register window frame.
	 */
ENTRY(ia64_execve)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(3)
	alloc loc1=ar.pfs,3,2,4,0
	mov loc0=rp
	.body
	mov out0=in0			// filename
	;;				// stop bit between alloc and call
	mov out1=in1			// argv
	mov out2=in2			// envp
	add out3=16,sp			// regs
	br.call.sptk.many rp=sys_execve
.ret0:	cmp4.ge p6,p7=r8,r0
	mov ar.pfs=loc1			// restore ar.pfs
	sxt4 r8=r8			// return 64-bit result
	;;
	stf.spill [sp]=f0
(p6)	cmp.ne pKStk,pUStk=r0,r0	// a successful execve() lands us in user-mode...
	mov rp=loc0
(p6)	mov ar.pfs=r0			// clear ar.pfs on success
(p7)	br.ret.sptk.many rp

	/*
	 * In theory, we'd have to zap this state only to prevent leaking of
	 * security sensitive state (e.g., if current->mm->dumpable is zero).  However,
	 * this executes in less than 20 cycles even on Itanium, so it's not worth
	 * optimizing for...).
	 */
	mov ar.unat=0; 		mov ar.lc=0
	mov r4=0;		mov f2=f0;		mov b1=r0
	mov r5=0;		mov f3=f0;		mov b2=r0
	mov r6=0;		mov f4=f0;		mov b3=r0
	mov r7=0;		mov f5=f0;		mov b4=r0
	ldf.fill f12=[sp];	mov f13=f0;		mov b5=r0
	ldf.fill f14=[sp];	ldf.fill f15=[sp];	mov f16=f0
	ldf.fill f17=[sp];	ldf.fill f18=[sp];	mov f19=f0
	ldf.fill f20=[sp];	ldf.fill f21=[sp];	mov f22=f0
	ldf.fill f23=[sp];	ldf.fill f24=[sp];	mov f25=f0
	ldf.fill f26=[sp];	ldf.fill f27=[sp];	mov f28=f0
	ldf.fill f29=[sp];	ldf.fill f30=[sp];	mov f31=f0
	br.ret.sptk.many rp
END(ia64_execve)

/*
 * sys_clone2(u64 flags, u64 ustack_base, u64 ustack_size, u64 parent_tidptr, u64 child_tidptr,
 *	      u64 tls)
 */
GLOBAL_ENTRY(sys_clone2)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	alloc r16=ar.pfs,6,2,6,0
	DO_SAVE_SWITCH_STACK
	adds r2=PT(R16)+IA64_SWITCH_STACK_SIZE+16,sp
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	.body
	mov out1=in1
	mov out3=in2
	tbit.nz p6,p0=in0,CLONE_SETTLS_BIT
	mov out4=in3	// parent_tidptr: valid only w/CLONE_PARENT_SETTID
	;;
(p6)	st8 [r2]=in5				// store TLS in r16 for copy_thread()
	mov out5=in4	// child_tidptr:  valid only w/CLONE_CHILD_SETTID or CLONE_CHILD_CLEARTID
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	dep out0=0,in0,CLONE_IDLETASK_BIT,1	// out0 = clone_flags & ~CLONE_IDLETASK
	br.call.sptk.many rp=do_fork
.ret1:	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(sys_clone2)

/*
 * sys_clone(u64 flags, u64 ustack_base, u64 parent_tidptr, u64 child_tidptr, u64 tls)
 *	Deprecated.  Use sys_clone2() instead.
 */
GLOBAL_ENTRY(sys_clone)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	alloc r16=ar.pfs,5,2,6,0
	DO_SAVE_SWITCH_STACK
	adds r2=PT(R16)+IA64_SWITCH_STACK_SIZE+16,sp
	mov loc0=rp
	mov loc1=r16				// save ar.pfs across do_fork
	.body
	mov out1=in1
	mov out3=16				// stacksize (compensates for 16-byte scratch area)
	tbit.nz p6,p0=in0,CLONE_SETTLS_BIT
	mov out4=in2	// parent_tidptr: valid only w/CLONE_PARENT_SETTID
	;;
(p6)	st8 [r2]=in4				// store TLS in r13 (tp)
	mov out5=in3	// child_tidptr:  valid only w/CLONE_CHILD_SETTID or CLONE_CHILD_CLEARTID
	adds out2=IA64_SWITCH_STACK_SIZE+16,sp	// out2 = &regs
	dep out0=0,in0,CLONE_IDLETASK_BIT,1	// out0 = clone_flags & ~CLONE_IDLETASK
	br.call.sptk.many rp=do_fork
.ret2:	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE,sp	// pop the switch stack
	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(sys_clone)

/*
 * prev_task <- ia64_switch_to(struct task_struct *next)
 *	With Ingo's new scheduler, interrupts are disabled when this routine gets
 *	called.  The code starting at .map relies on this.  The rest of the code
 *	doesn't care about the interrupt masking status.
 */
GLOBAL_ENTRY(ia64_switch_to)
	.prologue
	alloc r16=ar.pfs,1,0,0,0
	DO_SAVE_SWITCH_STACK
	.body

	adds r22=IA64_TASK_THREAD_KSP_OFFSET,r13
	mov r27=IA64_KR(CURRENT_STACK)
	dep r20=0,in0,61,3		// physical address of "current"
	;;
	st8 [r22]=sp			// save kernel stack pointer of old task
	shr.u r26=r20,IA64_GRANULE_SHIFT
	adds r21=IA64_TASK_THREAD_KSP_OFFSET,in0
	;;
	/*
	 * If we've already mapped this task's page, we can skip doing it again.
	 */
	cmp.eq p7,p6=r26,r27
(p6)	br.cond.dpnt .map
	;;
.done:
(p6)	ssm psr.ic			// if we we had to map, renable the psr.ic bit FIRST!!!
	;;
(p6)	srlz.d
	ld8 sp=[r21]			// load kernel stack pointer of new task
	mov IA64_KR(CURRENT)=in0	// update "current" application register
	mov r8=r13			// return pointer to previously running task
	mov r13=in0			// set "current" pointer
	;;
	DO_LOAD_SWITCH_STACK

#ifdef CONFIG_SMP
	sync.i				// ensure "fc"s done by this CPU are visible on other CPUs
#endif
	br.ret.sptk.many rp		// boogie on out in new context

.map:
	rsm psr.ic			// interrupts (psr.i) are already disabled here
	movl r25=PAGE_KERNEL
	;;
	srlz.d
	or r23=r25,r20			// construct PA | page properties
	mov r25=IA64_GRANULE_SHIFT<<2
	;;
	mov cr.itir=r25
	mov cr.ifa=in0			// VA of next task...
	;;
	mov r25=IA64_TR_CURRENT_STACK
	mov IA64_KR(CURRENT_STACK)=r26	// remember last page we mapped...
	;;
	itr.d dtr[r25]=r23		// wire in new mapping...
	br.cond.sptk .done
END(ia64_switch_to)

/*
 * Note that interrupts are enabled during save_switch_stack and load_switch_stack.  This
 * means that we may get an interrupt with "sp" pointing to the new kernel stack while
 * ar.bspstore is still pointing to the old kernel backing store area.  Since ar.rsc,
 * ar.rnat, ar.bsp, and ar.bspstore are all preserved by interrupts, this is not a
 * problem.  Also, we don't need to specify unwind information for preserved registers
 * that are not modified in save_switch_stack as the right unwind information is already
 * specified at the call-site of save_switch_stack.
 */

/*
 * save_switch_stack:
 *	- r16 holds ar.pfs
 *	- b7 holds address to return to
 *	- rp (b0) holds return address to save
 */
GLOBAL_ENTRY(save_switch_stack)
	.prologue
	.altrp b7
	flushrs			// flush dirty regs to backing store (must be first in insn group)
	.save @priunat,r17
	mov r17=ar.unat		// preserve caller's
	.body
#ifdef CONFIG_ITANIUM
	adds r2=16+128,sp
	adds r3=16+64,sp
	adds r14=SW(R4)+16,sp
	;;
	st8.spill [r14]=r4,16		// spill r4
	lfetch.fault.excl.nt1 [r3],128
	;;
	lfetch.fault.excl.nt1 [r2],128
	lfetch.fault.excl.nt1 [r3],128
	;;
	lfetch.fault.excl [r2]
	lfetch.fault.excl [r3]
	adds r15=SW(R5)+16,sp
#else
	add r2=16+3*128,sp
	add r3=16,sp
	add r14=SW(R4)+16,sp
	;;
	st8.spill [r14]=r4,SW(R6)-SW(R4)	// spill r4 and prefetch offset 0x1c0
	lfetch.fault.excl.nt1 [r3],128	//		prefetch offset 0x010
	;;
	lfetch.fault.excl.nt1 [r3],128	//		prefetch offset 0x090
	lfetch.fault.excl.nt1 [r2],128	//		prefetch offset 0x190
	;;
	lfetch.fault.excl.nt1 [r3]	//		prefetch offset 0x110
	lfetch.fault.excl.nt1 [r2]	//		prefetch offset 0x210
	adds r15=SW(R5)+16,sp
#endif
	;;
	st8.spill [r15]=r5,SW(R7)-SW(R5)	// spill r5
	mov.m ar.rsc=0			// put RSE in mode: enforced lazy, little endian, pl 0
	add r2=SW(F2)+16,sp		// r2 = &sw->f2
	;;
	st8.spill [r14]=r6,SW(B0)-SW(R6)	// spill r6
	mov.m r18=ar.fpsr		// preserve fpsr
	add r3=SW(F3)+16,sp		// r3 = &sw->f3
	;;
	stf.spill [r2]=f2,32
	mov.m r19=ar.rnat
	mov r21=b0

	stf.spill [r3]=f3,32
	st8.spill [r15]=r7,SW(B2)-SW(R7)	// spill r7
	mov r22=b1
	;;
	// since we're done with the spills, read and save ar.unat:
	mov.m r29=ar.unat
	mov.m r20=ar.bspstore
	mov r23=b2
	stf.spill [r2]=f4,32
	stf.spill [r3]=f5,32
	mov r24=b3
	;;
	st8 [r14]=r21,SW(B1)-SW(B0)		// save b0
	st8 [r15]=r23,SW(B3)-SW(B2)		// save b2
	mov r25=b4
	mov r26=b5
	;;
	st8 [r14]=r22,SW(B4)-SW(B1)		// save b1
	st8 [r15]=r24,SW(AR_PFS)-SW(B3)		// save b3
	mov r21=ar.lc		// I-unit
	stf.spill [r2]=f12,32
	stf.spill [r3]=f13,32
	;;
	st8 [r14]=r25,SW(B5)-SW(B4)		// save b4
	st8 [r15]=r16,SW(AR_LC)-SW(AR_PFS)	// save ar.pfs
	stf.spill [r2]=f14,32
	stf.spill [r3]=f15,32
	;;
	st8 [r14]=r26				// save b5
	st8 [r15]=r21				// save ar.lc
	stf.spill [r2]=f16,32
	stf.spill [r3]=f17,32
	;;
	stf.spill [r2]=f18,32
	stf.spill [r3]=f19,32
	;;
	stf.spill [r2]=f20,32
	stf.spill [r3]=f21,32
	;;
	stf.spill [r2]=f22,32
	stf.spill [r3]=f23,32
	;;
	stf.spill [r2]=f24,32
	stf.spill [r3]=f25,32
	;;
	stf.spill [r2]=f26,32
	stf.spill [r3]=f27,32
	;;
	stf.spill [r2]=f28,32
	stf.spill [r3]=f29,32
	;;
	stf.spill [r2]=f30,SW(AR_UNAT)-SW(F30)
	stf.spill [r3]=f31,SW(PR)-SW(F31)
	add r14=SW(CALLER_UNAT)+16,sp
	;;
	st8 [r2]=r29,SW(AR_RNAT)-SW(AR_UNAT)	// save ar.unat
	st8 [r14]=r17,SW(AR_FPSR)-SW(CALLER_UNAT) // save caller_unat
	mov r21=pr
	;;
	st8 [r2]=r19,SW(AR_BSPSTORE)-SW(AR_RNAT) // save ar.rnat
	st8 [r3]=r21				// save predicate registers
	;;
	st8 [r2]=r20				// save ar.bspstore
	st8 [r14]=r18				// save fpsr
	mov ar.rsc=3		// put RSE back into eager mode, pl 0
	br.cond.sptk.many b7
END(save_switch_stack)

/*
 * load_switch_stack:
 *	- "invala" MUST be done at call site (normally in DO_LOAD_SWITCH_STACK)
 *	- b7 holds address to return to
 *	- must not touch r8-r11
 */
ENTRY(load_switch_stack)
	.prologue
	.altrp b7

	.body
	lfetch.fault.nt1 [sp]
	adds r2=SW(AR_BSPSTORE)+16,sp
	adds r3=SW(AR_UNAT)+16,sp
	mov ar.rsc=0						// put RSE into enforced lazy mode
	adds r14=SW(CALLER_UNAT)+16,sp
	adds r15=SW(AR_FPSR)+16,sp
	;;
	ld8 r27=[r2],(SW(B0)-SW(AR_BSPSTORE))	// bspstore
	ld8 r29=[r3],(SW(B1)-SW(AR_UNAT))	// unat
	;;
	ld8 r21=[r2],16		// restore b0
	ld8 r22=[r3],16		// restore b1
	;;
	ld8 r23=[r2],16		// restore b2
	ld8 r24=[r3],16		// restore b3
	;;
	ld8 r25=[r2],16		// restore b4
	ld8 r26=[r3],16		// restore b5
	;;
	ld8 r16=[r2],(SW(PR)-SW(AR_PFS))	// ar.pfs
	ld8 r17=[r3],(SW(AR_RNAT)-SW(AR_LC))	// ar.lc
	;;
	ld8 r28=[r2]		// restore pr
	ld8 r30=[r3]		// restore rnat
	;;
	ld8 r18=[r14],16	// restore caller's unat
	ld8 r19=[r15],24	// restore fpsr
	;;
	ldf.fill f2=[r14],32
	ldf.fill f3=[r15],32
	;;
	ldf.fill f4=[r14],32
	ldf.fill f5=[r15],32
	;;
	ldf.fill f12=[r14],32
	ldf.fill f13=[r15],32
	;;
	ldf.fill f14=[r14],32
	ldf.fill f15=[r15],32
	;;
	ldf.fill f16=[r14],32
	ldf.fill f17=[r15],32
	;;
	ldf.fill f18=[r14],32
	ldf.fill f19=[r15],32
	mov b0=r21
	;;
	ldf.fill f20=[r14],32
	ldf.fill f21=[r15],32
	mov b1=r22
	;;
	ldf.fill f22=[r14],32
	ldf.fill f23=[r15],32
	mov b2=r23
	;;
	mov ar.bspstore=r27
	mov ar.unat=r29		// establish unat holding the NaT bits for r4-r7
	mov b3=r24
	;;
	ldf.fill f24=[r14],32
	ldf.fill f25=[r15],32
	mov b4=r25
	;;
	ldf.fill f26=[r14],32
	ldf.fill f27=[r15],32
	mov b5=r26
	;;
	ldf.fill f28=[r14],32
	ldf.fill f29=[r15],32
	mov ar.pfs=r16
	;;
	ldf.fill f30=[r14],32
	ldf.fill f31=[r15],24
	mov ar.lc=r17
	;;
	ld8.fill r4=[r14],16
	ld8.fill r5=[r15],16
	mov pr=r28,-1
	;;
	ld8.fill r6=[r14],16
	ld8.fill r7=[r15],16

	mov ar.unat=r18				// restore caller's unat
	mov ar.rnat=r30				// must restore after bspstore but before rsc!
	mov ar.fpsr=r19				// restore fpsr
	mov ar.rsc=3				// put RSE back into eager mode, pl 0
	br.cond.sptk.many b7
END(load_switch_stack)

GLOBAL_ENTRY(__ia64_syscall)
	.regstk 6,0,0,0
	mov r15=in5				// put syscall number in place
	break __BREAK_SYSCALL
	movl r2=errno
	cmp.eq p6,p7=-1,r10
	;;
(p6)	st4 [r2]=r8
(p6)	mov r8=-1
	br.ret.sptk.many rp
END(__ia64_syscall)

	/*
	 * We invoke syscall_trace through this intermediate function to
	 * ensure that the syscall input arguments are not clobbered.  We
	 * also use it to preserve b6, which contains the syscall entry point.
	 */
GLOBAL_ENTRY(invoke_syscall_trace)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,3,0,0
	mov loc0=rp
	.body
	mov loc2=b6
	;;
	br.call.sptk.many rp=syscall_trace
.ret3:	mov rp=loc0
	mov ar.pfs=loc1
	mov b6=loc2
	br.ret.sptk.many rp
END(invoke_syscall_trace)

	/*
	 * Invoke a system call, but do some tracing before and after the call.
	 * We MUST preserve the current register frame throughout this routine
	 * because some system calls (such as ia64_execve) directly
	 * manipulate ar.pfs.
	 *
	 * Input:
	 *	r15 = syscall number
	 *	b6  = syscall entry point
	 */
	.global ia64_strace_leave_kernel

GLOBAL_ENTRY(ia64_trace_syscall)
	PT_REGS_UNWIND_INFO(0)
{	/*
	 * Some versions of gas generate bad unwind info if the first instruction of a
	 * procedure doesn't go into the first slot of a bundle.  This is a workaround.
	 */
	nop.m 0
	nop.i 0
	br.call.sptk.many rp=invoke_syscall_trace // give parent a chance to catch syscall args
}
.ret6:	br.call.sptk.many rp=b6			// do the syscall
strace_check_retval:
	cmp.lt p6,p0=r8,r0			// syscall failed?
	adds r2=PT(R8)+16,sp			// r2 = &pt_regs.r8
	adds r3=PT(R10)+16,sp			// r3 = &pt_regs.r10
	mov r10=0
(p6)	br.cond.sptk strace_error		// syscall failed ->
	;;					// avoid RAW on r10
strace_save_retval:
.mem.offset 0,0; st8.spill [r2]=r8		// store return value in slot for r8
.mem.offset 8,0; st8.spill [r3]=r10		// clear error indication in slot for r10
ia64_strace_leave_kernel:
	br.call.sptk.many rp=invoke_syscall_trace // give parent a chance to catch return value
.rety:	br.cond.sptk ia64_leave_syscall

strace_error:
	ld8 r3=[r2]				// load pt_regs.r8
	sub r9=0,r8				// negate return value to get errno value
	;;
	cmp.ne p6,p0=r3,r0			// is pt_regs.r8!=0?
	adds r3=16,r2				// r3=&pt_regs.r10
	;;
(p6)	mov r10=-1
(p6)	mov r8=r9
	br.cond.sptk strace_save_retval
END(ia64_trace_syscall)

GLOBAL_ENTRY(ia64_ret_from_clone)
	PT_REGS_UNWIND_INFO(0)
{	/*
	 * Some versions of gas generate bad unwind info if the first instruction of a
	 * procedure doesn't go into the first slot of a bundle.  This is a workaround.
	 */
	nop.m 0
	nop.i 0
	/*
	 * We need to call schedule_tail() to complete the scheduling process.
	 * Called by ia64_switch_to() after do_fork()->copy_thread().  r8 contains the
	 * address of the previously executing task.
	 */
	br.call.sptk.many rp=ia64_invoke_schedule_tail
}
.ret8:
	adds r2=TI_FLAGS+IA64_TASK_SIZE,r13
	;;
	ld4 r2=[r2]
	;;
	mov r8=0
	tbit.nz p6,p0=r2,TIF_SYSCALL_TRACE
(p6)	br.cond.spnt strace_check_retval
	;;					// added stop bits to prevent r8 dependency
END(ia64_ret_from_clone)
	// fall through
GLOBAL_ENTRY(ia64_ret_from_syscall)
	PT_REGS_UNWIND_INFO(0)
	cmp.ge p6,p7=r8,r0			// syscall executed successfully?
	adds r2=PT(R8)+16,sp			// r2 = &pt_regs.r8
	adds r3=PT(R10)+16,sp			// r3 = &pt_regs.r10
	;;
.mem.offset 0,0; (p6) st8.spill [r2]=r8	// store return value in slot for r8 and set unat bit
.mem.offset 8,0; (p6) st8.spill [r3]=r0	// clear error indication in slot for r10 and set unat bit
(p7)	br.cond.spnt handle_syscall_error	// handle potential syscall failure
END(ia64_ret_from_syscall)
	// fall through
/*
 * ia64_leave_syscall(): Same as ia64_leave_kernel, except that it doesn't
 *	need to switch to bank 0 and doesn't restore the scratch registers.
 *	To avoid leaking kernel bits, the scratch registers are set to
 *	the following known-to-be-safe values:
 *
 *		  r1: restored (global pointer)
 *		  r2: cleared
 *		  r3: 1 (when returning to user-level)
 *	      r8-r11: restored (syscall return value(s))
 *		 r12: restored (user-level stack pointer)
 *		 r13: restored (user-level thread pointer)
 *		 r14: cleared
 *		 r15: restored (syscall #)
 *	     r16-r19: cleared
 *		 r20: user-level ar.fpsr
 *		 r21: user-level b0
 *		 r22: user-level b6
 *		 r23: user-level ar.bspstore
 *		 r24: user-level ar.rnat
 *		 r25: user-level ar.unat
 *		 r26: user-level ar.pfs
 *		 r27: user-level ar.rsc
 *		 r28: user-level ip
 *		 r29: user-level psr
 *		 r30: user-level cfm
 *		 r31: user-level pr
 *	      f6-f11: cleared
 *		  pr: restored (user-level pr)
 *		  b0: restored (user-level rp)
 *	          b6: restored
 *		  b7: cleared
 *	     ar.unat: restored (user-level ar.unat)
 *	      ar.pfs: restored (user-level ar.pfs)
 *	      ar.rsc: restored (user-level ar.rsc)
 *	     ar.rnat: restored (user-level ar.rnat)
 *	 ar.bspstore: restored (user-level ar.bspstore)
 *	     ar.fpsr: restored (user-level ar.fpsr)
 *	      ar.ccv: cleared
 *	      ar.csd: cleared
 *	      ar.ssd: cleared
 */
GLOBAL_ENTRY(ia64_leave_syscall)
	PT_REGS_UNWIND_INFO(0)
	/*
	 * work.need_resched etc. mustn't get changed by this CPU before it returns to
	 * user- or fsys-mode, hence we disable interrupts early on:
	 */
#ifdef CONFIG_PREEMPT
	rsm psr.i				// disable interrupts
#else
(pUStk)	rsm psr.i
#endif
	cmp.eq pLvSys,p0=r0,r0			// pLvSys=1: leave from syscall
(pUStk)	cmp.eq.unc p6,p0=r0,r0			// p6 <- pUStk
.work_processed_syscall:
#ifdef CONFIG_PREEMPT
(pKStk) adds r20=TI_PRE_COUNT+IA64_TASK_SIZE,r13
	;;
	.pred.rel.mutex pUStk,pKStk
(pKStk) ld4 r21=[r20]			// r21 <- preempt_count
(pUStk)	mov r21=0			// r21 <- 0
	;;
(p6)	cmp.eq.unc p6,p0=r21,r0		// p6 <- p6 && (r21 == 0)
#endif /* CONFIG_PREEMPT */
	adds r16=PT(LOADRS)+16,r12
	adds r17=PT(AR_BSPSTORE)+16,r12
	adds r18=TI_FLAGS+IA64_TASK_SIZE,r13
	;;
(p6)	ld4 r31=[r18]				// load current_thread_info()->flags
	ld8 r19=[r16],PT(B6)-PT(LOADRS)		// load ar.rsc value for "loadrs"
	nop.i 0
	;;
	ld8 r23=[r17],PT(R9)-PT(AR_BSPSTORE)	// load ar.bspstore (may be garbage)
	ld8 r22=[r16],PT(R8)-PT(B6)		// load b6
(p6)	and r15=TIF_WORK_MASK,r31		// any work other than TIF_SYSCALL_TRACE?
	;;

	mov.m ar.ccv=r0		// clear ar.ccv
(p6)	cmp4.ne.unc p6,p0=r15, r0		// any special work pending?
(p6)	br.cond.spnt .work_pending
	;;
	// start restoring the state saved on the kernel stack (struct pt_regs):
	ld8.fill r8=[r16],16
	ld8.fill r9=[r17],16
	mov f6=f0		// clear f6
	;;
	ld8.fill r10=[r16],16
	ld8.fill r11=[r17],16
	mov f7=f0		// clear f7
	;;
	ld8 r29=[r16],16	// load cr.ipsr
	ld8 r28=[r17],16	// load cr.iip
	mov f8=f0		// clear f8
	;;
	ld8 r30=[r16],16	// load cr.ifs
	ld8 r25=[r17],16	// load ar.unat
	cmp.eq p9,p0=r0,r0	// set p9 to indicate that we should restore cr.ifs
	;;
	rsm psr.i | psr.ic	// initiate turning off of interrupt and interruption collection
	invala			// invalidate ALAT
	mov f9=f0		// clear f9

	mov.m ar.ssd=r0		// clear ar.ssd
	mov.m ar.csd=r0		// clear ar.csd
	mov f10=f0		// clear f10
	;;
	ld8 r26=[r16],16	// load ar.pfs
	ld8 r27=[r17],PT(PR)-PT(AR_RSC)	// load ar.rsc
	mov f11=f0		// clear f11
	;;
	ld8 r24=[r16],PT(B0)-PT(AR_RNAT)	// load ar.rnat (may be garbage)
	ld8 r31=[r17],PT(R1)-PT(PR)		// load predicates
(pUStk) add r14=IA64_TASK_THREAD_ON_USTACK_OFFSET,r13
	;;
	ld8 r21=[r16],PT(R12)-PT(B0) // load b0
	ld8.fill r1=[r17],16	// load r1
(pUStk) mov r3=1
	;;
	ld8.fill r12=[r16],16
	ld8.fill r13=[r17],16
	mov r2=r0		// clear r2
	;;
	ld8 r20=[r16]		// load ar.fpsr
	ld8.fill r15=[r17]	// load r15
	mov b7=r0		// clear b7
	;;
(pUStk) st1 [r14]=r3
	movl r17=THIS_CPU(ia64_phys_stacked_size_p8)
	;;
	mov r16=ar.bsp		// get existing backing store pointer
	srlz.i			// ensure interruption collection is off
	mov r14=r0		// clear r14
	;;
	ld4 r17=[r17]		// r17 = cpu_data->phys_stacked_size_p8
	mov b6=r22				// restore b6
	shr.u r18=r19,16	// get byte size of existing "dirty" partition
(pKStk) br.cond.dpnt.many skip_rbs_switch
	br.cond.sptk.many rbs_switch
END(ia64_leave_syscall)

GLOBAL_ENTRY(ia64_leave_kernel)
	PT_REGS_UNWIND_INFO(0)
	/*
	 * work.need_resched etc. mustn't get changed by this CPU before it returns to
	 * user- or fsys-mode, hence we disable interrupts early on:
	 */
#ifdef CONFIG_PREEMPT
	rsm psr.i				// disable interrupts
#else
(pUStk)	rsm psr.i
#endif
	cmp.eq p0,pLvSys=r0,r0			// pLvSys=0: leave from kernel
(pUStk)	cmp.eq.unc p6,p0=r0,r0			// p6 <- pUStk
	;;
.work_processed_kernel:
#ifdef CONFIG_PREEMPT
	adds r20=TI_PRE_COUNT+IA64_TASK_SIZE,r13
	;;
	.pred.rel.mutex pUStk,pKStk
(pKStk)	ld4 r21=[r20]			// r21 <- preempt_count
(pUStk)	mov r21=0			// r21 <- 0
	;;
(p6)	cmp.eq.unc p6,p0=r21,r0		// p6 <- p6 && (r21 == 0)
#endif /* CONFIG_PREEMPT */
	adds r17=TI_FLAGS+IA64_TASK_SIZE,r13
	;;
(p6)	ld4 r31=[r17]				// load current_thread_info()->flags
	adds r21=PT(PR)+16,r12
	;;

	lfetch [r21],PT(CR_IPSR)-PT(PR)
	adds r2=PT(B6)+16,r12
	adds r3=PT(R16)+16,r12
	;;
	lfetch [r21]
	ld8 r28=[r2],8		// load b6
	adds r29=PT(R24)+16,r12

	ld8.fill r16=[r3],PT(AR_CSD)-PT(R16)
	adds r30=PT(AR_CCV)+16,r12
(p6)	and r19=TIF_WORK_MASK,r31		// any work other than TIF_SYSCALL_TRACE?
	;;
	ld8.fill r24=[r29]
	ld8 r15=[r30]		// load ar.ccv
(p6)	cmp4.ne.unc p6,p0=r19, r0		// any special work pending?
	;;
	ld8 r29=[r2],16		// load b7
	ld8 r30=[r3],16		// load ar.csd
(p6)	br.cond.spnt .work_pending
	;;
	ld8 r31=[r2],16		// load ar.ssd
	ld8.fill r8=[r3],16
	;;
	ld8.fill r9=[r2],16
	ld8.fill r10=[r3],PT(R17)-PT(R10)
	;;
	ld8.fill r11=[r2],PT(R18)-PT(R11)
	ld8.fill r17=[r3],16
	;;
	ld8.fill r18=[r2],16
	ld8.fill r19=[r3],16
	;;
	ld8.fill r20=[r2],16
	ld8.fill r21=[r3],16
	mov ar.csd=r30
	mov ar.ssd=r31
	;;
	rsm psr.i | psr.ic	// initiate turning off of interrupt and interruption collection
	invala			// invalidate ALAT
	;;
	ld8.fill r22=[r2],24
	ld8.fill r23=[r3],24
	mov b6=r28
	;;
	ld8.fill r25=[r2],16
	ld8.fill r26=[r3],16
	mov b7=r29
	;;
	ld8.fill r27=[r2],16
	ld8.fill r28=[r3],16
	;;
	ld8.fill r29=[r2],16
	ld8.fill r30=[r3],24
	;;
	ld8.fill r31=[r2],PT(F9)-PT(R31)
	adds r3=PT(F10)-PT(F6),r3
	;;
	ldf.fill f9=[r2],PT(F6)-PT(F9)
	ldf.fill f10=[r3],PT(F8)-PT(F10)
	;;
	ldf.fill f6=[r2],PT(F7)-PT(F6)
	;;
	ldf.fill f7=[r2],PT(F11)-PT(F7)
	ldf.fill f8=[r3],32
	;;
	srlz.i			// ensure interruption collection is off
	mov ar.ccv=r15
	;;
	bsw.0			// switch back to bank 0 (no stop bit required beforehand...)
	;;
	ldf.fill f11=[r2]
(pUStk)	mov r18=IA64_KR(CURRENT)	// Itanium 2: 12 cycle read latency
	adds r16=PT(CR_IPSR)+16,r12
	adds r17=PT(CR_IIP)+16,r12
	;;
	ld8 r29=[r16],16	// load cr.ipsr
	ld8 r28=[r17],16	// load cr.iip
	;;
	ld8 r30=[r16],16	// load cr.ifs
	ld8 r25=[r17],16	// load ar.unat
	;;
	ld8 r26=[r16],16	// load ar.pfs
	ld8 r27=[r17],16	// load ar.rsc
	cmp.eq p9,p0=r0,r0	// set p9 to indicate that we should restore cr.ifs
	;;
	ld8 r24=[r16],16	// load ar.rnat (may be garbage)
	ld8 r23=[r17],16// load ar.bspstore (may be garbage)
	;;
	ld8 r31=[r16],16	// load predicates
	ld8 r21=[r17],16	// load b0
	;;
	ld8 r19=[r16],16	// load ar.rsc value for "loadrs"
	ld8.fill r1=[r17],16	// load r1
	;;
	ld8.fill r12=[r16],16
	ld8.fill r13=[r17],16
(pUStk)	adds r18=IA64_TASK_THREAD_ON_USTACK_OFFSET,r18
	;;
	ld8 r20=[r16],16	// ar.fpsr
	ld8.fill r15=[r17],16
	;;
	ld8.fill r14=[r16],16
	ld8.fill r2=[r17]
(pUStk)	mov r17=1
	;;
	ld8.fill r3=[r16]
(pUStk)	st1 [r18]=r17		// restore current->thread.on_ustack
	shr.u r18=r19,16	// get byte size of existing "dirty" partition
	;;
	mov r16=ar.bsp		// get existing backing store pointer
	movl r17=THIS_CPU(ia64_phys_stacked_size_p8)
	;;
	ld4 r17=[r17]		// r17 = cpu_data->phys_stacked_size_p8
(pKStk)	br.cond.dpnt skip_rbs_switch

	/*
	 * Restore user backing store.
	 *
	 * NOTE: alloc, loadrs, and cover can't be predicated.
	 */
(pNonSys) br.cond.dpnt dont_preserve_current_frame

rbs_switch:
	cover				// add current frame into dirty partition and set cr.ifs
	;;
	mov r19=ar.bsp			// get new backing store pointer
	sub r16=r16,r18			// krbs = old bsp - size of dirty partition
	cmp.ne p9,p0=r0,r0		// clear p9 to skip restore of cr.ifs
	;;
	sub r19=r19,r16			// calculate total byte size of dirty partition
	add r18=64,r18			// don't force in0-in7 into memory...
	;;
	shl r19=r19,16			// shift size of dirty partition into loadrs position
	;;
dont_preserve_current_frame:
	/*
	 * To prevent leaking bits between the kernel and user-space,
	 * we must clear the stacked registers in the "invalid" partition here.
	 * Not pretty, but at least it's fast (3.34 registers/cycle on Itanium,
	 * 5 registers/cycle on McKinley).
	 */
#	define pRecurse	p6
#	define pReturn	p7
#ifdef CONFIG_ITANIUM
#	define Nregs	10
#else
#	define Nregs	14
#endif
	alloc loc0=ar.pfs,2,Nregs-2,2,0
	shr.u loc1=r18,9		// RNaTslots <= floor(dirtySize / (64*8))
	sub r17=r17,r18			// r17 = (physStackedSize + 8) - dirtySize
	;;
	mov ar.rsc=r19			// load ar.rsc to be used for "loadrs"
	shladd in0=loc1,3,r17
	mov in1=0
	;;
rse_clear_invalid:
#ifdef CONFIG_ITANIUM
	// cycle 0
 { .mii
	alloc loc0=ar.pfs,2,Nregs-2,2,0
	cmp.lt pRecurse,p0=Nregs*8,in0	// if more than Nregs regs left to clear, (re)curse
	add out0=-Nregs*8,in0
}{ .mfb
	add out1=1,in1			// increment recursion count
	nop.f 0
	nop.b 0				// can't do br.call here because of alloc (WAW on CFM)
	;;
}{ .mfi	// cycle 1
	mov loc1=0
	nop.f 0
	mov loc2=0
}{ .mib
	mov loc3=0
	mov loc4=0
(pRecurse) br.call.sptk.many b0=rse_clear_invalid

}{ .mfi	// cycle 2
	mov loc5=0
	nop.f 0
	cmp.ne pReturn,p0=r0,in1	// if recursion count != 0, we need to do a br.ret
}{ .mib
	mov loc6=0
	mov loc7=0
(pReturn) br.ret.sptk.many b0
}
#else /* !CONFIG_ITANIUM */
	alloc loc0=ar.pfs,2,Nregs-2,2,0
	cmp.lt pRecurse,p0=Nregs*8,in0	// if more than Nregs regs left to clear, (re)curse
	add out0=-Nregs*8,in0
	add out1=1,in1			// increment recursion count
	mov loc1=0
	mov loc2=0
	;;
	mov loc3=0
	mov loc4=0
	mov loc5=0
	mov loc6=0
	mov loc7=0
(pRecurse) br.call.sptk.few b0=rse_clear_invalid
	;;
	mov loc8=0
	mov loc9=0
	cmp.ne pReturn,p0=r0,in1	// if recursion count != 0, we need to do a br.ret
	mov loc10=0
	mov loc11=0
(pReturn) br.ret.sptk.many b0
#endif /* !CONFIG_ITANIUM */
#	undef pRecurse
#	undef pReturn
	;;
	alloc r17=ar.pfs,0,0,0,0	// drop current register frame
	;;
	loadrs
	;;
skip_rbs_switch:
(pLvSys)	mov r19=r0		// clear r19 for leave_syscall, no-op otherwise
	mov b0=r21
	mov ar.pfs=r26
(pUStk)	mov ar.bspstore=r23
(p9)	mov cr.ifs=r30
(pLvSys)mov r16=r0		// clear r16 for leave_syscall, no-op otherwise
	mov cr.ipsr=r29
	mov ar.fpsr=r20
(pLvSys)mov r17=r0		// clear r17 for leave_syscall, no-op otherwise
	mov cr.iip=r28
	;;
(pUStk)	mov ar.rnat=r24		// must happen with RSE in lazy mode
(pLvSys)mov r18=r0		// clear r18 for leave_syscall, no-op otherwise
	mov ar.rsc=r27
	mov ar.unat=r25
	mov pr=r31,-1
	rfi

	/*
	 * On entry:
	 *	r20 = &current->thread_info->pre_count (if CONFIG_PREEMPT)
	 *	r31 = current->thread_info->flags
	 * On exit:
	 *	p6 = TRUE if work-pending-check needs to be redone
	 */
.work_pending:
	tbit.z p6,p0=r31,TIF_NEED_RESCHED		// current_thread_info()->need_resched==0?
(p6)	br.cond.sptk.few .notify
#ifdef CONFIG_PREEMPT
(pKStk) dep r21=-1,r0,PREEMPT_ACTIVE_BIT,1
	;;
(pKStk) st4 [r20]=r21
	ssm psr.i		// enable interrupts
#endif
	br.call.spnt.many rp=schedule
.ret9:	cmp.eq p6,p0=r0,r0				// p6 <- 1
	rsm psr.i		// disable interrupts
	;;
#ifdef CONFIG_PREEMPT
(pKStk)	adds r20=TI_PRE_COUNT+IA64_TASK_SIZE,r13
	;;
(pKStk)	st4 [r20]=r0		// preempt_count() <- 0
#endif
(pLvSys)br.cond.sptk.many .work_processed_syscall	// re-check
	br.cond.sptk.many .work_processed_kernel	// re-check

.notify:
	br.call.spnt.many rp=notify_resume_user
.ret10:	cmp.ne p6,p0=r0,r0				// p6 <- 0
(pLvSys)br.cond.sptk.many .work_processed_syscall	// don't re-check
	br.cond.sptk.many .work_processed_kernel	// don't re-check
END(ia64_leave_kernel)

ENTRY(handle_syscall_error)
	/*
	 * Some system calls (e.g., ptrace, mmap) can return arbitrary values which could
	 * lead us to mistake a negative return value as a failed syscall.  Those syscall
	 * must deposit a non-zero value in pt_regs.r8 to indicate an error.  If
	 * pt_regs.r8 is zero, we assume that the call completed successfully.
	 */
	PT_REGS_UNWIND_INFO(0)
	ld8 r3=[r2]		// load pt_regs.r8
	sub r9=0,r8		// negate return value to get errno
	;;
	mov r10=-1		// return -1 in pt_regs.r10 to indicate error
	cmp.eq p6,p7=r3,r0	// is pt_regs.r8==0?
	adds r3=16,r2		// r3=&pt_regs.r10
	;;
(p6)	mov r9=r8
(p6)	mov r10=0
	;;
.mem.offset 0,0; st8.spill [r2]=r9	// store errno in pt_regs.r8 and set unat bit
.mem.offset 8,0; st8.spill [r3]=r10	// store error indication in pt_regs.r10 and set unat bit
	br.cond.sptk ia64_leave_syscall
END(handle_syscall_error)

	/*
	 * Invoke schedule_tail(task) while preserving in0-in7, which may be needed
	 * in case a system call gets restarted.
	 */
GLOBAL_ENTRY(ia64_invoke_schedule_tail)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,2,1,0
	mov loc0=rp
	mov out0=r8				// Address of previous task
	;;
	br.call.sptk.many rp=schedule_tail
.ret11:	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(ia64_invoke_schedule_tail)

	/*
	 * Setup stack and call do_notify_resume_user().  Note that pSys and pNonSys need to
	 * be set up by the caller.  We declare 8 input registers so the system call
	 * args get preserved, in case we need to restart a system call.
	 */
ENTRY(notify_resume_user)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,2,3,0 // preserve all eight input regs in case of syscall restart!
	mov r9=ar.unat
	mov loc0=rp				// save return address
	mov out0=0				// there is no "oldset"
	adds out1=8,sp				// out1=&sigscratch->ar_pfs
(pSys)	mov out2=1				// out2==1 => we're in a syscall
	;;
(pNonSys) mov out2=0				// out2==0 => not a syscall
	.fframe 16
	.spillpsp ar.unat, 16			// (note that offset is relative to psp+0x10!)
	st8 [sp]=r9,-16				// allocate space for ar.unat and save it
	st8 [out1]=loc1,-8			// save ar.pfs, out1=&sigscratch
	.body
	br.call.sptk.many rp=do_notify_resume_user
.ret15:	.restore sp
	adds sp=16,sp				// pop scratch stack space
	;;
	ld8 r9=[sp]				// load new unat from sigscratch->scratch_unat
	mov rp=loc0
	;;
	mov ar.unat=r9
	mov ar.pfs=loc1
	br.ret.sptk.many rp
END(notify_resume_user)

GLOBAL_ENTRY(sys_rt_sigsuspend)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(8)
	alloc loc1=ar.pfs,8,2,3,0 // preserve all eight input regs in case of syscall restart!
	mov r9=ar.unat
	mov loc0=rp				// save return address
	mov out0=in0				// mask
	mov out1=in1				// sigsetsize
	adds out2=8,sp				// out2=&sigscratch->ar_pfs
	;;
	.fframe 16
	.spillpsp ar.unat, 16			// (note that offset is relative to psp+0x10!)
	st8 [sp]=r9,-16				// allocate space for ar.unat and save it
	st8 [out2]=loc1,-8			// save ar.pfs, out2=&sigscratch
	.body
	br.call.sptk.many rp=ia64_rt_sigsuspend
.ret17:	.restore sp
	adds sp=16,sp				// pop scratch stack space
	;;
	ld8 r9=[sp]				// load new unat from sw->caller_unat
	mov rp=loc0
	;;
	mov ar.unat=r9
	mov ar.pfs=loc1
	br.ret.sptk.many rp
END(sys_rt_sigsuspend)

ENTRY(sys_rt_sigreturn)
	PT_REGS_UNWIND_INFO(0)
	alloc r2=ar.pfs,0,0,1,0
	.prologue
	PT_REGS_SAVES(16)
	adds sp=-16,sp
	.body
	cmp.eq pNonSys,pSys=r0,r0		// sigreturn isn't a normal syscall...
	;;
	/*
	 * leave_kernel() restores f6-f11 from pt_regs, but since the streamlined
	 * syscall-entry path does not save them we save them here instead.  Note: we
	 * don't need to save any other registers that are not saved by the stream-lined
	 * syscall path, because restore_sigcontext() restores them.
	 */
	adds r16=PT(F6)+32,sp
	adds r17=PT(F7)+32,sp
	;;
 	stf.spill [r16]=f6,32
 	stf.spill [r17]=f7,32
	;;
 	stf.spill [r16]=f8,32
 	stf.spill [r17]=f9,32
	;;
 	stf.spill [r16]=f10
 	stf.spill [r17]=f11
	adds out0=16,sp				// out0 = &sigscratch
	br.call.sptk.many rp=ia64_rt_sigreturn
.ret19:	.restore sp 0
	adds sp=16,sp
	;;
	ld8 r9=[sp]				// load new ar.unat
	mov.sptk b7=r8,ia64_leave_kernel
	;;
	mov ar.unat=r9
	br.many b7
END(sys_rt_sigreturn)

GLOBAL_ENTRY(ia64_prepare_handle_unaligned)
	.prologue
	/*
	 * r16 = fake ar.pfs, we simply need to make sure privilege is still 0
	 */
	mov r16=r0
	DO_SAVE_SWITCH_STACK
	br.call.sptk.many rp=ia64_handle_unaligned	// stack frame setup in ivt
.ret21:	.body
	DO_LOAD_SWITCH_STACK
	br.cond.sptk.many rp				// goes to ia64_leave_kernel
END(ia64_prepare_handle_unaligned)

	//
	// unw_init_running(void (*callback)(info, arg), void *arg)
	//
#	define EXTRA_FRAME_SIZE	((UNW_FRAME_INFO_SIZE+15)&~15)

GLOBAL_ENTRY(unw_init_running)
	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	alloc loc1=ar.pfs,2,3,3,0
	;;
	ld8 loc2=[in0],8
	mov loc0=rp
	mov r16=loc1
	DO_SAVE_SWITCH_STACK
	.body

	.prologue ASM_UNW_PRLG_RP|ASM_UNW_PRLG_PFS, ASM_UNW_PRLG_GRSAVE(2)
	.fframe IA64_SWITCH_STACK_SIZE+EXTRA_FRAME_SIZE
	SWITCH_STACK_SAVES(EXTRA_FRAME_SIZE)
	adds sp=-EXTRA_FRAME_SIZE,sp
	.body
	;;
	adds out0=16,sp				// &info
	mov out1=r13				// current
	adds out2=16+EXTRA_FRAME_SIZE,sp	// &switch_stack
	br.call.sptk.many rp=unw_init_frame_info
1:	adds out0=16,sp				// &info
	mov b6=loc2
	mov loc2=gp				// save gp across indirect function call
	;;
	ld8 gp=[in0]
	mov out1=in1				// arg
	br.call.sptk.many rp=b6			// invoke the callback function
1:	mov gp=loc2				// restore gp

	// For now, we don't allow changing registers from within
	// unw_init_running; if we ever want to allow that, we'd
	// have to do a load_switch_stack here:
	.restore sp
	adds sp=IA64_SWITCH_STACK_SIZE+EXTRA_FRAME_SIZE,sp

	mov ar.pfs=loc1
	mov rp=loc0
	br.ret.sptk.many rp
END(unw_init_running)

	.rodata
	.align 8
	.globl sys_call_table
sys_call_table:
	data8 sys_ni_syscall		//  This must be sys_ni_syscall!  See ivt.S.
	data8 sys_exit				// 1025
	data8 sys_read
	data8 sys_write
	data8 sys_open
	data8 sys_close
	data8 sys_creat				// 1030
	data8 sys_link
	data8 sys_unlink
	data8 ia64_execve
	data8 sys_chdir
	data8 sys_fchdir			// 1035
	data8 sys_utimes
	data8 sys_mknod
	data8 sys_chmod
	data8 sys_chown
	data8 sys_lseek				// 1040
	data8 sys_getpid
	data8 sys_getppid
	data8 sys_mount
	data8 sys_umount
	data8 sys_setuid			// 1045
	data8 sys_getuid
	data8 sys_geteuid
	data8 sys_ptrace
	data8 sys_access
	data8 sys_sync				// 1050
	data8 sys_fsync
	data8 sys_fdatasync
	data8 sys_kill
	data8 sys_rename
	data8 sys_mkdir				// 1055
	data8 sys_rmdir
	data8 sys_dup
	data8 sys_pipe
	data8 sys_times
	data8 ia64_brk				// 1060
	data8 sys_setgid
	data8 sys_getgid
	data8 sys_getegid
	data8 sys_acct
	data8 sys_ioctl				// 1065
	data8 sys_fcntl
	data8 sys_umask
	data8 sys_chroot
	data8 sys_ustat
	data8 sys_dup2				// 1070
	data8 sys_setreuid
	data8 sys_setregid
	data8 sys_getresuid
	data8 sys_setresuid
	data8 sys_getresgid			// 1075
	data8 sys_setresgid
	data8 sys_getgroups
	data8 sys_setgroups
	data8 sys_getpgid
	data8 sys_setpgid			// 1080
	data8 sys_setsid
	data8 sys_getsid
	data8 sys_sethostname
	data8 sys_setrlimit
	data8 sys_getrlimit			// 1085
	data8 sys_getrusage
	data8 sys_gettimeofday
	data8 sys_settimeofday
	data8 sys_select
	data8 sys_poll				// 1090
	data8 sys_symlink
	data8 sys_readlink
	data8 sys_uselib
	data8 sys_swapon
	data8 sys_swapoff			// 1095
	data8 sys_reboot
	data8 sys_truncate
	data8 sys_ftruncate
	data8 sys_fchmod
	data8 sys_fchown			// 1100
	data8 ia64_getpriority
	data8 sys_setpriority
	data8 sys_statfs
	data8 sys_fstatfs
	data8 sys_gettid			// 1105
	data8 sys_semget
	data8 sys_semop
	data8 sys_semctl
	data8 sys_msgget
	data8 sys_msgsnd			// 1110
	data8 sys_msgrcv
	data8 sys_msgctl
	data8 sys_shmget
	data8 ia64_shmat
	data8 sys_shmdt				// 1115
	data8 sys_shmctl
	data8 sys_syslog
	data8 sys_setitimer
	data8 sys_getitimer
	data8 ia64_ni_syscall			// 1120		/* was: ia64_oldstat */
	data8 ia64_ni_syscall					/* was: ia64_oldlstat */
	data8 ia64_ni_syscall					/* was: ia64_oldfstat */
	data8 sys_vhangup
	data8 sys_lchown
	data8 sys_remap_file_pages		// 1125
	data8 sys_wait4
	data8 sys_sysinfo
	data8 sys_clone
	data8 sys_setdomainname
	data8 sys_newuname			// 1130
	data8 sys_adjtimex
	data8 ia64_ni_syscall					/* was: ia64_create_module */
	data8 sys_init_module
	data8 sys_delete_module
	data8 ia64_ni_syscall			// 1135		/* was: sys_get_kernel_syms */
	data8 ia64_ni_syscall					/* was: sys_query_module */
	data8 sys_quotactl
	data8 sys_bdflush
	data8 sys_sysfs
	data8 sys_personality			// 1140
	data8 ia64_ni_syscall		// sys_afs_syscall
	data8 sys_setfsuid
	data8 sys_setfsgid
	data8 sys_getdents
	data8 sys_flock				// 1145
	data8 sys_readv
	data8 sys_writev
	data8 sys_pread64
	data8 sys_pwrite64
	data8 sys_sysctl			// 1150
	data8 sys_mmap
	data8 sys_munmap
	data8 sys_mlock
	data8 sys_mlockall
	data8 sys_mprotect			// 1155
	data8 ia64_mremap
	data8 sys_msync
	data8 sys_munlock
	data8 sys_munlockall
	data8 sys_sched_getparam		// 1160
	data8 sys_sched_setparam
	data8 sys_sched_getscheduler
	data8 sys_sched_setscheduler
	data8 sys_sched_yield
	data8 sys_sched_get_priority_max	// 1165
	data8 sys_sched_get_priority_min
	data8 sys_sched_rr_get_interval
	data8 sys_nanosleep
	data8 sys_nfsservctl
	data8 sys_prctl				// 1170
	data8 sys_getpagesize
	data8 sys_mmap2
	data8 sys_pciconfig_read
	data8 sys_pciconfig_write
	data8 sys_perfmonctl			// 1175
	data8 sys_sigaltstack
	data8 sys_rt_sigaction
	data8 sys_rt_sigpending
	data8 sys_rt_sigprocmask
	data8 sys_rt_sigqueueinfo		// 1180
	data8 sys_rt_sigreturn
	data8 sys_rt_sigsuspend
	data8 sys_rt_sigtimedwait
	data8 sys_getcwd
	data8 sys_capget			// 1185
	data8 sys_capset
	data8 sys_sendfile
	data8 sys_ni_syscall		// sys_getpmsg (STREAMS)
	data8 sys_ni_syscall		// sys_putpmsg (STREAMS)
	data8 sys_socket			// 1190
	data8 sys_bind
	data8 sys_connect
	data8 sys_listen
	data8 sys_accept
	data8 sys_getsockname			// 1195
	data8 sys_getpeername
	data8 sys_socketpair
	data8 sys_send
	data8 sys_sendto
	data8 sys_recv				// 1200
	data8 sys_recvfrom
	data8 sys_shutdown
	data8 sys_setsockopt
	data8 sys_getsockopt
	data8 sys_sendmsg			// 1205
	data8 sys_recvmsg
	data8 sys_pivot_root
	data8 sys_mincore
	data8 sys_madvise
	data8 sys_newstat			// 1210
	data8 sys_newlstat
	data8 sys_newfstat
	data8 sys_clone2
	data8 sys_getdents64
	data8 sys_getunwind			// 1215
	data8 sys_readahead
	data8 sys_setxattr
	data8 sys_lsetxattr
	data8 sys_fsetxattr
	data8 sys_getxattr			// 1220
	data8 sys_lgetxattr
	data8 sys_fgetxattr
	data8 sys_listxattr
	data8 sys_llistxattr
	data8 sys_flistxattr			// 1225
	data8 sys_removexattr
	data8 sys_lremovexattr
	data8 sys_fremovexattr
	data8 sys_tkill
	data8 sys_futex				// 1230
	data8 sys_sched_setaffinity
	data8 sys_sched_getaffinity
	data8 sys_set_tid_address
	data8 sys_fadvise64
	data8 sys_tgkill 			// 1235
	data8 sys_exit_group
	data8 sys_lookup_dcookie
	data8 sys_io_setup
	data8 sys_io_destroy
	data8 sys_io_getevents			// 1240
	data8 sys_io_submit
	data8 sys_io_cancel
	data8 sys_epoll_create
	data8 sys_epoll_ctl
	data8 sys_epoll_wait			// 1245
	data8 sys_restart_syscall
	data8 sys_semtimedop
	data8 sys_timer_create
	data8 sys_timer_settime
	data8 sys_timer_gettime			// 1250
	data8 sys_timer_getoverrun
	data8 sys_timer_delete
	data8 sys_clock_settime
	data8 sys_clock_gettime
	data8 sys_clock_getres			// 1255
	data8 sys_clock_nanosleep
	data8 sys_fstatfs64
	data8 sys_statfs64
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1260
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1265
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1270
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall			// 1275
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall
	data8 ia64_ni_syscall

	.org sys_call_table + 8*NR_syscalls	// guard against failures to increase NR_syscalls
