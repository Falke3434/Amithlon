/*
** ccio-dma.c:
**	DMA management routines for first generation cache-coherent machines.
**	Program U2/Uturn in "Virtual Mode" and use the I/O MMU.
**
**	(c) Copyright 2000 Grant Grundler
**	(c) Copyright 2000 Ryan Bradetich
**	(c) Copyright 2000 Hewlett-Packard Company
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
**
**  "Real Mode" operation refers to U2/Uturn chip operation.
**  U2/Uturn were designed to perform coherency checks w/o using
**  the I/O MMU - basically what x86 does.
**
**  Philipp Rumpf has a "Real Mode" driver for PCX-W machines at:
**      CVSROOT=:pserver:anonymous@198.186.203.37:/cvsroot/linux-parisc
**      cvs -z3 co linux/arch/parisc/kernel/dma-rm.c
**
**  I've rewritten his code to work under TPG's tree. See ccio-rm-dma.c.
**
**  Drawbacks of using Real Mode are:
**	o outbound DMA is slower - U2 won't prefetch data (GSC+ XQL signal).
**      o Inbound DMA less efficient - U2 can't use DMA_FAST attribute.
**	o Ability to do scatter/gather in HW is lost.
**	o Doesn't work under PCX-U/U+ machines since they didn't follow
**        the coherency design originally worked out. Only PCX-W does.
*/

#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#define PCI_DEBUG
#include <linux/pci.h>
#undef PCI_DEBUG

#include <asm/byteorder.h>
#include <asm/cache.h>		/* for L1_CACHE_BYTES */
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hardware.h>       /* for register_module() */
#include <asm/parisc-device.h>

/* 
** Choose "ccio" since that's what HP-UX calls it.
** Make it easier for folks to migrate from one to the other :^)
*/
#define MODULE_NAME "ccio"

#undef DEBUG_CCIO_RES
#undef DEBUG_CCIO_RUN
#undef DEBUG_CCIO_INIT
#undef DEBUG_CCIO_RUN_SG

#include <linux/proc_fs.h>
#include <asm/runway.h>		/* for proc_runway_root */

#ifdef DEBUG_CCIO_INIT
#define DBG_INIT(x...)  printk(x)
#else
#define DBG_INIT(x...)
#endif

#ifdef DEBUG_CCIO_RUN
#define DBG_RUN(x...)   printk(x)
#else
#define DBG_RUN(x...)
#endif

#ifdef DEBUG_CCIO_RES
#define DBG_RES(x...)   printk(x)
#else
#define DBG_RES(x...)
#endif

#ifdef DEBUG_CCIO_RUN_SG
#define DBG_RUN_SG(x...) printk(x)
#else
#define DBG_RUN_SG(x...)
#endif

#define CCIO_INLINE	/* inline */
#define WRITE_U32(value, addr) gsc_writel(value, (u32 *)(addr))
#define READ_U32(addr) gsc_readl((u32 *)(addr))

#define U2_IOA_RUNWAY 0x580
#define U2_BC_GSC     0x501
#define UTURN_IOA_RUNWAY 0x581
#define UTURN_BC_GSC     0x502

#define IOA_NORMAL_MODE      0x00020080 /* IO_CONTROL to turn on CCIO        */
#define CMD_TLB_DIRECT_WRITE 35         /* IO_COMMAND for I/O TLB Writes     */
#define CMD_TLB_PURGE        33         /* IO_COMMAND to Purge I/O TLB entry */

struct ioa_registers {
        /* Runway Supervisory Set */
        volatile int32_t    unused1[12];
        volatile uint32_t   io_command;             /* Offset 12 */
        volatile uint32_t   io_status;              /* Offset 13 */
        volatile uint32_t   io_control;             /* Offset 14 */
        volatile int32_t    unused2[1];

        /* Runway Auxiliary Register Set */
        volatile uint32_t   io_err_resp;            /* Offset  0 */
        volatile uint32_t   io_err_info;            /* Offset  1 */
        volatile uint32_t   io_err_req;             /* Offset  2 */
        volatile uint32_t   io_err_resp_hi;         /* Offset  3 */
        volatile uint32_t   io_tlb_entry_m;         /* Offset  4 */
        volatile uint32_t   io_tlb_entry_l;         /* Offset  5 */
        volatile uint32_t   unused3[1];
        volatile uint32_t   io_pdir_base;           /* Offset  7 */
        volatile uint32_t   io_io_low_hv;           /* Offset  8 */
        volatile uint32_t   io_io_high_hv;          /* Offset  9 */
        volatile uint32_t   unused4[1];
        volatile uint32_t   io_chain_id_mask;       /* Offset 11 */
        volatile uint32_t   unused5[2];
        volatile uint32_t   io_io_low;              /* Offset 14 */
        volatile uint32_t   io_io_high;             /* Offset 15 */
};

struct ioc {
	struct ioa_registers *ioc_hpa;  /* I/O MMU base address */
	u8  *res_map;	                /* resource map, bit == pdir entry */
	u64 *pdir_base;	                /* physical base address */
	u32 res_hint;	                /* next available IOVP - 
					   circular search */
	u32 res_size;		    	/* size of resource map in bytes */
	spinlock_t res_lock;

#ifdef CONFIG_PROC_FS
#define CCIO_SEARCH_SAMPLE 0x100
	unsigned long avg_search[CCIO_SEARCH_SAMPLE];
	unsigned long avg_idx;		  /* current index into avg_search */
	unsigned long used_pages;
	unsigned long msingle_calls;
	unsigned long msingle_pages;
	unsigned long msg_calls;
	unsigned long msg_pages;
	unsigned long usingle_calls;
	unsigned long usingle_pages;
	unsigned long usg_calls;
	unsigned long usg_pages;

	unsigned short cujo20_bug;
#endif

	/* STUFF We don't need in performance path */
	u32 pdir_size; 			/* in bytes, determined by IOV Space size */
	u32 chainid_shift; 		/* specify bit location of chain_id */
	struct ioc *next;		/* Linked list of discovered iocs */
	const char *name;		/* device name from firmware */
	unsigned int hw_path;           /* the hardware path this ioc is associatd with */
	struct pci_dev *fake_pci_dev;   /* the fake pci_dev for non-pci devs */
	struct resource mmio_region[2]; /* The "routed" MMIO regions */
};

/* Ratio of Host MEM to IOV Space size */
static unsigned long ccio_mem_ratio = 4;
static struct ioc *ioc_list;
static int ioc_count;

/**************************************************************
*
*   I/O Pdir Resource Management
*
*   Bits set in the resource map are in use.
*   Each bit can represent a number of pages.
*   LSbs represent lower addresses (IOVA's).
*
*   This was was copied from sba_iommu.c. Don't try to unify
*   the two resource managers unless a way to have different
*   allocation policies is also adjusted. We'd like to avoid
*   I/O TLB thrashing by having resource allocation policy
*   match the I/O TLB replacement policy.
*
***************************************************************/
#define IOVP_SIZE PAGE_SIZE
#define IOVP_SHIFT PAGE_SHIFT
#define IOVP_MASK PAGE_MASK

