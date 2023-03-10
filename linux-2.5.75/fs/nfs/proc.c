/*
 *  linux/fs/nfs/proc.c
 *
 *  Copyright (C) 1992, 1993, 1994  Rick Sladkey
 *
 *  OS-independent nfs remote procedure call functions
 *
 *  Tuned by Alan Cox <A.Cox@swansea.ac.uk> for >3K buffers
 *  so at last we can have decent(ish) throughput off a 
 *  Sun server.
 *
 *  Coding optimized and cleaned up by Florian La Roche.
 *  Note: Error returns are optimized for NFS_OK, which isn't translated via
 *  nfs_stat_to_errno(), but happens to be already the right return code.
 *
 *  Also, the code currently doesn't check the size of the packet, when
 *  it decodes the packet.
 *
 *  Feel free to fix it and mail me the diffs if it worries you.
 *
 *  Completely rewritten to support the new RPC call interface;
 *  rewrote and moved the entire XDR stuff to xdr.c
 *  --Olaf Kirch June 1996
 *
 *  The code below initializes all auto variables explicitly, otherwise
 *  it will fail to work as a module (gcc generates a memset call for an
 *  incomplete struct).
 */

#include <linux/types.h>
#include <linux/param.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_page.h>
#include <linux/smp_lock.h>

#define NFSDBG_FACILITY		NFSDBG_PROC

extern struct rpc_procinfo nfs_procedures[];

static void
nfs_write_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	if (!(fattr->valid & NFS_ATTR_WCC)) {
		fattr->pre_size  = NFS_CACHE_ISIZE(inode);
		fattr->pre_mtime = NFS_CACHE_MTIME(inode);
		fattr->pre_ctime = NFS_CACHE_CTIME(inode);
		fattr->valid |= NFS_ATTR_WCC;
	}
	nfs_refresh_inode(inode, fattr);
}

/*
 * Bare-bones access to getattr: this is for nfs_read_super.
 */
static int
nfs_proc_get_root(struct nfs_server *server, struct nfs_fh *fhandle,
		  struct nfs_fattr *fattr)
{
	int		status;

	dprintk("NFS call  getroot\n");
	fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_GETATTR, fhandle, fattr, 0);
	dprintk("NFS reply getroot\n");
	return status;
}

/*
 * One function for each procedure in the NFS protocol.
 */
static int
nfs_proc_getattr(struct inode *inode, struct nfs_fattr *fattr)
{
	int	status;

	dprintk("NFS call  getattr\n");
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFSPROC_GETATTR,
				NFS_FH(inode), fattr, 0);
	dprintk("NFS reply getattr\n");
	return status;
}

static int
nfs_proc_setattr(struct dentry *dentry, struct nfs_fattr *fattr,
		 struct iattr *sattr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_sattrargs	arg = { 
		.fh	= NFS_FH(inode),
		.sattr	= sattr
	};
	int	status;

	dprintk("NFS call  setattr\n");
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(inode), NFSPROC_SETATTR, &arg, fattr, 0);
	dprintk("NFS reply setattr\n");
	return status;
}

static int
nfs_proc_lookup(struct inode *dir, struct qstr *name,
		struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct nfs_diropok	res = {
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  lookup %s\n", name->name);
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_LOOKUP, &arg, &res, 0);
	dprintk("NFS reply lookup: %d\n", status);
	return status;
}

static int
nfs_proc_readlink(struct inode *inode, struct page *page)
{
	struct nfs_readlinkargs	args = {
		.fh		= NFS_FH(inode),
		.count		= PAGE_CACHE_SIZE,
		.pages		= &page
	};
	int			status;

	dprintk("NFS call  readlink\n");
	status = rpc_call(NFS_CLIENT(inode), NFSPROC_READLINK, &args, NULL, 0);
	dprintk("NFS reply readlink: %d\n", status);
	return status;
}

