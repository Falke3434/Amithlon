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
 * or the like.	 Any license provided herein, whether implied or
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
#ifndef __XFS_QM_H__
#define __XFS_QM_H__

#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_quota_priv.h"
#include "xfs_qm_stats.h"

struct xfs_qm;
struct xfs_inode;

extern mutex_t		xfs_Gqm_lock;
extern struct xfs_qm	*xfs_Gqm;
extern kmem_zone_t	*qm_dqzone;
extern kmem_zone_t	*qm_dqtrxzone;

/*
 * Used in xfs_qm_sync called by xfs_sync to count the max times that it can
 * iterate over the mountpt's dquot list in one call.
 */
#define XFS_QM_SYNC_MAX_RESTARTS	7

/*
 * Ditto, for xfs_qm_dqreclaim_one.
 */
#define XFS_QM_RECLAIM_MAX_RESTARTS	4

/*
 * Ideal ratio of free to in use dquots. Quota manager makes an attempt
 * to keep this balance.
 */
#define XFS_QM_DQFREE_RATIO		2

/*
 * Dquot hashtable constants/threshold values.
 */
#define XFS_QM_NCSIZE_THRESHOLD		5000
#define XFS_QM_HASHSIZE_LOW		32
#define XFS_QM_HASHSIZE_HIGH		64

/*
 * We output a cmn_err when quotachecking a quota file with more than
 * this many fsbs.
 */
#define XFS_QM_BIG_QCHECK_NBLKS		500

/*
 * This defines the unit of allocation of dquots.
 * Currently, it is just one file system block, and a 4K blk contains 30
 * (136 * 30 = 4080) dquots. It's probably not worth trying to make
 * this more dynamic.
 * XXXsup However, if this number is changed, we have to make sure that we don't
 * implicitly assume that we do allocations in chunks of a single filesystem
 * block in the dquot/xqm code.
 */
#define XFS_DQUOT_CLUSTER_SIZE_FSB	(xfs_filblks_t)1
/*
 * When doing a quotacheck, we log dquot clusters of this many FSBs at most
 * in a single transaction. We don't want to ask for too huge a log reservation.
 */
#define XFS_QM_MAX_DQCLUSTER_LOGSZ	3

typedef xfs_dqhash_t	xfs_dqlist_t;
/*
 * The freelist head. The first two fields match the first two in the
 * xfs_dquot_t structure (in xfs_dqmarker_t)
 */
typedef struct xfs_frlist {
       struct xfs_dquot *qh_next;
       struct xfs_dquot *qh_prev;
       mutex_t		 qh_lock;
       uint		 qh_version;
       uint		 qh_nelems;
} xfs_frlist_t;

/*
 * Quota Manager (global) structure. Lives only in core.
 */
typedef struct xfs_qm {
	xfs_dqlist_t	*qm_usr_dqhtable;/* udquot hash table */
	xfs_dqlist_t	*qm_grp_dqhtable;/* gdquot hash table */
	uint		 qm_dqhashmask;	 /* # buckets in dq hashtab - 1 */
	xfs_frlist_t	 qm_dqfreelist;	 /* freelist of dquots */
	atomic_t	 qm_totaldquots; /* total incore dquots */
	uint		 qm_nrefs;	 /* file systems with quota on */
	int		 qm_dqfree_ratio;/* ratio of free to inuse dquots */
	kmem_zone_t	*qm_dqzone;	 /* dquot mem-alloc zone */
	kmem_zone_t	*qm_dqtrxzone;	 /* t_dqinfo of transactions */
} xfs_qm_t;

/*
 * Various quota information for individual filesystems.
 * The mount structure keeps a pointer to this.
 */
typedef struct xfs_quotainfo {
	xfs_inode_t	*qi_uquotaip;	 /* user quota inode */
	xfs_inode_t	*qi_gquotaip;	 /* group quota inode */
	lock_t		 qi_pinlock;	 /* dquot pinning mutex */
	xfs_dqlist_t	 qi_dqlist;	 /* all dquots in filesys */
	int		 qi_dqreclaims;	 /* a change here indicates
					    a removal in the dqlist */
	time_t		 qi_btimelimit;	 /* limit for blks timer */
	time_t		 qi_itimelimit;	 /* limit for inodes timer */
	time_t		 qi_rtbtimelimit;/* limit for rt blks timer */
	xfs_qwarncnt_t	 qi_bwarnlimit;	 /* limit for num warnings */
	xfs_qwarncnt_t	 qi_iwarnlimit;	 /* limit for num warnings */
	mutex_t		 qi_quotaofflock;/* to serialize quotaoff */
	/* Some useful precalculated constants */
	xfs_filblks_t	 qi_dqchunklen;	 /* # BBs in a chunk of dqs */
	uint		 qi_dqperchunk;	 /* # ondisk dqs in above chunk */
} xfs_quotainfo_t;


extern xfs_dqtrxops_t	xfs_trans_dquot_ops;

