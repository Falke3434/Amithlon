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

/*
 * High level interface routines for log manager
 */

#include "xfs.h"
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_ag.h"
#include "xfs_sb.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_dir.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_buf_item.h"
#include "xfs_alloc_btree.h"
#include "xfs_log_recover.h"
#include "xfs_bit.h"
#include "xfs_rw.h"
#include "xfs_trans_priv.h"


#define xlog_write_adv_cnt(ptr, len, off, bytes) \
	{ (ptr) += (bytes); \
	  (len) -= (bytes); \
	  (off) += (bytes);}

/* Local miscellaneous function prototypes */
STATIC int	 xlog_bdstrat_cb(struct xfs_buf *);
STATIC int	 xlog_commit_record(xfs_mount_t *mp, xlog_ticket_t *ticket,
				    xlog_in_core_t **, xfs_lsn_t *);
STATIC xlog_t *  xlog_alloc_log(xfs_mount_t	*mp,
				dev_t		log_dev,
				xfs_daddr_t	blk_offset,
				int		num_bblks);
STATIC int	 xlog_space_left(xlog_t *log, int cycle, int bytes);
STATIC int	 xlog_sync(xlog_t *log, xlog_in_core_t *iclog);
STATIC void	 xlog_unalloc_log(xlog_t *log);
STATIC int	 xlog_write(xfs_mount_t *mp, xfs_log_iovec_t region[],
			    int nentries, xfs_log_ticket_t tic,
			    xfs_lsn_t *start_lsn,
			    xlog_in_core_t **commit_iclog,
			    uint flags);

/* local state machine functions */
STATIC void xlog_state_done_syncing(xlog_in_core_t *iclog, int);
STATIC void xlog_state_do_callback(xlog_t *log,int aborted, xlog_in_core_t *iclog);
STATIC int  xlog_state_get_iclog_space(xlog_t		*log,
				       int		len,
				       xlog_in_core_t	**iclog,
				       xlog_ticket_t	*ticket,
				       int		*continued_write,
				       int		*logoffsetp);
STATIC void xlog_state_put_ticket(xlog_t	*log,
				  xlog_ticket_t *tic);
STATIC int  xlog_state_release_iclog(xlog_t		*log,
				     xlog_in_core_t	*iclog);
STATIC void xlog_state_switch_iclogs(xlog_t		*log,
				     xlog_in_core_t *iclog,
				     int		eventual_size);
STATIC int  xlog_state_sync(xlog_t *log, xfs_lsn_t lsn, uint flags);
STATIC int  xlog_state_sync_all(xlog_t *log, uint flags);
STATIC void xlog_state_want_sync(xlog_t	*log, xlog_in_core_t *iclog);

/* local functions to manipulate grant head */
STATIC int  xlog_grant_log_space(xlog_t		*log,
				 xlog_ticket_t	*xtic);
STATIC void xlog_grant_push_ail(xfs_mount_t	*mp,
				int		need_bytes);
STATIC void xlog_regrant_reserve_log_space(xlog_t	 *log,
					   xlog_ticket_t *ticket);
STATIC int xlog_regrant_write_log_space(xlog_t		*log,
					 xlog_ticket_t  *ticket);
STATIC void xlog_ungrant_log_space(xlog_t	 *log,
				   xlog_ticket_t *ticket);


/* local ticket functions */
STATIC void		xlog_state_ticket_alloc(xlog_t *log);
STATIC xlog_ticket_t	*xlog_ticket_get(xlog_t *log,
					 int	unit_bytes,
					 int	count,
					 char	clientid,
					 uint	flags);
STATIC void		xlog_ticket_put(xlog_t *log, xlog_ticket_t *ticket);

/* local debug functions */
#if defined(DEBUG) && !defined(XLOG_NOLOG)
STATIC void	xlog_verify_dest_ptr(xlog_t *log, __psint_t ptr);
#ifdef XFSDEBUG
STATIC void	xlog_verify_disk_cycle_no(xlog_t *log, xlog_in_core_t *iclog);
#endif
STATIC void	xlog_verify_grant_head(xlog_t *log, int equals);
STATIC void	xlog_verify_iclog(xlog_t *log, xlog_in_core_t *iclog,
				  int count, boolean_t syncing);
STATIC void	xlog_verify_tail_lsn(xlog_t *log, xlog_in_core_t *iclog,
				     xfs_lsn_t tail_lsn);
#else
#define xlog_verify_dest_ptr(a,b)
#define xlog_verify_disk_cycle_no(a,b)
#define xlog_verify_grant_head(a,b)
#define xlog_verify_iclog(a,b,c,d)
#define xlog_verify_tail_lsn(a,b,c)
#endif

int		xlog_iclogs_empty(xlog_t *log);

#ifdef DEBUG
int xlog_do_error = 0;
int xlog_req_num  = 0;
int xlog_error_mod = 33;
#endif

#define XLOG_FORCED_SHUTDOWN(log)	(log->l_flags & XLOG_IO_ERROR)

/*
 * 0 => disable log manager
 * 1 => enable log manager
 * 2 => enable log manager and log debugging
 */
#if defined(XLOG_NOLOG) || defined(DEBUG)
int   xlog_debug = 1;
dev_t xlog_devt  = 0;
#endif

#if defined(XFS_LOG_TRACE)
void
xlog_trace_loggrant(xlog_t *log, xlog_ticket_t *tic, xfs_caddr_t string)
{
	if (! log->l_grant_trace)
		log->l_grant_trace = ktrace_alloc(1024, KM_SLEEP);

	ktrace_enter(log->l_grant_trace,
		     (void *)tic,
		     (void *)log->l_reserve_headq,
		     (void *)log->l_write_headq,
		     (void *)((unsigned long)log->l_grant_reserve_cycle),
		     (void *)((unsigned long)log->l_grant_reserve_bytes),
		     (void *)((unsigned long)log->l_grant_write_cycle),
		     (void *)((unsigned long)log->l_grant_write_bytes),
		     (void *)((unsigned long)log->l_curr_cycle),
		     (void *)((unsigned long)log->l_curr_block),
		     (void *)((unsigned long)CYCLE_LSN(log->l_tail_lsn, ARCH_NOCONVERT)),
		     (void *)((unsigned long)BLOCK_LSN(log->l_tail_lsn, ARCH_NOCONVERT)),
		     (void *)string,
		     (void *)((unsigned long)13),
		     (void *)((unsigned long)14),
		     (void *)((unsigned long)15),
		     (void *)((unsigned long)16));
}

void
xlog_trace_tic(xlog_t *log, xlog_ticket_t *tic)
{
	if (! log->l_trace)
		log->l_trace = ktrace_alloc(256, KM_SLEEP);

	ktrace_enter(log->l_trace,
		     (void *)tic,
		     (void *)((unsigned long)tic->t_curr_res),
		     (void *)((unsigned long)tic->t_unit_res),
		     (void *)((unsigned long)tic->t_ocnt),
		     (void *)((unsigned long)tic->t_cnt),
		     (void *)((unsigned long)tic->t_flags),
		     (void *)((unsigned long)7),
		     (void *)((unsigned long)8),
		     (void *)((unsigned long)9),
		     (void *)((unsigned long)10),
		     (void *)((unsigned long)11),
		     (void *)((unsigned long)12),
		     (void *)((unsigned long)13),
		     (void *)((unsigned long)14),
		     (void *)((unsigned long)15),
		     (void *)((unsigned long)16));
}

void
xlog_trace_iclog(xlog_in_core_t *iclog, uint state)
{
	pid_t pid;

	pid = current_pid();

	if (!iclog->ic_trace)
		iclog->ic_trace = ktrace_alloc(256, KM_SLEEP);
	ktrace_enter(iclog->ic_trace,
		     (void *)((unsigned long)state),
		     (void *)((unsigned long)pid),
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0,
		     (void *)0);
}

#else
#define	xlog_trace_loggrant(log,tic,string)
#define	xlog_trace_iclog(iclog,state)
#endif /* XFS_LOG_TRACE */

/*
 * NOTES:
 *
 *	1. currblock field gets updated at startup and after in-core logs
 *		marked as with WANT_SYNC.
 */

/*
 * This routine is called when a user of a log manager ticket is done with
 * the reservation.  If the ticket was ever used, then a commit record for
 * the associated transaction is written out as a log operation header with
 * no data.  The flag XLOG_TIC_INITED is set when the first write occurs with
 * a given ticket.  If the ticket was one with a permanent reservation, then
 * a few operations are done differently.  Permanent reservation tickets by
 * default don't release the reservation.  They just commit the current
 * transaction with the belief that the reservation is still needed.  A flag
 * must be passed in before permanent reservations are actually released.
 * When these type of tickets are not released, they need to be set into
 * the inited state again.  By doing this, a start record will be written
 * out when the next write occurs.
 */
xfs_lsn_t
xfs_log_done(xfs_mount_t	*mp,
	     xfs_log_ticket_t	xtic,
	     void		**iclog,
	     uint		flags)
{
	xlog_t		*log    = mp->m_log;
	xlog_ticket_t	*ticket = (xfs_log_ticket_t) xtic;
	xfs_lsn_t	lsn	= 0;

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
#endif

	if (XLOG_FORCED_SHUTDOWN(log) ||
	    /*
	     * If nothing was ever written, don't write out commit record.
	     * If we get an error, just continue and give back the log ticket.
	     */
	    (((ticket->t_flags & XLOG_TIC_INITED) == 0) &&
	     (xlog_commit_record(mp, ticket,
				 (xlog_in_core_t **)iclog, &lsn)))) {
		lsn = (xfs_lsn_t) -1;
		if (ticket->t_flags & XLOG_TIC_PERM_RESERV) {
			flags |= XFS_LOG_REL_PERM_RESERV;
		}
	}


	if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) == 0 ||
	    (flags & XFS_LOG_REL_PERM_RESERV)) {
		/*
		 * Release ticket if not permanent reservation or a specifc
		 * request has been made to release a permanent reservation.
		 */
		xlog_ungrant_log_space(log, ticket);
		xlog_state_put_ticket(log, ticket);
	} else {
		xlog_regrant_reserve_log_space(log, ticket);
	}

	/* If this ticket was a permanent reservation and we aren't
	 * trying to release it, reset the inited flags; so next time
	 * we write, a start record will be written out.
	 */
	if ((ticket->t_flags & XLOG_TIC_PERM_RESERV) &&
	    (flags & XFS_LOG_REL_PERM_RESERV) == 0)
		ticket->t_flags |= XLOG_TIC_INITED;

	return lsn;
}	/* xfs_log_done */


/*
 * Force the in-core log to disk.  If flags == XFS_LOG_SYNC,
 *	the force is done synchronously.
 *
 * Asynchronous forces are implemented by setting the WANT_SYNC
 * bit in the appropriate in-core log and then returning.
 *
 * Synchronous forces are implemented with a semaphore.  All callers
 * to force a given lsn to disk will wait on a semaphore attached to the
 * specific in-core log.  When given in-core log finally completes its
 * write to disk, that thread will wake up all threads waiting on the
 * semaphore.
 */
int
xfs_log_force(xfs_mount_t *mp,
	      xfs_lsn_t	  lsn,
	      uint	  flags)
{
	int	rval;
	xlog_t *log = mp->m_log;

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
#endif

	ASSERT(flags & XFS_LOG_FORCE);

	XFS_STATS_INC(xfsstats.xs_log_force);

	if ((log->l_flags & XLOG_IO_ERROR) == 0) {
		if (lsn == 0)
			rval = xlog_state_sync_all(log, flags);
		else
			rval = xlog_state_sync(log, lsn, flags);
	} else {
		rval = XFS_ERROR(EIO);
	}

	return rval;

}	/* xfs_log_force */


/*
 * This function will take a log sequence number and check to see if that
 * lsn has been flushed to disk.  If it has, then the callback function is
 * called with the callback argument.  If the relevant in-core log has not
 * been synced to disk, we add the callback to the callback list of the
 * in-core log.
 */
int
xfs_log_notify(xfs_mount_t	  *mp,		/* mount of partition */
	       void		  *iclog_hndl,	/* iclog to hang callback off */
	       xfs_log_callback_t *cb)
{
	xlog_t *log = mp->m_log;
	xlog_in_core_t	  *iclog = (xlog_in_core_t *)iclog_hndl;
	int	abortflg, spl;

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
#endif
	cb->cb_next = 0;
	spl = LOG_LOCK(log);
	abortflg = (iclog->ic_state & XLOG_STATE_IOERROR);
	if (!abortflg) {
		ASSERT_ALWAYS((iclog->ic_state == XLOG_STATE_ACTIVE) ||
			      (iclog->ic_state == XLOG_STATE_WANT_SYNC));
		cb->cb_next = 0;
		*(iclog->ic_callback_tail) = cb;
		iclog->ic_callback_tail = &(cb->cb_next);
	}
	LOG_UNLOCK(log, spl);
	if (abortflg) {
		cb->cb_func(cb->cb_arg, abortflg);
	}
	return 0;
}	/* xfs_log_notify */

int
xfs_log_release_iclog(xfs_mount_t *mp,
		      void	  *iclog_hndl)
{
	xlog_t *log = mp->m_log;
	xlog_in_core_t	  *iclog = (xlog_in_core_t *)iclog_hndl;

	if (xlog_state_release_iclog(log, iclog)) {
		xfs_force_shutdown(mp, XFS_LOG_IO_ERROR);
		return(EIO);
	}

	return 0;
}

/*
 *  1. Reserve an amount of on-disk log space and return a ticket corresponding
 *	to the reservation.
 *  2. Potentially, push buffers at tail of log to disk.
 *
 * Each reservation is going to reserve extra space for a log record header.
 * When writes happen to the on-disk log, we don't subtract the length of the
 * log record header from any reservation.  By wasting space in each
 * reservation, we prevent over allocation problems.
 */
int
xfs_log_reserve(xfs_mount_t	 *mp,
		int		 unit_bytes,
		int		 cnt,
		xfs_log_ticket_t *ticket,
		__uint8_t	 client,
		uint		 flags)
{
	xlog_t		*log = mp->m_log;
	xlog_ticket_t	*internal_ticket;
	int		retval;

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
#endif
	retval = 0;
	ASSERT(client == XFS_TRANSACTION || client == XFS_LOG);
	ASSERT((flags & XFS_LOG_NOSLEEP) == 0);

	if (XLOG_FORCED_SHUTDOWN(log))
		return XFS_ERROR(EIO);

	XFS_STATS_INC(xfsstats.xs_try_logspace);

	if (*ticket != NULL) {
		ASSERT(flags & XFS_LOG_PERM_RESERV);
		internal_ticket = (xlog_ticket_t *)*ticket;
		xlog_grant_push_ail(mp, internal_ticket->t_unit_res);
		retval = xlog_regrant_write_log_space(log, internal_ticket);
	} else {
		/* may sleep if need to allocate more tickets */
		internal_ticket = xlog_ticket_get(log, unit_bytes, cnt,
						  client, flags);
		*ticket = internal_ticket;
		xlog_grant_push_ail(mp,
				    (internal_ticket->t_unit_res *
				     internal_ticket->t_cnt));
		retval = xlog_grant_log_space(log, internal_ticket);
	}

	return retval;
}	/* xfs_log_reserve */


/*
 * Mount a log filesystem
 *
 * mp		- ubiquitous xfs mount point structure
 * log_dev	- device number of on-disk log device
 * blk_offset	- Start block # where block size is 512 bytes (BBSIZE)
 * num_bblocks	- Number of BBSIZE blocks in on-disk log
 *
 * Return error or zero.
 */
int
xfs_log_mount(xfs_mount_t	*mp,
	      dev_t		log_dev,
	      xfs_daddr_t	blk_offset,
	      int		num_bblks)
{
	if (!(mp->m_flags & XFS_MOUNT_NORECOVERY))
		cmn_err(CE_NOTE, "XFS mounting filesystem %s", mp->m_fsname);
	else {
		cmn_err(CE_NOTE,
			"!Mounting filesystem \"%s\" in no-recovery mode.  Filesystem will be inconsistent.",
			mp->m_fsname);
		ASSERT(XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY);
	}

	mp->m_log = xlog_alloc_log(mp, log_dev, blk_offset, num_bblks);

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug) {
		cmn_err(CE_NOTE, "log dev: 0x%x", log_dev);
		return 0;
	}
