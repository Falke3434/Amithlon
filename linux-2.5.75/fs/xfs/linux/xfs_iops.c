/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bit.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_cap.h"
#include "xfs_mac.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_utils.h"

#include <linux/xattr.h>


/*
 * Pull the link count and size up from the xfs inode to the linux inode
 */
STATIC void
validate_fields(
	struct inode	*ip)
{
	vnode_t		*vp = LINVFS_GET_VP(ip);
	vattr_t		va;
	int		error;

	va.va_mask = XFS_AT_NLINK|XFS_AT_SIZE|XFS_AT_NBLOCKS;
	VOP_GETATTR(vp, &va, ATTR_LAZY, NULL, error);
	ip->i_nlink = va.va_nlink;
	ip->i_size = va.va_size;
	ip->i_blocks = va.va_nblocks;
}

/*
 * Determine whether a process has a valid fs_struct (kernel daemons
 * like knfsd don't have an fs_struct).
 *
 * XXX(hch):  nfsd is broken, better fix it instead.
 */
STATIC inline int
has_fs_struct(struct task_struct *task)
{
	return (task->fs != init_task.fs);
}

STATIC int
linvfs_mknod(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	dev_t		rdev)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*vp = NULL, *dvp = LINVFS_GET_VP(dir);
	xattr_exists_t	test_default_acl = _ACL_DEFAULT_EXISTS;
	int		have_default_acl = 0;
	int		error = EINVAL;

	if (test_default_acl)
		have_default_acl = test_default_acl(dvp);

	if (IS_POSIXACL(dir) && !have_default_acl && has_fs_struct(current))
		mode &= ~current->fs->umask;

	memset(&va, 0, sizeof(va));
	va.va_mask = XFS_AT_TYPE|XFS_AT_MODE;
	va.va_type = IFTOVT(mode);
	va.va_mode = mode;

	switch (mode & S_IFMT) {
	case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		va.va_rdev = XFS_MKDEV(MAJOR(rdev), MINOR(rdev));
		va.va_mask |= XFS_AT_RDEV;
		/*FALLTHROUGH*/
	case S_IFREG:
		VOP_CREATE(dvp, dentry, &va, &vp, NULL, error);
		break;
	case S_IFDIR:
		VOP_MKDIR(dvp, dentry, &va, &vp, NULL, error);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (!error) {
		ASSERT(vp);
		ip = LINVFS_GET_IP(vp);
		if (!ip) {
			VN_RELE(vp);
			return -ENOMEM;
		}

		if (S_ISCHR(mode) || S_ISBLK(mode))
			ip->i_rdev = to_kdev_t(rdev);
		else if (S_ISDIR(mode))
			validate_fields(ip);
		d_instantiate(dentry, ip);
		validate_fields(dir);
	}

	if (!error && have_default_acl) {
		_ACL_DECL	(pdacl);

		if (!_ACL_ALLOC(pdacl)) {
			error = -ENOMEM;
		} else {
			if (_ACL_GET_DEFAULT(dvp, pdacl))
				error = _ACL_INHERIT(vp, &va, pdacl);
			VMODIFY(vp);
			_ACL_FREE(pdacl);
		}
	}
	return -error;
}

STATIC int
linvfs_create(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode,
	struct nameidata *nd)
{
	return linvfs_mknod(dir, dentry, mode, 0);
}

STATIC int
linvfs_mkdir(
	struct inode	*dir,
	struct dentry	*dentry,
	int		mode)
{
	return linvfs_mknod(dir, dentry, mode|S_IFDIR, 0);
}

STATIC struct dentry *
linvfs_lookup(
	struct inode	*dir,
	struct dentry	*dentry,
	struct nameidata *nd)
{
	struct inode	*ip = NULL;
	vnode_t		*vp, *cvp = NULL;
	int		error;

	if (dentry->d_name.len >= MAXNAMELEN)
		return ERR_PTR(-ENAMETOOLONG);

	vp = LINVFS_GET_VP(dir);
	VOP_LOOKUP(vp, dentry, &cvp, 0, NULL, NULL, error);
	if (!error) {
		ASSERT(cvp);
		ip = LINVFS_GET_IP(cvp);
		if (!ip) {
			VN_RELE(cvp);
			return ERR_PTR(-EACCES);
		}
	}
	if (error && (error != ENOENT))
		return ERR_PTR(-error);
	return d_splice_alias(ip, dentry);
}

