/*
 *  linux/fs/hpfs/inode.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  inode VFS functions
 */

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include "hpfs_fn.h"

static struct file_operations hpfs_file_ops =
{
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= hpfs_file_write,
	.mmap		= generic_file_mmap,
	.open		= hpfs_open,
	.release	= hpfs_file_release,
	.fsync		= hpfs_file_fsync,
	.sendfile	= generic_file_sendfile,
};

static struct inode_operations hpfs_file_iops =
{
	.truncate	= hpfs_truncate,
	.setattr	= hpfs_notify_change,
};

static struct file_operations hpfs_dir_ops =
{
	.llseek		= hpfs_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= hpfs_readdir,
	.open		= hpfs_open,
	.release	= hpfs_dir_release,
	.fsync		= hpfs_file_fsync,
};

static struct inode_operations hpfs_dir_iops =
{
	.create		= hpfs_create,
	.lookup		= hpfs_lookup,
	.unlink		= hpfs_unlink,
	.symlink	= hpfs_symlink,
	.mkdir		= hpfs_mkdir,
	.rmdir		= hpfs_rmdir,
	.mknod		= hpfs_mknod,
	.rename		= hpfs_rename,
	.setattr	= hpfs_notify_change,
};

struct address_space_operations hpfs_symlink_aops = {
	.readpage	= hpfs_symlink_readpage
};

void hpfs_read_inode(struct inode *i)
{
	struct buffer_head *bh;
	struct fnode *fnode;
	struct super_block *sb = i->i_sb;
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	unsigned char *ea;
	int ea_size;

	i->i_uid = hpfs_sb(sb)->sb_uid;
	i->i_gid = hpfs_sb(sb)->sb_gid;
	i->i_mode = hpfs_sb(sb)->sb_mode;
	hpfs_inode->i_conv = hpfs_sb(sb)->sb_conv;
	i->i_blksize = 512;
	i->i_size = -1;
	i->i_blocks = -1;
	
	hpfs_inode->i_dno = 0;
	hpfs_inode->i_n_secs = 0;
	hpfs_inode->i_file_sec = 0;
	hpfs_inode->i_disk_sec = 0;
	hpfs_inode->i_dpos = 0;
	hpfs_inode->i_dsubdno = 0;
	hpfs_inode->i_ea_mode = 0;
	hpfs_inode->i_ea_uid = 0;
	hpfs_inode->i_ea_gid = 0;
	hpfs_inode->i_ea_size = 0;

	hpfs_inode->i_rddir_off = NULL;
	hpfs_inode->i_dirty = 0;

	i->i_ctime.tv_sec = i->i_ctime.tv_nsec = 0;
	i->i_mtime.tv_sec = i->i_mtime.tv_nsec = 0;
	i->i_atime.tv_sec = i->i_atime.tv_nsec = 0;

	if (!hpfs_sb(i->i_sb)->sb_rd_inode)
		hpfs_error(i->i_sb, "read_inode: sb_rd_inode == 0");
	if (hpfs_sb(i->i_sb)->sb_rd_inode == 2) {
		i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		i->i_nlink = 1;
		return;
	}
	if (!(fnode = hpfs_map_fnode(sb, i->i_ino, &bh))) {
		/*i->i_mode |= S_IFREG;
		i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		i->i_nlink = 0;*/
		make_bad_inode(i);
		return;
	}
	if (hpfs_sb(i->i_sb)->sb_eas) {
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "UID", &ea_size))) {
			if (ea_size == 2) {
				i->i_uid = ea[0] + (ea[1] << 8);
				hpfs_inode->i_ea_uid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "GID", &ea_size))) {
			if (ea_size == 2) {
				i->i_gid = ea[0] + (ea[1] << 8);
				hpfs_inode->i_ea_gid = 1;
			}
			kfree(ea);
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "SYMLINK", &ea_size))) {
			kfree(ea);
			i->i_mode = S_IFLNK | 0777;
			i->i_op = &page_symlink_inode_operations;
			i->i_data.a_ops = &hpfs_symlink_aops;
			i->i_nlink = 1;
			i->i_size = ea_size;
			i->i_blocks = 1;
			brelse(bh);
			return;
		}
		if ((ea = hpfs_get_ea(i->i_sb, fnode, "MODE", &ea_size))) {
			int rdev = 0;
			umode_t mode = hpfs_sb(sb)->sb_mode;
			if (ea_size == 2) {
				mode = ea[0] + (ea[1] << 8);
				hpfs_inode->i_ea_mode = 1;
			}
			kfree(ea);
			i->i_mode = mode;
			if (S_ISBLK(mode) || S_ISCHR(mode)) {
				if ((ea = hpfs_get_ea(i->i_sb, fnode, "DEV", &ea_size))) {
					if (ea_size == 4)
						rdev = ea[0] + (ea[1] << 8) + (ea[2] << 16) + (ea[3] << 24);
					kfree(ea);
				}
			}
			if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISFIFO(mode) || S_ISSOCK(mode)) {
				brelse(bh);
				i->i_nlink = 1;
				i->i_size = 0;
				i->i_blocks = 1;
				init_special_inode(i, mode, rdev);
				return;
			}
		}
	}
	if (fnode->dirflag) {
		unsigned n_dnodes, n_subdirs;
		i->i_mode |= S_IFDIR;
		i->i_op = &hpfs_dir_iops;
		i->i_fop = &hpfs_dir_ops;
		hpfs_inode->i_parent_dir = fnode->up;
		hpfs_inode->i_dno = fnode->u.external[0].disk_secno;
		if (hpfs_sb(sb)->sb_chk >= 2) {
			struct buffer_head *bh0;
			if (hpfs_map_fnode(sb, hpfs_inode->i_parent_dir, &bh0)) brelse(bh0);
		}
		n_dnodes = 0; n_subdirs = 0;
		hpfs_count_dnodes(i->i_sb, hpfs_inode->i_dno, &n_dnodes, &n_subdirs, NULL);
		i->i_blocks = 4 * n_dnodes;
		i->i_size = 2048 * n_dnodes;
		i->i_nlink = 2 + n_subdirs;
	} else {
		i->i_mode |= S_IFREG;
		if (!hpfs_inode->i_ea_mode) i->i_mode &= ~0111;
		i->i_op = &hpfs_file_iops;
		i->i_fop = &hpfs_file_ops;
		i->i_nlink = 1;
		i->i_size = fnode->file_size;
		i->i_blocks = ((i->i_size + 511) >> 9) + 1;
		i->i_data.a_ops = &hpfs_aops;
		hpfs_i(i)->mmu_private = i->i_size;
	}
	brelse(bh);
}

