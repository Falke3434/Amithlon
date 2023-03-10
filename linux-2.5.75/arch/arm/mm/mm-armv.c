/*
 *  linux/arch/arm/mm/mm-armv.c
 *
 *  Copyright (C) 1998-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table sludge for ARM v3 and v4 processor architectures.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/rmap.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>

#include <asm/mach/map.h>

static unsigned int cachepolicy __initdata = PMD_SECT_WB;
static unsigned int ecc_mask __initdata = 0;

struct cachepolicy {
	char		*policy;
	unsigned int	cr_mask;
	unsigned int	pmd;
};

static struct cachepolicy cache_policies[] __initdata = {
	{ "uncached",		CR1_W|CR1_C,	PMD_SECT_UNCACHED },
	{ "buffered",		CR1_C,		PMD_SECT_BUFFERED },
	{ "writethrough",	0,		PMD_SECT_WT       },
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	{ "writeback",		0,		PMD_SECT_WB       },
	{ "writealloc",		0,		PMD_SECT_WBWA     }
#endif
};

/*
 * These are useful for identifing cache coherency
 * problems by allowing the cache or the cache and
 * writebuffer to be turned off.  (Note: the write
 * buffer should not be on and the cache off).
 */
static void __init early_cachepolicy(char **p)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_policies); i++) {
		int len = strlen(cache_policies[i].policy);

		if (memcmp(*p, cache_policies[i].policy, len) == 0) {
			cachepolicy = cache_policies[i].pmd;
			cr_alignment &= ~cache_policies[i].cr_mask;
			cr_no_alignment &= ~cache_policies[i].cr_mask;
			*p += len;
			break;
		}
	}
	if (i == ARRAY_SIZE(cache_policies))
		printk(KERN_ERR "ERROR: unknown or unsupported cache policy\n");
	flush_cache_all();
	set_cr(cr_alignment);
}

static void __init early_nocache(char **__unused)
{
	char *p = "buffered";
	printk(KERN_WARNING "nocache is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}

static void __init early_nowrite(char **__unused)
{
	char *p = "uncached";
	printk(KERN_WARNING "nowb is deprecated; use cachepolicy=%s\n", p);
	early_cachepolicy(&p);
}

static void __init early_ecc(char **p)
{
	if (memcmp(*p, "on", 2) == 0) {
		ecc_mask = PMD_PROTECTION;
		*p += 2;
	} else if (memcmp(*p, "off", 3) == 0) {
		ecc_mask = 0;
		*p += 3;
	}
}

__early_param("nocache", early_nocache);
__early_param("nowb", early_nowrite);
__early_param("cachepolicy=", early_cachepolicy);
__early_param("ecc=", early_ecc);

static int __init noalign_setup(char *__unused)
{
	cr_alignment &= ~CR1_A;
	cr_no_alignment &= ~CR1_A;
	set_cr(cr_alignment);
	return 1;
}

__setup("noalign", noalign_setup);

#define FIRST_KERNEL_PGD_NR	(FIRST_USER_PGD_NR + USER_PTRS_PER_PGD)

/*
 * need to get a 16k page for level 1
 */
pgd_t *get_pgd_slow(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = (pgd_t *)__get_free_pages(GFP_KERNEL, 2);
	if (!new_pgd)
		goto no_pgd;

	memzero(new_pgd, FIRST_KERNEL_PGD_NR * sizeof(pgd_t));

	init_pgd = pgd_offset_k(0);

	if (vectors_base() == 0) {
		/*
		 * This lock is here just to satisfy pmd_alloc and pte_lock
		 */
		spin_lock(&mm->page_table_lock);

		/*
		 * On ARM, first page must always be allocated since it
		 * contains the machine vectors.
		 */
		new_pmd = pmd_alloc(mm, new_pgd, 0);
		if (!new_pmd)
			goto no_pmd;

		new_pte = pte_alloc_map(mm, new_pmd, 0);
		if (!new_pte)
			goto no_pte;

		init_pmd = pmd_offset(init_pgd, 0);
		init_pte = pte_offset_map_nested(init_pmd, 0);
		set_pte(new_pte, *init_pte);
		pte_unmap_nested(init_pte);
		pte_unmap(new_pte);

		spin_unlock(&mm->page_table_lock);
	}

	/*
	 * Copy over the kernel and IO PGD entries
	 */
	memcpy(new_pgd + FIRST_KERNEL_PGD_NR, init_pgd + FIRST_KERNEL_PGD_NR,
		       (PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));

	clean_dcache_area(new_pgd, PTRS_PER_PGD * sizeof(pgd_t));

	return new_pgd;

no_pte:
	spin_unlock(&mm->page_table_lock);
	pmd_free(new_pmd);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pmd:
	spin_unlock(&mm->page_table_lock);
	free_pages((unsigned long)new_pgd, 2);
	return NULL;

no_pgd:
	return NULL;
}

void free_pgd_slow(pgd_t *pgd)
{
	pmd_t *pmd;
	struct page *pte;

	if (!pgd)
		return;

	/* pgd is always present and good */
	pmd = (pmd_t *)pgd;
	if (pmd_none(*pmd))
		goto free;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto free;
	}

	pte = pmd_page(*pmd);
	pmd_clear(pmd);
	pgtable_remove_rmap(pte);
	pte_free(pte);
	pmd_free(pmd);
free:
	free_pages((unsigned long) pgd, 2);
}

/*
 * Create a SECTION PGD between VIRT and PHYS in domain
 * DOMAIN with protection PROT
 */
static inline void
alloc_init_section(unsigned long virt, unsigned long phys, int prot)
{
	pmd_t *pmdp;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);
	if (virt & (1 << 20))
		pmdp++;

	set_pmd(pmdp, __pmd(phys | prot));
}