static int
nfs_proc_read(struct inode *inode, struct rpc_cred *cred,
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
		.count		= count
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs_procedures[NFSPROC_READ],
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
nfs_proc_write(struct inode *inode, struct rpc_cred *cred,
	       struct nfs_fattr *fattr, int how,
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
	struct nfs_writeres     res = {
		.fattr		= fattr,
		.verf		= verf,
		.count		= count
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs_procedures[NFSPROC_WRITE],
		.rpc_argp	= &arg,
		.rpc_resp	= &res,
		.rpc_cred	= cred
	};
	int			status, flags = 0;

	dprintk("NFS call  write %d @ %Ld\n", count, (long long)offset);
	fattr->valid = 0;
	if (how & NFS_RW_SWAP)
		flags |= NFS_RPC_SWAPFLAGS;
	status = rpc_call_sync(NFS_CLIENT(inode), &msg, flags);

	if (status >= 0)
		nfs_write_refresh_inode(inode, fattr);

	dprintk("NFS reply write: %d\n", status);
	verf->committed = NFS_FILE_SYNC;      /* NFSv2 always syncs data */
	return status < 0? status : count;
}

static int
nfs_proc_create(struct inode *dir, struct qstr *name, struct iattr *sattr,
		int flags, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr
	};
	struct nfs_diropok	res = {
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	fattr->valid = 0;
	dprintk("NFS call  create %s\n", name->name);
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_CREATE, &arg, &res, 0);
	dprintk("NFS reply create: %d\n", status);
	return status;
}

/*
 * In NFSv2, mknod is grafted onto the create call.
 */
static int
nfs_proc_mknod(struct inode *dir, struct qstr *name, struct iattr *sattr,
	       dev_t rdev, struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr
	};
	struct nfs_diropok	res = {
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status, mode;

	dprintk("NFS call  mknod %s\n", name->name);

	mode = sattr->ia_mode;
	if (S_ISFIFO(mode)) {
		sattr->ia_mode = (mode & ~S_IFMT) | S_IFCHR;
		sattr->ia_valid &= ~ATTR_SIZE;
	} else if (S_ISCHR(mode) || S_ISBLK(mode)) {
		sattr->ia_valid |= ATTR_SIZE;
		sattr->ia_size   = rdev;	/* get out your barf bag */
	}

	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_CREATE, &arg, &res, 0);

	if (status == -EINVAL && S_ISFIFO(mode)) {
		sattr->ia_mode = mode;
		fattr->valid = 0;
		status = rpc_call(NFS_CLIENT(dir), NFSPROC_CREATE, &arg, &res, 0);
	}
	dprintk("NFS reply mknod: %d\n", status);
	return status;
}
  
static int
nfs_proc_remove(struct inode *dir, struct qstr *name)
{
	struct nfs_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	struct rpc_message	msg = { 
		.rpc_proc	= &nfs_procedures[NFSPROC_REMOVE],
		.rpc_argp	= &arg,
		.rpc_resp	= NULL,
		.rpc_cred	= NULL
	};
	int			status;

	dprintk("NFS call  remove %s\n", name->name);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);

	dprintk("NFS reply remove: %d\n", status);
	return status;
}

static int
nfs_proc_unlink_setup(struct rpc_message *msg, struct dentry *dir, struct qstr *name)
{
	struct nfs_diropargs	*arg;

	arg = (struct nfs_diropargs *)kmalloc(sizeof(*arg), GFP_KERNEL);
	if (!arg)
		return -ENOMEM;
	arg->fh = NFS_FH(dir->d_inode);
	arg->name = name->name;
	arg->len = name->len;
	msg->rpc_proc = &nfs_procedures[NFSPROC_REMOVE];
	msg->rpc_argp = arg;
	return 0;
}

static int
nfs_proc_unlink_done(struct dentry *dir, struct rpc_task *task)
{
	struct rpc_message *msg = &task->tk_msg;
	
	if (msg->rpc_argp) {
		NFS_CACHEINV(dir->d_inode);
		kfree(msg->rpc_argp);
	}
	return 0;
}

static int
nfs_proc_rename(struct inode *old_dir, struct qstr *old_name,
		struct inode *new_dir, struct qstr *new_name)
{
	struct nfs_renameargs	arg = {
		.fromfh		= NFS_FH(old_dir),
		.fromname	= old_name->name,
		.fromlen	= old_name->len,
		.tofh		= NFS_FH(new_dir),
		.toname		= new_name->name,
		.tolen		= new_name->len
	};
	int			status;

	dprintk("NFS call  rename %s -> %s\n", old_name->name, new_name->name);
	status = rpc_call(NFS_CLIENT(old_dir), NFSPROC_RENAME, &arg, NULL, 0);
	dprintk("NFS reply rename: %d\n", status);
	return status;
}

static int
nfs_proc_link(struct inode *inode, struct inode *dir, struct qstr *name)
{
	struct nfs_linkargs	arg = {
		.fromfh		= NFS_FH(inode),
		.tofh		= NFS_FH(dir),
		.toname		= name->name,
		.tolen		= name->len
	};
	int			status;

	dprintk("NFS call  link %s\n", name->name);
	status = rpc_call(NFS_CLIENT(inode), NFSPROC_LINK, &arg, NULL, 0);
	dprintk("NFS reply link: %d\n", status);
	return status;
}

