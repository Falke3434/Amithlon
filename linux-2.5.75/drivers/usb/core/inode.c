/*****************************************************************************/

/*
 *	inode.c  --  Inode/Dentry functions for the USB device file system.
 *
 *	Copyright (C) 2000 Thomas Sailer (sailer@ife.ee.ethz.ch)
 *	Copyright (c) 2001,2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *   0.1  04.01.2000  Created
 *   0.2  10.12.2001  converted to use the vfs layer better
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/usb.h>
#include <linux/namei.h>
#include <linux/usbdevice_fs.h>
#include <linux/smp_lock.h>
#include <asm/byteorder.h>

static struct super_operations usbfs_ops;
static struct file_operations default_file_operations;
static struct inode_operations usbfs_dir_inode_operations;
static struct vfsmount *usbdevfs_mount;
static struct vfsmount *usbfs_mount;
static int usbdevfs_mount_count;	/* = 0 */
static int usbfs_mount_count;	/* = 0 */

static struct dentry *devices_usbdevfs_dentry;
static struct dentry *devices_usbfs_dentry;
static int num_buses;	/* = 0 */

static uid_t devuid;	/* = 0 */
static uid_t busuid;	/* = 0 */
static uid_t listuid;	/* = 0 */
static gid_t devgid;	/* = 0 */
static gid_t busgid;	/* = 0 */
static gid_t listgid;	/* = 0 */
static umode_t devmode = S_IWUSR | S_IRUGO;
static umode_t busmode = S_IXUGO | S_IRUGO;
static umode_t listmode = S_IRUGO;

static int parse_options(struct super_block *s, char *data)
{
	char *curopt = NULL, *value;

	while ((curopt = strsep(&data, ",")) != NULL) {
		if (!*curopt)
			continue;
		if ((value = strchr(curopt, '=')) != NULL)
			*value++ = 0;
		if (!strcmp(curopt, "devuid")) {
			if (!value || !value[0])
				return -EINVAL;
			devuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "devgid")) {
			if (!value || !value[0])
				return -EINVAL;
			devgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "devmode")) {
			if (!value || !value[0])
				return -EINVAL;
			devmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busuid")) {
			if (!value || !value[0])
				return -EINVAL;
			busuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busgid")) {
			if (!value || !value[0])
				return -EINVAL;
			busgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "busmode")) {
			if (!value || !value[0])
				return -EINVAL;
			busmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listuid")) {
			if (!value || !value[0])
				return -EINVAL;
			listuid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listgid")) {
			if (!value || !value[0])
				return -EINVAL;
			listgid = simple_strtoul(value, &value, 0);
			if (*value)
				return -EINVAL;
		}
		if (!strcmp(curopt, "listmode")) {
			if (!value || !value[0])
				return -EINVAL;
			listmode = simple_strtoul(value, &value, 0) & S_IRWXUGO;
			if (*value)
				return -EINVAL;
		}
	}

	return 0;
}


/* --------------------------------------------------------------------- */

static struct inode *usbfs_get_inode (struct super_block *sb, int mode, dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = current->fsuid;
		inode->i_gid = current->fsgid;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_rdev = NODEV;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = &default_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &usbfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inode->i_nlink++;
			break;
		}
	}
	return inode; 
}

/* SMP-safe */
static int usbfs_mknod (struct inode *dir, struct dentry *dentry, int mode,
			dev_t dev)
{
	struct inode *inode = usbfs_get_inode(dir->i_sb, mode, dev);
	int error = -EPERM;

	if (dentry->d_inode)
		return -EEXIST;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int usbfs_mkdir (struct inode *dir, struct dentry *dentry, int mode)
{
	int res;

	mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
	res = usbfs_mknod (dir, dentry, mode, 0);
	if (!res)
		dir->i_nlink++;
	return res;
}

static int usbfs_create (struct inode *dir, struct dentry *dentry, int mode)
{
	mode = (mode & S_IALLUGO) | S_IFREG;
	return usbfs_mknod (dir, dentry, mode, 0);
}

static inline int usbfs_positive (struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static int usbfs_empty (struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);

	list_for_each(list, &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);
		if (usbfs_positive(de)) {
			spin_unlock(&dcache_lock);
			return 0;
		}
	}

	spin_unlock(&dcache_lock);
	return 1;
}

static int usbfs_unlink (struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	down(&inode->i_sem);
	dentry->d_inode->i_nlink--;
	dput(dentry);
	up(&inode->i_sem);
	d_delete(dentry);
	return 0;
}

