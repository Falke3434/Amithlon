/*
 * mce.c - x86 Machine Check Exception Reporting
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/smp.h>
#include <linux/thread_info.h>

#include <asm/processor.h> 
#include <asm/system.h>

#include "mce.h"

int mce_disabled __initdata = 0;
int nr_mce_banks;

/* Handle unconfigured int18 (should never happen) */
static void unexpected_machine_check(struct pt_regs * regs, long error_code)
{	
	printk(KERN_ERR "CPU#%d: Unexpected int18 (Machine Check).\n", smp_processor_id());
}

/* Call the installed machine check handler for this CPU setup. */
void (*machine_check_vector)(struct pt_regs *, long error_code) = unexpected_machine_check;

asmlinkage void do_machine_check(struct pt_regs * regs, long error_code)
{
	machine_check_vector(regs, error_code);
}

/* This has to be run for each processor */
void __init mcheck_init(struct cpuinfo_x86 *c)
{
	if (mce_disabled==1)
		return;

	switch (c->x86_vendor) {
		case X86_VENDOR_AMD:
			if (c->x86==6 || c->x86==15)
				amd_mcheck_init(c);
			break;

		case X86_VENDOR_INTEL:
			if (c->x86==5)
				intel_p5_mcheck_init(c);
			if (c->x86==6)
				intel_p6_mcheck_init(c);
			if (c->x86==15)
				intel_p4_mcheck_init(c);
			break;

		case X86_VENDOR_CENTAUR:
			if (c->x86==5)
				winchip_mcheck_init(c);
			break;

		default:
			break;
	}
}

static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 0;
}

static int __init mcheck_enable(char *str)
{
	mce_disabled = -1;
	return 0;
}

__setup("nomce", mcheck_disable);
__setup("mce", mcheck_enable);
