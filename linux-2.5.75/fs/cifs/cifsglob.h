/*
 *   fs/cifs/cifsglob.h
 *
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 * 
 */
#include <linux/in.h>
#include "cifs_fs_sb.h"
/*
 * The sizes of various internal tables and strings
 */
#define MAX_UID_INFO 16
#define MAX_SES_INFO 2
#define MAX_TCON_INFO 4

#define MAX_TREE_SIZE 2 + MAX_SERVER_SIZE + 1 + MAX_SHARE_SIZE + 1
#define MAX_SERVER_SIZE 15
#define MAX_SHARE_SIZE  64	/* used to be 20 - this should still be enough */
#define MAX_USERNAME_SIZE 32	/* 32 is to allow for 15 char names + null
				   termination then *2 for unicode versions */
#define MAX_PASSWORD_SIZE 16

/*
 * MAX_REQ is the maximum number of requests that WE will send
 * on one NetBIOS handle concurently.
 */
#define MAX_REQ (10)

#define SERVER_NAME_LENGTH 15
#define SERVER_NAME_LEN_WITH_NULL     (SERVER_NAME_LENGTH + 1)

/* used to define string lengths for reversing unicode strings */
/*         (256+1)*2 = 514                                     */
/*           (max path length + 1 for null) * 2 for unicode    */
#define MAX_NAME 514

#include "cifspdu.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/*
 * This information is kept on every Server we know about.
 *
 * Some things to note:
 *
 */
#define SERVER_NAME_LEN_WITH_NULL	(SERVER_NAME_LENGTH + 1)

/*
 * CIFS vfs client Status information (based on what we know.)
 */

 /* associated with each tcp and smb session */
enum statusEnum {
	CifsNew = 0,
	CifsGood,
	CifsExiting,
	CifsNeedReconnect
};

enum securityEnum {
	NTLM = 0,		/* Legacy NTLM012 auth with NTLM hash */
	NTLMv2,			/* Legacy NTLM auth with NTLMv2 hash */
	RawNTLMSSP,		/* NTLMSSP without SPNEGO */
	NTLMSSP,		/* NTLMSSP via SPNEGO */
	Kerberos		/* Kerberos via SPNEGO */
};

/*
 *****************************************************************
 * Except the CIFS PDUs themselves all the
 * globally interesting structs should go here
 *****************************************************************
 */

struct TCP_Server_Info {
	char server_Name[SERVER_NAME_LEN_WITH_NULL];	/* 15 chars + X'20'in 16th */
	char unicode_server_Name[SERVER_NAME_LEN_WITH_NULL * 2];	/* Unicode version of server_Name */
	struct socket *ssocket;
	struct sockaddr_in sockAddr;
	wait_queue_head_t response_q;
	struct list_head pending_mid_q;
	void *Server_NlsInfo;	/* BB - placeholder for future NLS info  */
	unsigned short server_codepage;	/* codepage for the server    */
	unsigned long ip_address;	/* IP addr for the server if known     */
	unsigned long svType;	/* computer type                       */
	char versionMajor;
	char versionMinor;
	int svlocal:1;		/* local server or remote */
	atomic_t socketUseCount;	/* indicates if the server has any open cifs sessions */
	enum statusEnum tcpStatus; /* what we think the status is */
	struct semaphore tcpSem;
	struct task_struct *tsk;
	char server_GUID[16];
	char secMode;
	enum securityEnum secType;
	unsigned int maxReq;	/* Clients should submit no more */
	/* than maxReq distinct unanswered SMBs to the server when using  */
	/* multiplexed reads or writes */
	unsigned int maxBuf;	/* maxBuf specifies the maximum */
	/* message size the server can send or receive for non-raw SMBs */
	unsigned int maxRw;	/* maxRw specifies the maximum */
	/* message size the server can send or receive for */
	/* SMB_COM_WRITE_RAW or SMB_COM_READ_RAW. */
	char sessid[4];		/* unique token id for this session */
	/* (returned on Negotiate */
	int capabilities; /* allow selective disabling of caps by smb sess */
	__u16 timeZone;
	char cryptKey[CIFS_CRYPTO_KEY_SIZE];
};

/*
 * The following is our shortcut to user information.  We surface the uid,
 * and name. We always get the password on the fly in case it
 * has changed. We also hang a list of sessions owned by this user off here. 
 */
