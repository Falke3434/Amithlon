/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/fs.h>
#include <linux/reiserfs_fs.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

/*
** reiserfs_ioctl - handler for ioctl for inode
** supported commands:
**  1) REISERFS_IOC_UNPACK - try to unpack tail from direct item into indirect
**                           and prevent packing file (argument arg has to be non-zero)
**  2) REISERFS_IOC_[GS]ETFLAGS, REISERFS_IOC_[GS]ETVERSION
**  3) That's all for a while ...
*/
int reiserfs_ioctl (struct inode * inode, struct file * filp, unsigned int cmd,
		unsigned long arg)
{
	unsigned int flags;

	switch (cmd) {
	    case REISERFS_IOC_UNPACK:
		if( S_ISREG( inode -> i_mode ) ) {
		if (arg)
		    return reiserfs_unpack (inode, filp);
			else
				return 0;
		} else
			return -ENOTTY;
	/* following two cases are taken from fs/ext2/ioctl.c by Remy
	   Card (card@masi.ibp.fr) */
	case REISERFS_IOC_GETFLAGS:
		flags = REISERFS_I(inode) -> i_attrs;
		i_attrs_to_sd_attrs( inode, ( __u16 * ) &flags );
		return put_user(flags, (int *) arg);
	case REISERFS_IOC_SETFLAGS: {
		if (IS_RDONLY(inode))
			return -EROFS;

		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EPERM;

		if (get_user(flags, (int *) arg))
			return -EFAULT;

		if ( ( flags & REISERFS_IMMUTABLE_FL ) && 
		     !capable( CAP_LINUX_IMMUTABLE ) )
			return -EPERM;
			
		if( ( flags & REISERFS_NOTAIL_FL ) &&
		    S_ISREG( inode -> i_mode ) ) {
				int result;

				result = reiserfs_unpack( inode, filp );
				if( result )
					return result;
		}
		sd_attrs_to_i_attrs( flags, inode );
		REISERFS_I(inode) -> i_attrs = flags;
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
	}
	case REISERFS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int *) arg);
	case REISERFS_IOC_SETVERSION:
		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EPERM;
		if (IS_RDONLY(inode))
			return -EROFS;
		if (get_user(inode->i_generation, (int *) arg))
			return -EFAULT;	
		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(inode);
		return 0;
	default:
		return -ENOTTY;
	}
}

/*
** reiserfs_unpack
** Function try to convert tail from direct item into indirect.
** It set up nopack attribute in the REISERFS_I(inode)->nopack
*/
int reiserfs_unpack (struct inode * inode, struct file * filp)
{
    int retval = 0;
    int index ;
    struct page *page ;
    unsigned long write_from ;
    unsigned long blocksize = inode->i_sb->s_blocksize ;
    	
    if (inode->i_size == 0) {
        REISERFS_I(inode)->i_flags |= i_nopack_mask;
        return 0 ;
    }
    /* ioctl already done */
    if (REISERFS_I(inode)->i_flags & i_nopack_mask) {
        return 0 ;
    }
    reiserfs_write_lock(inode->i_sb);

    /* we need to make sure nobody is changing the file size beneath
    ** us
    */
    down(&inode->i_sem) ;

    write_from = inode->i_size & (blocksize - 1) ;
    /* if we are on a block boundary, we are already unpacked.  */
    if ( write_from == 0) {
	REISERFS_I(inode)->i_flags |= i_nopack_mask;
	goto out ;
    }

    /* we unpack by finding the page with the tail, and calling
    ** reiserfs_prepare_write on that page.  This will force a 
    ** reiserfs_get_block to unpack the tail for us.
    */
    index = inode->i_size >> PAGE_CACHE_SHIFT ;
    page = grab_cache_page(inode->i_mapping, index) ;
    retval = -ENOMEM;
    if (!page) {
        goto out ;
    }
    retval = reiserfs_prepare_write(NULL, page, write_from, blocksize) ;
    if (retval)
        goto out_unlock ;

    /* conversion can change page contents, must flush */
    flush_dcache_page(page) ;
    REISERFS_I(inode)->i_flags |= i_nopack_mask;

out_unlock:
    unlock_page(page) ;
    page_cache_release(page) ;

out:
    up(&inode->i_sem) ;
    reiserfs_write_unlock(inode->i_sb);
    return retval;
}
