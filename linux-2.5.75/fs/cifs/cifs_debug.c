/*
 *   fs/cifs_debug.c
 *
 *   Copyright (c) International Business Machines  Corp., 2000,2002
 *
 *   Modified by Steve French (sfrench@us.ibm.com)
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"

void
cifs_dump_mem(char *label, void *data, int length)
{
	int i, j;
	int *intptr = data;
	char *charptr = data;
	char buf[10], line[80];

	printk(KERN_DEBUG "%s: dump of %d bytes of data at 0x%p\n\n", 
		label, length, data);
	for (i = 0; i < length; i += 16) {
		line[0] = 0;
		for (j = 0; (j < 4) && (i + j * 4 < length); j++) {
			sprintf(buf, " %08x", intptr[i / 4 + j]);
			strcat(line, buf);
		}
		buf[0] = ' ';
		buf[2] = 0;
		for (j = 0; (j < 16) && (i + j < length); j++) {
			buf[1] = isprint(charptr[i + j]) ? charptr[i + j] : '.';
			strcat(line, buf);
		}
		printk(KERN_DEBUG "%s\n", line);
	}
}

#ifdef CONFIG_PROC_FS
int
cifs_debug_data_read(char *buf, char **beginBuffer, off_t offset,
		     int count, int *eof, void *data)
{
	struct list_head *tmp;
	struct cifsSesInfo *ses;
	struct cifsTconInfo *tcon;
	int i;
	int length = 0;
	char *buf_start = buf;

	length =
	    sprintf(buf,
		    "Display Internal CIFS Data Structures for Debugging\n"
		    "---------------------------------------------------\n");
	buf += length;

	length = sprintf(buf, "Servers: \n");
	buf += length;

	i = 0;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalSMBSessionList) {
		i++;
		ses = list_entry(tmp, struct cifsSesInfo, cifsSessionList);
		length =
		    sprintf(buf,
			    "\n%d) Name: %s  Domain: %s Mounts: %d ServerOS: %s  ServerNOS: %s\n\tCapabilities: 0x%x",
				i, ses->serverName, ses->serverDomain, atomic_read(&ses->inUse),
				ses->serverOS, ses->serverNOS, ses->capabilities);
		buf += length;
		if(ses->server)
			buf += sprintf(buf, "\tLocal Users To Same Server: %d ",
				atomic_read(&ses->server->socketUseCount));
	}
	read_unlock(&GlobalSMBSeslock);
	sprintf(buf, "\n");
	buf++;

	length = sprintf(buf, "\nShares: \n");
	buf += length;

	i = 0;
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &GlobalTreeConnectionList) {
		i++;
		tcon = list_entry(tmp, struct cifsTconInfo, cifsConnectionList);
		length =
		    sprintf(buf,
			    "\n%d) %s Uses: %d on FS: %s with characteristics: 0x%x Attributes: 0x%x\n\tPathComponentMax: %d",
			    i, tcon->treeName,
			    atomic_read(&tcon->useCount),
			    tcon->nativeFileSystem,
			    tcon->fsDevInfo.DeviceCharacteristics,
			    tcon->fsAttrInfo.Attributes,
			    tcon->fsAttrInfo.MaxPathNameComponentLength);
		buf += length;        
		if (tcon->fsDevInfo.DeviceType == FILE_DEVICE_DISK)
			length = sprintf(buf, " type: DISK ");
		else if (tcon->fsDevInfo.DeviceType == FILE_DEVICE_CD_ROM)
			length = sprintf(buf, " type: CDROM ");
		else
			length =
			    sprintf(buf, " type: %d ",
				    tcon->fsDevInfo.DeviceType);
		buf += length;
	}
	read_unlock(&GlobalSMBSeslock);
	length = sprintf(buf, "\n");
	buf += length;
	*eof = 1;
	/* BB add code to dump additional info such as TCP session info now */
	/*
	   if (offset >= (buf - buf_start)) 
	   {
	   *beginBuffer = buf;
	   return 0;
	   }
	   *beginBuffer = buf + offset;
	   if ((buf - buf_start - offset) > count)
	   return count;
   else */
	return (buf - buf_start - offset);
}

int
cifs_total_xid_read(char *buf, char **beginBuffer, off_t offset,
		    int length, int *eof, void *data)
{

	length =
	    sprintf(buf,
		    "Total vfs operations: %d and maximum simultaneous serviced by this filesystem: %d\n",
		    GlobalCurrentXid,GlobalMaxActiveXid);

	return length;
}

int
cifs_stats_read(char *buf, char **beginBuffer, off_t offset,
		  int length, int *eof, void *data)
{
	int item_length;
	length =
	    sprintf(buf,
		    "Currently Allocated structures\nCIFS Sessions: %d\n",sesInfoAllocCount.counter);
	buf += length;
	item_length = 
		sprintf(buf,"Shares (unique mount targets): %d\n",tconInfoAllocCount.counter);
	length += item_length;
	buf += item_length;      
	item_length = 
			sprintf(buf,"Allocated SMB Request and Response Buffers: %d\n",bufAllocCount.counter);
	length += item_length;
	buf += item_length;      
	item_length = 
		sprintf(buf,"Active Operations (MIDs in use): %d\n",midCount.counter);
	length += item_length;

	return length;
}

struct proc_dir_entry *proc_fs_cifs;
read_proc_t cifs_txanchor_read;
static read_proc_t cifsFYI_read;
static write_proc_t cifsFYI_write;
static read_proc_t oplockEnabled_read;
static write_proc_t oplockEnabled_write;
static read_proc_t lookupFlag_read;
static write_proc_t lookupFlag_write;
static read_proc_t traceSMB_read;
static write_proc_t traceSMB_write;
static read_proc_t multiuser_mount_read;
static write_proc_t multiuser_mount_write;
static read_proc_t extended_security_read;
static write_proc_t extended_security_write;
static read_proc_t ntlmv2_enabled_read;
static write_proc_t ntlmv2_enabled_write;
static read_proc_t packet_signing_enabled_read;
static write_proc_t packet_signing_enabled_write;

