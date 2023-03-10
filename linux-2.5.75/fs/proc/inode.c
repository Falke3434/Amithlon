/*
 *  linux/fs/proc/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/file.h>
#include <linux/limits.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>

extern void free_proc_entry(struct proc_dir_entry *);

static inline struct proc_dir_entry * de_get(struct proc_dir_entry *de)
{
	if (de)
		atomic_inc(&de->count);
	return de;
}

/*
 * Decrements the use count and checks for deferred deletion.
 */
static void de_put(struct proc_dir_entry *de)
{
	if (de) {	
		lock_kernel();		
		if (!atomic_read(&de->count)) {
			printk("de_put: entry %s already free!\n", de->name);
			unlock_kernel();
			return;
		}

		if (atomic_dec_and_test(&de->count)) {
			if (de->deleted) {
				printk("de_put: deferred delete of %s\n",
					de->name);
				free_proc_entry(de);
			}
		}		
		unlock_kernel();
	}
}

/*
 * Decrement the use count of the proc_dir_entry.
 */
static void proc_delete_inode(struct inode *inode)
{
	struct proc_dir_entry *de;
	struct task_struct *tsk;

	/* Let go of any associated process */
	tsk = PROC_I(inode)->task;
	if (tsk)
		put_task_struct(tsk);

	/* Let go of any associated proc directory entry */
	de = PROC_I(inode)->pde;
	if (de) {
		if (de->owner)
			module_put(de->owner);
		de_put(de);
	}
	clear_inode(inode);
}

struct vfsmount *proc_mnt;

static void proc_read_inode(struct inode * inode)
{
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
}

static kmem_cache_t * proc_inode_cachep;

static struct inode *proc_alloc_inode(struct super_block *sb)
{
	struct proc_inode *ei;
	struct inode *inode;

	ei = (struct proc_inode *)kmem_cache_alloc(proc_inode_cachep, SLAB_KERNEL);
	if (!ei)
		return NULL;
	ei->task = NULL;
	ei->type = 0;
	ei->op.proc_get_link = NULL;
	ei->pde = NULL;
	inode = &ei->vfs_inode;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	return inode;
}

static void proc_destroy_inode(struct inode *inode)
{
	kmem_cache_free(proc_inode_cachep, PROC_I(inode));
}

static void init_once(void * foo, kmem_cache_t * cachep, unsigned long flags)
{
	struct proc_inode *ei = (struct proc_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ei->vfs_inode);
}
 
int __init proc_init_inodecache(void)
{
	proc_inode_cachep = kmem_cache_create("proc_inode_cache",
					     sizeof(struct proc_inode),
					     0, SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					     init_once, NULL);
	if (proc_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static struct super_operations proc_sops = { 
	.alloc_inode	= proc_alloc_inode,
	.destroy_inode	= proc_destroy_inode,
	.read_inode	= proc_read_inode,
	.drop_inode	= generic_delete_inode,
	.delete_inode	= proc_delete_inode,
	.statfs		= simple_statfs,
};

static int parse_options(char *options,uid_t *uid,gid_t *gid)
{
	char *this_char,*value;

	*uid = current->uid;
	*gid = current->gid;
	if (!options)
		return 1;
	while ((this_char = strsep(&options,",")) != NULL) {
		if (!*this_char)
			continue;
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"uid")) {
			if (!value || !*value)
				return 0;
			*uid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"gid")) {
			if (!value || !*value)
				return 0;
			*gid = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else return 1;
	}
	return 1;
}

struct inode * proc_get_inode(struct super_block * sb, int ino,
				struct proc_dir_entry * de)
{
	struct inode * inode;

	/*
	 * Increment the use count so the dir entry can't disappear.
	 */
	de_get(de);
#if 1
/* shouldn't ever happen */
if (de && de->deleted)
printk("proc_iget: using deleted entry %s, count=%d\n", de->name, atomic_read(&de->count));
#endif

	inode = iget(sb, ino);
	if (!inode)
		goto out_fail;
	
	PROC_I(inode)->pde = de;
	if (de) {
		if (de->mode) {
			inode->i_mode = de->mode;
			inode->i_uid = de->uid;
			inode->i_gid = de->gid;
		}
		if (de->size)
			inode->i_size = de->size;
		if (de->nlink)
			inode->i_nlink = de->nlink;
		if (!try_module_get(de->owner))
			goto out_fail;
		if (de->proc_iops)
			inode->i_op = de->proc_iops;
		if (de->proc_fops)
			inode->i_fop = de->proc_fops;
		else if (S_ISBLK(de->mode)||S_ISCHR(de->mode)||S_ISFIFO(de->mode))
			init_special_inode(inode,de->mode,kdev_t_to_nr(de->rdev));
	}

out:
	return inode;

out_fail:
	de_put(de);
	goto out;
}			

int proc_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode * root_inode;

	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = PROC_SUPER_MAGIC;
	s->s_op = &proc_sops;
	
	root_inode = proc_get_inode(s, PROC_ROOT_INO, &proc_root);
	if (!root_inode)
		goto out_no_root;
	/*
	 * Fixup the root inode's nlink value
	 */
	root_inode->i_nlink += nr_processes();
	s->s_root = d_alloc_root(root_inode);
	if (!s->s_root)
		goto out_no_root;
	parse_options(data, &root_inode->i_uid, &root_inode->i_gid);
	return 0;

out_no_root:
	printk("proc_read_super: get root inode failed\n");
	iput(root_inode);
	return -ENOMEM;
}
MODULE_LICENSE("GPL");
