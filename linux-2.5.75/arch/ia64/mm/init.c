/*
 * Initialize MMU support.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/bootmem.h>
#include <linux/efi.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/personality.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/swap.h>

#include <asm/a.out.h>
#include <asm/bitops.h>
#include <asm/dma.h>
#include <asm/ia32.h>
#include <asm/io.h>
#include <asm/machvec.h>
#include <asm/patch.h>
#include <asm/pgalloc.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/tlb.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/* References to section boundaries: */
extern char _stext, _etext, _edata, __init_begin, __init_end, _end;

extern void ia64_tlb_init (void);

unsigned long MAX_DMA_ADDRESS = PAGE_OFFSET + 0x100000000UL;

#ifdef CONFIG_VIRTUAL_MEM_MAP
# define LARGE_GAP	0x40000000	/* Use virtual mem map if hole is > than this */
  unsigned long vmalloc_end = VMALLOC_END_INIT;
  static struct page *vmem_map;
  static unsigned long num_dma_physpages;
#endif

static int pgt_cache_water[2] = { 25, 50 };

struct page *zero_page_memmap_ptr;		/* map entry for zero page */

void
check_pgt_cache (void)
{
	int low, high;

	low = pgt_cache_water[0];
	high = pgt_cache_water[1];

	if (pgtable_cache_size > (u64) high) {
		do {
			if (pgd_quicklist)
				free_page((unsigned long)pgd_alloc_one_fast(0));
			if (pmd_quicklist)
				free_page((unsigned long)pmd_alloc_one_fast(0, 0));
		} while (pgtable_cache_size > (u64) low);
	}
}

void
update_mmu_cache (struct vm_area_struct *vma, unsigned long vaddr, pte_t pte)
{
	unsigned long addr;
	struct page *page;

	if (!pte_exec(pte))
		return;				/* not an executable page... */

	page = pte_page(pte);
	/* don't use VADDR: it may not be mapped on this CPU (or may have just been flushed): */
	addr = (unsigned long) page_address(page);

	if (test_bit(PG_arch_1, &page->flags))
		return;				/* i-cache is already coherent with d-cache */

	flush_icache_range(addr, addr + PAGE_SIZE);
	set_bit(PG_arch_1, &page->flags);	/* mark page as clean */
}

inline void
ia64_set_rbs_bot (void)
{
	unsigned long stack_size = current->rlim[RLIMIT_STACK].rlim_max & -16;

	if (stack_size > MAX_USER_STACK_SIZE)
		stack_size = MAX_USER_STACK_SIZE;
	current->thread.rbs_bot = STACK_TOP - stack_size;
}

/*
 * This performs some platform-dependent address space initialization.
 * On IA-64, we want to setup the VM area for the register backing
 * store (which grows upwards) and install the gateway page which is
 * used for signal trampolines, etc.
 */
void
ia64_init_addr_space (void)
{
	struct vm_area_struct *vma;

	ia64_set_rbs_bot();

	/*
	 * If we're out of memory and kmem_cache_alloc() returns NULL, we simply ignore
	 * the problem.  When the process attempts to write to the register backing store
	 * for the first time, it will get a SEGFAULT in this case.
	 */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (vma) {
		vma->vm_mm = current->mm;
		vma->vm_start = current->thread.rbs_bot & PAGE_MASK;
		vma->vm_end = vma->vm_start + PAGE_SIZE;
		vma->vm_page_prot = protection_map[VM_DATA_DEFAULT_FLAGS & 0x7];
		vma->vm_flags = VM_READ|VM_WRITE|VM_MAYREAD|VM_MAYWRITE|VM_GROWSUP;
		vma->vm_ops = NULL;
		vma->vm_pgoff = 0;
		vma->vm_file = NULL;
		vma->vm_private_data = NULL;
		insert_vm_struct(current->mm, vma);
	}

	/* map NaT-page at address zero to speed up speculative dereferencing of NULL: */
	if (!(current->personality & MMAP_PAGE_ZERO)) {
		vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
		if (vma) {
			memset(vma, 0, sizeof(*vma));
			vma->vm_mm = current->mm;
			vma->vm_end = PAGE_SIZE;
			vma->vm_page_prot = __pgprot(pgprot_val(PAGE_READONLY) | _PAGE_MA_NAT);
			vma->vm_flags = VM_READ | VM_MAYREAD | VM_IO | VM_RESERVED;
			insert_vm_struct(current->mm, vma);
		}
	}
}

