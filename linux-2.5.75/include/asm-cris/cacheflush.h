#ifndef _CRIS_CACHEFLUSH_H
#define _CRIS_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/* The cache doesn't need to be flushed when TLB entries change because 
 * the cache is mapped to physical memory, not virtual memory
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_page_to_ram(page)			do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

void global_flush_tlb(void); 
int change_page_attr(struct page *page, int numpages, pgprot_t prot);

#endif /* _CRIS_CACHEFLUSH_H */
