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
#ifndef	__XFS_INODE_H__
#define	__XFS_INODE_H__

/*
 * File incore extent information, present for each of data & attr forks.
 */
#define	XFS_INLINE_EXTS	2
#define	XFS_INLINE_DATA	32
typedef struct xfs_ifork {
	int			if_bytes;	/* bytes in if_u1 */
	int			if_real_bytes;	/* bytes allocated in if_u1 */
	xfs_bmbt_block_t	*if_broot;	/* file's incore btree root */
	short			if_broot_bytes;	/* bytes allocated for root */
	unsigned char		if_flags;	/* per-fork flags */
	unsigned char		if_ext_max;	/* max # of extent records */
	xfs_extnum_t		if_lastex;	/* last if_extents used */
	union {
		xfs_bmbt_rec_t	*if_extents;	/* linear map file exts */
		char		*if_data;	/* inline file data */
	} if_u1;
	union {
		xfs_bmbt_rec_t	if_inline_ext[XFS_INLINE_EXTS];
						/* very small file extents */
		char		if_inline_data[XFS_INLINE_DATA];
						/* very small file data */
		xfs_dev_t	if_rdev;	/* dev number if special */
		uuid_t		if_uuid;	/* mount point value */
	} if_u2;
} xfs_ifork_t;

/*
 * Flags for xfs_ichgtime().
 */
#define	XFS_ICHGTIME_MOD	0x1	/* data fork modification timestamp */
#define	XFS_ICHGTIME_ACC	0x2	/* data fork access timestamp */
#define	XFS_ICHGTIME_CHG	0x4	/* inode field change timestamp */

/*
 * Per-fork incore inode flags.
 */
#define	XFS_IFINLINE	0x0001	/* Inline data is read in */
#define	XFS_IFEXTENTS	0x0002	/* All extent pointers are read in */
#define	XFS_IFBROOT	0x0004	/* i_broot points to the bmap b-tree root */

/*
 * Flags for xfs_imap() and xfs_dilocate().
 */
#define	XFS_IMAP_LOOKUP		0x1

/*
 * Maximum number of extent pointers in if_u1.if_extents.
 */
#define	XFS_MAX_INCORE_EXTENTS	32768


#ifdef __KERNEL__
struct bhv_desc;
struct cred;
struct ktrace;
struct vnode;
struct xfs_buf;
struct xfs_bmap_free;
struct xfs_bmbt_irec;
struct xfs_bmbt_block;
struct xfs_inode;
struct xfs_inode_log_item;
struct xfs_mount;
struct xfs_trans;
struct xfs_dquot;


/*
 * This structure is used to communicate which extents of a file
 * were holes when a write started from xfs_write_file() to
 * xfs_strat_read().  This is necessary so that we can know which
 * blocks need to be zeroed when they are read in in xfs_strat_read()
 * if they weren\'t allocated when the buffer given to xfs_strat_read()
 * was mapped.
 *
 * We keep a list of these attached to the inode.  The list is
 * protected by the inode lock and the fact that the io lock is
 * held exclusively by writers.
 */
typedef struct xfs_gap {
	struct xfs_gap	*xg_next;
	xfs_fileoff_t	xg_offset_fsb;
	xfs_extlen_t	xg_count_fsb;
} xfs_gap_t;

typedef struct dm_attrs_s {
	__uint32_t	da_dmevmask;	/* DMIG event mask */
	__uint16_t	da_dmstate;	/* DMIG state info */
	__uint16_t	da_pad;		/* DMIG extra padding */
} dm_attrs_t;

typedef struct xfs_iocore {
	void			*io_obj;	/* pointer to container
						 * inode or dcxvn structure */
	struct xfs_mount	*io_mount;	/* fs mount struct ptr */
#ifdef DEBUG
	mrlock_t		*io_lock;	/* inode IO lock */
	mrlock_t		*io_iolock;	/* inode IO lock */
#endif

	/* I/O state */
	xfs_fsize_t		io_new_size;	/* sz when write completes */

	/* Miscellaneous state. */
	unsigned int		io_flags;	/* IO related flags */

	/* DMAPI state */
	dm_attrs_t		io_dmattrs;

} xfs_iocore_t;