/* Convert from IOVP to IOVA and vice versa. */
#define CCIO_IOVA(iovp,offset) ((iovp) | (offset))
#define CCIO_IOVP(iova) ((iova) & IOVP_MASK)

#define PDIR_INDEX(iovp)    ((iovp)>>IOVP_SHIFT)
#define MKIOVP(pdir_idx)    ((long)(pdir_idx) << IOVP_SHIFT)
#define MKIOVA(iovp,offset) (dma_addr_t)((long)iovp | (long)offset)
#define ROUNDUP(x,y) ((x + ((y)-1)) & ~((y)-1))

/*
** Don't worry about the 150% average search length on a miss.
** If the search wraps around, and passes the res_hint, it will
** cause the kernel to panic anyhow.
*/
#define CCIO_SEARCH_LOOP(ioc, res_idx, mask_ptr, size)  \
       for(; res_ptr < res_end; ++res_ptr) { \
               if(0 == (*res_ptr & *mask_ptr)) { \
                       *res_ptr |= *mask_ptr; \
                       res_idx = (int)((unsigned long)res_ptr - (unsigned long)ioc->res_map); \
                       ioc->res_hint = res_idx + (size >> 3); \
                       goto resource_found; \
               } \
       }

#define CCIO_FIND_FREE_MAPPING(ioa, res_idx, mask, size) \
       u##size *res_ptr = (u##size *)&((ioc)->res_map[ioa->res_hint & ~((size >> 3) - 1)]); \
       u##size *res_end = (u##size *)&(ioc)->res_map[ioa->res_size]; \
       u##size *mask_ptr = (u##size *)&mask; \
       CCIO_SEARCH_LOOP(ioc, res_idx, mask_ptr, size); \
       res_ptr = (u##size *)&(ioc)->res_map[0]; \
       CCIO_SEARCH_LOOP(ioa, res_idx, mask_ptr, size);

/*
** Find available bit in this ioa's resource map.
** Use a "circular" search:
**   o Most IOVA's are "temporary" - avg search time should be small.
** o keep a history of what happened for debugging
** o KISS.
**
** Perf optimizations:
** o search for log2(size) bits at a time.
** o search for available resource bits using byte/word/whatever.
** o use different search for "large" (eg > 4 pages) or "very large"
**   (eg > 16 pages) mappings.
*/

/**
 * ccio_alloc_range - Allocate pages in the ioc's resource map.
 * @ioc: The I/O Controller.
 * @pages_needed: The requested number of pages to be mapped into the
 * I/O Pdir...
 *
 * This function searches the resource map of the ioc to locate a range
 * of available pages for the requested size.
 */
static int
ccio_alloc_range(struct ioc *ioc, unsigned long pages_needed)
{
	int res_idx;
	unsigned long mask;
#ifdef CONFIG_PROC_FS
	unsigned long cr_start = mfctl(16);
#endif
	
	ASSERT(pages_needed);
	ASSERT((pages_needed * IOVP_SIZE) <= DMA_CHUNK_SIZE);
	ASSERT(pages_needed <= BITS_PER_LONG);

	mask = ~(~0UL >> pages_needed);
     
	DBG_RES("%s() size: %d pages_needed %d mask 0x%08lx\n", 
		__FUNCTION__, size, pages_needed, mask);

	/*
	** "seek and ye shall find"...praying never hurts either...
	** ggg sacrifices another 710 to the computer gods.
	*/

	if(pages_needed <= 8) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, mask, 8);
	} else if(pages_needed <= 16) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, mask, 16);
	} else if(pages_needed <= 32) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, mask, 32);
#ifdef __LP64__
	} else if(pages_needed <= 64) {
		CCIO_FIND_FREE_MAPPING(ioc, res_idx, mask, 64);
#endif
	} else {
		panic(__FILE__ ": %s() Too many pages to map. pages_needed: %ld\n", 
		      __FUNCTION__, pages_needed);
	}

	panic(__FILE__ ": %s() I/O MMU is out of mapping resources.\n", 
	      __FUNCTION__);
	
resource_found:
	
	DBG_RES("%s() res_idx %d mask 0x%08lx res_hint: %d\n",
		__FUNCTION__, res_idx, mask, ioc->res_hint);

#ifdef CONFIG_PROC_FS
	{
		unsigned long cr_end = mfctl(16);
		unsigned long tmp = cr_end - cr_start;
		/* check for roll over */
		cr_start = (cr_end < cr_start) ?  -(tmp) : (tmp);
	}
	ioc->avg_search[ioc->avg_idx++] = cr_start;
	ioc->avg_idx &= CCIO_SEARCH_SAMPLE - 1;

	ioc->used_pages += pages_needed;
#endif

	/* 
	** return the bit address.
	*/
	return res_idx << 3;
}

#define CCIO_FREE_MAPPINGS(ioc, res_idx, mask, size) \
        u##size *res_ptr = (u##size *)&((ioc)->res_map[res_idx]); \
	u##size *mask_ptr = (u##size *)&mask; \
        ASSERT((*res_ptr & *mask_ptr) == *mask_ptr); \
        *res_ptr &= ~(*mask_ptr);

/**
 * ccio_free_range - Free pages from the ioc's resource map.
 * @ioc: The I/O Controller.
 * @iova: The I/O Virtual Address.
 * @pages_mapped: The requested number of pages to be freed from the
 * I/O Pdir.
 *
 * This function frees the resouces allocated for the iova.
 */
static void
ccio_free_range(struct ioc *ioc, dma_addr_t iova, unsigned long pages_mapped)
{
	unsigned long mask;
	unsigned long iovp = CCIO_IOVP(iova);
	unsigned int res_idx = PDIR_INDEX(iovp) >> 3;

	ASSERT(pages_mapped);
	ASSERT((pages_mapped * IOVP_SIZE) <= DMA_CHUNK_SIZE);
	ASSERT(pages_mapped <= BITS_PER_LONG);

	mask = ~(~0UL >> pages_mapped);

	DBG_RES("%s():  res_idx: %d pages_mapped %d mask 0x%08lx\n", 
		__FUNCTION__, res_idx, pages_mapped, mask);

#ifdef CONFIG_PROC_FS
	ioc->used_pages -= pages_mapped;
#endif

	if(pages_mapped <= 8) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, mask, 8);
	} else if(pages_mapped <= 16) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, mask, 16);
	} else if(pages_mapped <= 32) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, mask, 32);
#ifdef __LP64__
	} else if(pages_mapped <= 64) {
		CCIO_FREE_MAPPINGS(ioc, res_idx, mask, 64);
#endif
	} else {
		panic(__FILE__ ":%s() Too many pages to unmap.\n", 
		      __FUNCTION__);
	}
}

/****************************************************************
**
**          CCIO dma_ops support routines
**
*****************************************************************/

typedef unsigned long space_t;
#define KERNEL_SPACE 0