#endif
	/*
	 * skip log recovery on a norecovery mount.  pretend it all
	 * just worked.
	 */
	if (!(mp->m_flags & XFS_MOUNT_NORECOVERY)) {
		int	error;
		vfs_t	*vfsp = XFS_MTOVFS(mp);
		int	readonly = (vfsp->vfs_flag & VFS_RDONLY);

		if (readonly)
			vfsp->vfs_flag &= ~VFS_RDONLY;

		error = xlog_recover(mp->m_log, readonly);

		if (readonly)
			vfsp->vfs_flag |= VFS_RDONLY;
		if (error) {
			cmn_err(CE_WARN, "XFS: log mount/recovery failed");
			xlog_unalloc_log(mp->m_log);
			return error;
		}
	}

	/* Normal transactions can now occur */
	mp->m_log->l_flags &= ~XLOG_ACTIVE_RECOVERY;

	/* End mounting message in xfs_log_mount_finish */
	return 0;
}	/* xfs_log_mount */

/*
 * Finish the recovery of the file system.  This is separate from
 * the xfs_log_mount() call, because it depends on the code in
 * xfs_mountfs() to read in the root and real-time bitmap inodes
 * between calling xfs_log_mount() and here.
 *
 * mp		- ubiquitous xfs mount point structure
 */
int
xfs_log_mount_finish(xfs_mount_t *mp, int mfsi_flags)
{
	int	error;

	if (!(mp->m_flags & XFS_MOUNT_NORECOVERY))
		error = xlog_recover_finish(mp->m_log, mfsi_flags);
	else {
		error = 0;
		ASSERT(XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY);
	}

	return error;
}

/*
 * Unmount processing for the log.
 */
int
xfs_log_unmount(xfs_mount_t *mp)
{
	int		error;

	error = xfs_log_unmount_write(mp);
	xfs_log_unmount_dealloc(mp);
	return (error);
}

/*
 * Final log writes as part of unmount.
 *
 * Mark the filesystem clean as unmount happens.  Note that during relocation
 * this routine needs to be executed as part of source-bag while the
 * deallocation must not be done until source-end.
 */

/*
 * Unmount record used to have a string "Unmount filesystem--" in the
 * data section where the "Un" was really a magic number (XLOG_UNMOUNT_TYPE).
 * We just write the magic number now since that particular field isn't
 * currently architecture converted and "nUmount" is a bit foo.
 * As far as I know, there weren't any dependencies on the old behaviour.
 */

int
xfs_log_unmount_write(xfs_mount_t *mp)
{
	xlog_t		 *log = mp->m_log;
	xlog_in_core_t	 *iclog;
#ifdef DEBUG
	xlog_in_core_t	 *first_iclog;
#endif
	xfs_log_iovec_t  reg[1];
	xfs_log_ticket_t tic = 0;
	xfs_lsn_t	 lsn;
	int		 error;
	SPLDECL(s);

	/* the data section must be 32 bit size aligned */
	struct {
	    __uint16_t magic;
	    __uint16_t pad1;
	    __uint32_t pad2; /* may as well make it 64 bits */
	} magic = { XLOG_UNMOUNT_TYPE, 0, 0 };

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (! xlog_debug && xlog_devt == log->l_dev)
		return 0;
#endif

	/*
	 * Don't write out unmount record on read-only mounts.
	 * Or, if we are doing a forced umount (typically because of IO errors).
	 */
	if (XFS_MTOVFS(mp)->vfs_flag & VFS_RDONLY)
		return 0;

	xfs_log_force(mp, 0, XFS_LOG_FORCE|XFS_LOG_SYNC);

#ifdef DEBUG
	first_iclog = iclog = log->l_iclog;
	do {
		if (!(iclog->ic_state & XLOG_STATE_IOERROR)) {
			ASSERT(iclog->ic_state & XLOG_STATE_ACTIVE);
			ASSERT(iclog->ic_offset == 0);
		}
		iclog = iclog->ic_next;
	} while (iclog != first_iclog);
#endif
	if (! (XLOG_FORCED_SHUTDOWN(log))) {
		reg[0].i_addr = (void*)&magic;
		reg[0].i_len  = sizeof(magic);

		error = xfs_log_reserve(mp, 600, 1, &tic, XFS_LOG, 0);
		if (!error) {
			/* remove inited flag */
			((xlog_ticket_t *)tic)->t_flags = 0;
			error = xlog_write(mp, reg, 1, tic, &lsn,
					   NULL, XLOG_UNMOUNT_TRANS);
			/*
			 * At this point, we're umounting anyway,
			 * so there's no point in transitioning log state
			 * to IOERROR. Just continue...
			 */
		}

		if (error) {
			xfs_fs_cmn_err(CE_ALERT, mp,
				"xfs_log_unmount: unmount record failed");
		}


		s = LOG_LOCK(log);
		iclog = log->l_iclog;
		iclog->ic_refcnt++;
		LOG_UNLOCK(log, s);
		xlog_state_want_sync(log, iclog);
		(void) xlog_state_release_iclog(log, iclog);

		s = LOG_LOCK(log);
		if (!(iclog->ic_state == XLOG_STATE_ACTIVE ||
		      iclog->ic_state == XLOG_STATE_DIRTY)) {
			if (!XLOG_FORCED_SHUTDOWN(log)) {
				sv_wait(&iclog->ic_forcesema, PMEM,
					&log->l_icloglock, s);
			} else {
				LOG_UNLOCK(log, s);
			}
		} else {
			LOG_UNLOCK(log, s);
		}
		if (tic)
			xlog_state_put_ticket(log, tic);
	} else {
		/*
		 * We're already in forced_shutdown mode, couldn't
		 * even attempt to write out the unmount transaction.
		 *
		 * Go through the motions of sync'ing and releasing
		 * the iclog, even though no I/O will actually happen,
		 * we need to wait for other log I/O's that may already
		 * be in progress.  Do this as a separate section of
		 * code so we'll know if we ever get stuck here that
		 * we're in this odd situation of trying to unmount
		 * a file system that went into forced_shutdown as
		 * the result of an unmount..
		 */
		s = LOG_LOCK(log);
		iclog = log->l_iclog;
		iclog->ic_refcnt++;
		LOG_UNLOCK(log, s);

		xlog_state_want_sync(log, iclog);
		(void) xlog_state_release_iclog(log, iclog);

		s = LOG_LOCK(log);

		if ( ! (   iclog->ic_state == XLOG_STATE_ACTIVE
			|| iclog->ic_state == XLOG_STATE_DIRTY
			|| iclog->ic_state == XLOG_STATE_IOERROR) ) {

				sv_wait(&iclog->ic_forcesema, PMEM,
					&log->l_icloglock, s);
		} else {
			LOG_UNLOCK(log, s);
		}
	}

	return 0;
}	/* xfs_log_unmount_write */

/*
 * Deallocate log structures for unmount/relocation.
 */
void
xfs_log_unmount_dealloc(xfs_mount_t *mp)
{
	xlog_unalloc_log(mp->m_log);
}

/*
 * Write region vectors to log.  The write happens using the space reservation
 * of the ticket (tic).  It is not a requirement that all writes for a given
 * transaction occur with one call to xfs_log_write().
 */
int
xfs_log_write(xfs_mount_t *	mp,
	      xfs_log_iovec_t	reg[],
	      int		nentries,
	      xfs_log_ticket_t	tic,
	      xfs_lsn_t		*start_lsn)
{
	int	error;
	xlog_t *log = mp->m_log;
#if defined(DEBUG) || defined(XLOG_NOLOG)

	if (! xlog_debug && xlog_devt == log->l_dev) {
		*start_lsn = 0;
		return 0;
	}
#endif
	if (XLOG_FORCED_SHUTDOWN(log))
		return XFS_ERROR(EIO);

	if ((error = xlog_write(mp, reg, nentries, tic, start_lsn, NULL, 0))) {
		xfs_force_shutdown(mp, XFS_LOG_IO_ERROR);
	}
	return (error);
}	/* xfs_log_write */