#define        io_dmevmask     io_dmattrs.da_dmevmask
#define        io_dmstate      io_dmattrs.da_dmstate

#define XFS_IO_INODE(io)	((xfs_inode_t *) ((io)->io_obj))
#define XFS_IO_DCXVN(io)	((dcxvn_t *) ((io)->io_obj))

/*
 * Flags in the flags field
 */

#define XFS_IOCORE_RT		0x1

/*
 * xfs_iocore prototypes
 */

extern void xfs_iocore_inode_init(struct xfs_inode *);
extern void xfs_iocore_inode_reinit(struct xfs_inode *);


/*
 * This is the type used in the xfs inode hash table.
 * An array of these is allocated for each mounted
 * file system to hash the inodes for that file system.
 */
typedef struct xfs_ihash {
	struct xfs_inode	*ih_next;
	rwlock_t		ih_lock;
	uint			ih_version;
} xfs_ihash_t;

/*
 * Inode hashing and hash bucket locking.
 */
#define XFS_BUCKETS(mp) (37*(mp)->m_sb.sb_agcount-1)
#define XFS_IHASH(mp,ino) ((mp)->m_ihash + (((uint)ino) % (mp)->m_ihsize))

/*
 * This is the xfs inode cluster hash.  This hash is used by xfs_iflush to
 * find inodes that share a cluster and can be flushed to disk at the same
 * time.
 */

typedef struct xfs_chashlist {
	struct xfs_chashlist	*chl_next;
	struct xfs_inode	*chl_ip;
	xfs_daddr_t		chl_blkno;	/* starting block number of
						 * the cluster */
	struct xfs_buf		*chl_buf;	/* the inode buffer */
} xfs_chashlist_t;

typedef struct xfs_chash {
	xfs_chashlist_t		*ch_list;
	lock_t			ch_lock;
} xfs_chash_t;


/*
 * This is the xfs in-core inode structure.
 * Most of the on-disk inode is embedded in the i_d field.
 *
 * The extent pointers/inline file space, however, are managed
 * separately.  The memory for this information is pointed to by
 * the if_u1 unions depending on the type of the data.
 * This is used to linearize the array of extents for fast in-core
 * access.  This is used until the file's number of extents
 * surpasses XFS_MAX_INCORE_EXTENTS, at which point all extent pointers
 * are accessed through the buffer cache.
 *
 * Other state kept in the in-core inode is used for identification,
 * locking, transactional updating, etc of the inode.
 *
 * Generally, we do not want to hold the i_rlock while holding the
 * i_ilock. Hierarchy is i_iolock followed by i_rlock.
 *
 * xfs_iptr_t contains all the inode fields upto and including the
 * i_mnext and i_mprev fields, it is used as a marker in the inode
 * chain off the mount structure by xfs_sync calls.
 */

typedef struct {
	struct xfs_ihash	*ip_hash;	/* pointer to hash header */
	struct xfs_inode	*ip_next;	/* inode hash link forw */
	struct xfs_inode	*ip_mnext;	/* next inode in mount list */
	struct xfs_inode	*ip_mprev;	/* ptr to prev inode */
	struct xfs_inode	**ip_prevp;	/* ptr to prev i_next */
	struct xfs_mount	*ip_mount;	/* fs mount struct ptr */
} xfs_iptr_t;