void
free_initmem (void)
{
	unsigned long addr, eaddr;

	addr = (unsigned long) ia64_imva(&__init_begin);
	eaddr = (unsigned long) ia64_imva(&__init_end);
	while (addr < eaddr) {
		ClearPageReserved(virt_to_page(addr));
		set_page_count(virt_to_page(addr), 1);
		free_page(addr);
		++totalram_pages;
		addr += PAGE_SIZE;
	}
	printk(KERN_INFO "Freeing unused kernel memory: %ldkB freed\n",
	       (&__init_end - &__init_begin) >> 10);
}

void
free_initrd_mem (unsigned long start, unsigned long end)
{
	struct page *page;
	/*
	 * EFI uses 4KB pages while the kernel can use 4KB  or bigger.
	 * Thus EFI and the kernel may have different page sizes. It is
	 * therefore possible to have the initrd share the same page as
	 * the end of the kernel (given current setup).
	 *
	 * To avoid freeing/using the wrong page (kernel sized) we:
	 *	- align up the beginning of initrd
	 *	- align down the end of initrd
	 *
	 *  |             |
	 *  |=============| a000
	 *  |             |
	 *  |             |
	 *  |             | 9000
	 *  |/////////////|
	 *  |/////////////|
	 *  |=============| 8000
	 *  |///INITRD////|
	 *  |/////////////|
	 *  |/////////////| 7000
	 *  |             |
	 *  |KKKKKKKKKKKKK|
	 *  |=============| 6000
	 *  |KKKKKKKKKKKKK|
	 *  |KKKKKKKKKKKKK|
	 *  K=kernel using 8KB pages
	 *
	 * In this example, we must free page 8000 ONLY. So we must align up
	 * initrd_start and keep initrd_end as is.
	 */
	start = PAGE_ALIGN(start);
	end = end & PAGE_MASK;

	if (start < end)
		printk(KERN_INFO "Freeing initrd memory: %ldkB freed\n", (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		if (!virt_addr_valid(start))
			continue;
		page = virt_to_page(start);
		ClearPageReserved(page);
		set_page_count(page, 1);
		free_page(start);
		++totalram_pages;
	}
}

void
show_mem(void)
{
	int i, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();

#ifdef CONFIG_DISCONTIGMEM
	{
		pg_data_t *pgdat;

		printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
		for_each_pgdat(pgdat) {
			printk("Node ID: %d\n", pgdat->node_id);
			for(i = 0; i < pgdat->node_spanned_pages; i++) {
				if (PageReserved(pgdat->node_mem_map+i))
					reserved++;
				else if (PageSwapCache(pgdat->node_mem_map+i))
					cached++;
				else if (page_count(pgdat->node_mem_map + i))
					shared += page_count(pgdat->node_mem_map + i) - 1;
			}
			printk("\t%d pages of RAM\n", pgdat->node_spanned_pages);
			printk("\t%d reserved pages\n", reserved);
			printk("\t%d pages shared\n", shared);
			printk("\t%d pages swap cached\n", cached);
		}
		printk("Total of %ld pages in page table cache\n", pgtable_cache_size);
		printk("%d free buffer pages\n", nr_free_buffer_pages());
	}
#else /* !CONFIG_DISCONTIGMEM */
	printk("Free swap:       %6dkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (page_count(mem_map + i))
			shared += page_count(mem_map + i) - 1;
	}
	printk("%d pages of RAM\n", total);
	printk("%d reserved pages\n", reserved);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
	printk("%ld pages in page table cache\n", pgtable_cache_size);
#endif /* !CONFIG_DISCONTIGMEM */
}

/*
 * This is like put_dirty_page() but installs a clean page in the kernel's page table.
 */
struct page *
put_kernel_page (struct page *page, unsigned long address, pgprot_t pgprot)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	if (!PageReserved(page))
		printk(KERN_ERR "put_kernel_page: page at 0x%p not in reserved memory\n",
		       page_address(page));

	pgd = pgd_offset_k(address);		/* note: this is NOT pgd_offset()! */

	spin_lock(&init_mm.page_table_lock);
	{
		pmd = pmd_alloc(&init_mm, pgd, address);
		if (!pmd)
			goto out;
		pte = pte_alloc_map(&init_mm, pmd, address);
		if (!pte)
			goto out;
		if (!pte_none(*pte)) {
			pte_unmap(pte);
			goto out;
		}
		set_pte(pte, mk_pte(page, pgprot));
		pte_unmap(pte);
	}
  out:	spin_unlock(&init_mm.page_table_lock);
	/* no need for flush_tlb */
	return page;
}

