/*
 *  fs/nfs/nfs4xdr.c
 *
 *  Server-side XDR for NFSv4
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson   <andros@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * TODO: Neil Brown made the following observation:  We currently
 * initially reserve NFSD_BUFSIZE space on the transmit queue and
 * never release any of that until the request is complete.
 * It would be good to calculate a new maximum response size while
 * decoding the COMPOUND, and call svc_reserve with this number
 * at the end of nfs4svc_decode_compoundargs.
 */

#include <linux/param.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/vfs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/name_lookup.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>

#define NFSDDBG_FACILITY		NFSDDBG_XDR

/*
 * From Peter Astrand <peter@cendio.se>: The following routines check
 * whether a filename supplied by the client is valid.
 */
static const char trailing_bytes_for_utf8[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

static inline int
is_legal_iso_utf8_sequence(unsigned char *source, int length)
{
	unsigned char a;
	unsigned char *srcptr;

	srcptr = source + length;

	switch (length) {
		/* Everything else falls through when "1"... */
	default:
		/* Sequences with more than 6 bytes are invalid */
		return 0;

		/*
		   Byte 3-6 must be 80..BF
		*/
	case 6:
		if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
	case 5:
		if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
	case 4:
		if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
	case 3:
		if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;

	case 2:
		a = *--srcptr;

		/* Upper limit */
		if (a > 0xBF)
			/* 2nd byte may never be > 0xBF */
			return 0;

		/*
		   Lower limits checks, to detect non-shortest forms.
		   No fall-through in this inner switch.
		*/
		switch (*source) {
		case 0xE0: /* 3 bytes */
			if (a < 0xA0) return 0;
			break;
		case 0xF0: /* 4 bytes */
			if (a < 0x90) return 0;
			break;
		case 0xF8: /* 5 bytes */
			if (a < 0xC8) return 0;
			break;
		case 0xFC: /* 6 bytes */
			if (a < 0x84) return 0;
			break;
		default:
			/* In all cases, 2nd byte must be >= 0x80 (because leading
			   10...) */
			if (a < 0x80) return 0;
		}

	case 1:
		/* Invalid ranges */
		if (*source >= 0x80 && *source < 0xC2)
			/* Multibyte char with value < 0xC2, non-shortest */
			return 0;
		if (*source > 0xFD)
			/* Leading byte starting with 11111110 is illegal */
			return 0;
		if (!*source)
			return 0;
	}

	return 1;
}

static int
check_utf8(char *str, int len)
{
	unsigned char *chunk, *sourceend;
	int chunklen;

	chunk = str;
	sourceend = str + len;

	while (chunk < sourceend) {
		chunklen = trailing_bytes_for_utf8[*chunk]+1;
		if (chunk + chunklen > sourceend)
			return nfserr_inval;
		if (!is_legal_iso_utf8_sequence(chunk, chunklen))
			return nfserr_inval;
		chunk += chunklen;
	}

	return 0;
}

static int
check_filename(char *str, int len, int err)
{
	int i;

	if (len == 0)
		return nfserr_inval;
	if (isdotent(str, len))
		return err;
	for (i = 0; i < len; i++)
		if (str[i] == '/')
			return err;
	return check_utf8(str, len);
}

/*
 * START OF "GENERIC" DECODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define DECODE_HEAD				\
	u32 *p;					\
	int status
#define DECODE_TAIL				\
	status = 0;				\
out:						\
	return status;				\
xdr_error:					\
	printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__);	\
	status = nfserr_bad_xdr;		\
	goto out

#define READ32(x)         (x) = ntohl(*p++)
#define READ64(x)         do {			\
	(x) = (u64)ntohl(*p++) << 32;		\
	(x) |= ntohl(*p++);			\
} while (0)
#define READTIME(x)       do {			\
	p++;					\
	(x) = ntohl(*p++);			\
	p++;					\
} while (0)
#define READMEM(x,nbytes) do {			\
	x = (char *)p;				\
	p += XDR_QUADLEN(nbytes);		\
} while (0)
#define SAVEMEM(x,nbytes) do {			\
	if (!(x = (p==argp->tmp || p == argp->tmpp) ? \
 		savemem(argp, p, nbytes) :	\
 		(char *)p)) {			\
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
		goto xdr_error;			\
		}				\
	p += XDR_QUADLEN(nbytes);		\
} while (0)
#define COPYMEM(x,nbytes) do {			\
	memcpy((x), p, nbytes);			\
	p += XDR_QUADLEN(nbytes);		\
} while (0)

/* READ_BUF, read_buf(): nbytes must be <= PAGE_SIZE */
#define READ_BUF(nbytes)  do {			\
	if (nbytes <= (u32)((char *)argp->end - (char *)argp->p)) {	\
		p = argp->p;			\
		argp->p += XDR_QUADLEN(nbytes);	\
	} else if (!(p = read_buf(argp, nbytes))) { \
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); \
		goto xdr_error;			\
	}					\
} while (0)

u32 *read_buf(struct nfsd4_compoundargs *argp, int nbytes)
{
	/* We want more bytes than seem to be available.
	 * Maybe we need a new page, maybe we have just run out
	 */
	int avail = (char*)argp->end - (char*)argp->p;
	u32 *p;
	if (avail + argp->pagelen < nbytes)
		return NULL;
	if (avail + PAGE_SIZE < nbytes) /* need more than a page !! */
		return NULL;
	/* ok, we can do it with the current plus the next page */
	if (nbytes <= sizeof(argp->tmp))
		p = argp->tmp;
	else {
		if (argp->tmpp)
			kfree(argp->tmpp);
		p = argp->tmpp = kmalloc(nbytes, GFP_KERNEL);
		if (!p)
			return NULL;
		
	}
	memcpy(p, argp->p, avail);
	/* step to next page */
	argp->p = page_address(argp->pagelist[0]);
	argp->pagelist++;
	if (argp->pagelen < PAGE_SIZE) {
		argp->end = p + (argp->pagelen>>2);
		argp->pagelen = 0;
	} else {
		argp->end = p + (PAGE_SIZE>>2);
		argp->pagelen -= PAGE_SIZE;
	}
	memcpy(((char*)p)+avail, argp->p, (nbytes - avail));
	argp->p += XDR_QUADLEN(nbytes - avail);
	return p;
}

char *savemem(struct nfsd4_compoundargs *argp, u32 *p, int nbytes)
{
	struct tmpbuf *tb;
	if (p == argp->tmp) {
		p = kmalloc(nbytes, GFP_KERNEL);
		if (!p) return NULL;
		memcpy(p, argp->tmp, nbytes);
	} else {
		if (p != argp->tmpp)
			BUG();
		argp->tmpp = NULL;
	}
	tb = kmalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb) {
		kfree(p);
		return NULL;
	}
	tb->buf = p;
	tb->next = argp->to_free;
	argp->to_free = tb;
	return (char*)p;
}


static int
nfsd4_decode_bitmap(struct nfsd4_compoundargs *argp, u32 *bmval)
{
	u32 bmlen;
	DECODE_HEAD;

	bmval[0] = 0;
	bmval[1] = 0;

	READ_BUF(4);
	READ32(bmlen);
	if (bmlen > 1000)
		goto xdr_error;

	READ_BUF(bmlen << 2);
	if (bmlen > 0)
		READ32(bmval[0]);
	if (bmlen > 1)
		READ32(bmval[1]);

	DECODE_TAIL;
}

