#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/blk.h>
#include <linux/blkpg.h>
#include <linux/cdrom.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

#include "sr.h"

#if 0
#define DEBUG
#endif

/* The sr_is_xa() seems to trigger firmware bugs with some drives :-(
 * It is off by default and can be turned on with this module parameter */
static int xa_test = 0;

#define IOCTL_RETRIES 3

/* ATAPI drives don't have a SCMD_PLAYAUDIO_TI command.  When these drives
   are emulating a SCSI device via the idescsi module, they need to have
   CDROMPLAYTRKIND commands translated into CDROMPLAYMSF commands for them */

static int sr_fake_playtrkind(struct cdrom_device_info *cdi, struct cdrom_ti *ti)
{
	struct cdrom_tocentry trk0_te, trk1_te;
	struct cdrom_tochdr tochdr;
	struct cdrom_generic_command cgc;
	int ntracks, ret;

	if ((ret = sr_audio_ioctl(cdi, CDROMREADTOCHDR, &tochdr)))
		return ret;

	ntracks = tochdr.cdth_trk1 - tochdr.cdth_trk0 + 1;
	
	if (ti->cdti_trk1 == ntracks) 
		ti->cdti_trk1 = CDROM_LEADOUT;
	else if (ti->cdti_trk1 != CDROM_LEADOUT)
		ti->cdti_trk1 ++;

	trk0_te.cdte_track = ti->cdti_trk0;
	trk0_te.cdte_format = CDROM_MSF;
	trk1_te.cdte_track = ti->cdti_trk1;
	trk1_te.cdte_format = CDROM_MSF;
	
	if ((ret = sr_audio_ioctl(cdi, CDROMREADTOCENTRY, &trk0_te)))
		return ret;
	if ((ret = sr_audio_ioctl(cdi, CDROMREADTOCENTRY, &trk1_te)))
		return ret;

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_PLAY_AUDIO_MSF;
	cgc.cmd[3] = trk0_te.cdte_addr.msf.minute;
	cgc.cmd[4] = trk0_te.cdte_addr.msf.second;
	cgc.cmd[5] = trk0_te.cdte_addr.msf.frame;
	cgc.cmd[6] = trk1_te.cdte_addr.msf.minute;
	cgc.cmd[7] = trk1_te.cdte_addr.msf.second;
	cgc.cmd[8] = trk1_te.cdte_addr.msf.frame;
	cgc.data_direction = SCSI_DATA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cdi->handle, &cgc);
}

/* We do our own retries because we want to know what the specific
   error code is.  Normally the UNIT_ATTENTION code will automatically
   clear after one error */

int sr_do_ioctl(Scsi_CD *cd, struct cdrom_generic_command *cgc)
{
	struct scsi_request *SRpnt;
	struct scsi_device *SDev;
        struct request *req;
	int result, err = 0, retries = 0;

	SDev = cd->device;
	SRpnt = scsi_allocate_request(SDev);
        if (!SRpnt) {
                printk(KERN_ERR "Unable to allocate SCSI request in sr_do_ioctl");
		err = -ENOMEM;
		goto out;
        }
	SRpnt->sr_data_direction = cgc->data_direction;

      retry:
	if (!scsi_block_when_processing_errors(SDev)) {
		err = -ENODEV;
		goto out_free;
	}

	scsi_wait_req(SRpnt, cgc->cmd, cgc->buffer, cgc->buflen,
		      cgc->timeout, IOCTL_RETRIES);

	req = SRpnt->sr_request;
	if (SRpnt->sr_buffer && req->buffer && SRpnt->sr_buffer != req->buffer) {
		memcpy(req->buffer, SRpnt->sr_buffer, SRpnt->sr_bufflen);
		kfree(SRpnt->sr_buffer);
		SRpnt->sr_buffer = req->buffer;
        }

	result = SRpnt->sr_result;

	/* Minimal error checking.  Ignore cases we know about, and report the rest. */
	if (driver_byte(result) != 0) {
		switch (SRpnt->sr_sense_buffer[2] & 0xf) {
		case UNIT_ATTENTION:
			SDev->changed = 1;
			if (!cgc->quiet)
				printk(KERN_INFO "%s: disc change detected.\n", cd->cdi.name);
			if (retries++ < 10)
				goto retry;
			err = -ENOMEDIUM;
			break;
		case NOT_READY:	/* This happens if there is no disc in drive */
			if (SRpnt->sr_sense_buffer[12] == 0x04 &&
			    SRpnt->sr_sense_buffer[13] == 0x01) {
				/* sense: Logical unit is in process of becoming ready */
				if (!cgc->quiet)
					printk(KERN_INFO "%s: CDROM not ready yet.\n", cd->cdi.name);
				if (retries++ < 10) {
					/* sleep 2 sec and try again */
					scsi_sleep(2 * HZ);
					goto retry;
				} else {
					/* 20 secs are enough? */
					err = -ENOMEDIUM;
					break;
				}
			}
			if (!cgc->quiet)
				printk(KERN_INFO "%s: CDROM not ready.  Make sure there is a disc in the drive.\n", cd->cdi.name);
#ifdef DEBUG
			print_req_sense("sr", SRpnt);
#endif
			err = -ENOMEDIUM;
			break;
		case ILLEGAL_REQUEST:
			err = -EIO;
			if (SRpnt->sr_sense_buffer[12] == 0x20 &&
			    SRpnt->sr_sense_buffer[13] == 0x00)
				/* sense: Invalid command operation code */
				err = -EDRIVE_CANT_DO_THIS;
#ifdef DEBUG
			print_command(cgc->cmd);
			print_req_sense("sr", SRpnt);
#endif
			break;
		default:
			printk(KERN_ERR "%s: CDROM (ioctl) error, command: ", cd->cdi.name);
			print_command(cgc->cmd);
			print_req_sense("sr", SRpnt);
			err = -EIO;
		}
	}

	if (cgc->sense)
		memcpy(cgc->sense, SRpnt->sr_sense_buffer, sizeof(*cgc->sense));

	/* Wake up a process waiting for device */
      out_free:
	scsi_release_request(SRpnt);
	SRpnt = NULL;
      out:
	cgc->stat = err;
	return err;
}