typedef struct xfs_inode {
	/* Inode linking and identification information. */
	struct xfs_ihash	*i_hash;	/* pointer to hash header */
	struct xfs_inode	*i_next;	/* inode hash link forw */
	struct xfs_inode	*i_mnext;	/* next inode in mount list */
	struct xfs_inode	*i_mprev;	/* ptr to prev inode */
	struct xfs_inode	**i_prevp;	/* ptr to prev i_next */
	struct xfs_mount	*i_mount;	/* fs mount struct ptr */
	struct list_head	i_reclaim;	/* reclaim list */
	struct bhv_desc		i_bhv_desc;	/* inode behavior descriptor*/
	struct xfs_dquot	*i_udquot;	/* user dquot */
	struct xfs_dquot	*i_gdquot;	/* group dquot */

	/* Inode location stuff */
	xfs_ino_t		i_ino;		/* inode number (agno/agino)*/
	xfs_daddr_t		i_blkno;	/* blkno of inode buffer */
	ushort			i_len;		/* len of inode buffer */
	ushort			i_boffset;	/* off of inode in buffer */

	/* Extent information. */
	xfs_ifork_t		*i_afp;		/* attribute fork pointer */
	xfs_ifork_t		i_df;		/* data fork */

	/* Transaction and locking information. */
	struct xfs_trans	*i_transp;	/* ptr to owning transaction*/
	struct xfs_inode_log_item *i_itemp;	/* logging information */
	mrlock_t		i_lock;		/* inode lock */
	mrlock_t		i_iolock;	/* inode IO lock */
	sema_t			i_flock;	/* inode flush lock */
	atomic_t		i_pincount;	/* inode pin count */
	wait_queue_head_t	i_ipin_wait;	/* inode pinning wait queue */

	/* I/O state */
	xfs_iocore_t		i_iocore;	/* I/O core */

	/* Miscellaneous state. */
	unsigned short		i_flags;	/* see defined flags below */
	unsigned char		i_update_core;	/* timestamps/size is dirty */
	unsigned char		i_update_size;	/* di_size field is dirty */
	unsigned int		i_gen;		/* generation count */
	unsigned int		i_delayed_blks;	/* count of delay alloc blks */

	xfs_dinode_core_t	i_d;		/* most of ondisk inode */
	xfs_chashlist_t		*i_chash;	/* cluster hash list header */
	struct xfs_inode	*i_cnext;	/* cluster hash link forward */
	struct xfs_inode	*i_cprev;	/* cluster hash link backward */

#ifdef DEBUG
	/* Trace buffers per inode. */
	struct ktrace		*i_xtrace;	/* inode extent list trace */
	struct ktrace		*i_btrace;	/* inode bmap btree trace */
	struct ktrace		*i_rwtrace;	/* inode read/write trace */
	struct ktrace		*i_strat_trace;	/* inode strat_write trace */
	struct ktrace		*i_lock_trace;	/* inode lock/unlock trace */
	struct ktrace		*i_dir_trace;	/* inode directory trace */
#endif /* DEBUG */
} xfs_inode_t;

#endif	/* __KERNEL__ */