static int
nfsd4_decode_fattr(struct nfsd4_compoundargs *argp, u32 *bmval, struct iattr *iattr)
{
	int expected_len, len = 0;
	u32 dummy32;
	char *buf;

	DECODE_HEAD;
	iattr->ia_valid = 0;
	if ((status = nfsd4_decode_bitmap(argp, bmval)))
		return status;

	/*
	 * According to spec, unsupported attributes return ERR_NOTSUPP;
	 * read-only attributes return ERR_INVAL.
	 */
	if ((bmval[0] & ~NFSD_SUPPORTED_ATTRS_WORD0) || (bmval[1] & ~NFSD_SUPPORTED_ATTRS_WORD1))
		return nfserr_notsupp;
	if ((bmval[0] & ~NFSD_WRITEABLE_ATTRS_WORD0) || (bmval[1] & ~NFSD_WRITEABLE_ATTRS_WORD1))
		return nfserr_inval;

	READ_BUF(4);
	READ32(expected_len);

	if (bmval[0] & FATTR4_WORD0_SIZE) {
		READ_BUF(8);
		len += 8;
		READ64(iattr->ia_size);
		iattr->ia_valid |= ATTR_SIZE;
	}
	if (bmval[1] & FATTR4_WORD1_MODE) {
		READ_BUF(4);
		len += 4;
		READ32(iattr->ia_mode);
		iattr->ia_mode &= (S_IFMT | S_IALLUGO);
		iattr->ia_valid |= ATTR_MODE;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if (check_utf8(buf, dummy32))
			return nfserr_inval;
		if ((status = name_get_uid(buf, dummy32, &iattr->ia_uid)))
			goto out_nfserr;
		iattr->ia_valid |= ATTR_UID;
	}
	if (bmval[1] & FATTR4_WORD1_OWNER_GROUP) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		READ_BUF(dummy32);
		len += (XDR_QUADLEN(dummy32) << 2);
		READMEM(buf, dummy32);
		if (check_utf8(buf, dummy32))
			return nfserr_inval;
		if ((status = name_get_gid(buf, dummy32, &iattr->ia_gid)))
			goto out_nfserr;
		iattr->ia_valid |= ATTR_GID;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_ACCESS_SET) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			/* We require the high 32 bits of 'seconds' to be 0, and we ignore
			   all 32 bits of 'nseconds'. */
			READ_BUF(12);
			len += 12;
			READ32(dummy32);
			if (dummy32)
				return nfserr_inval;
			READ32(iattr->ia_atime.tv_sec);
			READ32(iattr->ia_atime.tv_nsec);
			if (iattr->ia_atime.tv_nsec >= (u32)1000000000)
				return nfserr_inval;
			iattr->ia_valid |= (ATTR_ATIME | ATTR_ATIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_ATIME;
			break;
		default:
			goto xdr_error;
		}
	}
	if (bmval[1] & FATTR4_WORD1_TIME_METADATA) {
		/* We require the high 32 bits of 'seconds' to be 0, and we ignore
		   all 32 bits of 'nseconds'. */
		READ_BUF(12);
		len += 12;
		READ32(dummy32);
		if (dummy32)
			return nfserr_inval;
		READ32(iattr->ia_ctime.tv_sec);
		READ32(iattr->ia_ctime.tv_nsec);
		if (iattr->ia_ctime.tv_nsec >= (u32)1000000000)
			return nfserr_inval;
		iattr->ia_valid |= ATTR_CTIME;
	}
	if (bmval[1] & FATTR4_WORD1_TIME_MODIFY_SET) {
		READ_BUF(4);
		len += 4;
		READ32(dummy32);
		switch (dummy32) {
		case NFS4_SET_TO_CLIENT_TIME:
			/* We require the high 32 bits of 'seconds' to be 0, and we ignore
			   all 32 bits of 'nseconds'. */
			READ_BUF(12);
			len += 12;
			READ32(dummy32);
			if (dummy32)
				return nfserr_inval;
			READ32(iattr->ia_mtime.tv_sec);
			READ32(iattr->ia_mtime.tv_nsec);
			if (iattr->ia_mtime.tv_nsec >= (u32)1000000000)
				return nfserr_inval;
			iattr->ia_valid |= (ATTR_MTIME | ATTR_MTIME_SET);
			break;
		case NFS4_SET_TO_SERVER_TIME:
			iattr->ia_valid |= ATTR_MTIME;
			break;
		default:
			goto xdr_error;
		}
	}
	if (len != expected_len)
		goto xdr_error;

	DECODE_TAIL;

out_nfserr:
	status = nfserrno(status);
	goto out;
}

static int
nfsd4_decode_access(struct nfsd4_compoundargs *argp, struct nfsd4_access *access)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(access->ac_req_access);

	DECODE_TAIL;
}

static int
nfsd4_decode_close(struct nfsd4_compoundargs *argp, struct nfsd4_close *close)
{
	DECODE_HEAD;

	READ_BUF(4 + sizeof(stateid_t));
	READ32(close->cl_seqid);
	READ32(close->cl_stateid.si_generation);
	COPYMEM(&close->cl_stateid.si_opaque, sizeof(stateid_opaque_t));

	DECODE_TAIL;
}


static int
nfsd4_decode_commit(struct nfsd4_compoundargs *argp, struct nfsd4_commit *commit)
{
	DECODE_HEAD;

	READ_BUF(12);
	READ64(commit->co_offset);
	READ32(commit->co_count);

	DECODE_TAIL;
}

static int
nfsd4_decode_create(struct nfsd4_compoundargs *argp, struct nfsd4_create *create)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(create->cr_type);
	switch (create->cr_type) {
	case NF4LNK:
		READ_BUF(4);
		READ32(create->cr_linklen);
		READ_BUF(create->cr_linklen);
		SAVEMEM(create->cr_linkname, create->cr_linklen);
		if (check_utf8(create->cr_linkname, create->cr_linklen))
			return nfserr_inval;
		break;
	case NF4BLK:
	case NF4CHR:
		READ_BUF(8);
		READ32(create->cr_specdata1);
		READ32(create->cr_specdata2);
		break;
	case NF4SOCK:
	case NF4FIFO:
	case NF4DIR:
		break;
	default:
		goto xdr_error;
	}

	READ_BUF(4);
	READ32(create->cr_namelen);
	READ_BUF(create->cr_namelen);
	SAVEMEM(create->cr_name, create->cr_namelen);
	if ((status = check_filename(create->cr_name, create->cr_namelen, nfserr_inval)))
		return status;

	if ((status = nfsd4_decode_fattr(argp, create->cr_bmval, &create->cr_iattr)))
		goto out;

	DECODE_TAIL;
}

static inline int
nfsd4_decode_getattr(struct nfsd4_compoundargs *argp, struct nfsd4_getattr *getattr)
{
	return nfsd4_decode_bitmap(argp, getattr->ga_bmval);
}

