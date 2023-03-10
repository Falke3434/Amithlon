#ifndef _SPARC64_CACHEFLUSH_H
#define _SPARC64_CACHEFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>

/* Cache flush operations. */

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(__mm) \
	do { if ((__mm) == current->mm) flushw_user(); } while(0)
extern void flush_cache_range(struct vm_area_struct *, unsigned long, unsigned long);
#define flush_cache_page(vma, page) \
	flush_cache_mm((vma)->vm_mm)

/* 
 * On spitfire, the icache doesn't snoop local stores and we don't
 * use block commit stores (which invalidate icache lines) during
 * module load, so we need this.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);

extern void __flush_dcache_page(void *addr, int flush_icache);
extern void __flush_icache_page(unsigned long);
extern void flush_dcache_page_impl(struct page *page);
#ifdef CONFIG_SMP
extern void smp_flush_dcache_page_impl(struct page *page, int cpu);
extern void flush_dcache_page_all(struct mm_struct *mm, struct page *page);
#else
#define smp_flush_dcache_page_impl(page,cpu) flush_dcache_page_impl(page)
#define flush_dcache_page_all(mm,page) flush_dcache_page_impl(page)
#endif

extern void __flush_dcache_range(unsigned long start, unsigned long end);

extern void __flush_cache_all(void);

#ifndef CONFIG_SMP

#define flush_cache_all()	__flush_cache_all()

#else /* CONFIG_SMP */

extern void smp_flush_cache_all(void);

#endif /* ! CONFIG_SMP */

#define flush_icache_page(vma, pg)	do { } while(0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

extern void flush_dcache_page(struct page *page);

#endif /* _SPARC64_CACHEFLUSH_H */
