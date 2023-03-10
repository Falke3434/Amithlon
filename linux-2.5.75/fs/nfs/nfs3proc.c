/*
 *  linux/fs/nfs/nfs3proc.c
 *
 *  Client-side NFSv3 procedures stubs.
 *
 *  Copyright (C) 1997, Olaf Kirch
 */

#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>

#define NFSDBG_FACILITY		NFSDBG_PROC

extern struct rpc_procinfo nfs3_procedures[];

/* A wrapper to handle the EJUKEBOX error message */
static int
nfs3_rpc_wrapper(struct rpc_clnt *clnt, struct rpc_message *msg, int flags)
{
	sigset_t oldset;
	int res;
	rpc_clnt_sigmask(clnt, &oldset);
	do {
		res = rpc_call_sync(clnt, msg, flags);
		if (res != -EJUKEBOX)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(NFS_JUKEBOX_RETRY_TIME);
		res = -ERESTARTSYS;
	} while (!signalled());
	rpc_clnt_sigunmask(clnt, &oldset);
	return res;
}

static inline int
nfs3_rpc_call_wrapper(struct rpc_clnt *clnt, u32 proc, void *argp, void *resp, int flags)
{
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[proc],
		.rpc_argp	= argp,
		.rpc_resp	= resp,
	};
	return nfs3_rpc_wrapper(clnt, &msg, flags);
}

#define rpc_call(clnt, proc, argp, resp, flags) \
		nfs3_rpc_call_wrapper(clnt, proc, argp, resp, flags)
#define rpc_call_sync(clnt, msg, flags) \
		nfs3_rpc_wrapper(clnt, msg, flags)

static int
nfs3_async_handle_jukebox(struct rpc_task *task)
{
	if (task->tk_status != -EJUKEBOX)
		return 0;
	task->tk_status = 0;
	rpc_restart_call(task);
	rpc_delay(task, NFS_JUKEBOX_RETRY_TIME);
	return 1;
}

static void
nfs3_write_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	if (fattr->valid & NFS_ATTR_FATTR) {
		if (!(fattr->valid & NFS_ATTR_WCC)) {
			fattr->pre_size  = NFS_CACHE_ISIZE(inode);
			fattr->pre_mtime = NFS_CACHE_MTIME(inode);
			fattr->pre_ctime = NFS_CACHE_CTIME(inode);
			fattr->valid |= NFS_ATTR_WCC;
		}
		nfs_refresh_inode(inode, fattr);
	}
}

/*
 * Bare-bones access to getattr: this is for nfs_read_super.
 */
static int
nfs3_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_fattr *fattr)
{
	int	status;

	dprintk("NFS call  getroot\n");
	fattr->valid = 0;
	status = rpc_call(server->client, NFS3PROC_GETATTR, fhandle, fattr, 0);
	dprintk("NFS reply getroot\n");
	return status;
}

/*
 * One function for each procedure in the NFS protocol.
 */
static int
nfs3_proc_getattr(struct inode *inode, struct nfs_fattr *fattr)
{
	int	status;

	dprintk("NFS call  getattr\n");
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFS3PROC_GETATTR,
			  NFS_FH(inode), fattr, 0);
	dprintk("NFS reply getattr\n");
	return status;
}

static int
nfs3_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
			struct iattr *sattr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs3_sattrargs	arg = {
		.fh		= NFS_FH(inode),
		.sattr		= sattr,
	};
	int	status;

	dprintk("NFS call  setattr\n");
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFS3PROC_SETATTR, &arg, fattr, 0);
	dprintk("NFS reply setattr\n");
	return status;
}

static int
nfs3_proc_lookup(struct inode *dir, struct qstr *name,
		 struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  lookup %s\n", name->name);
	dir_attr.valid = 0;
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_LOOKUP, &arg, &res, 0);
	if (status >= 0 && !(fattr->valid & NFS_ATTR_FATTR))
		status = rpc_call(NFS_CLIENT(dir), NFS3PROC_GETATTR,
			 fhandle, fattr, 0);
	dprintk("NFS reply lookup: %d\n", status);
	if (status >= 0)
		status = nfs_refresh_inode(dir, &dir_attr);
	return status;
}

