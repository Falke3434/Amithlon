/*
 * linux/include/linux/nfsd/nfsd.h
 *
 * Hodge-podge collection of knfsd-related stuff.
 * I will sort this out later.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_NFSD_H
#define LINUX_NFSD_NFSD_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/dirent.h>
#include <linux/fs.h>

#include <linux/nfsd/debug.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/export.h>
#include <linux/nfsd/auth.h>
#include <linux/nfsd/stats.h>
#include <linux/nfsd/interface.h>
/*
 * nfsd version
 */
#define NFSD_VERSION		"0.5"
#define NFSD_SUPPORTED_MINOR_VERSION	0

#ifdef __KERNEL__
/*
 * Special flags for nfsd_permission. These must be different from MAY_READ,
 * MAY_WRITE, and MAY_EXEC.
 */
#define MAY_NOP			0
#define MAY_SATTR		8
#define MAY_TRUNC		16
#define MAY_LOCK		32
#define MAY_OWNER_OVERRIDE	64
#define	MAY_LOCAL_ACCESS	128 /* IRIX doing local access check on device special file*/
#if (MAY_SATTR | MAY_TRUNC | MAY_LOCK | MAY_OWNER_OVERRIDE | MAY_LOCAL_ACCESS) & (MAY_READ | MAY_WRITE | MAY_EXEC)
# error "please use a different value for MAY_SATTR or MAY_TRUNC or MAY_LOCK or MAY_LOCAL_ACCESS or MAY_OWNER_OVERRIDE."
#endif
#define MAY_CREATE		(MAY_EXEC|MAY_WRITE)
#define MAY_REMOVE		(MAY_EXEC|MAY_WRITE|MAY_TRUNC)

/*
 * Callback function for readdir
 */
struct readdir_cd {
	int			err;	/* 0, nfserr, or nfserr_eof */
};
typedef int		(*encode_dent_fn)(struct readdir_cd *, const char *,
						int, loff_t, ino_t, unsigned int);
typedef int (*nfsd_dirop_t)(struct inode *, struct dentry *, int, int);

extern struct svc_program	nfsd_program;
extern struct svc_version	nfsd_version2, nfsd_version3,
				nfsd_version4;

/*
 * Function prototypes.
 */
int		nfsd_svc(unsigned short port, int nrservs);
int		nfsd_dispatch(struct svc_rqst *rqstp, u32 *statp);

/* nfsd/vfs.c */
int		fh_lock_parent(struct svc_fh *, struct dentry *);
int		nfsd_racache_init(int);
void		nfsd_racache_shutdown(void);
int		nfsd_cross_mnt(struct svc_rqst *rqstp, struct dentry **dpp,
		                struct svc_export **expp);
int		nfsd_lookup(struct svc_rqst *, struct svc_fh *,
				const char *, int, struct svc_fh *);
int		nfsd_setattr(struct svc_rqst *, struct svc_fh *,
				struct iattr *, int, time_t);
int		nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				int type, dev_t rdev, struct svc_fh *res);
#ifdef CONFIG_NFSD_V3
int		nfsd_access(struct svc_rqst *, struct svc_fh *, u32 *, u32 *);
int		nfsd_create_v3(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				struct svc_fh *res, int createmode,
				u32 *verifier, int *truncp);
int		nfsd_commit(struct svc_rqst *, struct svc_fh *,
				off_t, unsigned long);
#endif /* CONFIG_NFSD_V3 */
int		nfsd_open(struct svc_rqst *, struct svc_fh *, int,
				int, struct file *);
void		nfsd_close(struct file *);
int		nfsd_read(struct svc_rqst *, struct svc_fh *,
				loff_t, struct iovec *,int, unsigned long *);
int		nfsd_write(struct svc_rqst *, struct svc_fh *,
				loff_t, struct iovec *,int, unsigned long, int *);
int		nfsd_readlink(struct svc_rqst *, struct svc_fh *,
				char *, int *);
