/*
 * Copyright (c) 2001-2002 Silicon Graphics, Inc.  All Rights Reserved.
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
#include "xfs_rw.h"
#include <linux/sysctl.h>
#include <linux/proc_fs.h>


static struct ctl_table_header *xfs_table_header;


#ifdef CONFIG_PROC_FS
STATIC int
xfs_stats_clear_proc_handler(
	ctl_table	*ctl,
	int		write,
	struct file	*filp,
	void		*buffer,
	size_t		*lenp)
{
	int		ret, *valp = ctl->data;
	__uint32_t	vn_active;

	ret = proc_doulongvec_minmax(ctl, write, filp, buffer, lenp);

	if (!ret && write && *valp) {
		printk("XFS Clearing xfsstats\n");
		/* save vn_active, it's a universal truth! */
		vn_active = xfsstats.vn_active;
		memset(&xfsstats, 0, sizeof(xfsstats));
		xfsstats.vn_active = vn_active;
		xfs_stats_clear = 0;
	}

	return ret;
}
#endif /* CONFIG_PROC_FS */

STATIC ctl_table xfs_table[] = {
	{XFS_RESTRICT_CHOWN, "restrict_chown", &xfs_params.restrict_chown.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, 
	&xfs_params.restrict_chown.min, &xfs_params.restrict_chown.max},

	{XFS_SGID_INHERIT, "irix_sgid_inherit", &xfs_params.sgid_inherit.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL,
	&xfs_params.sgid_inherit.min, &xfs_params.sgid_inherit.max},

	{XFS_SYMLINK_MODE, "irix_symlink_mode", &xfs_params.symlink_mode.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, 
	&xfs_params.symlink_mode.min, &xfs_params.symlink_mode.max},

	{XFS_PANIC_MASK, "panic_mask", &xfs_params.panic_mask.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, 
	&xfs_params.panic_mask.min, &xfs_params.panic_mask.max},

	{XFS_ERRLEVEL, "error_level", &xfs_params.error_level.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, 
	&xfs_params.error_level.min, &xfs_params.error_level.max},

	{XFS_SYNC_INTERVAL, "sync_interval", &xfs_params.sync_interval.val,
	sizeof(ulong), 0644, NULL, &proc_doulongvec_minmax,
	&sysctl_intvec, NULL, 
	&xfs_params.sync_interval.min, &xfs_params.sync_interval.max},

	/* please keep this the last entry */
#ifdef CONFIG_PROC_FS
	{XFS_STATS_CLEAR, "stats_clear", &xfs_params.stats_clear.val,
	sizeof(ulong), 0644, NULL, &xfs_stats_clear_proc_handler,
	&sysctl_intvec, NULL, 
	&xfs_params.stats_clear.min, &xfs_params.stats_clear.max},
#endif /* CONFIG_PROC_FS */

	{0}
};

STATIC ctl_table xfs_dir_table[] = {
	{FS_XFS, "xfs", NULL, 0, 0555, xfs_table},
	{0}
};

STATIC ctl_table xfs_root_table[] = {
	{CTL_FS, "fs",  NULL, 0, 0555, xfs_dir_table},
	{0}
};

void
xfs_sysctl_register(void)
{
	xfs_table_header = register_sysctl_table(xfs_root_table, 1);
}

void
xfs_sysctl_unregister(void)
{
	if (xfs_table_header)
		unregister_sysctl_table(xfs_table_header);
}