struct cifsUidInfo {
	struct list_head userList;
	struct list_head sessionList; /* SMB sessions for this user */
	uid_t linux_uid;
	char user[MAX_USERNAME_SIZE + 1];	/* ascii name of user */
	/* BB may need ptr or callback for PAM or WinBind info */
};

/*
 * Session structure.  One of these for each uid session with a particular host
 */
struct cifsSesInfo {
	struct list_head cifsSessionList;
	struct semaphore sesSem;
	struct cifsUidInfo *uidInfo;	/* pointer to user info */
	struct TCP_Server_Info *server;	/* pointer to server info */
	atomic_t inUse;		/* # of CURRENT users of this ses */
	enum statusEnum status;
	__u32 sequence_number;  /* needed for CIFS PDU signature */
	__u16 ipc_tid;		/* special tid for connection to IPC share */
	char mac_signing_key[CIFS_SESSION_KEY_SIZE + 16];	
	char *serverOS;		/* name of operating system underlying the server */
	char *serverNOS;	/* name of network operating system that the server is running */
	char *serverDomain;	/* security realm of server */
	int Suid;		/* needed for user level security */
	int capabilities;
	char serverName[SERVER_NAME_LEN_WITH_NULL * 2];	/* BB make bigger for tcp names - will ipv6 and sctp addresses fit here?? */
	char userName[MAX_USERNAME_SIZE + 1];
	char domainName[MAX_USERNAME_SIZE + 1];
	char password_with_pad[CIFS_ENCPWD_SIZE];
};

/*
 * there is one of these for each connection to a resource on a particular
 * session 
 */
struct cifsTconInfo {
	struct list_head cifsConnectionList;
	struct list_head openFileList;
	struct semaphore tconSem;
	struct cifsSesInfo *ses;	/* pointer to session associated with */
	char treeName[MAX_TREE_SIZE + 1]; /* UNC name of resource (in ASCII not UTF) */
	char *nativeFileSystem;
	__u16 tid;		/* The 2 byte transaction id */
	__u16 Flags;		/* optional support bits */
	enum statusEnum tidStatus;
	atomic_t useCount;	/* how many mounts (explicit or implicit refer to this share */
	FILE_SYSTEM_DEVICE_INFO fsDevInfo;
	FILE_SYSTEM_ATTRIBUTE_INFO fsAttrInfo;	/* note file system name may be truncated - but very unlikely */
	FILE_SYSTEM_UNIX_INFO fsUnixInfo;
	/* BB add field for back pointer to sb struct? */
};

/*
 * This info hangs off the cifsFileInfo structure.  This is used to track
 * byte stream locks on the file
 */
struct cifsLockInfo {
	struct cifsLockInfo *next;
	int start;
	int length;
	int type;
};

/*
 * One of these for each open instance of a file
 */
struct cifsFileInfo {
	struct list_head tlist;	/* pointer to next fid owned by tcon */
	struct list_head flist;	/* next fid (file instance) for this inode */
	unsigned int uid;	/* allows finding which FileInfo structure */
	__u32 pid;		/* process id who opened file */
	__u16 netfid;		/* file id from remote */
	/* BB add lock scope info here if needed */ ;
	/* lock scope id (0 if none) */
	struct file * pfile; /* needed for writepage */
	int endOfSearch:1;	/* we have reached end of search */
	int closePend:1;	/* file is marked to close */
	int emptyDir:1;
	char * search_resume_name;
	unsigned int resume_name_length;
	__u32 resume_key;
};

/*
 * One of these for each file inode
 */

struct cifsInodeInfo {
	struct list_head lockList;
	/* BB add in lists for dirty pages - i.e. write caching info for oplock */
	struct list_head openFileList;
	int write_behind_rc;
	__u32 cifsAttrs; /* e.g. DOS archive bit, sparse, compressed, system */
	atomic_t inUse;	 /* num concurrent users (local openers cifs) of file*/
	unsigned long time;	/* jiffies of last update/check of inode */
	int clientCanCacheRead:1; /* read oplock */
	int clientCanCacheAll:1;  /* read and writebehind oplock */
	int oplockPending:1;
	struct inode vfs_inode;
};

static inline struct cifsInodeInfo *
CIFS_I(struct inode *inode)
{
	return container_of(inode, struct cifsInodeInfo, vfs_inode);
}

