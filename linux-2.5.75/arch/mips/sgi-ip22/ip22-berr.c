/*
 * ip22-berr.c: Bus error handling.
 *
 * Copyright (C) 2002 Ladislav Michl
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/addrspace.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/branch.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>


static unsigned int cpu_err_stat;	/* Status reg for CPU */
static unsigned int gio_err_stat;	/* Status reg for GIO */
static unsigned int cpu_err_addr;	/* Error address reg for CPU */
static unsigned int gio_err_addr;	/* Error address reg for GIO */

static void save_and_clear_buserr(void)
{
	/* save memory controler's error status registers */
	cpu_err_addr = sgimc->cerr;
	cpu_err_stat = sgimc->cstat;
	gio_err_addr = sgimc->gerr;
	gio_err_stat = sgimc->gstat;

	sgimc->cstat = sgimc->gstat = 0;
}

#define GIO_ERRMASK	0xff00
#define CPU_ERRMASK	0x3f00

static void print_buserr(void)
{
	if (cpu_err_stat & CPU_ERRMASK)
		printk(KERN_ALERT "CPU error 0x%x<%s%s%s%s%s%s> @ 0x%08x\n",
			cpu_err_stat,
			cpu_err_stat & SGIMC_CSTAT_RD ? "RD " : "",
			cpu_err_stat & SGIMC_CSTAT_PAR ? "PAR " : "",
			cpu_err_stat & SGIMC_CSTAT_ADDR ? "ADDR " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSAD_PAR ? "SYSAD " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSCMD_PAR ? "SYSCMD " : "",
			cpu_err_stat & SGIMC_CSTAT_BAD_DATA ? "BAD_DATA " : "",
			cpu_err_addr);
	if (gio_err_stat & GIO_ERRMASK)
		printk(KERN_ALERT "GIO error 0x%x:<%s%s%s%s%s%s%s%s> @ 0x08%x\n",
			gio_err_stat,
			gio_err_stat & SGIMC_GSTAT_RD ? "RD " : "",
			gio_err_stat & SGIMC_GSTAT_WR ? "WR " : "",
			gio_err_stat & SGIMC_GSTAT_TIME ? "TIME " : "",
			gio_err_stat & SGIMC_GSTAT_PROM ? "PROM " : "",
			gio_err_stat & SGIMC_GSTAT_ADDR ? "ADDR " : "",
			gio_err_stat & SGIMC_GSTAT_BC ? "BC " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_RD ? "PIO_RD " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_WR ? "PIO_WR " : "",
			gio_err_addr);
}

/*
 * MC sends an interrupt whenever bus or parity errors occur. In addition,
 * if the error happened during a CPU read, it also asserts the bus error
 * pin on the R4K. Code in bus error handler save the MC bus error registers
 * and then clear the interrupt when this happens.
 */

void ip22_be_interrupt(int irq, struct pt_regs *regs)
{
	save_and_clear_buserr();
	print_buserr();
	panic("Bus error, epc == %08lx, ra == %08lx",
	      regs->cp0_epc, regs->regs[31]);
}

int ip22_be_handler(struct pt_regs *regs, int is_fixup)
{
	save_and_clear_buserr();
	if (is_fixup)
		return MIPS_BE_FIXUP;
	print_buserr();
	return MIPS_BE_FATAL;
}

void __init ip22_be_init(void)
{
	board_be_handler = ip22_be_handler;
}