int		nfsd_symlink(struct svc_rqst *, struct svc_fh *,
				char *name, int len, char *path, int plen,
				struct svc_fh *res, struct iattr *);
int		nfsd_link(struct svc_rqst *, struct svc_fh *,
				char *, int, struct svc_fh *);
int		nfsd_rename(struct svc_rqst *,
				struct svc_fh *, char *, int,
				struct svc_fh *, char *, int);
int		nfsd_remove(struct svc_rqst *,
				struct svc_fh *, char *, int);
int		nfsd_unlink(struct svc_rqst *, struct svc_fh *, int type,
				char *name, int len);
int		nfsd_truncate(struct svc_rqst *, struct svc_fh *,
				unsigned long size);
int		nfsd_readdir(struct svc_rqst *, struct svc_fh *,
			     loff_t *, struct readdir_cd *, encode_dent_fn);
int		nfsd_statfs(struct svc_rqst *, struct svc_fh *,
				struct kstatfs *);

int		nfsd_notify_change(struct inode *, struct iattr *);
int		nfsd_permission(struct svc_export *, struct dentry *, int);


/* 
 * NFSv4 State
 */
#ifdef CONFIG_NFSD_V4
void nfs4_state_init(void);
void nfs4_state_shutdown(void);
#else
void static inline nfs4_state_init(void){}
void static inline nfs4_state_shutdown(void){}
#endif

/*
 * lockd binding
 */
void		nfsd_lockd_init(void);
void		nfsd_lockd_shutdown(void);


/*
 * These macros provide pre-xdr'ed values for faster operation.
 */
#define	nfs_ok			__constant_htonl(NFS_OK)
#define	nfserr_perm		__constant_htonl(NFSERR_PERM)
#define	nfserr_noent		__constant_htonl(NFSERR_NOENT)
#define	nfserr_io		__constant_htonl(NFSERR_IO)
#define	nfserr_nxio		__constant_htonl(NFSERR_NXIO)
#define	nfserr_eagain		__constant_htonl(NFSERR_EAGAIN)
#define	nfserr_acces		__constant_htonl(NFSERR_ACCES)
#define	nfserr_exist		__constant_htonl(NFSERR_EXIST)
#define	nfserr_xdev		__constant_htonl(NFSERR_XDEV)
#define	nfserr_nodev		__constant_htonl(NFSERR_NODEV)
#define	nfserr_notdir		__constant_htonl(NFSERR_NOTDIR)
#define	nfserr_isdir		__constant_htonl(NFSERR_ISDIR)
#define	nfserr_inval		__constant_htonl(NFSERR_INVAL)
#define	nfserr_fbig		__constant_htonl(NFSERR_FBIG)
#define	nfserr_nospc		__constant_htonl(NFSERR_NOSPC)
#define	nfserr_rofs		__constant_htonl(NFSERR_ROFS)
#define	nfserr_mlink		__constant_htonl(NFSERR_MLINK)
#define	nfserr_opnotsupp	__constant_htonl(NFSERR_OPNOTSUPP)
#define	nfserr_nametoolong	__constant_htonl(NFSERR_NAMETOOLONG)
#define	nfserr_notempty		__constant_htonl(NFSERR_NOTEMPTY)
#define	nfserr_dquot		__constant_htonl(NFSERR_DQUOT)
#define	nfserr_stale		__constant_htonl(NFSERR_STALE)
#define	nfserr_remote		__constant_htonl(NFSERR_REMOTE)
#define	nfserr_wflush		__constant_htonl(NFSERR_WFLUSH)
#define	nfserr_badhandle	__constant_htonl(NFSERR_BADHANDLE)
#define	nfserr_notsync		__constant_htonl(NFSERR_NOT_SYNC)
#define	nfserr_badcookie	__constant_htonl(NFSERR_BAD_COOKIE)
#define	nfserr_notsupp		__constant_htonl(NFSERR_NOTSUPP)
#define	nfserr_toosmall		__constant_htonl(NFSERR_TOOSMALL)
#define	nfserr_serverfault	__constant_htonl(NFSERR_SERVERFAULT)
#define	nfserr_badtype		__constant_htonl(NFSERR_BADTYPE)
#define	nfserr_jukebox		__constant_htonl(NFSERR_JUKEBOX)
#define nfserr_expired          __constant_htonl(NFSERR_EXPIRED)
#define	nfserr_bad_cookie	__constant_htonl(NFSERR_BAD_COOKIE)
#define	nfserr_same		__constant_htonl(NFSERR_SAME)
#define	nfserr_clid_inuse	__constant_htonl(NFSERR_CLID_INUSE)
#define	nfserr_stale_clientid	__constant_htonl(NFSERR_STALE_CLIENTID)
#define	nfserr_resource		__constant_htonl(NFSERR_RESOURCE)
#define	nfserr_nofilehandle	__constant_htonl(NFSERR_NOFILEHANDLE)
#define	nfserr_minor_vers_mismatch	__constant_htonl(NFSERR_MINOR_VERS_MISMATCH)
#define nfserr_share_denied	__constant_htonl(NFSERR_SHARE_DENIED)
#define nfserr_stale_stateid	__constant_htonl(NFSERR_STALE_STATEID)
#define nfserr_old_stateid	__constant_htonl(NFSERR_OLD_STATEID)
#define nfserr_bad_stateid	__constant_htonl(NFSERR_BAD_STATEID)
#define nfserr_bad_seqid	__constant_htonl(NFSERR_BAD_SEQID)
#define	nfserr_symlink		__constant_htonl(NFSERR_SYMLINK)
#define	nfserr_not_same		__constant_htonl(NFSERR_NOT_SAME)
#define	nfserr_readdir_nospc	__constant_htonl(NFSERR_READDIR_NOSPC)
#define	nfserr_bad_xdr		__constant_htonl(NFSERR_BAD_XDR)
#define	nfserr_openmode		__constant_htonl(NFSERR_OPENMODE)