/*
** DMA "Page Type" and Hints 
** o if SAFE_DMA isn't set, mapping is for FAST_DMA. SAFE_DMA should be
**   set for subcacheline DMA transfers since we don't want to damage the
**   other part of a cacheline.
** o SAFE_DMA must be set for "memory" allocated via pci_alloc_consistent().
**   This bit tells U2 to do R/M/W for partial cachelines. "Streaming"
**   data can avoid this if the mapping covers full cache lines.
** o STOP_MOST is needed for atomicity across cachelines.
**   Apperently only "some EISA devices" need this.
**   Using CONFIG_ISA is hack. Only the IOA with EISA under it needs
**   to use this hint iff the EISA devices needs this feature.
**   According to the U2 ERS, STOP_MOST enabled pages hurt performance.
** o PREFETCH should *not* be set for cases like Multiple PCI devices
**   behind GSCtoPCI (dino) bus converter. Only one cacheline per GSC
**   device can be fetched and multiply DMA streams will thrash the
**   prefetch buffer and burn memory bandwidth. See 6.7.3 "Prefetch Rules
**   and Invalidation of Prefetch Entries".
**
** FIXME: the default hints need to be per GSC device - not global.
** 
** HP-UX dorks: linux device driver programming model is totally different
**    than HP-UX's. HP-UX always sets HINT_PREFETCH since it's drivers
**    do special things to work on non-coherent platforms...linux has to
**    be much more careful with this.
*/
#define IOPDIR_VALID    0x01UL
#define HINT_SAFE_DMA   0x02UL	/* used for pci_alloc_consistent() pages */
#ifdef CONFIG_ISA	/* EISA support really */
#define HINT_STOP_MOST  0x04UL	/* LSL support */
#else
#define HINT_STOP_MOST  0x00UL	/* only needed for "some EISA devices" */
#endif
#define HINT_UDPATE_ENB 0x08UL  /* not used/supported by U2 */
#define HINT_PREFETCH   0x10UL	/* for outbound pages which are not SAFE */


/*
** Use direction (ie PCI_DMA_TODEVICE) to pick hint.
** ccio_alloc_consistent() depends on this to get SAFE_DMA
** when it passes in BIDIRECTIONAL flag.
*/
static u32 hint_lookup[] = {
	[PCI_DMA_BIDIRECTIONAL]	= HINT_STOP_MOST | HINT_SAFE_DMA | IOPDIR_VALID,
	[PCI_DMA_TODEVICE]	= HINT_STOP_MOST | HINT_PREFETCH | IOPDIR_VALID,
	[PCI_DMA_FROMDEVICE]	= HINT_STOP_MOST | IOPDIR_VALID,
};

/**
 * ccio_io_pdir_entry - Initialize an I/O Pdir.
 * @pdir_ptr: A pointer into I/O Pdir.
 * @sid: The Space Identifier.
 * @vba: The virtual address.
 * @hints: The DMA Hint.
 *
 * Given a virtual address (vba, arg2) and space id, (sid, arg1),
 * load the I/O PDIR entry pointed to by pdir_ptr (arg0). Each IO Pdir
 * entry consists of 8 bytes as shown below (MSB == bit 0):
 *
 *
 * WORD 0:
 * +------+----------------+-----------------------------------------------+
 * | Phys | Virtual Index  |               Phys                            |
 * | 0:3  |     0:11       |               4:19                            |
 * |4 bits|   12 bits      |              16 bits                          |
 * +------+----------------+-----------------------------------------------+
 * WORD 1:
 * +-----------------------+-----------------------------------------------+
 * |      Phys    |  Rsvd  | Prefetch |Update |Rsvd  |Lock  |Safe  |Valid  |
 * |     20:39    |        | Enable   |Enable |      |Enable|DMA   |       |
 * |    20 bits   | 5 bits | 1 bit    |1 bit  |2 bits|1 bit |1 bit |1 bit  |
 * +-----------------------+-----------------------------------------------+
 *
 * The virtual index field is filled with the results of the LCI
 * (Load Coherence Index) instruction.  The 8 bits used for the virtual
 * index are bits 12:19 of the value returned by LCI.
 */ 
void CCIO_INLINE
ccio_io_pdir_entry(u64 *pdir_ptr, space_t sid, void * vba, unsigned long hints)
{
	register unsigned long pa = (volatile unsigned long) vba;
	register unsigned long ci; /* coherent index */

	/* We currently only support kernel addresses */
	ASSERT(sid == KERNEL_SPACE);

	mtsp(sid,1);

	/*
	** WORD 1 - low order word
	** "hints" parm includes the VALID bit!
	** "dep" clobbers the physical address offset bits as well.
	*/
	pa = virt_to_phys(vba);
	asm volatile("depw  %1,31,12,%0" : "+r" (pa) : "r" (hints));
	((u32 *)pdir_ptr)[1] = (u32) pa;

	/*
	** WORD 0 - high order word
	*/

#ifdef __LP64__
	/*
	** get bits 12:15 of physical address
	** shift bits 16:31 of physical address
	** and deposit them
	*/
	asm volatile ("extrd,u %1,15,4,%0" : "=r" (ci) : "r" (pa));
	asm volatile ("extrd,u %1,31,16,%0" : "+r" (pa) : "r" (pa));
	asm volatile ("depd  %1,35,4,%0" : "+r" (pa) : "r" (ci));
#else
	pa = 0;
#endif
	/*
	** get CPU coherency index bits
	** Grab virtual index [0:11]
	** Deposit virt_idx bits into I/O PDIR word
	*/
	asm volatile ("lci 0(%%sr1, %1), %0" : "=r" (ci) : "r" (vba));
	asm volatile ("extru %1,19,12,%0" : "+r" (ci) : "r" (ci));
	asm volatile ("depw  %1,15,12,%0" : "+r" (pa) : "r" (ci));

	((u32 *)pdir_ptr)[0] = (u32) pa;


	/* FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
	**        PCX-U/U+ do. (eg C200/C240)
	**        PCX-T'? Don't know. (eg C110 or similar K-class)
	**
	** See PDC_MODEL/option 0/SW_CAP word for "Non-coherent IO-PDIR bit".
	** Hopefully we can patch (NOP) these out at boot time somehow.
	**
	** "Since PCX-U employs an offset hash that is incompatible with
	** the real mode coherence index generation of U2, the PDIR entry
	** must be flushed to memory to retain coherence."
	*/
	asm volatile("fdc 0(%0)" : : "r" (pdir_ptr));
	asm volatile("sync");
}

/**
 * ccio_clear_io_tlb - Remove stale entries from the I/O TLB.
 * @ioc: The I/O Controller.
 * @iovp: The I/O Virtual Page.
 * @byte_cnt: The requested number of bytes to be freed from the I/O Pdir.
 *
 * Purge invalid I/O PDIR entries from the I/O TLB.
 *
 * FIXME: Can we change the byte_cnt to pages_mapped?
 */
static CCIO_INLINE void
ccio_clear_io_tlb(struct ioc *ioc, dma_addr_t iovp, size_t byte_cnt)
{
	u32 chain_size = 1 << ioc->chainid_shift;

	iovp &= IOVP_MASK;	/* clear offset bits, just want pagenum */
	byte_cnt += chain_size;

	while(byte_cnt > chain_size) {
		WRITE_U32(CMD_TLB_PURGE | iovp, &ioc->ioc_hpa->io_command);
		iovp += chain_size;
		byte_cnt -= chain_size;
      }
}