static void
setup_gate (void)
{
	struct page *page;
	extern char __start_gate_section[];

	/*
	 * Map the gate page twice: once read-only to export the ELF headers etc. and once
	 * execute-only page to enable privilege-promotion via "epc":
	 */
	page = virt_to_page(ia64_imva(__start_gate_section));
	put_kernel_page(page, GATE_ADDR, PAGE_READONLY);
#ifdef HAVE_BUGGY_SEGREL
	page = virt_to_page(ia64_imva(__start_gate_section + PAGE_SIZE));
	put_kernel_page(page, GATE_ADDR + PAGE_SIZE, PAGE_GATE);
#else
	put_kernel_page(page, GATE_ADDR + PERCPU_PAGE_SIZE, PAGE_GATE);
#endif
	ia64_patch_gate();
}

void __init
ia64_mmu_init (void *my_cpu_data)
{
	unsigned long psr, pta, impl_va_bits;
	extern void __init tlb_init (void);
#ifdef CONFIG_DISABLE_VHPT
#	define VHPT_ENABLE_BIT	0
#else
#	define VHPT_ENABLE_BIT	1
#endif

	/* Pin mapping for percpu area into TLB */
	psr = ia64_clear_ic();
	ia64_itr(0x2, IA64_TR_PERCPU_DATA, PERCPU_ADDR,
		 pte_val(pfn_pte(__pa(my_cpu_data) >> PAGE_SHIFT, PAGE_KERNEL)),
		 PERCPU_PAGE_SHIFT);

	ia64_set_psr(psr);
	ia64_srlz_i();

	/*
	 * Check if the virtually mapped linear page table (VMLPT) overlaps with a mapped
	 * address space.  The IA-64 architecture guarantees that at least 50 bits of
	 * virtual address space are implemented but if we pick a large enough page size
	 * (e.g., 64KB), the mapped address space is big enough that it will overlap with
	 * VMLPT.  I assume that once we run on machines big enough to warrant 64KB pages,
	 * IMPL_VA_MSB will be significantly bigger, so this is unlikely to become a
	 * problem in practice.  Alternatively, we could truncate the top of the mapped
	 * address space to not permit mappings that would overlap with the VMLPT.
	 * --davidm 00/12/06
	 */
#	define pte_bits			3
#	define mapped_space_bits	(3*(PAGE_SHIFT - pte_bits) + PAGE_SHIFT)
	/*
	 * The virtual page table has to cover the entire implemented address space within
	 * a region even though not all of this space may be mappable.  The reason for
	 * this is that the Access bit and Dirty bit fault handlers perform
	 * non-speculative accesses to the virtual page table, so the address range of the
	 * virtual page table itself needs to be covered by virtual page table.
	 */
#	define vmlpt_bits		(impl_va_bits - PAGE_SHIFT + pte_bits)
#	define POW2(n)			(1ULL << (n))

	impl_va_bits = ffz(~(local_cpu_data->unimpl_va_mask | (7UL << 61)));

	if (impl_va_bits < 51 || impl_va_bits > 61)
		panic("CPU has bogus IMPL_VA_MSB value of %lu!\n", impl_va_bits - 1);

	/* place the VMLPT at the end of each page-table mapped region: */
	pta = POW2(61) - POW2(vmlpt_bits);

	if (POW2(mapped_space_bits) >= pta)
		panic("mm/init: overlap between virtually mapped linear page table and "
		      "mapped kernel space!");
	/*
	 * Set the (virtually mapped linear) page table address.  Bit
	 * 8 selects between the short and long format, bits 2-7 the
	 * size of the table, and bit 0 whether the VHPT walker is
	 * enabled.
	 */
	ia64_set_pta(pta | (0 << 8) | (vmlpt_bits << 2) | VHPT_ENABLE_BIT);

	ia64_tlb_init();
}