static void d_unhash(struct dentry *dentry)
{
	dget(dentry);
	spin_lock(&dcache_lock);
	switch (atomic_read(&dentry->d_count)) {
	default:
		spin_unlock(&dcache_lock);
		shrink_dcache_parent(dentry);
		spin_lock(&dcache_lock);
		if (atomic_read(&dentry->d_count) != 2)
			break;
	case 2:
		__d_drop(dentry);
	}
	spin_unlock(&dcache_lock);
}

static int usbfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error = -ENOTEMPTY;
	struct inode * inode = dentry->d_inode;

	down(&inode->i_sem);
	d_unhash(dentry);
	if (usbfs_empty(dentry)) {
		dentry->d_inode->i_nlink -= 2;
		dput(dentry);
		inode->i_flags |= S_DEAD;
		dir->i_nlink--;
		error = 0;
	}
	up(&inode->i_sem);
	if (!error)
		d_delete(dentry);
	dput(dentry);
	return error;
}


/* default file operations */
static ssize_t default_read_file (struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t default_write_file (struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return count;
}

static loff_t default_file_lseek (struct file *file, loff_t offset, int orig)
{
	loff_t retval = -EINVAL;

	down(&file->f_dentry->d_inode->i_sem);
	switch(orig) {
	case 0:
		if (offset > 0) {
			file->f_pos = offset;
			retval = file->f_pos;
		} 
		break;
	case 1:
		if ((offset + file->f_pos) > 0) {
			file->f_pos += offset;
			retval = file->f_pos;
		} 
		break;
	default:
		break;
	}
	up(&file->f_dentry->d_inode->i_sem);
	return retval;
}

static int default_open (struct inode *inode, struct file *file)
{
	if (inode->u.generic_ip)
		file->private_data = inode->u.generic_ip;

	return 0;
}

static struct file_operations default_file_operations = {
	.read =		default_read_file,
	.write =	default_write_file,
	.open =		default_open,
	.llseek =	default_file_lseek,
};

static struct inode_operations usbfs_dir_inode_operations = {
	.lookup =	simple_lookup,
};

static struct super_operations usbfs_ops = {
	.statfs =	simple_statfs,
	.drop_inode =	generic_delete_inode,
};

static int usbfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;
	struct dentry *root;

	if (parse_options(sb, data)) {
		warn("usbfs: mount parameter error:");
		return -EINVAL;
	}

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = USBDEVICE_SUPER_MAGIC;
	sb->s_op = &usbfs_ops;
	inode = usbfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!inode) {
		dbg("%s: could not get inode!",__FUNCTION__);
		return -ENOMEM;
	}

	root = d_alloc_root(inode);
	if (!root) {
		dbg("%s: could not get root dentry!",__FUNCTION__);
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;
}

static struct dentry * get_dentry(struct dentry *parent, const char *name)
{               
	struct qstr qstr;

	qstr.name = name;
	qstr.len = strlen(name);
	qstr.hash = full_name_hash(name,qstr.len);
	return lookup_hash(&qstr,parent);
}               


/*
 * fs_create_by_name - create a file, given a name
 * @name:	name of file
 * @mode:	type of file
 * @parent:	dentry of directory to create it in
 * @dentry:	resulting dentry of file
 *
 * This function handles both regular files and directories.
 */
static int fs_create_by_name (const char *name, mode_t mode,
			      struct dentry *parent, struct dentry **dentry)
{
	int error = 0;

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super 
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent ) {
		if (usbfs_mount && usbfs_mount->mnt_sb) {
			parent = usbfs_mount->mnt_sb->s_root;
		}
	}

	if (!parent) {
		dbg("Ah! can not find a parent!");
		return -EFAULT;
	}

	*dentry = NULL;
	down(&parent->d_inode->i_sem);
	*dentry = get_dentry (parent, name);
	if (!IS_ERR(dentry)) {
		if ((mode & S_IFMT) == S_IFDIR)
			error = usbfs_mkdir (parent->d_inode, *dentry, mode);
		else 
			error = usbfs_create (parent->d_inode, *dentry, mode);
	} else
		error = PTR_ERR(dentry);
	up(&parent->d_inode->i_sem);

	return error;
}

static struct dentry *fs_create_file (const char *name, mode_t mode,
				      struct dentry *parent, void *data,
				      struct file_operations *fops,
				      uid_t uid, gid_t gid)
{
	struct dentry *dentry;
	int error;

	dbg("creating file '%s'",name);

	error = fs_create_by_name (name, mode, parent, &dentry);
	if (error) {
		dentry = NULL;
	} else {
		if (dentry->d_inode) {
			if (data)
				dentry->d_inode->u.generic_ip = data;
			if (fops)
				dentry->d_inode->i_fop = fops;
			dentry->d_inode->i_uid = uid;
			dentry->d_inode->i_gid = gid;
		}
	}

	return dentry;
}