/**
 * ccio_mark_invalid - Mark the I/O Pdir entries invalid.
 * @ioc: The I/O Controller.
 * @iova: The I/O Virtual Address.
 * @byte_cnt: The requested number of bytes to be freed from the I/O Pdir.
 *
 * Mark the I/O Pdir entries invalid and blow away the corresponding I/O
 * TLB entries.
 *
 * FIXME: at some threshhold it might be "cheaper" to just blow
 *        away the entire I/O TLB instead of individual entries.
 *
 * FIXME: Uturn has 256 TLB entries. We don't need to purge every
 *        PDIR entry - just once for each possible TLB entry.
 *        (We do need to maker I/O PDIR entries invalid regardless).
 *
 * FIXME: Can we change byte_cnt to pages_mapped?
 */ 
static CCIO_INLINE void
ccio_mark_invalid(struct ioc *ioc, dma_addr_t iova, size_t byte_cnt)
{
	u32 iovp = (u32)CCIO_IOVP(iova);
	size_t saved_byte_cnt;

	/* round up to nearest page size */
	saved_byte_cnt = byte_cnt = ROUNDUP(byte_cnt, IOVP_SIZE);

	while(byte_cnt > 0) {
		/* invalidate one page at a time */
		unsigned int idx = PDIR_INDEX(iovp);
		char *pdir_ptr = (char *) &(ioc->pdir_base[idx]);

		ASSERT(idx < (ioc->pdir_size / sizeof(u64)));
		pdir_ptr[7] = 0;	/* clear only VALID bit */ 
		/*
		** FIXME: PCX_W platforms don't need FDC/SYNC. (eg C360)
		**   PCX-U/U+ do. (eg C200/C240)
		** See PDC_MODEL/option 0/SW_CAP for "Non-coherent IO-PDIR bit".
		**
		** Hopefully someone figures out how to patch (NOP) the
		** FDC/SYNC out at boot time.
		*/
		asm volatile("fdc 0(%0)" : : "r" (pdir_ptr[7]));

		iovp     += IOVP_SIZE;
		byte_cnt -= IOVP_SIZE;
	}

	asm volatile("sync");
	ccio_clear_io_tlb(ioc, CCIO_IOVP(iova), saved_byte_cnt);
}

/****************************************************************
**
**          CCIO dma_ops
**
*****************************************************************/

/**
 * ccio_dma_supported - Verify the IOMMU supports the DMA address range.
 * @dev: The PCI device.
 * @mask: A bit mask describing the DMA address range of the device.
 *
 * This function implements the pci_dma_supported function.
 */
static int 
ccio_dma_supported(struct device *dev, u64 mask)
{
	if(dev == NULL) {
		printk(KERN_ERR MODULE_NAME ": EISA/ISA/et al not supported\n");
		BUG();
		return 0;
	}

	/* only support 32-bit devices (ie PCI/GSC) */
	return (int)(mask == 0xffffffffUL);
}

/**
 * ccio_map_single - Map an address range into the IOMMU.
 * @dev: The PCI device.
 * @addr: The start address of the DMA region.
 * @size: The length of the DMA region.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_map_single function.
 */
static dma_addr_t 
ccio_map_single(struct device *dev, void *addr, size_t size,
		enum dma_data_direction direction)
{
	int idx;
	struct ioc *ioc;
	unsigned long flags;
	dma_addr_t iovp;
	dma_addr_t offset;
	u64 *pdir_start;
	unsigned long hint = hint_lookup[(int)direction];

	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	ASSERT(size > 0);

	/* save offset bits */
	offset = ((unsigned long) addr) & ~IOVP_MASK;

	/* round up to nearest IOVP_SIZE */
	size = ROUNDUP(size + offset, IOVP_SIZE);
	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CONFIG_PROC_FS
	ioc->msingle_calls++;
	ioc->msingle_pages += size >> IOVP_SHIFT;
#endif

	idx = ccio_alloc_range(ioc, (size >> IOVP_SHIFT));
	iovp = (dma_addr_t)MKIOVP(idx);

	pdir_start = &(ioc->pdir_base[idx]);

	DBG_RUN("%s() 0x%p -> 0x%lx size: %0x%x\n",
		__FUNCTION__, addr, (long)iovp | offset, size);

	/* If not cacheline aligned, force SAFE_DMA on the whole mess */
	if((size % L1_CACHE_BYTES) || ((unsigned long)addr % L1_CACHE_BYTES))
		hint |= HINT_SAFE_DMA;

	while(size > 0) {
		ccio_io_pdir_entry(pdir_start, KERNEL_SPACE, addr, hint);

		DBG_RUN(" pdir %p %08x%08x\n",
			pdir_start,
			(u32) (((u32 *) pdir_start)[0]),
			(u32) (((u32 *) pdir_start)[1]));
		++pdir_start;
		addr += IOVP_SIZE;
		size -= IOVP_SIZE;
	}

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	/* form complete address */
	return CCIO_IOVA(iovp, offset);
}

/**
 * ccio_unmap_single - Unmap an address range from the IOMMU.
 * @dev: The PCI device.
 * @addr: The start address of the DMA region.
 * @size: The length of the DMA region.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_unmap_single function.
 */
static void 
ccio_unmap_single(struct device *dev, dma_addr_t iova, size_t size, 
		  enum dma_data_direction direction)
{
	struct ioc *ioc;
	unsigned long flags; 
	dma_addr_t offset = iova & ~IOVP_MASK;
	
	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	DBG_RUN("%s() iovp 0x%lx/%x\n",
		__FUNCTION__, (long)iova, size);

	iova ^= offset;        /* clear offset bits */
	size += offset;
	size = ROUNDUP(size, IOVP_SIZE);

	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CONFIG_PROC_FS
	ioc->usingle_calls++;
	ioc->usingle_pages += size >> IOVP_SHIFT;
#endif

	ccio_mark_invalid(ioc, iova, size);
	ccio_free_range(ioc, iova, (size >> IOVP_SHIFT));
	spin_unlock_irqrestore(&ioc->res_lock, flags);
}

/**
 * ccio_alloc_consistent - Allocate a consistent DMA mapping.
 * @dev: The PCI device.
 * @size: The length of the DMA region.
 * @dma_handle: The DMA address handed back to the device (not the cpu).
 *
 * This function implements the pci_alloc_consistent function.
 */
static void * 
ccio_alloc_consistent(struct device *dev, size_t size, dma_addr_t *dma_handle, int flag)
{
      void *ret;
#if 0
/* GRANT Need to establish hierarchy for non-PCI devs as well
** and then provide matching gsc_map_xxx() functions for them as well.
*/
	if(!hwdev) {
		/* only support PCI */
		*dma_handle = 0;
		return 0;
	}
#endif
        ret = (void *) __get_free_pages(flag, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = ccio_map_single(dev, ret, size, PCI_DMA_BIDIRECTIONAL);
	}

	return ret;
}

/**
 * ccio_free_consistent - Free a consistent DMA mapping.
 * @dev: The PCI device.
 * @size: The length of the DMA region.
 * @cpu_addr: The cpu address returned from the ccio_alloc_consistent.
 * @dma_handle: The device address returned from the ccio_alloc_consistent.
 *
 * This function implements the pci_free_consistent function.
 */