void
xfs_log_move_tail(xfs_mount_t	*mp,
		  xfs_lsn_t	tail_lsn)
{
	xlog_ticket_t	*tic;
	xlog_t		*log = mp->m_log;
	int		need_bytes, free_bytes, cycle, bytes;
	SPLDECL(s);

#if defined(DEBUG) || defined(XLOG_NOLOG)
	if (!xlog_debug && xlog_devt == log->l_dev)
		return;
#endif
	/* XXXsup tmp */
	if (XLOG_FORCED_SHUTDOWN(log))
		return;
	ASSERT(!XFS_FORCED_SHUTDOWN(mp));

	if (tail_lsn == 0) {
		/* needed since sync_lsn is 64 bits */
		s = LOG_LOCK(log);
		tail_lsn = log->l_last_sync_lsn;
		LOG_UNLOCK(log, s);
	}

	s = GRANT_LOCK(log);

	/* Also an illegal lsn.  1 implies that we aren't passing in a legal
	 * tail_lsn.
	 */
	if (tail_lsn != 1)
		log->l_tail_lsn = tail_lsn;

	if ((tic = log->l_write_headq)) {
#ifdef DEBUG
		if (log->l_flags & XLOG_ACTIVE_RECOVERY)
			panic("Recovery problem");
#endif
		cycle = log->l_grant_write_cycle;
		bytes = log->l_grant_write_bytes;
		free_bytes = xlog_space_left(log, cycle, bytes);
		do {
			ASSERT(tic->t_flags & XLOG_TIC_PERM_RESERV);

			if (free_bytes < tic->t_unit_res && tail_lsn != 1)
				break;
			tail_lsn = 0;
			free_bytes -= tic->t_unit_res;
			sv_signal(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_write_headq);
	}
	if ((tic = log->l_reserve_headq)) {
#ifdef DEBUG
		if (log->l_flags & XLOG_ACTIVE_RECOVERY)
			panic("Recovery problem");
#endif
		cycle = log->l_grant_reserve_cycle;
		bytes = log->l_grant_reserve_bytes;
		free_bytes = xlog_space_left(log, cycle, bytes);
		do {
			if (tic->t_flags & XLOG_TIC_PERM_RESERV)
				need_bytes = tic->t_unit_res*tic->t_cnt;
			else
				need_bytes = tic->t_unit_res;
			if (free_bytes < need_bytes && tail_lsn != 1)
				break;
			tail_lsn = 0;
			free_bytes -= need_bytes;
			sv_signal(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_reserve_headq);
	}
	GRANT_UNLOCK(log, s);
}	/* xfs_log_move_tail */

/*
 * Determine if we have a transaction that has gone to disk
 * that needs to be covered. Log activity needs to be idle (no AIL and
 * nothing in the iclogs). And, we need to be in the right state indicating
 * something has gone out.
 */
int
xfs_log_need_covered(xfs_mount_t *mp)
{
	SPLDECL(s);
	int		needed = 0, gen;
	xlog_t		*log = mp->m_log;
	vfs_t		*vfsp = XFS_MTOVFS(mp);

	if (mp->m_frozen || XFS_FORCED_SHUTDOWN(mp) ||
	    (vfsp->vfs_flag & VFS_RDONLY))
		return 0;

	s = LOG_LOCK(log);
	if (((log->l_covered_state == XLOG_STATE_COVER_NEED) ||
		(log->l_covered_state == XLOG_STATE_COVER_NEED2))
			&& !xfs_trans_first_ail(mp, &gen)
			&& xlog_iclogs_empty(log)) {
		if (log->l_covered_state == XLOG_STATE_COVER_NEED)
			log->l_covered_state = XLOG_STATE_COVER_DONE;
		else {
			ASSERT(log->l_covered_state == XLOG_STATE_COVER_NEED2);
			log->l_covered_state = XLOG_STATE_COVER_DONE2;
		}
		needed = 1;
	}
	LOG_UNLOCK(log, s);
	return(needed);
}

/******************************************************************************
 *
 *	local routines
 *
 ******************************************************************************
 */

/* xfs_trans_tail_ail returns 0 when there is nothing in the list.
 * The log manager must keep track of the last LR which was committed
 * to disk.  The lsn of this LR will become the new tail_lsn whenever
 * xfs_trans_tail_ail returns 0.  If we don't do this, we run into
 * the situation where stuff could be written into the log but nothing
 * was ever in the AIL when asked.  Eventually, we panic since the
 * tail hits the head.
 *
 * We may be holding the log iclog lock upon entering this routine.
 */
xfs_lsn_t
xlog_assign_tail_lsn(xfs_mount_t *mp)
{
	xfs_lsn_t tail_lsn;
	SPLDECL(s);
	xlog_t	  *log = mp->m_log;

	tail_lsn = xfs_trans_tail_ail(mp);
	s = GRANT_LOCK(log);
	if (tail_lsn != 0)
		log->l_tail_lsn = tail_lsn;
	else
		tail_lsn = log->l_tail_lsn = log->l_last_sync_lsn;
	GRANT_UNLOCK(log, s);

	return tail_lsn;
}	/* xlog_assign_tail_lsn */


/*
 * Return the space in the log between the tail and the head.  The head
 * is passed in the cycle/bytes formal parms.  In the special case where
 * the reserve head has wrapped passed the tail, this calculation is no
 * longer valid.  In this case, just return 0 which means there is no space
 * in the log.  This works for all places where this function is called
 * with the reserve head.  Of course, if the write head were to ever
 * wrap the tail, we should blow up.  Rather than catch this case here,
 * we depend on other ASSERTions in other parts of the code.   XXXmiken
 *
 * This code also handles the case where the reservation head is behind
 * the tail.  The details of this case are described below, but the end
 * result is that we return the size of the log as the amount of space left.
 */
int
xlog_space_left(xlog_t *log, int cycle, int bytes)
{
	int free_bytes;
	int tail_bytes;
	int tail_cycle;

	tail_bytes = BBTOB(BLOCK_LSN(log->l_tail_lsn, ARCH_NOCONVERT));
	tail_cycle = CYCLE_LSN(log->l_tail_lsn, ARCH_NOCONVERT);
	if ((tail_cycle == cycle) && (bytes >= tail_bytes)) {
		free_bytes = log->l_logsize - (bytes - tail_bytes);
	} else if ((tail_cycle + 1) < cycle) {
		return 0;
	} else if (tail_cycle < cycle) {
		ASSERT(tail_cycle == (cycle - 1));
		free_bytes = tail_bytes - bytes;
	} else {
		/*
		 * The reservation head is behind the tail.
		 * This can only happen when the AIL is empty so the tail
		 * is equal to the head and the l_roundoff value in the
		 * log structure is taking up the difference between the
		 * reservation head and the tail.  The bytes accounted for
		 * by the l_roundoff field are temporarily 'lost' to the
		 * reservation mechanism, but they are cleaned up when the
		 * log buffers that created them are reused.  These lost
		 * bytes are what allow the reservation head to fall behind
		 * the tail in the case that the log is 'empty'.
		 * In this case we just want to return the size of the
		 * log as the amount of space left.
		 */
/* This assert does not take into account padding from striped log writes *
		ASSERT((tail_cycle == (cycle + 1)) ||
		       ((bytes + log->l_roundoff) >= tail_bytes));
*/
		free_bytes = log->l_logsize;
	}
	return free_bytes;
}	/* xlog_space_left */


/*
 * Log function which is called when an io completes.
 *
 * The log manager needs its own routine, in order to control what
 * happens with the buffer after the write completes.
 */
void
xlog_iodone(xfs_buf_t *bp)
{
	xlog_in_core_t *iclog;
	int		aborted;

	iclog = XFS_BUF_FSPRIVATE(bp, xlog_in_core_t *);
	ASSERT(XFS_BUF_FSPRIVATE2(bp, unsigned long) == (unsigned long) 2);
	XFS_BUF_SET_FSPRIVATE2(bp, (unsigned long)1);
	aborted = 0;

	/*
	 * Race to shutdown the filesystem if we see an error.
	 */
	if (XFS_BUF_GETERROR(bp)) {
		/* Some versions of cpp barf on the recursive definition of
		 * ic_log -> hic_fields.ic_log and expand ic_log twice when
		 * it is passed through two macros.  Workaround for broken cpp
		 */
		struct log *l;
		xfs_ioerror_alert("xlog_iodone",
				  iclog->ic_log->l_mp, bp, XFS_BUF_ADDR(bp));
		XFS_BUF_STALE(bp);
		l = iclog->ic_log;
		xfs_force_shutdown(l->l_mp, XFS_LOG_IO_ERROR);
		/*
		 * This flag will be propagated to the trans-committed
		 * callback routines to let them know that the log-commit
		 * didn't succeed.
		 */
		aborted = XFS_LI_ABORTED;
	} else if (iclog->ic_state & XLOG_STATE_IOERROR) {
		aborted = XFS_LI_ABORTED;
	}
	xlog_state_done_syncing(iclog, aborted);
	if (!(XFS_BUF_ISASYNC(bp))) {
		/*
		 * Corresponding psema() will be done in bwrite().  If we don't
		 * vsema() here, panic.
		 */
		XFS_BUF_V_IODONESEMA(bp);
	}
}	/* xlog_iodone */

/*
 * The bdstrat callback function for log bufs. This gives us a central
 * place to trap bufs in case we get hit by a log I/O error and need to
 * shutdown. Actually, in practice, even when we didn't get a log error,
 * we transition the iclogs to IOERROR state *after* flushing all existing
 * iclogs to disk. This is because we don't want anymore new transactions to be
 * started or completed afterwards.
 */
STATIC int
xlog_bdstrat_cb(struct xfs_buf *bp)
{
	xlog_in_core_t *iclog;

	iclog = XFS_BUF_FSPRIVATE(bp, xlog_in_core_t *);

	if ((iclog->ic_state & XLOG_STATE_IOERROR) == 0) {
	  /* note for irix bstrat will need  struct bdevsw passed
	   * Fix the following macro if the code ever is merged
	   */
	    XFS_bdstrat(bp);
		return 0;
	}

	xfs_buftrace("XLOG__BDSTRAT IOERROR", bp);
	XFS_BUF_ERROR(bp, EIO);
	XFS_BUF_STALE(bp);
	xfs_biodone(bp);
	return (XFS_ERROR(EIO));


}

/*
 * Return size of each in-core log record buffer.
 *
 * Low memory machines only get 2 16KB buffers.  We don't want to waste
 * memory here.  However, all other machines get at least 2 32KB buffers.
 * The number is hard coded because we don't care about the minimum
 * memory size, just 32MB systems.
 *
 * If the filesystem blocksize is too large, we may need to choose a
 * larger size since the directory code currently logs entire blocks.
 * XXXmiken XXXcurtis
 */

STATIC void
xlog_get_iclog_buffer_size(xfs_mount_t	*mp,
			   xlog_t	*log)
{
	int size;
	int xhdrs;

#if defined(DEBUG) || defined(XLOG_NOLOG)
	/*
	 * When logbufs == 0, someone has disabled the log from the FSTAB
	 * file.  This is not a documented feature.  We need to set xlog_debug
	 * to zero (this deactivates the log) and set xlog_devt to the
	 * appropriate dev_t.  Only one filesystem may be affected as such
	 * since this is just a performance hack to test what we might be able
	 * to get if the log were not present.
	 */
	if (mp->m_logbufs == 0) {
		xlog_debug = 0;
		xlog_devt = log->l_dev;
		log->l_iclog_bufs = XLOG_NUM_ICLOGS;
	} else
#endif
	{
		/*
		 * This is the normal path.  If m_logbufs == -1, then the
		 * admin has chosen to use the system defaults for logbuffers.
		 */
		if (mp->m_logbufs == -1)
			log->l_iclog_bufs = XLOG_NUM_ICLOGS;
		else
			log->l_iclog_bufs = mp->m_logbufs;

#if defined(DEBUG) || defined(XLOG_NOLOG)
		/* We are reactivating a filesystem after it was active */
		if (log->l_dev == xlog_devt) {
			xlog_devt = 1;
			xlog_debug = 1;
		}
#endif
	}

	/*
	 * Buffer size passed in from mount system call.
	 */
	if (mp->m_logbsize != -1) {
		size = log->l_iclog_size = mp->m_logbsize;
		log->l_iclog_size_log = 0;
		while (size != 1) {
			log->l_iclog_size_log++;
			size >>= 1;
		}

		if (XFS_SB_VERSION_HASLOGV2(&mp->m_sb)) {
			/* # headers = size / 32K
			 * one header holds cycles from 32K of data
			 */

			xhdrs = mp->m_logbsize / XLOG_HEADER_CYCLE_SIZE;
			if (mp->m_logbsize % XLOG_HEADER_CYCLE_SIZE)
				xhdrs++;
			log->l_iclog_hsize = xhdrs << BBSHIFT;
			log->l_iclog_heads = xhdrs;
		} else {
			ASSERT(mp->m_logbsize <= XLOG_BIG_RECORD_BSIZE);
			log->l_iclog_hsize = BBSIZE;
			log->l_iclog_heads = 1;
		}
		return;
	}

	/*
	 * Special case machines that have less than 32MB of memory.
	 * All machines with more memory use 32KB buffers.
	 */
	if (xfs_physmem <= btoc(32*1024*1024)) {
		/* Don't change; min configuration */
		log->l_iclog_size = XLOG_RECORD_BSIZE;		/* 16k */
		log->l_iclog_size_log = XLOG_RECORD_BSHIFT;
	} else {
		log->l_iclog_size = XLOG_BIG_RECORD_BSIZE;	/* 32k */
		log->l_iclog_size_log = XLOG_BIG_RECORD_BSHIFT;
	}

	/* the default log size is 16k or 32k which is one header sector */
	log->l_iclog_hsize = BBSIZE;
	log->l_iclog_heads = 1;

	/*
	 * For 16KB, we use 3 32KB buffers.  For 32KB block sizes, we use
	 * 4 32KB buffers.  For 64KB block sizes, we use 8 32KB buffers.
	 */
	if (mp->m_sb.sb_blocksize >= 16*1024) {
		log->l_iclog_size = XLOG_BIG_RECORD_BSIZE;
		log->l_iclog_size_log = XLOG_BIG_RECORD_BSHIFT;
		if (mp->m_logbufs == -1) {
			switch (mp->m_sb.sb_blocksize) {
			    case 16*1024:			/* 16 KB */
				log->l_iclog_bufs = 3;
				break;
			    case 32*1024:			/* 32 KB */
				log->l_iclog_bufs = 4;
				break;
			    case 64*1024:			/* 64 KB */
				log->l_iclog_bufs = 8;
				break;
			    default:
				xlog_panic("XFS: Illegal blocksize");
				break;
			}
		}
	}
}	/* xlog_get_iclog_buffer_size */


/*
 * This routine initializes some of the log structure for a given mount point.
 * Its primary purpose is to fill in enough, so recovery can occur.  However,
 * some other stuff may be filled in too.
 */
STATIC xlog_t *
xlog_alloc_log(xfs_mount_t	*mp,
	       dev_t		log_dev,
	       xfs_daddr_t	blk_offset,
	       int		num_bblks)
{
	xlog_t			*log;
	xlog_rec_header_t	*head;
	xlog_in_core_t		**iclogp;
	xlog_in_core_t		*iclog, *prev_iclog=NULL;
	xfs_buf_t		*bp;
	int			i;
	int			iclogsize;

	log = (xlog_t *)kmem_zalloc(sizeof(xlog_t), KM_SLEEP);

	log->l_mp	   = mp;
	log->l_dev	   = log_dev;
	log->l_logsize     = BBTOB(num_bblks);
	log->l_logBBstart  = blk_offset;
	log->l_logBBsize   = num_bblks;
	log->l_covered_state = XLOG_STATE_COVER_IDLE;
	log->l_flags	   |= XLOG_ACTIVE_RECOVERY;

	log->l_prev_block  = -1;
	ASSIGN_ANY_LSN(log->l_tail_lsn, 1, 0, ARCH_NOCONVERT);
	/* log->l_tail_lsn = 0x100000000LL; cycle = 1; current block = 0 */
	log->l_last_sync_lsn = log->l_tail_lsn;
	log->l_curr_cycle  = 1;	    /* 0 is bad since this is initial value */
	log->l_grant_reserve_cycle = 1;
	log->l_grant_write_cycle = 1;

	if (XFS_SB_VERSION_HASLOGV2(&mp->m_sb)) {
		if (mp->m_sb.sb_logsunit <= 1) {
			log->l_stripemask = 1;
		} else {
			log->l_stripemask = 1 <<
				xfs_highbit32(mp->m_sb.sb_logsunit >> BBSHIFT);
		}
	}
	if (XFS_SB_VERSION_HASSECTOR(&mp->m_sb)) {
		log->l_sectbb_log = mp->m_sb.sb_logsectlog - BBSHIFT;
		ASSERT(log->l_sectbb_log <= mp->m_sectbb_log);
		/* for larger sector sizes, must have v2 or external log */
		ASSERT(log->l_sectbb_log == 0 ||
			log->l_logBBstart == 0 ||
			XFS_SB_VERSION_HASLOGV2(&mp->m_sb));
		ASSERT(mp->m_sb.sb_logsectlog >= BBSHIFT);
	}
	log->l_sectbb_mask = (1 << log->l_sectbb_log) - 1;

	xlog_get_iclog_buffer_size(mp, log);

	bp = log->l_xbuf   = XFS_getrbuf(0,mp);	/* get my locked buffer */ /* mp needed for pagebuf/linux only */

	XFS_BUF_SET_TARGET(bp, mp->m_logdev_targp);
	XFS_BUF_SET_SIZE(bp, log->l_iclog_size);
	XFS_BUF_SET_IODONE_FUNC(bp, xlog_iodone);
	XFS_BUF_SET_BDSTRAT_FUNC(bp, xlog_bdstrat_cb);
	XFS_BUF_SET_FSPRIVATE2(bp, (unsigned long)1);
	ASSERT(XFS_BUF_ISBUSY(log->l_xbuf));
	ASSERT(XFS_BUF_VALUSEMA(log->l_xbuf) <= 0);
	spinlock_init(&log->l_icloglock, "iclog");
	spinlock_init(&log->l_grant_lock, "grhead_iclog");
	initnsema(&log->l_flushsema, 0, "ic-flush");
	xlog_state_ticket_alloc(log);  /* wait until after icloglock inited */

	/* log record size must be multiple of BBSIZE; see xlog_rec_header_t */
	ASSERT((XFS_BUF_SIZE(bp) & BBMASK) == 0);

	iclogp = &log->l_iclog;
	/*
	 * The amount of memory to allocate for the iclog structure is
	 * rather funky due to the way the structure is defined.  It is
	 * done this way so that we can use different sizes for machines
	 * with different amounts of memory.  See the definition of
	 * xlog_in_core_t in xfs_log_priv.h for details.
	 */
	iclogsize = log->l_iclog_size;
	ASSERT(log->l_iclog_size >= 4096);
	for (i=0; i < log->l_iclog_bufs; i++) {
		*iclogp = (xlog_in_core_t *)
			  kmem_zalloc(sizeof(xlog_in_core_t), KM_SLEEP);
		iclog = *iclogp;
		iclog->hic_data = (xlog_in_core_2_t *)
			  kmem_alloc(iclogsize, KM_SLEEP);

		iclog->ic_prev = prev_iclog;
		prev_iclog = iclog;
		log->l_iclog_bak[i] = (xfs_caddr_t)&(iclog->ic_header);

		head = &iclog->ic_header;
		memset(head, 0, sizeof(xlog_rec_header_t));
		INT_SET(head->h_magicno, ARCH_CONVERT, XLOG_HEADER_MAGIC_NUM);
		INT_SET(head->h_version, ARCH_CONVERT,
			XFS_SB_VERSION_HASLOGV2(&log->l_mp->m_sb) ? 2 : 1);
		INT_SET(head->h_size, ARCH_CONVERT, log->l_iclog_size);
		/* new fields */
		INT_SET(head->h_fmt, ARCH_CONVERT, XLOG_FMT);
		memcpy(&head->h_fs_uuid, &mp->m_sb.sb_uuid, sizeof(uuid_t));

		bp = iclog->ic_bp = XFS_getrbuf(0,mp);		/* my locked buffer */ /* mp need for pagebuf/linux only */
		XFS_BUF_SET_TARGET(bp, mp->m_logdev_targp);
		XFS_BUF_SET_SIZE(bp, log->l_iclog_size);
		XFS_BUF_SET_IODONE_FUNC(bp, xlog_iodone);
		XFS_BUF_SET_BDSTRAT_FUNC(bp, xlog_bdstrat_cb);
		XFS_BUF_SET_FSPRIVATE2(bp, (unsigned long)1);

		iclog->ic_size = XFS_BUF_SIZE(bp) - log->l_iclog_hsize;
		iclog->ic_state = XLOG_STATE_ACTIVE;
		iclog->ic_log = log;
		iclog->ic_callback_tail = &(iclog->ic_callback);
		iclog->ic_datap = (char *)iclog->hic_data + log->l_iclog_hsize;

		ASSERT(XFS_BUF_ISBUSY(iclog->ic_bp));
		ASSERT(XFS_BUF_VALUSEMA(iclog->ic_bp) <= 0);
		sv_init(&iclog->ic_forcesema, SV_DEFAULT, "iclog-force");
		sv_init(&iclog->ic_writesema, SV_DEFAULT, "iclog-write");

		iclogp = &iclog->ic_next;
	}
	*iclogp = log->l_iclog;			/* complete ring */
	log->l_iclog->ic_prev = prev_iclog;	/* re-write 1st prev ptr */

	return log;
}	/* xlog_alloc_log */


/*
 * Write out the commit record of a transaction associated with the given
 * ticket.  Return the lsn of the commit record.
 */
STATIC int
xlog_commit_record(xfs_mount_t  *mp,
		   xlog_ticket_t *ticket,
		   xlog_in_core_t **iclog,
		   xfs_lsn_t	*commitlsnp)
{
	int		error;
	xfs_log_iovec_t	reg[1];

	reg[0].i_addr = 0;
	reg[0].i_len = 0;

	ASSERT_ALWAYS(iclog);
	if ((error = xlog_write(mp, reg, 1, ticket, commitlsnp,
			       iclog, XLOG_COMMIT_TRANS))) {
		xfs_force_shutdown(mp, XFS_LOG_IO_ERROR);
	}
	return (error);
}	/* xlog_commit_record */


/*
 * Push on the buffer cache code if we ever use more than 75% of the on-disk
 * log space.  This code pushes on the lsn which would supposedly free up
 * the 25% which we want to leave free.  We may need to adopt a policy which
 * pushes on an lsn which is further along in the log once we reach the high
 * water mark.  In this manner, we would be creating a low water mark.
 */
void
xlog_grant_push_ail(xfs_mount_t	*mp,
		    int		need_bytes)
{
    xlog_t	*log = mp->m_log;	/* pointer to the log */
    xfs_lsn_t	tail_lsn;		/* lsn of the log tail */
    xfs_lsn_t	threshold_lsn = 0;	/* lsn we'd like to be at */
    int		free_blocks;		/* free blocks left to write to */
    int		free_bytes;		/* free bytes left to write to */
    int		threshold_block;	/* block in lsn we'd like to be at */
    int		threshold_cycle;	/* lsn cycle we'd like to be at */
    int		free_threshold;
    SPLDECL(s);

    ASSERT(BTOBB(need_bytes) < log->l_logBBsize);

    s = GRANT_LOCK(log);
    free_bytes = xlog_space_left(log,
				 log->l_grant_reserve_cycle,
				 log->l_grant_reserve_bytes);
    tail_lsn = log->l_tail_lsn;
    free_blocks = BTOBBT(free_bytes);

    /*
     * Set the threshold for the minimum number of free blocks in the
     * log to the maximum of what the caller needs, one quarter of the
     * log, and 256 blocks.
     */
    free_threshold = BTOBB(need_bytes);
    free_threshold = MAX(free_threshold, (log->l_logBBsize >> 2));
    free_threshold = MAX(free_threshold, 256);
    if (free_blocks < free_threshold) {
	threshold_block = BLOCK_LSN(tail_lsn, ARCH_NOCONVERT) + free_threshold;
	threshold_cycle = CYCLE_LSN(tail_lsn, ARCH_NOCONVERT);
	if (threshold_block >= log->l_logBBsize) {
	    threshold_block -= log->l_logBBsize;
	    threshold_cycle += 1;
	}
	ASSIGN_ANY_LSN(threshold_lsn, threshold_cycle,
		       threshold_block, ARCH_NOCONVERT);

	/* Don't pass in an lsn greater than the lsn of the last
	 * log record known to be on disk.
	 */
	if (XFS_LSN_CMP_ARCH(threshold_lsn, log->l_last_sync_lsn, ARCH_NOCONVERT) > 0)
	    threshold_lsn = log->l_last_sync_lsn;
    }
    GRANT_UNLOCK(log, s);

    /*
     * Get the transaction layer to kick the dirty buffers out to
     * disk asynchronously. No point in trying to do this if
     * the filesystem is shutting down.
     */
    if (threshold_lsn &&
	!XLOG_FORCED_SHUTDOWN(log))
	    xfs_trans_push_ail(mp, threshold_lsn);
}	/* xlog_grant_push_ail */


/*
 * Flush out the in-core log (iclog) to the on-disk log in a synchronous or
 * asynchronous fashion.  Previously, we should have moved the current iclog
 * ptr in the log to point to the next available iclog.  This allows further
 * write to continue while this code syncs out an iclog ready to go.
 * Before an in-core log can be written out, the data section must be scanned
 * to save away the 1st word of each BBSIZE block into the header.  We replace
 * it with the current cycle count.  Each BBSIZE block is tagged with the
 * cycle count because there in an implicit assumption that drives will
 * guarantee that entire 512 byte blocks get written at once.  In other words,
 * we can't have part of a 512 byte block written and part not written.  By
 * tagging each block, we will know which blocks are valid when recovering
 * after an unclean shutdown.
 *
 * This routine is single threaded on the iclog.  No other thread can be in
 * this routine with the same iclog.  Changing contents of iclog can there-
 * fore be done without grabbing the state machine lock.  Updating the global
 * log will require grabbing the lock though.
 *
 * The entire log manager uses a logical block numbering scheme.  Only
 * log_sync (and then only bwrite()) know about the fact that the log may
 * not start with block zero on a given device.  The log block start offset
 * is added immediately before calling bwrite().
 */

int
xlog_sync(xlog_t		*log,
	  xlog_in_core_t	*iclog)
{
	xfs_caddr_t	dptr;		/* pointer to byte sized element */
	xfs_buf_t	*bp;
	int		i, ops;
	uint		roundup;
	uint		count;		/* byte count of bwrite */
	int		split = 0;	/* split write into two regions */
	int		error;

	XFS_STATS_INC(xfsstats.xs_log_writes);
	ASSERT(iclog->ic_refcnt == 0);

	/* Round out the log write size */
	if (iclog->ic_offset & BBMASK) {
		/* count of 0 is already accounted for up in
		 * xlog_state_sync_all().  Once in this routine,
		 * operations on the iclog are single threaded.
		 *
		 * Difference between rounded up size and size
		 */
		count = iclog->ic_offset & BBMASK;
		iclog->ic_roundoff += BBSIZE - count;
	}
	if (XFS_SB_VERSION_HASLOGV2(&log->l_mp->m_sb)) {
		unsigned sunit = BTOBB(log->l_mp->m_sb.sb_logsunit);
		if (!sunit)
			sunit = 1;

		count = BTOBB(log->l_iclog_hsize + iclog->ic_offset);
		if (count & (sunit - 1)) {
			roundup = sunit - (count & (sunit - 1));
		} else {
			roundup = 0;
		}
		iclog->ic_offset += BBTOB(roundup);
	}

	log->l_roundoff += iclog->ic_roundoff;

	xlog_pack_data(log, iclog);       /* put cycle number in every block */

	/* real byte length */
	INT_SET(iclog->ic_header.h_len, ARCH_CONVERT, iclog->ic_offset);
	/* put ops count in correct order */
	ops = iclog->ic_header.h_num_logops;
	INT_SET(iclog->ic_header.h_num_logops, ARCH_CONVERT, ops);

	bp	    = iclog->ic_bp;
	ASSERT(XFS_BUF_FSPRIVATE2(bp, unsigned long) == (unsigned long)1);
	XFS_BUF_SET_FSPRIVATE2(bp, (unsigned long)2);
	XFS_BUF_SET_ADDR(bp, BLOCK_LSN(iclog->ic_header.h_lsn, ARCH_CONVERT));

	/* Count is already rounded up to a BBSIZE above */
	count = iclog->ic_offset + iclog->ic_roundoff;
	ASSERT((count & BBMASK) == 0);

	/* Add for LR header */
	count += log->l_iclog_hsize;
	XFS_STATS_ADD(xfsstats.xs_log_blocks, BTOBB(count));

	/* Do we need to split this write into 2 parts? */
	if (XFS_BUF_ADDR(bp) + BTOBB(count) > log->l_logBBsize) {
		split = count - (BBTOB(log->l_logBBsize - XFS_BUF_ADDR(bp)));
		count = BBTOB(log->l_logBBsize - XFS_BUF_ADDR(bp));
		iclog->ic_bwritecnt = 2;	/* split into 2 writes */
	} else {
		iclog->ic_bwritecnt = 1;
	}
	XFS_BUF_SET_PTR(bp, (xfs_caddr_t) &(iclog->ic_header), count);
	XFS_BUF_SET_FSPRIVATE(bp, iclog);	/* save for later */
	XFS_BUF_BUSY(bp);
	XFS_BUF_ASYNC(bp);
	/*
	 * Do a disk write cache flush for the log block.
	 * This is a bit of a sledgehammer, it would be better
	 * to use a tag barrier here that just prevents reordering.
	 * It may not be needed to flush the first split block in the log wrap
	 * case, but do it anyways to be safe -AK
	 */
	if (!(log->l_mp->m_flags & XFS_MOUNT_NOLOGFLUSH))
		XFS_BUF_FLUSH(bp);

	ASSERT(XFS_BUF_ADDR(bp) <= log->l_logBBsize-1);
	ASSERT(XFS_BUF_ADDR(bp) + BTOBB(count) <= log->l_logBBsize);

	xlog_verify_iclog(log, iclog, count, B_TRUE);

	/* account for log which doesn't start at block #0 */
	XFS_BUF_SET_ADDR(bp, XFS_BUF_ADDR(bp) + log->l_logBBstart);
	/*
	 * Don't call xfs_bwrite here. We do log-syncs even when the filesystem
	 * is shutting down.
	 */
	XFS_BUF_WRITE(bp);

	if ((error = XFS_bwrite(bp))) {
		xfs_ioerror_alert("xlog_sync", log->l_mp, bp,
				  XFS_BUF_ADDR(bp));
		return (error);
	}
	if (split) {
		bp		= iclog->ic_log->l_xbuf;
		ASSERT(XFS_BUF_FSPRIVATE2(bp, unsigned long) ==
							(unsigned long)1);
		XFS_BUF_SET_FSPRIVATE2(bp, (unsigned long)2);
		XFS_BUF_SET_ADDR(bp, 0);	     /* logical 0 */
		XFS_BUF_SET_PTR(bp, (xfs_caddr_t)((__psint_t)&(iclog->ic_header)+
					    (__psint_t)count), split);
		XFS_BUF_SET_FSPRIVATE(bp, iclog);
		XFS_BUF_BUSY(bp);
		XFS_BUF_ASYNC(bp);
		if (!(log->l_mp->m_flags & XFS_MOUNT_NOLOGFLUSH))
			XFS_BUF_FLUSH(bp);
		dptr = XFS_BUF_PTR(bp);
		/*
		 * Bump the cycle numbers at the start of each block
		 * since this part of the buffer is at the start of
		 * a new cycle.  Watch out for the header magic number
		 * case, though.
		 */
		for (i=0; i<split; i += BBSIZE) {
			INT_MOD(*(uint *)dptr, ARCH_CONVERT, +1);
			if (INT_GET(*(uint *)dptr, ARCH_CONVERT) == XLOG_HEADER_MAGIC_NUM)
				INT_MOD(*(uint *)dptr, ARCH_CONVERT, +1);
			dptr += BBSIZE;
		}

		ASSERT(XFS_BUF_ADDR(bp) <= log->l_logBBsize-1);
		ASSERT(XFS_BUF_ADDR(bp) + BTOBB(count) <= log->l_logBBsize);

		/* account for internal log which does't start at block #0 */
		XFS_BUF_SET_ADDR(bp, XFS_BUF_ADDR(bp) + log->l_logBBstart);
		XFS_BUF_WRITE(bp);
		if ((error = XFS_bwrite(bp))) {
			xfs_ioerror_alert("xlog_sync (split)", log->l_mp,
					  bp, XFS_BUF_ADDR(bp));
			return (error);
		}
	}
	return (0);
}	/* xlog_sync */


/*
 * Unallocate a log structure
 */
void
xlog_unalloc_log(xlog_t *log)
{
	xlog_in_core_t	*iclog, *next_iclog;
	xlog_ticket_t	*tic, *next_tic;
	int		i;


	iclog = log->l_iclog;
	for (i=0; i<log->l_iclog_bufs; i++) {
		sv_destroy(&iclog->ic_forcesema);
		sv_destroy(&iclog->ic_writesema);
		XFS_freerbuf(iclog->ic_bp);
#ifdef DEBUG
		if (iclog->ic_trace != NULL) {
			ktrace_free(iclog->ic_trace);
		}
#endif
		next_iclog = iclog->ic_next;
		kmem_free(iclog->hic_data, log->l_iclog_size);
		kmem_free(iclog, sizeof(xlog_in_core_t));
		iclog = next_iclog;
	}
	freesema(&log->l_flushsema);
	spinlock_destroy(&log->l_icloglock);
	spinlock_destroy(&log->l_grant_lock);

	/* XXXsup take a look at this again. */
	if ((log->l_ticket_cnt != log->l_ticket_tcnt)  &&
	    !XLOG_FORCED_SHUTDOWN(log)) {
		xfs_fs_cmn_err(CE_WARN, log->l_mp,
			"xlog_unalloc_log: (cnt: %d, total: %d)",
			log->l_ticket_cnt, log->l_ticket_tcnt);
		/* ASSERT(log->l_ticket_cnt == log->l_ticket_tcnt); */

	} else {
		tic = log->l_unmount_free;
		while (tic) {
			next_tic = tic->t_next;
			kmem_free(tic, NBPP);
			tic = next_tic;
		}
	}
	XFS_freerbuf(log->l_xbuf);
#ifdef DEBUG
	if (log->l_trace != NULL) {
		ktrace_free(log->l_trace);
	}
	if (log->l_grant_trace != NULL) {
		ktrace_free(log->l_grant_trace);
	}
#endif
	log->l_mp->m_log = NULL;
	kmem_free(log, sizeof(xlog_t));
}	/* xlog_unalloc_log */

/*
 * Update counters atomically now that memcpy is done.
 */
/* ARGSUSED */
static inline void
xlog_state_finish_copy(xlog_t		*log,
		       xlog_in_core_t	*iclog,
		       int		record_cnt,
		       int		copy_bytes)
{
	SPLDECL(s);

	s = LOG_LOCK(log);

	iclog->ic_header.h_num_logops += record_cnt;
	iclog->ic_offset += copy_bytes;

	LOG_UNLOCK(log, s);
}	/* xlog_state_finish_copy */




/*
 * Write some region out to in-core log
 *
 * This will be called when writing externally provided regions or when
 * writing out a commit record for a given transaction.
 *
 * General algorithm:
 *	1. Find total length of this write.  This may include adding to the
 *		lengths passed in.
 *	2. Check whether we violate the tickets reservation.
 *	3. While writing to this iclog
 *	    A. Reserve as much space in this iclog as can get
 *	    B. If this is first write, save away start lsn
 *	    C. While writing this region:
 *		1. If first write of transaction, write start record
 *		2. Write log operation header (header per region)
 *		3. Find out if we can fit entire region into this iclog
 *		4. Potentially, verify destination memcpy ptr
 *		5. Memcpy (partial) region
 *		6. If partial copy, release iclog; otherwise, continue
 *			copying more regions into current iclog
 *	4. Mark want sync bit (in simulation mode)
 *	5. Release iclog for potential flush to on-disk log.
 *
 * ERRORS:
 * 1.	Panic if reservation is overrun.  This should never happen since
 *	reservation amounts are generated internal to the filesystem.
 * NOTES:
 * 1. Tickets are single threaded data structures.
 * 2. The XLOG_END_TRANS & XLOG_CONTINUE_TRANS flags are passed down to the
 *	syncing routine.  When a single log_write region needs to span
 *	multiple in-core logs, the XLOG_CONTINUE_TRANS bit should be set
 *	on all log operation writes which don't contain the end of the
 *	region.  The XLOG_END_TRANS bit is used for the in-core log
 *	operation which contains the end of the continued log_write region.
 * 3. When xlog_state_get_iclog_space() grabs the rest of the current iclog,
 *	we don't really know exactly how much space will be used.  As a result,
 *	we don't update ic_offset until the end when we know exactly how many
 *	bytes have been written out.
 */
int
xlog_write(xfs_mount_t *	mp,
	   xfs_log_iovec_t	reg[],
	   int			nentries,
	   xfs_log_ticket_t	tic,
	   xfs_lsn_t		*start_lsn,
	   xlog_in_core_t	**commit_iclog,
	   uint			flags)
{
    xlog_t	     *log    = mp->m_log;
    xlog_ticket_t    *ticket = (xlog_ticket_t *)tic;
    xlog_op_header_t *logop_head;    /* ptr to log operation header */
    xlog_in_core_t   *iclog;	     /* ptr to current in-core log */
    __psint_t	     ptr;	     /* copy address into data region */
    int		     len;	     /* # xlog_write() bytes 2 still copy */
    int		     index;	     /* region index currently copying */
    int		     log_offset;     /* offset (from 0) into data region */
    int		     start_rec_copy; /* # bytes to copy for start record */
    int		     partial_copy;   /* did we split a region? */
    int		     partial_copy_len;/* # bytes copied if split region */
    int		     need_copy;	     /* # bytes need to memcpy this region */
    int		     copy_len;	     /* # bytes actually memcpy'ing */
    int		     copy_off;	     /* # bytes from entry start */
    int		     contwr;	     /* continued write of in-core log? */
    int		     firstwr = 0;    /* first write of transaction */
    int		     error;
    int		     record_cnt = 0, data_cnt = 0;

    partial_copy_len = partial_copy = 0;

    /* Calculate potential maximum space.  Each region gets its own
     * xlog_op_header_t and may need to be double word aligned.
     */
    len = 0;
    if (ticket->t_flags & XLOG_TIC_INITED)     /* acct for start rec of xact */
	len += sizeof(xlog_op_header_t);

    for (index = 0; index < nentries; index++) {
	len += sizeof(xlog_op_header_t);	    /* each region gets >= 1 */
	len += reg[index].i_len;
    }
    contwr = *start_lsn = 0;

    if (ticket->t_curr_res < len) {
#ifdef DEBUG
	xlog_panic(
		"xfs_log_write: reservation ran out. Need to up reservation");
#else
	/* Customer configurable panic */
	xfs_cmn_err(XFS_PTAG_LOGRES, CE_ALERT, mp,
		"xfs_log_write: reservation ran out. Need to up reservation");
	/* If we did not panic, shutdown the filesystem */
	xfs_force_shutdown(mp, XFS_CORRUPT_INCORE);
#endif
    } else
	ticket->t_curr_res -= len;

    for (index = 0; index < nentries; ) {
	if ((error = xlog_state_get_iclog_space(log, len, &iclog, ticket,
					       &contwr, &log_offset)))
		return (error);

	ASSERT(log_offset <= iclog->ic_size - 1);
	ptr = (__psint_t) ((char *)iclog->ic_datap+log_offset);

	/* start_lsn is the first lsn written to. That's all we need. */
	if (! *start_lsn)
	    *start_lsn = INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT);

	/* This loop writes out as many regions as can fit in the amount
	 * of space which was allocated by xlog_state_get_iclog_space().
	 */
	while (index < nentries) {
	    ASSERT(reg[index].i_len % sizeof(__int32_t) == 0);
	    ASSERT((__psint_t)ptr % sizeof(__int32_t) == 0);
	    start_rec_copy = 0;

	    /* If first write for transaction, insert start record.
	     * We can't be trying to commit if we are inited.  We can't
	     * have any "partial_copy" if we are inited.
	     */
	    if (ticket->t_flags & XLOG_TIC_INITED) {
		logop_head		= (xlog_op_header_t *)ptr;
		INT_SET(logop_head->oh_tid, ARCH_CONVERT, ticket->t_tid);
		logop_head->oh_clientid = ticket->t_clientid;
		INT_ZERO(logop_head->oh_len, ARCH_CONVERT);
		logop_head->oh_flags    = XLOG_START_TRANS;
		INT_ZERO(logop_head->oh_res2, ARCH_CONVERT);
		ticket->t_flags		&= ~XLOG_TIC_INITED;	/* clear bit */
		firstwr = 1;			  /* increment log ops below */
		record_cnt++;

		start_rec_copy = sizeof(xlog_op_header_t);
		xlog_write_adv_cnt(ptr, len, log_offset, start_rec_copy);
	    }

	    /* Copy log operation header directly into data section */
	    logop_head			= (xlog_op_header_t *)ptr;
	    INT_SET(logop_head->oh_tid, ARCH_CONVERT, ticket->t_tid);
	    logop_head->oh_clientid	= ticket->t_clientid;
	    INT_ZERO(logop_head->oh_res2, ARCH_CONVERT);

	    /* header copied directly */
	    xlog_write_adv_cnt(ptr, len, log_offset, sizeof(xlog_op_header_t));

	    /* are we copying a commit or unmount record? */
	    logop_head->oh_flags = flags;

	    /*
	     * We've seen logs corrupted with bad transaction client
	     * ids.  This makes sure that XFS doesn't generate them on.
	     * Turn this into an EIO and shut down the filesystem.
	     */
	    switch (logop_head->oh_clientid)  {
	    case XFS_TRANSACTION:
	    case XFS_VOLUME:
	    case XFS_LOG:
		break;
	    default:
		xfs_fs_cmn_err(CE_WARN, mp,
		    "Bad XFS transaction clientid 0x%x in ticket 0x%p",
		    logop_head->oh_clientid, tic);
		return XFS_ERROR(EIO);
	    }

	    /* Partial write last time? => (partial_copy != 0)
	     * need_copy is the amount we'd like to copy if everything could
	     * fit in the current memcpy.
	     */
	    need_copy =	reg[index].i_len - partial_copy_len;

	    copy_off = partial_copy_len;
	    if (need_copy <= iclog->ic_size - log_offset) { /*complete write */
		INT_SET(logop_head->oh_len, ARCH_CONVERT, copy_len = need_copy);
		if (partial_copy)
		    logop_head->oh_flags|= (XLOG_END_TRANS|XLOG_WAS_CONT_TRANS);
		partial_copy_len = partial_copy = 0;
	    } else {					    /* partial write */
		copy_len = iclog->ic_size - log_offset;
		INT_SET(logop_head->oh_len, ARCH_CONVERT, copy_len);
		logop_head->oh_flags |= XLOG_CONTINUE_TRANS;
		if (partial_copy)
			logop_head->oh_flags |= XLOG_WAS_CONT_TRANS;
		partial_copy_len += copy_len;
		partial_copy++;
		len += sizeof(xlog_op_header_t); /* from splitting of region */
		/* account for new log op header */
		ticket->t_curr_res -= sizeof(xlog_op_header_t);
	    }
	    xlog_verify_dest_ptr(log, ptr);

	    /* copy region */
	    ASSERT(copy_len >= 0);
	    memcpy((xfs_caddr_t)ptr, reg[index].i_addr + copy_off, copy_len);
	    xlog_write_adv_cnt(ptr, len, log_offset, copy_len);

	    /* make copy_len total bytes copied, including headers */
	    copy_len += start_rec_copy + sizeof(xlog_op_header_t);
	    record_cnt++;
	    data_cnt += contwr ? copy_len : 0;
	    firstwr = 0;
	    if (partial_copy) {			/* copied partial region */
		    /* already marked WANT_SYNC by xlog_state_get_iclog_space */
		    xlog_state_finish_copy(log, iclog, record_cnt, data_cnt);
		    record_cnt = data_cnt = 0;
		    if ((error = xlog_state_release_iclog(log, iclog)))
			    return (error);
		    break;			/* don't increment index */
	    } else {				/* copied entire region */
		index++;
		partial_copy_len = partial_copy = 0;

		if (iclog->ic_size - log_offset <= sizeof(xlog_op_header_t)) {
		    xlog_state_finish_copy(log, iclog, record_cnt, data_cnt);
		    record_cnt = data_cnt = 0;
		    xlog_state_want_sync(log, iclog);
		    if (commit_iclog) {
			ASSERT(flags & XLOG_COMMIT_TRANS);
			*commit_iclog = iclog;
		    } else if ((error = xlog_state_release_iclog(log, iclog)))
			   return (error);
		    if (index == nentries)
			    return 0;		/* we are done */
		    else
			    break;
		}
	    } /* if (partial_copy) */
	} /* while (index < nentries) */
    } /* for (index = 0; index < nentries; ) */
    ASSERT(len == 0);

    xlog_state_finish_copy(log, iclog, record_cnt, data_cnt);
    if (commit_iclog) {
	ASSERT(flags & XLOG_COMMIT_TRANS);
	*commit_iclog = iclog;
	return 0;
    }
    return (xlog_state_release_iclog(log, iclog));
}	/* xlog_write */


/*****************************************************************************
 *
 *		State Machine functions
 *
 *****************************************************************************
 */

/* Clean iclogs starting from the head.  This ordering must be
 * maintained, so an iclog doesn't become ACTIVE beyond one that
 * is SYNCING.  This is also required to maintain the notion that we use
 * a counting semaphore to hold off would be writers to the log when every
 * iclog is trying to sync to disk.
 *
 * State Change: DIRTY -> ACTIVE
 */
void
xlog_state_clean_log(xlog_t *log)
{
	xlog_in_core_t	*iclog;
	int changed = 0;

	iclog = log->l_iclog;
	do {
		if (iclog->ic_state == XLOG_STATE_DIRTY) {
			iclog->ic_state	= XLOG_STATE_ACTIVE;
			iclog->ic_offset       = 0;
			iclog->ic_callback	= 0;   /* don't need to free */
			/*
			 * If the number of ops in this iclog indicate it just
			 * contains the dummy transaction, we can
			 * change state into IDLE (the second time around).
			 * Otherwise we should change the state into
			 * NEED a dummy.
			 * We don't need to cover the dummy.
			 */
			if (!changed &&
			   (INT_GET(iclog->ic_header.h_num_logops, ARCH_CONVERT) == XLOG_COVER_OPS)) {
				changed = 1;
			} else {
				/*
				 * We have two dirty iclogs so start over
				 * This could also be num of ops indicates
				 * this is not the dummy going out.
				 */
				changed = 2;
			}
			INT_ZERO(iclog->ic_header.h_num_logops, ARCH_CONVERT);
			memset(iclog->ic_header.h_cycle_data, 0,
			      sizeof(iclog->ic_header.h_cycle_data));
			INT_ZERO(iclog->ic_header.h_lsn, ARCH_CONVERT);
		} else if (iclog->ic_state == XLOG_STATE_ACTIVE)
			/* do nothing */;
		else
			break;	/* stop cleaning */
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);

	/* log is locked when we are called */
	/*
	 * Change state for the dummy log recording.
	 * We usually go to NEED. But we go to NEED2 if the changed indicates
	 * we are done writing the dummy record.
	 * If we are done with the second dummy recored (DONE2), then
	 * we go to IDLE.
	 */
	if (changed) {
		switch (log->l_covered_state) {
		case XLOG_STATE_COVER_IDLE:
		case XLOG_STATE_COVER_NEED:
		case XLOG_STATE_COVER_NEED2:
			log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		case XLOG_STATE_COVER_DONE:
			if (changed == 1)
				log->l_covered_state = XLOG_STATE_COVER_NEED2;
			else
				log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		case XLOG_STATE_COVER_DONE2:
			if (changed == 1)
				log->l_covered_state = XLOG_STATE_COVER_IDLE;
			else
				log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		default:
			ASSERT(0);
		}
	}
}	/* xlog_state_clean_log */

STATIC xfs_lsn_t
xlog_get_lowest_lsn(
	xlog_t		*log)
{
	xlog_in_core_t  *lsn_log;
	xfs_lsn_t	lowest_lsn, lsn;

	lsn_log = log->l_iclog;
	lowest_lsn = 0;
	do {
	    if (!(lsn_log->ic_state & (XLOG_STATE_ACTIVE|XLOG_STATE_DIRTY))) {
		lsn = INT_GET(lsn_log->ic_header.h_lsn, ARCH_CONVERT);
		if ((lsn && !lowest_lsn) ||
		    (XFS_LSN_CMP_ARCH(lsn, lowest_lsn, ARCH_NOCONVERT) < 0)) {
			lowest_lsn = lsn;
		}
	    }
	    lsn_log = lsn_log->ic_next;
	} while (lsn_log != log->l_iclog);
	return(lowest_lsn);
}


STATIC void
xlog_state_do_callback(
	xlog_t		*log,
	int		aborted,
	xlog_in_core_t	*ciclog)
{
	xlog_in_core_t	   *iclog;
	xlog_in_core_t	   *first_iclog;	/* used to know when we've
						 * processed all iclogs once */
	xfs_log_callback_t *cb, *cb_next;
	int		   flushcnt = 0;
	xfs_lsn_t	   lowest_lsn;
	int		   ioerrors;	/* counter: iclogs with errors */
	int		   loopdidcallbacks; /* flag: inner loop did callbacks*/
	int		   funcdidcallbacks; /* flag: function did callbacks */
	int		   repeats;	/* for issuing console warnings if
					 * looping too many times */
	SPLDECL(s);

	s = LOG_LOCK(log);
	first_iclog = iclog = log->l_iclog;
	ioerrors = 0;
	funcdidcallbacks = 0;
	repeats = 0;

	do {
		/*
		 * Scan all iclogs starting with the one pointed to by the
		 * log.  Reset this starting point each time the log is
		 * unlocked (during callbacks).
		 *
		 * Keep looping through iclogs until one full pass is made
		 * without running any callbacks.
		 */
		first_iclog = log->l_iclog;
		iclog = log->l_iclog;
		loopdidcallbacks = 0;
		repeats++;

		do {

			/* skip all iclogs in the ACTIVE & DIRTY states */
			if (iclog->ic_state &
			    (XLOG_STATE_ACTIVE|XLOG_STATE_DIRTY)) {
				iclog = iclog->ic_next;
				continue;
			}

			/*
			 * Between marking a filesystem SHUTDOWN and stopping
			 * the log, we do flush all iclogs to disk (if there
			 * wasn't a log I/O error). So, we do want things to
			 * go smoothly in case of just a SHUTDOWN  w/o a
			 * LOG_IO_ERROR.
			 */
			if (!(iclog->ic_state & XLOG_STATE_IOERROR)) {
				/*
				 * Can only perform callbacks in order.  Since
				 * this iclog is not in the DONE_SYNC/
				 * DO_CALLBACK state, we skip the rest and
				 * just try to clean up.  If we set our iclog
				 * to DO_CALLBACK, we will not process it when
				 * we retry since a previous iclog is in the
				 * CALLBACK and the state cannot change since
				 * we are holding the LOG_LOCK.
				 */
				if (!(iclog->ic_state &
					(XLOG_STATE_DONE_SYNC |
						 XLOG_STATE_DO_CALLBACK))) {
					if (ciclog && (ciclog->ic_state ==
							XLOG_STATE_DONE_SYNC)) {
						ciclog->ic_state = XLOG_STATE_DO_CALLBACK;
					}
					break;
				}
				/*
				 * We now have an iclog that is in either the
				 * DO_CALLBACK or DONE_SYNC states. The other
				 * states (WANT_SYNC, SYNCING, or CALLBACK were
				 * caught by the above if and are going to
				 * clean (i.e. we aren't doing their callbacks)
				 * see the above if.
				 */

				/*
				 * We will do one more check here to see if we
				 * have chased our tail around.
				 */

				lowest_lsn = xlog_get_lowest_lsn(log);
				if (lowest_lsn && (
					XFS_LSN_CMP_ARCH(
						lowest_lsn,
						INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT),
						ARCH_NOCONVERT
					)<0)) {
					iclog = iclog->ic_next;
					continue; /* Leave this iclog for
						   * another thread */
				}

				iclog->ic_state = XLOG_STATE_CALLBACK;

				LOG_UNLOCK(log, s);

				/* l_last_sync_lsn field protected by
				 * GRANT_LOCK. Don't worry about iclog's lsn.
				 * No one else can be here except us.
				 */
				s = GRANT_LOCK(log);
				ASSERT(XFS_LSN_CMP_ARCH(
						log->l_last_sync_lsn,
						INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT),
						ARCH_NOCONVERT
					)<=0);
				log->l_last_sync_lsn = INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT);
				GRANT_UNLOCK(log, s);

				/*
				 * Keep processing entries in the callback list
				 * until we come around and it is empty.  We
				 * need to atomically see that the list is
				 * empty and change the state to DIRTY so that
				 * we don't miss any more callbacks being added.
				 */
				s = LOG_LOCK(log);
			} else {
				ioerrors++;
			}
			cb = iclog->ic_callback;

			while (cb != 0) {
				iclog->ic_callback_tail = &(iclog->ic_callback);
				iclog->ic_callback = 0;
				LOG_UNLOCK(log, s);

				/* perform callbacks in the order given */
				for (; cb != 0; cb = cb_next) {
					cb_next = cb->cb_next;
					cb->cb_func(cb->cb_arg, aborted);
				}
				s = LOG_LOCK(log);
				cb = iclog->ic_callback;
			}

			loopdidcallbacks++;
			funcdidcallbacks++;

			ASSERT(iclog->ic_callback == 0);
			if (!(iclog->ic_state & XLOG_STATE_IOERROR))
				iclog->ic_state = XLOG_STATE_DIRTY;

			/*
			 * Transition from DIRTY to ACTIVE if applicable.
			 * NOP if STATE_IOERROR.
			 */
			xlog_state_clean_log(log);

			/* wake up threads waiting in xfs_log_force() */
			sv_broadcast(&iclog->ic_forcesema);

			iclog = iclog->ic_next;
		} while (first_iclog != iclog);
		if (repeats && (repeats % 10) == 0) {
			xfs_fs_cmn_err(CE_WARN, log->l_mp,
				"xlog_state_do_callback: looping %d", repeats);
		}
	} while (!ioerrors && loopdidcallbacks);

	/*
	 * make one last gasp attempt to see if iclogs are being left in
	 * limbo..
	 */