static int
nfsd4_decode_link(struct nfsd4_compoundargs *argp, struct nfsd4_link *link)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(link->li_namelen);
	READ_BUF(link->li_namelen);
	SAVEMEM(link->li_name, link->li_namelen);
	if ((status = check_filename(link->li_name, link->li_namelen, nfserr_inval)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_lookup(struct nfsd4_compoundargs *argp, struct nfsd4_lookup *lookup)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(lookup->lo_len);
	READ_BUF(lookup->lo_len);
	SAVEMEM(lookup->lo_name, lookup->lo_len);
	if ((status = check_filename(lookup->lo_name, lookup->lo_len, nfserr_noent)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_open(struct nfsd4_compoundargs *argp, struct nfsd4_open *open)
{
	DECODE_HEAD;

	memset(open->op_bmval, 0, sizeof(open->op_bmval));
	open->op_iattr.ia_valid = 0;

	/* seqid, share_access, share_deny, clientid, ownerlen */
	READ_BUF(16 + sizeof(clientid_t));
	READ32(open->op_seqid);
	READ32(open->op_share_access);
	READ32(open->op_share_deny);
	COPYMEM(&open->op_clientid, sizeof(clientid_t));
	READ32(open->op_owner.len);

	/* owner, open_flag */
	READ_BUF(open->op_owner.len + 4);
	SAVEMEM(open->op_owner.data, open->op_owner.len);
	READ32(open->op_create);
	switch (open->op_create) {
	case NFS4_OPEN_NOCREATE:
		break;
	case NFS4_OPEN_CREATE:
		READ_BUF(4);
		READ32(open->op_createmode);
		switch (open->op_createmode) {
		case NFS4_CREATE_UNCHECKED:
		case NFS4_CREATE_GUARDED:
			if ((status = nfsd4_decode_fattr(argp, open->op_bmval, &open->op_iattr)))
				goto out;
			break;
		case NFS4_CREATE_EXCLUSIVE:
			READ_BUF(8);
			COPYMEM(open->op_verf, 8);
			break;
		default:
			goto xdr_error;
		}
		break;
	default:
		goto xdr_error;
	}

	/* open_claim */
	READ_BUF(4);
	READ32(open->op_claim_type);
	switch (open->op_claim_type) {
	case NFS4_OPEN_CLAIM_NULL:
	case NFS4_OPEN_CLAIM_DELEGATE_PREV:
		READ_BUF(4);
		READ32(open->op_fname.len);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len, nfserr_inval)))
			return status;
		break;
	case NFS4_OPEN_CLAIM_PREVIOUS:
		READ_BUF(4);
		READ32(open->op_delegate_type);
		break;
	case NFS4_OPEN_CLAIM_DELEGATE_CUR:
		READ_BUF(sizeof(delegation_stateid_t) + 4);
		COPYMEM(&open->op_delegate_stateid, sizeof(delegation_stateid_t));
		READ32(open->op_fname.len);
		READ_BUF(open->op_fname.len);
		SAVEMEM(open->op_fname.data, open->op_fname.len);
		if ((status = check_filename(open->op_fname.data, open->op_fname.len, nfserr_inval)))
			return status;
		break;
	default:
		goto xdr_error;
	}

	DECODE_TAIL;
}

static int
nfsd4_decode_open_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_open_confirm *open_conf)
{
	DECODE_HEAD;
		    
	READ_BUF(4 + sizeof(stateid_t));
	READ32(open_conf->oc_req_stateid.si_generation);
	COPYMEM(&open_conf->oc_req_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ32(open_conf->oc_seqid);
						        
	DECODE_TAIL;
}

static int
nfsd4_decode_open_downgrade(struct nfsd4_compoundargs *argp, struct nfsd4_open_downgrade *open_down)
{
	DECODE_HEAD;
		    
	READ_BUF(4 + sizeof(stateid_t));
	READ32(open_down->od_stateid.si_generation);
	COPYMEM(&open_down->od_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ32(open_down->od_seqid);
	READ32(open_down->od_share_access);
	READ32(open_down->od_share_deny);
						        
	DECODE_TAIL;
}

static int
nfsd4_decode_putfh(struct nfsd4_compoundargs *argp, struct nfsd4_putfh *putfh)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(putfh->pf_fhlen);
	if (putfh->pf_fhlen > NFS4_FHSIZE)
		goto xdr_error;
	READ_BUF(putfh->pf_fhlen);
	SAVEMEM(putfh->pf_fhval, putfh->pf_fhlen);

	DECODE_TAIL;
}

static int
nfsd4_decode_read(struct nfsd4_compoundargs *argp, struct nfsd4_read *read)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t) + 12);
	READ32(read->rd_stateid.si_generation);
	COPYMEM(&read->rd_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ64(read->rd_offset);
	READ32(read->rd_length);

	DECODE_TAIL;
}

static int
nfsd4_decode_readdir(struct nfsd4_compoundargs *argp, struct nfsd4_readdir *readdir)
{
	DECODE_HEAD;

	READ_BUF(24);
	READ64(readdir->rd_cookie);
	COPYMEM(readdir->rd_verf, sizeof(nfs4_verifier));
	READ32(readdir->rd_dircount);    /* just in case you needed a useless field... */
	READ32(readdir->rd_maxcount);
	if ((status = nfsd4_decode_bitmap(argp, readdir->rd_bmval)))
		goto out;

	DECODE_TAIL;
}

static int
nfsd4_decode_remove(struct nfsd4_compoundargs *argp, struct nfsd4_remove *remove)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(remove->rm_namelen);
	READ_BUF(remove->rm_namelen);
	SAVEMEM(remove->rm_name, remove->rm_namelen);
	if ((status = check_filename(remove->rm_name, remove->rm_namelen, nfserr_noent)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_rename(struct nfsd4_compoundargs *argp, struct nfsd4_rename *rename)
{
	DECODE_HEAD;

	READ_BUF(4);
	READ32(rename->rn_snamelen);
	READ_BUF(rename->rn_snamelen + 4);
	SAVEMEM(rename->rn_sname, rename->rn_snamelen);
	READ32(rename->rn_tnamelen);
	READ_BUF(rename->rn_tnamelen);
	SAVEMEM(rename->rn_tname, rename->rn_tnamelen);
	if ((status = check_filename(rename->rn_sname, rename->rn_snamelen, nfserr_noent)))
		return status;
	if ((status = check_filename(rename->rn_tname, rename->rn_tnamelen, nfserr_inval)))
		return status;

	DECODE_TAIL;
}

static int
nfsd4_decode_renew(struct nfsd4_compoundargs *argp, clientid_t *clientid)
{
	DECODE_HEAD;

	READ_BUF(sizeof(clientid_t));
	COPYMEM(clientid, sizeof(clientid_t));

	DECODE_TAIL;
}

static int
nfsd4_decode_setattr(struct nfsd4_compoundargs *argp, struct nfsd4_setattr *setattr)
{
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t));
	READ32(setattr->sa_stateid.si_generation);
	COPYMEM(&setattr->sa_stateid.si_opaque, sizeof(stateid_opaque_t));
	if ((status = nfsd4_decode_fattr(argp, setattr->sa_bmval, &setattr->sa_iattr)))
		goto out;

	DECODE_TAIL;
}

static int
nfsd4_decode_setclientid(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid *setclientid)
{
	DECODE_HEAD;

	READ_BUF(12);
	COPYMEM(setclientid->se_verf, 8);
	READ32(setclientid->se_namelen);

	READ_BUF(setclientid->se_namelen + 8);
	SAVEMEM(setclientid->se_name, setclientid->se_namelen);
	READ32(setclientid->se_callback_prog);
	READ32(setclientid->se_callback_netid_len);

	READ_BUF(setclientid->se_callback_netid_len + 4);
	SAVEMEM(setclientid->se_callback_netid_val, setclientid->se_callback_netid_len);
	READ32(setclientid->se_callback_addr_len);

	READ_BUF(setclientid->se_callback_addr_len + 4);
	SAVEMEM(setclientid->se_callback_addr_val, setclientid->se_callback_addr_len);
	READ32(setclientid->se_callback_ident);

	DECODE_TAIL;
}

static int
nfsd4_decode_setclientid_confirm(struct nfsd4_compoundargs *argp, struct nfsd4_setclientid_confirm *scd_c)
{
	DECODE_HEAD;

	READ_BUF(8 + sizeof(nfs4_verifier));
	COPYMEM(&scd_c->sc_clientid, 8);
	COPYMEM(&scd_c->sc_confirm, sizeof(nfs4_verifier));

	DECODE_TAIL;
}

/* Also used for NVERIFY */
static int
nfsd4_decode_verify(struct nfsd4_compoundargs *argp, struct nfsd4_verify *verify)
{
	DECODE_HEAD;

	if ((status = nfsd4_decode_bitmap(argp, verify->ve_bmval)))
		goto out;
	READ_BUF(4);
	READ32(verify->ve_attrlen);
	READ_BUF(verify->ve_attrlen);
	SAVEMEM(verify->ve_attrval, verify->ve_attrlen);

	DECODE_TAIL;
}

