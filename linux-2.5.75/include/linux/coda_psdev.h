#ifndef __CODA_PSDEV_H
#define __CODA_PSDEV_H

#define CODA_PSDEV_MAJOR 67
#define MAX_CODADEVS  5	   /* how many do we allow */

#define CODA_SUPER_MAGIC	0x73757245

struct statfs;

struct coda_sb_info
{
	struct venus_comm * sbi_vcomm;
	struct super_block *sbi_sb;
	struct list_head    sbi_cihead;
};

/* communication pending/processing queues */
struct venus_comm {
	u_long		    vc_seq;
	wait_queue_head_t   vc_waitq; /* Venus wait queue */
	struct list_head    vc_pending;
	struct list_head    vc_processing;
	int                 vc_inuse;
	struct super_block *vc_sb;
};


static inline struct coda_sb_info *coda_sbp(struct super_block *sb)
{
    return ((struct coda_sb_info *)((sb)->s_fs_info));
}


/* upcalls */
int venus_rootfid(struct super_block *sb, ViceFid *fidp);
int venus_getattr(struct super_block *sb, struct ViceFid *fid, 
		     struct coda_vattr *attr);
int venus_setattr(struct super_block *, struct ViceFid *, 
		     struct coda_vattr *);
int venus_lookup(struct super_block *sb, struct ViceFid *fid, 
		    const char *name, int length, int *type, 
		    struct ViceFid *resfid);
int venus_store(struct super_block *sb, struct ViceFid *fid, int flags,
		struct coda_cred *);
int venus_release(struct super_block *sb, struct ViceFid *fid, int flags);
int venus_close(struct super_block *sb, struct ViceFid *fid, int flags,
		struct coda_cred *);
int venus_open(struct super_block *sb, struct ViceFid *fid,
		int flags, struct file **f);
int venus_mkdir(struct super_block *sb, struct ViceFid *dirfid, 
		const char *name, int length, 
		struct ViceFid *newfid, struct coda_vattr *attrs);
int venus_create(struct super_block *sb, struct ViceFid *dirfid, 
		 const char *name, int length, int excl, int mode, dev_t rdev,
		 struct ViceFid *newfid, struct coda_vattr *attrs) ;
int venus_rmdir(struct super_block *sb, struct ViceFid *dirfid, 
		const char *name, int length);
int venus_remove(struct super_block *sb, struct ViceFid *dirfid, 
		 const char *name, int length);
int venus_readlink(struct super_block *sb, struct ViceFid *fid, 
		   char *buffer, int *length);
int venus_rename(struct super_block *, struct ViceFid *new_fid, 
		 struct ViceFid *old_fid, size_t old_length, 
		 size_t new_length, const char *old_name, 
		 const char *new_name);
int venus_link(struct super_block *sb, struct ViceFid *fid, 
		  struct ViceFid *dirfid, const char *name, int len );
int venus_symlink(struct super_block *sb, struct ViceFid *fid,
		  const char *name, int len, const char *symname, int symlen);
int venus_access(struct super_block *sb, struct ViceFid *fid, int mask);
int venus_pioctl(struct super_block *sb, struct ViceFid *fid,
		 unsigned int cmd, struct PioctlData *data);
int coda_downcall(int opcode, union outputArgs *out, struct super_block *sb);
int venus_fsync(struct super_block *sb, struct ViceFid *fid);
int venus_statfs(struct super_block *sb, struct kstatfs *sfs);


/* messages between coda filesystem in kernel and Venus */
extern int coda_hard;
extern unsigned long coda_timeout;
struct upc_req {
	struct list_head    uc_chain;
	caddr_t	            uc_data;
	u_short	            uc_flags;
	u_short             uc_inSize;  /* Size is at most 5000 bytes */
	u_short	            uc_outSize;
	u_short	            uc_opcode;  /* copied from data to save lookup */
	int		    uc_unique;
	wait_queue_head_t   uc_sleep;   /* process' wait queue */
	unsigned long       uc_posttime;
};

#define REQ_ASYNC  0x1
#define REQ_READ   0x2
#define REQ_WRITE  0x4
#define REQ_ABORT  0x8


/*
 * Statistics
 */

extern struct venus_comm coda_comms[];

#endif
