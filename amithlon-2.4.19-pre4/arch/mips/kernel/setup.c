/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995  Linus Torvalds
 * Copyright (C) 1995  Waldorf Electronics
 * Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2001  Ralf Baechle
 * Copyright (C) 1996  Stoned Elipot
 * Copyright (C) 2000, 2001  Maciej W. Rozycki
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/bootmem.h>
#include <linux/blk.h>
#include <linux/ide.h>
#include <linux/timex.h>

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/stackframe.h>
#include <asm/system.h>
#ifdef CONFIG_SGI_IP22
#include <asm/sgialib.h>
#endif

#ifndef CONFIG_SMP
struct cpuinfo_mips cpu_data[1];
#endif

/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait. 
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void) = NULL;

/*
 * There are several bus types available for MIPS machines.  "RISC PC"
 * type machines have ISA, EISA, VLB or PCI available, DECstations
 * have Turbochannel or Q-Bus, SGI has GIO, there are lots of VME
 * boxes ...
 * This flag is set if a EISA slots are available.
 */
int EISA_bus = 0;

struct screen_info screen_info;

extern struct fd_ops no_fd_ops;
struct fd_ops *fd_ops;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
extern struct ide_ops no_ide_ops;
struct ide_ops *ide_ops;
#endif

extern void * __rd_start, * __rd_end;

extern struct rtc_ops no_rtc_ops;
struct rtc_ops *rtc_ops;

#ifdef CONFIG_PC_KEYB
extern struct kbd_ops no_kbd_ops;
struct kbd_ops *kbd_ops;
#endif

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */
unsigned long mips_machtype = MACH_UNKNOWN;
unsigned long mips_machgroup = MACH_GROUP_UNKNOWN;

struct boot_mem_map boot_mem_map;

unsigned char aux_device_present;
extern char _ftext, _etext, _fdata, _edata, _end;

static char command_line[CL_SIZE];
       char saved_command_line[CL_SIZE];
extern char arcs_cmdline[CL_SIZE];

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
const unsigned long mips_io_port_base = -1;
EXPORT_SYMBOL(mips_io_port_base);


/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;
EXPORT_SYMBOL(isa_slot_offset);

extern void sgi_sysinit(void);
extern void SetUpBootInfo(void);
extern void loadmmu(void);
extern asmlinkage void start_kernel(void);
extern void prom_init(int, char **, char **, int *);

static struct resource code_resource = { "Kernel code" };
static struct resource data_resource = { "Kernel data" };

static inline void check_wait(void)
{
	printk("Checking for 'wait' instruction... ");
	switch(mips_cpu.cputype) {
	case CPU_R3081:
	case CPU_R3081E:
		cpu_wait = r3081_wait;
		printk(" available.\n");
		break;
	case CPU_TX3927:
	case CPU_TX39XX:
		cpu_wait = r39xx_wait;
		printk(" available.\n");
		break;
	case CPU_R4200: 
/*	case CPU_R4300: */
	case CPU_R4600: 
	case CPU_R4640: 
	case CPU_R4650: 
	case CPU_R4700: 
	case CPU_R5000: 
	case CPU_NEVADA:
	case CPU_RM7000:
	case CPU_TX49XX:
		cpu_wait = r4k_wait;
		printk(" available.\n");
		break;
	default:
		printk(" unavailable.\n");
		break;
	}
}

void __init check_bugs(void)
{
	check_wait();
}

/*
 * Probe whether cpu has config register by trying to play with
 * alternate cache bit and see whether it matters.
 * It's used by cpu_probe to distinguish between R3000A and R3081.
 */
static inline int cpu_has_confreg(void)
{
#ifdef CONFIG_CPU_R3000
	extern unsigned long r3k_cache_size(unsigned long);
	unsigned long size1, size2; 
	unsigned long cfg = read_32bit_cp0_register(CP0_CONF);

	size1 = r3k_cache_size(ST0_ISC);
	write_32bit_cp0_register(CP0_CONF, cfg^CONF_AC);
	size2 = r3k_cache_size(ST0_ISC);
	write_32bit_cp0_register(CP0_CONF, cfg);
	return size1 != size2;
#else
	return 0;
#endif
}