/*
 * Add a PAGE mapping between VIRT and PHYS in domain
 * DOMAIN with protection PROT.  Note that due to the
 * way we map the PTEs, we must allocate two PTE_SIZE'd
 * blocks - one for the Linux pte table, and one for
 * the hardware pte table.
 */
static inline void
alloc_init_page(unsigned long virt, unsigned long phys, unsigned int prot_l1, pgprot_t prot)
{
	pmd_t *pmdp;
	pte_t *ptep;

	pmdp = pmd_offset(pgd_offset_k(virt), virt);

	if (pmd_none(*pmdp)) {
		unsigned long pmdval;
		ptep = alloc_bootmem_low_pages(2 * PTRS_PER_PTE *
					       sizeof(pte_t));

		pmdval = __pa(ptep) | prot_l1;
		pmdp[0] = __pmd(pmdval);
		pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t));
		flush_pmd_entry(pmdp);
	}
	ptep = pte_offset_kernel(pmdp, virt);

	set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
}

/*
 * Clear any PGD mapping.  On a two-level page table system,
 * the clearance is done by the middle-level functions (pmd)
 * rather than the top-level (pgd) functions.
 */
static inline void clear_mapping(unsigned long virt)
{
	pmd_clear(pmd_offset(pgd_offset_k(virt), virt));
}

struct mem_types {
	unsigned int	prot_pte;
	unsigned int	prot_l1;
	unsigned int	prot_sect;
	unsigned int	domain;
};

static struct mem_types mem_types[] __initdata = {
	[MT_DEVICE] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_WRITE,
		.prot_l1   = PMD_TYPE_TABLE | PMD_BIT4,
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_UNCACHED |
				PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_IO,
	},
	[MT_CACHECLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_MINICLEAN] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_MINICACHE,
		.domain    = DOMAIN_KERNEL,
	},
	[MT_VECTORS] = {
		.prot_pte  = L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY |
				L_PTE_EXEC,
		.prot_l1   = PMD_TYPE_TABLE | PMD_BIT4,
		.domain    = DOMAIN_USER,
	},
	[MT_MEMORY] = {
		.prot_sect = PMD_TYPE_SECT | PMD_BIT4 | PMD_SECT_AP_WRITE,
		.domain    = DOMAIN_KERNEL,
	}
};

/*
 * Adjust the PMD section entries according to the CPU in use.
 */