/* error codes for internal use */
/* if a request fails due to kmalloc failure, it gets dropped.
 *  Client should resend eventually
 */
#define	nfserr_dropit		__constant_htonl(30000)
/* end-of-file indicator in readdir */
#define	nfserr_eof		__constant_htonl(30001)

/* Check for dir entries '.' and '..' */
#define isdotent(n, l)	(l < 3 && n[0] == '.' && (l == 1 || n[1] == '.'))

/*
 * Time of server startup
 */
extern struct timeval	nfssvc_boot;


#ifdef CONFIG_NFSD_V4

/* before processing a COMPOUND operation, we have to check that there
 * is enough space in the buffer for XDR encode to succeed.  otherwise,
 * we might process an operation with side effects, and be unable to
 * tell the client that the operation succeeded.
 *
 * COMPOUND_SLACK_SPACE - this is the minimum amount of buffer space
 * needed to encode an "ordinary" _successful_ operation.  (GETATTR,
 * READ, READDIR, and READLINK have their own buffer checks.)  if we
 * fall below this level, we fail the next operation with NFS4ERR_RESOURCE.
 *
 * COMPOUND_ERR_SLACK_SPACE - this is the minimum amount of buffer space
 * needed to encode an operation which has failed with NFS4ERR_RESOURCE.
 * care is taken to ensure that we never fall below this level for any
 * reason.
 */
#define	COMPOUND_SLACK_SPACE		140    /* OP_GETFH */
#define COMPOUND_ERR_SLACK_SPACE	12     /* OP_SETATTR */

#define NFSD_LEASE_TIME			60  /* seconds */
#define NFSD_LAUNDROMAT_MINTIMEOUT      10   /* seconds */

/*
 * The following attributes are currently not supported by the NFSv4 server:
 *    ACL           (will be supported in a forthcoming patch)
 *    ARCHIVE       (deprecated anyway)
 *    FS_LOCATIONS  (will be supported eventually)
 *    HIDDEN        (unlikely to be supported any time soon)
 *    MIMETYPE      (unlikely to be supported any time soon)
 *    QUOTA_*       (will be supported in a forthcoming patch)
 *    SYSTEM        (unlikely to be supported any time soon)
 *    TIME_BACKUP   (unlikely to be supported any time soon)
 *    TIME_CREATE   (unlikely to be supported any time soon)
 */
