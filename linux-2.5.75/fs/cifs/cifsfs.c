/*
 *   fs/cifs/cifsfs.c
 *
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Common Internet FileSystem (CIFS) client
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* Note that BB means BUGBUG (ie something to fix eventually) */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/vfs.h>
#include "cifsfs.h"
#include "cifspdu.h"
#define DECLARE_GLOBALS_HERE
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include <linux/mm.h>
#define CIFS_MAGIC_NUMBER 0xFF534D42	/* the first four bytes of all SMB PDUs */

extern struct file_system_type cifs_fs_type;

int cifsFYI = 0;
int cifsERROR = 1;
int traceSMB = 0;
unsigned int oplockEnabled = 0;
unsigned int lookupCacheEnabled = 1;
unsigned int multiuser_mount = 0;
unsigned int extended_security = 0;
unsigned int ntlmv2_support = 0;
unsigned int sign_CIFS_PDUs = 0;
unsigned int CIFSMaximumBufferSize = CIFS_MAX_MSGSIZE;
struct task_struct * oplockThread = NULL;

extern int cifs_mount(struct super_block *, struct cifs_sb_info *, char *,
			const char *);
extern int cifs_umount(struct super_block *, struct cifs_sb_info *);
void cifs_proc_init(void);
void cifs_proc_clean(void);

static DECLARE_COMPLETION(cifs_oplock_exited);


static int
cifs_read_super(struct super_block *sb, void *data,
		const char *devname, int silent)
{
	struct inode *inode;
	struct cifs_sb_info *cifs_sb;
	int rc = 0;

	sb->s_fs_info = kmalloc(sizeof(struct cifs_sb_info),GFP_KERNEL);
	cifs_sb = CIFS_SB(sb);
	if(cifs_sb == NULL)
		return -ENOMEM;
	cifs_sb->local_nls = load_nls_default();	/* needed for ASCII cp to Unicode converts */

	rc = cifs_mount(sb, cifs_sb, data, devname);

	if (rc) {
		if (!silent)
			cERROR(1,
			       ("cifs_mount failed w/return code = %d", rc));
		goto out_mount_failed;
	}

	sb->s_magic = CIFS_MAGIC_NUMBER;
	sb->s_op = &cifs_super_ops;
	if(cifs_sb->tcon->ses->server->maxBuf > MAX_CIFS_HDR_SIZE + 512)
	    sb->s_blocksize = cifs_sb->tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE;
	else
		sb->s_blocksize = CIFSMaximumBufferSize;
	sb->s_blocksize_bits = 14;	/* default 2**14 = CIFS_MAX_MSGSIZE */
	inode = iget(sb, ROOT_I);

	if (!inode) {
		rc = -ENOMEM;
		goto out_no_root;
	}

	sb->s_root = d_alloc_root(inode);

	if (!sb->s_root) {
		rc = -ENOMEM;
		goto out_no_root;
	}

	return 0;

out_no_root:
	cERROR(1, ("cifs_read_super: get root inode failed"));
	if (inode)
		iput(inode);

out_mount_failed:
	if(cifs_sb->local_nls)
		unload_nls(cifs_sb->local_nls);	
	if(cifs_sb)
		kfree(cifs_sb);
	return rc;
}

void
cifs_put_super(struct super_block *sb)
{
	int rc = 0;
	struct cifs_sb_info *cifs_sb;

	cFYI(1, ("In cifs_put_super"));
	cifs_sb = CIFS_SB(sb);
	if(cifs_sb == NULL) {
		cFYI(1,("Empty cifs superblock info passed to unmount"));
		return;
	}
	rc = cifs_umount(sb, cifs_sb); 
	if (rc) {
		cERROR(1, ("cifs_umount failed with return code %d", rc));
	}
	unload_nls(cifs_sb->local_nls);
	kfree(cifs_sb);
	return;
}