static int
nfsd4_decode_write(struct nfsd4_compoundargs *argp, struct nfsd4_write *write)
{
	int avail;
	int v;
	int len;
	DECODE_HEAD;

	READ_BUF(sizeof(stateid_t) + 16);
	READ32(write->wr_stateid.si_generation);
	COPYMEM(&write->wr_stateid.si_opaque, sizeof(stateid_opaque_t));
	READ64(write->wr_offset);
	READ32(write->wr_stable_how);
	if (write->wr_stable_how > 2)
		goto xdr_error;
	READ32(write->wr_buflen);

	/* Sorry .. no magic macros for this.. *
	 * READ_BUF(write->wr_buflen);
	 * SAVEMEM(write->wr_buf, write->wr_buflen);
	 */
	avail = (char*)argp->end - (char*)argp->p;
	if (avail + argp->pagelen < write->wr_buflen) {
		printk(KERN_NOTICE "xdr error! (%s:%d)\n", __FILE__, __LINE__); 
		goto xdr_error;
	}
	write->wr_vec[0].iov_base = p;
	write->wr_vec[0].iov_len = avail;
	v = 0;
	len = write->wr_buflen;
	while (len > write->wr_vec[v].iov_len) {
		len -= write->wr_vec[v].iov_len;
		v++;
		write->wr_vec[v].iov_base = page_address(argp->pagelist[0]);
		argp->pagelist++;
		if (len >= PAGE_SIZE) {
			write->wr_vec[v].iov_len = PAGE_SIZE;
			argp->pagelen -= PAGE_SIZE;
		} else {
			write->wr_vec[v].iov_len = argp->pagelen;
			argp->pagelen -= len;
		}
	}
	argp->end = (u32*) (write->wr_vec[v].iov_base + write->wr_vec[v].iov_len);
	argp->p = (u32*)  (write->wr_vec[v].iov_base + (XDR_QUADLEN(len) << 2));
	write->wr_vec[v].iov_len = len;
	write->wr_vlen = v+1;

	DECODE_TAIL;
}

static int
nfsd4_decode_compound(struct nfsd4_compoundargs *argp)
{
	DECODE_HEAD;
	struct nfsd4_op *op;
	int i;

	/*
	 * XXX: According to spec, we should check the tag
	 * for UTF-8 compliance.  I'm postponing this for
	 * now because it seems that some clients do use
	 * binary tags.
	 */
	READ_BUF(4);
	READ32(argp->taglen);
	READ_BUF(argp->taglen + 8);
	SAVEMEM(argp->tag, argp->taglen);
	READ32(argp->minorversion);
	READ32(argp->opcnt);

	if (argp->taglen > NFSD4_MAX_TAGLEN)
		goto xdr_error;
	if (argp->opcnt > 100)
		goto xdr_error;

	if (argp->opcnt > sizeof(argp->iops)/sizeof(argp->iops[0])) {
		argp->ops = kmalloc(argp->opcnt * sizeof(*argp->ops), GFP_KERNEL);
		if (!argp->ops) {
			argp->ops = argp->iops;
			printk(KERN_INFO "nfsd: couldn't allocate room for COMPOUND\n");
			goto xdr_error;
		}
	}

	for (i = 0; i < argp->opcnt; i++) {
		op = &argp->ops[i];

		/*
		 * Before reading the opcode, we test for the 4-byte buffer
		 * overrun explicitly, instead of using READ_BUF().  This is
		 * because we want a missing opcode to be treated as opcode
		 * OP_WRITE+1, instead of a failed XDR.
		 */
		if (argp->p == argp->end) {
			op->opnum = OP_WRITE + 1;
			op->status = nfserr_bad_xdr;
			argp->opcnt = i+1;
			break;
		}
		op->opnum = ntohl(*argp->p++);

		switch (op->opnum) {
		case OP_ACCESS:
			op->status = nfsd4_decode_access(argp, &op->u.access);
			break;
		case OP_CLOSE:
			op->status = nfsd4_decode_close(argp, &op->u.close);
			break;
		case OP_COMMIT:
			op->status = nfsd4_decode_commit(argp, &op->u.commit);
			break;
		case OP_CREATE:
			op->status = nfsd4_decode_create(argp, &op->u.create);
			break;
		case OP_GETATTR:
			op->status = nfsd4_decode_getattr(argp, &op->u.getattr);
			break;
		case OP_GETFH:
			op->status = nfs_ok;
			break;
		case OP_LINK:
			op->status = nfsd4_decode_link(argp, &op->u.link);
			break;
		case OP_LOOKUP:
			op->status = nfsd4_decode_lookup(argp, &op->u.lookup);
			break;
		case OP_LOOKUPP:
			op->status = nfs_ok;
			break;
		case OP_NVERIFY:
			op->status = nfsd4_decode_verify(argp, &op->u.nverify);
			break;
		case OP_OPEN:
			op->status = nfsd4_decode_open(argp, &op->u.open);
			break;
		case OP_OPEN_CONFIRM:
			op->status = nfsd4_decode_open_confirm(argp, &op->u.open_confirm);
			break;
		case OP_OPEN_DOWNGRADE:
			op->status = nfsd4_decode_open_downgrade(argp, &op->u.open_downgrade);
			break;
		case OP_PUTFH:
			op->status = nfsd4_decode_putfh(argp, &op->u.putfh);
			break;
		case OP_PUTROOTFH:
			op->status = nfs_ok;
			break;
		case OP_READ:
			op->status = nfsd4_decode_read(argp, &op->u.read);
			break;
		case OP_READDIR:
			op->status = nfsd4_decode_readdir(argp, &op->u.readdir);
			break;
		case OP_READLINK:
			op->status = nfs_ok;
			break;
		case OP_REMOVE:
			op->status = nfsd4_decode_remove(argp, &op->u.remove);
			break;
		case OP_RENAME:
			op->status = nfsd4_decode_rename(argp, &op->u.rename);
			break;
		case OP_RESTOREFH:
			op->status = nfs_ok;
			break;
		case OP_RENEW:
			op->status = nfsd4_decode_renew(argp, &op->u.renew);
			break;
		case OP_SAVEFH:
			op->status = nfs_ok;
			break;
		case OP_SETATTR:
			op->status = nfsd4_decode_setattr(argp, &op->u.setattr);
			break;
		case OP_SETCLIENTID:
			op->status = nfsd4_decode_setclientid(argp, &op->u.setclientid);
			break;
		case OP_SETCLIENTID_CONFIRM:
			op->status = nfsd4_decode_setclientid_confirm(argp, &op->u.setclientid_confirm);
			break;
		case OP_VERIFY:
			op->status = nfsd4_decode_verify(argp, &op->u.verify);
			break;
		case OP_WRITE:
			op->status = nfsd4_decode_write(argp, &op->u.write);
			break;
		default:
			/*
			 * According to spec, anything greater than OP_WRITE
			 * is treated as OP_WRITE+1 in the response.
			 */
			if (op->opnum > OP_WRITE)
			op->opnum = OP_WRITE + 1;
			op->status = nfserr_notsupp;
			break;
		}

		if (op->status) {
			argp->opcnt = i+1;
			break;
		}
	}

	DECODE_TAIL;
}
/*
 * END OF "GENERIC" DECODE ROUTINES.
 */

/*
 * START OF "GENERIC" ENCODE ROUTINES.
 *   These may look a little ugly since they are imported from a "generic"
 * set of XDR encode/decode routines which are intended to be shared by
 * all of our NFSv4 implementations (OpenBSD, MacOS X...).
 *
 * If the pain of reading these is too great, it should be a straightforward
 * task to translate them into Linux-specific versions which are more
 * consistent with the style used in NFSv2/v3...
 */
#define ENCODE_HEAD              u32 *p

