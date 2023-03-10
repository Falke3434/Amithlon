/*
 *	Intel Multiprocessor Specificiation 1.1 and 1.4
 *	compliant MP-table parsing routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998, 1999, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *		Erich Boleyn	:	MP v1.4 and additional changes.
 *		Alan Cox	:	Added EBDA scanning
 *		Ingo Molnar	:	various cleanups and rewrites
 *		Maciej W. Rozycki:	Bits for default MP configurations
 *		Paul Diefenbaugh:	Added full ACPI support
 */

#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/bootmem.h>
#include <linux/smp_lock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>

#include <asm/smp.h>
#include <asm/acpi.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>
#include <asm/io_apic.h>

#include <mach_apic.h>
#include <mach_mpparse.h>
#include <bios_ebda.h>

/* Have we found an MP table */
int smp_found_config;

/*
 * Various Linux-internal data structures created from the
 * MP-table.
 */
int apic_version [MAX_APICS];
int mp_bus_id_to_type [MAX_MP_BUSSES];
int mp_bus_id_to_node [MAX_MP_BUSSES];
int mp_bus_id_to_local [MAX_MP_BUSSES];
int quad_local_to_mp_bus_id [NR_CPUS/4][4];
int mp_bus_id_to_pci_bus [MAX_MP_BUSSES] = { [0 ... MAX_MP_BUSSES-1] = -1 };
int mp_current_pci_id;

/* I/O APIC entries */
struct mpc_config_ioapic mp_ioapics[MAX_IO_APICS];

/* # of MP IRQ source entries */
struct mpc_config_intsrc mp_irqs[MAX_IRQ_SOURCES];

/* MP IRQ source entries */
int mp_irq_entries;

int nr_ioapics;

int pic_mode;
unsigned long mp_lapic_addr;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid = -1U;
unsigned int boot_cpu_logical_apicid = -1U;
/* Internal processor count */
static unsigned int __initdata num_processors;

/* Bitmask of physically existing CPUs */
unsigned long phys_cpu_present_map;

u8 bios_cpu_apicid[NR_CPUS] = { [0 ... NR_CPUS-1] = BAD_APICID };

/*
 * Intel MP BIOS table parsing routines:
 */


/*
 * Checksum an MP configuration block.
 */

static int __init mpf_checksum(unsigned char *mp, int len)
{
	int sum = 0;

	while (len--)
		sum += *mp++;

	return sum & 0xFF;
}

/*
 * Have to match translation table entries to main table entries by counter
 * hence the mpc_record variable .... can't see a less disgusting way of
 * doing this ....
 */

static int mpc_record; 
static struct mpc_config_translation *translation_table[MAX_MPC_ENTRY] __initdata;

void __init MP_processor_info (struct mpc_config_processor *m)
{
 	int ver, apicid;
 	
	if (!(m->mpc_cpuflag & CPU_ENABLED))
		return;

	apicid = mpc_apic_id(m, translation_table[mpc_record]);

	if (m->mpc_featureflag&(1<<0))
		Dprintk("    Floating point unit present.\n");
	if (m->mpc_featureflag&(1<<7))
		Dprintk("    Machine Exception supported.\n");
	if (m->mpc_featureflag&(1<<8))
		Dprintk("    64 bit compare & exchange supported.\n");
	if (m->mpc_featureflag&(1<<9))
		Dprintk("    Internal APIC present.\n");
	if (m->mpc_featureflag&(1<<11))
		Dprintk("    SEP present.\n");
	if (m->mpc_featureflag&(1<<12))
		Dprintk("    MTRR  present.\n");
	if (m->mpc_featureflag&(1<<13))
		Dprintk("    PGE  present.\n");
	if (m->mpc_featureflag&(1<<14))
		Dprintk("    MCA  present.\n");
	if (m->mpc_featureflag&(1<<15))
		Dprintk("    CMOV  present.\n");
	if (m->mpc_featureflag&(1<<16))
		Dprintk("    PAT  present.\n");
	if (m->mpc_featureflag&(1<<17))
		Dprintk("    PSE  present.\n");
	if (m->mpc_featureflag&(1<<18))
		Dprintk("    PSN  present.\n");
	if (m->mpc_featureflag&(1<<19))
		Dprintk("    Cache Line Flush Instruction present.\n");
	/* 20 Reserved */
	if (m->mpc_featureflag&(1<<21))
		Dprintk("    Debug Trace and EMON Store present.\n");
	if (m->mpc_featureflag&(1<<22))
		Dprintk("    ACPI Thermal Throttle Registers  present.\n");
	if (m->mpc_featureflag&(1<<23))
		Dprintk("    MMX  present.\n");
	if (m->mpc_featureflag&(1<<24))
		Dprintk("    FXSR  present.\n");
	if (m->mpc_featureflag&(1<<25))
		Dprintk("    XMM  present.\n");
	if (m->mpc_featureflag&(1<<26))
		Dprintk("    Willamette New Instructions  present.\n");
	if (m->mpc_featureflag&(1<<27))
		Dprintk("    Self Snoop  present.\n");
	if (m->mpc_featureflag&(1<<28))
		Dprintk("    HT  present.\n");
	if (m->mpc_featureflag&(1<<29))
		Dprintk("    Thermal Monitor present.\n");
	/* 30, 31 Reserved */


	if (m->mpc_cpuflag & CPU_BOOTPROCESSOR) {
		Dprintk("    Bootup CPU\n");
		boot_cpu_physical_apicid = m->mpc_apicid;
		boot_cpu_logical_apicid = apicid;
	}

	num_processors++;

	if (MAX_APICS - m->mpc_apicid <= 0) {
		printk(KERN_WARNING "Processor #%d INVALID. (Max ID: %d).\n",
			m->mpc_apicid, MAX_APICS);
		--num_processors;
		return;
	}
	ver = m->mpc_apicver;

	phys_cpu_present_map |= apicid_to_cpu_present(apicid);
	
	/*
	 * Validate version
	 */
	if (ver == 0x0) {
		printk(KERN_WARNING "BIOS bug, APIC version is 0 for CPU#%d! fixing up to 0x10. (tell your hw vendor)\n", m->mpc_apicid);
		ver = 0x10;
	}
	apic_version[m->mpc_apicid] = ver;
	bios_cpu_apicid[num_processors - 1] = m->mpc_apicid;
}