#ifdef CONFIG_VIRTUAL_MEM_MAP

static int
create_mem_map_page_table (u64 start, u64 end, void *arg)
{
	unsigned long address, start_page, end_page;
	struct page *map_start, *map_end;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	map_start = vmem_map + (__pa(start) >> PAGE_SHIFT);
	map_end   = vmem_map + (__pa(end) >> PAGE_SHIFT);

	start_page = (unsigned long) map_start & PAGE_MASK;
	end_page = PAGE_ALIGN((unsigned long) map_end);

	for (address = start_page; address < end_page; address += PAGE_SIZE) {
		pgd = pgd_offset_k(address);
		if (pgd_none(*pgd))
			pgd_populate(&init_mm, pgd, alloc_bootmem_pages(PAGE_SIZE));
		pmd = pmd_offset(pgd, address);

		if (pmd_none(*pmd))
			pmd_populate_kernel(&init_mm, pmd, alloc_bootmem_pages(PAGE_SIZE));
		pte = pte_offset_kernel(pmd, address);

		if (pte_none(*pte))
			set_pte(pte, pfn_pte(__pa(alloc_bootmem_pages(PAGE_SIZE)) >> PAGE_SHIFT,
					     PAGE_KERNEL));
	}
	return 0;
}

struct memmap_init_callback_data {
	struct page *start;
	struct page *end;
	int nid;
	unsigned long zone;
};

static int
virtual_memmap_init (u64 start, u64 end, void *arg)
{
	struct memmap_init_callback_data *args;
	struct page *map_start, *map_end;

	args = (struct memmap_init_callback_data *) arg;

	map_start = vmem_map + (__pa(start) >> PAGE_SHIFT);
	map_end   = vmem_map + (__pa(end) >> PAGE_SHIFT);

	if (map_start < args->start)
		map_start = args->start;
	if (map_end > args->end)
		map_end = args->end;

	/*
	 * We have to initialize "out of bounds" struct page elements that fit completely
	 * on the same pages that were allocated for the "in bounds" elements because they
	 * may be referenced later (and found to be "reserved").
	 */
	map_start -= ((unsigned long) map_start & (PAGE_SIZE - 1)) / sizeof(struct page);
	map_end += ((PAGE_ALIGN((unsigned long) map_end) - (unsigned long) map_end)
		    / sizeof(struct page));

	if (map_start < map_end)
		memmap_init_zone(map_start, (unsigned long) (map_end - map_start),
				 args->nid, args->zone, page_to_pfn(map_start));
	return 0;
}

void
memmap_init (struct page *start, unsigned long size, int nid,
	     unsigned long zone, unsigned long start_pfn)
{
	if (!vmem_map)
		memmap_init_zone(start, size, nid, zone, start_pfn);
	else {
		struct memmap_init_callback_data args;

		args.start = start;
		args.end = start + size;
		args.nid = nid;
		args.zone = zone;

		efi_memmap_walk(virtual_memmap_init, &args);
	}
}

int
ia64_pfn_valid (unsigned long pfn)
{
	char byte;

	return __get_user(byte, (char *) pfn_to_page(pfn)) == 0;
}