#define NFSD_SUPPORTED_ATTRS_WORD0                                                          \
(FATTR4_WORD0_SUPPORTED_ATTRS   | FATTR4_WORD0_TYPE         | FATTR4_WORD0_FH_EXPIRE_TYPE   \
 | FATTR4_WORD0_CHANGE          | FATTR4_WORD0_SIZE         | FATTR4_WORD0_LINK_SUPPORT     \
 | FATTR4_WORD0_SYMLINK_SUPPORT | FATTR4_WORD0_NAMED_ATTR   | FATTR4_WORD0_FSID             \
 | FATTR4_WORD0_UNIQUE_HANDLES  | FATTR4_WORD0_LEASE_TIME   | FATTR4_WORD0_RDATTR_ERROR     \
 | FATTR4_WORD0_ACLSUPPORT      | FATTR4_WORD0_CANSETTIME   | FATTR4_WORD0_CASE_INSENSITIVE \
 | FATTR4_WORD0_CASE_PRESERVING | FATTR4_WORD0_CHOWN_RESTRICTED                             \
 | FATTR4_WORD0_FILEHANDLE      | FATTR4_WORD0_FILEID       | FATTR4_WORD0_FILES_AVAIL      \
 | FATTR4_WORD0_FILES_FREE      | FATTR4_WORD0_FILES_TOTAL  | FATTR4_WORD0_HOMOGENEOUS      \
 | FATTR4_WORD0_MAXFILESIZE     | FATTR4_WORD0_MAXLINK      | FATTR4_WORD0_MAXNAME          \
 | FATTR4_WORD0_MAXREAD         | FATTR4_WORD0_MAXWRITE)

#define NFSD_SUPPORTED_ATTRS_WORD1                                                          \
(FATTR4_WORD1_MODE              | FATTR4_WORD1_NO_TRUNC     | FATTR4_WORD1_NUMLINKS         \
 | FATTR4_WORD1_OWNER	        | FATTR4_WORD1_OWNER_GROUP  | FATTR4_WORD1_RAWDEV           \
 | FATTR4_WORD1_SPACE_AVAIL     | FATTR4_WORD1_SPACE_FREE   | FATTR4_WORD1_SPACE_TOTAL      \
 | FATTR4_WORD1_SPACE_USED      | FATTR4_WORD1_TIME_ACCESS  | FATTR4_WORD1_TIME_ACCESS_SET  \
 | FATTR4_WORD1_TIME_CREATE     | FATTR4_WORD1_TIME_DELTA   | FATTR4_WORD1_TIME_METADATA    \
 | FATTR4_WORD1_TIME_MODIFY     | FATTR4_WORD1_TIME_MODIFY_SET)

/* These will return ERR_INVAL if specified in GETATTR or READDIR. */
#define NFSD_WRITEONLY_ATTRS_WORD1							    \
(FATTR4_WORD1_TIME_ACCESS_SET   | FATTR4_WORD1_TIME_MODIFY_SET)

/* These are the only attrs allowed in CREATE/OPEN/SETATTR. */
#define NFSD_WRITEABLE_ATTRS_WORD0                            FATTR4_WORD0_SIZE
#define NFSD_WRITEABLE_ATTRS_WORD1                                                          \
(FATTR4_WORD1_MODE              | FATTR4_WORD1_OWNER         | FATTR4_WORD1_OWNER_GROUP     \
 | FATTR4_WORD1_TIME_ACCESS_SET | FATTR4_WORD1_TIME_METADATA | FATTR4_WORD1_TIME_MODIFY_SET)

#endif /* CONFIG_NFSD_V4 */

#endif /* __KERNEL__ */

#endif /* LINUX_NFSD_NFSD_H */