#define WRITE32(n)               *p++ = htonl(n)
#define WRITE64(n)               do {				\
	*p++ = htonl((u32)((n) >> 32));				\
	*p++ = htonl((u32)(n));					\
} while (0)
#define WRITEMEM(ptr,nbytes)     do {				\
	*(p + XDR_QUADLEN(nbytes) -1) = 0;                      \
	memcpy(p, ptr, nbytes);					\
	p += XDR_QUADLEN(nbytes);				\
} while (0)
#define WRITECINFO(c)		do {				\
	*p++ = htonl(c.atomic);					\
	*p++ = htonl(c.before_size);				\
	*p++ = htonl(c.before_ctime);				\
	*p++ = htonl(c.after_size);				\
	*p++ = htonl(c.after_ctime);				\
} while (0)

#define RESERVE_SPACE(nbytes)	do {				\
	p = resp->p;						\
	BUG_ON(p + XDR_QUADLEN(nbytes) > resp->end);		\
} while (0)
#define ADJUST_ARGS()		resp->p = p

/*
 * Routine for encoding the result of a
 * "seqid-mutating" NFSv4 operation.  This is
 * where seqids are incremented
 */

#define ENCODE_SEQID_OP_TAIL(stateowner)		\
	BUG_ON(!stateowner);				\
	if (seqid_mutating_err(nfserr) && stateowner) {	\
		if (stateowner->so_confirmed)		\
			stateowner->so_seqid++;		\
	}						\
	return nfserr;


static u32 nfs4_ftypes[16] = {
        NF4BAD,  NF4FIFO, NF4CHR, NF4BAD,
        NF4DIR,  NF4BAD,  NF4BLK, NF4BAD,
        NF4REG,  NF4BAD,  NF4LNK, NF4BAD,
        NF4SOCK, NF4BAD,  NF4LNK, NF4BAD,
};

/*
 * Note: @fhp can be NULL; in this case, we might have to compose the filehandle
 * ourselves.
 *
 * @countp is the buffer size in _words_; upon successful return this becomes
 * replaced with the number of words written.
 */
int
nfsd4_encode_fattr(struct svc_fh *fhp, struct svc_export *exp,
		   struct dentry *dentry, u32 *buffer, int *countp, u32 *bmval)
{
	u32 bmval0 = bmval[0];
	u32 bmval1 = bmval[1];
	struct kstat stat;
	struct name_ent *owner = NULL;
	struct name_ent *group = NULL;
	struct svc_fh tempfh;
	struct kstatfs statfs;
	int buflen = *countp << 2;
	u32 *attrlenp;
	u32 dummy;
	u64 dummy64;
	u32 *p = buffer;
	int status;

	BUG_ON(bmval1 & NFSD_WRITEONLY_ATTRS_WORD1);
	BUG_ON(bmval0 & ~NFSD_SUPPORTED_ATTRS_WORD0);
	BUG_ON(bmval1 & ~NFSD_SUPPORTED_ATTRS_WORD1);

	status = vfs_getattr(exp->ex_mnt, dentry, &stat);
	if (status)
		goto out_nfserr;
	if ((bmval0 & (FATTR4_WORD0_FILES_FREE | FATTR4_WORD0_FILES_TOTAL)) ||
	    (bmval1 & (FATTR4_WORD1_SPACE_AVAIL | FATTR4_WORD1_SPACE_FREE |
		       FATTR4_WORD1_SPACE_TOTAL))) {
		status = vfs_statfs(dentry->d_inode->i_sb, &statfs);
		if (status)
			goto out_nfserr;
	}
	if ((bmval0 & FATTR4_WORD0_FILEHANDLE) && !fhp) {
		fh_init(&tempfh, NFS4_FHSIZE);
		status = fh_compose(&tempfh, exp, dentry, NULL);
		if (status)
			goto out;
		fhp = &tempfh;
	}
	if (bmval1 & FATTR4_WORD1_OWNER) {
		status = name_get_user(stat.uid, &owner);
		if (status)
			goto out_nfserr;
	}
	if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
		status = name_get_group(stat.gid, &group);
		if (status)
			goto out_nfserr;
	}

	if ((buflen -= 16) < 0)
		goto out_resource;

	WRITE32(2);
	WRITE32(bmval0);
	WRITE32(bmval1);
	attrlenp = p++;                /* to be backfilled later */

	if (bmval0 & FATTR4_WORD0_SUPPORTED_ATTRS) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(2);
		WRITE32(NFSD_SUPPORTED_ATTRS_WORD0);
		WRITE32(NFSD_SUPPORTED_ATTRS_WORD1);
	}
	if (bmval0 & FATTR4_WORD0_TYPE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		dummy = nfs4_ftypes[(stat.mode & S_IFMT) >> 12];
		if (dummy == NF4BAD)
			goto out_serverfault;
		WRITE32(dummy);
	}
	if (bmval0 & FATTR4_WORD0_FH_EXPIRE_TYPE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32( NFS4_FH_NOEXPIRE_WITH_OPEN | NFS4_FH_VOL_RENAME );
	}
	if (bmval0 & FATTR4_WORD0_CHANGE) {
		/*
		 * XXX: We currently use the inode ctime as the nfsv4 "changeid"
		 * attribute.  This violates the spec, which says
		 *
		 *    The server may return the object's time_modify attribute
		 *    for this attribute, but only if the file system object
		 *    can not be updated more frequently than the resolution
		 *    of time_modify.
		 *
		 * Since we only have 1-second ctime resolution, this is a pretty
		 * serious violation.  Indeed, 1-second ctime resolution is known
		 * to be a problem in practice in the NFSv3 world.
		 *
		 * The real solution to this problem is probably to work on
		 * adding high-resolution mtimes to the VFS layer.
		 *
		 * Note: Started using i_size for the high 32 bits of the changeid.
		 *
		 * Note 2: This _must_ be consistent with the scheme for writing
		 * change_info, so any changes made here must be reflected there
		 * as well.  (See xdr4.h:set_change_info() and the WRITECINFO()
		 * macro above.)
		 */
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE32(stat.size);
		WRITE32(stat.mtime.tv_sec); /* AK: nsec dropped? */
	}
	if (bmval0 & FATTR4_WORD0_SIZE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64(stat.size);
	}
	if (bmval0 & FATTR4_WORD0_LINK_SUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_SYMLINK_SUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_NAMED_ATTR) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_FSID) {
		if ((buflen -= 16) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(MAJOR(stat.dev));
		WRITE32(0);
		WRITE32(MINOR(stat.dev));
	}
	if (bmval0 & FATTR4_WORD0_UNIQUE_HANDLES) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_LEASE_TIME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(NFSD_LEASE_TIME);
	}
	if (bmval0 & FATTR4_WORD0_RDATTR_ERROR) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_ACLSUPPORT) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(0);
	}
	if (bmval0 & FATTR4_WORD0_CANSETTIME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CASE_INSENSITIVE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CASE_PRESERVING) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_CHOWN_RESTRICTED) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_FILEHANDLE) {
		buflen -= (XDR_QUADLEN(fhp->fh_handle.fh_size) << 2) + 4;
		if (buflen < 0)
			goto out_resource;
		WRITE32(fhp->fh_handle.fh_size);
		WRITEMEM(&fhp->fh_handle.fh_base, fhp->fh_handle.fh_size);
	}
	if (bmval0 & FATTR4_WORD0_FILEID) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) stat.ino);
	}
	if (bmval0 & FATTR4_WORD0_FILES_AVAIL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_FREE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_ffree);
	}
	if (bmval0 & FATTR4_WORD0_FILES_TOTAL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) statfs.f_files);
	}
	if (bmval0 & FATTR4_WORD0_HOMOGENEOUS) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval0 & FATTR4_WORD0_MAXFILESIZE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64(~(u64)0);
	}
	if (bmval0 & FATTR4_WORD0_MAXLINK) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(255);
	}
	if (bmval0 & FATTR4_WORD0_MAXNAME) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(~(u32) 0);
	}
	if (bmval0 & FATTR4_WORD0_MAXREAD) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) NFSSVC_MAXBLKSIZE);
	}
	if (bmval0 & FATTR4_WORD0_MAXWRITE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE64((u64) NFSSVC_MAXBLKSIZE);
	}
	if (bmval1 & FATTR4_WORD1_MODE) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(stat.mode & S_IALLUGO);
	}
	if (bmval1 & FATTR4_WORD1_NO_TRUNC) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(1);
	}
	if (bmval1 & FATTR4_WORD1_NUMLINKS) {
		if ((buflen -= 4) < 0)
			goto out_resource;
		WRITE32(stat.nlink);
	}
	if (bmval1 & FATTR4_WORD1_OWNER) {
		int namelen  = strlen(owner->name);
		buflen -= (XDR_QUADLEN(namelen) << 2) + 4;
		if (buflen < 0)
			goto out_resource;
		WRITE32(namelen);
		WRITEMEM(owner->name, namelen);
	}
	if (bmval1 & FATTR4_WORD1_OWNER_GROUP) {
		int namelen = strlen(group->name);
		buflen -= (XDR_QUADLEN(namelen) << 2) + 4;
		if (buflen < 0)
			goto out_resource;
		WRITE32(namelen);
		WRITEMEM(group->name, namelen);
	}
	if (bmval1 & FATTR4_WORD1_RAWDEV) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		WRITE32((u32) MAJOR(stat.rdev));
		WRITE32((u32) MINOR(stat.rdev));
	}
	if (bmval1 & FATTR4_WORD1_SPACE_AVAIL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_bavail * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_FREE) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_bfree * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_TOTAL) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)statfs.f_blocks * (u64)statfs.f_bsize;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_SPACE_USED) {
		if ((buflen -= 8) < 0)
			goto out_resource;
		dummy64 = (u64)stat.blocks << 9;
		WRITE64(dummy64);
	}
	if (bmval1 & FATTR4_WORD1_TIME_ACCESS) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.atime.tv_sec);
		WRITE32(stat.atime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_DELTA) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(1);
		WRITE32(0);
	}
	if (bmval1 & FATTR4_WORD1_TIME_METADATA) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.ctime.tv_sec);
		WRITE32(stat.ctime.tv_nsec);
	}
	if (bmval1 & FATTR4_WORD1_TIME_MODIFY) {
		if ((buflen -= 12) < 0)
			goto out_resource;
		WRITE32(0);
		WRITE32(stat.mtime.tv_sec);
		WRITE32(stat.mtime.tv_nsec);
	}

	*attrlenp = htonl((char *)p - (char *)attrlenp - 4);
	*countp = p - buffer;
	status = nfs_ok;