int
cifs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	int xid, rc;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	xid = GetXid();

	cifs_sb = CIFS_SB(sb);
	pTcon = cifs_sb->tcon;

	buf->f_type = CIFS_MAGIC_NUMBER;

	/* instead could get the real value via SMB_QUERY_FS_ATTRIBUTE_INFO */
	buf->f_namelen = PATH_MAX;	/* PATH_MAX may be too long - it would presumably
					   be length of total path, note that some servers may be 
					   able to support more than this, but best to be safe
					   since Win2k and others can not handle very long filenames */
	buf->f_files = 0;	/* undefined */
	buf->f_ffree = 0;	/* unlimited */

	rc = CIFSSMBQFSInfo(xid, pTcon, buf, cifs_sb->local_nls);

	/*     
	   int f_type;
	   __fsid_t f_fsid;
	   int f_namelen;  */
	/* BB get from info put in tcon struct at mount time with call to QFSAttrInfo */
	FreeXid(xid);
	return 0;		/* always return success? what if volume is no longer available? */
}

static int cifs_permission(struct inode * inode, int mask, struct nameidata *nd)
{
	/* the server does permission checks, we do not need to do it here */
	return 0;
}

static kmem_cache_t *cifs_inode_cachep;
kmem_cache_t *cifs_req_cachep;
kmem_cache_t *cifs_mid_cachep;
kmem_cache_t *cifs_oplock_cachep;

static struct inode *
cifs_alloc_inode(struct super_block *sb)
{
	struct cifsInodeInfo *cifs_inode;
	cifs_inode =
	    (struct cifsInodeInfo *) kmem_cache_alloc(cifs_inode_cachep,
						      SLAB_KERNEL);
	if (!cifs_inode)
		return NULL;
	cifs_inode->cifsAttrs = 0x20;	/* default */
	atomic_set(&cifs_inode->inUse, 0);
	cifs_inode->time = 0;
	if(oplockEnabled) {
		cifs_inode->clientCanCacheRead = 1;
		cifs_inode->clientCanCacheAll = 1;
	}
	INIT_LIST_HEAD(&cifs_inode->openFileList);
	return &cifs_inode->vfs_inode;
}

static void
cifs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(cifs_inode_cachep, CIFS_I(inode));
}

/*
 * cifs_show_options() is for displaying mount options in /proc/mounts.
 * Not all settable options are displayed but most of the important
 * ones are.
 */
static int
cifs_show_options(struct seq_file *s, struct vfsmount *m)
{
	struct cifs_sb_info *cifs_sb;

	cifs_sb = CIFS_SB(m->mnt_sb);

	if (cifs_sb) {
		if (cifs_sb->tcon) {
			seq_printf(s, ",unc=%s", cifs_sb->tcon->treeName);
			if (cifs_sb->tcon->ses->userName)
				seq_printf(s, ",username=%s",
					   cifs_sb->tcon->ses->userName);
			if(cifs_sb->tcon->ses->domainName)
				seq_printf(s, ",domain=%s",
					cifs_sb->tcon->ses->domainName);
		}
		seq_printf(s, ",rsize=%d",cifs_sb->rsize);
		seq_printf(s, ",wsize=%d",cifs_sb->wsize);
	}
	return 0;
}

struct super_operations cifs_super_ops = {
	.read_inode = cifs_read_inode,
	.put_super = cifs_put_super,
	.statfs = cifs_statfs,
	.alloc_inode = cifs_alloc_inode,
	.destroy_inode = cifs_destroy_inode,
/*	.drop_inode	    = generic_delete_inode, 
	.delete_inode	= cifs_delete_inode,  *//* Do not need the above two functions     
   unless later we add lazy close of inodes or unless the kernel forgets to call
   us with the same number of releases (closes) as opens */
	.show_options = cifs_show_options,
/*    .umount_begin   = cifs_umount_begin, *//* consider adding in the future */
};

static struct super_block *
cifs_get_sb(struct file_system_type *fs_type,
	    int flags, const char *dev_name, void *data)
{
	int rc;
	struct super_block *sb = sget(fs_type, NULL, set_anon_super, NULL);

	cFYI(1, ("Devname: %s flags: %d ", dev_name, flags));

	if (IS_ERR(sb))
		return sb;

	sb->s_flags = flags;

	rc = cifs_read_super(sb, data, dev_name, flags & MS_VERBOSE ? 1 : 0);
	if (rc) {
		up_write(&sb->s_umount);
		deactivate_super(sb);
		return ERR_PTR(rc);
	}
	sb->s_flags |= MS_ACTIVE;
	return sb;
}