static void fs_remove_file (struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	
	if (!parent || !parent->d_inode)
		return;

	down(&parent->d_inode->i_sem);
	if (usbfs_positive(dentry)) {
		if (dentry->d_inode) {
			if (S_ISDIR(dentry->d_inode->i_mode))
				usbfs_rmdir(parent->d_inode, dentry);
			else
				usbfs_unlink(parent->d_inode, dentry);
		dput(dentry);
		}
	}
	up(&parent->d_inode->i_sem);
}

/* --------------------------------------------------------------------- */



/*
 * The usbdevfs name is now deprecated (as of 2.5.1).
 * It will be removed when the 2.7.x development cycle is started.
 * You have been warned :)
 */
static struct file_system_type usbdevice_fs_type;

static struct super_block *usb_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	if (fs_type == &usbdevice_fs_type)
		printk (KERN_INFO "Please use the 'usbfs' filetype instead, "
				"the 'usbdevfs' name is deprecated.\n");

	return get_sb_single(fs_type, flags, data, usbfs_fill_super);
}

static struct file_system_type usbdevice_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"usbdevfs",
	.get_sb =	usb_get_sb,
	.kill_sb =	kill_litter_super,
};

static struct file_system_type usb_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"usbfs",
	.get_sb =	usb_get_sb,
	.kill_sb =	kill_litter_super,
};

/* --------------------------------------------------------------------- */

static int create_special_files (void)
{
	struct dentry *parent;
	int retval;

	/* create the devices special file */
	retval = simple_pin_fs("usbdevfs", &usbdevfs_mount, &usbdevfs_mount_count);
	if (retval) {
		err ("Unable to get usbdevfs mount");
		goto exit;
	}

	retval = simple_pin_fs("usbfs", &usbfs_mount, &usbfs_mount_count);
	if (retval) {
		err ("Unable to get usbfs mount");
		goto error_clean_usbdevfs_mount;
	}

	parent = usbfs_mount->mnt_sb->s_root;
	devices_usbfs_dentry = fs_create_file ("devices",
					       listmode | S_IFREG, parent,
					       NULL, &usbdevfs_devices_fops,
					       listuid, listgid);
	if (devices_usbfs_dentry == NULL) {
		err ("Unable to create devices usbfs file");
		retval = -ENODEV;
		goto error_clean_mounts;
	}

	parent = usbdevfs_mount->mnt_sb->s_root;
	devices_usbdevfs_dentry = fs_create_file ("devices",
						  listmode | S_IFREG, parent,
						  NULL, &usbdevfs_devices_fops,
						  listuid, listgid);
	if (devices_usbdevfs_dentry == NULL) {
		err ("Unable to create devices usbfs file");
		retval = -ENODEV;
		goto error_remove_file;
	}

	goto exit;
	
error_remove_file:
	fs_remove_file (devices_usbfs_dentry);
	devices_usbfs_dentry = NULL;

error_clean_mounts:
	simple_release_fs(&usbfs_mount, &usbfs_mount_count);

error_clean_usbdevfs_mount:
	simple_release_fs(&usbdevfs_mount, &usbdevfs_mount_count);

exit:
	return retval;
}

static void remove_special_files (void)
{
	if (devices_usbdevfs_dentry)
		fs_remove_file (devices_usbdevfs_dentry);
	if (devices_usbfs_dentry)
		fs_remove_file (devices_usbfs_dentry);
	devices_usbdevfs_dentry = NULL;
	devices_usbfs_dentry = NULL;
	simple_release_fs(&usbdevfs_mount, &usbdevfs_mount_count);
	simple_release_fs(&usbfs_mount, &usbfs_mount_count);
}