static int
count_dma_pages (u64 start, u64 end, void *arg)
{
	unsigned long *count = arg;

	if (end <= MAX_DMA_ADDRESS)
		*count += (end - start) >> PAGE_SHIFT;
	return 0;
}

static int
find_largest_hole (u64 start, u64 end, void *arg)
{
	u64 *max_gap = arg;

	static u64 last_end = PAGE_OFFSET;

	/* NOTE: this algorithm assumes efi memmap table is ordered */

	if (*max_gap < (start - last_end))
		*max_gap = start - last_end;
	last_end = end;
	return 0;
}
#endif /* CONFIG_VIRTUAL_MEM_MAP */

static int
count_pages (u64 start, u64 end, void *arg)
{
	unsigned long *count = arg;

	*count += (end - start) >> PAGE_SHIFT;
	return 0;
}

/*
 * Set up the page tables.
 */

#ifdef CONFIG_DISCONTIGMEM
void
paging_init (void)
{
	extern void discontig_paging_init(void);

	discontig_paging_init();
	efi_memmap_walk(count_pages, &num_physpages);
	zero_page_memmap_ptr = virt_to_page(ia64_imva(empty_zero_page));
}
#else /* !CONFIG_DISCONTIGMEM */
void
paging_init (void)
{
	unsigned long max_dma;
	unsigned long zones_size[MAX_NR_ZONES];
#  ifdef CONFIG_VIRTUAL_MEM_MAP
	unsigned long zholes_size[MAX_NR_ZONES];
	unsigned long max_gap;
#  endif

	/* initialize mem_map[] */

	memset(zones_size, 0, sizeof(zones_size));

	num_physpages = 0;
	efi_memmap_walk(count_pages, &num_physpages);

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;

#  ifdef CONFIG_VIRTUAL_MEM_MAP
	memset(zholes_size, 0, sizeof(zholes_size));

	num_dma_physpages = 0;
	efi_memmap_walk(count_dma_pages, &num_dma_physpages);

	if (max_low_pfn < max_dma) {
		zones_size[ZONE_DMA] = max_low_pfn;
		zholes_size[ZONE_DMA] = max_low_pfn - num_dma_physpages;
	} else {
		zones_size[ZONE_DMA] = max_dma;
		zholes_size[ZONE_DMA] = max_dma - num_dma_physpages;
		if (num_physpages > num_dma_physpages) {
			zones_size[ZONE_NORMAL] = max_low_pfn - max_dma;
			zholes_size[ZONE_NORMAL] = ((max_low_pfn - max_dma)
						    - (num_physpages - num_dma_physpages));
		}
	}

	max_gap = 0;
	efi_memmap_walk(find_largest_hole, (u64 *)&max_gap);
	if (max_gap < LARGE_GAP) {
		vmem_map = (struct page *) 0;
		free_area_init_node(0, &contig_page_data, NULL, zones_size, 0, zholes_size);
		mem_map = contig_page_data.node_mem_map;
	}
	else {
		unsigned long map_size;

		/* allocate virtual_mem_map */

		map_size = PAGE_ALIGN(max_low_pfn * sizeof(struct page));
		vmalloc_end -= map_size;
		vmem_map = (struct page *) vmalloc_end;
		efi_memmap_walk(create_mem_map_page_table, 0);

		free_area_init_node(0, &contig_page_data, vmem_map, zones_size, 0, zholes_size);

		mem_map = contig_page_data.node_mem_map;
		printk("Virtual mem_map starts at 0x%p\n", mem_map);
	}
#  else /* !CONFIG_VIRTUAL_MEM_MAP */
	if (max_low_pfn < max_dma)
		zones_size[ZONE_DMA] = max_low_pfn;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = max_low_pfn - max_dma;
	}
	free_area_init(zones_size);
#  endif /* !CONFIG_VIRTUAL_MEM_MAP */
	zero_page_memmap_ptr = virt_to_page(ia64_imva(empty_zero_page));
}
#endif /* !CONFIG_DISCONTIGMEM */

