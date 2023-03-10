/*
 *  linux/include/asm-arm/proc-armv/pgalloc.h
 *
 *  Copyright (C) 2001-2002 Russell King
 *
 * Page table allocation/freeing primitives for 32-bit ARM processors.
 */
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include "pgtable.h"

/*
 * Allocate one PTE table.
 *
 * This actually allocates two hardware PTE tables, but we wrap this up
 * into one table thus:
 *
 *  +------------+
 *  |  h/w pt 0  |
 *  +------------+
 *  |  h/w pt 1  |
 *  +------------+
 *  | Linux pt 0 |
 *  +------------+
 *  | Linux pt 1 |
 *  +------------+
 */
static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	if (pte) {
		clear_page(pte);
		clean_dcache_area(pte, sizeof(pte_t) * PTRS_PER_PTE);
		pte += PTRS_PER_PTE;
	}

	return pte;
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	struct page *pte;

	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT, 0);
	if (pte) {
		void *page = page_address(pte);
		clear_page(page);
		clean_dcache_area(page, sizeof(pte_t) * PTRS_PER_PTE);
	}

	return pte;
}

/*
 * Free one PTE table.
 */
static inline void pte_free_kernel(pte_t *pte)
{
	if (pte) {
		pte -= PTRS_PER_PTE;
		free_page((unsigned long)pte);
	}
}

static inline void pte_free(struct page *pte)
{
	__free_page(pte);
}

/*
 * Populate the pmdp entry with a pointer to the pte.  This pmd is part
 * of the mm address space.
 *
 * Ensure that we always set both PMD entries.
 */
static inline void
pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmdp, pte_t *ptep)
{
	unsigned long pte_ptr = (unsigned long)ptep;
	unsigned long pmdval;

	BUG_ON(mm != &init_mm);

	/*
	 * The pmd must be loaded with the physical
	 * address of the PTE table
	 */
	pte_ptr -= PTRS_PER_PTE * sizeof(void *);
	pmdval = __pa(pte_ptr) | _PAGE_KERNEL_TABLE;
	pmdp[0] = __pmd(pmdval);
	pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t));
	flush_pmd_entry(pmdp);
}

static inline void
pmd_populate(struct mm_struct *mm, pmd_t *pmdp, struct page *ptep)
{
	unsigned long pmdval;

	BUG_ON(mm == &init_mm);

	pmdval = page_to_pfn(ptep) << PAGE_SHIFT | _PAGE_USER_TABLE;
	pmdp[0] = __pmd(pmdval);
	pmdp[1] = __pmd(pmdval + 256 * sizeof(pte_t));
	flush_pmd_entry(pmdp);
}