/* ---------------------------------------------------------------------- */
/* interface to cdrom.c                                                   */

static int test_unit_ready(Scsi_CD *cd)
{
	struct cdrom_generic_command cgc;

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_TEST_UNIT_READY;
	cgc.quiet = 1;
	cgc.data_direction = SCSI_DATA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cd, &cgc);
}

int sr_tray_move(struct cdrom_device_info *cdi, int pos)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_generic_command cgc;

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_START_STOP_UNIT;
	cgc.cmd[4] = (pos == 0) ? 0x03 /* close */ : 0x02 /* eject */ ;
	cgc.data_direction = SCSI_DATA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cd, &cgc);
}

int sr_lock_door(struct cdrom_device_info *cdi, int lock)
{
	Scsi_CD *cd = cdi->handle;

	return scsi_set_medium_removal(cd->device, lock ?
		       SCSI_REMOVAL_PREVENT : SCSI_REMOVAL_ALLOW);
}

int sr_drive_status(struct cdrom_device_info *cdi, int slot)
{
	if (CDSL_CURRENT != slot) {
		/* we have no changer support */
		return -EINVAL;
	}
	if (0 == test_unit_ready(cdi->handle))
		return CDS_DISC_OK;

	return CDS_TRAY_OPEN;
}

int sr_disk_status(struct cdrom_device_info *cdi)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_tochdr toc_h;
	struct cdrom_tocentry toc_e;
	int i, rc, have_datatracks = 0;

	/* look for data tracks */
	if (0 != (rc = sr_audio_ioctl(cdi, CDROMREADTOCHDR, &toc_h)))
		return (rc == -ENOMEDIUM) ? CDS_NO_DISC : CDS_NO_INFO;

	for (i = toc_h.cdth_trk0; i <= toc_h.cdth_trk1; i++) {
		toc_e.cdte_track = i;
		toc_e.cdte_format = CDROM_LBA;
		if (sr_audio_ioctl(cdi, CDROMREADTOCENTRY, &toc_e))
			return CDS_NO_INFO;
		if (toc_e.cdte_ctrl & CDROM_DATA_TRACK) {
			have_datatracks = 1;
			break;
		}
	}
	if (!have_datatracks)
		return CDS_AUDIO;

	if (cd->xa_flag)
		return CDS_XA_2_1;
	else
		return CDS_DATA_1;
}

int sr_get_last_session(struct cdrom_device_info *cdi,
			struct cdrom_multisession *ms_info)
{
	Scsi_CD *cd = cdi->handle;

	ms_info->addr.lba = cd->ms_offset;
	ms_info->xa_flag = cd->xa_flag || cd->ms_offset > 0;

	return 0;
}

/* primitive to determine whether we need to have GFP_DMA set based on
 * the status of the unchecked_isa_dma flag in the host structure */
#define SR_GFP_DMA(cd) (((cd)->device->host->unchecked_isa_dma) ? GFP_DMA : 0)

