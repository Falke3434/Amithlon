/*
 * linux/include/linux/nfs_page.h
 *
 * Copyright (C) 2000 Trond Myklebust
 *
 * NFS page cache wrapper.
 */

#ifndef _LINUX_NFS_PAGE_H
#define _LINUX_NFS_PAGE_H


#include <linux/list.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/nfs_fs_sb.h>
#include <linux/sunrpc/auth.h>
#include <linux/nfs_xdr.h>

/*
 * Valid flags for a dirty buffer
 */
#define PG_BUSY			0

struct nfs_page {
	struct list_head	wb_list,	/* Defines state of page: */
				*wb_list_head;	/*      read/write/commit */
	struct file		*wb_file;
	struct inode		*wb_inode;
	struct rpc_cred		*wb_cred;
	struct page		*wb_page;	/* page to read in/write out */
	wait_queue_head_t	wb_wait;	/* wait queue */
	unsigned long		wb_index;	/* Offset >> PAGE_CACHE_SHIFT */
	unsigned int		wb_offset,	/* Offset & ~PAGE_CACHE_MASK */
				wb_pgbase,	/* Start of page data */
				wb_bytes,	/* Length of request */
				wb_count;	/* reference count */
	unsigned long		wb_flags;
	struct nfs_writeverf	wb_verf;	/* Commit cookie */
};

#define NFS_WBACK_BUSY(req)	(test_bit(PG_BUSY,&(req)->wb_flags))

extern	struct nfs_page *nfs_create_request(struct rpc_cred *, struct inode *,
					    struct page *,
					    unsigned int, unsigned int);
extern	void nfs_clear_request(struct nfs_page *req);
extern	void nfs_release_request(struct nfs_page *req);
extern	void nfs_release_list(struct list_head *list);


extern	void nfs_list_add_request(struct nfs_page *, struct list_head *);

extern	int nfs_scan_list(struct list_head *, struct list_head *,
			  struct file *, unsigned long, unsigned int);
extern	int nfs_coalesce_requests(struct list_head *, struct list_head *,
				  unsigned int);
extern  int nfs_wait_on_request(struct nfs_page *);
extern	int nfs_wait_for_reads(struct list_head *);

extern	spinlock_t nfs_wreq_lock;

/*
 * Lock the page of an asynchronous request without incrementing the wb_count
 */
static inline int
nfs_lock_request_dontget(struct nfs_page *req)
{
	if (test_and_set_bit(PG_BUSY, &req->wb_flags))
		return 0;
	return 1;
}

/*
 * Lock the page of an asynchronous request
 */
static inline int
nfs_lock_request(struct nfs_page *req)
{
	if (test_and_set_bit(PG_BUSY, &req->wb_flags))
		return 0;
	req->wb_count++;
	return 1;
}

static inline void
nfs_unlock_request(struct nfs_page *req)
{
	if (!NFS_WBACK_BUSY(req)) {
		printk(KERN_ERR "NFS: Invalid unlock attempted\n");
		BUG();
	}
	smp_mb__before_clear_bit();
	clear_bit(PG_BUSY, &req->wb_flags);
	smp_mb__after_clear_bit();
	if (waitqueue_active(&req->wb_wait))
		wake_up_all(&req->wb_wait);
	nfs_release_request(req);
}

/**
 * nfs_list_remove_request - Remove a request from its wb_list
 * @req: request
 */
static inline void
nfs_list_remove_request(struct nfs_page *req)
{
	if (list_empty(&req->wb_list))
		return;
	if (!NFS_WBACK_BUSY(req)) {
		printk(KERN_ERR "NFS: unlocked request attempted removed from list!\n");
		BUG();
	}
	list_del_init(&req->wb_list);
	req->wb_list_head = NULL;
}

static inline struct nfs_page *
nfs_list_entry(struct list_head *head)
{
	return list_entry(head, struct nfs_page, wb_list);
}

#endif /* _LINUX_NFS_PAGE_H */
