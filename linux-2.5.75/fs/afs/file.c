/* file.c: AFS filesystem file handling
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include <rxrpc/call.h>
#include "internal.h"

#if 0
static int afs_file_open(struct inode *inode, struct file *file);
static int afs_file_release(struct inode *inode, struct file *file);
#endif

static int afs_file_readpage(struct file *file, struct page *page);

static ssize_t afs_file_write(struct file *file, const char *buf, size_t size, loff_t *off);

struct inode_operations afs_file_inode_operations = {
	.getattr	= afs_inode_getattr,
};

struct file_operations afs_file_file_operations = {
	.read		= generic_file_read,
	.write		= afs_file_write,
	.mmap		= generic_file_readonly_mmap,
#if 0
	.open		= afs_file_open,
	.release	= afs_file_release,
	.fsync		= afs_file_fsync,
#endif
};

struct address_space_operations afs_fs_aops = {
	.readpage	= afs_file_readpage,
};

/*****************************************************************************/
/*
 * AFS file write
 */
static ssize_t afs_file_write(struct file *file, const char *buf, size_t size, loff_t *off)
{
	afs_vnode_t *vnode;

	vnode = AFS_FS_I(file->f_dentry->d_inode);
	if (vnode->flags & AFS_VNODE_DELETED)
		return -ESTALE;

	return -EIO;
} /* end afs_file_write() */

/*****************************************************************************/
/*
 * AFS read page from file (or symlink)
 */
static int afs_file_readpage(struct file *file, struct page *page)
{
	struct afs_rxfs_fetch_descriptor desc;
	struct inode *inode;
	afs_vnode_t *vnode;
	int ret;

	inode = page->mapping->host;

	_enter("{%lu},{%lu}",inode->i_ino,page->index);

	vnode = AFS_FS_I(inode);

	if (!PageLocked(page))
		PAGE_BUG(page);

	ret = -ESTALE;
	if (vnode->flags & AFS_VNODE_DELETED)
		goto error;

	/* work out how much to get and from where */
	desc.fid	= vnode->fid;
	desc.offset	= page->index << PAGE_CACHE_SHIFT;
	desc.size	= min((size_t)(inode->i_size - desc.offset),(size_t)PAGE_SIZE);
	desc.buffer	= kmap(page);

	clear_page(desc.buffer);

	/* read the contents of the file from the server into the page */
	ret = afs_vnode_fetch_data(vnode,&desc);
	kunmap(page);
	if (ret<0) {
		if (ret==-ENOENT) {
			_debug("got NOENT from server - marking file deleted and stale");
			vnode->flags |= AFS_VNODE_DELETED;
			ret = -ESTALE;
		}
		goto error;
	}

	SetPageUptodate(page);
	unlock_page(page);

	_leave(" = 0");
	return 0;

 error:
	SetPageError(page);
	unlock_page(page);

	_leave(" = %d",ret);
	return ret;

} /* end afs_file_readpage() */