static void __init build_mem_type_table(void)
{
	int cpu_arch = cpu_architecture();
	const char *policy;

	/*
	 * ARMv5 and higher can use ECC memory.
	 */
	if (cpu_arch >= CPU_ARCH_ARMv5) {
		mem_types[MT_VECTORS].prot_l1 |= ecc_mask;
		mem_types[MT_MEMORY].prot_sect |= ecc_mask;
	} else {
		mem_types[MT_MINICLEAN].prot_sect &= ~PMD_SECT_TEX(1);
		if (cachepolicy == PMD_SECT_WBWA)
			cachepolicy = PMD_SECT_WB;
		ecc_mask = 0;
	}

	mem_types[MT_MEMORY].prot_sect |= cachepolicy;

	switch (cachepolicy) {
	default:
	case PMD_SECT_UNCACHED:
		policy = "uncached";
		break;
	case PMD_SECT_BUFFERED:
		mem_types[MT_VECTORS].prot_pte |= PTE_BUFFERABLE;
		policy = "buffered";
		break;
	case PMD_SECT_WT:
		mem_types[MT_VECTORS].prot_pte |= PTE_BUFFERABLE|PTE_CACHEABLE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WT;
		policy = "write through";
		break;
	case PMD_SECT_WB:
		mem_types[MT_VECTORS].prot_pte |= PTE_BUFFERABLE|PTE_CACHEABLE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WB;
		policy = "write back";
		break;
	case PMD_SECT_WBWA:
		mem_types[MT_VECTORS].prot_pte |= PTE_BUFFERABLE|PTE_CACHEABLE;
		mem_types[MT_CACHECLEAN].prot_sect |= PMD_SECT_WB;
		policy = "write back, write allocate";
		break;
	}
	printk("Memory policy: ECC %sabled, Data cache %s\n",
		ecc_mask ? "en" : "dis", policy);
}

/*
 * Create the page directory entries and any necessary
 * page tables for the mapping specified by `md'.  We
 * are able to cope here with varying sizes and address
 * offsets, and we take full advantage of sections.
 */
static void __init create_mapping(struct map_desc *md)
{
	unsigned long virt, length;
	int prot_sect, prot_l1, domain;
	pgprot_t prot_pte;
	long off;

	if (md->virtual != vectors_base() && md->virtual < PAGE_OFFSET) {
		printk(KERN_WARNING "MM: not creating mapping for "
		       "0x%08lx at 0x%08lx in user region\n",
		       md->physical, md->virtual);
		return;
	}

	domain	  = mem_types[md->type].domain;
	prot_pte  = __pgprot(mem_types[md->type].prot_pte);
	prot_l1   = mem_types[md->type].prot_l1 | PMD_DOMAIN(domain);
	prot_sect = mem_types[md->type].prot_sect | PMD_DOMAIN(domain);

	virt   = md->virtual;
	off    = md->physical - virt;
	length = md->length;

	if (mem_types[md->type].prot_l1 == 0 &&
	    (virt & 0xfffff || (virt + off) & 0xfffff || (virt + length) & 0xfffff)) {
		printk(KERN_WARNING "MM: map for 0x%08lx at 0x%08lx can not "
		       "be mapped using pages, ignoring.\n",
		       md->physical, md->virtual);
		return;
	}

	while ((virt & 0xfffff || (virt + off) & 0xfffff) && length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}

	while (length >= (PGDIR_SIZE / 2)) {
		alloc_init_section(virt, virt + off, prot_sect);

		virt   += (PGDIR_SIZE / 2);
		length -= (PGDIR_SIZE / 2);
	}

	while (length >= PAGE_SIZE) {
		alloc_init_page(virt, virt + off, prot_l1, prot_pte);

		virt   += PAGE_SIZE;
		length -= PAGE_SIZE;
	}
}

/*
 * In order to soft-boot, we need to insert a 1:1 mapping in place of
 * the user-mode pages.  This will then ensure that we have predictable
 * results when turning the mmu off
 */
void setup_mm_for_reboot(char mode)
{
	unsigned long pmdval;
	pgd_t *pgd;
	pmd_t *pmd;
	int i;

	if (current->mm && current->mm->pgd)
		pgd = current->mm->pgd;
	else
		pgd = init_mm.pgd;

	for (i = 0; i < FIRST_USER_PGD_NR + USER_PTRS_PER_PGD; i++) {
		pmdval = (i << PGDIR_SHIFT) |
			 PMD_SECT_AP_WRITE | PMD_SECT_AP_READ |
			 PMD_BIT4 | PMD_TYPE_SECT;
		pmd = pmd_offset(pgd + i, i << PGDIR_SHIFT);
		set_pmd(pmd, __pmd(pmdval));
	}
}