static struct file_system_type cifs_fs_type = {
	.owner = THIS_MODULE,
	.name = "cifs",
	.get_sb = cifs_get_sb,
	.kill_sb = kill_anon_super,
	/*  .fs_flags */
};
struct inode_operations cifs_dir_inode_ops = {
	.create = cifs_create,
	.lookup = cifs_lookup,
	.getattr = cifs_getattr,
	.unlink = cifs_unlink,
	.link = cifs_hardlink,
	.mkdir = cifs_mkdir,
	.rmdir = cifs_rmdir,
	.rename = cifs_rename,
	.permission = cifs_permission,
/*	revalidate:cifs_revalidate,   */
	.setattr = cifs_setattr,
	.symlink = cifs_symlink,
};

struct inode_operations cifs_file_inode_ops = {
/*	revalidate:cifs_revalidate, */
	.setattr = cifs_setattr,
	.getattr = cifs_getattr, /* do we need this anymore? */
	.rename = cifs_rename,
	.permission = cifs_permission,
#ifdef CIFS_XATTR
	.setxattr = cifs_setxattr,
	.getxattr = cifs_getxattr,
	.listxattr = cifs_listxattr,
	.removexattr = cifs_removexattr,
#endif 
};

struct inode_operations cifs_symlink_inode_ops = {
	.readlink = cifs_readlink,
	.follow_link = cifs_follow_link,
	.permission = cifs_permission,
	/* BB add the following two eventually */
	/* revalidate: cifs_revalidate,
	   setattr:    cifs_notify_change, *//* BB do we need notify change */
#ifdef CIFS_XATTR
	.setxattr = cifs_setxattr,
	.getxattr = cifs_getxattr,
	.listxattr = cifs_listxattr,
	.removexattr = cifs_removexattr,
#endif 
};

struct file_operations cifs_file_ops = {
	.read = generic_file_read,
	.write = generic_file_write, 
	.open = cifs_open,
	.release = cifs_close,
	.lock = cifs_lock,
	.fsync = cifs_fsync,
	.flush = cifs_flush,
	.mmap  = cifs_file_mmap,
	.sendfile = generic_file_sendfile,
};

struct file_operations cifs_dir_ops = {
	.readdir = cifs_readdir,
	.release = cifs_closedir,
	.read    = generic_read_dir,
};

static void
cifs_init_once(void *inode, kmem_cache_t * cachep, unsigned long flags)
{
	struct cifsInodeInfo *cifsi = (struct cifsInodeInfo *) inode;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&cifsi->vfs_inode);
		INIT_LIST_HEAD(&cifsi->lockList);
	}
}

