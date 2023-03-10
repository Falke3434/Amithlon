/* 
 * APIC driver for the IBM "Summit" chipset.
 */
#define APIC_DEFINITION 1
#include <asm/genapic.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/mach-summit/mach_apic.h>
#include <asm/mach-summit/mach_apicdef.h>
#include <asm/mach-summit/mach_ipi.h>
#include <asm/mach-summit/mach_mpparse.h>

static __init int probe_summit(void)
{ 
	/* probed later in mptable/ACPI hooks */
	return 0;
} 

struct genapic apic_summit = APIC_INIT("summit", probe_summit); 
