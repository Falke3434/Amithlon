/* 
 * Coda File System, Linux Kernel module
 * 
 * Original version, adapted from cfs_mach.c, (C) Carnegie Mellon University
 * Linux modifications (C) 1996, Peter J. Braam
 * Rewritten for Linux 2.1 (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this software to
 * contribute improvements to the Coda project.
 */

#ifndef _LINUX_CODA_FS
#define _LINUX_CODA_FS

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/wait.h>		
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/coda_fs_i.h>

/* operations */
extern struct inode_operations coda_dir_inode_operations;
extern struct inode_operations coda_file_inode_operations;
extern struct inode_operations coda_ioctl_inode_operations;

extern struct address_space_operations coda_file_aops;
extern struct address_space_operations coda_symlink_aops;

extern struct file_operations coda_dir_operations;
extern struct file_operations coda_file_operations;
extern struct file_operations coda_ioctl_operations;

/* operations shared over more than one file */
int coda_open(struct inode *i, struct file *f);
int coda_flush(struct file *f);
int coda_release(struct inode *i, struct file *f);
int coda_permission(struct inode *inode, int mask, struct nameidata *nd);
int coda_revalidate_inode(struct dentry *);
int coda_getattr(struct vfsmount *, struct dentry *, struct kstat *);
int coda_setattr(struct dentry *, struct iattr *);
int coda_isnullfid(ViceFid *fid);

/* global variables */
extern int coda_fake_statfs;

/* this file:  heloers */
static __inline__ struct ViceFid *coda_i2f(struct inode *);
static __inline__ char *coda_i2s(struct inode *);
static __inline__ void coda_flag_inode(struct inode *, int flag);
char *coda_f2s(ViceFid *f);
int coda_isroot(struct inode *i);
int coda_iscontrol(const char *name, size_t length);

void coda_load_creds(struct coda_cred *cred);
void coda_vattr_to_iattr(struct inode *, struct coda_vattr *);
void coda_iattr_to_vattr(struct iattr *, struct coda_vattr *);
unsigned short coda_flags_to_cflags(unsigned short);
void print_vattr( struct coda_vattr *attr );
int coda_cred_ok(struct coda_cred *cred);
int coda_cred_eq(struct coda_cred *cred1, struct coda_cred *cred2);

/* sysctl.h */
void coda_sysctl_init(void);
void coda_sysctl_clean(void);

#define CODA_ALLOC(ptr, cast, size) do { \
    if (size < PAGE_SIZE) \
        ptr = (cast)kmalloc((unsigned long) size, GFP_KERNEL); \
    else \
        ptr = (cast)vmalloc((unsigned long) size); \
    if (!ptr) \
        printk("kernel malloc returns 0 at %s:%d\n", __FILE__, __LINE__); \
    else memset( ptr, 0, size ); \
} while (0)


#define CODA_FREE(ptr,size) \
    do { if (size < PAGE_SIZE) kfree((ptr)); else vfree((ptr)); } while (0)

/* inode to cnode access functions */

static inline struct coda_inode_info *ITOC(struct inode *inode)
{
	return list_entry(inode, struct coda_inode_info, vfs_inode);
}

static __inline__ struct ViceFid *coda_i2f(struct inode *inode)
{
	return &(ITOC(inode)->c_fid);
}

static __inline__ char *coda_i2s(struct inode *inode)
{
	return coda_f2s(&(ITOC(inode)->c_fid));
}

/* this will not zap the inode away */
static __inline__ void coda_flag_inode(struct inode *inode, int flag)
{
	ITOC(inode)->c_flags |= flag;
}		

#endif
