/*
 * This file contains the routines setting up the linux page tables.
 *  -- paulus
 * 
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/io.h>

#include "mmu_decl.h"

unsigned long ioremap_base;
unsigned long ioremap_bot;
int io_bat_index;

#if defined(CONFIG_6xx) || defined(CONFIG_POWER3)
#define HAVE_BATS	1
#endif

extern char etext[], _stext[];

#ifdef HAVE_BATS
extern unsigned long v_mapped_by_bats(unsigned long va);
extern unsigned long p_mapped_by_bats(unsigned long pa);
void setbat(int index, unsigned long virt, unsigned long phys,
	    unsigned int size, int flags);

#else /* !HAVE_BATS */
#define v_mapped_by_bats(x)	(0UL)
#define p_mapped_by_bats(x)	(0UL)
#endif /* HAVE_BATS */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret;

	if ((ret = (pgd_t *)__get_free_page(GFP_KERNEL)) != NULL)
		clear_page(ret);
	return ret;
}

void pgd_free(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;
	extern int mem_init_done;
	extern void *early_get_page(void);

	if (mem_init_done)
		pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT);
	else
		pte = (pte_t *)early_get_page();
	if (pte)
		clear_page(pte);
	return pte;
}

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	int flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_REPEAT;
#else
	int flags = GFP_KERNEL | __GFP_REPEAT;
#endif

	pte = alloc_pages(flags, 0);
	if (pte)
		clear_highpage(pte);
	return pte;
}

void pte_free_kernel(pte_t *pte)
{
	free_page((unsigned long)pte);
}

void pte_free(struct page *pte)
{
	__free_page(pte);
}

void *
ioremap(unsigned long addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE);
}