static void 
ccio_free_consistent(struct device *dev, size_t size, void *cpu_addr, 
		     dma_addr_t dma_handle)
{
	ccio_unmap_single(dev, dma_handle, size, 0);
	free_pages((unsigned long)cpu_addr, get_order(size));
}

/*
** Since 0 is a valid pdir_base index value, can't use that
** to determine if a value is valid or not. Use a flag to indicate
** the SG list entry contains a valid pdir index.
*/
#define PIDE_FLAG 0x80000000UL

/**
 * ccio_fill_pdir - Insert coalesced scatter/gather chunks into the I/O Pdir.
 * @ioc: The I/O Controller.
 * @startsg: The scatter/gather list of coalesced chunks.
 * @nents: The number of entries in the scatter/gather list.
 * @hint: The DMA Hint.
 *
 * This function inserts the coalesced scatter/gather list chunks into the
 * I/O Controller's I/O Pdir.
 */ 
static CCIO_INLINE int
ccio_fill_pdir(struct ioc *ioc, struct scatterlist *startsg, int nents, 
	       unsigned long hint)
{
	struct scatterlist *dma_sg = startsg;	/* pointer to current DMA */
	int n_mappings = 0;
	u64 *pdirp = 0;
	unsigned long dma_offset = 0;

	dma_sg--;
	while (nents-- > 0) {
		int cnt = sg_dma_len(startsg);
		sg_dma_len(startsg) = 0;

		DBG_RUN_SG(" %d : %08lx/%05x %08lx/%05x\n", nents,
			   (unsigned long)sg_dma_address(startsg), cnt,
			   sg_virt_addr(startsg), startsg->length
		);

		/*
		** Look for the start of a new DMA stream
		*/
		if(sg_dma_address(startsg) & PIDE_FLAG) {
			u32 pide = sg_dma_address(startsg) & ~PIDE_FLAG;
			dma_offset = (unsigned long) pide & ~IOVP_MASK;
			sg_dma_address(startsg) = 0;
			dma_sg++;
			sg_dma_address(dma_sg) = pide;
			pdirp = &(ioc->pdir_base[pide >> IOVP_SHIFT]);
			n_mappings++;
		}

		/*
		** Look for a VCONTIG chunk
		*/
		if (cnt) {
			unsigned long vaddr = sg_virt_addr(startsg);
			ASSERT(pdirp);

			/* Since multiple Vcontig blocks could make up
			** one DMA stream, *add* cnt to dma_len.
			*/
			sg_dma_len(dma_sg) += cnt;
			cnt += dma_offset;
			dma_offset=0;	/* only want offset on first chunk */
			cnt = ROUNDUP(cnt, IOVP_SIZE);
#ifdef CONFIG_PROC_FS
			ioc->msg_pages += cnt >> IOVP_SHIFT;
#endif
			do {
				ccio_io_pdir_entry(pdirp, KERNEL_SPACE, 
						   (void *)vaddr, hint);
				vaddr += IOVP_SIZE;
				cnt -= IOVP_SIZE;
				pdirp++;
			} while (cnt > 0);
		}
		startsg++;
	}
	return(n_mappings);
}

/*
** First pass is to walk the SG list and determine where the breaks are
** in the DMA stream. Allocates PDIR entries but does not fill them.
** Returns the number of DMA chunks.
**
** Doing the fill separate from the coalescing/allocation keeps the
** code simpler. Future enhancement could make one pass through
** the sglist do both.
*/

static CCIO_INLINE int
ccio_coalesce_chunks(struct ioc *ioc, struct scatterlist *startsg, int nents)
{
	struct scatterlist *vcontig_sg;    /* VCONTIG chunk head */
	unsigned long vcontig_len;         /* len of VCONTIG chunk */
	unsigned long vcontig_end;
	struct scatterlist *dma_sg;        /* next DMA stream head */
	unsigned long dma_offset, dma_len; /* start/len of DMA stream */
	int n_mappings = 0;

	while (nents > 0) {

		/*
		** Prepare for first/next DMA stream
		*/
		dma_sg = vcontig_sg = startsg;
		dma_len = vcontig_len = vcontig_end = startsg->length;
		vcontig_end += sg_virt_addr(startsg);
		dma_offset = sg_virt_addr(startsg) & ~IOVP_MASK;

		/* PARANOID: clear entries */
		sg_dma_address(startsg) = 0;
		sg_dma_len(startsg) = 0;

		/*
		** This loop terminates one iteration "early" since
		** it's always looking one "ahead".
		*/
		while(--nents > 0) {
			unsigned long startsg_end;

			startsg++;
			startsg_end = sg_virt_addr(startsg) + 
				startsg->length;

			/* PARANOID: clear entries */
			sg_dma_address(startsg) = 0;
			sg_dma_len(startsg) = 0;

			/*
			** First make sure current dma stream won't
			** exceed DMA_CHUNK_SIZE if we coalesce the
			** next entry.
			*/   
			if(ROUNDUP(dma_len + dma_offset + startsg->length,
				   IOVP_SIZE) > DMA_CHUNK_SIZE)
				break;

			/*
			** Append the next transaction?
			*/
			if (vcontig_end == sg_virt_addr(startsg)) {
				vcontig_len += startsg->length;
				vcontig_end += startsg->length;
				dma_len     += startsg->length;
				continue;
			}

			/*
			** Not virtually contigous.
			** Terminate prev chunk.
			** Start a new chunk.
			**
			** Once we start a new VCONTIG chunk, dma_offset
			** can't change. And we need the offset from the first
			** chunk - not the last one. Ergo Successive chunks
			** must start on page boundaries and dove tail
			** with its predecessor.
			*/
			sg_dma_len(vcontig_sg) = vcontig_len;

			vcontig_sg = startsg;
			vcontig_len = startsg->length;
			break;
		}

		/*
		** End of DMA Stream
		** Terminate last VCONTIG block.
		** Allocate space for DMA stream.
		*/
		sg_dma_len(vcontig_sg) = vcontig_len;
		dma_len = ROUNDUP(dma_len + dma_offset, IOVP_SIZE);
		sg_dma_address(dma_sg) =
			PIDE_FLAG 
			| (ccio_alloc_range(ioc, (dma_len >> IOVP_SHIFT)) << IOVP_SHIFT)
			| dma_offset;
		n_mappings++;
	}

	return n_mappings;
}

/**
 * ccio_map_sg - Map the scatter/gather list into the IOMMU.
 * @dev: The PCI device.
 * @sglist: The scatter/gather list to be mapped in the IOMMU.
 * @nents: The number of entries in the scatter/gather list.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_map_sg function.
 */
static int
ccio_map_sg(struct device *dev, struct scatterlist *sglist, int nents, 
	    enum dma_data_direction direction)
{
	struct ioc *ioc;
	int coalesced, filled = 0;
	unsigned long flags;
	unsigned long hint = hint_lookup[(int)direction];
	
	BUG_ON(!dev);
	ioc = GET_IOC(dev);
	
	DBG_RUN_SG("%s() START %d entries\n", __FUNCTION__, nents);