/*
 * Get the FPU Implementation/Revision.
 */
static inline unsigned long cpu_get_fpu_id(void)
{
	unsigned long tmp, fpu_id;

	tmp = read_32bit_cp0_register(CP0_STATUS);
	__enable_fpu();
	fpu_id = read_32bit_cp1_register(CP1_REVISION);
	write_32bit_cp0_register(CP0_STATUS, tmp);
	return fpu_id;
}

/*
 * Check the CPU has an FPU the official way.
 */
static inline int cpu_has_fpu(void)
{
	return ((cpu_get_fpu_id() & 0xff00) != FPIR_IMP_NONE);
}

/* declaration of the global struct */
struct mips_cpu mips_cpu = {
    processor_id:	PRID_IMP_UNKNOWN,
    fpu_id:		FPIR_IMP_NONE,
    cputype:		CPU_UNKNOWN
};

/* Shortcut for assembler access to mips_cpu.options */
int *cpuoptions = &mips_cpu.options;

#define R4K_OPTS (MIPS_CPU_TLB | MIPS_CPU_4KEX | MIPS_CPU_4KTLB \
		| MIPS_CPU_COUNTER | MIPS_CPU_CACHE_CDEX)

static inline void cpu_probe(void)
{
#ifdef CONFIG_CPU_MIPS32
	unsigned long config1;
#endif

	mips_cpu.processor_id = read_32bit_cp0_register(CP0_PRID);
	switch (mips_cpu.processor_id & 0xff0000) {
	case PRID_COMP_LEGACY:
		switch (mips_cpu.processor_id & 0xff00) {
		case PRID_IMP_R2000:
			mips_cpu.cputype = CPU_R2000;
			mips_cpu.isa_level = MIPS_CPU_ISA_I;
			mips_cpu.options = MIPS_CPU_TLB;
			if (cpu_has_fpu())
				mips_cpu.options |= MIPS_CPU_FPU;
			mips_cpu.tlbsize = 64;
			break;
		case PRID_IMP_R3000:
			if ((mips_cpu.processor_id & 0xff) == PRID_REV_R3000A)
				if (cpu_has_confreg())
					mips_cpu.cputype = CPU_R3081E;
				else
					mips_cpu.cputype = CPU_R3000A;
			else
				mips_cpu.cputype = CPU_R3000;
			mips_cpu.isa_level = MIPS_CPU_ISA_I;
			mips_cpu.options = MIPS_CPU_TLB;
			if (cpu_has_fpu())
				mips_cpu.options |= MIPS_CPU_FPU;
			mips_cpu.tlbsize = 64;
			break;
		case PRID_IMP_R4000:
			if ((mips_cpu.processor_id & 0xff) == PRID_REV_R4400)
				mips_cpu.cputype = CPU_R4400SC;
			else
				mips_cpu.cputype = CPU_R4000SC;
			mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR | MIPS_CPU_WATCH |
			                   MIPS_CPU_VCE;
			mips_cpu.tlbsize = 48;
			break;
                case PRID_IMP_VR41XX:
                        mips_cpu.cputype = CPU_VR41XX;
                        mips_cpu.isa_level = MIPS_CPU_ISA_III;
                        mips_cpu.options = R4K_OPTS;
                        mips_cpu.tlbsize = 32;
                        break;
		case PRID_IMP_R4300:
			mips_cpu.cputype = CPU_R4300;
			mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR;
			mips_cpu.tlbsize = 32;
			break;
		case PRID_IMP_R4600:
			mips_cpu.cputype = CPU_R4600;
			mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU;
			mips_cpu.tlbsize = 48;
			break;
		#if 0
 		case PRID_IMP_R4650:
			/*
			 * This processor doesn't have an MMU, so it's not
			 * "real easy" to run Linux on it. It is left purely
			 * for documentation.  Commented out because it shares
			 * it's c0_prid id number with the TX3900.
			 */
	 		mips_cpu.cputype = CPU_R4650;
		 	mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU;
		        mips_cpu.tlbsize = 48;
			break;
		#endif
		case PRID_IMP_TX39:
			mips_cpu.isa_level = MIPS_CPU_ISA_I;
			mips_cpu.options = MIPS_CPU_TLB;

			if ((mips_cpu.processor_id & 0xf0) ==
			    (PRID_REV_TX3927 & 0xf0)) {
				mips_cpu.cputype = CPU_TX3927;
				mips_cpu.tlbsize = 64;
				mips_cpu.icache.ways = 2;
				mips_cpu.dcache.ways = 2;
			} else {
				switch (mips_cpu.processor_id & 0xff) {
				case PRID_REV_TX3912:
					mips_cpu.cputype = CPU_TX3912;
					mips_cpu.tlbsize = 32;
					break;
				case PRID_REV_TX3922:
					mips_cpu.cputype = CPU_TX3922;
					mips_cpu.tlbsize = 64;
					break;
				default:
					mips_cpu.cputype = CPU_UNKNOWN;
					break;
				}
			}
			break;
		case PRID_IMP_R4700:
			mips_cpu.cputype = CPU_R4700;
			mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR;
			mips_cpu.tlbsize = 48;
			break;
		case PRID_IMP_TX49:
			mips_cpu.cputype = CPU_TX49XX;
			mips_cpu.isa_level = MIPS_CPU_ISA_III;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR;
			mips_cpu.tlbsize = 48;
			mips_cpu.icache.ways = 4;
			mips_cpu.dcache.ways = 4;
			break;
		case PRID_IMP_R5000:
			mips_cpu.cputype = CPU_R5000;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV; 
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR;
			mips_cpu.tlbsize = 48;
			break;
		case PRID_IMP_R5432:
			mips_cpu.cputype = CPU_R5432;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV; 
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR | MIPS_CPU_WATCH;
			mips_cpu.tlbsize = 48;
			break;
		case PRID_IMP_R5500:
			mips_cpu.cputype = CPU_R5500;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV; 
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR | MIPS_CPU_WATCH;
			mips_cpu.tlbsize = 48;
			break;
		case PRID_IMP_NEVADA:
			mips_cpu.cputype = CPU_NEVADA;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV; 
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR | MIPS_CPU_DIVEC;
			mips_cpu.tlbsize = 48;
			mips_cpu.icache.ways = 2;
			mips_cpu.dcache.ways = 2;
			break;
		case PRID_IMP_R6000:
			mips_cpu.cputype = CPU_R6000;
			mips_cpu.isa_level = MIPS_CPU_ISA_II;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_FPU;
			mips_cpu.tlbsize = 32;
			break;
		case PRID_IMP_R6000A:
			mips_cpu.cputype = CPU_R6000A;
			mips_cpu.isa_level = MIPS_CPU_ISA_II;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_FPU;
			mips_cpu.tlbsize = 32;
			break;
		case PRID_IMP_RM7000:
			mips_cpu.cputype = CPU_RM7000;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV;
			mips_cpu.options = R4K_OPTS | MIPS_CPU_FPU |
			                   MIPS_CPU_32FPR;
			/*
			 * Undocumented RM7000:  Bit 29 in the info register of
			 * the RM7000 v2.0 indicates if the TLB has 48 or 64
			 * entries.
			 *
			 * 29      1 =>    64 entry JTLB
			 *         0 =>    48 entry JTLB
			 */
			mips_cpu.tlbsize = (get_info() & (1 << 29)) ? 64 : 48;
			break;
		case PRID_IMP_R8000:
			mips_cpu.cputype = CPU_R8000;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX |
				           MIPS_CPU_FPU | MIPS_CPU_32FPR;
			mips_cpu.tlbsize = 384;      /* has wierd TLB: 3-way x 128 */
			break;
		case PRID_IMP_R10000:
			mips_cpu.cputype = CPU_R10000;
			mips_cpu.isa_level = MIPS_CPU_ISA_IV;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX | 
				           MIPS_CPU_FPU | MIPS_CPU_32FPR | 
				           MIPS_CPU_COUNTER | MIPS_CPU_WATCH;
			mips_cpu.tlbsize = 64;
			break;
		default:
			mips_cpu.cputype = CPU_UNKNOWN;
			break;
		}
		break;
#ifdef CONFIG_CPU_MIPS32
	case PRID_COMP_MIPS:
		switch (mips_cpu.processor_id & 0xff00) {
		case PRID_IMP_4KC:
			mips_cpu.cputype = CPU_4KC;
			goto cpu_4kc;
		case PRID_IMP_4KEC:
			mips_cpu.cputype = CPU_4KEC;
			goto cpu_4kc;
		case PRID_IMP_4KSC:
			mips_cpu.cputype = CPU_4KSC;
cpu_4kc:
			/*
			 * Why do we set all these options by default, THEN
			 * query them??
			 */
			mips_cpu.isa_level = MIPS_CPU_ISA_M32;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX | 
				           MIPS_CPU_4KTLB | MIPS_CPU_COUNTER | 
				           MIPS_CPU_DIVEC | MIPS_CPU_WATCH |
			                   MIPS_CPU_MCHECK;
			config1 = read_mips32_cp0_config1();
			if (config1 & (1 << 3))
				mips_cpu.options |= MIPS_CPU_WATCH;
			if (config1 & (1 << 2))
				mips_cpu.options |= MIPS_CPU_MIPS16;
			if (config1 & 1)
				mips_cpu.options |= MIPS_CPU_FPU;
			mips_cpu.scache.flags = MIPS_CACHE_NOT_PRESENT;
			break;
		case PRID_IMP_5KC:
			mips_cpu.cputype = CPU_5KC;
			mips_cpu.isa_level = MIPS_CPU_ISA_M64;
			/* See comment above about querying options */
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX | 
				           MIPS_CPU_4KTLB | MIPS_CPU_COUNTER | 
				           MIPS_CPU_DIVEC | MIPS_CPU_WATCH |
			                   MIPS_CPU_MCHECK;
			config1 = read_mips32_cp0_config1();
			if (config1 & (1 << 3))
				mips_cpu.options |= MIPS_CPU_WATCH;
			if (config1 & (1 << 2))
				mips_cpu.options |= MIPS_CPU_MIPS16;
			if (config1 & 1)
				mips_cpu.options |= MIPS_CPU_FPU;
			mips_cpu.scache.flags = MIPS_CACHE_NOT_PRESENT;
			break;
		default:
			mips_cpu.cputype = CPU_UNKNOWN;
			break;
		}		
		break;
	case PRID_COMP_ALCHEMY:
		switch (mips_cpu.processor_id & 0xff00) {
		case PRID_IMP_AU1_REV1:
		case PRID_IMP_AU1_REV2:
			if (mips_cpu.processor_id & 0xff000000)
				mips_cpu.cputype = CPU_AU1500;
			else
				mips_cpu.cputype = CPU_AU1000;
			mips_cpu.isa_level = MIPS_CPU_ISA_M32;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX | 
					   MIPS_CPU_4KTLB | MIPS_CPU_COUNTER | 
					   MIPS_CPU_DIVEC | MIPS_CPU_WATCH;
			config1 = read_mips32_cp0_config1();
			if (config1 & (1 << 3))
				mips_cpu.options |= MIPS_CPU_WATCH;
			if (config1 & (1 << 2))
				mips_cpu.options |= MIPS_CPU_MIPS16;
			if (config1 & 1)
				mips_cpu.options |= MIPS_CPU_FPU;
			mips_cpu.scache.flags = MIPS_CACHE_NOT_PRESENT;
			break;
		default:
			mips_cpu.cputype = CPU_UNKNOWN;
			break;
		}
		break;
#endif /* CONFIG_CPU_MIPS32 */
	case PRID_COMP_SIBYTE:
		switch (mips_cpu.processor_id & 0xff00) {
		case PRID_IMP_SB1:
			mips_cpu.cputype = CPU_SB1;
			mips_cpu.isa_level = MIPS_CPU_ISA_M64;
			mips_cpu.options = MIPS_CPU_TLB | MIPS_CPU_4KEX |
			                   MIPS_CPU_COUNTER | MIPS_CPU_DIVEC |
			                   MIPS_CPU_MCHECK;
#ifndef CONFIG_SB1_PASS_1_WORKAROUNDS
			/* FPU in pass1 is known to have issues. */
			mips_cpu.options |= MIPS_CPU_FPU;
#endif
			break;
		default:
			mips_cpu.cputype = CPU_UNKNOWN;
			break;
		}
		break;
	default:
		mips_cpu.cputype = CPU_UNKNOWN;
	}
	if (mips_cpu.options & MIPS_CPU_FPU)
		mips_cpu.fpu_id = cpu_get_fpu_id();
}