out:
	if (fhp == &tempfh)
		fh_put(&tempfh);
	if (owner)
		name_put(owner);
	if (group)
		name_put(group);
	return status;
out_nfserr:
	status = nfserrno(status);
	goto out;
out_resource:
	*countp = 0;
	status = nfserr_resource;
	goto out;
out_serverfault:
	status = nfserr_serverfault;
	goto out;
}

static int
nfsd4_encode_dirent(struct readdir_cd *ccd, const char *name, int namlen,
		    loff_t offset, ino_t ino, unsigned int d_type)
{
	struct nfsd4_readdir *cd = container_of(ccd, struct nfsd4_readdir, common);
	int buflen;
	u32 *p = cd->buffer;
	u32 *attrlenp;
	struct dentry *dentry;
	struct svc_export *exp = cd->rd_fhp->fh_export;
	u32 bmval0, bmval1;
	int nfserr = 0;

	/* In nfsv4, "." and ".." never make it onto the wire.. */
	if (name && isdotent(name, namlen)) {
		cd->common.err = nfs_ok;
		return 0;
	}

	if (cd->offset)
		xdr_encode_hyper(cd->offset, (u64) offset);

	buflen = cd->buflen - 4 - XDR_QUADLEN(namlen);
	if (buflen < 0)
		goto nospc;

	*p++ = xdr_one;                             /* mark entry present */
	cd->offset = p;                             /* remember pointer */
	p = xdr_encode_hyper(p, NFS_OFFSET_MAX);    /* offset of next entry */
	p = xdr_encode_array(p, name, namlen);      /* name length & name */

	/*
	 * Now we come to the ugly part: writing the fattr for this entry.
	 */
	bmval0 = cd->rd_bmval[0];
	bmval1 = cd->rd_bmval[1];
	if ((bmval0 & ~(FATTR4_WORD0_RDATTR_ERROR | FATTR4_WORD0_FILEID)) || bmval1)  {
		/*
		 * "Heavyweight" case: we have no choice except to
		 * call nfsd4_encode_fattr(). 
		 */
		dentry = lookup_one_len(name, cd->rd_fhp->fh_dentry, namlen);
		if (IS_ERR(dentry)) {
			nfserr = nfserrno(PTR_ERR(dentry));
			goto error;
		}

		if (d_mountpoint(dentry)) {
			if ((nfserr = nfsd_cross_mnt(cd->rd_rqstp, &dentry, 
					 &exp))) {	
			/* 
			 * -EAGAIN is the only error returned from 
			 * nfsd_cross_mnt() and it indicates that an 
			 * up-call has  been initiated to fill in the export 
			 * options on exp.  When the answer comes back,
			 * this call will be retried.
			 */
				dput(dentry);
				nfserr = nfserr_dropit;
				goto error;
			}

		}

		nfserr = nfsd4_encode_fattr(NULL, exp,
				dentry, p, &buflen, cd->rd_bmval);
		if (!nfserr) {
			p += buflen;
			goto out;
		}
		if (nfserr == nfserr_resource)
			goto nospc;

error:
		/*
		 * If we get here, we experienced a miscellaneous
		 * failure while writing the attributes.  If the
		 * client requested the RDATTR_ERROR attribute,
		 * we stuff the error code into this attribute
		 * and continue.  If this attribute was not requested,
		 * then in accordance with the spec, we fail the
		 * entire READDIR operation(!)
		 */
		if (!(bmval0 & FATTR4_WORD0_RDATTR_ERROR)) {
			cd->common.err = nfserr;
			return -EINVAL;
		}

		bmval0 = FATTR4_WORD0_RDATTR_ERROR;
		bmval1 = 0;
		/* falling through here will do the right thing... */
	}

	/*
	 * In the common "lightweight" case, we avoid
	 * the overhead of nfsd4_encode_fattr() by assembling
	 * a small fattr by hand.
	 */
	if (buflen < 6)
		goto nospc;
	*p++ = htonl(2);
	*p++ = htonl(bmval0);
	*p++ = htonl(bmval1);

	attrlenp = p++;
	if (bmval0 & FATTR4_WORD0_RDATTR_ERROR)
		*p++ = nfserr;       /* no htonl */
	if (bmval0 & FATTR4_WORD0_FILEID)
		p = xdr_encode_hyper(p, (u64)ino);
	*attrlenp = htonl((char *)p - (char *)attrlenp - 4);

out:
	cd->buflen -= (p - cd->buffer);
	cd->buffer = p;
	cd->common.err = nfs_ok;
	return 0;

nospc:
	cd->common.err = nfserr_readdir_nospc;
	return -EINVAL;
}

static void
nfsd4_encode_access(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_access *access)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8);
		WRITE32(access->ac_supported);
		WRITE32(access->ac_resp_access);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_close(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_close *close)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(close->cl_stateid.si_generation);
		WRITEMEM(&close->cl_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}
}


static void
nfsd4_encode_commit(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_commit *commit)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8);
		WRITEMEM(commit->co_verf, 8);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_create(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_create *create)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(32);
		WRITECINFO(create->cr_cinfo);
		WRITE32(2);
		WRITE32(create->cr_bmval[0]);
		WRITE32(create->cr_bmval[1]);
		ADJUST_ARGS();
	}
}

