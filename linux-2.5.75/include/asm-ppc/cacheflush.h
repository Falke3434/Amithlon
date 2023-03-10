/*
 * include/asm-ppc/cacheflush.h
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__
#ifndef _PPC_CACHEFLUSH_H
#define _PPC_CACHEFLUSH_H

#include <linux/mm.h>

/*
 * No cache flushing is required when address mappings are
 * changed, because the caches on PowerPCs are physically
 * addressed.  -- paulus
 * Also, when SMP we use the coherency (M) bit of the
 * BATs and PTEs.  -- Cort
 */
#define flush_cache_all()		do { } while (0)
#define flush_cache_mm(mm)		do { } while (0)
#define flush_cache_range(vma, a, b)	do { } while (0)
#define flush_cache_page(vma, p)	do { } while (0)
#define flush_icache_page(vma, page)	do { } while (0)

extern void flush_dcache_page(struct page *page);
extern void flush_icache_range(unsigned long, unsigned long);
extern void flush_icache_user_range(struct vm_area_struct *vma,
		struct page *page, unsigned long addr, int len);

extern void __flush_dcache_icache(void *page_va);
extern void __flush_dcache_icache_phys(unsigned long physaddr);

#endif /* _PPC_CACHEFLUSH_H */
#endif /* __KERNEL__ */