/*
 * Fork handling.
 */
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_PTR)
xfs_ifork_t *xfs_ifork_ptr(xfs_inode_t *ip, int w);
#define	XFS_IFORK_PTR(ip,w)		xfs_ifork_ptr(ip,w)
#else
#define	XFS_IFORK_PTR(ip,w)   ((w) == XFS_DATA_FORK ? &(ip)->i_df : (ip)->i_afp)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_Q)
int xfs_ifork_q(xfs_inode_t *ip);
#define	XFS_IFORK_Q(ip)			xfs_ifork_q(ip)
#else
#define	XFS_IFORK_Q(ip)			XFS_CFORK_Q(&(ip)->i_d)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_DSIZE)
int xfs_ifork_dsize(xfs_inode_t *ip);
#define	XFS_IFORK_DSIZE(ip)		xfs_ifork_dsize(ip)
#else
#define	XFS_IFORK_DSIZE(ip)		XFS_CFORK_DSIZE(&ip->i_d, ip->i_mount)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_ASIZE)
int xfs_ifork_asize(xfs_inode_t *ip);
#define	XFS_IFORK_ASIZE(ip)		xfs_ifork_asize(ip)
#else
#define	XFS_IFORK_ASIZE(ip)		XFS_CFORK_ASIZE(&ip->i_d, ip->i_mount)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_SIZE)
int xfs_ifork_size(xfs_inode_t *ip, int w);
#define	XFS_IFORK_SIZE(ip,w)		xfs_ifork_size(ip,w)
#else
#define	XFS_IFORK_SIZE(ip,w)		XFS_CFORK_SIZE(&ip->i_d, ip->i_mount, w)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_FORMAT)
int xfs_ifork_format(xfs_inode_t *ip, int w);
#define	XFS_IFORK_FORMAT(ip,w)		xfs_ifork_format(ip,w)
#else
#define	XFS_IFORK_FORMAT(ip,w)		XFS_CFORK_FORMAT(&ip->i_d, w)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_FMT_SET)
void xfs_ifork_fmt_set(xfs_inode_t *ip, int w, int n);
#define	XFS_IFORK_FMT_SET(ip,w,n)	xfs_ifork_fmt_set(ip,w,n)
#else
#define	XFS_IFORK_FMT_SET(ip,w,n)	XFS_CFORK_FMT_SET(&ip->i_d, w, n)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_NEXTENTS)
int xfs_ifork_nextents(xfs_inode_t *ip, int w);
#define	XFS_IFORK_NEXTENTS(ip,w)	xfs_ifork_nextents(ip,w)
#else
#define	XFS_IFORK_NEXTENTS(ip,w)	XFS_CFORK_NEXTENTS(&ip->i_d, w)
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_IFORK_NEXT_SET)
void xfs_ifork_next_set(xfs_inode_t *ip, int w, int n);
#define	XFS_IFORK_NEXT_SET(ip,w,n)	xfs_ifork_next_set(ip,w,n)
#else
#define	XFS_IFORK_NEXT_SET(ip,w,n)	XFS_CFORK_NEXT_SET(&ip->i_d, w, n)
#endif


#ifdef __KERNEL__

/*
 * In-core inode flags.
 */
#define XFS_IGRIO	0x0001  /* inode used for guaranteed rate i/o */
#define XFS_IUIOSZ	0x0002  /* inode i/o sizes have been explicitly set */
#define XFS_IQUIESCE    0x0004  /* we have started quiescing for this inode */
#define XFS_IRECLAIM    0x0008  /* we have started reclaiming this inode    */
#define XFS_IRECLAIMABLE 0x0010 /* inode can be reclaimed */

/*
 * Flags for inode locking.
 */
#define	XFS_IOLOCK_EXCL		0x001
#define	XFS_IOLOCK_SHARED	0x002
#define	XFS_ILOCK_EXCL		0x004
#define	XFS_ILOCK_SHARED	0x008
#define	XFS_IUNLOCK_NONOTIFY	0x010
#define XFS_EXTENT_TOKEN_RD	0x040
#define XFS_SIZE_TOKEN_RD	0x080
#define XFS_EXTSIZE_RD		(XFS_EXTENT_TOKEN_RD|XFS_SIZE_TOKEN_RD)
#define XFS_WILLLEND		0x100	/* Always acquire tokens for lending */
#define XFS_EXTENT_TOKEN_WR	(XFS_EXTENT_TOKEN_RD | XFS_WILLLEND)
#define XFS_SIZE_TOKEN_WR       (XFS_SIZE_TOKEN_RD | XFS_WILLLEND)
#define XFS_EXTSIZE_WR		(XFS_EXTSIZE_RD | XFS_WILLLEND)


#define XFS_LOCK_MASK	\
	(XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED | XFS_ILOCK_EXCL | \
	 XFS_ILOCK_SHARED | XFS_EXTENT_TOKEN_RD | XFS_SIZE_TOKEN_RD | \
	 XFS_WILLLEND)

/*
 * Flags for xfs_iflush()
 */
#define	XFS_IFLUSH_DELWRI_ELSE_SYNC	1
#define	XFS_IFLUSH_DELWRI_ELSE_ASYNC	2
#define	XFS_IFLUSH_SYNC			3
#define	XFS_IFLUSH_ASYNC		4
#define	XFS_IFLUSH_DELWRI		5

/*
 * Flags for xfs_iflush_all.
 */