static int
nfsd4_encode_getattr(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_getattr *getattr)
{
	struct svc_fh *fhp = getattr->ga_fhp;
	int buflen;

	if (nfserr)
		return nfserr;

	buflen = resp->end - resp->p - (COMPOUND_ERR_SLACK_SPACE >> 2);
	nfserr = nfsd4_encode_fattr(fhp, fhp->fh_export, fhp->fh_dentry,
				    resp->p, &buflen, getattr->ga_bmval);

	if (!nfserr)
		resp->p += buflen;
	return nfserr;
}

static void
nfsd4_encode_getfh(struct nfsd4_compoundres *resp, int nfserr, struct svc_fh *fhp)
{
	unsigned int len;
	ENCODE_HEAD;

	if (!nfserr) {
		len = fhp->fh_handle.fh_size;
		RESERVE_SPACE(len + 4);
		WRITE32(len);
		WRITEMEM(&fhp->fh_handle.fh_base, len);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_link(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_link *link)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(20);
		WRITECINFO(link->li_cinfo);
		ADJUST_ARGS();
	}
}


static void
nfsd4_encode_open(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open *open)
{
	ENCODE_HEAD;

	if (nfserr)
		return;

	RESERVE_SPACE(36 + sizeof(stateid_t));
	WRITE32(open->op_stateid.si_generation);
	WRITEMEM(&open->op_stateid.si_opaque, sizeof(stateid_opaque_t));
	WRITECINFO(open->op_cinfo);
	WRITE32(open->op_rflags);
	WRITE32(2);
	WRITE32(open->op_bmval[0]);
	WRITE32(open->op_bmval[1]);
	WRITE32(open->op_delegate_type);
	ADJUST_ARGS();

	switch (open->op_delegate_type) {
	case NFS4_OPEN_DELEGATE_NONE:
		break;
	case NFS4_OPEN_DELEGATE_READ:
		RESERVE_SPACE(20 + sizeof(delegation_stateid_t));
		WRITEMEM(&open->op_delegate_stateid, sizeof(delegation_stateid_t));
		WRITE32(0);

		/*
		 * TODO: ACE's in delegations
		 */
		WRITE32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		WRITE32(0);
		WRITE32(0);
		WRITE32(0);   /* XXX: is NULL principal ok? */
		ADJUST_ARGS();
		break;
	case NFS4_OPEN_DELEGATE_WRITE:
		RESERVE_SPACE(32 + sizeof(delegation_stateid_t));
		WRITEMEM(&open->op_delegate_stateid, sizeof(delegation_stateid_t));
		WRITE32(0);

		/*
		 * TODO: space_limit's in delegations
		 */
		WRITE32(NFS4_LIMIT_SIZE);
		WRITE32(~(u32)0);
		WRITE32(~(u32)0);

		/*
		 * TODO: ACE's in delegations
		 */
		WRITE32(NFS4_ACE_ACCESS_ALLOWED_ACE_TYPE);
		WRITE32(0);
		WRITE32(0);
		WRITE32(0);   /* XXX: is NULL principal ok? */
		ADJUST_ARGS();
		break;
	default:
		BUG();
	}
}

static int
nfsd4_encode_open_confirm(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open_confirm *oc)
{
	ENCODE_HEAD;
				        
	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(oc->oc_resp_stateid.si_generation);
		WRITEMEM(&oc->oc_resp_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}

	ENCODE_SEQID_OP_TAIL(oc->oc_stateowner);
}

static int
nfsd4_encode_open_downgrade(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_open_downgrade *od)
{
	ENCODE_HEAD;
				        
	if (!nfserr) {
		RESERVE_SPACE(sizeof(stateid_t));
		WRITE32(od->od_stateid.si_generation);
		WRITEMEM(&od->od_stateid.si_opaque, sizeof(stateid_opaque_t));
		ADJUST_ARGS();
	}

	ENCODE_SEQID_OP_TAIL(od->od_stateowner);
}

static int
nfsd4_encode_read(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_read *read)
{
	u32 eof;
	int v, pn;
	unsigned long maxcount; 
	long len;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	RESERVE_SPACE(8); /* eof flag and byte count */

	maxcount = NFSSVC_MAXBLKSIZE;
	if (maxcount > read->rd_length)
		maxcount = read->rd_length;

	len = maxcount;
	v = 0;
	while (len > 0) {
		pn = resp->rqstp->rq_resused;
		svc_take_page(resp->rqstp);
		read->rd_iov[v].iov_base = page_address(resp->rqstp->rq_respages[pn]);
		read->rd_iov[v].iov_len = len < PAGE_SIZE ? len : PAGE_SIZE;
		v++;
		len -= PAGE_SIZE;
	}
	read->rd_vlen = v;

	nfserr = nfsd_read(read->rd_rqstp, read->rd_fhp,
			   read->rd_offset,
			   read->rd_iov, read->rd_vlen,
			   &maxcount);
	if (nfserr)
		return nfserr;
	eof = (read->rd_offset + maxcount >= read->rd_fhp->fh_dentry->d_inode->i_size);

	WRITE32(eof);
	WRITE32(maxcount);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	resp->xbuf->page_len = maxcount;

	/* read zero bytes -> don't set up tail */
	if(!maxcount)
		return 0;        

	/* set up page for remaining responses */
	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	if (maxcount&3) {
		*(resp->p)++ = 0;
		resp->xbuf->tail[0].iov_base += maxcount&3;
		resp->xbuf->tail[0].iov_len = 4 - (maxcount&3);
	}
	return 0;
}

static int
nfsd4_encode_readlink(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_readlink *readlink)
{
	int maxcount;
	char *page;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	svc_take_page(resp->rqstp);
	page = page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);

	maxcount = PAGE_SIZE;
	RESERVE_SPACE(4);

	/*
	 * XXX: By default, the ->readlink() VFS op will truncate symlinks
	 * if they would overflow the buffer.  Is this kosher in NFSv4?  If
	 * not, one easy fix is: if ->readlink() precisely fills the buffer,
	 * assume that truncation occurred, and return NFS4ERR_RESOURCE.
	 */
	nfserr = nfsd_readlink(readlink->rl_rqstp, readlink->rl_fhp, page, &maxcount);
	if (nfserr)
		return nfserr;

	WRITE32(maxcount);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	resp->xbuf->page_len = maxcount;
	if (maxcount&3) {
		*(resp->p)++ = 0;
		resp->xbuf->tail[0].iov_base += maxcount&3;
		resp->xbuf->tail[0].iov_len = 4 - (maxcount&3);
	}
	return 0;
}

static int
nfsd4_encode_readdir(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_readdir *readdir)
{
	int maxcount;
	loff_t offset;
	u32 *page;
	ENCODE_HEAD;

	if (nfserr)
		return nfserr;
	if (resp->xbuf->page_len)
		return nfserr_resource;

	RESERVE_SPACE(8);  /* verifier */

	/* XXX: Following NFSv3, we ignore the READDIR verifier for now. */
	WRITE32(0);
	WRITE32(0);
	ADJUST_ARGS();
	resp->xbuf->head[0].iov_len = ((char*)resp->p) - (char*)resp->xbuf->head[0].iov_base;

	maxcount = PAGE_SIZE;
	if (maxcount > readdir->rd_maxcount)
		maxcount = readdir->rd_maxcount;

	/*
	 * Convert from bytes to words, account for the two words already
	 * written, make sure to leave two words at the end for the next
	 * pointer and eof field.
	 */
	maxcount = (maxcount >> 2) - 4;
	if (maxcount < 0)
		return nfserr_readdir_nospc;

	svc_take_page(resp->rqstp);
	page = page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	readdir->common.err = 0;
	readdir->buflen = maxcount;
	readdir->buffer = page;
	readdir->offset = NULL;

	offset = readdir->rd_cookie;
	nfserr = nfsd_readdir(readdir->rd_rqstp, readdir->rd_fhp,
			      &offset,
			      &readdir->common, nfsd4_encode_dirent);
	if (nfserr == nfs_ok &&
	    readdir->common.err == nfserr_readdir_nospc &&
	    readdir->buffer == page) 
		nfserr = nfserr_readdir_nospc;
	if (nfserr)
		return nfserr;

	if (readdir->offset)
		xdr_encode_hyper(readdir->offset, offset);

	p = readdir->buffer;
	*p++ = 0;	/* no more entries */
	*p++ = htonl(readdir->common.err == nfserr_eof);
	resp->xbuf->page_len = ((char*)p) - (char*)page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);

	/* allocate a page for the tail */
	svc_take_page(resp->rqstp);
	resp->xbuf->tail[0].iov_base = 
		page_address(resp->rqstp->rq_respages[resp->rqstp->rq_resused-1]);
	resp->rqstp->rq_restailpage = resp->rqstp->rq_resused-1;
	resp->xbuf->tail[0].iov_len = 0;
	resp->p = resp->xbuf->tail[0].iov_base;
	resp->end = resp->p + PAGE_SIZE/4;

	return 0;
}

