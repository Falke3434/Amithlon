/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 01, 02, 03 by Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef __ASM_CACHEFLUSH_H
#define __ASM_CACHEFLUSH_H

#include <linux/config.h>

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/* Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_page(mm, vmaddr) flushes a single page
 *  - flush_cache_range(vma, start, end) flushes a range of pages
 *  - flush_icache_range(start, end) flush a range of instructions
 *  - flush_dcache_page(pg) flushes(wback&invalidates) a page for dcache
 *  - flush_icache_page(vma, pg) flushes(invalidates) a page for icache
 *
 * MIPS specific flush operations:
 *
 *  - flush_cache_sigtramp() flush signal trampoline
 *  - flush_icache_all() flush the entire instruction cache
 *  - flush_data_cache_page() flushes a page from the data cache
 */
extern void (*flush_cache_all)(void);
extern void (*__flush_cache_all)(void);
extern void (*flush_cache_mm)(struct mm_struct *mm);
extern void (*flush_cache_range)(struct vm_area_struct *vma,
	unsigned long start, unsigned long end);
extern void (*flush_cache_page)(struct vm_area_struct *vma,
	unsigned long page);
extern void flush_dcache_page(struct page *page);
extern void (*flush_icache_page)(struct vm_area_struct *vma,
	struct page *page);
extern void (*flush_icache_range)(unsigned long start, unsigned long end);
#define flush_icache_user_range(vma, page, addr, len)   \
					flush_icache_page(vma, page)


extern void (*flush_cache_sigtramp)(unsigned long addr);
extern void (*flush_icache_all)(void);
extern void (*flush_data_cache_page)(unsigned long addr);

/*
 * This flag is used to indicate that the page pointed to by a pte
 * is dirty and requires cleaning before returning it to the user.
 */
#define PG_dcache_dirty			PG_arch_1

#define Page_dcache_dirty(page)		\
	test_bit(PG_dcache_dirty, &(page)->flags)
#define SetPageDcacheDirty(page)	\
	set_bit(PG_dcache_dirty, &(page)->flags)
#define ClearPageDcacheDirty(page)	\
	clear_bit(PG_dcache_dirty, &(page)->flags)

#endif /* __ASM_CACHEFLUSH_H */
