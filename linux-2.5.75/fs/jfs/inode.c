/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_extent.h"
#include "jfs_unicode.h"
#include "jfs_debug.h"


extern struct inode_operations jfs_dir_inode_operations;
extern struct inode_operations jfs_file_inode_operations;
extern struct inode_operations jfs_symlink_inode_operations;
extern struct file_operations jfs_dir_operations;
extern struct file_operations jfs_file_operations;
struct address_space_operations jfs_aops;
extern int freeZeroLink(struct inode *);

void jfs_read_inode(struct inode *inode)
{
	if (diRead(inode)) { 
		make_bad_inode(inode);
		return;
	}

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &jfs_file_inode_operations;
		inode->i_fop = &jfs_file_operations;
		inode->i_mapping->a_ops = &jfs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &jfs_dir_inode_operations;
		inode->i_fop = &jfs_dir_operations;
		inode->i_mapping->a_ops = &jfs_aops;
		inode->i_mapping->gfp_mask = GFP_NOFS;
	} else if (S_ISLNK(inode->i_mode)) {
		if (inode->i_size >= IDATASIZE) {
			inode->i_op = &page_symlink_inode_operations;
			inode->i_mapping->a_ops = &jfs_aops;
		} else
			inode->i_op = &jfs_symlink_inode_operations;
	} else {
		inode->i_op = &jfs_file_inode_operations;
		init_special_inode(inode, inode->i_mode,
				   kdev_t_to_nr(inode->i_rdev));
	}
}

/* This define is from fs/open.c */
#define special_file(m) (S_ISCHR(m)||S_ISBLK(m)||S_ISFIFO(m)||S_ISSOCK(m))

/*
 * Workhorse of both fsync & write_inode
 */
int jfs_commit_inode(struct inode *inode, int wait)
{
	int rc = 0;
	tid_t tid;
	static int noisy = 5;

	jfs_info("In jfs_commit_inode, inode = 0x%p", inode);

	/*
	 * Don't commit if inode has been committed since last being
	 * marked dirty, or if it has been deleted.
	 */
	if (test_cflag(COMMIT_Nolink, inode) ||
	    !test_cflag(COMMIT_Dirty, inode))
		return 0;

	if (isReadOnly(inode)) {
		/* kernel allows writes to devices on read-only
		 * partitions and may think inode is dirty
		 */
		if (!special_file(inode->i_mode) && noisy) {
			jfs_err("jfs_commit_inode(0x%p) called on "
				   "read-only volume", inode);
			jfs_err("Is remount racy?");
			noisy--;
		}
		return 0;
	}

	tid = txBegin(inode->i_sb, COMMIT_INODE);
	down(&JFS_IP(inode)->commit_sem);
	rc = txCommit(tid, 1, &inode, wait ? COMMIT_SYNC : 0);
	txEnd(tid);
	up(&JFS_IP(inode)->commit_sem);
	return -rc;
}

void jfs_write_inode(struct inode *inode, int wait)
{
	/*
	 * If COMMIT_DIRTY is not set, the inode isn't really dirty.
	 * It has been committed since the last change, but was still
	 * on the dirty inode list
	 */
	if (test_cflag(COMMIT_Nolink, inode) ||
	    !test_cflag(COMMIT_Dirty, inode))
		return;

	if (jfs_commit_inode(inode, wait)) {
		jfs_err("jfs_write_inode: jfs_commit_inode failed!");
	}
}

void jfs_delete_inode(struct inode *inode)
{
	jfs_info("In jfs_delete_inode, inode = 0x%p", inode);

	if (test_cflag(COMMIT_Freewmap, inode))
		freeZeroLink(inode);

	diFree(inode);

	clear_inode(inode);
}

void jfs_dirty_inode(struct inode *inode)
{
	static int noisy = 5;

	if (isReadOnly(inode)) {
		if (!special_file(inode->i_mode) && noisy) {
			/* kernel allows writes to devices on read-only
			 * partitions and may try to mark inode dirty
			 */
			jfs_err("jfs_dirty_inode called on read-only volume");
			jfs_err("Is remount racy?");
			noisy--;
		}
		return;
	}

	set_cflag(COMMIT_Dirty, inode);
}

static int
jfs_get_blocks(struct inode *ip, sector_t lblock, unsigned long max_blocks,
			struct buffer_head *bh_result, int create)
{
	s64 lblock64 = lblock;
	int no_size_check = 0;
	int rc = 0;
	int take_locks;
	xad_t xad;
	s64 xaddr;
	int xflag;
	s32 xlen;

	/*
	 * If this is a special inode (imap, dmap) or directory,
	 * the lock should already be taken
	 */
	take_locks = ((JFS_IP(ip)->fileset != AGGREGATE_I) &&
		      !S_ISDIR(ip->i_mode));
	/*
	 * Take appropriate lock on inode
	 */
	if (take_locks) {
		if (create)
			IWRITE_LOCK(ip);
		else
			IREAD_LOCK(ip);
	}

	/*
	 * A directory's "data" is the inode index table, but i_size is the
	 * size of the d-tree, so don't check the offset against i_size
	 */
	if (S_ISDIR(ip->i_mode))
		no_size_check = 1;