STATIC int
linvfs_link(
	struct dentry	*old_dentry,
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*ip;	/* inode of guy being linked to */
	vnode_t		*tdvp;	/* target directory for new name/link */
	vnode_t		*vp;	/* vp of name being linked */
	int		error;

	ip = old_dentry->d_inode;	/* inode being linked to */
	if (S_ISDIR(ip->i_mode))
		return -EPERM;

	tdvp = LINVFS_GET_VP(dir);
	vp = LINVFS_GET_VP(ip);

	VOP_LINK(tdvp, vp, dentry, NULL, error);
	if (!error) {
		VMODIFY(tdvp);
		VN_HOLD(vp);
		validate_fields(ip);
		d_instantiate(dentry, ip);
	}
	return -error;
}

STATIC int
linvfs_unlink(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode;
	vnode_t		*dvp;	/* directory containing name to remove */
	int		error;

	inode = dentry->d_inode;
	dvp = LINVFS_GET_VP(dir);

	VOP_REMOVE(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(dir);	/* For size only */
		validate_fields(inode);
	}

	return -error;
}

STATIC int
linvfs_symlink(
	struct inode	*dir,
	struct dentry	*dentry,
	const char	*symname)
{
	struct inode	*ip;
	vattr_t		va;
	vnode_t		*dvp;	/* directory containing name to remove */
	vnode_t		*cvp;	/* used to lookup symlink to put in dentry */
	int		error;

	dvp = LINVFS_GET_VP(dir);
	cvp = NULL;

	memset(&va, 0, sizeof(va));
	va.va_type = VLNK;
	va.va_mode = irix_symlink_mode ? 0777 & ~current->fs->umask : S_IRWXUGO;
	va.va_mask = XFS_AT_TYPE|XFS_AT_MODE;

	error = 0;
	VOP_SYMLINK(dvp, dentry, &va, (char *)symname, &cvp, NULL, error);
	if (!error && cvp) {
		ASSERT(cvp->v_type == VLNK);
		ip = LINVFS_GET_IP(cvp);
		d_instantiate(dentry, ip);
		validate_fields(dir);
		validate_fields(ip); /* size needs update */
	}
	return -error;
}

STATIC int
linvfs_rmdir(
	struct inode	*dir,
	struct dentry	*dentry)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*dvp = LINVFS_GET_VP(dir);
	int		error;

	VOP_RMDIR(dvp, dentry, NULL, error);
	if (!error) {
		validate_fields(inode);
		validate_fields(dir);
	}
	return -error;
}

STATIC int
linvfs_rename(
	struct inode	*odir,
	struct dentry	*odentry,
	struct inode	*ndir,
	struct dentry	*ndentry)
{
	struct inode	*new_inode = ndentry->d_inode;
	vnode_t		*fvp;	/* from directory */
	vnode_t		*tvp;	/* target directory */
	int		error;

	fvp = LINVFS_GET_VP(odir);
	tvp = LINVFS_GET_VP(ndir);

	VOP_RENAME(fvp, odentry, tvp, ndentry, NULL, error);
	if (error)
		return -error;

	if (new_inode)
		validate_fields(new_inode);

	validate_fields(odir);
	if (ndir != odir)
		validate_fields(ndir);
	return 0;
}

STATIC int
linvfs_readlink(
	struct dentry	*dentry,
	char		*buf,
	int		size)
{
	vnode_t		*vp = LINVFS_GET_VP(dentry->d_inode);
	uio_t		uio;
	iovec_t		iov;
	int		error;

	iov.iov_base = buf;
	iov.iov_len = size;

	uio.uio_iov = &iov;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_USERSPACE;
	uio.uio_resid = size;
	uio.uio_iovcnt = 1;

	VOP_READLINK(vp, &uio, NULL, error);
	if (error)
		return -error;

	return (size - uio.uio_resid);
}

/*
 * careful here - this function can get called recursively, so
 * we need to be very careful about how much stack we use.
 * uio is kmalloced for this reason...
 */
STATIC int
linvfs_follow_link(
	struct dentry		*dentry,
	struct nameidata	*nd)
{
	vnode_t			*vp;
	uio_t			*uio;
	iovec_t			iov;
	int			error;
	char			*link;

	ASSERT(dentry);
	ASSERT(nd);