static inline void cpu_report(void)
{
	printk("CPU revision is: %08x\n", mips_cpu.processor_id);
	if (mips_cpu.options & MIPS_CPU_FPU)
		printk("FPU revision is: %08x\n", mips_cpu.fpu_id);
}

asmlinkage void __init
init_arch(int argc, char **argv, char **envp, int *prom_vec)
{
	unsigned int s;

	/* Determine which MIPS variant we are running on. */
	cpu_probe();

	prom_init(argc, argv, envp, prom_vec);

#ifdef CONFIG_SGI_IP22
	sgi_sysinit();
#endif

	cpu_report();

	/*
	 * Determine the mmu/cache attached to this machine,
	 * then flush the tlb and caches.  On the r4xx0
	 * variants this also sets CP0_WIRED to zero.
	 */
	loadmmu();

	/* Disable coprocessors and set FPU for 16 FPRs */
	s = read_32bit_cp0_register(CP0_STATUS);
	s &= ~(ST0_CU1|ST0_CU2|ST0_CU3|ST0_KX|ST0_SX|ST0_FR);
	s |= ST0_CU0;
	write_32bit_cp0_register(CP0_STATUS, s);

	start_kernel();
}

void __init add_memory_region(phys_t start, phys_t size,
			      long type)
{
	int x = boot_mem_map.nr_map;

	if (x == BOOT_MEM_MAP_MAX) {
		printk("Ooops! Too many entries in the memory map!\n");
		return;
	}

	boot_mem_map.map[x].addr = start;
	boot_mem_map.map[x].size = size;
	boot_mem_map.map[x].type = type;
	boot_mem_map.nr_map++;
}