#ifdef DEBUG
	if (funcdidcallbacks) {
		first_iclog = iclog = log->l_iclog;
		do {
			ASSERT(iclog->ic_state != XLOG_STATE_DO_CALLBACK);
			/*
			 * Terminate the loop if iclogs are found in states
			 * which will cause other threads to clean up iclogs.
			 *
			 * SYNCING - i/o completion will go through logs
			 * DONE_SYNC - interrupt thread should be waiting for
			 *              LOG_LOCK
			 * IOERROR - give up hope all ye who enter here
			 */
			if (iclog->ic_state == XLOG_STATE_SYNCING ||
			    iclog->ic_state == XLOG_STATE_DONE_SYNC ||
			    iclog->ic_state == XLOG_STATE_IOERROR )
				break;
			iclog = iclog->ic_next;
		} while (first_iclog != iclog);
	}
#endif

	if (log->l_iclog->ic_state & (XLOG_STATE_ACTIVE|XLOG_STATE_IOERROR)) {
		flushcnt = log->l_flushcnt;
		log->l_flushcnt = 0;
	}
	LOG_UNLOCK(log, s);
	while (flushcnt--)
		vsema(&log->l_flushsema);
}	/* xlog_state_do_callback */


/*
 * Finish transitioning this iclog to the dirty state.
 *
 * Make sure that we completely execute this routine only when this is
 * the last call to the iclog.  There is a good chance that iclog flushes,
 * when we reach the end of the physical log, get turned into 2 separate
 * calls to bwrite.  Hence, one iclog flush could generate two calls to this
 * routine.  By using the reference count bwritecnt, we guarantee that only
 * the second completion goes through.
 *
 * Callbacks could take time, so they are done outside the scope of the
 * global state machine log lock.  Assume that the calls to cvsema won't
 * take a long time.  At least we know it won't sleep.
 */