static void
nfsd4_encode_remove(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_remove *remove)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(20);
		WRITECINFO(remove->rm_cinfo);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_rename(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_rename *rename)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(40);
		WRITECINFO(rename->rn_sinfo);
		WRITECINFO(rename->rn_tinfo);
		ADJUST_ARGS();
	}
}

/*
 * The SETATTR encode routine is special -- it always encodes a bitmap,
 * regardless of the error status.
 */
static void
nfsd4_encode_setattr(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_setattr *setattr)
{
	ENCODE_HEAD;

	RESERVE_SPACE(12);
	if (nfserr) {
		WRITE32(2);
		WRITE32(0);
		WRITE32(0);
	}
	else {
		WRITE32(2);
		WRITE32(setattr->sa_bmval[0]);
		WRITE32(setattr->sa_bmval[1]);
	}
	ADJUST_ARGS();
}

static void
nfsd4_encode_setclientid(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_setclientid *scd)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(8 + sizeof(nfs4_verifier));
		WRITEMEM(&scd->se_clientid, 8);
		WRITEMEM(&scd->se_confirm, sizeof(nfs4_verifier));
		ADJUST_ARGS();
	}
	else if (nfserr == nfserr_clid_inuse) {
		RESERVE_SPACE(8);
		WRITE32(0);
		WRITE32(0);
		ADJUST_ARGS();
	}
}

static void
nfsd4_encode_write(struct nfsd4_compoundres *resp, int nfserr, struct nfsd4_write *write)
{
	ENCODE_HEAD;

	if (!nfserr) {
		RESERVE_SPACE(16);
		WRITE32(write->wr_bytes_written);
		WRITE32(write->wr_how_written);
		WRITEMEM(write->wr_verifier, 8);
		ADJUST_ARGS();
	}
}

void
nfsd4_encode_operation(struct nfsd4_compoundres *resp, struct nfsd4_op *op)
{
	u32 *statp;
	ENCODE_HEAD;

	RESERVE_SPACE(8);
	WRITE32(op->opnum);
	statp = p++;                  /* to be backfilled at the end */
	ADJUST_ARGS();

	switch (op->opnum) {
	case OP_ACCESS:
		nfsd4_encode_access(resp, op->status, &op->u.access);
		break;
	case OP_CLOSE:
		nfsd4_encode_close(resp, op->status, &op->u.close);
		break;
	case OP_COMMIT:
		nfsd4_encode_commit(resp, op->status, &op->u.commit);
		break;
	case OP_CREATE:
		nfsd4_encode_create(resp, op->status, &op->u.create);
		break;
	case OP_GETATTR:
		op->status = nfsd4_encode_getattr(resp, op->status, &op->u.getattr);
		break;
	case OP_GETFH:
		nfsd4_encode_getfh(resp, op->status, op->u.getfh);
		break;
	case OP_LINK:
		nfsd4_encode_link(resp, op->status, &op->u.link);
		break;
	case OP_LOOKUP:
		break;
	case OP_LOOKUPP:
		break;
	case OP_NVERIFY:
		break;
	case OP_OPEN:
		nfsd4_encode_open(resp, op->status, &op->u.open);
		break;
	case OP_OPEN_CONFIRM:
		nfsd4_encode_open_confirm(resp, op->status, &op->u.open_confirm);
		break;
	case OP_OPEN_DOWNGRADE:
		nfsd4_encode_open_downgrade(resp, op->status, &op->u.open_downgrade);
		break;
	case OP_PUTFH:
		break;
	case OP_PUTROOTFH:
		break;
	case OP_READ:
		op->status = nfsd4_encode_read(resp, op->status, &op->u.read);
		break;
	case OP_READDIR:
		op->status = nfsd4_encode_readdir(resp, op->status, &op->u.readdir);
		break;
	case OP_READLINK:
		op->status = nfsd4_encode_readlink(resp, op->status, &op->u.readlink);
		break;
	case OP_REMOVE:
		nfsd4_encode_remove(resp, op->status, &op->u.remove);
		break;
	case OP_RENAME:
		nfsd4_encode_rename(resp, op->status, &op->u.rename);
		break;
	case OP_RENEW:
		break;
	case OP_RESTOREFH:
		break;
	case OP_SAVEFH:
		break;
	case OP_SETATTR:
		nfsd4_encode_setattr(resp, op->status, &op->u.setattr);
		break;
	case OP_SETCLIENTID:
		nfsd4_encode_setclientid(resp, op->status, &op->u.setclientid);
		break;
	case OP_SETCLIENTID_CONFIRM:
		break;
	case OP_VERIFY:
		break;
	case OP_WRITE:
		nfsd4_encode_write(resp, op->status, &op->u.write);
		break;
	default:
		break;
	}

	/*
	 * Note: We write the status directly, instead of using WRITE32(),
	 * since it is already in network byte order.
	 */
	*statp = op->status;
}

/*
 * END OF "GENERIC" ENCODE ROUTINES.
 */

int
nfs4svc_encode_voidres(struct svc_rqst *rqstp, u32 *p, void *dummy)
{
        return xdr_ressize_check(rqstp, p);
}

int
nfs4svc_decode_compoundargs(struct svc_rqst *rqstp, u32 *p, struct nfsd4_compoundargs *args)
{
	int status;

	args->p = p;
	args->end = rqstp->rq_arg.head[0].iov_base + rqstp->rq_arg.head[0].iov_len;
	args->pagelist = rqstp->rq_arg.pages;
	args->pagelen = rqstp->rq_arg.page_len;
	args->tmpp = NULL;
	args->to_free = NULL;
	args->ops = args->iops;

	status = nfsd4_decode_compound(args);
	if (status) {
		if (args->ops != args->iops) {
			kfree(args->ops);
			args->ops = args->iops;
		}
		if (args->tmpp) {
			kfree(args->tmpp);
			args->tmpp = NULL;
		}
		while (args->to_free) {
			struct tmpbuf *tb = args->to_free;
			args->to_free = tb->next;
			kfree(tb->buf);
			kfree(tb);
		}
	}
	return !status;
}

int
nfs4svc_encode_compoundres(struct svc_rqst *rqstp, u32 *p, struct nfsd4_compoundres *resp)
{
	/*
	 * All that remains is to write the tag and operation count...
	 */
	struct iovec *iov;
	p = resp->tagp;
	*p++ = htonl(resp->taglen);
	memcpy(p, resp->tag, resp->taglen);
	p += XDR_QUADLEN(resp->taglen);
	*p++ = htonl(resp->opcnt);

	if (rqstp->rq_res.page_len) 
		iov = &rqstp->rq_res.tail[0];
	else
		iov = &rqstp->rq_res.head[0];
	iov->iov_len = ((char*)resp->p) - (char*)iov->iov_base;
	BUG_ON(iov->iov_len > PAGE_SIZE);
	return 1;
}

/*
 * Local variables:
 *  c-basic-offset: 8
 * End:
 */