#define	XFS_FLUSH_ALL		0x1

/*
 * Flags for xfs_itruncate_start().
 */
#define	XFS_ITRUNC_DEFINITE	0x1
#define	XFS_ITRUNC_MAYBE	0x2

/*
 * max file offset is 2^(31+PAGE_SHIFT) - 1 (due to linux page cache)
 *
 * NOTE: XFS itself can handle 2^63 - 1 (largest positive value of xfs_fsize_t)
 * but this is the Linux limit.
 */
#define XFS_MAX_FILE_OFFSET	MAX_LFS_FILESIZE

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ITOV)
struct vnode *xfs_itov(xfs_inode_t *ip);
#define	XFS_ITOV(ip)		xfs_itov(ip)
#else
#define	XFS_ITOV(ip)		BHV_TO_VNODE(XFS_ITOBHV(ip))
#endif
#define	XFS_ITOV_NULL(ip)	BHV_TO_VNODE_NULL(XFS_ITOBHV(ip))
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_ITOBHV)
struct bhv_desc *xfs_itobhv(xfs_inode_t *ip);
#define	XFS_ITOBHV(ip)		xfs_itobhv(ip)
#else
#define	XFS_ITOBHV(ip)		((struct bhv_desc *)(&((ip)->i_bhv_desc)))
#endif
#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_BHVTOI)
xfs_inode_t *xfs_bhvtoi(struct bhv_desc *bhvp);
#define	XFS_BHVTOI(bhvp)	xfs_bhvtoi(bhvp)
#else
#define	XFS_BHVTOI(bhvp)	\
	((xfs_inode_t *)((char *)(bhvp) - \
			 (char *)&(((xfs_inode_t *)0)->i_bhv_desc)))
#endif

#define BHV_IS_XFS(bdp)		(BHV_OPS(bdp) == &xfs_vnodeops)

/*
 * Pick the inode cluster hash bucket
 * (m_chash is the same size as m_ihash)
 */
#define XFS_CHASH(mp,blk) ((mp)->m_chash + (((uint)blk) % (mp)->m_chsize))

/*
 * For multiple groups support: if ISGID bit is set in the parent
 * directory, group of new file is set to that of the parent, and
 * new subdirectory gets ISGID bit from parent.
 */
#define XFS_INHERIT_GID(pip, vfsp)	((pip) != NULL && \
	(((vfsp)->vfs_flag & VFS_GRPID) || ((pip)->i_d.di_mode & ISGID)))

/*
 * xfs_iget.c prototypes.
 */
void		xfs_ihash_init(struct xfs_mount *);
void		xfs_ihash_free(struct xfs_mount *);
void		xfs_chash_init(struct xfs_mount *);
void		xfs_chash_free(struct xfs_mount *);
xfs_inode_t	*xfs_inode_incore(struct xfs_mount *, xfs_ino_t,
				  struct xfs_trans *);
void            xfs_inode_lock_init(xfs_inode_t *, struct vnode *);
int		xfs_iget(struct xfs_mount *, struct xfs_trans *, xfs_ino_t,
			 uint, xfs_inode_t **, xfs_daddr_t);
void		xfs_iput(xfs_inode_t *, uint);
void		xfs_iput_new(xfs_inode_t *, uint);
void		xfs_ilock(xfs_inode_t *, uint);
int		xfs_ilock_nowait(xfs_inode_t *, uint);
void		xfs_iunlock(xfs_inode_t *, uint);
void		xfs_ilock_demote(xfs_inode_t *, uint);
void		xfs_iflock(xfs_inode_t *);
int		xfs_iflock_nowait(xfs_inode_t *);
uint		xfs_ilock_map_shared(xfs_inode_t *);
void		xfs_iunlock_map_shared(xfs_inode_t *, uint);
void		xfs_ifunlock(xfs_inode_t *);
void		xfs_ireclaim(xfs_inode_t *);
int		xfs_finish_reclaim(xfs_inode_t *, int, int);
int		xfs_finish_reclaim_all(struct xfs_mount *, int);