int
cifs_init_inodecache(void)
{
	cifs_inode_cachep = kmem_cache_create("cifs_inode_cache",
					      sizeof (struct cifsInodeInfo),
					      0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					      cifs_init_once, NULL);
	if (cifs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void
cifs_destroy_inodecache(void)
{
	if (kmem_cache_destroy(cifs_inode_cachep))
		printk(KERN_WARNING "cifs_inode_cache: error freeing\n");
}

int
cifs_init_request_bufs(void)
{
	cifs_req_cachep = kmem_cache_create("cifs_request",
					    CIFS_MAX_MSGSIZE +
					    MAX_CIFS_HDR_SIZE, 0,
					    SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (cifs_req_cachep == NULL)
		return -ENOMEM;

	return 0;
}

void
cifs_destroy_request_bufs(void)
{
	if (kmem_cache_destroy(cifs_req_cachep))
		printk(KERN_WARNING
		       "cifs_destroy_request_cache: error not all structures were freed\n");
}

int
cifs_init_mids(void)
{
	cifs_mid_cachep = kmem_cache_create("cifs_mpx_ids",
				sizeof (struct mid_q_entry), 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (cifs_mid_cachep == NULL)
		return -ENOMEM;
	cifs_oplock_cachep = kmem_cache_create("cifs_oplock_structs",
				sizeof (struct oplock_q_entry), 0,
				SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (cifs_oplock_cachep == NULL) {
		kmem_cache_destroy(cifs_mid_cachep);
		return -ENOMEM;
	}

	return 0;
}

void
cifs_destroy_mids(void)
{
	if (kmem_cache_destroy(cifs_mid_cachep))
		printk(KERN_WARNING
		       "cifs_destroy_mids: error not all structures were freed\n");
	if (kmem_cache_destroy(cifs_oplock_cachep))
		printk(KERN_WARNING
		       "error not all oplock structures were freed\n");
}

static int cifs_oplock_thread(void * dummyarg)
{
	struct list_head * tmp;
	struct list_head * tmp1;
	struct oplock_q_entry * oplock_item;
	struct file * pfile;
	struct cifsTconInfo *pTcon;
	int rc;

	daemonize("cifsoplockd");
	allow_signal(SIGTERM);

	oplockThread = current;
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(100*HZ);
		/* BB add missing code */
		write_lock(&GlobalMid_Lock); 
		list_for_each_safe(tmp, tmp1, &GlobalOplock_Q) {
			oplock_item = list_entry(tmp, struct oplock_q_entry,
							       qhead);
			if(oplock_item) {
				pTcon = oplock_item->tcon;
				pfile = oplock_item->file_to_flush;
				cFYI(1,("process item on queue"));/* BB remove */
				DeleteOplockQEntry(oplock_item);
				write_unlock(&GlobalMid_Lock);
				rc = filemap_fdatawrite(pfile->f_dentry->d_inode->i_mapping);
				if(rc)
					CIFS_I(pfile->f_dentry->d_inode)->write_behind_rc 
						= rc;
				cFYI(1,("Oplock flush file %p rc %d",pfile,rc));
				/* send oplock break */
				write_lock(&GlobalMid_Lock);
			} else
				break;
		}
		write_unlock(&GlobalMid_Lock);
	} while(!signal_pending(current));
	complete_and_exit (&cifs_oplock_exited, 0);
}

static int __init
init_cifs(void)
{
	int rc = 0;
#ifdef CONFIG_PROC_FS
	cifs_proc_init();
#endif
	INIT_LIST_HEAD(&GlobalServerList);	/* BB not implemented yet */
	INIT_LIST_HEAD(&GlobalSMBSessionList);
	INIT_LIST_HEAD(&GlobalTreeConnectionList);
	INIT_LIST_HEAD(&GlobalOplock_Q);
/*
 *  Initialize Global counters
 */
	atomic_set(&sesInfoAllocCount, 0);
	atomic_set(&tconInfoAllocCount, 0);
	atomic_set(&bufAllocCount, 0);
	atomic_set(&midCount, 0);
	GlobalCurrentXid = 0;
	GlobalTotalActiveXid = 0;
	GlobalMaxActiveXid = 0;
	GlobalSMBSeslock = RW_LOCK_UNLOCKED;
	GlobalMid_Lock = RW_LOCK_UNLOCKED;

	rc = cifs_init_inodecache();
	if (!rc) {
		rc = cifs_init_mids();
		if (!rc) {
			rc = cifs_init_request_bufs();
			if (!rc) {
				rc = register_filesystem(&cifs_fs_type);
				if (!rc) {                
					kernel_thread(cifs_oplock_thread, NULL, 
						CLONE_FS | CLONE_FILES | CLONE_VM);
					return rc; /* Success */
				} else
					cifs_destroy_request_bufs();
			}
			cifs_destroy_mids();
		}
		cifs_destroy_inodecache();
	}
#ifdef CONFIG_PROC_FS
	cifs_proc_clean();
#endif
	return rc;
}

static void __exit
exit_cifs(void)
{
	cFYI(0, ("In unregister ie exit_cifs"));
#ifdef CONFIG_PROC_FS
	cifs_proc_clean();
#endif
	unregister_filesystem(&cifs_fs_type);
	cifs_destroy_inodecache();
	cifs_destroy_mids();
	cifs_destroy_request_bufs();
	if(oplockThread) {
		send_sig(SIGTERM, oplockThread, 1);
		wait_for_completion(&cifs_oplock_exited);
	}
}

MODULE_AUTHOR("Steve French <sfrench@us.ibm.com>");
MODULE_LICENSE("GPL");		/* combination of LGPL + GPL source behaves as GPL */
MODULE_DESCRIPTION
    ("VFS to access servers complying with the SNIA CIFS Specification e.g. Samba and Windows");
module_init(init_cifs)
module_exit(exit_cifs)