	/* Fast path single entry scatterlists. */
	if (nents == 1) {
		sg_dma_address(sglist) = ccio_map_single(dev,
				(void *)sg_virt_addr(sglist), sglist->length,
				direction);
		sg_dma_len(sglist) = sglist->length;
		return 1;
	}
	
	spin_lock_irqsave(&ioc->res_lock, flags);

#ifdef CONFIG_PROC_FS
	ioc->msg_calls++;
#endif

	/*
	** First coalesce the chunks and allocate I/O pdir space
	**
	** If this is one DMA stream, we can properly map using the
	** correct virtual address associated with each DMA page.
	** w/o this association, we wouldn't have coherent DMA!
	** Access to the virtual address is what forces a two pass algorithm.
	*/
	coalesced = ccio_coalesce_chunks(ioc, sglist, nents);

	/*
	** Program the I/O Pdir
	**
	** map the virtual addresses to the I/O Pdir
	** o dma_address will contain the pdir index
	** o dma_len will contain the number of bytes to map 
	** o page/offset contain the virtual address.
	*/
	filled = ccio_fill_pdir(ioc, sglist, nents, hint);

	spin_unlock_irqrestore(&ioc->res_lock, flags);

	ASSERT(coalesced == filled);
	DBG_RUN_SG("%s() DONE %d mappings\n", __FUNCTION__, filled);

	return filled;
}

/**
 * ccio_unmap_sg - Unmap the scatter/gather list from the IOMMU.
 * @dev: The PCI device.
 * @sglist: The scatter/gather list to be unmapped from the IOMMU.
 * @nents: The number of entries in the scatter/gather list.
 * @direction: The direction of the DMA transaction (to/from device).
 *
 * This function implements the pci_unmap_sg function.
 */
static void 
ccio_unmap_sg(struct device *dev, struct scatterlist *sglist, int nents, 
	      enum dma_data_direction direction)
{
	struct ioc *ioc;

	BUG_ON(!dev);
	ioc = GET_IOC(dev);

	DBG_RUN_SG("%s() START %d entries,  %08lx,%x\n",
		__FUNCTION__, nents, sg_virt_addr(sglist), sglist->length);

#ifdef CONFIG_PROC_FS
	ioc->usg_calls++;
#endif

	while(sg_dma_len(sglist) && nents--) {

#ifdef CONFIG_PROC_FS
		ioc->usg_pages += sg_dma_len(sglist) >> PAGE_SHIFT;
#endif
		ccio_unmap_single(dev, sg_dma_address(sglist),
				  sg_dma_len(sglist), direction);
		++sglist;
	}

	DBG_RUN_SG("%s() DONE (nents %d)\n", __FUNCTION__, nents);
}

static struct hppa_dma_ops ccio_ops = {
	.dma_supported =	ccio_dma_supported,
	.alloc_consistent =	ccio_alloc_consistent,
	.alloc_noncoherent =	ccio_alloc_consistent,
	.free_consistent =	ccio_free_consistent,
	.map_single =		ccio_map_single,
	.unmap_single =		ccio_unmap_single,
	.map_sg = 		ccio_map_sg,
	.unmap_sg = 		ccio_unmap_sg,
	.dma_sync_single =	NULL,	/* NOP for U2/Uturn */
	.dma_sync_sg =		NULL,	/* ditto */
};

#ifdef CONFIG_PROC_FS
static int proc_append(char *src, int len, char **dst, off_t *offset, int *max)
{
	if (len < *offset) {
		*offset -= len;
		return 0;
	}
	if (*offset > 0) {
		src += *offset;
		len -= *offset;
		*offset = 0;
	}
	if (len > *max) {
		len = *max;
	}
	memcpy(*dst, src, len);
	*dst += len;
	*max -= len;
	return (*max == 0);
}

static int ccio_proc_info(char *buf, char **start, off_t offset, int count,
			  int *eof, void *data)
{
	int max = count;
	char tmp[80]; /* width of an ANSI-standard terminal */
	struct ioc *ioc = ioc_list;

	while (ioc != NULL) {
		unsigned int total_pages = ioc->res_size << 3;
		unsigned long avg = 0, min, max;
		int j, len;

		len = sprintf(tmp, "%s\n", ioc->name);
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		
		len = sprintf(tmp, "Cujo 2.0 bug    : %s\n",
			      (ioc->cujo20_bug ? "yes" : "no"));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		
		len = sprintf(tmp, "IO PDIR size    : %d bytes (%d entries)\n",
			      total_pages * 8, total_pages);
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		
		len = sprintf(tmp, "IO PDIR entries : %ld free  %ld used (%d%%)\n",
			      total_pages - ioc->used_pages, ioc->used_pages,
			      (int)(ioc->used_pages * 100 / total_pages));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		
		len = sprintf(tmp, "Resource bitmap : %d bytes (%d pages)\n", 
			ioc->res_size, total_pages);
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		
		min = max = ioc->avg_search[0];
		for(j = 0; j < CCIO_SEARCH_SAMPLE; ++j) {
			avg += ioc->avg_search[j];
			if(ioc->avg_search[j] > max) 
				max = ioc->avg_search[j];
			if(ioc->avg_search[j] < min) 
				min = ioc->avg_search[j];
		}
		avg /= CCIO_SEARCH_SAMPLE;
		len = sprintf(tmp, "  Bitmap search : %ld/%ld/%ld (min/avg/max CPU Cycles)\n",
			      min, avg, max);
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;

		len = sprintf(tmp, "pci_map_single(): %8ld calls  %8ld pages (avg %d/1000)\n",
			      ioc->msingle_calls, ioc->msingle_pages,
			      (int)((ioc->msingle_pages * 1000)/ioc->msingle_calls));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		

		/* KLUGE - unmap_sg calls unmap_single for each mapped page */
		min = ioc->usingle_calls - ioc->usg_calls;
		max = ioc->usingle_pages - ioc->usg_pages;
		len = sprintf(tmp, "pci_unmap_single: %8ld calls  %8ld pages (avg %d/1000)\n",
			      min, max, (int)((max * 1000)/min));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
 
		len = sprintf(tmp, "pci_map_sg()    : %8ld calls  %8ld pages (avg %d/1000)\n",
			      ioc->msg_calls, ioc->msg_pages,
			      (int)((ioc->msg_pages * 1000)/ioc->msg_calls));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;
		len = sprintf(tmp, "pci_unmap_sg()  : %8ld calls  %8ld pages (avg %d/1000)\n\n\n",
			      ioc->usg_calls, ioc->usg_pages,
			      (int)((ioc->usg_pages * 1000)/ioc->usg_calls));
		if (proc_append(tmp, len, &buf, &offset, &count))
			break;

		ioc = ioc->next;
	}

	if (count == 0) {
		*eof = 1;
	}
	return (max - count);
}

static int ccio_resource_map(char *buf, char **start, off_t offset, int len,
			     int *eof, void *data)
{
	struct ioc *ioc = ioc_list;

	buf[0] = '\0';
	while (ioc != NULL) {
		u32 *res_ptr = (u32 *)ioc->res_map;
		int j;

		for (j = 0; j < (ioc->res_size / sizeof(u32)); j++) {
			if ((j & 7) == 0)
				strcat(buf,"\n   ");
			sprintf(buf, "%s %08x", buf, *res_ptr);
			res_ptr++;
		}
		strcat(buf, "\n\n");
		ioc = ioc->next;
		break; /* XXX - remove me */
	}

	return strlen(buf);
}
#endif