	link = (char *)kmalloc(MAXNAMELEN+1, GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	uio = (uio_t *)kmalloc(sizeof(uio_t), GFP_KERNEL);
	if (!uio) {
		kfree(link);
		return -ENOMEM;
	}

	vp = LINVFS_GET_VP(dentry->d_inode);

	iov.iov_base = link;
	iov.iov_len = MAXNAMELEN;

	uio->uio_iov = &iov;
	uio->uio_offset = 0;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_resid = MAXNAMELEN;
	uio->uio_fmode = 0;
	uio->uio_iovcnt = 1;

	VOP_READLINK(vp, uio, NULL, error);
	if (error) {
		kfree(uio);
		kfree(link);
		return -error;
	}

	link[MAXNAMELEN - uio->uio_resid] = '\0';
	kfree(uio);

	/* vfs_follow_link returns (-) errors */
	error = vfs_follow_link(nd, link);
	kfree(link);
	return error;
}

STATIC int
linvfs_permission(
	struct inode	*inode,
	int		mode,
	struct nameidata *nd)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error;

	mode <<= 6;		/* convert from linux to vnode access bits */
	VOP_ACCESS(vp, mode, NULL, error);
	return -error;
}

STATIC int
linvfs_getattr(
	struct vfsmount	*mnt,
	struct dentry	*dentry,
	struct kstat	*stat)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	int		error = 0;

	if (unlikely(vp->v_flag & VMODIFIED))
		error = vn_revalidate(vp);
	if (!error)
		generic_fillattr(inode, stat);
	return 0;
}

STATIC int
linvfs_setattr(
	struct dentry	*dentry,
	struct iattr	*attr)
{
	struct inode	*inode = dentry->d_inode;
	unsigned int	ia_valid = attr->ia_valid;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vattr_t		vattr;
	int		flags = 0;
	int		error;

	memset(&vattr, 0, sizeof(vattr_t));
	if (ia_valid & ATTR_UID) {
		vattr.va_mask |= XFS_AT_UID;
		vattr.va_uid = attr->ia_uid;
	}
	if (ia_valid & ATTR_GID) {
		vattr.va_mask |= XFS_AT_GID;
		vattr.va_gid = attr->ia_gid;
	}
	if (ia_valid & ATTR_SIZE) {
		vattr.va_mask |= XFS_AT_SIZE;
		vattr.va_size = attr->ia_size;
	}
	if (ia_valid & ATTR_ATIME) {
		vattr.va_mask |= XFS_AT_ATIME;
		vattr.va_atime = attr->ia_atime;
	}
	if (ia_valid & ATTR_MTIME) {
		vattr.va_mask |= XFS_AT_MTIME;
		vattr.va_mtime = attr->ia_mtime;
	}
	if (ia_valid & ATTR_CTIME) {
		vattr.va_mask |= XFS_AT_CTIME;
		vattr.va_ctime = attr->ia_ctime;
	}
	if (ia_valid & ATTR_MODE) {
		vattr.va_mask |= XFS_AT_MODE;
		vattr.va_mode = attr->ia_mode;
		if (!in_group_p(inode->i_gid) && !capable(CAP_FSETID))
			inode->i_mode &= ~S_ISGID;
	}

	if (ia_valid & (ATTR_MTIME_SET | ATTR_ATIME_SET))
		flags = ATTR_UTIME;

	VOP_SETATTR(vp, &vattr, flags, NULL, error);
	if (error)
		return(-error);	/* Positive error up from XFS */
	if (ia_valid & ATTR_SIZE) {
		error = vmtruncate(inode, attr->ia_size);
	}

	if (!error) {
		vn_revalidate(vp);
	}
	return error;
}

STATIC void
linvfs_truncate(
	struct inode	*inode)
{
	block_truncate_page(inode->i_mapping, inode->i_size, linvfs_get_block);
}



/*
 * Extended attributes interfaces
 */

#define SYSTEM_NAME	"system."	/* VFS shared names/values */
#define ROOT_NAME	"trusted."	/* root's own names/values */
#define USER_NAME	"user."		/* user's own names/values */
STATIC xattr_namespace_t xfs_namespace_array[] = {
	{ .name= SYSTEM_NAME,	.namelen= sizeof(SYSTEM_NAME)-1,.exists= NULL },
	{ .name= ROOT_NAME,	.namelen= sizeof(ROOT_NAME)-1,	.exists= NULL },
	{ .name= USER_NAME,	.namelen= sizeof(USER_NAME)-1,	.exists= NULL },
	{ .name= NULL }
};
xattr_namespace_t *xfs_namespaces = &xfs_namespace_array[0];