static void __init print_memory_map(void)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		printk(" memory: %08Lx @ %08Lx ",
			(unsigned long long) boot_mem_map.map[i].size,
			(unsigned long long) boot_mem_map.map[i].addr);
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
			printk("(usable)\n");
			break;
		case BOOT_MEM_ROM_DATA:
			printk("(ROM data)\n");
			break;
		case BOOT_MEM_RESERVED:
			printk("(reserved)\n");
			break;
		default:
			printk("type %lu\n", boot_mem_map.map[i].type);
			break;
		}
	}
}

static inline void parse_mem_cmdline(void)
{
	char c = ' ', *to = command_line, *from = saved_command_line;
	unsigned long start_at, mem_size;
	int len = 0;
	int usermem = 0;

	printk("Determined physical RAM map:\n");
	print_memory_map();

	for (;;) {
		/*
		 * "mem=XXX[kKmM]" defines a memory region from
		 * 0 to <XXX>, overriding the determined size.
		 * "mem=XXX[KkmM]@YYY[KkmM]" defines a memory region from
		 * <YYY> to <YYY>+<XXX>, overriding the determined size.
		 */
		if (c == ' ' && !memcmp(from, "mem=", 4)) {
			if (to != command_line)
				to--;
			/*
			 * If a user specifies memory size, we
			 * blow away any automatically generated
			 * size.
			 */
			if (usermem == 0) {
				boot_mem_map.nr_map = 0;
				usermem = 1;
			}
			mem_size = memparse(from + 4, &from);
			if (*from == '@')
				start_at = memparse(from + 1, &from);
			else
				start_at = 0;
			add_memory_region(start_at, mem_size, BOOT_MEM_RAM);
		}
		c = *(from++);
		if (!c)
			break;
		if (CL_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';

	if (usermem) {
		printk("User-defined physical RAM map:\n");
		print_memory_map();
	}
}

void __init setup_arch(char **cmdline_p)
{
	void atlas_setup(void);
	void baget_setup(void);
	void cobalt_setup(void);
	void ddb_setup(void);
	void decstation_setup(void);
	void deskstation_setup(void);
	void jazz_setup(void);
	void sni_rm200_pci_setup(void);
	void ip22_setup(void);
        void ev96100_setup(void);
	void malta_setup(void);
	void ikos_setup(void);
	void momenco_ocelot_setup(void);
	void nino_setup(void);
	void nec_osprey_setup(void);
	void jmr3927_setup(void);
 	void it8172_setup(void);
	void swarm_setup(void);
	void hp_setup(void);

	unsigned long bootmap_size;
	unsigned long start_pfn, max_pfn, max_low_pfn, first_usable_pfn;
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long tmp;
	unsigned long* initrd_header;
#endif

	int i;

#ifdef CONFIG_BLK_DEV_FD
	fd_ops = &no_fd_ops;
#endif

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &no_ide_ops;
#endif

#ifdef CONFIG_PC_KEYB
	kbd_ops = &no_kbd_ops;
#endif
	
	rtc_ops = &no_rtc_ops;

	switch(mips_machgroup)
	{
#ifdef CONFIG_BAGET_MIPS
	case MACH_GROUP_BAGET: 
		baget_setup();
		break;
#endif
#ifdef CONFIG_MIPS_COBALT
        case MACH_GROUP_COBALT:
                cobalt_setup();
                break;
#endif
#ifdef CONFIG_DECSTATION
	case MACH_GROUP_DEC:
		decstation_setup();
		break;
#endif
#ifdef CONFIG_MIPS_ATLAS
	case MACH_GROUP_UNKNOWN:
		atlas_setup();
		break;
#endif
#ifdef CONFIG_MIPS_JAZZ
	case MACH_GROUP_JAZZ:
		jazz_setup();
		break;
#endif
#ifdef CONFIG_MIPS_MALTA
	case MACH_GROUP_UNKNOWN:
		malta_setup();
		break;
#endif
#ifdef CONFIG_MOMENCO_OCELOT
	case MACH_GROUP_MOMENCO:
		momenco_ocelot_setup();
		break;
#endif
#ifdef CONFIG_SGI_IP22
	/* As of now this is only IP22.  */
	case MACH_GROUP_SGI:
		ip22_setup();
		break;
#endif
#ifdef CONFIG_SNI_RM200_PCI
	case MACH_GROUP_SNI_RM:
		sni_rm200_pci_setup();
		break;
#endif
#ifdef CONFIG_DDB5074
	case MACH_GROUP_NEC_DDB:
		ddb_setup();
		break;
#endif
#ifdef CONFIG_DDB5476
       case MACH_GROUP_NEC_DDB:
               ddb_setup();
               break;
#endif
#ifdef CONFIG_DDB5477
       case MACH_GROUP_NEC_DDB:
               ddb_setup();
               break;
#endif
#ifdef CONFIG_NEC_OSPREY
	case MACH_GROUP_NEC_VR41XX:
		nec_osprey_setup();
		break;
#endif
#ifdef CONFIG_MIPS_EV96100
	case MACH_GROUP_GALILEO:
		ev96100_setup();
		break;
#endif
#ifdef CONFIG_MIPS_EV64120
	case MACH_GROUP_GALILEO:
		ev64120_setup();
		break;
#endif
#if defined(CONFIG_MIPS_IVR) || defined(CONFIG_MIPS_ITE8172)
	case  MACH_GROUP_ITE:
	case  MACH_GROUP_GLOBESPAN:
		it8172_setup();
		break;
#endif  
#ifdef CONFIG_NINO
	case MACH_GROUP_PHILIPS:
		nino_setup();
		break;
#endif
#ifdef CONFIG_MIPS_PB1000
	case MACH_GROUP_ALCHEMY:
		au1000_setup();
		break;
#endif
#ifdef CONFIG_MIPS_PB1500
	case MACH_GROUP_ALCHEMY:
		au1500_setup();
		break;
#endif
#ifdef CONFIG_TOSHIBA_JMR3927
	case MACH_GROUP_TOSHIBA:
		jmr3927_setup();
		break;
#endif
#ifdef CONFIG_SIBYTE_SWARM
	case MACH_GROUP_SIBYTE:
		swarm_setup();
		break;
#endif
#ifdef CONFIG_HP_LASERJET
        case MACH_GROUP_HP_LJ:
                hp_setup();
                break;
#endif
	default:
		panic("Unsupported architecture");
	}

	strncpy(command_line, arcs_cmdline, sizeof command_line);
	command_line[sizeof command_line - 1] = 0;
	strcpy(saved_command_line, command_line);
	*cmdline_p = command_line;

	parse_mem_cmdline();

#define PFN_UP(x)	(((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)

#define MAXMEM		HIGHMEM_START
#define MAXMEM_PFN	PFN_DOWN(MAXMEM)

#ifdef CONFIG_BLK_DEV_INITRD
	tmp = (((unsigned long)&_end + PAGE_SIZE-1) & PAGE_MASK) - 8; 
	if (tmp < (unsigned long)&_end) 
		tmp += PAGE_SIZE;
	initrd_header = (unsigned long *)tmp;
	if (initrd_header[0] == 0x494E5244) {
		initrd_start = (unsigned long)&initrd_header[2];
		initrd_end = initrd_start + initrd_header[1];
	}
	start_pfn = PFN_UP(__pa((&_end)+(initrd_end - initrd_start) + PAGE_SIZE));
#else
	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards.
	 */
	start_pfn = PFN_UP(__pa(&_end));
#endif	/* CONFIG_BLK_DEV_INITRD */

	/* Find the highest page frame number we have available.  */
	max_pfn = 0;
	first_usable_pfn = -1UL;
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long start, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		start = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr
		      + boot_mem_map.map[i].size);

		if (start >= end)
			continue;
		if (end > max_pfn)
			max_pfn = end;
		if (start < first_usable_pfn) {
			if (start > start_pfn) {
				first_usable_pfn = start;
			} else if (end > start_pfn) {
				first_usable_pfn = start_pfn;
			}
		}
	}

	/*
	 * Determine low and high memory ranges
	 */
	max_low_pfn = max_pfn;
	if (max_low_pfn > MAXMEM_PFN) {
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* Maximum memory usable is what is directly addressable */
		printk(KERN_WARNING "Warning only %ldMB will be used.\n",
		       MAXMEM>>20);
		printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
#endif
	}

#ifdef CONFIG_HIGHMEM
	/*
	 * Crude, we really should make a better attempt at detecting
	 * highstart_pfn
	 */
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > MAXMEM_PFN) {
		highstart_pfn = MAXMEM_PFN;
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		       (highend_pfn - highstart_pfn) >> (20 - PAGE_SHIFT));
	}