/**
 * ccio_find_ioc - Find the ioc in the ioc_list
 * @hw_path: The hardware path of the ioc.
 *
 * This function searches the ioc_list for an ioc that matches
 * the provide hardware path.
 */
static struct ioc * ccio_find_ioc(int hw_path)
{
	int i;
	struct ioc *ioc;

	ioc = ioc_list;
	for (i = 0; i < ioc_count; i++) {
		if (ioc->hw_path == hw_path)
			return ioc;

		ioc = ioc->next;
	}

	return NULL;
}

/**
 * ccio_get_iommu - Find the iommu which controls this device
 * @dev: The parisc device.
 *
 * This function searches through the registerd IOMMU's and returns the
 * appropriate IOMMU for the device based upon the devices hardware path.
 */
void * ccio_get_iommu(const struct parisc_device *dev)
{
	dev = find_pa_parent_type(dev, HPHW_IOA);
	if (!dev)
		return NULL;

	return ccio_find_ioc(dev->hw_path);
}

#define CUJO_20_STEP       0x10000000	/* inc upper nibble */

/* Cujo 2.0 has a bug which will silently corrupt data being transferred
 * to/from certain pages.  To avoid this happening, we mark these pages
 * as `used', and ensure that nothing will try to allocate from them.
 */
void ccio_cujo20_fixup(struct parisc_device *dev, u32 iovp)
{
	unsigned int idx;
	struct ioc *ioc = ccio_get_iommu(dev);
	u8 *res_ptr;

#ifdef CONFIG_PROC_FS
	ioc->cujo20_bug = 1;
#endif
	res_ptr = ioc->res_map;
	idx = PDIR_INDEX(iovp) >> 3;

	while (idx < ioc->res_size) {
 		res_ptr[idx] |= 0xff;
		idx += PDIR_INDEX(CUJO_20_STEP) >> 3;
	}
}

#if 0
/* GRANT -  is this needed for U2 or not? */

/*
** Get the size of the I/O TLB for this I/O MMU.
**
** If spa_shift is non-zero (ie probably U2),
** then calculate the I/O TLB size using spa_shift.
**
** Otherwise we are supposed to get the IODC entry point ENTRY TLB
** and execute it. However, both U2 and Uturn firmware supplies spa_shift.
** I think only Java (K/D/R-class too?) systems don't do this.
*/
static int
ccio_get_iotlb_size(struct parisc_device *dev)
{
	if (dev->spa_shift == 0) {
		panic("%s() : Can't determine I/O TLB size.\n", __FUNCTION__);
	}
	return (1 << dev->spa_shift);
}
#else

/* Uturn supports 256 TLB entries */
#define CCIO_CHAINID_SHIFT	8
#define CCIO_CHAINID_MASK	0xff
#endif /* 0 */

/**
 * ccio_ioc_init - Initalize the I/O Controller
 * @ioc: The I/O Controller.
 *
 * Initalize the I/O Controller which includes setting up the
 * I/O Page Directory, the resource map, and initalizing the
 * U2/Uturn chip into virtual mode.
 */
static void
ccio_ioc_init(struct ioc *ioc)
{
	int i, iov_order;
	u32 iova_space_size;
	unsigned long physmem;

	/*
	** Determine IOVA Space size from memory size.
	**
	** Ideally, PCI drivers would register the maximum number
	** of DMA they can have outstanding for each device they
	** own.  Next best thing would be to guess how much DMA
	** can be outstanding based on PCI Class/sub-class. Both
	** methods still require some "extra" to support PCI
	** Hot-Plug/Removal of PCI cards. (aka PCI OLARD).
	*/

	/* limit IOVA space size to 1MB-1GB */

	physmem = num_physpages << PAGE_SHIFT;
	if(physmem < (ccio_mem_ratio * 1024 * 1024)) {
		iova_space_size = 1024 * 1024;
#ifdef __LP64__
	} else if(physmem > (ccio_mem_ratio * 512 * 1024 * 1024)) {
		iova_space_size = 512 * 1024 * 1024;
#endif
	} else {
		iova_space_size = (u32)(physmem / ccio_mem_ratio);
	}

	/*
	** iova space must be log2() in size.
	** thus, pdir/res_map will also be log2().
	*/

	/* We could use larger page sizes in order to *decrease* the number
	** of mappings needed.  (ie 8k pages means 1/2 the mappings).
	**
	** Note: Grant Grunder says "Using 8k I/O pages isn't trivial either
	**   since the pages must also be physically contiguous - typically
	**   this is the case under linux."
	*/

	iov_order = get_order(iova_space_size) >> (IOVP_SHIFT - PAGE_SHIFT);
	ASSERT(iov_order <= (30 - IOVP_SHIFT));   /* iova_space_size <= 1GB */
	ASSERT(iov_order >= (20 - IOVP_SHIFT));   /* iova_space_size >= 1MB */
	iova_space_size = 1 << (iov_order + IOVP_SHIFT);

	ioc->pdir_size = (iova_space_size / IOVP_SIZE) * sizeof(u64);

	ASSERT(ioc->pdir_size < 4 * 1024 * 1024);   /* max pdir size < 4MB */

	/* Verify it's a power of two */
	ASSERT((1 << get_order(ioc->pdir_size)) == (ioc->pdir_size >> PAGE_SHIFT));

	DBG_INIT("%s() hpa 0x%p mem %luMB IOV %dMB (%d bits) PDIR size 0x%0x",
		__FUNCTION__, ioc->ioc_hpa, physmem>>20, iova_space_size>>20,
		 iov_order + PAGE_SHIFT, ioc->pdir_size);

	ioc->pdir_base = (u64 *)__get_free_pages(GFP_KERNEL, 
						 get_order(ioc->pdir_size));
	if(NULL == ioc->pdir_base) {
		panic(__FILE__ ":%s() could not allocate I/O Page Table\n", __FUNCTION__);
	}
	memset(ioc->pdir_base, 0, ioc->pdir_size);

	ASSERT((((unsigned long)ioc->pdir_base) & PAGE_MASK) == (unsigned long)ioc->pdir_base);
	DBG_INIT(" base %p", ioc->pdir_base);

	/* resource map size dictated by pdir_size */
 	ioc->res_size = (ioc->pdir_size / sizeof(u64)) >> 3;
	DBG_INIT("%s() res_size 0x%x\n", __FUNCTION__, ioc->res_size);
	
	ioc->res_map = (u8 *)__get_free_pages(GFP_KERNEL, 
					      get_order(ioc->res_size));
	if(NULL == ioc->res_map) {
		panic(__FILE__ ":%s() could not allocate resource map\n", __FUNCTION__);
	}
	memset(ioc->res_map, 0, ioc->res_size);

	/* Initialize the res_hint to 16 */
	ioc->res_hint = 16;

	/* Initialize the spinlock */
	spin_lock_init(&ioc->res_lock);

	/*
	** Chainid is the upper most bits of an IOVP used to determine
	** which TLB entry an IOVP will use.
	*/
	ioc->chainid_shift = get_order(iova_space_size) + PAGE_SHIFT - CCIO_CHAINID_SHIFT;
	DBG_INIT(" chainid_shift 0x%x\n", ioc->chainid_shift);

	/*
	** Initialize IOA hardware
	*/
	WRITE_U32(CCIO_CHAINID_MASK << ioc->chainid_shift, 
		  &ioc->ioc_hpa->io_chain_id_mask);

	WRITE_U32(virt_to_phys(ioc->pdir_base), 
		  &ioc->ioc_hpa->io_pdir_base);

	/*
	** Go to "Virtual Mode"
	*/
	WRITE_U32(IOA_NORMAL_MODE, &ioc->ioc_hpa->io_control);

	/*
	** Initialize all I/O TLB entries to 0 (Valid bit off).
	*/
	WRITE_U32(0, &ioc->ioc_hpa->io_tlb_entry_m);
	WRITE_U32(0, &ioc->ioc_hpa->io_tlb_entry_l);

	for(i = 1 << CCIO_CHAINID_SHIFT; i ; i--) {
		WRITE_U32((CMD_TLB_DIRECT_WRITE | (i << ioc->chainid_shift)),
			  &ioc->ioc_hpa->io_command);
	}
}

