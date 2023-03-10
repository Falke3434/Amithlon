/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/version.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/module.h>

#include <asm/branch.h>
#include <asm/hardirq.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>

#define development_version (LINUX_VERSION_CODE & 0x100)

/*
 * Macro for exception fixup code to access integer registers.
 */
#define dpf_reg(r) (regs->regs[r])

/*
 * Unlock any spinlocks which will prevent us from getting the out
 */
void bust_spinlocks(int yes)
{
	int loglevel_save = console_loglevel;

	if (yes) {
		oops_in_progress = 1;
		return;
	}
#ifdef CONFIG_VT
	unblank_screen();
#endif
	oops_in_progress = 0;
	/*
	 * OK, the message is on the console.  Now we call printk()
	 * without oops_in_progress set so that printk will give klogd
	 * a poke.  Hold onto your hats...
	 */
	console_loglevel = 15;		/* NMI oopser may have shut the console up */
	printk(" ");
	console_loglevel = loglevel_save;
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long write,
			      unsigned long address)
{
	struct vm_area_struct * vma;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	const struct exception_table_entry *fixup;
	siginfo_t info;

#if 0
	printk("Cpu%d[%s:%d:%08lx:%ld:%08lx]\n", smp_processor_id(),
	       current->comm, current->pid, address, write, regs->cp0_epc);
#endif

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (address >= VMALLOC_START)
		goto vmalloc_fault;

	info.si_code = SEGV_MAPERR;
	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	info.si_code = SEGV_ACCERR;

	if (write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

survive:
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, write)) {
	case VM_FAULT_MINOR:
		tsk->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto do_sigbus;
	case VM_FAULT_OOM:
		goto out_of_memory;
	default:
		BUG();
	}

	up_read(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		tsk->thread.cp0_badvaddr = address;
		tsk->thread.error_code = write;
#if 0
		printk("do_page_fault() #2: sending SIGSEGV to %s for illegal %s\n"
		       "%08lx (epc == %08lx, ra == %08lx)\n",
		       tsk->comm,
		       write ? "write access to" : "read access from",
		       address,
		       (unsigned long) regs->cp0_epc,
		       (unsigned long) regs->regs[31]);
#endif
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *) address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(exception_epc(regs));
	if (fixup) {
		unsigned long new_epc = fixup->nextinsn;

		tsk->thread.cp0_baduaddr = address;
		if (development_version)
			printk(KERN_DEBUG "%s: Exception at [<%lx>] (%lx)\n",
			       tsk->comm, regs->cp0_epc, new_epc);
		regs->cp0_epc = new_epc;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */

	bust_spinlocks(1);

	printk(KERN_ALERT "Cpu %d Unable to handle kernel paging request at "
	       "address %016lx, epc == %016lx, ra == %016lx\n",
	       smp_processor_id(), address, regs->cp0_epc, regs->regs[31]);
	die("Oops", regs);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (tsk->pid == 1) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.cp0_badvaddr = address;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *) address;
	force_sig_info(SIGBUS, &info, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;

	return;

vmalloc_fault:
	panic("Pagefault for kernel virtual memory");
}