static int
nfs_proc_symlink(struct inode *dir, struct qstr *name, struct qstr *path,
		 struct iattr *sattr, struct nfs_fh *fhandle,
		 struct nfs_fattr *fattr)
{
	struct nfs_symlinkargs	arg = {
		.fromfh		= NFS_FH(dir),
		.fromname	= name->name,
		.fromlen	= name->len,
		.topath		= path->name,
		.tolen		= path->len,
		.sattr		= sattr
	};
	int			status;

	dprintk("NFS call  symlink %s -> %s\n", name->name, path->name);
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_SYMLINK, &arg, NULL, 0);
	dprintk("NFS reply symlink: %d\n", status);
	return status;
}

static int
nfs_proc_mkdir(struct inode *dir, struct qstr *name, struct iattr *sattr,
	       struct nfs_fh *fhandle, struct nfs_fattr *fattr)
{
	struct nfs_createargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len,
		.sattr		= sattr
	};
	struct nfs_diropok	res = {
		.fh		= fhandle,
		.fattr		= fattr
	};
	int			status;

	dprintk("NFS call  mkdir %s\n", name->name);
	fattr->valid = 0;
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_MKDIR, &arg, &res, 0);
	dprintk("NFS reply mkdir: %d\n", status);
	return status;
}

static int
nfs_proc_rmdir(struct inode *dir, struct qstr *name)
{
	struct nfs_diropargs	arg = {
		.fh		= NFS_FH(dir),
		.name		= name->name,
		.len		= name->len
	};
	int			status;

	dprintk("NFS call  rmdir %s\n", name->name);
	status = rpc_call(NFS_CLIENT(dir), NFSPROC_RMDIR, &arg, NULL, 0);
	dprintk("NFS reply rmdir: %d\n", status);
	return status;
}

/*
 * The READDIR implementation is somewhat hackish - we pass a temporary
 * buffer to the encode function, which installs it in the receive
 * the receive iovec. The decode function just parses the reply to make
 * sure it is syntactically correct; the entries itself are decoded
 * from nfs_readdir by calling the decode_entry function directly.
 */
static int
nfs_proc_readdir(struct dentry *dentry, struct rpc_cred *cred,
		 u64 cookie, struct page *page, unsigned int count, int plus)
{
	struct inode		*dir = dentry->d_inode;
	struct nfs_readdirargs	arg = {
		.fh		= NFS_FH(dir),
		.cookie		= cookie,
		.count		= count,
		.pages		= &page
	};
	struct rpc_message	msg = {
		.rpc_proc	= &nfs_procedures[NFSPROC_READDIR],
		.rpc_argp	= &arg,
		.rpc_resp	= NULL,
		.rpc_cred	= cred
	};
	int			status;

	lock_kernel();

	dprintk("NFS call  readdir %d\n", (unsigned int)cookie);
	status = rpc_call_sync(NFS_CLIENT(dir), &msg, 0);

	dprintk("NFS reply readdir: %d\n", status);
	unlock_kernel();
	return status;
}

static int
nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsstat *stat)
{
	struct nfs2_fsstat fsinfo;
	int	status;

	dprintk("NFS call  statfs\n");
	stat->fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_STATFS, fhandle, &fsinfo, 0);
	dprintk("NFS reply statfs: %d\n", status);
	if (status)
		goto out;
	stat->tbytes = (u64)fsinfo.blocks * fsinfo.bsize;
	stat->fbytes = (u64)fsinfo.bfree  * fsinfo.bsize;
	stat->abytes = (u64)fsinfo.bavail * fsinfo.bsize;
	stat->tfiles = 0;
	stat->ffiles = 0;
	stat->afiles = 0;
out:
	return status;
}

static int
nfs_proc_fsinfo(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *info)
{
	struct nfs2_fsstat fsinfo;
	int	status;

	dprintk("NFS call  fsinfo\n");
	info->fattr->valid = 0;
	status = rpc_call(server->client, NFSPROC_STATFS, fhandle, &fsinfo, 0);
	dprintk("NFS reply fsinfo: %d\n", status);
	if (status)
		goto out;
	info->rtmax  = NFS_MAXDATA;
	info->rtpref = fsinfo.tsize;
	info->rtmult = fsinfo.bsize;
	info->wtmax  = NFS_MAXDATA;
	info->wtpref = fsinfo.tsize;
	info->wtmult = fsinfo.bsize;
	info->dtpref = fsinfo.tsize;
	info->maxfilesize = 0x7FFFFFFF;
	info->lease_time = 0;
out:
	return status;
}