void hpfs_write_inode_ea(struct inode *i, struct fnode *fnode)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	if (fnode->acl_size_l || fnode->acl_size_s) {
		/* Some unknown structures like ACL may be in fnode,
		   we'd better not overwrite them */
		hpfs_error(i->i_sb, "fnode %08x has some unknown HPFS386 stuctures", i->i_ino);
	} else if (hpfs_sb(i->i_sb)->sb_eas >= 2) {
		unsigned char ea[4];
		if ((i->i_uid != hpfs_sb(i->i_sb)->sb_uid) || hpfs_inode->i_ea_uid) {
			ea[0] = i->i_uid & 0xff;
			ea[1] = i->i_uid >> 8;
			hpfs_set_ea(i, fnode, "UID", ea, 2);
			hpfs_inode->i_ea_uid = 1;
		}
		if ((i->i_gid != hpfs_sb(i->i_sb)->sb_gid) || hpfs_inode->i_ea_gid) {
			ea[0] = i->i_gid & 0xff;
			ea[1] = i->i_gid >> 8;
			hpfs_set_ea(i, fnode, "GID", ea, 2);
			hpfs_inode->i_ea_gid = 1;
		}
		if (!S_ISLNK(i->i_mode))
			if ((i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0 : 0111))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))
			  && i->i_mode != ((hpfs_sb(i->i_sb)->sb_mode & ~(S_ISDIR(i->i_mode) ? 0222 : 0333))
			  | (S_ISDIR(i->i_mode) ? S_IFDIR : S_IFREG))) || hpfs_inode->i_ea_mode) {
				ea[0] = i->i_mode & 0xff;
				ea[1] = i->i_mode >> 8;
				hpfs_set_ea(i, fnode, "MODE", ea, 2);
				hpfs_inode->i_ea_mode = 1;
			}
		if (S_ISBLK(i->i_mode) || S_ISCHR(i->i_mode)) {
			int d = kdev_t_to_nr(i->i_rdev);
			ea[0] = d & 0xff;
			ea[1] = (d >> 8) & 0xff;
			ea[2] = (d >> 16) & 0xff;
			ea[3] = d >> 24;
			hpfs_set_ea(i, fnode, "DEV", ea, 4);
		}
	}
}