extern void	xfs_trans_mod_dquot(xfs_trans_t *, xfs_dquot_t *, uint, long);
extern int	xfs_trans_reserve_quota_bydquots(xfs_trans_t *, xfs_mount_t *,
			xfs_dquot_t *, xfs_dquot_t *, long, long, uint);
extern void	xfs_trans_dqjoin(xfs_trans_t *, xfs_dquot_t *);
extern void	xfs_trans_log_dquot(xfs_trans_t *, xfs_dquot_t *);

/*
 * We keep the usr and grp dquots separately so that locking will be easier
 * to do at commit time. All transactions that we know of at this point
 * affect no more than two dquots of one type. Hence, the TRANS_MAXDQS value.
 */
#define XFS_QM_TRANS_MAXDQS		2
typedef struct xfs_dquot_acct {
	xfs_dqtrx_t	dqa_usrdquots[XFS_QM_TRANS_MAXDQS];
	xfs_dqtrx_t	dqa_grpdquots[XFS_QM_TRANS_MAXDQS];
} xfs_dquot_acct_t;

/*
 * Users are allowed to have a usage exceeding their softlimit for
 * a period this long.
 */
#define XFS_QM_BTIMELIMIT	DQ_BTIMELIMIT
#define XFS_QM_RTBTIMELIMIT	DQ_BTIMELIMIT
#define XFS_QM_ITIMELIMIT	DQ_FTIMELIMIT

#define XFS_QM_BWARNLIMIT	5
#define XFS_QM_IWARNLIMIT	5

#define XFS_QM_LOCK(xqm)	(mutex_lock(&xqm##_lock, PINOD))
#define XFS_QM_UNLOCK(xqm)	(mutex_unlock(&xqm##_lock))
#define XFS_QM_HOLD(xqm)	((xqm)->qm_nrefs++)
#define XFS_QM_RELE(xqm)	((xqm)->qm_nrefs--)

extern int		xfs_qm_init_quotainfo(xfs_mount_t *);
extern void		xfs_qm_destroy_quotainfo(xfs_mount_t *);
extern int		xfs_qm_mount_quotas(xfs_mount_t *);
extern void		xfs_qm_mount_quotainit(xfs_mount_t *, uint);
extern void		xfs_qm_unmount_quotadestroy(xfs_mount_t *);
extern int		xfs_qm_unmount_quotas(xfs_mount_t *);
extern int		xfs_qm_write_sb_changes(xfs_mount_t *, __int64_t);
extern int		xfs_qm_sync(xfs_mount_t *, short);

/* dquot stuff */
extern void		xfs_qm_dqunlink(xfs_dquot_t *);
extern boolean_t	xfs_qm_dqalloc_incore(xfs_dquot_t **);
extern int		xfs_qm_dqattach(xfs_inode_t *, uint);
extern void		xfs_qm_dqdetach(xfs_inode_t *);
extern int		xfs_qm_dqpurge_all(xfs_mount_t *, uint);
extern void		xfs_qm_dqrele_all_inodes(xfs_mount_t *, uint);

/* vop stuff */
extern int		xfs_qm_vop_dqalloc(xfs_mount_t *, xfs_inode_t *,
					uid_t, gid_t, uint,
					xfs_dquot_t **, xfs_dquot_t **);
extern void		xfs_qm_vop_dqattach_and_dqmod_newinode(
					xfs_trans_t *, xfs_inode_t *,
					xfs_dquot_t *, xfs_dquot_t *);
extern int		xfs_qm_vop_rename_dqattach(xfs_inode_t **);
extern xfs_dquot_t *	xfs_qm_vop_chown(xfs_trans_t *, xfs_inode_t *,
					xfs_dquot_t **, xfs_dquot_t *);
extern int		xfs_qm_vop_chown_reserve(xfs_trans_t *, xfs_inode_t *,
					xfs_dquot_t *, xfs_dquot_t *, uint);

/* list stuff */
extern void		xfs_qm_freelist_init(xfs_frlist_t *);
extern void		xfs_qm_freelist_destroy(xfs_frlist_t *);
extern void		xfs_qm_freelist_insert(xfs_frlist_t *, xfs_dquot_t *);
extern void		xfs_qm_freelist_append(xfs_frlist_t *, xfs_dquot_t *);
extern void		xfs_qm_freelist_unlink(xfs_dquot_t *);
extern int		xfs_qm_freelist_lock_nowait(xfs_qm_t *);
extern int		xfs_qm_mplist_nowait(xfs_mount_t *);
extern int		xfs_qm_dqhashlock_nowait(xfs_dquot_t *);

/* system call interface */
extern int		xfs_qm_quotactl(bhv_desc_t *, int, int, xfs_caddr_t);

#ifdef DEBUG
extern int		xfs_qm_internalqcheck(xfs_mount_t *);
#else
#define xfs_qm_internalqcheck(mp)	(0)
#endif

#endif /* __XFS_QM_H__ */