static int
count_reserved_pages (u64 start, u64 end, void *arg)
{
	unsigned long num_reserved = 0;
	unsigned long *count = arg;

	for (; start < end; start += PAGE_SIZE)
		if (PageReserved(virt_to_page(start)))
			++num_reserved;
	*count += num_reserved;
	return 0;
}

/*
 * Boot command-line option "nolwsys" can be used to disable the use of any light-weight
 * system call handler.  When this option is in effect, all fsyscalls will end up bubbling
 * down into the kernel and calling the normal (heavy-weight) syscall handler.  This is
 * useful for performance testing, but conceivably could also come in handy for debugging
 * purposes.
 */

static int nolwsys;

static int __init
nolwsys_setup (char *s)
{
	nolwsys = 1;
	return 1;
}

__setup("nolwsys", nolwsys_setup);

void
mem_init (void)
{
	long reserved_pages, codesize, datasize, initsize;
	unsigned long num_pgt_pages;
	pg_data_t *pgdat;
	int i;
	static struct kcore_list kcore_mem, kcore_vmem, kcore_kernel;

#ifdef CONFIG_PCI
	/*
	 * This needs to be called _after_ the command line has been parsed but _before_
	 * any drivers that may need the PCI DMA interface are initialized or bootmem has
	 * been freed.
	 */
	platform_dma_init();
#endif

#ifndef CONFIG_DISCONTIGMEM
	if (!mem_map)
		BUG();
	max_mapnr = max_low_pfn;
#endif

	high_memory = __va(max_low_pfn * PAGE_SIZE);

	kclist_add(&kcore_mem, __va(0), max_low_pfn * PAGE_SIZE);
	kclist_add(&kcore_vmem, (void *)VMALLOC_START, VMALLOC_END-VMALLOC_START);
	kclist_add(&kcore_kernel, &_stext, &_end - &_stext);

	for_each_pgdat(pgdat)
		totalram_pages += free_all_bootmem_node(pgdat);

	reserved_pages = 0;
	efi_memmap_walk(count_reserved_pages, &reserved_pages);

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk(KERN_INFO "Memory: %luk/%luk available (%luk code, %luk reserved, "
	       "%luk data, %luk init)\n", (unsigned long) nr_free_pages() << (PAGE_SHIFT - 10),
	       num_physpages << (PAGE_SHIFT - 10), codesize >> 10,
	       reserved_pages << (PAGE_SHIFT - 10), datasize >> 10, initsize >> 10);

	/*
	 * Allow for enough (cached) page table pages so that we can map the entire memory
	 * at least once.  Each task also needs a couple of page tables pages, so add in a
	 * fudge factor for that (don't use "threads-max" here; that would be wrong!).
	 * Don't allow the cache to be more than 10% of total memory, though.
	 */
#	define NUM_TASKS	500	/* typical number of tasks */
	num_pgt_pages = nr_free_pages() / PTRS_PER_PGD + NUM_TASKS;
	if (num_pgt_pages > nr_free_pages() / 10)
		num_pgt_pages = nr_free_pages() / 10;
	if (num_pgt_pages > (u64) pgt_cache_water[1])
		pgt_cache_water[1] = num_pgt_pages;

	/*
	 * For fsyscall entrpoints with no light-weight handler, use the ordinary
	 * (heavy-weight) handler, but mark it by setting bit 0, so the fsyscall entry
	 * code can tell them apart.
	 */
	for (i = 0; i < NR_syscalls; ++i) {
		extern unsigned long fsyscall_table[NR_syscalls];
		extern unsigned long sys_call_table[NR_syscalls];

		if (!fsyscall_table[i] || nolwsys)
			fsyscall_table[i] = sys_call_table[i] | 1;
	}
	setup_gate();	/* setup gate pages before we free up boot memory... */

#ifdef CONFIG_IA32_SUPPORT
	ia32_gdt_init();
#endif
}
