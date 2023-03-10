/* cnode related routines for the coda kernel code
   (C) 1996 Peter Braam
   */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/time.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>

inline int coda_fideq(ViceFid *fid1, ViceFid *fid2)
{
	if (fid1->Vnode != fid2->Vnode)   return 0;
	if (fid1->Volume != fid2->Volume) return 0;
	if (fid1->Unique != fid2->Unique) return 0;
	return 1;
}

inline int coda_isnullfid(ViceFid *fid)
{
	if (fid->Vnode || fid->Volume || fid->Unique) return 0;
	return 1;
}

static struct inode_operations coda_symlink_inode_operations = {
	.readlink	= page_readlink,
	.follow_link	= page_follow_link,
	.setattr	= coda_setattr,
};

/* cnode.c */
static void coda_fill_inode(struct inode *inode, struct coda_vattr *attr)
{
        coda_vattr_to_iattr(inode, attr);

        if (S_ISREG(inode->i_mode)) {
                inode->i_op = &coda_file_inode_operations;
                inode->i_fop = &coda_file_operations;
        } else if (S_ISDIR(inode->i_mode)) {
                inode->i_op = &coda_dir_inode_operations;
                inode->i_fop = &coda_dir_operations;
        } else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &coda_symlink_inode_operations;
		inode->i_data.a_ops = &coda_symlink_aops;
		inode->i_mapping = &inode->i_data;
	} else
                init_special_inode(inode, inode->i_mode, attr->va_rdev);
}

static int coda_test_inode(struct inode *inode, void *data)
{
	ViceFid *fid = (ViceFid *)data;
	return coda_fideq(&(ITOC(inode)->c_fid), fid);
}

static int coda_set_inode(struct inode *inode, void *data)
{
	ViceFid *fid = (ViceFid *)data;
	ITOC(inode)->c_fid = *fid;
	return 0;
}

static int coda_fail_inode(struct inode *inode, void *data)
{
	return -1;
}

struct inode * coda_iget(struct super_block * sb, ViceFid * fid,
			 struct coda_vattr * attr)
{
	struct inode *inode;
	struct coda_inode_info *cii;
	struct coda_sb_info *sbi = coda_sbp(sb);
	unsigned long hash = coda_f2i(fid);

	inode = iget5_locked(sb, hash, coda_test_inode, coda_set_inode, fid);

	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		cii = ITOC(inode);
		/* we still need to set i_ino for things like stat(2) */
		inode->i_ino = hash;
		cii->c_mapcount = 0;
		list_add(&cii->c_cilist, &sbi->sbi_cihead);
		unlock_new_inode(inode);
	}

	/* always replace the attributes, type might have changed */
	coda_fill_inode(inode, attr);
	return inode;
}

/* this is effectively coda_iget:
   - get attributes (might be cached)
   - get the inode for the fid using vfs iget
   - link the two up if this is needed
   - fill in the attributes
*/
int coda_cnode_make(struct inode **inode, ViceFid *fid, struct super_block *sb)
{
        struct coda_vattr attr;
        int error;
        
	/* We get inode numbers from Venus -- see venus source */
	error = venus_getattr(sb, fid, &attr);
	if ( error ) {
	    *inode = NULL;
	    return error;
	} 

	*inode = coda_iget(sb, fid, &attr);
	if ( IS_ERR(*inode) ) {
		printk("coda_cnode_make: coda_iget failed\n");
                return PTR_ERR(*inode);
        }
	return 0;
}


void coda_replace_fid(struct inode *inode, struct ViceFid *oldfid, 
		      struct ViceFid *newfid)
{
	struct coda_inode_info *cii;
	unsigned long hash = coda_f2i(newfid);
	
	cii = ITOC(inode);

	if (!coda_fideq(&cii->c_fid, oldfid))
		BUG();

	/* replace fid and rehash inode */
	/* XXX we probably need to hold some lock here! */
	remove_inode_hash(inode);
	cii->c_fid = *newfid;
	inode->i_ino = hash;
	__insert_inode_hash(inode, hash);
}

/* convert a fid to an inode. */
struct inode *coda_fid_to_inode(ViceFid *fid, struct super_block *sb) 
{
	struct inode *inode;
	unsigned long hash = coda_f2i(fid);

	if ( !sb ) {
		printk("coda_fid_to_inode: no sb!\n");
		return NULL;
	}

	inode = iget5_locked(sb, hash, coda_test_inode, coda_fail_inode, fid);
	if ( !inode )
		return NULL;

	/* we should never see newly created inodes because we intentionally
	 * fail in the initialization callback */
	BUG_ON(inode->i_state & I_NEW);

	return inode;
}

/* the CONTROL inode is made without asking attributes from Venus */
int coda_cnode_makectl(struct inode **inode, struct super_block *sb)
{
	int error = -ENOMEM;

	*inode = new_inode(sb);
	if (*inode) {
		(*inode)->i_ino = CTL_INO;
		(*inode)->i_op = &coda_ioctl_inode_operations;
		(*inode)->i_fop = &coda_ioctl_operations;
		(*inode)->i_mode = 0444;
		error = 0;
	}

	return error;
}