void
xlog_state_done_syncing(
	xlog_in_core_t	*iclog,
	int		aborted)
{
	xlog_t		   *log = iclog->ic_log;
	SPLDECL(s);

	s = LOG_LOCK(log);

	ASSERT(iclog->ic_state == XLOG_STATE_SYNCING ||
	       iclog->ic_state == XLOG_STATE_IOERROR);
	ASSERT(iclog->ic_refcnt == 0);
	ASSERT(iclog->ic_bwritecnt == 1 || iclog->ic_bwritecnt == 2);


	/*
	 * If we got an error, either on the first buffer, or in the case of
	 * split log writes, on the second, we mark ALL iclogs STATE_IOERROR,
	 * and none should ever be attempted to be written to disk
	 * again.
	 */
	if (iclog->ic_state != XLOG_STATE_IOERROR) {
		if (--iclog->ic_bwritecnt == 1) {
			LOG_UNLOCK(log, s);
			return;
		}
		iclog->ic_state = XLOG_STATE_DONE_SYNC;
	}

	/*
	 * Someone could be sleeping prior to writing out the next
	 * iclog buffer, we wake them all, one will get to do the
	 * I/O, the others get to wait for the result.
	 */
	sv_broadcast(&iclog->ic_writesema);
	LOG_UNLOCK(log, s);
	xlog_state_do_callback(log, aborted, iclog);	/* also cleans log */
}	/* xlog_state_done_syncing */