static int
nfs_proc_pathconf(struct nfs_server *server, struct nfs_fh *fhandle,
		  struct nfs_pathconf *info)
{
	info->max_link = 0;
	info->max_namelen = NFS2_MAXNAMLEN;
	return 0;
}

extern u32 * nfs_decode_dirent(u32 *, struct nfs_entry *, int);

static void
nfs_read_done(struct rpc_task *task)
{
	struct nfs_read_data *data = (struct nfs_read_data *) task->tk_calldata;

	if (task->tk_status >= 0)
		nfs_refresh_inode(data->inode, data->res.fattr);
	nfs_readpage_result(task);
}

static void
nfs_proc_read_setup(struct nfs_read_data *data, unsigned int count)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode = data->inode;
	struct nfs_page		*req;
	int			flags;
	struct rpc_message	msg = {
		.rpc_proc	= &nfs_procedures[NFSPROC_READ],
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
	rpc_init_task(task, NFS_CLIENT(inode), nfs_read_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_readdata_release;

	rpc_call_setup(&data->task, &msg, 0);
}

static void
nfs_write_done(struct rpc_task *task)
{
	struct nfs_write_data *data = (struct nfs_write_data *) task->tk_calldata;

	if (task->tk_status >= 0)
		nfs_write_refresh_inode(data->inode, data->res.fattr);
	nfs_writeback_done(task);
}

static void
nfs_proc_write_setup(struct nfs_write_data *data, unsigned int count, int how)
{
	struct rpc_task		*task = &data->task;
	struct inode		*inode = data->inode;
	struct nfs_page		*req;
	int			flags;
	struct rpc_message	msg = {
		.rpc_proc	= &nfs_procedures[NFSPROC_WRITE],
		.rpc_argp	= &data->args,
		.rpc_resp	= &data->res,
		.rpc_cred	= data->cred,
	};

	/* Note: NFSv2 ignores @stable and always uses NFS_FILE_SYNC */
	
	req = nfs_list_entry(data->pages.next);
	data->args.fh     = NFS_FH(inode);
	data->args.offset = req_offset(req);
	data->args.pgbase = req->wb_pgbase;
	data->args.count  = count;
	data->args.stable = NFS_FILE_SYNC;
	data->args.pages  = data->pagevec;
	data->res.fattr   = &data->fattr;
	data->res.count   = count;
	data->res.verf    = &data->verf;

	/* Set the initial flags for the task.  */
	flags = (how & FLUSH_SYNC) ? 0 : RPC_TASK_ASYNC;

	/* Finalize the task. */
	rpc_init_task(task, NFS_CLIENT(inode), nfs_write_done, flags);
	task->tk_calldata = data;
	/* Release requests */
	task->tk_release = nfs_writedata_release;

	rpc_call_setup(&data->task, &msg, 0);
}

static void
nfs_proc_commit_setup(struct nfs_write_data *data, u64 start, u32 len, int how)
{
	BUG();
}

struct nfs_rpc_ops	nfs_v2_clientops = {
	.version	= 2,		       /* protocol version */
	.getroot	= nfs_proc_get_root,
	.getattr	= nfs_proc_getattr,
	.setattr	= nfs_proc_setattr,
	.lookup		= nfs_proc_lookup,
	.access		= NULL,		       /* access */
	.readlink	= nfs_proc_readlink,
	.read		= nfs_proc_read,
	.write		= nfs_proc_write,
	.commit		= NULL,		       /* commit */
	.create		= nfs_proc_create,
	.remove		= nfs_proc_remove,
	.unlink_setup	= nfs_proc_unlink_setup,
	.unlink_done	= nfs_proc_unlink_done,
	.rename		= nfs_proc_rename,
	.link		= nfs_proc_link,
	.symlink	= nfs_proc_symlink,
	.mkdir		= nfs_proc_mkdir,
	.rmdir		= nfs_proc_rmdir,
	.readdir	= nfs_proc_readdir,
	.mknod		= nfs_proc_mknod,
	.statfs		= nfs_proc_statfs,
	.fsinfo		= nfs_proc_fsinfo,
	.pathconf	= nfs_proc_pathconf,
	.decode_dirent	= nfs_decode_dirent,
	.read_setup	= nfs_proc_read_setup,
	.write_setup	= nfs_proc_write_setup,
	.commit_setup	= nfs_proc_commit_setup,
};