void usbfs_update_special (void)
{
	struct inode *inode;

	if (devices_usbdevfs_dentry) {
		inode = devices_usbdevfs_dentry->d_inode;
		if (inode)
			inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	if (devices_usbfs_dentry) {
		inode = devices_usbfs_dentry->d_inode;
		if (inode)
			inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
}

void usbfs_add_bus(struct usb_bus *bus)
{
	struct dentry *parent;
	char name[8];
	int retval;

	/* create the special files if this is the first bus added */
	if (num_buses == 0) {
		retval = create_special_files();
		if (retval)
			return;
	}
	++num_buses;

	sprintf (name, "%03d", bus->busnum);

	parent = usbfs_mount->mnt_sb->s_root;
	bus->usbfs_dentry = fs_create_file (name, busmode | S_IFDIR, parent,
					    bus, NULL, busuid, busgid);
	if (bus->usbfs_dentry == NULL) {
		err ("error creating usbfs bus entry");
		return;
	}

	parent = usbdevfs_mount->mnt_sb->s_root;
	bus->usbdevfs_dentry = fs_create_file (name, busmode | S_IFDIR, parent,
					       bus, NULL, busuid, busgid);
	if (bus->usbdevfs_dentry == NULL) {
		err ("error creating usbdevfs bus entry");
		return;
	}

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}


void usbfs_remove_bus(struct usb_bus *bus)
{
	if (bus->usbfs_dentry) {
		fs_remove_file (bus->usbfs_dentry);
		bus->usbfs_dentry = NULL;
	}
	if (bus->usbdevfs_dentry) {
		fs_remove_file (bus->usbdevfs_dentry);
		bus->usbdevfs_dentry = NULL;
	}

	--num_buses;
	if (num_buses <= 0) {
		remove_special_files();
		num_buses = 0;
	}

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

void usbfs_add_device(struct usb_device *dev)
{
	char name[8];
	int i;
	int i_size;

	sprintf (name, "%03d", dev->devnum);
	dev->usbfs_dentry = fs_create_file (name, devmode | S_IFREG,
					    dev->bus->usbfs_dentry, dev,
					    &usbdevfs_device_file_operations,
					    devuid, devgid);
	if (dev->usbfs_dentry == NULL) {
		err ("error creating usbfs device entry");
		return;
	}
	dev->usbdevfs_dentry = fs_create_file (name, devmode | S_IFREG,
					       dev->bus->usbdevfs_dentry, dev,
					       &usbdevfs_device_file_operations,
					       devuid, devgid);
	if (dev->usbdevfs_dentry == NULL) {
		err ("error creating usbdevfs device entry");
		return;
	}

	/* Set the size of the device's file to be
	 * equal to the size of the device descriptors. */
	i_size = sizeof (struct usb_device_descriptor);
	for (i = 0; i < dev->descriptor.bNumConfigurations; ++i) {
		struct usb_config_descriptor *config =
			(struct usb_config_descriptor *)dev->rawdescriptors[i];
		i_size += le16_to_cpu (config->wTotalLength);
	}
	if (dev->usbfs_dentry->d_inode)
		dev->usbfs_dentry->d_inode->i_size = i_size;
	if (dev->usbdevfs_dentry->d_inode)
		dev->usbdevfs_dentry->d_inode->i_size = i_size;

	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

void usbfs_remove_device(struct usb_device *dev)
{
	struct dev_state *ds;
	struct siginfo sinfo;

	if (dev->usbfs_dentry) {
		fs_remove_file (dev->usbfs_dentry);
		dev->usbfs_dentry = NULL;
	}
	if (dev->usbdevfs_dentry) {
		fs_remove_file (dev->usbdevfs_dentry);
		dev->usbdevfs_dentry = NULL;
	}
	while (!list_empty(&dev->filelist)) {
		ds = list_entry(dev->filelist.next, struct dev_state, list);
		list_del_init(&ds->list);
		down_write(&ds->devsem);
		ds->dev = NULL;
		up_write(&ds->devsem);
		if (ds->discsignr) {
			sinfo.si_signo = SIGPIPE;
			sinfo.si_errno = EPIPE;
			sinfo.si_code = SI_ASYNCIO;
			sinfo.si_addr = ds->disccontext;
			send_sig_info(ds->discsignr, &sinfo, ds->disctask);
		}
	}
	usbfs_update_special();
	usbdevfs_conn_disc_event();
}

/* --------------------------------------------------------------------- */

#ifdef CONFIG_PROC_FS		
static struct proc_dir_entry *usbdir = NULL;
#endif	

int __init usbfs_init(void)
{
	int retval;

	retval = usb_register(&usbdevfs_driver);
	if (retval)
		return retval;

	retval = register_filesystem(&usb_fs_type);
	if (retval) {
		usb_deregister(&usbdevfs_driver);
		return retval;
	}
	retval = register_filesystem(&usbdevice_fs_type);
	if (retval) {
		unregister_filesystem(&usb_fs_type);
		usb_deregister(&usbdevfs_driver);
		return retval;
	}

#ifdef CONFIG_PROC_FS		
	/* create mount point for usbdevfs */
	usbdir = proc_mkdir("usb", proc_bus);
#endif	

	return 0;
}

void __exit usbfs_cleanup(void)
{
	usb_deregister(&usbdevfs_driver);
	unregister_filesystem(&usb_fs_type);
	unregister_filesystem(&usbdevice_fs_type);
#ifdef CONFIG_PROC_FS	
	if (usbdir)
		remove_proc_entry("usb", proc_bus);
#endif
}