static void
ccio_init_resource(struct resource *res, char *name, unsigned long ioaddr)
{
	int result;

	res->flags = IORESOURCE_MEM;
	res->start = (unsigned long)(signed) __raw_readl(ioaddr) << 16;
	res->end = (unsigned long)(signed) (__raw_readl(ioaddr + 4) << 16) - 1;
	if (res->end + 1 == res->start)
		return;
	res->name = name;
	result = request_resource(&iomem_resource, res);
	if (result < 0) {
		printk(KERN_ERR "%s: failed to claim CCIO bus address space (%08lx,%08lx)\n", 
		       __FILE__, res->start, res->end);
	}
}

static void __init ccio_init_resources(struct ioc *ioc)
{
	struct resource *res = ioc->mmio_region;
	char *name = kmalloc(14, GFP_KERNEL);

	sprintf(name, "GSC Bus [%d/]", ioc->hw_path);

	ccio_init_resource(res, name, (unsigned long)&ioc->ioc_hpa->io_io_low);
	ccio_init_resource(res + 1, name,
			(unsigned long)&ioc->ioc_hpa->io_io_low_hv);
}

static void expand_ioc_area(struct ioc *ioc, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align)
{
#ifdef NASTY_HACK_FOR_K_CLASS
	__raw_writel(0xfffff600, (unsigned long)&(ioc->ioc_hpa->io_io_high));
	ioc->mmio_region[0].end = 0xf5ffffff;
#endif
}

static struct resource *ccio_get_resource(struct ioc* ioc,
		const struct parisc_device *dev)
{
	if (!ioc) {
		return &iomem_resource;
	} else if ((ioc->mmio_region->start <= dev->hpa) &&
			(dev->hpa < ioc->mmio_region->end)) {
		return ioc->mmio_region;
	} else if (((ioc->mmio_region + 1)->start <= dev->hpa) &&
			(dev->hpa < (ioc->mmio_region + 1)->end)) {
		return ioc->mmio_region + 1;
	} else {
		return NULL;
	}
}

int ccio_allocate_resource(const struct parisc_device *dev,
		struct resource *res, unsigned long size,
		unsigned long min, unsigned long max, unsigned long align,
		void (*alignf)(void *, struct resource *, unsigned long, unsigned long),
		void *alignf_data)
{
	struct ioc *ioc = ccio_get_iommu(dev);
	struct resource *parent = ccio_get_resource(ioc, dev);
	if (!parent)
		return -EBUSY;

	if (!allocate_resource(parent, res, size, min, max, align, alignf,
			alignf_data))
		return 0;

	expand_ioc_area(ioc, size, min, max, align);
	return allocate_resource(parent, res, size, min, max, align, alignf,
			alignf_data);
}

int ccio_request_resource(const struct parisc_device *dev,
		struct resource *res)
{
	struct ioc *ioc = ccio_get_iommu(dev);
	struct resource *parent = ccio_get_resource(ioc, dev);

	return request_resource(parent, res);
}
/**
 * ccio_probe - Determine if ccio should claim this device.
 * @dev: The device which has been found
 *
 * Determine if ccio should claim this chip (return 0) or not (return 1).
 * If so, initialize the chip and tell other partners in crime they
 * have work to do.
 */
static int ccio_probe(struct parisc_device *dev)
{
	int i;
	struct ioc *ioc, **ioc_p = &ioc_list;
	
	ioc = kmalloc(sizeof(struct ioc), GFP_KERNEL);
	if (ioc == NULL) {
		printk(KERN_ERR MODULE_NAME ": memory allocation failure\n");
		return 1;
	}
	memset(ioc, 0, sizeof(struct ioc));

	ioc->name = dev->id.hversion == U2_IOA_RUNWAY ? "U2" : "UTurn";
	strlcpy(dev->dev.name, ioc->name, sizeof(dev->dev.name));

	printk(KERN_INFO "Found %s at 0x%lx\n", ioc->name, dev->hpa);

	for (i = 0; i < ioc_count; i++) {
		ioc_p = &(*ioc_p)->next;
	}
	*ioc_p = ioc;

	ioc->hw_path = dev->hw_path;
	ioc->ioc_hpa = (struct ioa_registers *)dev->hpa;
	ccio_ioc_init(ioc);
	ccio_init_resources(ioc);
	hppa_dma_ops = &ccio_ops;
	dev->dev.platform_data = kmalloc(sizeof(struct pci_hba_data), GFP_KERNEL);

	/* if this fails, no I/O cards will work, so may as well bug */
	BUG_ON(dev->dev.platform_data == NULL);
	HBA_DATA(dev->dev.platform_data)->iommu = ioc;
	

	if (ioc_count == 0) {
		/* XXX: Create separate entries for each ioc */
		create_proc_read_entry(MODULE_NAME, S_IRWXU, proc_runway_root,
				       ccio_proc_info, NULL);
		create_proc_read_entry(MODULE_NAME"-bitmap", S_IRWXU,
				       proc_runway_root, ccio_resource_map, NULL);
	}

	ioc_count++;
	return 0;
}

/* We *can't* support JAVA (T600). Venture there at your own risk. */
static struct parisc_device_id ccio_tbl[] = {
	{ HPHW_IOA, HVERSION_REV_ANY_ID, U2_IOA_RUNWAY, 0xb }, /* U2 */
	{ HPHW_IOA, HVERSION_REV_ANY_ID, UTURN_IOA_RUNWAY, 0xb }, /* UTurn */
	{ 0, }
};

static struct parisc_driver ccio_driver = {
	.name =		"U2:Uturn",
	.id_table =	ccio_tbl,
	.probe =	ccio_probe,
};

/**
 * ccio_init - ccio initalization procedure.
 *
 * Register this driver.
 */
void __init ccio_init(void)
{
	register_parisc_driver(&ccio_driver);
}