static void __init MP_bus_info (struct mpc_config_bus *m)
{
	char str[7];

	memcpy(str, m->mpc_bustype, 6);
	str[6] = 0;

	mpc_oem_bus_info(m, str, translation_table[mpc_record]);

	if (strncmp(str, BUSTYPE_ISA, sizeof(BUSTYPE_ISA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_ISA;
	} else if (strncmp(str, BUSTYPE_EISA, sizeof(BUSTYPE_EISA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_EISA;
	} else if (strncmp(str, BUSTYPE_PCI, sizeof(BUSTYPE_PCI)-1) == 0) {
		mpc_oem_pci_bus(m, translation_table[mpc_record]);
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_PCI;
		mp_bus_id_to_pci_bus[m->mpc_busid] = mp_current_pci_id;
		mp_current_pci_id++;
	} else if (strncmp(str, BUSTYPE_MCA, sizeof(BUSTYPE_MCA)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_MCA;
	} else if (strncmp(str, BUSTYPE_NEC98, sizeof(BUSTYPE_NEC98)-1) == 0) {
		mp_bus_id_to_type[m->mpc_busid] = MP_BUS_NEC98;
	} else {
		printk(KERN_WARNING "Unknown bustype %s - ignoring\n", str);
	}
}

static void __init MP_ioapic_info (struct mpc_config_ioapic *m)
{
	if (!(m->mpc_flags & MPC_APIC_USABLE))
		return;

	printk(KERN_INFO "I/O APIC #%d Version %d at 0x%lX.\n",
		m->mpc_apicid, m->mpc_apicver, m->mpc_apicaddr);
	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_CRIT "Max # of I/O APICs (%d) exceeded (found %d).\n",
			MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!.\n");
	}
	if (!m->mpc_apicaddr) {
		printk(KERN_ERR "WARNING: bogus zero I/O APIC address"
			" found in MP table, skipping!\n");
		return;
	}
	mp_ioapics[nr_ioapics] = *m;
	nr_ioapics++;
}

static void __init MP_intsrc_info (struct mpc_config_intsrc *m)
{
	mp_irqs [mp_irq_entries] = *m;
	Dprintk("Int: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC INT %02x\n",
			m->mpc_irqtype, m->mpc_irqflag & 3,
			(m->mpc_irqflag >> 2) & 3, m->mpc_srcbus,
			m->mpc_srcbusirq, m->mpc_dstapic, m->mpc_dstirq);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

static void __init MP_lintsrc_info (struct mpc_config_lintsrc *m)
{
	Dprintk("Lint: type %d, pol %d, trig %d, bus %d,"
		" IRQ %02x, APIC ID %x, APIC LINT %02x\n",
			m->mpc_irqtype, m->mpc_irqflag & 3,
			(m->mpc_irqflag >> 2) &3, m->mpc_srcbusid,
			m->mpc_srcbusirq, m->mpc_destapic, m->mpc_destapiclint);
	/*
	 * Well it seems all SMP boards in existence
	 * use ExtINT/LVT1 == LINT0 and
	 * NMI/LVT2 == LINT1 - the following check
	 * will show us if this assumptions is false.
	 * Until then we do not have to add baggage.
	 */
	if ((m->mpc_irqtype == mp_ExtINT) &&
		(m->mpc_destapiclint != 0))
			BUG();
	if ((m->mpc_irqtype == mp_NMI) &&
		(m->mpc_destapiclint != 1))
			BUG();
}

#ifdef CONFIG_X86_NUMAQ
static void __init MP_translation_info (struct mpc_config_translation *m)
{
	printk(KERN_INFO "Translation: record %d, type %d, quad %d, global %d, local %d\n", mpc_record, m->trans_type, m->trans_quad, m->trans_global, m->trans_local);

	if (mpc_record >= MAX_MPC_ENTRY) 
		printk(KERN_ERR "MAX_MPC_ENTRY exceeded!\n");
	else
		translation_table[mpc_record] = m; /* stash this for later */
	if (m->trans_quad+1 > numnodes)
		numnodes = m->trans_quad+1;
}

/*
 * Read/parse the MPC oem tables
 */

static void __init smp_read_mpc_oem(struct mp_config_oemtable *oemtable, \
	unsigned short oemsize)
{
	int count = sizeof (*oemtable); /* the header size */
	unsigned char *oemptr = ((unsigned char *)oemtable)+count;
	
	mpc_record = 0;
	printk(KERN_INFO "Found an OEM MPC table at %8p - parsing it ... \n", oemtable);
	if (memcmp(oemtable->oem_signature,MPC_OEM_SIGNATURE,4))
	{
		printk(KERN_WARNING "SMP mpc oemtable: bad signature [%c%c%c%c]!\n",
			oemtable->oem_signature[0],
			oemtable->oem_signature[1],
			oemtable->oem_signature[2],
			oemtable->oem_signature[3]);
		return;
	}
	if (mpf_checksum((unsigned char *)oemtable,oemtable->oem_length))
	{
		printk(KERN_WARNING "SMP oem mptable: checksum error!\n");
		return;
	}
	while (count < oemtable->oem_length) {
		switch (*oemptr) {
			case MP_TRANSLATION:
			{
				struct mpc_config_translation *m=
					(struct mpc_config_translation *)oemptr;
				MP_translation_info(m);
				oemptr += sizeof(*m);
				count += sizeof(*m);
				++mpc_record;
				break;
			}
			default:
			{
				printk(KERN_WARNING "Unrecognised OEM table entry type! - %d\n", (int) *oemptr);
				return;
			}
		}
       }
}
#endif	/* CONFIG_X86_NUMAQ */

/*
 * Read/parse the MPC
 */

static int __init smp_read_mpc(struct mp_config_table *mpc)
{
	char str[16];
	char oem[10];
	int count=sizeof(*mpc);
	unsigned char *mpt=((unsigned char *)mpc)+count;

	if (memcmp(mpc->mpc_signature,MPC_SIGNATURE,4)) {
		panic("SMP mptable: bad signature [%c%c%c%c]!\n",
			mpc->mpc_signature[0],
			mpc->mpc_signature[1],
			mpc->mpc_signature[2],
			mpc->mpc_signature[3]);
		return 0;
	}
	if (mpf_checksum((unsigned char *)mpc,mpc->mpc_length)) {
		panic("SMP mptable: checksum error!\n");
		return 0;
	}
	if (mpc->mpc_spec!=0x01 && mpc->mpc_spec!=0x04) {
		printk(KERN_ERR "SMP mptable: bad table version (%d)!!\n",
			mpc->mpc_spec);
		return 0;
	}
	if (!mpc->mpc_lapic) {
		printk(KERN_ERR "SMP mptable: null local APIC address!\n");
		return 0;
	}
	memcpy(oem,mpc->mpc_oem,8);
	oem[8]=0;
	printk(KERN_INFO "OEM ID: %s ",oem);

	memcpy(str,mpc->mpc_productid,12);
	str[12]=0;
	printk("Product ID: %s ",str);

	mps_oem_check(mpc, oem, str);

	printk("APIC at: 0x%lX\n",mpc->mpc_lapic);

	/* 
	 * Save the local APIC address (it might be non-default) -- but only
	 * if we're not using ACPI.
	 */
	if (!acpi_lapic)
		mp_lapic_addr = mpc->mpc_lapic;

	/*
	 *	Now process the configuration blocks.
	 */
	mpc_record = 0;
	while (count < mpc->mpc_length) {
		switch(*mpt) {
			case MP_PROCESSOR:
			{
				struct mpc_config_processor *m=
					(struct mpc_config_processor *)mpt;
				/* ACPI may have already provided this data */
				if (!acpi_lapic)
					MP_processor_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
			case MP_BUS:
			{
				struct mpc_config_bus *m=
					(struct mpc_config_bus *)mpt;
				MP_bus_info(m);
				mpt += sizeof(*m);
				count += sizeof(*m);
				break;
			}
			case MP_IOAPIC:
			{
				struct mpc_config_ioapic *m=
					(struct mpc_config_ioapic *)mpt;
				MP_ioapic_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_INTSRC:
			{
				struct mpc_config_intsrc *m=
					(struct mpc_config_intsrc *)mpt;

				MP_intsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			case MP_LINTSRC:
			{
				struct mpc_config_lintsrc *m=
					(struct mpc_config_lintsrc *)mpt;
				MP_lintsrc_info(m);
				mpt+=sizeof(*m);
				count+=sizeof(*m);
				break;
			}
			default:
			{
				count = mpc->mpc_length;
				break;
			}
		}
		++mpc_record;
	}
	clustered_apic_check();
	if (!num_processors)
		printk(KERN_ERR "SMP mptable: no processors registered!\n");
	return num_processors;
}

static int __init ELCR_trigger(unsigned int irq)
{
	unsigned int port;

	port = 0x4d0 + (irq >> 3);
	return (inb(port) >> (irq & 7)) & 1;
}

static void __init construct_default_ioirq_mptable(int mpc_default_type)
{
	struct mpc_config_intsrc intsrc;
	int i;
	int ELCR_fallback = 0;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqflag = 0;			/* conforming */
	intsrc.mpc_srcbus = 0;
	intsrc.mpc_dstapic = mp_ioapics[0].mpc_apicid;

	intsrc.mpc_irqtype = mp_INT;

	/*
	 *  If true, we have an ISA/PCI system with no IRQ entries
	 *  in the MP table. To prevent the PCI interrupts from being set up
	 *  incorrectly, we try to use the ELCR. The sanity check to see if
	 *  there is good ELCR data is very simple - IRQ0, 1, 2 and 13 can
	 *  never be level sensitive, so we simply see if the ELCR agrees.
	 *  If it does, we assume it's valid.
	 */
	if (mpc_default_type == 5) {
		printk(KERN_INFO "ISA/PCI bus type with no IRQ information... falling back to ELCR\n");

		if (ELCR_trigger(0) || ELCR_trigger(1) || ELCR_trigger(2) || ELCR_trigger(13))
			printk(KERN_WARNING "ELCR contains invalid data... not using ELCR\n");
		else {
			printk(KERN_INFO "Using ELCR to identify PCI interrupts\n");
			ELCR_fallback = 1;
		}
	}

	for (i = 0; i < 16; i++) {
		switch (mpc_default_type) {
		case 2:
			if (i == 0 || i == 13)
				continue;	/* IRQ0 & IRQ13 not connected */
			/* fall through */
		default:
			if (i == 2)
				continue;	/* IRQ2 is never connected */
		}

		if (ELCR_fallback) {
			/*
			 *  If the ELCR indicates a level-sensitive interrupt, we
			 *  copy that information over to the MP table in the
			 *  irqflag field (level sensitive, active high polarity).
			 */
			if (ELCR_trigger(i))
				intsrc.mpc_irqflag = 13;
			else
				intsrc.mpc_irqflag = 0;
		}

		intsrc.mpc_srcbusirq = i;
		intsrc.mpc_dstirq = i ? i : 2;		/* IRQ0 to INTIN2 */
		MP_intsrc_info(&intsrc);
	}

	intsrc.mpc_irqtype = mp_ExtINT;
	intsrc.mpc_srcbusirq = 0;
	intsrc.mpc_dstirq = 0;				/* 8259A to INTIN0 */
	MP_intsrc_info(&intsrc);
}

static inline void __init construct_default_ISA_mptable(int mpc_default_type)
{
	struct mpc_config_processor processor;
	struct mpc_config_bus bus;
	struct mpc_config_ioapic ioapic;
	struct mpc_config_lintsrc lintsrc;
	int linttypes[2] = { mp_ExtINT, mp_NMI };
	int i;

	/*
	 * local APIC has default address
	 */
	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	/*
	 * 2 CPUs, numbered 0 & 1.
	 */
	processor.mpc_type = MP_PROCESSOR;
	/* Either an integrated APIC or a discrete 82489DX. */
	processor.mpc_apicver = mpc_default_type > 4 ? 0x10 : 0x01;
	processor.mpc_cpuflag = CPU_ENABLED;
	processor.mpc_cpufeature = (boot_cpu_data.x86 << 8) |
				   (boot_cpu_data.x86_model << 4) |
				   boot_cpu_data.x86_mask;
	processor.mpc_featureflag = boot_cpu_data.x86_capability[0];
	processor.mpc_reserved[0] = 0;
	processor.mpc_reserved[1] = 0;
	for (i = 0; i < 2; i++) {
		processor.mpc_apicid = i;
		MP_processor_info(&processor);
	}

	bus.mpc_type = MP_BUS;
	bus.mpc_busid = 0;
	switch (mpc_default_type) {
		default:
			printk("???\n");
			printk(KERN_ERR "Unknown standard configuration %d\n",
				mpc_default_type);
			/* fall through */
		case 1:
		case 5:
			memcpy(bus.mpc_bustype, "ISA   ", 6);
			break;
		case 2:
		case 6:
		case 3:
			memcpy(bus.mpc_bustype, "EISA  ", 6);
			break;
		case 4:
		case 7:
			memcpy(bus.mpc_bustype, "MCA   ", 6);
	}
	MP_bus_info(&bus);
	if (mpc_default_type > 4) {
		bus.mpc_busid = 1;
		memcpy(bus.mpc_bustype, "PCI   ", 6);
		MP_bus_info(&bus);
	}

	ioapic.mpc_type = MP_IOAPIC;
	ioapic.mpc_apicid = 2;
	ioapic.mpc_apicver = mpc_default_type > 4 ? 0x10 : 0x01;
	ioapic.mpc_flags = MPC_APIC_USABLE;
	ioapic.mpc_apicaddr = 0xFEC00000;
	MP_ioapic_info(&ioapic);

	/*
	 * We set up most of the low 16 IO-APIC pins according to MPS rules.
	 */
	construct_default_ioirq_mptable(mpc_default_type);

	lintsrc.mpc_type = MP_LINTSRC;
	lintsrc.mpc_irqflag = 0;		/* conforming */
	lintsrc.mpc_srcbusid = 0;
	lintsrc.mpc_srcbusirq = 0;
	lintsrc.mpc_destapic = MP_APIC_ALL;
	for (i = 0; i < 2; i++) {
		lintsrc.mpc_irqtype = linttypes[i];
		lintsrc.mpc_destapiclint = i;
		MP_lintsrc_info(&lintsrc);
	}
}

static struct intel_mp_floating *mpf_found;

/*
 * Scan the memory blocks for an SMP configuration block.
 */
void __init get_smp_config (void)
{
	struct intel_mp_floating *mpf = mpf_found;

	/*
	 * ACPI may be used to obtain the entire SMP configuration or just to 
	 * enumerate/configure processors (CONFIG_ACPI_HT_ONLY).  Note that 
	 * ACPI supports both logical (e.g. Hyper-Threading) and physical 
	 * processors, where MPS only supports physical.
	 */
	if (acpi_lapic && acpi_ioapic) {
		printk(KERN_INFO "Using ACPI (MADT) for SMP configuration information\n");
		return;
	}
	else if (acpi_lapic)
		printk(KERN_INFO "Using ACPI for processor (LAPIC) configuration information\n");

	printk(KERN_INFO "Intel MultiProcessor Specification v1.%d\n", mpf->mpf_specification);
	if (mpf->mpf_feature2 & (1<<7)) {
		printk(KERN_INFO "    IMCR and PIC compatibility mode.\n");
		pic_mode = 1;
	} else {
		printk(KERN_INFO "    Virtual Wire compatibility mode.\n");
		pic_mode = 0;
	}

	/*
	 * Now see if we need to read further.
	 */
	if (mpf->mpf_feature1 != 0) {

		printk(KERN_INFO "Default MP configuration #%d\n", mpf->mpf_feature1);
		construct_default_ISA_mptable(mpf->mpf_feature1);

	} else if (mpf->mpf_physptr) {

		/*
		 * Read the physical hardware table.  Anything here will
		 * override the defaults.
		 */
		if (!smp_read_mpc((void *)mpf->mpf_physptr)) {
			smp_found_config = 0;
			printk(KERN_ERR "BIOS bug, MP table errors detected!...\n");
			printk(KERN_ERR "... disabling SMP support. (tell your hw vendor)\n");
			return;
		}
		/*
		 * If there are no explicit MP IRQ entries, then we are
		 * broken.  We set up most of the low 16 IO-APIC pins to
		 * ISA defaults and hope it will work.
		 */
		if (!mp_irq_entries) {
			struct mpc_config_bus bus;

			printk(KERN_ERR "BIOS bug, no explicit IRQ entries, using default mptable. (tell your hw vendor)\n");

			bus.mpc_type = MP_BUS;
			bus.mpc_busid = 0;
			memcpy(bus.mpc_bustype, "ISA   ", 6);
			MP_bus_info(&bus);

			construct_default_ioirq_mptable(0);
		}

	} else
		BUG();

	printk(KERN_INFO "Processors: %d\n", num_processors);
	/*
	 * Only use the first configuration found.
	 */
}

static int __init smp_scan_config (unsigned long base, unsigned long length)
{
	unsigned long *bp = phys_to_virt(base);
	struct intel_mp_floating *mpf;

	Dprintk("Scan SMP from %p for %ld bytes.\n", bp,length);
	if (sizeof(*mpf) != 16)
		printk("Error: MPF size\n");

	while (length > 0) {
		mpf = (struct intel_mp_floating *)bp;
		if ((*bp == SMP_MAGIC_IDENT) &&
			(mpf->mpf_length == 1) &&
			!mpf_checksum((unsigned char *)bp, 16) &&
			((mpf->mpf_specification == 1)
				|| (mpf->mpf_specification == 4)) ) {

			smp_found_config = 1;
			printk(KERN_INFO "found SMP MP-table at %08lx\n",
						virt_to_phys(mpf));
			reserve_bootmem(virt_to_phys(mpf), PAGE_SIZE);
			if (mpf->mpf_physptr) {
				/*
				 * We cannot access to MPC table to compute
				 * table size yet, as only few megabytes from
				 * the bottom is mapped now.
				 * PC-9800's MPC table places on the very last
				 * of physical memory; so that simply reserving
				 * PAGE_SIZE from mpg->mpf_physptr yields BUG()
				 * in reserve_bootmem.
				 */
				unsigned long size = PAGE_SIZE;
				unsigned long end = max_low_pfn * PAGE_SIZE;
				if (mpf->mpf_physptr + size > end)
					size = end - mpf->mpf_physptr;
				reserve_bootmem(mpf->mpf_physptr, size);
			}

			mpf_found = mpf;
			return 1;
		}
		bp += 4;
		length -= 16;
	}
	return 0;
}

void __init find_smp_config (void)
{
	unsigned int address;

	/*
	 * FIXME: Linux assumes you have 640K of base ram..
	 * this continues the error...
	 *
	 * 1) Scan the bottom 1K for a signature
	 * 2) Scan the top 1K of base RAM
	 * 3) Scan the 64K of bios
	 */
	if (smp_scan_config(0x0,0x400) ||
		smp_scan_config(639*0x400,0x400) ||
			smp_scan_config(0xF0000,0x10000))
		return;
	/*
	 * If it is an SMP machine we should know now, unless the
	 * configuration is in an EISA/MCA bus machine with an
	 * extended bios data area.
	 *
	 * there is a real-mode segmented pointer pointing to the
	 * 4K EBDA area at 0x40E, calculate and scan it here.
	 *
	 * NOTE! There are Linux loaders that will corrupt the EBDA
	 * area, and as such this kind of SMP config may be less
	 * trustworthy, simply because the SMP table may have been
	 * stomped on during early boot. These loaders are buggy and
	 * should be fixed.
	 *
	 * MP1.4 SPEC states to only scan first 1K of 4K EBDA.
	 */

	address = get_bios_ebda();
	if (address)
		smp_scan_config(address, 0x400);
}


/* --------------------------------------------------------------------------
                            ACPI-based MP Configuration
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_BOOT

void __init mp_register_lapic_address (
	u64			address)
{
	mp_lapic_addr = (unsigned long) address;

	set_fixmap_nocache(FIX_APIC_BASE, mp_lapic_addr);

	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = GET_APIC_ID(apic_read(APIC_ID));

	Dprintk("Boot CPU = %d\n", boot_cpu_physical_apicid);
}


void __init mp_register_lapic (
	u8			id, 
	u8			enabled)
{
	struct mpc_config_processor processor;
	int			boot_cpu = 0;
	
	if (MAX_APICS - id <= 0) {
		printk(KERN_WARNING "Processor #%d invalid (max %d)\n",
			id, MAX_APICS);
		return;
	}

	if (id == boot_cpu_physical_apicid)
		boot_cpu = 1;

	processor.mpc_type = MP_PROCESSOR;
	processor.mpc_apicid = id;
	processor.mpc_apicver = GET_APIC_VERSION(apic_read(APIC_LVR));
	processor.mpc_cpuflag = (enabled ? CPU_ENABLED : 0);
	processor.mpc_cpuflag |= (boot_cpu ? CPU_BOOTPROCESSOR : 0);
	processor.mpc_cpufeature = (boot_cpu_data.x86 << 8) | 
		(boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	processor.mpc_featureflag = boot_cpu_data.x86_capability[0];
	processor.mpc_reserved[0] = 0;
	processor.mpc_reserved[1] = 0;

	MP_processor_info(&processor);
}

#ifdef CONFIG_X86_IO_APIC

#define MP_ISA_BUS		0
#define MP_MAX_IOAPIC_PIN	127

struct mp_ioapic_routing {
	int			apic_id;
	int			irq_start;
	int			irq_end;
	u32			pin_programmed[4];
} mp_ioapic_routing[MAX_IO_APICS];


static int __init mp_find_ioapic (
	int			irq)
{
	int			i = 0;

	/* Find the IOAPIC that manages this IRQ. */
	for (i = 0; i < nr_ioapics; i++) {
		if ((irq >= mp_ioapic_routing[i].irq_start)
			&& (irq <= mp_ioapic_routing[i].irq_end))
			return i;
	}

	printk(KERN_ERR "ERROR: Unable to locate IOAPIC for IRQ %d/n", irq);

	return -1;
}
	

void __init mp_register_ioapic (
	u8			id, 
	u32			address,
	u32			irq_base)
{
	int			idx = 0;

	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_ERR "ERROR: Max # of I/O APICs (%d) exceeded "
			"(found %d)\n", MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
	if (!address) {
		printk(KERN_ERR "WARNING: Bogus (zero) I/O APIC address"
			" found in MADT table, skipping!\n");
		return;
	}

	idx = nr_ioapics++;

	mp_ioapics[idx].mpc_type = MP_IOAPIC;
	mp_ioapics[idx].mpc_flags = MPC_APIC_USABLE;
	mp_ioapics[idx].mpc_apicaddr = address;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, address);
	mp_ioapics[idx].mpc_apicid = io_apic_get_unique_id(idx, id);
	mp_ioapics[idx].mpc_apicver = io_apic_get_version(idx);
	
	/* 
	 * Build basic IRQ lookup table to facilitate irq->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI IRQs).
	 */
	mp_ioapic_routing[idx].apic_id = mp_ioapics[idx].mpc_apicid;
	mp_ioapic_routing[idx].irq_start = irq_base;
	mp_ioapic_routing[idx].irq_end = irq_base + 
		io_apic_get_redir_entries(idx);

	printk("IOAPIC[%d]: apic_id %d, version %d, address 0x%lx, "
		"IRQ %d-%d\n", idx, mp_ioapics[idx].mpc_apicid, 
		mp_ioapics[idx].mpc_apicver, mp_ioapics[idx].mpc_apicaddr,
		mp_ioapic_routing[idx].irq_start,
		mp_ioapic_routing[idx].irq_end);

	return;
}


void __init mp_override_legacy_irq (
	u8			bus_irq,
	u8			polarity, 
	u8			trigger, 
	u32			global_irq)
{
	struct mpc_config_intsrc intsrc;
	int			i = 0;
	int			found = 0;
	int			ioapic = -1;
	int			pin = -1;

	/* 
	 * Convert 'global_irq' to 'ioapic.pin'.
	 */
	ioapic = mp_find_ioapic(global_irq);
	if (ioapic < 0)
		return;
	pin = global_irq - mp_ioapic_routing[ioapic].irq_start;

	/*
	 * TBD: This check is for faulty timer entries, where the override
	 *      erroneously sets the trigger to level, resulting in a HUGE 
	 *      increase of timer interrupts!
	 */
	if ((bus_irq == 0) && (global_irq == 2) && (trigger == 3))
		trigger = 1;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqtype = mp_INT;
	intsrc.mpc_irqflag = (trigger << 2) | polarity;
	intsrc.mpc_srcbus = MP_ISA_BUS;
	intsrc.mpc_srcbusirq = bus_irq;				       /* IRQ */
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;	   /* APIC ID */
	intsrc.mpc_dstirq = pin;				    /* INTIN# */

	Dprintk("Int: type %d, pol %d, trig %d, bus %d, irq %d, %d-%d\n",
		intsrc.mpc_irqtype, intsrc.mpc_irqflag & 3, 
		(intsrc.mpc_irqflag >> 2) & 3, intsrc.mpc_srcbus, 
		intsrc.mpc_srcbusirq, intsrc.mpc_dstapic, intsrc.mpc_dstirq);

	/* 
	 * If an existing [IOAPIC.PIN -> IRQ] routing entry exists we override it.
	 * Otherwise create a new entry (e.g. global_irq == 2).
	 */
	for (i = 0; i < mp_irq_entries; i++) {
		if ((mp_irqs[i].mpc_dstapic == intsrc.mpc_dstapic) 
			&& (mp_irqs[i].mpc_srcbusirq == intsrc.mpc_srcbusirq)) {
			mp_irqs[i] = intsrc;
			found = 1;
			break;
		}
	}
	if (!found) {
		mp_irqs[mp_irq_entries] = intsrc;
		if (++mp_irq_entries == MAX_IRQ_SOURCES)
			panic("Max # of irq sources exceeded!\n");
	}

	return;
}


void __init mp_config_acpi_legacy_irqs (void)
{
	struct mpc_config_intsrc intsrc;
	int			i = 0;
	int			ioapic = -1;

	/* 
	 * Fabricate the legacy ISA bus (bus #31).
	 */
	mp_bus_id_to_type[MP_ISA_BUS] = MP_BUS_ISA;
	Dprintk("Bus #%d is ISA\n", MP_ISA_BUS);

	/* 
	 * Locate the IOAPIC that manages the ISA IRQs (0-15). 
	 */
	ioapic = mp_find_ioapic(0);
	if (ioapic < 0)
		return;

	intsrc.mpc_type = MP_INTSRC;
	intsrc.mpc_irqflag = 0;					/* Conforming */
	intsrc.mpc_srcbus = MP_ISA_BUS;
	intsrc.mpc_dstapic = mp_ioapics[ioapic].mpc_apicid;

	/* 
	 * Use the default configuration for the IRQs 0-15.  These may be
	 * overriden by (MADT) interrupt source override entries.
	 */
	for (i = 0; i < 16; i++) {

		if (i == 2) continue;			/* Don't connect IRQ2 */

		intsrc.mpc_irqtype = i ? mp_INT : mp_ExtINT;   /* 8259A to #0 */
		intsrc.mpc_srcbusirq = i;		   /* Identity mapped */
		intsrc.mpc_dstirq = i;

		Dprintk("Int: type %d, pol %d, trig %d, bus %d, irq %d, "
			"%d-%d\n", intsrc.mpc_irqtype, intsrc.mpc_irqflag & 3, 
			(intsrc.mpc_irqflag >> 2) & 3, intsrc.mpc_srcbus, 
			intsrc.mpc_srcbusirq, intsrc.mpc_dstapic, 
			intsrc.mpc_dstirq);

		mp_irqs[mp_irq_entries] = intsrc;
		if (++mp_irq_entries == MAX_IRQ_SOURCES)
			panic("Max # of irq sources exceeded!\n");
	}
}

#ifndef CONFIG_ACPI_HT_ONLY

/* Ensure the ACPI SCI interrupt level is active low, edge-triggered */

extern FADT_DESCRIPTOR acpi_fadt;

void __init mp_config_ioapic_for_sci(int irq)
{
	int ioapic;
	int ioapic_pin;
	struct acpi_table_madt *madt;
	struct acpi_table_int_src_ovr *entry = NULL;
	void *madt_end;
	acpi_status status;

	/*
	 * Ensure that if there is an interrupt source override entry
	 * for the ACPI SCI, we leave it as is. Unfortunately this involves
	 * walking the MADT again.
	 */
	status = acpi_get_firmware_table("APIC", 1, ACPI_LOGICAL_ADDRESSING,
		(struct acpi_table_header **) &madt);
	if (ACPI_SUCCESS(status)) {
		madt_end = (void *) (unsigned long)madt + madt->header.length;

		entry = (struct acpi_table_int_src_ovr *)
                ((unsigned long) madt + sizeof(struct acpi_table_madt));

		while ((void *) entry < madt_end) {
                	if (entry->header.type == ACPI_MADT_INT_SRC_OVR &&
			    acpi_fadt.sci_int == entry->bus_irq) {
				/*
				 * See the note at the end of ACPI 2.0b section
				 * 5.2.10.8 for what this is about.
				 */
				if (entry->bus_irq != entry->global_irq) {
					acpi_fadt.sci_int = entry->global_irq;
					irq = entry->global_irq;
					break;
				}
				else
                			return;
			}

                	entry = (struct acpi_table_int_src_ovr *)
                	        ((unsigned long) entry + entry->header.length);
        	}
	}

	ioapic = mp_find_ioapic(irq);

	ioapic_pin = irq - mp_ioapic_routing[ioapic].irq_start;

	io_apic_set_pci_routing(ioapic, ioapic_pin, irq);
}

#endif /*CONFIG_ACPI_HT_ONLY*/

#ifdef CONFIG_ACPI_PCI

void __init mp_parse_prt (void)
{
	struct list_head	*node = NULL;
	struct acpi_prt_entry	*entry = NULL;
	int			ioapic = -1;
	int			ioapic_pin = 0;
	int			irq = 0;
	int			idx, bit = 0;

	/*
	 * Parsing through the PCI Interrupt Routing Table (PRT) and program
	 * routing for all entries.
	 */
	list_for_each(node, &acpi_prt.entries) {
		entry = list_entry(node, struct acpi_prt_entry, node);

		/* Need to get irq for dynamic entry */
		if (entry->link.handle) {
			irq = acpi_pci_link_get_irq(entry->link.handle, entry->link.index);
			if (!irq)
				continue;
		}
		else
			irq = entry->link.index;

		/* Don't set up the ACPI SCI because it's already set up */
		if (acpi_fadt.sci_int == irq)
			continue;
	
		ioapic = mp_find_ioapic(irq);
		if (ioapic < 0)
			continue;
		ioapic_pin = irq - mp_ioapic_routing[ioapic].irq_start;

		if (!ioapic && (irq < 16))
			irq += 16;
		/* 
		 * Avoid pin reprogramming.  PRTs typically include entries  
		 * with redundant pin->irq mappings (but unique PCI devices);
		 * we only only program the IOAPIC on the first.
		 */
		bit = ioapic_pin % 32;
		idx = (ioapic_pin < 32) ? 0 : (ioapic_pin / 32);
		if (idx > 3) {
			printk(KERN_ERR "Invalid reference to IOAPIC pin "
				"%d-%d\n", mp_ioapic_routing[ioapic].apic_id, 
				ioapic_pin);
			continue;
		}
		if ((1<<bit) & mp_ioapic_routing[ioapic].pin_programmed[idx]) {
			printk(KERN_DEBUG "Pin %d-%d already programmed\n",
				mp_ioapic_routing[ioapic].apic_id, ioapic_pin);
			entry->irq = irq;
			continue;
		}

		mp_ioapic_routing[ioapic].pin_programmed[idx] |= (1<<bit);

		if (!io_apic_set_pci_routing(ioapic, ioapic_pin, irq))
			entry->irq = irq;

		printk(KERN_DEBUG "%02x:%02x:%02x[%c] -> %d-%d -> IRQ %d\n",
			entry->id.segment, entry->id.bus, 
			entry->id.device, ('A' + entry->pin), 
			mp_ioapic_routing[ioapic].apic_id, ioapic_pin, 
			entry->irq);
	}
	
	return;
}

#endif /*CONFIG_ACPI_PCI*/

#endif /*CONFIG_X86_IO_APIC*/

#endif /*CONFIG_ACPI_BOOT*/