int sr_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_generic_command cgc;
	char *buffer = kmalloc(32, GFP_KERNEL | SR_GFP_DMA(cd));
	int result;

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_READ_SUBCHANNEL;
	cgc.cmd[2] = 0x40;	/* I do want the subchannel info */
	cgc.cmd[3] = 0x02;	/* Give me medium catalog number info */
	cgc.cmd[8] = 24;
	cgc.buffer = buffer;
	cgc.buflen = 24;
	cgc.data_direction = SCSI_DATA_READ;
	cgc.timeout = IOCTL_TIMEOUT;
	result = sr_do_ioctl(cd, &cgc);

	memcpy(mcn->medium_catalog_number, buffer + 9, 13);
	mcn->medium_catalog_number[13] = 0;

	kfree(buffer);
	return result;
}

int sr_reset(struct cdrom_device_info *cdi)
{
	return 0;
}

int sr_select_speed(struct cdrom_device_info *cdi, int speed)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_generic_command cgc;

	if (speed == 0)
		speed = 0xffff;	/* set to max */
	else
		speed *= 177;	/* Nx to kbyte/s */

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_SET_SPEED;	/* SET CD SPEED */
	cgc.cmd[2] = (speed >> 8) & 0xff;	/* MSB for speed (in kbytes/sec) */
	cgc.cmd[3] = speed & 0xff;	/* LSB */
	cgc.data_direction = SCSI_DATA_NONE;
	cgc.timeout = IOCTL_TIMEOUT;

	if (sr_do_ioctl(cd, &cgc))
		return -EIO;
	return 0;
}

/* ----------------------------------------------------------------------- */
/* this is called by the generic cdrom driver. arg is a _kernel_ pointer,  */
/* because the generic cdrom driver does the user access stuff for us.     */
/* only cdromreadtochdr and cdromreadtocentry are left - for use with the  */
/* sr_disk_status interface for the generic cdrom driver.                  */

int sr_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd, void *arg)
{
	Scsi_CD *cd = cdi->handle;
	struct cdrom_generic_command cgc;
	int result;
	unsigned char *buffer = kmalloc(32, GFP_KERNEL | SR_GFP_DMA(cd));

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.timeout = IOCTL_TIMEOUT;

	switch (cmd) {
	case CDROMREADTOCHDR:
		{
			struct cdrom_tochdr *tochdr = (struct cdrom_tochdr *) arg;

			cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
			cgc.cmd[8] = 12;		/* LSB of length */
			cgc.buffer = buffer;
			cgc.buflen = 12;
			cgc.quiet = 1;
			cgc.data_direction = SCSI_DATA_READ;

			result = sr_do_ioctl(cd, &cgc);

			tochdr->cdth_trk0 = buffer[2];
			tochdr->cdth_trk1 = buffer[3];

			break;
		}

	case CDROMREADTOCENTRY:
		{
			struct cdrom_tocentry *tocentry = (struct cdrom_tocentry *) arg;

			cgc.cmd[0] = GPCMD_READ_TOC_PMA_ATIP;
			cgc.cmd[1] |= (tocentry->cdte_format == CDROM_MSF) ? 0x02 : 0;
			cgc.cmd[6] = tocentry->cdte_track;
			cgc.cmd[8] = 12;		/* LSB of length */
			cgc.buffer = buffer;
			cgc.buflen = 12;
			cgc.data_direction = SCSI_DATA_READ;

			result = sr_do_ioctl(cd, &cgc);

			tocentry->cdte_ctrl = buffer[5] & 0xf;
			tocentry->cdte_adr = buffer[5] >> 4;
			tocentry->cdte_datamode = (tocentry->cdte_ctrl & 0x04) ? 1 : 0;
			if (tocentry->cdte_format == CDROM_MSF) {
				tocentry->cdte_addr.msf.minute = buffer[9];
				tocentry->cdte_addr.msf.second = buffer[10];
				tocentry->cdte_addr.msf.frame = buffer[11];
			} else
				tocentry->cdte_addr.lba = (((((buffer[8] << 8) + buffer[9]) << 8)
					+ buffer[10]) << 8) + buffer[11];

			break;
		}

	case CDROMPLAYTRKIND: {
		struct cdrom_ti* ti = (struct cdrom_ti*)arg;

		cgc.cmd[0] = GPCMD_PLAYAUDIO_TI;
		cgc.cmd[4] = ti->cdti_trk0;
		cgc.cmd[5] = ti->cdti_ind0;
		cgc.cmd[7] = ti->cdti_trk1;
		cgc.cmd[8] = ti->cdti_ind1;
		cgc.data_direction = SCSI_DATA_NONE;

		result = sr_do_ioctl(cd, &cgc);
		if (result == -EDRIVE_CANT_DO_THIS)
			result = sr_fake_playtrkind(cdi, ti);

		break;
	}

	default:
		result = -EINVAL;
	}

#if 0
	if (result)
		printk("DEBUG: sr_audio: result for ioctl %x: %x\n", cmd, result);
#endif

	kfree(buffer);
	return result;
}