void *
__ioremap(unsigned long addr, unsigned long size, unsigned long flags)
{
	unsigned long p, v, i;
	int err;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we use space going down from ioremap_base
	 * (ioremap_bot records where we're up to).
	 */
	p = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;

	/*
	 * If the address lies within the first 16 MB, assume it's in ISA
	 * memory space
	 */
	if (p < 16*1024*1024)
		p += _ISA_MEM_BASE;

	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 */
	if ( mem_init_done && (p < virt_to_phys(high_memory)) )
	{
		printk("__ioremap(): phys addr %0lx is RAM lr %p\n", p,
		       __builtin_return_address(0));
		return NULL;
	}

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped?  Perhaps overlapped by a previous
	 * BAT mapping.  If the whole area is mapped then we're done,
	 * otherwise remap it since we want to keep the virt addrs for
	 * each request contiguous.
	 *
	 * We make the assumption here that if the bottom and top
	 * of the range we want are mapped then it's mapped to the
	 * same virt address (and this is contiguous).
	 *  -- Cort
	 */
	if ((v = p_mapped_by_bats(p)) /*&& p_mapped_by_bats(p+size-1)*/ )
		goto out;
	
	if (mem_init_done) {
		struct vm_struct *area;
		area = get_vm_area(size, VM_IOREMAP);
		if (area == 0)
			return NULL;
		v = VMALLOC_VMADDR(area->addr);
	} else {
		v = (ioremap_bot -= size);
	}

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= _PAGE_KERNEL;
	if (flags & _PAGE_NO_CACHE)
		flags |= _PAGE_GUARDED;

	/*
	 * Should check if it is a candidate for a BAT mapping
	 */

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_page(v+i, p+i, flags);
	if (err) {
		if (mem_init_done)
			vunmap((void *)v);
		return NULL;
	}

out:
	return (void *) (v + (addr & ~PAGE_MASK));
}

void iounmap(void *addr)
{
	/*
	 * If mapped by BATs then there is nothing to do.
	 * Calling vfree() generates a benign warning.
	 */
	if (v_mapped_by_bats((unsigned long)addr)) return;

	if (addr > high_memory && (unsigned long) addr < ioremap_bot)
		vunmap((void *) (PAGE_MASK & (unsigned long)addr));
}

int
map_page(unsigned long va, unsigned long pa, int flags)
{
	pmd_t *pd;
	pte_t *pg;
	int err = -ENOMEM;

	spin_lock(&init_mm.page_table_lock);
	/* Use upper 10 bits of VA to index the first level map */
	pd = pmd_offset(pgd_offset_k(va), va);
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_alloc_kernel(&init_mm, pd, va);
	if (pg != 0) {
		err = 0;
		set_pte(pg, pfn_pte(pa >> PAGE_SHIFT, __pgprot(flags)));
		if (mem_init_done)
			flush_HPTE(0, va, pmd_val(*pd));
	}
	spin_unlock(&init_mm.page_table_lock);
	return err;
}

/*
 * Map in all of physical memory starting at KERNELBASE.
 */
void __init mapin_ram(void)
{
	unsigned long v, p, s, f;

	s = mmu_mapin_ram();
	v = KERNELBASE + s;
	p = PPC_MEMSTART + s;
	for (; s < total_lowmem; s += PAGE_SIZE) {
		if ((char *) v >= _stext && (char *) v < etext)
			f = _PAGE_RAM_TEXT;
		else
			f = _PAGE_RAM;
		map_page(v, p, f);
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

/* is x a power of 2? */
#define is_power_of_2(x)	((x) != 0 && (((x) & ((x) - 1)) == 0))

/*
 * Set up a mapping for a block of I/O.
 * virt, phys, size must all be page-aligned.
 * This should only be called before ioremap is called.
 */
void __init io_block_mapping(unsigned long virt, unsigned long phys,
			     unsigned int size, int flags)
{
	int i;

	if (virt > KERNELBASE && virt < ioremap_bot)
		ioremap_bot = ioremap_base = virt;

#ifdef HAVE_BATS
	/*
	 * Use a BAT for this if possible...
	 */
	if (io_bat_index < 2 && is_power_of_2(size)
	    && (virt & (size - 1)) == 0 && (phys & (size - 1)) == 0) {
		setbat(io_bat_index, virt, phys, size, flags);
		++io_bat_index;
		return;
	}
#endif /* HAVE_BATS */

	/* No BATs available, put it in the page tables. */
	for (i = 0; i < size; i += PAGE_SIZE)
		map_page(virt + i, phys + i, flags);
}

/* Scan the real Linux page tables and return a PTE pointer for
 * a virtual address in a context.
 * Returns true (1) if PTE was found, zero otherwise.  The pointer to
 * the PTE pointer is unmodified if PTE is not found.
 */
int
get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep)
{
        pgd_t	*pgd;
        pmd_t	*pmd;
        pte_t	*pte;
        int     retval = 0;

        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
                pmd = pmd_offset(pgd, addr & PAGE_MASK);
                if (pmd_present(*pmd)) {
                        pte = pte_offset_map(pmd, addr & PAGE_MASK);
                        if (pte) {
				retval = 1;
				*ptep = pte;
				/* XXX caller needs to do pte_unmap, yuck */
                        }
                }
        }
        return(retval);
}

/* Find physical address for this virtual address.  Normally used by
 * I/O functions, but anyone can call it.
 */
unsigned long iopa(unsigned long addr)
{
	unsigned long pa;

	/* I don't know why this won't work on PMacs or CHRP.  It
	 * appears there is some bug, or there is some implicit
	 * mapping done not properly represented by BATs or in page
	 * tables.......I am actively working on resolving this, but
	 * can't hold up other stuff.  -- Dan
	 */
	pte_t *pte;
	struct mm_struct *mm;

	/* Check the BATs */
	pa = v_mapped_by_bats(addr);
	if (pa)
		return pa;

	/* Allow mapping of user addresses (within the thread)
	 * for DMA if necessary.
	 */
	if (addr < TASK_SIZE)
		mm = current->mm;
	else
		mm = &init_mm;
	
	pa = 0;
	if (get_pteptr(mm, addr, &pte)) {
		pa = (pte_val(*pte) & PAGE_MASK) | (addr & ~PAGE_MASK);
		pte_unmap(pte);
	}

	return(pa);
}

/* This is will find the virtual address for a physical one....
 * Swiped from APUS, could be dangerous :-).
 * This is only a placeholder until I really find a way to make this
 * work.  -- Dan
 */
unsigned long
mm_ptov (unsigned long paddr)
{
	unsigned long ret;
#if 0
	if (paddr < 16*1024*1024)
		ret = ZTWO_VADDR(paddr);
	else {
		int i;

		for (i = 0; i < kmap_chunk_count;){
			unsigned long phys = kmap_chunks[i++];
			unsigned long size = kmap_chunks[i++];
			unsigned long virt = kmap_chunks[i++];
			if (paddr >= phys
			    && paddr < (phys + size)){
				ret = virt + paddr - phys;
				goto exit;
			}
		}
		
		ret = (unsigned long) __va(paddr);
	}
exit:
#ifdef DEBUGPV
	printk ("PTOV(%lx)=%lx\n", paddr, ret);
#endif
#else
	ret = (unsigned long)paddr + KERNELBASE;
#endif
	return ret;
}

