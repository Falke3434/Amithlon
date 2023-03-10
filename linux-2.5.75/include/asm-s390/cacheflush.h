#ifndef _S390_CACHEFLUSH_H
#define _S390_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

/* Caches aren't brain-dead on the s390. */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

#endif /* _S390_CACHEFLUSH_H */