/*
 * If the head of the in-core log ring is not (ACTIVE or DIRTY), then we must
 * sleep.  The flush semaphore is set to the number of in-core buffers and
 * decremented around disk syncing.  Therefore, if all buffers are syncing,
 * this semaphore will cause new writes to sleep until a sync completes.
 * Otherwise, this code just does p() followed by v().  This approximates
 * a sleep/wakeup except we can't race.
 *
 * The in-core logs are used in a circular fashion. They are not used
 * out-of-order even when an iclog past the head is free.
 *
 * return:
 *	* log_offset where xlog_write() can start writing into the in-core
 *		log's data space.
 *	* in-core log pointer to which xlog_write() should write.
 *	* boolean indicating this is a continued write to an in-core log.
 *		If this is the last write, then the in-core log's offset field
 *		needs to be incremented, depending on the amount of data which
 *		is copied.
 */
int
xlog_state_get_iclog_space(xlog_t	  *log,
			   int		  len,
			   xlog_in_core_t **iclogp,
			   xlog_ticket_t  *ticket,
			   int		  *continued_write,
			   int		  *logoffsetp)
{
	SPLDECL(s);
	int		  log_offset;
	xlog_rec_header_t *head;
	xlog_in_core_t	  *iclog;
	int		  error;

restart:
	s = LOG_LOCK(log);
	if (XLOG_FORCED_SHUTDOWN(log)) {
		LOG_UNLOCK(log, s);
		return XFS_ERROR(EIO);
	}

	iclog = log->l_iclog;
	if (! (iclog->ic_state == XLOG_STATE_ACTIVE)) {
		log->l_flushcnt++;
		LOG_UNLOCK(log, s);
		xlog_trace_iclog(iclog, XLOG_TRACE_SLEEP_FLUSH);
		XFS_STATS_INC(xfsstats.xs_log_noiclogs);
		/* Ensure that log writes happen */
		psema(&log->l_flushsema, PINOD);
		goto restart;
	}
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	head = &iclog->ic_header;

	iclog->ic_refcnt++;			/* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked XLOG_STATE_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		ticket->t_curr_res -= log->l_iclog_hsize;
		INT_SET(head->h_cycle, ARCH_CONVERT, log->l_curr_cycle);
		ASSIGN_LSN(head->h_lsn, log, ARCH_CONVERT);
		ASSERT(log->l_curr_block >= 0);

		/* round off error from last write with this iclog */
		ticket->t_curr_res -= iclog->ic_roundoff;
		log->l_roundoff -= iclog->ic_roundoff;
		iclog->ic_roundoff = 0;
	}

	/* If there is enough room to write everything, then do it.  Otherwise,
	 * claim the rest of the region and make sure the XLOG_STATE_WANT_SYNC
	 * bit is on, so this will get flushed out.  Don't update ic_offset
	 * until you know exactly how many bytes get copied.  Therefore, wait
	 * until later to update ic_offset.
	 *
	 * xlog_write() algorithm assumes that at least 2 xlog_op_header_t's
	 * can fit into remaining data section.
	 */
	if (iclog->ic_size - iclog->ic_offset < 2*sizeof(xlog_op_header_t)) {
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);

		/* If I'm the only one writing to this iclog, sync it to disk */
		if (iclog->ic_refcnt == 1) {
			LOG_UNLOCK(log, s);
			if ((error = xlog_state_release_iclog(log, iclog)))
				return (error);
		} else {
			iclog->ic_refcnt--;
			LOG_UNLOCK(log, s);
		}
		goto restart;
	}

	/* Do we have enough room to write the full amount in the remainder
	 * of this iclog?  Or must we continue a write on the next iclog and
	 * mark this iclog as completely taken?  In the case where we switch
	 * iclogs (to mark it taken), this particular iclog will release/sync
	 * to disk in xlog_write().
	 */
	if (len <= iclog->ic_size - iclog->ic_offset) {
		*continued_write = 0;
		iclog->ic_offset += len;
	} else {
		*continued_write = 1;
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);
	}
	*iclogp = iclog;

	ASSERT(iclog->ic_offset <= iclog->ic_size);
	LOG_UNLOCK(log, s);

	*logoffsetp = log_offset;
	return 0;
}	/* xlog_state_get_iclog_space */

/*
 * Atomically get the log space required for a log ticket.
 *
 * Once a ticket gets put onto the reserveq, it will only return after
 * the needed reservation is satisfied.
 */
STATIC int
xlog_grant_log_space(xlog_t	   *log,
		     xlog_ticket_t *tic)
{
	int		 free_bytes;
	int		 need_bytes;
	SPLDECL(s);
#ifdef DEBUG
	xfs_lsn_t	 tail_lsn;
#endif


#ifdef DEBUG
	if (log->l_flags & XLOG_ACTIVE_RECOVERY)
		panic("grant Recovery problem");
#endif

	/* Is there space or do we need to sleep? */
	s = GRANT_LOCK(log);
	xlog_trace_loggrant(log, tic, "xlog_grant_log_space: enter");

	/* something is already sleeping; insert new transaction at end */
	if (log->l_reserve_headq) {
		XLOG_INS_TICKETQ(log->l_reserve_headq, tic);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: sleep 1");
		/*
		 * Gotta check this before going to sleep, while we're
		 * holding the grant lock.
		 */
		if (XLOG_FORCED_SHUTDOWN(log))
			goto error_return;

		XFS_STATS_INC(xfsstats.xs_sleep_logspace);
		sv_wait(&tic->t_sema, PINOD|PLTWAIT, &log->l_grant_lock, s);
		/*
		 * If we got an error, and the filesystem is shutting down,
		 * we'll catch it down below. So just continue...
		 */
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: wake 1");
		s = GRANT_LOCK(log);
	}
	if (tic->t_flags & XFS_LOG_PERM_RESERV)
		need_bytes = tic->t_unit_res*tic->t_ocnt;
	else
		need_bytes = tic->t_unit_res;

redo:
	if (XLOG_FORCED_SHUTDOWN(log))
		goto error_return;

	free_bytes = xlog_space_left(log, log->l_grant_reserve_cycle,
				     log->l_grant_reserve_bytes);
	if (free_bytes < need_bytes) {
		if ((tic->t_flags & XLOG_TIC_IN_Q) == 0)
			XLOG_INS_TICKETQ(log->l_reserve_headq, tic);
		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: sleep 2");
		XFS_STATS_INC(xfsstats.xs_sleep_logspace);
		sv_wait(&tic->t_sema, PINOD|PLTWAIT, &log->l_grant_lock, s);

		if (XLOG_FORCED_SHUTDOWN(log)) {
			s = GRANT_LOCK(log);
			goto error_return;
		}

		xlog_trace_loggrant(log, tic,
				    "xlog_grant_log_space: wake 2");
		xlog_grant_push_ail(log->l_mp, need_bytes);
		s = GRANT_LOCK(log);
		goto redo;
	} else if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_reserve_headq, tic);

	/* we've got enough space */
	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'w');
	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'r');
#ifdef DEBUG
	tail_lsn = log->l_tail_lsn;
	/*
	 * Check to make sure the grant write head didn't just over lap the
	 * tail.  If the cycles are the same, we can't be overlapping.
	 * Otherwise, make sure that the cycles differ by exactly one and
	 * check the byte count.
	 */
	if (CYCLE_LSN(tail_lsn, ARCH_NOCONVERT) != log->l_grant_write_cycle) {
		ASSERT(log->l_grant_write_cycle-1 == CYCLE_LSN(tail_lsn, ARCH_NOCONVERT));
		ASSERT(log->l_grant_write_bytes <= BBTOB(BLOCK_LSN(tail_lsn, ARCH_NOCONVERT)));
	}
#endif
	xlog_trace_loggrant(log, tic, "xlog_grant_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, s);
	return 0;

 error_return:
	if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_reserve_headq, tic);
	xlog_trace_loggrant(log, tic, "xlog_grant_log_space: err_ret");
	/*
	 * If we are failing, make sure the ticket doesn't have any
	 * current reservations. We don't want to add this back when
	 * the ticket/transaction gets cancelled.
	 */
	tic->t_curr_res = 0;
	tic->t_cnt = 0; /* ungrant will give back unit_res * t_cnt. */
	GRANT_UNLOCK(log, s);
	return XFS_ERROR(EIO);
}	/* xlog_grant_log_space */


/*
 * Replenish the byte reservation required by moving the grant write head.
 *
 *
 */
STATIC int
xlog_regrant_write_log_space(xlog_t	   *log,
			     xlog_ticket_t *tic)
{
	SPLDECL(s);
	int		free_bytes, need_bytes;
	xlog_ticket_t	*ntic;
#ifdef DEBUG
	xfs_lsn_t	tail_lsn;
#endif

	tic->t_curr_res = tic->t_unit_res;

	if (tic->t_cnt > 0)
		return (0);

#ifdef DEBUG
	if (log->l_flags & XLOG_ACTIVE_RECOVERY)
		panic("regrant Recovery problem");
#endif

	s = GRANT_LOCK(log);
	xlog_trace_loggrant(log, tic, "xlog_regrant_write_log_space: enter");

	if (XLOG_FORCED_SHUTDOWN(log))
		goto error_return;

	/* If there are other waiters on the queue then give them a
	 * chance at logspace before us. Wake up the first waiters,
	 * if we do not wake up all the waiters then go to sleep waiting
	 * for more free space, otherwise try to get some space for
	 * this transaction.
	 */

	if ((ntic = log->l_write_headq)) {
		free_bytes = xlog_space_left(log, log->l_grant_write_cycle,
					     log->l_grant_write_bytes);
		do {
			ASSERT(ntic->t_flags & XLOG_TIC_PERM_RESERV);

			if (free_bytes < ntic->t_unit_res)
				break;
			free_bytes -= ntic->t_unit_res;
			sv_signal(&ntic->t_sema);
			ntic = ntic->t_next;
		} while (ntic != log->l_write_headq);

		if (ntic != log->l_write_headq) {
			if ((tic->t_flags & XLOG_TIC_IN_Q) == 0)
				XLOG_INS_TICKETQ(log->l_write_headq, tic);

			xlog_trace_loggrant(log, tic,
				    "xlog_regrant_write_log_space: sleep 1");
			XFS_STATS_INC(xfsstats.xs_sleep_logspace);
			sv_wait(&tic->t_sema, PINOD|PLTWAIT,
				&log->l_grant_lock, s);

			/* If we're shutting down, this tic is already
			 * off the queue */
			if (XLOG_FORCED_SHUTDOWN(log)) {
				s = GRANT_LOCK(log);
				goto error_return;
			}

			xlog_trace_loggrant(log, tic,
				    "xlog_regrant_write_log_space: wake 1");
			xlog_grant_push_ail(log->l_mp, tic->t_unit_res);
			s = GRANT_LOCK(log);
		}
	}

	need_bytes = tic->t_unit_res;

redo:
	if (XLOG_FORCED_SHUTDOWN(log))
		goto error_return;

	free_bytes = xlog_space_left(log, log->l_grant_write_cycle,
				     log->l_grant_write_bytes);
	if (free_bytes < need_bytes) {
		if ((tic->t_flags & XLOG_TIC_IN_Q) == 0)
			XLOG_INS_TICKETQ(log->l_write_headq, tic);
		XFS_STATS_INC(xfsstats.xs_sleep_logspace);
		sv_wait(&tic->t_sema, PINOD|PLTWAIT, &log->l_grant_lock, s);

		/* If we're shutting down, this tic is already off the queue */
		if (XLOG_FORCED_SHUTDOWN(log)) {
			s = GRANT_LOCK(log);
			goto error_return;
		}

		xlog_trace_loggrant(log, tic,
				    "xlog_regrant_write_log_space: wake 2");
		xlog_grant_push_ail(log->l_mp, need_bytes);
		s = GRANT_LOCK(log);
		goto redo;
	} else if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_write_headq, tic);

	XLOG_GRANT_ADD_SPACE(log, need_bytes, 'w'); /* we've got enough space */
#ifdef DEBUG
	tail_lsn = log->l_tail_lsn;
	if (CYCLE_LSN(tail_lsn, ARCH_NOCONVERT) != log->l_grant_write_cycle) {
		ASSERT(log->l_grant_write_cycle-1 == CYCLE_LSN(tail_lsn, ARCH_NOCONVERT));
		ASSERT(log->l_grant_write_bytes <= BBTOB(BLOCK_LSN(tail_lsn, ARCH_NOCONVERT)));
	}
#endif

	xlog_trace_loggrant(log, tic, "xlog_regrant_write_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, s);
	return (0);


 error_return:
	if (tic->t_flags & XLOG_TIC_IN_Q)
		XLOG_DEL_TICKETQ(log->l_reserve_headq, tic);
	xlog_trace_loggrant(log, tic, "xlog_regrant_write_log_space: err_ret");
	/*
	 * If we are failing, make sure the ticket doesn't have any
	 * current reservations. We don't want to add this back when
	 * the ticket/transaction gets cancelled.
	 */
	tic->t_curr_res = 0;
	tic->t_cnt = 0; /* ungrant will give back unit_res * t_cnt. */
	GRANT_UNLOCK(log, s);
	return XFS_ERROR(EIO);
}	/* xlog_regrant_write_log_space */


/* The first cnt-1 times through here we don't need to
 * move the grant write head because the permanent
 * reservation has reserved cnt times the unit amount.
 * Release part of current permanent unit reservation and
 * reset current reservation to be one units worth.  Also
 * move grant reservation head forward.
 */
STATIC void
xlog_regrant_reserve_log_space(xlog_t	     *log,
			       xlog_ticket_t *ticket)
{
	SPLDECL(s);

	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: enter");
	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	s = GRANT_LOCK(log);
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'w');
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'r');
	ticket->t_curr_res = ticket->t_unit_res;
	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: sub current res");
	xlog_verify_grant_head(log, 1);

	/* just return if we still have some of the pre-reserved space */
	if (ticket->t_cnt > 0) {
		GRANT_UNLOCK(log, s);
		return;
	}

	XLOG_GRANT_ADD_SPACE(log, ticket->t_unit_res, 'r');
	xlog_trace_loggrant(log, ticket,
			    "xlog_regrant_reserve_log_space: exit");
	xlog_verify_grant_head(log, 0);
	GRANT_UNLOCK(log, s);
	ticket->t_curr_res = ticket->t_unit_res;
}	/* xlog_regrant_reserve_log_space */


/*
 * Give back the space left from a reservation.
 *
 * All the information we need to make a correct determination of space left
 * is present.  For non-permanent reservations, things are quite easy.  The
 * count should have been decremented to zero.  We only need to deal with the
 * space remaining in the current reservation part of the ticket.  If the
 * ticket contains a permanent reservation, there may be left over space which
 * needs to be released.  A count of N means that N-1 refills of the current
 * reservation can be done before we need to ask for more space.  The first
 * one goes to fill up the first current reservation.  Once we run out of
 * space, the count will stay at zero and the only space remaining will be
 * in the current reservation field.
 */
