
#include <linux/config.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/smp.h>
#include <asm/io.h>

#include "cobalt.h"
#include "mach_apic.h"

/* Have we found an MP table */
int smp_found_config;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [MAX_APICS];

int pic_mode;
unsigned long mp_lapic_addr;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
unsigned int boot_cpu_logical_apicid = -1U;

/* Bitmask of physically existing CPUs */
unsigned long phys_cpu_present_map;


/*
 * The Visual Workstation is Intel MP compliant in the hardware
 * sense, but it doesn't have a BIOS(-configuration table).
 * No problem for Linux.
 */

void __init MP_processor_info (struct mpc_config_processor *m)
{
 	int ver, logical_apicid;
 	
	if (!(m->mpc_cpuflag & CPU_ENABLED))
		return;

	logical_apicid = m->mpc_apicid;
	printk(KERN_INFO "%sCPU #%d %ld:%ld APIC version %d\n",
		m->mpc_cpuflag & CPU_BOOTPROCESSOR ? "Bootup " : "",
		m->mpc_apicid,
		(m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
		(m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
		m->mpc_apicver);

	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		boot_cpu_physical_apicid = m->mpc_apicid;
		boot_cpu_logical_apicid = logical_apicid;
	}

	if (m->mpc_apicid > MAX_APICS) {
		printk(KERN_ERR "Processor #%d INVALID. (Max ID: %d).\n",
			m->mpc_apicid, MAX_APICS);
		return;
	}
	ver = m->mpc_apicver;

	phys_cpu_present_map |= apicid_to_cpu_present(m->mpc_apicid);
	/*
	 * Validate version
	 */
	if (ver == 0x0) {
		printk(KERN_ERR "BIOS bug, APIC version is 0 for CPU#%d! "
			"fixing up to 0x10. (tell your hw vendor)\n",
			m->mpc_apicid);
		ver = 0x10;
	}
	apic_version[m->mpc_apicid] = ver;
}

void __init find_smp_config(void)
{
	struct mpc_config_processor *mp = phys_to_virt(CO_CPU_TAB_PHYS);
	unsigned short ncpus = readw(phys_to_virt(CO_CPU_NUM_PHYS));

	if (ncpus > CO_CPU_MAX) {
		printk(KERN_WARNING "find_visws_smp: got cpu count of %d at %p\n",
			ncpus, mp);

		ncpus = CO_CPU_MAX;
	}

	smp_found_config = 1;
	while (ncpus--)
		MP_processor_info(mp++);

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
}

void __init get_smp_config (void)
{
}