#define POSIXACL_ACCESS		"posix_acl_access"
#define POSIXACL_ACCESS_SIZE	(sizeof(POSIXACL_ACCESS)-1)
#define POSIXACL_DEFAULT	"posix_acl_default"
#define POSIXACL_DEFAULT_SIZE	(sizeof(POSIXACL_DEFAULT)-1)
#define POSIXCAP		"posix_capabilities"
#define POSIXCAP_SIZE		(sizeof(POSIXCAP)-1)
#define POSIXMAC		"posix_mac"
#define POSIXMAC_SIZE		(sizeof(POSIXMAC)-1)
STATIC xattr_namespace_t sys_namespace_array[] = {
	{ .name= POSIXACL_ACCESS,
		.namelen= POSIXACL_ACCESS_SIZE,	 .exists= _ACL_ACCESS_EXISTS },
	{ .name= POSIXACL_DEFAULT,
		.namelen= POSIXACL_DEFAULT_SIZE, .exists= _ACL_DEFAULT_EXISTS },
	{ .name= POSIXCAP,
		.namelen= POSIXCAP_SIZE,	 .exists= _CAP_EXISTS },
	{ .name= POSIXMAC,
		.namelen= POSIXMAC_SIZE,	 .exists= _MAC_EXISTS },
	{ .name= NULL }
};

/*
 * Some checks to prevent people abusing EAs to get over quota:
 * - Don't allow modifying user EAs on devices/symlinks;
 * - Don't allow modifying user EAs if sticky bit set;
 */
STATIC int
capable_user_xattr(
	struct inode	*inode)
{
	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode) &&
	    !capable(CAP_SYS_ADMIN))
		return 0;
	if (S_ISDIR(inode->i_mode) && (inode->i_mode & S_ISVTX) &&
	    (current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		return 0;
	return 1;
}

STATIC int
linvfs_setxattr(
	struct dentry	*dentry,
	const char	*name,
	const void	*data,
	size_t		size,
	int		flags)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	char		*p = (char *)name;
	int		xflags = 0;
	int		error;

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -EINVAL;
		if (flags & XATTR_CREATE)
			 return error;
		error = -EOPNOTSUPP;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0)
			error = xfs_acl_vset(vp, (void *) data, size,
						_ACL_TYPE_ACCESS);
		else if (strcmp(p, POSIXACL_DEFAULT) == 0)
			error = xfs_acl_vset(vp, (void *) data, size,
						_ACL_TYPE_DEFAULT);
		else if (strcmp(p, POSIXCAP) == 0)
			error = xfs_cap_vset(vp, (void *) data, size);
		if (!error)
			error = vn_revalidate(vp);
		return error;
	}

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (flags & XATTR_CREATE)
		xflags |= ATTR_CREATE;
	if (flags & XATTR_REPLACE)
		xflags |= ATTR_REPLACE;

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_SET(vp, p, (void *) data, size, xflags, NULL, error);
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		if (!capable_user_xattr(inode))
			return -EPERM;
		p += xfs_namespaces[USER_NAMES].namelen;
		VOP_ATTR_SET(vp, p, (void *) data, size, xflags, NULL, error);
		return -error;
	}
	return -EOPNOTSUPP;
}

STATIC ssize_t
__linvfs_getxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	char		*p = (char *)name;
	int		xflags = 0;
	ssize_t		error;

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -EOPNOTSUPP;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0)
			error = xfs_acl_vget(vp, data, size, _ACL_TYPE_ACCESS);
		else if (strcmp(p, POSIXACL_DEFAULT) == 0)
			error = xfs_acl_vget(vp, data, size, _ACL_TYPE_DEFAULT);
		else if (strcmp(p, POSIXCAP) == 0)
			error = xfs_cap_vget(vp, data, size);
		return error;
	}

	/* Convert Linux syscall to XFS internal ATTR flags */
	if (!size) {
		xflags |= ATTR_KERNOVAL;
		data = NULL;
	}

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_GET(vp, p, data, (int *)&size, xflags, NULL, error);
		if (!error)
			error = -size;
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		p += xfs_namespaces[USER_NAMES].namelen;
		if (!capable_user_xattr(inode))
			return -EPERM;
		VOP_ATTR_GET(vp, p, data, (int *)&size, xflags, NULL, error);
		if (!error)
			error = -size;
		return -error;
	}
	return -EOPNOTSUPP;
}

STATIC ssize_t
linvfs_getxattr(
	struct dentry	*dentry,
	const char	*name,
	void		*data,
	size_t		size)
{
	int error;

	down(&dentry->d_inode->i_sem);
	error = __linvfs_getxattr(dentry, name, data, size);
	up(&dentry->d_inode->i_sem);

	return error;
}