void
cifs_proc_init(void)
{
	struct proc_dir_entry *pde;

	proc_fs_cifs = proc_mkdir("cifs", proc_root_fs);
	if (proc_fs_cifs == NULL)
		return;

	proc_fs_cifs->owner = THIS_MODULE;
	create_proc_read_entry("DebugData", 0, proc_fs_cifs,
			       cifs_debug_data_read, 0);

	create_proc_read_entry("SimultaneousOps", 0, proc_fs_cifs,
			       cifs_total_xid_read, 0);

	create_proc_read_entry("Stats", 0, proc_fs_cifs,
			       cifs_stats_read, 0);

	pde = create_proc_read_entry("cifsFYI", 0, proc_fs_cifs,
				     cifsFYI_read, 0);
	if (pde)
		pde->write_proc = cifsFYI_write;

	pde =
	    create_proc_read_entry("traceSMB", 0, proc_fs_cifs,
				   traceSMB_read, 0);
	if (pde)
		pde->write_proc = traceSMB_write;

	pde = create_proc_read_entry("OplockEnabled", 0, proc_fs_cifs,
				     oplockEnabled_read, 0);
	if (pde)
		pde->write_proc = oplockEnabled_write;

	pde =
	    create_proc_read_entry("MultiuserMount", 0, proc_fs_cifs,
				   multiuser_mount_read, 0);
	if (pde)
		pde->write_proc = multiuser_mount_write;

	pde =
	    create_proc_read_entry("ExtendedSecurity", 0, proc_fs_cifs,
				   extended_security_read, 0);
	if (pde)
		pde->write_proc = extended_security_write;

	pde =
	create_proc_read_entry("LookupCacheEnable", 0, proc_fs_cifs,
		lookupFlag_read, 0);
	if (pde)
		pde->write_proc = lookupFlag_write;

	pde =
	    create_proc_read_entry("NTLMV2Enabled", 0, proc_fs_cifs,
				   ntlmv2_enabled_read, 0);
	if (pde)
		pde->write_proc = ntlmv2_enabled_write;

	pde =
	    create_proc_read_entry("PacketSigningEnabled", 0, proc_fs_cifs,
				   packet_signing_enabled_read, 0);
	if (pde)
		pde->write_proc = packet_signing_enabled_write;
}

void
cifs_proc_clean(void)
{
	if (proc_fs_cifs == NULL)
		return;

	remove_proc_entry("DebugData", proc_fs_cifs);
	remove_proc_entry("cifsFYI", proc_fs_cifs);
	remove_proc_entry("TraceSMB", proc_fs_cifs);
	remove_proc_entry("SimultaneousOps", proc_fs_cifs);
	remove_proc_entry("TotalOps", proc_fs_cifs);
	remove_proc_entry("MultiuserMount", proc_fs_cifs);
	remove_proc_entry("OplockEnabled", proc_fs_cifs);
	remove_proc_entry("NTLMV2Enabled",proc_fs_cifs);
	remove_proc_entry("ExtendedSecurity",proc_fs_cifs);
	remove_proc_entry("PacketSigningEnabled",proc_fs_cifs);
	remove_proc_entry("cifs", proc_root_fs);
}

static int
cifsFYI_read(char *page, char **start, off_t off, int count,
	     int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", cifsFYI);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
cifsFYI_write(struct file *file, const char *buffer,
	      unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		cifsFYI = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		cifsFYI = 1;

	return count;
}

static int
oplockEnabled_read(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", oplockEnabled);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
oplockEnabled_write(struct file *file, const char *buffer,
		    unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		oplockEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		oplockEnabled = 1;

	return count;
}

static int
lookupFlag_read(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", lookupCacheEnabled);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
lookupFlag_write(struct file *file, const char *buffer,
		    unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		lookupCacheEnabled = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		lookupCacheEnabled = 1;

	return count;
}
static int
traceSMB_read(char *page, char **start, off_t off, int count,
	      int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", traceSMB);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
traceSMB_write(struct file *file, const char *buffer,
	       unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		traceSMB = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		traceSMB = 1;

	return count;
}

static int
multiuser_mount_read(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", multiuser_mount);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
multiuser_mount_write(struct file *file, const char *buffer,
		      unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		multiuser_mount = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		multiuser_mount = 1;

	return count;
}

static int
extended_security_read(char *page, char **start, off_t off,
		       int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", extended_security);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
extended_security_write(struct file *file, const char *buffer,
			unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		extended_security = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		extended_security = 1;

	return count;
}

static int
ntlmv2_enabled_read(char *page, char **start, off_t off,
		       int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", ntlmv2_support);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
ntlmv2_enabled_write(struct file *file, const char *buffer,
			unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		ntlmv2_support = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		ntlmv2_support = 1;

	return count;
}

static int
packet_signing_enabled_read(char *page, char **start, off_t off,
		       int count, int *eof, void *data)
{
	int len;

	len = sprintf(page, "%d\n", sign_CIFS_PDUs);

	len -= off;
	*start = page + off;

	if (len > count)
		len = count;
	else
		*eof = 1;

	if (len < 0)
		len = 0;

	return len;
}
static int
packet_signing_enabled_write(struct file *file, const char *buffer,
			unsigned long count, void *data)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;
	if (c == '0' || c == 'n' || c == 'N')
		sign_CIFS_PDUs = 0;
	else if (c == '1' || c == 'y' || c == 'Y')
		sign_CIFS_PDUs = 1;

	return count;
}


#endif