/*
 * Setup initial mappings.  We use the page we allocated for zero page to hold
 * the mappings, which will get overwritten by the vectors in traps_init().
 * The mappings must be in virtual address order.
 */
void __init memtable_init(struct meminfo *mi)
{
	struct map_desc *init_maps, *p, *q;
	unsigned long address = 0;
	int i;

	build_mem_type_table();

	init_maps = p = alloc_bootmem_low_pages(PAGE_SIZE);

	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0)
			continue;

		p->physical   = mi->bank[i].start;
		p->virtual    = __phys_to_virt(p->physical);
		p->length     = mi->bank[i].size;
		p->type       = MT_MEMORY;
		p ++;
	}

#ifdef FLUSH_BASE
	p->physical   = FLUSH_BASE_PHYS;
	p->virtual    = FLUSH_BASE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_CACHECLEAN;
	p ++;
#endif

#ifdef FLUSH_BASE_MINICACHE
	p->physical   = FLUSH_BASE_PHYS + PGDIR_SIZE;
	p->virtual    = FLUSH_BASE_MINICACHE;
	p->length     = PGDIR_SIZE;
	p->type       = MT_MINICLEAN;
	p ++;
#endif

	/*
	 * Go through the initial mappings, but clear out any
	 * pgdir entries that are not in the description.
	 */
	q = init_maps;
	do {
		if (address < q->virtual || q == p) {
			clear_mapping(address);
			address += PGDIR_SIZE;
		} else {
			create_mapping(q);

			address = q->virtual + q->length;
			address = (address + PGDIR_SIZE - 1) & PGDIR_MASK;

			q ++;
		}
	} while (address != 0);

	/*
	 * Create a mapping for the machine vectors at virtual address 0
	 * or 0xffff0000.  We should always try the high mapping.
	 */
	init_maps->physical   = virt_to_phys(init_maps);
	init_maps->virtual    = vectors_base();
	init_maps->length     = PAGE_SIZE;
	init_maps->type       = MT_VECTORS;

	create_mapping(init_maps);

	flush_cache_all();
	flush_tlb_all();
}

/*
 * Create the architecture specific mappings
 */
void __init iotable_init(struct map_desc *io_desc, int nr)
{
	int i;

	for (i = 0; i < nr; i++)
		create_mapping(io_desc + i);
}

static inline void free_memmap(int node, unsigned long start, unsigned long end)
{
	unsigned long pg, pgend;

	start = __phys_to_virt(start);
	end   = __phys_to_virt(end);

	pg    = PAGE_ALIGN((unsigned long)(virt_to_page(start)));
	pgend = ((unsigned long)(virt_to_page(end))) & PAGE_MASK;

	start = __virt_to_phys(pg);
	end   = __virt_to_phys(pgend);

	free_bootmem_node(NODE_DATA(node), start, end - start);
}

static inline void free_unused_memmap_node(int node, struct meminfo *mi)
{
	unsigned long bank_start, prev_bank_end = 0;
	unsigned int i;

	/*
	 * [FIXME] This relies on each bank being in address order.  This
	 * may not be the case, especially if the user has provided the
	 * information on the command line.
	 */
	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0 || mi->bank[i].node != node)
			continue;

		bank_start = mi->bank[i].start & PAGE_MASK;

		/*
		 * If we had a previous bank, and there is a space
		 * between the current bank and the previous, free it.
		 */
		if (prev_bank_end && prev_bank_end != bank_start)
			free_memmap(node, prev_bank_end, bank_start);

		prev_bank_end = PAGE_ALIGN(mi->bank[i].start +
					   mi->bank[i].size);
	}
}

/*
 * The mem_map array can get very big.  Free
 * the unused area of the memory map.
 */
void __init create_memmap_holes(struct meminfo *mi)
{
	int node;

	for (node = 0; node < numnodes; node++)
		free_unused_memmap_node(node, mi);
}