STATIC ssize_t
__linvfs_listxattr(
	struct dentry		*dentry,
	char			*data,
	size_t			size)
{
	attrlist_cursor_kern_t	cursor;
	xattr_namespace_t	*sys;
	vnode_t			*vp = LINVFS_GET_VP(dentry->d_inode);
	char			*k = data;
	int			xflags = ATTR_KERNAMELS;
	int			result = 0;
	ssize_t			error;

	if (!size)
		xflags |= ATTR_KERNOVAL;
	if (capable(CAP_SYS_ADMIN))
		xflags |= ATTR_KERNFULLS;

	memset(&cursor, 0, sizeof(cursor));
	VOP_ATTR_LIST(vp, data, size, xflags, &cursor, NULL, error);
	if (error > 0)
		return -error;
	result += -error;

	k += result;		/* advance start of our buffer */
	for (sys = &sys_namespace_array[0]; sys->name != NULL; sys++) {
		if (sys->exists == NULL || !sys->exists(vp))
			continue;
		result += xfs_namespaces[SYSTEM_NAMES].namelen;
		result += sys->namelen + 1;
		if (size) {
			if (result > size)
				return -ERANGE;
			strcpy(k, xfs_namespaces[SYSTEM_NAMES].name);
			k += xfs_namespaces[SYSTEM_NAMES].namelen;
			strcpy(k, sys->name);
			k += sys->namelen + 1;
		}
	}
	return result;
}

STATIC ssize_t
linvfs_listxattr(
	struct dentry		*dentry,
	char			*data,
	size_t			size)
{
	int error;

	down(&dentry->d_inode->i_sem);
	error = __linvfs_listxattr(dentry, data, size);
	up(&dentry->d_inode->i_sem);

	return error;
}

STATIC int
linvfs_removexattr(
	struct dentry	*dentry,
	const char	*name)
{
	struct inode	*inode = dentry->d_inode;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	char		*p = (char *)name;
	int		xflags = 0;
	int		error;

	if (strncmp(name, xfs_namespaces[SYSTEM_NAMES].name,
			xfs_namespaces[SYSTEM_NAMES].namelen) == 0) {
		error = -EOPNOTSUPP;
		p += xfs_namespaces[SYSTEM_NAMES].namelen;
		if (strcmp(p, POSIXACL_ACCESS) == 0)
			error = xfs_acl_vremove(vp, _ACL_TYPE_ACCESS);
		else if (strcmp(p, POSIXACL_DEFAULT) == 0)
			error = xfs_acl_vremove(vp, _ACL_TYPE_DEFAULT);
		else if (strcmp(p, POSIXCAP) == 0)
			error = xfs_cap_vremove(vp);
		return error;
	}

	if (strncmp(name, xfs_namespaces[ROOT_NAMES].name,
			xfs_namespaces[ROOT_NAMES].namelen) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		xflags |= ATTR_ROOT;
		p += xfs_namespaces[ROOT_NAMES].namelen;
		VOP_ATTR_REMOVE(vp, p, xflags, NULL, error);
		return -error;
	}
	if (strncmp(name, xfs_namespaces[USER_NAMES].name,
			xfs_namespaces[USER_NAMES].namelen) == 0) {
		p += xfs_namespaces[USER_NAMES].namelen;
		if (!capable_user_xattr(inode))
			return -EPERM;
		VOP_ATTR_REMOVE(vp, p, xflags, NULL, error);
		return -error;
	}
	return -EOPNOTSUPP;
}


struct inode_operations linvfs_file_inode_operations =
{
	.permission		= linvfs_permission,
	.truncate		= linvfs_truncate,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_dir_inode_operations =
{
	.create			= linvfs_create,
	.lookup			= linvfs_lookup,
	.link			= linvfs_link,
	.unlink			= linvfs_unlink,
	.symlink		= linvfs_symlink,
	.mkdir			= linvfs_mkdir,
	.rmdir			= linvfs_rmdir,
	.mknod			= linvfs_mknod,
	.rename			= linvfs_rename,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};

struct inode_operations linvfs_symlink_inode_operations =
{
	.readlink		= linvfs_readlink,
	.follow_link		= linvfs_follow_link,
	.permission		= linvfs_permission,
	.getattr		= linvfs_getattr,
	.setattr		= linvfs_setattr,
	.setxattr		= linvfs_setxattr,
	.getxattr		= linvfs_getxattr,
	.listxattr		= linvfs_listxattr,
	.removexattr		= linvfs_removexattr,
};