#endif

	/* Initialize the boot-time allocator with low memory only.  */
	bootmap_size = init_bootmem(first_usable_pfn, max_low_pfn);

	/*
	 * Register fully available low RAM pages with the bootmem allocator.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long curr_pfn, last_pfn, size;

		/*
		 * Reserve usable memory.
		 */
		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			continue;

		/*
		 * We are rounding up the start address of usable memory:
		 */
		curr_pfn = PFN_UP(boot_mem_map.map[i].addr);
		if (curr_pfn >= max_low_pfn)
			continue;
		if (curr_pfn < start_pfn)
			curr_pfn = start_pfn;

		/*
		 * ... and at the end of the usable range downwards:
		 */
		last_pfn = PFN_DOWN(boot_mem_map.map[i].addr
				    + boot_mem_map.map[i].size);

		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * Only register lowmem part of lowmem segment with bootmem.
		 */
		size = last_pfn - curr_pfn;
		if (curr_pfn > PFN_DOWN(HIGHMEM_START))
			continue;
		if (curr_pfn + size - 1 > PFN_DOWN(HIGHMEM_START))
			size = PFN_DOWN(HIGHMEM_START) - curr_pfn;
		if (!size)
			continue;

		/*
		 * ... finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		/* Register lowmem ranges */
		free_bootmem(PFN_PHYS(curr_pfn), PFN_PHYS(size));
	}

	/* Reserve the bootmap memory.  */
	reserve_bootmem(PFN_PHYS(first_usable_pfn), bootmap_size);