/*
 * xfs_inode.c prototypes.
 */
int		xfs_inotobp(struct xfs_mount *, struct xfs_trans *, xfs_ino_t,
			    xfs_dinode_t **, struct xfs_buf **, int *);
int		xfs_itobp(struct xfs_mount *, struct xfs_trans *,
			  xfs_inode_t *, xfs_dinode_t **, struct xfs_buf **,
			  xfs_daddr_t);
int		xfs_iread(struct xfs_mount *, struct xfs_trans *, xfs_ino_t,
			  xfs_inode_t **, xfs_daddr_t);
int		xfs_iread_extents(struct xfs_trans *, xfs_inode_t *, int);
int		xfs_ialloc(struct xfs_trans *, xfs_inode_t *, mode_t, nlink_t,
			   xfs_dev_t, struct cred *, xfs_prid_t, int,
			   struct xfs_buf **, boolean_t *, xfs_inode_t **);
void		xfs_xlate_dinode_core(xfs_caddr_t, struct xfs_dinode_core *, int,
			   xfs_arch_t);
int		xfs_ifree(struct xfs_trans *, xfs_inode_t *);
int		xfs_atruncate_start(xfs_inode_t *);
void		xfs_itruncate_start(xfs_inode_t *, uint, xfs_fsize_t);
int		xfs_itruncate_finish(struct xfs_trans **, xfs_inode_t *,
				     xfs_fsize_t, int, int);
int		xfs_iunlink(struct xfs_trans *, xfs_inode_t *);
int		xfs_igrow_start(xfs_inode_t *, xfs_fsize_t, struct cred *);
void		xfs_igrow_finish(struct xfs_trans *, xfs_inode_t *,
				 xfs_fsize_t, int);

void		xfs_idestroy_fork(xfs_inode_t *, int);
void		xfs_idestroy(xfs_inode_t *);
void		xfs_idata_realloc(xfs_inode_t *, int, int);
void		xfs_iextract(xfs_inode_t *);
void		xfs_iext_realloc(xfs_inode_t *, int, int);
void		xfs_iroot_realloc(xfs_inode_t *, int, int);
void		xfs_ipin(xfs_inode_t *);
void		xfs_iunpin(xfs_inode_t *);
int		xfs_iextents_copy(xfs_inode_t *, xfs_bmbt_rec_t *, int);
int		xfs_iflush(xfs_inode_t *, uint);
int		xfs_iflush_all(struct xfs_mount *, int);
int		xfs_iaccess(xfs_inode_t *, mode_t, cred_t *);
uint		xfs_iroundup(uint);
void		xfs_ichgtime(xfs_inode_t *, int);
xfs_fsize_t	xfs_file_last_byte(xfs_inode_t *);
void		xfs_lock_inodes(xfs_inode_t **, int, int, uint);

#define xfs_ipincount(ip)	((unsigned int) atomic_read(&ip->i_pincount))

#ifdef DEBUG
void		xfs_isize_check(struct xfs_mount *, xfs_inode_t *, xfs_fsize_t);
#else	/* DEBUG */
#define xfs_isize_check(mp, ip, isize)
#endif	/* DEBUG */

#if defined(DEBUG)
void		xfs_inobp_check(struct xfs_mount *, struct xfs_buf *);
#else
#define	xfs_inobp_check(mp, bp)
#endif /* DEBUG */

extern struct kmem_zone	*xfs_chashlist_zone;
extern struct kmem_zone	*xfs_ifork_zone;
extern struct kmem_zone	*xfs_inode_zone;
extern struct kmem_zone	*xfs_ili_zone;
extern struct vnodeops	xfs_vnodeops;

#ifdef XFS_ILOCK_TRACE
#define XFS_ILOCK_KTRACE_SIZE	32
void	xfs_ilock_trace(xfs_inode_t *ip, int lock, unsigned int lockflags,
			inst_t *ra);
#endif

#endif	/* __KERNEL__ */

#endif	/* __XFS_INODE_H__ */