static int
nfs3_proc_access(struct inode *inode, struct rpc_cred *cred, int mode)
{
	struct nfs_fattr	fattr;
	struct nfs3_accessargs	arg = {
		.fh		= NFS_FH(inode),
	};
	struct nfs3_accessres	res = {
		.fattr		= &fattr,
	};
	struct rpc_message msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_ACCESS],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int	status;

	dprintk("NFS call  access\n");
	fattr.valid = 0;

	if (mode & MAY_READ)
		arg.access |= NFS3_ACCESS_READ;
	if (S_ISDIR(inode->i_mode)) {
		if (mode & MAY_WRITE)
			arg.access |= NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND | NFS3_ACCESS_DELETE;
		if (mode & MAY_EXEC)
			arg.access |= NFS3_ACCESS_LOOKUP;
	} else {
		if (mode & MAY_WRITE)
			arg.access |= NFS3_ACCESS_MODIFY | NFS3_ACCESS_EXTEND;
		if (mode & MAY_EXEC)
			arg.access |= NFS3_ACCESS_EXECUTE;
	}
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, 0);
	nfs_refresh_inode(inode, &fattr);
	dprintk("NFS reply access\n");

	if (status == 0 && (arg.access & res.access) != arg.access)
		status = -EACCES;
	return status;
}

static int
nfs3_proc_readlink(struct inode *inode, struct page *page)
{
	struct nfs_fattr	fattr;
	struct nfs3_readlinkargs args = {
		.fh		= NFS_FH(inode),
		.count		= PAGE_CACHE_SIZE,
		.pages		= &page
	};
	int			status;

	dprintk("NFS call  readlink\n");
	fattr.valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFS3PROC_READLINK,
			  &args, &fattr, 0);
	nfs_refresh_inode(inode, &fattr);
	dprintk("NFS reply readlink: %d\n", status);
	return status;
}

static int
nfs3_proc_read(struct inode *inode, struct rpc_cred *cred,
	       struct nfs_fattr *fattr, int flags,
	       unsigned int base, unsigned int count, struct page *page,
	       int *eofp)
{
	u64			offset = page_offset(page) + base;
	struct nfs_readargs	arg = {
		.fh		= NFS_FH(inode),
		.offset		= offset,
		.count		= count,
		.pgbase		= base,
		.pages		= &page
	};
	struct nfs_readres	res = {
		.fattr		= fattr,
		.count		= count,
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READ],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int			status;

	dprintk("NFS call  read %d @ %Ld\n", count, (long long)offset);
	fattr->valid = 0;
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, flags);
	if (status >= 0)
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS reply read: %d\n", status);
	*eofp = res.eof;
	return status;
}

static int
nfs3_proc_write(struct inode *inode, struct rpc_cred *cred,
		struct nfs_fattr *fattr, int flags,
		unsigned int base, unsigned int count,
		struct page *page, struct nfs_writeverf *verf)
{
	u64			offset = page_offset(page) + base;
	struct nfs_writeargs	arg = {
		.fh		= NFS_FH(inode),
		.offset		= offset,
		.count		= count,
		.stable		= NFS_FILE_SYNC,
		.pgbase		= base,
		.pages		= &page
	};
	struct nfs_writeres	res = {
		.fattr		= fattr,
		.verf		= verf,
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_WRITE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int			status, rpcflags = 0;

	dprintk("NFS call  write %d @ %Ld\n", count, (long long)offset);
	fattr->valid = 0;
	if (flags & NFS_RW_SWAP)
		rpcflags |= NFS_RPC_SWAPFLAGS;
	arg.stable = (flags & NFS_RW_SYNC) ? NFS_FILE_SYNC : NFS_UNSTABLE;

	status = rpc_call_sync(NFS_CLIENT(inode), &msg, rpcflags);

	if (status >= 0)
		nfs3_write_refresh_inode(inode, fattr);

	dprintk("NFS reply read: %d\n", status);
	return status < 0? status : res.count;
}

/*
 * Create a regular file.
 * For now, we don't implement O_EXCL.
 */
static int
nfs3_proc_create(struct inode *dir, struct qstr *name, struct iattr *sattr,
		 int flags, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_createargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr,
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  create %s\n", name->name);
	arg.createmode = NFS3_CREATE_UNCHECKED;
	if (flags & O_EXCL) {
		arg.createmode  = NFS3_CREATE_EXCLUSIVE;
		arg.verifier[0] = jiffies;
		arg.verifier[1] = current->pid;
	}

again:
	dir_attr.valid = 0;
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_CREATE, &arg, &res, 0);
	nfs_refresh_inode(dir, &dir_attr);