STATIC void
xlog_ungrant_log_space(xlog_t	     *log,
		       xlog_ticket_t *ticket)
{
	SPLDECL(s);

	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	s = GRANT_LOCK(log);
	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: enter");

	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'w');
	XLOG_GRANT_SUB_SPACE(log, ticket->t_curr_res, 'r');

	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: sub current");

	/* If this is a permanent reservation ticket, we may be able to free
	 * up more space based on the remaining count.
	 */
	if (ticket->t_cnt > 0) {
		ASSERT(ticket->t_flags & XLOG_TIC_PERM_RESERV);
		XLOG_GRANT_SUB_SPACE(log, ticket->t_unit_res*ticket->t_cnt,'w');
		XLOG_GRANT_SUB_SPACE(log, ticket->t_unit_res*ticket->t_cnt,'r');
	}

	xlog_trace_loggrant(log, ticket, "xlog_ungrant_log_space: exit");
	xlog_verify_grant_head(log, 1);
	GRANT_UNLOCK(log, s);
	xfs_log_move_tail(log->l_mp, 1);
}	/* xlog_ungrant_log_space */


/*
 * Atomically put back used ticket.
 */
void
xlog_state_put_ticket(xlog_t	    *log,
		      xlog_ticket_t *tic)
{
	unsigned long s;

	s = LOG_LOCK(log);
	xlog_ticket_put(log, tic);
	LOG_UNLOCK(log, s);
}	/* xlog_state_put_ticket */

/*
 * Flush iclog to disk if this is the last reference to the given iclog and
 * the WANT_SYNC bit is set.
 *
 * When this function is entered, the iclog is not necessarily in the
 * WANT_SYNC state.  It may be sitting around waiting to get filled.
 *
 *
 */
int
xlog_state_release_iclog(xlog_t		*log,
			 xlog_in_core_t	*iclog)
{
	SPLDECL(s);
	int		sync = 0;	/* do we sync? */

	xlog_assign_tail_lsn(log->l_mp);

	s = LOG_LOCK(log);

	if (iclog->ic_state & XLOG_STATE_IOERROR) {
		LOG_UNLOCK(log, s);
		return XFS_ERROR(EIO);
	}

	ASSERT(iclog->ic_refcnt > 0);
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE ||
	       iclog->ic_state == XLOG_STATE_WANT_SYNC);

	if (--iclog->ic_refcnt == 0 &&
	    iclog->ic_state == XLOG_STATE_WANT_SYNC) {
		sync++;
		iclog->ic_state = XLOG_STATE_SYNCING;
		INT_SET(iclog->ic_header.h_tail_lsn, ARCH_CONVERT, log->l_tail_lsn);
		xlog_verify_tail_lsn(log, iclog, log->l_tail_lsn);
		/* cycle incremented when incrementing curr_block */
	}

	LOG_UNLOCK(log, s);

	/*
	 * We let the log lock go, so it's possible that we hit a log I/O
	 * error or someother SHUTDOWN condition that marks the iclog
	 * as XLOG_STATE_IOERROR before the bwrite. However, we know that
	 * this iclog has consistent data, so we ignore IOERROR
	 * flags after this point.
	 */
	if (sync) {
		return xlog_sync(log, iclog);
	}
	return (0);

}	/* xlog_state_release_iclog */


/*
 * This routine will mark the current iclog in the ring as WANT_SYNC
 * and move the current iclog pointer to the next iclog in the ring.
 * When this routine is called from xlog_state_get_iclog_space(), the
 * exact size of the iclog has not yet been determined.  All we know is
 * that every data block.  We have run out of space in this log record.
 */
STATIC void
xlog_state_switch_iclogs(xlog_t		*log,
			 xlog_in_core_t *iclog,
			 int		eventual_size)
{
	uint roundup;

	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	if (!eventual_size)
		eventual_size = iclog->ic_offset;
	iclog->ic_state = XLOG_STATE_WANT_SYNC;
	INT_SET(iclog->ic_header.h_prev_block, ARCH_CONVERT, log->l_prev_block);
	log->l_prev_block = log->l_curr_block;
	log->l_prev_cycle = log->l_curr_cycle;

	/* roll log?: ic_offset changed later */
	log->l_curr_block += BTOBB(eventual_size)+BTOBB(log->l_iclog_hsize);

	/* Round up to next log-sunit */
	if (XFS_SB_VERSION_HASLOGV2(&log->l_mp->m_sb)) {
		if (log->l_curr_block & (log->l_stripemask - 1)) {
			roundup = log->l_stripemask -
				(log->l_curr_block & (log->l_stripemask - 1));
		} else {
			roundup = 0;
		}
		log->l_curr_block += roundup;
	}

	if (log->l_curr_block >= log->l_logBBsize) {
		log->l_curr_cycle++;
		if (log->l_curr_cycle == XLOG_HEADER_MAGIC_NUM)
			log->l_curr_cycle++;
		log->l_curr_block -= log->l_logBBsize;
		ASSERT(log->l_curr_block >= 0);
	}
	ASSERT(iclog == log->l_iclog);
	log->l_iclog = iclog->ic_next;
}	/* xlog_state_switch_iclogs */


/*
 * Write out all data in the in-core log as of this exact moment in time.
 *
 * Data may be written to the in-core log during this call.  However,
 * we don't guarantee this data will be written out.  A change from past
 * implementation means this routine will *not* write out zero length LRs.
 *
 * Basically, we try and perform an intelligent scan of the in-core logs.
 * If we determine there is no flushable data, we just return.  There is no
 * flushable data if:
 *
 *	1. the current iclog is active and has no data; the previous iclog
 *		is in the active or dirty state.
 *	2. the current iclog is drity, and the previous iclog is in the
 *		active or dirty state.
 *
 * We may sleep (call psema) if:
 *
 *	1. the current iclog is not in the active nor dirty state.
 *	2. the current iclog dirty, and the previous iclog is not in the
 *		active nor dirty state.
 *	3. the current iclog is active, and there is another thread writing
 *		to this particular iclog.
 *	4. a) the current iclog is active and has no other writers
 *	   b) when we return from flushing out this iclog, it is still
 *		not in the active nor dirty state.
 */
STATIC int
xlog_state_sync_all(xlog_t *log, uint flags)
{
	xlog_in_core_t	*iclog;
	xfs_lsn_t	lsn;
	SPLDECL(s);

	s = LOG_LOCK(log);

	iclog = log->l_iclog;
	if (iclog->ic_state & XLOG_STATE_IOERROR) {
		LOG_UNLOCK(log, s);
		return XFS_ERROR(EIO);
	}

	/* If the head iclog is not active nor dirty, we just attach
	 * ourselves to the head and go to sleep.
	 */
	if (iclog->ic_state == XLOG_STATE_ACTIVE ||
	    iclog->ic_state == XLOG_STATE_DIRTY) {
		/*
		 * If the head is dirty or (active and empty), then
		 * we need to look at the previous iclog.  If the previous
		 * iclog is active or dirty we are done.  There is nothing
		 * to sync out.  Otherwise, we attach ourselves to the
		 * previous iclog and go to sleep.
		 */
		if (iclog->ic_state == XLOG_STATE_DIRTY ||
		    (iclog->ic_refcnt == 0 && iclog->ic_offset == 0)) {
			iclog = iclog->ic_prev;
			if (iclog->ic_state == XLOG_STATE_ACTIVE ||
			    iclog->ic_state == XLOG_STATE_DIRTY)
				goto no_sleep;
			else
				goto maybe_sleep;
		} else {
			if (iclog->ic_refcnt == 0) {
				/* We are the only one with access to this
				 * iclog.  Flush it out now.  There should
				 * be a roundoff of zero to show that someone
				 * has already taken care of the roundoff from
				 * the previous sync.
				 */
				ASSERT(iclog->ic_roundoff == 0);
				iclog->ic_refcnt++;
				lsn = INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT);
				xlog_state_switch_iclogs(log, iclog, 0);
				LOG_UNLOCK(log, s);

				if (xlog_state_release_iclog(log, iclog))
					return XFS_ERROR(EIO);
				s = LOG_LOCK(log);
				if (INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT) == lsn &&
				    iclog->ic_state != XLOG_STATE_DIRTY)
					goto maybe_sleep;
				else
					goto no_sleep;
			} else {
				/* Someone else is writing to this iclog.
				 * Use its call to flush out the data.  However,
				 * the other thread may not force out this LR,
				 * so we mark it WANT_SYNC.
				 */
				xlog_state_switch_iclogs(log, iclog, 0);
				goto maybe_sleep;
			}
		}
	}

	/* By the time we come around again, the iclog could've been filled
	 * which would give it another lsn.  If we have a new lsn, just
	 * return because the relevant data has been flushed.
	 */
maybe_sleep:
	if (flags & XFS_LOG_SYNC) {
		/*
		 * We must check if we're shutting down here, before
		 * we wait, while we're holding the LOG_LOCK.
		 * Then we check again after waking up, in case our
		 * sleep was disturbed by a bad news.
		 */
		if (iclog->ic_state & XLOG_STATE_IOERROR) {
			LOG_UNLOCK(log, s);
			return XFS_ERROR(EIO);
		}
		XFS_STATS_INC(xfsstats.xs_log_force_sleep);
		sv_wait(&iclog->ic_forcesema, PINOD, &log->l_icloglock, s);
		/*
		 * No need to grab the log lock here since we're
		 * only deciding whether or not to return EIO
		 * and the memory read should be atomic.
		 */
		if (iclog->ic_state & XLOG_STATE_IOERROR)
			return XFS_ERROR(EIO);

	} else {

no_sleep:
		LOG_UNLOCK(log, s);
	}
	return 0;
}	/* xlog_state_sync_all */


/*
 * Used by code which implements synchronous log forces.
 *
 * Find in-core log with lsn.
 *	If it is in the DIRTY state, just return.
 *	If it is in the ACTIVE state, move the in-core log into the WANT_SYNC
 *		state and go to sleep or return.
 *	If it is in any other state, go to sleep or return.
 *
 * If filesystem activity goes to zero, the iclog will get flushed only by
 * bdflush().
 */
int
xlog_state_sync(xlog_t	  *log,
		xfs_lsn_t lsn,
		uint	  flags)
{
    xlog_in_core_t	*iclog;
    int			already_slept = 0;
    SPLDECL(s);


try_again:
    s = LOG_LOCK(log);
    iclog = log->l_iclog;

    if (iclog->ic_state & XLOG_STATE_IOERROR) {
	    LOG_UNLOCK(log, s);
	    return XFS_ERROR(EIO);
    }

    do {
	if (INT_GET(iclog->ic_header.h_lsn, ARCH_CONVERT) != lsn) {
	    iclog = iclog->ic_next;
	    continue;
	}

	if (iclog->ic_state == XLOG_STATE_DIRTY) {
		LOG_UNLOCK(log, s);
		return 0;
	}

	if (iclog->ic_state == XLOG_STATE_ACTIVE) {
		/*
		 * We sleep here if we haven't already slept (e.g.
		 * this is the first time we've looked at the correct
		 * iclog buf) and the buffer before us is going to
		 * be sync'ed. The reason for this is that if we
		 * are doing sync transactions here, by waiting for
		 * the previous I/O to complete, we can allow a few
		 * more transactions into this iclog before we close
		 * it down.
		 *
		 * Otherwise, we mark the buffer WANT_SYNC, and bump
		 * up the refcnt so we can release the log (which drops
		 * the ref count).  The state switch keeps new transaction
		 * commits from using this buffer.  When the current commits
		 * finish writing into the buffer, the refcount will drop to
		 * zero and the buffer will go out then.
		 */
		if (!already_slept &&
		    (iclog->ic_prev->ic_state & (XLOG_STATE_WANT_SYNC |
						 XLOG_STATE_SYNCING))) {
			ASSERT(!(iclog->ic_state & XLOG_STATE_IOERROR));
			XFS_STATS_INC(xfsstats.xs_log_force_sleep);
			sv_wait(&iclog->ic_prev->ic_writesema, PSWP,
				&log->l_icloglock, s);
			already_slept = 1;
			goto try_again;
		} else {
			iclog->ic_refcnt++;
			xlog_state_switch_iclogs(log, iclog, 0);
			LOG_UNLOCK(log, s);
			if (xlog_state_release_iclog(log, iclog))
				return XFS_ERROR(EIO);
			s = LOG_LOCK(log);
		}
	}

	if ((flags & XFS_LOG_SYNC) && /* sleep */
	    !(iclog->ic_state & (XLOG_STATE_ACTIVE | XLOG_STATE_DIRTY))) {

		/*
		 * Don't wait on the forcesema if we know that we've
		 * gotten a log write error.
		 */
		if (iclog->ic_state & XLOG_STATE_IOERROR) {
			LOG_UNLOCK(log, s);
			return XFS_ERROR(EIO);
		}
		XFS_STATS_INC(xfsstats.xs_log_force_sleep);
		sv_wait(&iclog->ic_forcesema, PSWP, &log->l_icloglock, s);
		/*
		 * No need to grab the log lock here since we're
		 * only deciding whether or not to return EIO
		 * and the memory read should be atomic.
		 */
		if (iclog->ic_state & XLOG_STATE_IOERROR)
			return XFS_ERROR(EIO);
	} else {		/* just return */
		LOG_UNLOCK(log, s);
	}
	return 0;

    } while (iclog != log->l_iclog);

    LOG_UNLOCK(log, s);
    return (0);
}	/* xlog_state_sync */


/*
 * Called when we want to mark the current iclog as being ready to sync to
 * disk.
 */
void
xlog_state_want_sync(xlog_t *log, xlog_in_core_t *iclog)
{
	SPLDECL(s);

	s = LOG_LOCK(log);

	if (iclog->ic_state == XLOG_STATE_ACTIVE) {
		xlog_state_switch_iclogs(log, iclog, 0);
	} else {
		ASSERT(iclog->ic_state &
			(XLOG_STATE_WANT_SYNC|XLOG_STATE_IOERROR));
	}

	LOG_UNLOCK(log, s);
}	/* xlog_state_want_sync */



/*****************************************************************************
 *
 *		TICKET functions
 *
 *****************************************************************************
 */

/*
 *	Algorithm doesn't take into account page size. ;-(
 */
STATIC void
xlog_state_ticket_alloc(xlog_t *log)
{
	xlog_ticket_t	*t_list;
	xlog_ticket_t	*next;
	xfs_caddr_t	buf;
	uint		i = (NBPP / sizeof(xlog_ticket_t)) - 2;
	SPLDECL(s);

	/*
	 * The kmem_zalloc may sleep, so we shouldn't be holding the
	 * global lock.  XXXmiken: may want to use zone allocator.
	 */
	buf = (xfs_caddr_t) kmem_zalloc(NBPP, 0);

	s = LOG_LOCK(log);

	/* Attach 1st ticket to Q, so we can keep track of allocated memory */
	t_list = (xlog_ticket_t *)buf;
	t_list->t_next = log->l_unmount_free;
	log->l_unmount_free = t_list++;
	log->l_ticket_cnt++;
	log->l_ticket_tcnt++;

	/* Next ticket becomes first ticket attached to ticket free list */
	if (log->l_freelist != NULL) {
		ASSERT(log->l_tail != NULL);
		log->l_tail->t_next = t_list;
	} else {
		log->l_freelist = t_list;
	}
	log->l_ticket_cnt++;
	log->l_ticket_tcnt++;

	/* Cycle through rest of alloc'ed memory, building up free Q */
	for ( ; i > 0; i--) {
		next = t_list + 1;
		t_list->t_next = next;
		t_list = next;
		log->l_ticket_cnt++;
		log->l_ticket_tcnt++;
	}
	t_list->t_next = 0;
	log->l_tail = t_list;
	LOG_UNLOCK(log, s);
}	/* xlog_state_ticket_alloc */


/*
 * Put ticket into free list
 *
 * Assumption: log lock is held around this call.
 */
STATIC void
xlog_ticket_put(xlog_t		*log,
		xlog_ticket_t	*ticket)
{
	sv_destroy(&ticket->t_sema);

	/*
	 * Don't think caching will make that much difference.  It's
	 * more important to make debug easier.
	 */
#if 0
	/* real code will want to use LIFO for caching */
	ticket->t_next = log->l_freelist;
	log->l_freelist = ticket;
	/* no need to clear fields */
#else
	/* When we debug, it is easier if tickets are cycled */
	ticket->t_next     = 0;
	if (log->l_tail != 0) {
		log->l_tail->t_next = ticket;
	} else {
		ASSERT(log->l_freelist == 0);
		log->l_freelist = ticket;
	}
	log->l_tail	    = ticket;
#endif /* DEBUG */
	log->l_ticket_cnt++;
}	/* xlog_ticket_put */


