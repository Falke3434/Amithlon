/*
 * Copyright (c) 2000 Silicon Graphics, Inc.  All Rights Reserved.
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
#ifndef __XFS_STATS_H__
#define __XFS_STATS_H__


#if defined(CONFIG_PROC_FS) && !defined(XFS_STATS_OFF)

/*
 * XFS global statistics
 */
struct xfsstats {
# define XFSSTAT_END_EXTENT_ALLOC	4
	__uint32_t		xs_allocx;
	__uint32_t		xs_allocb;
	__uint32_t		xs_freex;
	__uint32_t		xs_freeb;
# define XFSSTAT_END_ALLOC_BTREE	(XFSSTAT_END_EXTENT_ALLOC+4)
	__uint32_t		xs_abt_lookup;
	__uint32_t		xs_abt_compare;
	__uint32_t		xs_abt_insrec;
	__uint32_t		xs_abt_delrec;
# define XFSSTAT_END_BLOCK_MAPPING	(XFSSTAT_END_ALLOC_BTREE+7)
	__uint32_t		xs_blk_mapr;
	__uint32_t		xs_blk_mapw;
	__uint32_t		xs_blk_unmap;
	__uint32_t		xs_add_exlist;
	__uint32_t		xs_del_exlist;
	__uint32_t		xs_look_exlist;
	__uint32_t		xs_cmp_exlist;
# define XFSSTAT_END_BLOCK_MAP_BTREE	(XFSSTAT_END_BLOCK_MAPPING+4)
	__uint32_t		xs_bmbt_lookup;
	__uint32_t		xs_bmbt_compare;
	__uint32_t		xs_bmbt_insrec;
	__uint32_t		xs_bmbt_delrec;
# define XFSSTAT_END_DIRECTORY_OPS	(XFSSTAT_END_BLOCK_MAP_BTREE+4)
	__uint32_t		xs_dir_lookup;
	__uint32_t		xs_dir_create;
	__uint32_t		xs_dir_remove;
	__uint32_t		xs_dir_getdents;
# define XFSSTAT_END_TRANSACTIONS	(XFSSTAT_END_DIRECTORY_OPS+3)
	__uint32_t		xs_trans_sync;
	__uint32_t		xs_trans_async;
	__uint32_t		xs_trans_empty;
# define XFSSTAT_END_INODE_OPS		(XFSSTAT_END_TRANSACTIONS+7)
	__uint32_t		xs_ig_attempts;
	__uint32_t		xs_ig_found;
	__uint32_t		xs_ig_frecycle;
	__uint32_t		xs_ig_missed;
	__uint32_t		xs_ig_dup;
	__uint32_t		xs_ig_reclaims;
	__uint32_t		xs_ig_attrchg;
# define XFSSTAT_END_LOG_OPS		(XFSSTAT_END_INODE_OPS+5)
	__uint32_t		xs_log_writes;
	__uint32_t		xs_log_blocks;
	__uint32_t		xs_log_noiclogs;
	__uint32_t		xs_log_force;
	__uint32_t		xs_log_force_sleep;
# define XFSSTAT_END_TAIL_PUSHING	(XFSSTAT_END_LOG_OPS+10)
	__uint32_t		xs_try_logspace;
	__uint32_t		xs_sleep_logspace;
	__uint32_t		xs_push_ail;
	__uint32_t		xs_push_ail_success;
	__uint32_t		xs_push_ail_pushbuf;
	__uint32_t		xs_push_ail_pinned;
	__uint32_t		xs_push_ail_locked;
	__uint32_t		xs_push_ail_flushing;
	__uint32_t		xs_push_ail_restarts;
	__uint32_t		xs_push_ail_flush;
# define XFSSTAT_END_WRITE_CONVERT	(XFSSTAT_END_TAIL_PUSHING+2)
	__uint32_t		xs_xstrat_quick;
	__uint32_t		xs_xstrat_split;
# define XFSSTAT_END_READ_WRITE_OPS	(XFSSTAT_END_WRITE_CONVERT+2)
	__uint32_t		xs_write_calls;
	__uint32_t		xs_read_calls;
# define XFSSTAT_END_ATTRIBUTE_OPS	(XFSSTAT_END_READ_WRITE_OPS+4)
	__uint32_t		xs_attr_get;
	__uint32_t		xs_attr_set;
	__uint32_t		xs_attr_remove;
	__uint32_t		xs_attr_list;
# define XFSSTAT_END_INODE_CLUSTER	(XFSSTAT_END_ATTRIBUTE_OPS+3)
	__uint32_t		xs_iflush_count;
	__uint32_t		xs_icluster_flushcnt;
	__uint32_t		xs_icluster_flushinode;
# define XFSSTAT_END_VNODE_OPS		(XFSSTAT_END_INODE_CLUSTER+8)
	__uint32_t		vn_active;	/* # vnodes not on free lists */
	__uint32_t		vn_alloc;	/* # times vn_alloc called */
	__uint32_t		vn_get;		/* # times vn_get called */
	__uint32_t		vn_hold;	/* # times vn_hold called */
	__uint32_t		vn_rele;	/* # times vn_rele called */
	__uint32_t		vn_reclaim;	/* # times vn_reclaim called */
	__uint32_t		vn_remove;	/* # times vn_remove called */
	__uint32_t		vn_free;	/* # times vn_free called */
/* Extra precision counters */
	__uint64_t		xs_xstrat_bytes;
	__uint64_t		xs_write_bytes;
	__uint64_t		xs_read_bytes;
};

extern struct xfsstats xfsstats;

# define XFS_STATS_INC(count)		( (count)++ )
# define XFS_STATS_DEC(count)		( (count)-- )
# define XFS_STATS_ADD(count, inc)	( (count) += (inc) )

extern void xfs_init_procfs(void);
extern void xfs_cleanup_procfs(void);


#else	/* !CONFIG_PROC_FS */

# define XFS_STATS_INC(count)
# define XFS_STATS_DEC(count)
# define XFS_STATS_ADD(count, inc)

static __inline void xfs_init_procfs(void) { };
static __inline void xfs_cleanup_procfs(void) { };

#endif	/* !CONFIG_PROC_FS */

#endif /* __XFS_STATS_H__ */