	if ((no_size_check ||
	     ((lblock64 << ip->i_sb->s_blocksize_bits) < ip->i_size)) &&
	    (xtLookup(ip, lblock64, max_blocks, &xflag, &xaddr, &xlen, no_size_check)
	     == 0) && xlen) {
		if (xflag & XAD_NOTRECORDED) {
			if (!create)
				/*
				 * Allocated but not recorded, read treats
				 * this as a hole
				 */
				goto unlock;
#ifdef _JFS_4K
			XADoffset(&xad, lblock64);
			XADlength(&xad, xlen);
			XADaddress(&xad, xaddr);
#else				/* _JFS_4K */
			/*
			 * As long as block size = 4K, this isn't a problem.
			 * We should mark the whole page not ABNR, but how
			 * will we know to mark the other blocks BH_New?
			 */
			BUG();
#endif				/* _JFS_4K */
			rc = extRecord(ip, &xad);
			if (rc)
				goto unlock;
			set_buffer_new(bh_result);
		}

		map_bh(bh_result, ip->i_sb, xaddr);
		bh_result->b_size = xlen << ip->i_blkbits;
		goto unlock;
	}
	if (!create)
		goto unlock;

	/*
	 * Allocate a new block
	 */
#ifdef _JFS_4K
	if ((rc = extHint(ip, lblock64 << ip->i_sb->s_blocksize_bits, &xad)))
		goto unlock;
	rc = extAlloc(ip, max_blocks, lblock64, &xad, FALSE);
	if (rc)
		goto unlock;

	set_buffer_new(bh_result);
	map_bh(bh_result, ip->i_sb, addressXAD(&xad));
	bh_result->b_size = lengthXAD(&xad) << ip->i_blkbits;

#else				/* _JFS_4K */
	/*
	 * We need to do whatever it takes to keep all but the last buffers
	 * in 4K pages - see jfs_write.c
	 */
	BUG();
#endif				/* _JFS_4K */

      unlock:
	/*
	 * Release lock on inode
	 */
	if (take_locks) {
		if (create)
			IWRITE_UNLOCK(ip);
		else
			IREAD_UNLOCK(ip);
	}
	return rc;
}

static int jfs_get_block(struct inode *ip, sector_t lblock,
			 struct buffer_head *bh_result, int create)
{
	return jfs_get_blocks(ip, lblock, 1, bh_result, create);
}

static int jfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, jfs_get_block, wbc);
}

static int jfs_writepages(struct address_space *mapping,
			struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, jfs_get_block);
}

static int jfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, jfs_get_block);
}

static int jfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, jfs_get_block);
}

static int jfs_prepare_write(struct file *file,
			     struct page *page, unsigned from, unsigned to)
{
	return nobh_prepare_write(page, from, to, jfs_get_block);
}

static sector_t jfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, jfs_get_block);
}

static int jfs_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_dentry->d_inode->i_mapping->host;

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				offset, nr_segs, jfs_get_blocks);
}

struct address_space_operations jfs_aops = {
	.readpage	= jfs_readpage,
	.readpages	= jfs_readpages,
	.writepage	= jfs_writepage,
	.writepages	= jfs_writepages,
	.sync_page	= block_sync_page,
	.prepare_write	= jfs_prepare_write,
	.commit_write	= nobh_commit_write,
	.bmap		= jfs_bmap,
	.direct_IO	= jfs_direct_IO,
};

/*
 * Guts of jfs_truncate.  Called with locks already held.  Can be called
 * with directory for truncating directory index table.
 */
void jfs_truncate_nolock(struct inode *ip, loff_t length)
{
	loff_t newsize;
	tid_t tid;

	ASSERT(length >= 0);

	if (test_cflag(COMMIT_Nolink, ip)) {
		xtTruncate(0, ip, length, COMMIT_WMAP);
		return;
	}

	do {
		tid = txBegin(ip->i_sb, 0);

		/*
		 * The commit_sem cannot be taken before txBegin.
		 * txBegin may block and there is a chance the inode
		 * could be marked dirty and need to be committed
		 * before txBegin unblocks
		 */
		down(&JFS_IP(ip)->commit_sem);

		newsize = xtTruncate(tid, ip, length,
				     COMMIT_TRUNCATE | COMMIT_PWMAP);
		if (newsize < 0) {
			txEnd(tid);
			up(&JFS_IP(ip)->commit_sem);
			break;
		}

		ip->i_mtime = ip->i_ctime = CURRENT_TIME;
		mark_inode_dirty(ip);

		txCommit(tid, 1, &ip, 0);
		txEnd(tid);
		up(&JFS_IP(ip)->commit_sem);
	} while (newsize > length);	/* Truncate isn't always atomic */
}

void jfs_truncate(struct inode *ip)
{
	jfs_info("jfs_truncate: size = 0x%lx", (ulong) ip->i_size);

	nobh_truncate_page(ip->i_mapping, ip->i_size);

	IWRITE_LOCK(ip);
	jfs_truncate_nolock(ip, ip->i_size);
	IWRITE_UNLOCK(ip);
}