/* -----------------------------------------------------------------------
 * a function to read all sorts of funny cdrom sectors using the READ_CD
 * scsi-3 mmc command
 *
 * lba:     linear block address
 * format:  0 = data (anything)
 *          1 = audio
 *          2 = data (mode 1)
 *          3 = data (mode 2)
 *          4 = data (mode 2 form1)
 *          5 = data (mode 2 form2)
 * blksize: 2048 | 2336 | 2340 | 2352
 */

static int sr_read_cd(Scsi_CD *cd, unsigned char *dest, int lba, int format, int blksize)
{
	struct cdrom_generic_command cgc;

#ifdef DEBUG
	printk("%s: sr_read_cd lba=%d format=%d blksize=%d\n",
	       cd->cdi.name, lba, format, blksize);
#endif

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_READ_CD;	/* READ_CD */
	cgc.cmd[1] = ((format & 7) << 2);
	cgc.cmd[2] = (unsigned char) (lba >> 24) & 0xff;
	cgc.cmd[3] = (unsigned char) (lba >> 16) & 0xff;
	cgc.cmd[4] = (unsigned char) (lba >> 8) & 0xff;
	cgc.cmd[5] = (unsigned char) lba & 0xff;
	cgc.cmd[8] = 1;
	switch (blksize) {
	case 2336:
		cgc.cmd[9] = 0x58;
		break;
	case 2340:
		cgc.cmd[9] = 0x78;
		break;
	case 2352:
		cgc.cmd[9] = 0xf8;
		break;
	default:
		cgc.cmd[9] = 0x10;
		break;
	}
	cgc.buffer = dest;
	cgc.buflen = blksize;
	cgc.data_direction = SCSI_DATA_READ;
	cgc.timeout = IOCTL_TIMEOUT;
	return sr_do_ioctl(cd, &cgc);
}

/*
 * read sectors with blocksizes other than 2048
 */

static int sr_read_sector(Scsi_CD *cd, int lba, int blksize, unsigned char *dest)
{
	struct cdrom_generic_command cgc;
	int rc;

	/* we try the READ CD command first... */
	if (cd->readcd_known) {
		rc = sr_read_cd(cd, dest, lba, 0, blksize);
		if (-EDRIVE_CANT_DO_THIS != rc)
			return rc;
		cd->readcd_known = 0;
		printk("CDROM does'nt support READ CD (0xbe) command\n");
		/* fall & retry the other way */
	}
	/* ... if this fails, we switch the blocksize using MODE SELECT */
	if (blksize != cd->device->sector_size) {
		if (0 != (rc = sr_set_blocklength(cd, blksize)))
			return rc;
	}
#ifdef DEBUG
	printk("%s: sr_read_sector lba=%d blksize=%d\n", cd->cdi.name, lba, blksize);
#endif

	memset(&cgc, 0, sizeof(struct cdrom_generic_command));
	cgc.cmd[0] = GPCMD_READ_10;
	cgc.cmd[2] = (unsigned char) (lba >> 24) & 0xff;
	cgc.cmd[3] = (unsigned char) (lba >> 16) & 0xff;
	cgc.cmd[4] = (unsigned char) (lba >> 8) & 0xff;
	cgc.cmd[5] = (unsigned char) lba & 0xff;
	cgc.cmd[8] = 1;
	cgc.buffer = dest;
	cgc.buflen = blksize;
	cgc.data_direction = SCSI_DATA_READ;
	cgc.timeout = IOCTL_TIMEOUT;
	rc = sr_do_ioctl(cd, &cgc);

	return rc;
}

/*
 * read a sector in raw mode to check the sector format
 * ret: 1 == mode2 (XA), 0 == mode1, <0 == error 
 */

int sr_is_xa(Scsi_CD *cd)
{
	unsigned char *raw_sector;
	int is_xa;

	if (!xa_test)
		return 0;

	raw_sector = (unsigned char *) kmalloc(2048, GFP_KERNEL | SR_GFP_DMA(cd));
	if (!raw_sector)
		return -ENOMEM;
	if (0 == sr_read_sector(cd, cd->ms_offset + 16,
				CD_FRAMESIZE_RAW1, raw_sector)) {
		is_xa = (raw_sector[3] == 0x02) ? 1 : 0;
	} else {
		/* read a raw sector failed for some reason. */
		is_xa = -1;
	}
	kfree(raw_sector);
#ifdef DEBUG
	printk("%s: sr_is_xa: %d\n", cd->cdi.name, is_xa);
#endif
	return is_xa;
}

int sr_dev_ioctl(struct cdrom_device_info *cdi,
		 unsigned int cmd, unsigned long arg)
{
	Scsi_CD *cd = cdi->handle;
	return scsi_ioctl(cd->device, cmd, (void *)arg);
}