	/* If the server doesn't support the exclusive creation semantics,
	 * try again with simple 'guarded' mode. */
	if (status == NFSERR_NOTSUPP) {
		switch (arg.createmode) {
			case NFS3_CREATE_EXCLUSIVE:
				arg.createmode = NFS3_CREATE_GUARDED;
				break;

			case NFS3_CREATE_GUARDED:
				arg.createmode = NFS3_CREATE_UNCHECKED;
				break;

			case NFS3_CREATE_UNCHECKED:
				goto exit;
		}
		goto again;
	}

exit:
	dprintk("NFS reply create: %d\n", status);

	/* When we created the file with exclusive semantics, make
	 * sure we set the attributes afterwards. */
	if (status == 0 && arg.createmode == NFS3_CREATE_EXCLUSIVE) {
		struct nfs3_sattrargs	arg = {
			.fh		= fhandle,
			.sattr		= sattr,
		};
		dprintk("NFS call  setattr (post-create)\n");

		/* Note: we could use a guarded setattr here, but I'm
		 * not sure this buys us anything (and I'd have
		 * to revamp the NFSv3 XDR code) */
		fattr->valid = 0;
		status = rpc_call(NFS_CLIENT(dir), NFS3PROC_SETATTR,
						&arg, fattr, 0);
		dprintk("NFS reply setattr (post-create): %d\n", status);
	}

	return status;
}

static int
nfs3_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_REMOVE],
		.rpc_argp	= &arg,
		.rpc_resp	= &dir_attr,
	};
	int			status;

	dprintk("NFS call  remove %s\n", name->name);
	dir_attr.valid = 0;
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply remove: %d\n", status);
	return status;
}

static int
nfs3_proc_unlink_setup(struct rpc_message *msg, struct dentry *dir, struct qstr *name)
{
	struct nfs3_diropargs	*arg;
	struct nfs_fattr	*res;

	arg = (struct nfs3_diropargs *)kmalloc(sizeof(*arg)+sizeof(*res), GFP_KERNEL);
	if (!arg)
		return -ENOMEM;
	res = (struct nfs_fattr*)(arg + 1);
	arg->fh = NFS_FH(dir->d_inode);
	arg->name = name->name;
	arg->len = name->len;
	res->valid = 0;
	msg->rpc_proc = &nfs3_procedures[NFS3PROC_REMOVE];
	msg->rpc_argp = arg;
	msg->rpc_resp = res;
	return 0;
}

static int
nfs3_proc_unlink_done(struct dentry *dir, struct rpc_task *task)
{
	struct rpc_message *msg = &task->tk_msg;
	struct nfs_fattr	*dir_attr;

	if (nfs3_async_handle_jukebox(task))
		return 1;
	if (msg->rpc_argp) {
		dir_attr = (struct nfs_fattr*)msg->rpc_resp;
		nfs_refresh_inode(dir->d_inode, dir_attr);
		kfree(msg->rpc_argp);
	}
	return 0;
}

static int
nfs3_proc_rename(struct inode *old_dir, struct qstr *old_name,
		 struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_fattr	old_dir_attr, new_dir_attr;
	struct nfs3_renameargs	arg = {
		.fromfh		= NFS_FH(old_dir),
		.fromname	= old_name->name,
		.fromlen	= old_name->len,
		.tofh		= NFS_FH(new_dir),
		.toname		= new_name->name,
		.tolen		= new_name->len
	};
	struct nfs3_renameres	res = {
		.fromattr	= &old_dir_attr,
		.toattr		= &new_dir_attr
	};
	int			status;

	dprintk("NFS call  rename %s -> %s\n", old_name->name, new_name->name);
	old_dir_attr.valid = 0;
	new_dir_attr.valid = 0;
	status = rpc_call(NFS_CLIENT(old_dir), NFS3PROC_RENAME, &arg, &res, 0);
	nfs_refresh_inode(old_dir, &old_dir_attr);
	nfs_refresh_inode(new_dir, &new_dir_attr);
	dprintk("NFS reply rename: %d\n", status);
	return status;
}

static int
nfs3_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs_fattr	dir_attr, fattr;
	struct nfs3_linkargs	arg = {
		.fromfh		= NFS_FH(inode),
		.tofh		= NFS_FH(dir),
		.toname		= name->name,
		.tolen		= name->len
	};
	struct nfs3_linkres	res = {
		.dir_attr	= &dir_attr,
		.fattr		= &fattr
	};
	int			status;

	dprintk("NFS call  link %s\n", name->name);
	dir_attr.valid = 0;
	fattr.valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFS3PROC_LINK, &arg, &res, 0);
	nfs_refresh_inode(dir, &dir_attr);
	nfs_refresh_inode(inode, &fattr);
	dprintk("NFS reply link: %d\n", status);
	return status;
}

