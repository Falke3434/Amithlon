/*
 *   linux/mm/fremap.c
 * 
 * Explicit pagetable population and nonlinear (random) mappings support.
 *
 * started by Ingo Molnar, Copyright (C) 2002
 */

#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swapops.h>
#include <linux/rmap-locking.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline int zap_pte(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;

	if (pte_none(pte))
		return 0;
	if (pte_present(pte)) {
		unsigned long pfn = pte_pfn(pte);

		flush_cache_page(vma, addr);
		pte = ptep_get_and_clear(ptep);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			if (!PageReserved(page)) {
				if (pte_dirty(pte))
					set_page_dirty(page);
				page_remove_rmap(page, ptep);
				page_cache_release(page);
				mm->rss--;
			}
		}
		return 1;
	} else {
		if (!pte_file(pte))
			free_swap_and_cache(pte_to_swp_entry(pte));
		pte_clear(ptep);
		return 0;
	}
}

/*
 * Install a page to a given virtual memory address, release any
 * previously existing mapping.
 */
int install_page(struct mm_struct *mm, struct vm_area_struct *vma,
		unsigned long addr, struct page *page, pgprot_t prot)
{
	int err = -ENOMEM, flush;
	pte_t *pte;
	pgd_t *pgd;
	pmd_t *pmd;
	struct pte_chain *pte_chain;

	pte_chain = pte_chain_alloc(GFP_KERNEL);
	if (!pte_chain)
		goto err;
	pgd = pgd_offset(mm, addr);
	spin_lock(&mm->page_table_lock);

	pmd = pmd_alloc(mm, pgd, addr);
	if (!pmd)
		goto err_unlock;

	pte = pte_alloc_map(mm, pmd, addr);
	if (!pte)
		goto err_unlock;

	flush = zap_pte(mm, vma, addr, pte);

	mm->rss++;
	flush_icache_page(vma, page);
	set_pte(pte, mk_pte(page, prot));
	pte_chain = page_add_rmap(page, pte, pte_chain);
	pte_unmap(pte);
	if (flush)
		flush_tlb_page(vma, addr);
	update_mmu_cache(vma, addr, *pte);
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
	return 0;

err_unlock:
	spin_unlock(&mm->page_table_lock);
	pte_chain_free(pte_chain);
err:
	return err;
}

/***
 * sys_remap_file_pages - remap arbitrary pages of a shared backing store
 *                        file within an existing vma.
 * @start: start of the remapped virtual memory range
 * @size: size of the remapped virtual memory range
 * @prot: new protection bits of the range
 * @pgoff: to be mapped page of the backing store file
 * @flags: 0 or MAP_NONBLOCKED - the later will cause no IO.
 *
 * this syscall works purely via pagetables, so it's the most efficient
 * way to map the same (large) file into a given virtual window. Unlike
 * mmap()/mremap() it does not create any new vmas. The new mappings are
 * also safe across swapout.
 *
 * NOTE: the 'prot' parameter right now is ignored, and the vma's default
 * protection is used. Arbitrary protections might be implemented in the
 * future.
 */
long sys_remap_file_pages(unsigned long start, unsigned long size,
	unsigned long __prot, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	unsigned long end = start + size;
	struct vm_area_struct *vma;
	int err = -EINVAL;

	if (__prot)
		return err;
	/*
	 * Sanitize the syscall parameters:
	 */
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	/* Does the address range wrap, or is the span zero-sized? */
	if (start + size <= start)
		return err;

	/* Can we represent this offset inside this architecture's pte's? */
#if PTE_FILE_MAX_BITS < BITS_PER_LONG
	if (pgoff + (size >> PAGE_SHIFT) >= (1UL << PTE_FILE_MAX_BITS))
		return err;
#endif

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, start);
	/*
	 * Make sure the vma is shared, that it supports prefaulting,
	 * and that the remapped range is valid and fully within
	 * the single existing vma:
	 */
	if (vma && (vma->vm_flags & VM_SHARED) &&
		vma->vm_ops && vma->vm_ops->populate &&
			end > start && start >= vma->vm_start &&
				end <= vma->vm_end)
		err = vma->vm_ops->populate(vma, start, size, vma->vm_page_prot,
				pgoff, flags & MAP_NONBLOCK);

	up_read(&mm->mmap_sem);

	return err;
}