#ifdef CONFIG_BLK_DEV_INITRD
	/* Board specific code should have set up initrd_start and initrd_end */
	ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	if (&__rd_start != &__rd_end) {
		initrd_start = (unsigned long)&__rd_start;
		initrd_end = (unsigned long)&__rd_end;
	}
	initrd_below_start_ok = 1;
	if (initrd_start) {
		unsigned long initrd_size = ((unsigned char *)initrd_end) - ((unsigned char *)initrd_start); 
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *)initrd_start, 
		       initrd_size);
		if ((void *)initrd_end > phys_to_virt(PFN_PHYS(max_low_pfn))) {
			printk("initrd extends beyond end of memory "
			       "(0x%lx > 0x%p)\ndisabling initrd\n",
			       initrd_end,
			       phys_to_virt(PFN_PHYS(max_low_pfn)));
			initrd_start = initrd_end = 0;
		} 
	}
#endif /* CONFIG_BLK_DEV_INITRD  */

	paging_init();

	code_resource.start = virt_to_bus(&_ftext);
	code_resource.end = virt_to_bus(&_etext) - 1;
	data_resource.start = virt_to_bus(&_fdata);
	data_resource.end = virt_to_bus(&_edata) - 1;

	/*
	 * Request address space for all standard RAM.
	 */
	for (i = 0; i < boot_mem_map.nr_map; i++) {
		struct resource *res;
		unsigned long addr_pfn, end_pfn;

		res = alloc_bootmem(sizeof(struct resource));
		switch (boot_mem_map.map[i].type) {
		case BOOT_MEM_RAM:
		case BOOT_MEM_ROM_DATA:
			res->name = "System RAM";
			break;
		case BOOT_MEM_RESERVED:
		default:
			res->name = "reserved";
		}
		addr_pfn = PFN_UP(boot_mem_map.map[i].addr);
		end_pfn = PFN_UP(boot_mem_map.map[i].addr+boot_mem_map.map[i].size);
		if (addr_pfn > max_low_pfn)
			continue;
		res->start = boot_mem_map.map[i].addr;
		if (end_pfn < max_low_pfn) {
			res->end = res->start + boot_mem_map.map[i].size - 1;
		} else {
			res->end = max_low_pfn - 1;
		}
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, res);

		/*
		 *  We dont't know which RAM region contains kernel data,
		 *  so we try it repeatedly and let the resource manager
		 *  test it.
		 */
		request_resource(res, &code_resource);
		request_resource(res, &data_resource);
	}
}

void r3081_wait(void) 
{
	unsigned long cfg = read_32bit_cp0_register(CP0_CONF);
	write_32bit_cp0_register(CP0_CONF, cfg|CONF_HALT);
}

void r39xx_wait(void)
{
	unsigned long cfg = read_32bit_cp0_register(CP0_CONF);
	write_32bit_cp0_register(CP0_CONF, cfg|TX39_CONF_HALT);
}

void r4k_wait(void)
{
	__asm__(".set\tmips3\n\t"
		"wait\n\t"
		".set\tmips0");
}

int __init fpu_disable(char *s)
{
	mips_cpu.options &= ~MIPS_CPU_FPU;
	return 1;
}
__setup("nofpu", fpu_disable);