void hpfs_write_inode(struct inode *i)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct inode *parent;
	if (!i->i_nlink) return;
	if (i->i_ino == hpfs_sb(i->i_sb)->sb_root) return;
	if (hpfs_inode->i_rddir_off && !atomic_read(&i->i_count)) {
		if (*hpfs_inode->i_rddir_off) printk("HPFS: write_inode: some position still there\n");
		kfree(hpfs_inode->i_rddir_off);
		hpfs_inode->i_rddir_off = NULL;
	}
	hpfs_inode->i_dirty = 0;
	hpfs_lock_iget(i->i_sb, 1);
	parent = iget(i->i_sb, hpfs_inode->i_parent_dir);
	hpfs_unlock_iget(i->i_sb);
	hpfs_lock_inode(parent);
	hpfs_write_inode_nolock(i);
	hpfs_unlock_inode(parent);
	iput(parent);
}

void hpfs_write_inode_nolock(struct inode *i)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(i);
	struct buffer_head *bh;
	struct fnode *fnode;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	if (i->i_ino == hpfs_sb(i->i_sb)->sb_root) return;
	if (!(fnode = hpfs_map_fnode(i->i_sb, i->i_ino, &bh))) return;
	if (i->i_ino != hpfs_sb(i->i_sb)->sb_root) {
		if (!(de = map_fnode_dirent(i->i_sb, i->i_ino, fnode, &qbh))) {
			brelse(bh);
			return;
		}
	} else de = NULL;
	if (S_ISREG(i->i_mode)) {
		fnode->file_size = de->file_size = i->i_size;
	} else if (S_ISDIR(i->i_mode)) {
		fnode->file_size = de->file_size = 0;
	}
	hpfs_write_inode_ea(i, fnode);
	if (de) {
		de->write_date = gmt_to_local(i->i_sb, i->i_mtime.tv_sec);
		de->read_date = gmt_to_local(i->i_sb, i->i_atime.tv_sec);
		de->creation_date = gmt_to_local(i->i_sb, i->i_ctime.tv_sec);
		de->read_only = !(i->i_mode & 0222);
		de->ea_size = hpfs_inode->i_ea_size;
		hpfs_mark_4buffers_dirty(&qbh);
		hpfs_brelse4(&qbh);
	}
	if (S_ISDIR(i->i_mode)) {
		if ((de = map_dirent(i, hpfs_inode->i_dno, "\001\001", 2, NULL, &qbh))) {
			de->write_date = gmt_to_local(i->i_sb, i->i_mtime.tv_sec);
			de->read_date = gmt_to_local(i->i_sb, i->i_atime.tv_sec);
			de->creation_date = gmt_to_local(i->i_sb, i->i_ctime.tv_sec);
			de->read_only = !(i->i_mode & 0222);
			de->ea_size = /*hpfs_inode->i_ea_size*/0;
			de->file_size = 0;
			hpfs_mark_4buffers_dirty(&qbh);
			hpfs_brelse4(&qbh);
		} else hpfs_error(i->i_sb, "directory %08x doesn't have '.' entry", i->i_ino);
	}
	mark_buffer_dirty(bh);
	brelse(bh);
}

int hpfs_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error=0;
	lock_kernel();
	if ( ((attr->ia_valid & ATTR_SIZE) && attr->ia_size > inode->i_size) ||
	     (hpfs_sb(inode->i_sb)->sb_root == inode->i_ino) ) {
		error = -EINVAL;
	} else if ((error = inode_change_ok(inode, attr))) {
	} else if ((error = inode_setattr(inode, attr))) {
	} else {
		hpfs_write_inode(inode);
	}
	unlock_kernel();
	return error;
}

void hpfs_write_if_changed(struct inode *inode)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);

	if (hpfs_inode->i_dirty)
		hpfs_write_inode(inode);
}

void hpfs_delete_inode(struct inode *inode)
{
	lock_kernel();
	hpfs_remove_fnode(inode->i_sb, inode->i_ino);
	unlock_kernel();
	clear_inode(inode);
}
