/*
 *  drivers/s390/s390mach.c
 *   S/390 machine check handler
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <asm/lowcore.h>

#include "s390mach.h"

#define DBG printk
// #define DBG(args,...) do {} while (0);

extern void css_process_crw(int);
extern void chsc_process_crw(void);
extern void chp_process_crw(int, int);

static void
s390_handle_damage(char *msg)
{
	printk(KERN_EMERG "%s\n", msg);
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	disabled_wait((unsigned long) __builtin_return_address(0));
}

/*
 * Retrieve CRWs and call function to handle event.
 *
 * Note : we currently process CRWs for io and chsc subchannels only
 */
static void
s390_collect_crw_info(void)
{
	struct crw crw;
	int ccode;

	do {
		ccode = stcrw(&crw);
		if (ccode != 0)
			break;
		DBG(KERN_DEBUG "crw_info : CRW reports slct=%d, oflw=%d, "
		    "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
		    crw.slct, crw.oflw, crw.chn, crw.rsc, crw.anc,
		    crw.erc, crw.rsid);
		switch (crw.rsc) {
		case CRW_RSC_SCH:
			pr_debug("source is subchannel %04X\n", crw.rsid);
			css_process_crw (crw.rsid);
			break;
		case CRW_RSC_MONITOR:
			pr_debug("source is monitoring facility\n");
			break;
		case CRW_RSC_CPATH:
			pr_debug("source is channel path %02X\n", crw.rsid);
			switch (crw.erc) {
			case CRW_ERC_IPARM: /* Path has come. */
				chp_process_crw(crw.rsid, 1);
				break;
			case CRW_ERC_PERRI: /* Path has gone. */
				chp_process_crw(crw.rsid, 0);
				break;
			default:
				pr_debug("Don't know how to handle erc=%x\n",
					 crw.erc);
			}
			break;
		case CRW_RSC_CONFIG:
			pr_debug("source is configuration-alert facility\n");
			break;
		case CRW_RSC_CSS:
			pr_debug("source is channel subsystem\n");
			chsc_process_crw();
			break;
		default:
			pr_debug("unknown source\n");
			break;
		}
	} while (crw.chn);
}

/*
 * machine check handler.
 */
void
s390_do_machine_check(void)
{
	struct mci *mci;

	mci = (struct mci *) &S390_lowcore.mcck_interruption_code;

	if (mci->sd)		/* system damage */
		s390_handle_damage("received system damage machine check\n");

	if (mci->pd)		/* instruction processing damage */
		s390_handle_damage("received instruction processing "
				   "damage machine check\n");

	if (mci->se)		/* storage error uncorrected */
		s390_handle_damage("received storage error uncorrected "
				   "machine check\n");

	if (mci->sc)		/* storage error corrected */
		printk(KERN_WARNING
		       "received storage error corrected machine check\n");

	if (mci->ke)		/* storage key-error uncorrected */
		s390_handle_damage("received storage key-error uncorrected "
				   "machine check\n");

	if (mci->ds && mci->fa)	/* storage degradation */
		s390_handle_damage("received storage degradation machine "
				   "check\n");

	if (mci->cp)		/* channel report word pending */
		s390_collect_crw_info();

#ifdef CONFIG_MACHCHK_WARNING
/*
 * The warning may remain for a prolonged period on the bare iron.
 * (actually till the machine is powered off, or until the problem is gone)
 * So we just stop listening for the WARNING MCH and prevent continuously
 * being interrupted.  One caveat is however, that we must do this per
 * processor and cannot use the smp version of ctl_clear_bit().
 * On VM we only get one interrupt per virtally presented machinecheck.
 * Though one suffices, we may get one interrupt per (virtual) processor.
 */
	if (mci->w) {	/* WARNING pending ? */
		static int mchchk_wng_posted = 0;
		/*
		 * Use single machine clear, as we cannot handle smp right now
		 */
		__ctl_clear_bit(14, 24);	/* Disable WARNING MCH */
		if (xchg(&mchchk_wng_posted, 1) == 0)
			kill_proc(1, SIGPWR, 1);
	}
#endif
}

/*
 * s390_init_machine_check
 *
 * initialize machine check handling
 */
static int
machine_check_init(void)
{
	ctl_clear_bit(14, 25);	/* disable damage MCH */
	ctl_set_bit(14, 26);	/* enable degradation MCH */
	ctl_set_bit(14, 27);	/* enable system recovery MCH */
#ifdef CONFIG_MACHCHK_WARNING
	ctl_set_bit(14, 24);	/* enable warning MCH */
#endif
	return 0;
}

/*
 * Initialize the machine check handler really early to be able to
 * catch all machine checks that happen during boot
 */
arch_initcall(machine_check_init);

/*
 * Machine checks for the channel subsystem must be enabled
 * after the channel subsystem is initialized
 */
static int __init
machine_check_crw_init (void)
{
	ctl_set_bit(14, 28);	/* enable channel report MCH */
	return 0;
}

device_initcall (machine_check_crw_init);