/*
 * Grab ticket off freelist or allocation some more
 */
xlog_ticket_t *
xlog_ticket_get(xlog_t		*log,
		int		unit_bytes,
		int		cnt,
		char		client,
		uint		xflags)
{
	xlog_ticket_t	*tic;
	SPLDECL(s);

 alloc:
	if (log->l_freelist == NULL)
		xlog_state_ticket_alloc(log);		/* potentially sleep */

	s = LOG_LOCK(log);
	if (log->l_freelist == NULL) {
		LOG_UNLOCK(log, s);
		goto alloc;
	}
	tic		= log->l_freelist;
	log->l_freelist	= tic->t_next;
	if (log->l_freelist == NULL)
		log->l_tail = NULL;
	log->l_ticket_cnt--;
	LOG_UNLOCK(log, s);

	/*
	 * Permanent reservations have up to 'cnt'-1 active log operations
	 * in the log.  A unit in this case is the amount of space for one
	 * of these log operations.  Normal reservations have a cnt of 1
	 * and their unit amount is the total amount of space required.
	 * The following line of code adds one log record header length
	 * for each part of an operation which may fall on a different
	 * log record.
	 *
	 * One more XLOG_HEADER_SIZE is added to account for possible
	 * round off errors when syncing a LR to disk.  The bytes are
	 * subtracted if the thread using this ticket is the first writer
	 * to a new LR.
	 *
	 * We add an extra log header for the possibility that the commit
	 * record is the first data written to a new log record.  In this
	 * case it is separate from the rest of the transaction data and
	 * will be charged for the log record header.
	 */
	unit_bytes += log->l_iclog_hsize * (XLOG_BTOLRBB(unit_bytes) + 2);

	tic->t_unit_res		= unit_bytes;
	tic->t_curr_res		= unit_bytes;
	tic->t_cnt		= cnt;
	tic->t_ocnt		= cnt;
	tic->t_tid		= (xlog_tid_t)((__psint_t)tic & 0xffffffff);
	tic->t_clientid		= client;
	tic->t_flags		= XLOG_TIC_INITED;
	if (xflags & XFS_LOG_PERM_RESERV)
		tic->t_flags |= XLOG_TIC_PERM_RESERV;
	sv_init(&(tic->t_sema), SV_DEFAULT, "logtick");

	return tic;
}	/* xlog_ticket_get */


/******************************************************************************
 *
 *		Log debug routines
 *
 ******************************************************************************
 */
#if defined(DEBUG) && !defined(XLOG_NOLOG)
/*
 * Make sure that the destination ptr is within the valid data region of
 * one of the iclogs.  This uses backup pointers stored in a different
 * part of the log in case we trash the log structure.
 */
void
xlog_verify_dest_ptr(xlog_t     *log,
		     __psint_t  ptr)
{
	int i;
	int good_ptr = 0;

	for (i=0; i < log->l_iclog_bufs; i++) {
		if (ptr >= (__psint_t)log->l_iclog_bak[i] &&
		    ptr <= (__psint_t)log->l_iclog_bak[i]+log->l_iclog_size)
			good_ptr++;
	}
	if (! good_ptr)
		xlog_panic("xlog_verify_dest_ptr: invalid ptr");
}	/* xlog_verify_dest_ptr */


#ifdef XFSDEBUG
/* check split LR write */
STATIC void
xlog_verify_disk_cycle_no(xlog_t	 *log,
			  xlog_in_core_t *iclog)
{
    xfs_buf_t	*bp;
    uint	cycle_no;
    xfs_caddr_t ptr;
    xfs_daddr_t	i;

    if (BLOCK_LSN(iclog->ic_header.h_lsn, ARCH_CONVERT) < 10) {
	cycle_no = CYCLE_LSN(iclog->ic_header.h_lsn, ARCH_CONVERT);
	bp = xlog_get_bp(log, 1);
	ASSERT(bp);
	for (i = 0; i < BLOCK_LSN(iclog->ic_header.h_lsn, ARCH_CONVERT); i++) {
	    xlog_bread(log, i, 1, bp);
	    ptr = xlog_align(log, i, 1, bp);
	    if (GET_CYCLE(ptr, ARCH_CONVERT) != cycle_no)
		xlog_warn("XFS: xlog_verify_disk_cycle_no: bad cycle no");
	}
	xlog_put_bp(bp);
    }
}	/* xlog_verify_disk_cycle_no */
#endif

STATIC void
xlog_verify_grant_head(xlog_t *log, int equals)
{
    if (log->l_grant_reserve_cycle == log->l_grant_write_cycle) {
	if (equals)
	    ASSERT(log->l_grant_reserve_bytes >= log->l_grant_write_bytes);
	else
	    ASSERT(log->l_grant_reserve_bytes > log->l_grant_write_bytes);
    } else {
	ASSERT(log->l_grant_reserve_cycle-1 == log->l_grant_write_cycle);
	ASSERT(log->l_grant_write_bytes >= log->l_grant_reserve_bytes);
    }
}	/* xlog_verify_grant_head */

/* check if it will fit */
STATIC void
xlog_verify_tail_lsn(xlog_t	    *log,
		     xlog_in_core_t *iclog,
		     xfs_lsn_t	    tail_lsn)
{
    int blocks;

    if (CYCLE_LSN(tail_lsn, ARCH_NOCONVERT) == log->l_prev_cycle) {
	blocks =
	    log->l_logBBsize - (log->l_prev_block - BLOCK_LSN(tail_lsn, ARCH_NOCONVERT));
	if (blocks < BTOBB(iclog->ic_offset)+BTOBB(log->l_iclog_hsize))
	    xlog_panic("xlog_verify_tail_lsn: ran out of log space");
    } else {
	ASSERT(CYCLE_LSN(tail_lsn, ARCH_NOCONVERT)+1 == log->l_prev_cycle);

	if (BLOCK_LSN(tail_lsn, ARCH_NOCONVERT) == log->l_prev_block)
	    xlog_panic("xlog_verify_tail_lsn: tail wrapped");

	blocks = BLOCK_LSN(tail_lsn, ARCH_NOCONVERT) - log->l_prev_block;
	if (blocks < BTOBB(iclog->ic_offset) + 1)
	    xlog_panic("xlog_verify_tail_lsn: ran out of log space");
    }
}	/* xlog_verify_tail_lsn */

/*
 * Perform a number of checks on the iclog before writing to disk.
 *
 * 1. Make sure the iclogs are still circular
 * 2. Make sure we have a good magic number
 * 3. Make sure we don't have magic numbers in the data
 * 4. Check fields of each log operation header for:
 *	A. Valid client identifier
 *	B. tid ptr value falls in valid ptr space (user space code)
 *	C. Length in log record header is correct according to the
 *		individual operation headers within record.
 * 5. When a bwrite will occur within 5 blocks of the front of the physical
 *	log, check the preceding blocks of the physical log to make sure all
 *	the cycle numbers agree with the current cycle number.
 */
STATIC void
xlog_verify_iclog(xlog_t	 *log,
		  xlog_in_core_t *iclog,
		  int		 count,
		  boolean_t	 syncing)
{
	xlog_op_header_t	*ophead;
	xlog_in_core_t		*icptr;
	xfs_caddr_t		ptr;
	xfs_caddr_t		base_ptr;
	__psint_t		field_offset;
	__uint8_t		clientid;
	int			len, i, j, k, op_len;
	int			idx;
	SPLDECL(s);

	union ich {
		xlog_rec_ext_header_t	hic_xheader;
		char			hic_sector[XLOG_HEADER_SIZE];
	}*xhdr;

	/* check validity of iclog pointers */
	s = LOG_LOCK(log);
	icptr = log->l_iclog;
	for (i=0; i < log->l_iclog_bufs; i++) {
		if (icptr == 0)
			xlog_panic("xlog_verify_iclog: illegal ptr");
		icptr = icptr->ic_next;
	}
	if (icptr != log->l_iclog)
		xlog_panic("xlog_verify_iclog: corrupt iclog ring");
	LOG_UNLOCK(log, s);

	/* check log magic numbers */
	ptr = (xfs_caddr_t) &(iclog->ic_header);
	if (INT_GET(*(uint *)ptr, ARCH_CONVERT) != XLOG_HEADER_MAGIC_NUM)
		xlog_panic("xlog_verify_iclog: illegal magic num");

	for (ptr += BBSIZE; ptr < ((xfs_caddr_t)&(iclog->ic_header))+count;
	     ptr += BBSIZE) {
		if (INT_GET(*(uint *)ptr, ARCH_CONVERT) == XLOG_HEADER_MAGIC_NUM)
			xlog_panic("xlog_verify_iclog: unexpected magic num");
	}

	/* check fields */
	len = INT_GET(iclog->ic_header.h_num_logops, ARCH_CONVERT);
	ptr = iclog->ic_datap;
	base_ptr = ptr;
	ophead = (xlog_op_header_t *)ptr;
	xhdr = (union ich*)&iclog->ic_header;
	for (i = 0; i < len; i++) {
		ophead = (xlog_op_header_t *)ptr;

		/* clientid is only 1 byte */
		field_offset = (__psint_t)
			       ((xfs_caddr_t)&(ophead->oh_clientid) - base_ptr);
		if (syncing == B_FALSE || (field_offset & 0x1ff)) {
			clientid = ophead->oh_clientid;
		} else {
			idx = BTOBB((xfs_caddr_t)&(ophead->oh_clientid) - iclog->ic_datap);
			if (idx > (XLOG_HEADER_CYCLE_SIZE / BBSIZE)) {
				j = idx / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				k = idx % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				clientid = GET_CLIENT_ID(xhdr[j].hic_xheader.xh_cycle_data[k], ARCH_CONVERT);
			} else {
				clientid = GET_CLIENT_ID(iclog->ic_header.h_cycle_data[idx], ARCH_CONVERT);
			}
		}
		if (clientid != XFS_TRANSACTION && clientid != XFS_LOG)
			cmn_err(CE_WARN, "xlog_verify_iclog: illegal clientid %d op 0x%p offset 0x%x", clientid, ophead, field_offset);

		/* check length */
		field_offset = (__psint_t)
			       ((xfs_caddr_t)&(ophead->oh_len) - base_ptr);
		if (syncing == B_FALSE || (field_offset & 0x1ff)) {
			op_len = INT_GET(ophead->oh_len, ARCH_CONVERT);
		} else {
			idx = BTOBB((__psint_t)&ophead->oh_len -
				    (__psint_t)iclog->ic_datap);
			if (idx > (XLOG_HEADER_CYCLE_SIZE / BBSIZE)) {
				j = idx / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				k = idx % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				op_len = INT_GET(xhdr[j].hic_xheader.xh_cycle_data[k], ARCH_CONVERT);
			} else {
				op_len = INT_GET(iclog->ic_header.h_cycle_data[idx], ARCH_CONVERT);
			}
		}
		ptr += sizeof(xlog_op_header_t) + op_len;
	}
}	/* xlog_verify_iclog */
#endif /* DEBUG && !XLOG_NOLOG */

/*
 * Mark all iclogs IOERROR. LOG_LOCK is held by the caller.
 */
STATIC int
xlog_state_ioerror(
	xlog_t	*log)
{
	xlog_in_core_t	*iclog, *ic;

	iclog = log->l_iclog;
	if (! (iclog->ic_state & XLOG_STATE_IOERROR)) {
		/*
		 * Mark all the incore logs IOERROR.
		 * From now on, no log flushes will result.
		 */
		ic = iclog;
		do {
			ic->ic_state = XLOG_STATE_IOERROR;
			ic = ic->ic_next;
		} while (ic != iclog);
		return (0);
	}
	/*
	 * Return non-zero, if state transition has already happened.
	 */
	return (1);
}

/*
 * This is called from xfs_force_shutdown, when we're forcibly
 * shutting down the filesystem, typically because of an IO error.
 * Our main objectives here are to make sure that:
 *	a. the filesystem gets marked 'SHUTDOWN' for all interested
 *	   parties to find out, 'atomically'.
 *	b. those who're sleeping on log reservations, pinned objects and
 *	    other resources get woken up, and be told the bad news.
 *	c. nothing new gets queued up after (a) and (b) are done.
 *	d. if !logerror, flush the iclogs to disk, then seal them off
 *	   for business.
 */
int
xfs_log_force_umount(
	struct xfs_mount	*mp,
	int			logerror)
{
	xlog_ticket_t	*tic;
	xlog_t		*log;
	int		retval;
	SPLDECL(s);
	SPLDECL(s2);

	log = mp->m_log;

	/*
	 * If this happens during log recovery, don't worry about
	 * locking; the log isn't open for business yet.
	 */
	if (!log ||
	    log->l_flags & XLOG_ACTIVE_RECOVERY) {
		mp->m_flags |= XFS_MOUNT_FS_SHUTDOWN;
		XFS_BUF_DONE(mp->m_sb_bp);
		return (0);
	}

	/*
	 * Somebody could've already done the hard work for us.
	 * No need to get locks for this.
	 */
	if (logerror && log->l_iclog->ic_state & XLOG_STATE_IOERROR) {
		ASSERT(XLOG_FORCED_SHUTDOWN(log));
		return (1);
	}
	retval = 0;
	/*
	 * We must hold both the GRANT lock and the LOG lock,
	 * before we mark the filesystem SHUTDOWN and wake
	 * everybody up to tell the bad news.
	 */
	s = GRANT_LOCK(log);
	s2 = LOG_LOCK(log);
	mp->m_flags |= XFS_MOUNT_FS_SHUTDOWN;
	XFS_BUF_DONE(mp->m_sb_bp);
	/*
	 * This flag is sort of redundant because of the mount flag, but
	 * it's good to maintain the separation between the log and the rest
	 * of XFS.
	 */
	log->l_flags |= XLOG_IO_ERROR;

	/*
	 * If we hit a log error, we want to mark all the iclogs IOERROR
	 * while we're still holding the loglock.
	 */
	if (logerror)
		retval = xlog_state_ioerror(log);
	LOG_UNLOCK(log, s2);

	/*
	 * We don't want anybody waiting for log reservations
	 * after this. That means we have to wake up everybody
	 * queued up on reserve_headq as well as write_headq.
	 * In addition, we make sure in xlog_{re}grant_log_space
	 * that we don't enqueue anything once the SHUTDOWN flag
	 * is set, and this action is protected by the GRANTLOCK.
	 */
	if ((tic = log->l_reserve_headq)) {
		do {
			sv_signal(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_reserve_headq);
	}

	if ((tic = log->l_write_headq)) {
		do {
			sv_signal(&tic->t_sema);
			tic = tic->t_next;
		} while (tic != log->l_write_headq);
	}
	GRANT_UNLOCK(log, s);

	if (! (log->l_iclog->ic_state & XLOG_STATE_IOERROR)) {
		ASSERT(!logerror);
		/*
		 * Force the incore logs to disk before shutting the
		 * log down completely.
		 */
		xlog_state_sync_all(log, XFS_LOG_FORCE|XFS_LOG_SYNC);
		s2 = LOG_LOCK(log);
		retval = xlog_state_ioerror(log);
		LOG_UNLOCK(log, s2);
	}
	/*
	 * Wake up everybody waiting on xfs_log_force.
	 * Callback all log item committed functions as if the
	 * log writes were completed.
	 */
	xlog_state_do_callback(log, XFS_LI_ABORTED, NULL);

#ifdef XFSERRORDEBUG
	{
		xlog_in_core_t	*iclog;

		s = LOG_LOCK(log);
		iclog = log->l_iclog;
		do {
			ASSERT(iclog->ic_callback == 0);
			iclog = iclog->ic_next;
		} while (iclog != log->l_iclog);
		LOG_UNLOCK(log, s);
	}
#endif
	/* return non-zero if log IOERROR transition had already happened */
	return (retval);
}

int
xlog_iclogs_empty(xlog_t *log)
{
	xlog_in_core_t	*iclog;

	iclog = log->l_iclog;
	do {
		/* endianness does not matter here, zero is zero in
		 * any language.
		 */
		if (iclog->ic_header.h_num_logops)
			return(0);
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);
	return(1);
}