static inline struct cifs_sb_info *
CIFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}


/* one of these for every pending CIFS request to the server */
struct mid_q_entry {
	struct list_head qhead;	/* mids waiting on reply from this server */
	__u16 mid;		/* multiplex id */
	__u16 pid;		/* process id */
	__u32 sequence_number;  /* for CIFS signing */
	__u16 command;		/* smb command code */
	struct timeval when_sent;	/* time when smb sent */
	struct cifsSesInfo *ses;	/* smb was sent to this server */
	struct task_struct *tsk;	/* task waiting for response */
	struct smb_hdr *resp_buf;	/* response buffer */
	int midState;	/* wish this were enum but can not pass to wait_event */
};

struct oplock_q_entry {
	struct list_head qhead;
	struct file * file_to_flush;
	struct cifsTconInfo * tcon; 
};

#define   MID_FREE 0
#define   MID_REQUEST_ALLOCATED 1
#define   MID_REQUEST_SUBMITTED 2
#define   MID_RESPONSE_RECEIVED 4

struct servers_not_supported { /* @z4a */
	struct servers_not_supported *next1;  /* @z4a */
	char server_Name[SERVER_NAME_LEN_WITH_NULL]; /* @z4a */
	/* Server Names in SMB protocol are 15 chars + X'20'  */
	/*   in 16th byte...                      @z4a        */
};

/*
 *****************************************************************
 * All constants go here
 *****************************************************************
 */

#define UID_HASH (16)

/*
 * Note that ONE module should define _DECLARE_GLOBALS_HERE to cause the
 * following to be declared.
 */

/* BB Every global should have an associated mutex for safe update BB */

#ifdef DECLARE_GLOBALS_HERE
#define GLOBAL_EXTERN
#else
#define GLOBAL_EXTERN extern
#endif

/*
 * The list of servers that did not respond with NT LM 0.12.
 * This list helps improve performance and eliminate the messages indicating
 * that we had a communications error talking to the server in this list. 
 */
GLOBAL_EXTERN struct servers_not_supported *NotSuppList;	/*@z4a */

/*
 * The following is a hash table of all the users we know about.
 */
GLOBAL_EXTERN struct smbUidInfo *GlobalUidList[UID_HASH];

GLOBAL_EXTERN struct list_head GlobalServerList; /* BB not implemented yet */
GLOBAL_EXTERN struct list_head GlobalSMBSessionList;
GLOBAL_EXTERN struct list_head GlobalTreeConnectionList;
GLOBAL_EXTERN rwlock_t GlobalSMBSeslock;  /* protects list inserts on 3 above */

GLOBAL_EXTERN struct list_head GlobalOplock_Q;

/*
 * Global transaction id (XID) information
 */
GLOBAL_EXTERN unsigned int GlobalCurrentXid;	/* protected by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalTotalActiveXid;	/* prot by GlobalMid_Sem */
GLOBAL_EXTERN unsigned int GlobalMaxActiveXid;	/* prot by GlobalMid_Sem */
GLOBAL_EXTERN rwlock_t GlobalMid_Lock;  /* protects above and list operations */
					/* on midQ entries */
GLOBAL_EXTERN char Local_System_Name[15];

/*
 *  Global counters, updated atomically
 */
GLOBAL_EXTERN atomic_t sesInfoAllocCount;
GLOBAL_EXTERN atomic_t tconInfoAllocCount;

/* Various Debug counters to remove someday (BB) */
GLOBAL_EXTERN atomic_t bufAllocCount;
GLOBAL_EXTERN atomic_t midCount;

/* Misc globals */
GLOBAL_EXTERN unsigned int multiuser_mount;	/* if enabled allows new sessions
				to be established on existing mount if we
				have the uid/password or Kerberos credential 
				or equivalent for current user */
GLOBAL_EXTERN unsigned int oplockEnabled;
GLOBAL_EXTERN unsigned int lookupCacheEnabled;
GLOBAL_EXTERN unsigned int extended_security;	/* if on, session setup sent 
				with more secure ntlmssp2 challenge/resp */
GLOBAL_EXTERN unsigned int ntlmv2_support;  /* better optional password hash */
GLOBAL_EXTERN unsigned int sign_CIFS_PDUs;  /* enable smb packet signing */