static int
nfs3_proc_symlink(struct inode *dir, struct qstr *name, struct qstr *path,
		  struct iattr *sattr, struct nfs_fh *fhandle,
		  struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_symlinkargs	arg = {
		.fromfh		= NFS_FH(dir),
		.fromname	= name->name,
		.fromlen	= name->len,
		.topath		= path->name,
		.tolen		= path->len,
		.sattr		= sattr
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  symlink %s -> %s\n", name->name, path->name);
	dir_attr.valid = 0;
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_SYMLINK, &arg, &res, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

static int
nfs3_proc_mkdir(struct inode *dir, struct qstr *name, struct iattr *sattr,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_mkdirargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  mkdir %s\n", name->name);
	dir_attr.valid = 0;
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_MKDIR, &arg, &res, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply mkdir: %d\n", status);
	return status;
}

static int
nfs3_proc_rmdir(struct inode *dir, struct qstr *name)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	int			status;

	dprintk("NFS call  rmdir %s\n", name->name);
	dir_attr.valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_RMDIR, &arg, &dir_attr, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply rmdir: %d\n", status);
	return status;
}

/*
 * The READDIR implementation is somewhat hackish - we pass the user buffer
 * to the encode function, which installs it in the receive iovec.
 * The decode function itself doesn't perform any decoding, it just makes
 * sure the reply is syntactically correct.
 *
 * Also note that this implementation handles both plain readdir and
 * readdirplus.
 */
static int
nfs3_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
		  u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs_fattr	dir_attr;
	u32			*verf = NFS_COOKIEVERF(dir);
	struct nfs3_readdirargs	arg = {
		.fh		= NFS_FH(dir),
		.cookie		= cookie,
		.verf		= {verf[0], verf[1]},
		.plus		= plus,
		.count		= count,
		.pages		= &page
	};
	struct nfs3_readdirres	res = {
		.dir_attr	= &dir_attr,
		.verf		= verf,
		.plus		= plus
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int			status;

	lock_kernel();

	if (plus)
		msg.rpc_proc = &nfs3_procedures[NFS3PROC_READDIRPLUS];

	dprintk("NFS call  readdir%s %d\n",
			plus? "plus" : "", (unsigned int) cookie);

	dir_attr.valid = 0;
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply readdir: %d\n", status);
	unlock_kernel();
	return status;
}

static int
nfs3_proc_mknod(struct inode *dir, struct qstr *name, struct iattr *sattr,
		dev_t rdev, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs_fattr	dir_attr;
	struct nfs3_mknodargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr,
		.rdev		= rdev
	};
	struct nfs3_diropres	res = {
		.dir_attr	= &dir_attr,
		.fh		= fh,
		.fattr		= fattr
	};
	int			status;

	switch (sattr->ia_mode & S_IFMT) {
	case S_IFBLK:	arg.type = NF3BLK;  break;
	case S_IFCHR:	arg.type = NF3CHR;  break;
	case S_IFIFO:	arg.type = NF3FIFO; break;
	case S_IFSOCK:	arg.type = NF3SOCK; break;
	default:	return -EINVAL;
	}

	dprintk("NFS call  mknod %s %x\n", name->name, (unsigned)rdev);
	dir_attr.valid = 0;
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFS3PROC_MKNOD, &arg, &res, 0);
	nfs_refresh_inode(dir, &dir_attr);
	dprintk("NFS reply mknod: %d\n", status);
	return status;
}

static int
nfs3_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsstat *stat)
{
	int	status;

	dprintk("NFS call  fsstat\n");
	stat->fattr->valid = 0;
	status = rpc_call(server->client, NFS3PROC_FSSTAT, fhandle, stat, 0);
	dprintk("NFS reply statfs: %d\n", status);
	return status;
}

static int
nfs3_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
		 struct nfs_fsinfo *info)
{
	int	status;

	dprintk("NFS call  fsinfo\n");
	info->fattr->valid = 0;
	status = rpc_call(server->client, NFS3PROC_FSINFO, fhandle, info, 0);
	dprintk("NFS reply fsinfo: %d\n", status);
	return status;
}

static int
nfs3_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		   struct nfs_pathconf *info)
{
	int	status;

	dprintk("NFS call  pathconf\n");
	info->fattr->valid = 0;
	status = rpc_call(server->client, NFS3PROC_PATHCONF, fhandle, info, 0);
	dprintk("NFS reply pathconf: %d\n", status);
	return status;
}

