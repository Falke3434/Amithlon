/* cpu.c: Dinky routines to look for the kind of Sparc cpu
 *        we are on.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/fpumacro.h>

struct cpu_iu_info {
  short manuf;
  short impl;
  char* cpu_name;   /* should be enough I hope... */
};

struct cpu_fp_info {
  short manuf;
  short impl;
  char fpu_vers;
  char* fp_name;
};

struct cpu_fp_info linux_sparc_fpu[] = {
  { 0x17, 0x10, 0, "UltraSparc I integrated FPU"},
  { 0x22, 0x10, 0, "UltraSparc I integrated FPU"},
  { 0x17, 0x11, 0, "UltraSparc II integrated FPU"},
  { 0x17, 0x12, 0, "UltraSparc IIi integrated FPU"},
  { 0x17, 0x13, 0, "UltraSparc IIe integrated FPU"},
  { 0x3e, 0x14, 0, "UltraSparc III integrated FPU"},
  { 0x3e, 0x15, 0, "UltraSparc III+ integrated FPU"},
};

#define NSPARCFPU  (sizeof(linux_sparc_fpu)/sizeof(struct cpu_fp_info))

struct cpu_iu_info linux_sparc_chips[] = {
  { 0x17, 0x10, "TI UltraSparc I   (SpitFire)"},
  { 0x22, 0x10, "TI UltraSparc I   (SpitFire)"},
  { 0x17, 0x11, "TI UltraSparc II  (BlackBird)"},
  { 0x17, 0x12, "TI UltraSparc IIi"},
  { 0x17, 0x13, "TI UltraSparc IIe"},
  { 0x3e, 0x14, "TI UltraSparc III (Cheetah)"},
  { 0x3e, 0x15, "TI UltraSparc III+ (Cheetah+)"},
};

#define NSPARCCHIPS  (sizeof(linux_sparc_chips)/sizeof(struct cpu_iu_info))

char *sparc_cpu_type[NR_CPUS] = { "cpu-oops", };
char *sparc_fpu_type[NR_CPUS] = { "fpu-oops", };

unsigned int fsr_storage;

void __init cpu_probe(void)
{
	unsigned long ver, fpu_vers, manuf, impl, fprs;
	int i, cpuid;
	
	cpuid = hard_smp_processor_id();

	fprs = fprs_read();
	fprs_write(FPRS_FEF);
	__asm__ __volatile__ ("rdpr %%ver, %0; stx %%fsr, [%1]"
			      : "=&r" (ver)
			      : "r" (&fpu_vers));
	fprs_write(fprs);
	
	manuf = ((ver >> 48) & 0xffff);
	impl = ((ver >> 32) & 0xffff);

	fpu_vers = ((fpu_vers >> 17) & 0x7);

retry:
	for (i = 0; i < NSPARCCHIPS; i++) {
		if (linux_sparc_chips[i].manuf == manuf) {
			if (linux_sparc_chips[i].impl == impl) {
				sparc_cpu_type[cpuid] =
					linux_sparc_chips[i].cpu_name;
				break;
			}
		}
	}

	if (i == NSPARCCHIPS) {
 		/* Maybe it is a cheetah+ derivative, report it as cheetah+
 		 * in that case until we learn the real names.
 		 */
 		if (manuf == 0x3e &&
 		    impl > 0x15) {
 			impl = 0x15;
 			goto retry;
 		} else {
 			printk("DEBUG: manuf[%lx] impl[%lx]\n",
 			       manuf, impl);
 		}
		sparc_cpu_type[cpuid] = "Unknown CPU";
	}

	for (i = 0; i < NSPARCFPU; i++) {
		if (linux_sparc_fpu[i].manuf == manuf &&
		    linux_sparc_fpu[i].impl == impl) {
			if (linux_sparc_fpu[i].fpu_vers == fpu_vers) {
				sparc_fpu_type[cpuid] =
					linux_sparc_fpu[i].fp_name;
				break;
			}
		}
	}

	if (i == NSPARCFPU) {
 		printk("DEBUG: manuf[%lx] impl[%lx] fsr.vers[%lx]\n",
 		       manuf, impl, fpu_vers);
		sparc_fpu_type[cpuid] = "Unknown FPU";
	}
}
