/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 2000 Ulf Carlsson
 *
 * At this time Linux/MIPS64 only supports syscall tracing, even for 32-bit
 * binaries.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>
#include <linux/security.h>

#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* Nothing to do.. */
}

/*
 * Tracing a 32-bit process with a 64-bit strace and vice versa will not
 * work.  I don't know how to fix this.
 */
asmlinkage int sys32_ptrace(int request, int pid, int addr, int data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		if ((ret = security_ptrace(current->parent, current)))
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned int tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp, (unsigned int *) (unsigned long) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned int tmp;

		regs = (struct pt_regs *) ((unsigned long) child->thread_info +
			KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		ret = 0;

		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				unsigned long long *fregs;
				fregs = (unsigned long long *)get_fpu_regs(child);
				/*
				 * The odd registers are actually the high
				 * order bits of the values stored in the even
				 * registers - unless we're using r2k_switch.S.
				 */
				if (addr & 1)
					tmp = (unsigned long) (fregs[((addr & ~1) - 32)] >> 32);
				else
					tmp = (unsigned long) (fregs[(addr - 32)] & 0xffffffff);
			} else {
				tmp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
		case FPC_CSR:
			if (cpu_has_fpu)
				tmp = child->thread.fpu.hard.control;
			else
				tmp = child->thread.fpu.soft.sr;
			break;
		case FPC_EIR: { /* implementation / version register */
			unsigned int flags;
			flags = read_c0_status();
			__enable_fpu();
			__asm__ __volatile__("cfc1\t%0,$0": "=r" (tmp));
			write_c0_status(flags);
			break;
		}
		default:
			tmp = 0;
			ret = -EIO;
			goto out_tsk;
		}
		ret = put_user(tmp, (unsigned *) (unsigned long) data);
		break;
		}
	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = (struct pt_regs *) ((unsigned long) child->thread_info +
			KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case FPR_BASE ... FPR_BASE + 31: {
			unsigned long long *fregs;
			fregs = (unsigned long long *)get_fpu_regs(child);
			if (!child->used_math) {
				/* FP not yet used  */
				memset(&child->thread.fpu.hard, ~0,
				       sizeof(child->thread.fpu.hard));
				child->thread.fpu.hard.control = 0;
			}
			/*
			 * The odd registers are actually the high order bits
			 * of the values stored in the even registers - unless
			 * we're using r2k_switch.S.
			 */
			if (addr & 1) {
				fregs[(addr & ~1) - FPR_BASE] &= 0xffffffff;
				fregs[(addr & ~1) - FPR_BASE] |= ((unsigned long long) data) << 32;
			} else {
				fregs[addr - FPR_BASE] &= ~0xffffffffLL;
				/* Must cast, lest sign extension fill upper
				   bits!  */
				fregs[addr - FPR_BASE] |= (unsigned int)data;
			}
			break;
		}
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
		case FPC_CSR:
			if (cpu_has_fpu)
				child->thread.fpu.hard.control = data;
			else
				child->thread.fpu.soft.sr = data;
			break;
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}
	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		ret = -EIO;
		if ((unsigned int) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill.
 * perhaps it should be put in the status that it wants to
 * exit.
 */
	case PTRACE_KILL: {
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		if ((ret = security_ptrace(current->parent, current)))
			goto out;
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	ret = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out_tsk;

	switch (request) {
	/* when I and D space are separate, these will need to be fixed. */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		ret = put_user(tmp,(unsigned long *) data);
		break;
	}

	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp;

		regs = (struct pt_regs *) ((unsigned long) child->thread_info +
			KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		ret = 0;

		switch (addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				unsigned long *fregs = get_fpu_regs(child);
				tmp = fregs[addr - FPR_BASE];
			} else {
				tmp = -EIO;
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
		case FPC_CSR:
			if (cpu_has_fpu)
				tmp = child->thread.fpu.hard.control;
			else
				tmp = child->thread.fpu.soft.sr;
			break;
		case FPC_EIR: { /* implementation / version register */
			unsigned int flags;
			flags = read_c0_status();
			__enable_fpu();
			__asm__ __volatile__("cfc1\t%0,$0": "=r" (tmp));
			write_c0_status(flags);
			break;
		}
		default:
			tmp = 0;
			ret = -EIO;
			goto out_tsk;
		}
		ret = put_user(tmp, (unsigned long *) data);
		break;
		}
	/* when I and D space are separate, this will have to be fixed. */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1) == sizeof(data))
			break;
		ret = -EIO;
		break;

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		ret = 0;
		regs = (struct pt_regs *) ((unsigned long) child->thread_info +
			KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case FPR_BASE ... FPR_BASE + 31: {
			unsigned long *fregs = get_fpu_regs(child);
			if (!child->used_math) {
				/* FP not yet used  */
				memset(&child->thread.fpu.hard, ~0,
				       sizeof(child->thread.fpu.hard));
				child->thread.fpu.hard.control = 0;
			}
			fregs[addr - FPR_BASE] = data;
			break;
		}
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
		case FPC_CSR:
			if (cpu_has_fpu)
				child->thread.fpu.hard.control = data;
			else
				child->thread.fpu.soft.sr = data;
			break;
		default:
			/* The rest are not allowed. */
			ret = -EIO;
			break;
		}
		break;
		}
	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL) {
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		else {
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		}
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;
	}

/*
 * make the child exit.  Best I can do is send it a sigkill.
 * perhaps it should be put in the status that it wants to
 * exit.
 */
	case PTRACE_KILL: {
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;
	}

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

out_tsk:
	put_task_struct(child);
out:
	unlock_kernel();
	return ret;
}

struct task_work_bf {
#ifdef __MIPSEB__
	signed		need_resched  :8;
	unsigned	syscall_trace;	/* count of syscall interceptors */
	unsigned	sigpending;
	unsigned	notify_resume;	/* request for notification on
					   userspace execution resumption */
#endif
#ifdef __MIPSEL__
	unsigned	notify_resume : 8;	/* request for notification on
						   userspace execution
						   resumption */
	unsigned	sigpending    : 8;
	unsigned	syscall_trace : 8;	/* count of syscall
						   interceptors */
	signed		need_resched : 8;
#endif
};

asmlinkage void do_syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;

	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	current->exit_code = SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
	                                ? 0x80 : 0);
	preempt_disable();
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	preempt_enable();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