extern u32 *nfs3_decode_dirent(u32 *, struct nfs_entry *, int);

static void
nfs3_read_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;

	if (nfs3_async_handle_jukebox(task))
		return;
	/* Call back common NFS readpage processing */
	if (task->tk_status >= 0)
		nfs_refresh_inode(data->inode, &data->fattr);
	nfs_readpage_result(task);
}

static void
nfs3_proc_read_setup(struct nfs_read_data *data, unsigned int count)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode = data->inode;
	struct nfs_page		*req;
	int			flags;
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_READ],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};
	
	req = nfs_list_entry(data->pages.next);
	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req);
	data->args.pgbase = req->wb_pgbase;
	data->args.pages  = data->pagevec;
	data->args.count  = count;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.eof     = 0;
	
	/* N.B. Do we need to test? Never called for swapfile inode */
	flags = RPC_TASK_ASYNC | (IS_SWAPFILE(inode)? NFS_RPC_SWAPFLAGS : 0);

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs3_read_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_readdata_release;

	rpc_call_setup(&data->task, &msg, 0);
}

static void
nfs3_write_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;

	if (nfs3_async_handle_jukebox(task))
		return;
	if (task->tk_status >= 0)
		nfs3_write_refresh_inode(data->inode, data->res.fattr);
	nfs_writeback_done(task);
}

static void
nfs3_proc_write_setup(struct nfs_write_data *data, unsigned int count, int how)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode = data->inode;
	struct nfs_page		*req;
	int			stable;
	int			flags;
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_WRITE],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	if (how & FLUSH_STABLE) {
		if (!NFS_I(inode)->ncommit)
			stable = NFS_FILE_SYNC;
		else
			stable = NFS_DATA_SYNC;
	} else
		stable = NFS_UNSTABLE;
	
	req = nfs_list_entry(data->pages.next);
	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req);
	data->args.pgbase = req->wb_pgbase;
	data->args.count  = count;
	data->args.stable = stable;
	data->args.pages  = data->pagevec;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;

	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs3_write_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_writedata_release;

	rpc_call_setup(&data->task, &msg, 0);
}

static void
nfs3_commit_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;

	if (nfs3_async_handle_jukebox(task))
		return;
	if (task->tk_status >= 0)
		nfs3_write_refresh_inode(data->inode, data->res.fattr);
	nfs_commit_done(task);
}

static void
nfs3_proc_commit_setup(struct nfs_write_data *data, u64 start, u32 len, int how)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode = data->inode;
	int			flags;
	struct rpc_message	msg = {
		.rpc_proc	= &nfs3_procedures[NFS3PROC_COMMIT],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	data->args.fh     = NFS_FH(data->inode);
	data->args.offset = start;
	data->args.count  = len;
	data->res.count   = len;
	data->res.fattr   = &data->fattr;
	data->res.verf    = &data->verf;
	
	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs3_commit_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_commit_release;
	
	rpc_call_setup(&data->task, &msg, 0);
}

struct nfs_rpc_ops	nfs_v3_clientops = {
	.version	= 3,			/* protocol version */
	.getroot	= nfs3_proc_get_root,
	.getattr	= nfs3_proc_getattr,
	.setattr	= nfs3_proc_setattr,
	.lookup		= nfs3_proc_lookup,
	.access		= nfs3_proc_access,
	.readlink	= nfs3_proc_readlink,
	.read		= nfs3_proc_read,
	.write		= nfs3_proc_write,
	.create		= nfs3_proc_create,
	.remove		= nfs3_proc_remove,
	.unlink_setup	= nfs3_proc_unlink_setup,
	.unlink_done	= nfs3_proc_unlink_done,
	.rename		= nfs3_proc_rename,
	.link		= nfs3_proc_link,
	.symlink	= nfs3_proc_symlink,
	.mkdir		= nfs3_proc_mkdir,
	.rmdir		= nfs3_proc_rmdir,
	.readdir	= nfs3_proc_readdir,
	.mknod		= nfs3_proc_mknod,
	.statfs		= nfs3_proc_statfs,
	.fsinfo		= nfs3_proc_fsinfo,
	.pathconf	= nfs3_proc_pathconf,
	.decode_dirent	= nfs3_decode_dirent,
	.read_setup	= nfs3_proc_read_setup,
	.write_setup	= nfs3_proc_write_setup,
	.commit_setup	= nfs3_proc_commit_setup,
};
