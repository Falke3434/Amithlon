/* Driver for Lexar "Jumpshot" Compact Flash reader
 *
 * $Id: jumpshot.c,v 1.7 2002/02/25 00:40:13 mdharm Exp $
 *
 * jumpshot driver v0.1:
 *
 * First release
 *
 * Current development and maintenance by:
 *   (c) 2000 Jimmie Mayfield (mayfield+usb@sackheads.org)
 *
 *   Many thanks to Robert Baruch for the SanDisk SmartMedia reader driver
 *   which I used as a template for this driver.
 *
 *   Some bugfixes and scatter-gather code by Gregory P. Smith 
 *   (greg-usb@electricrain.com)
 *
 *   Fix for media change by Joerg Schneider (js@joergschneider.com)
 *
 * Developed with the assistance of:
 *
 *   (C) 2002 Alan Stern <stern@rowland.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
 
 /*
  * This driver attempts to support the Lexar Jumpshot USB CompactFlash 
  * reader.  Like many other USB CompactFlash readers, the Jumpshot contains
  * a USB-to-ATA chip. 
  *
  * This driver supports reading and writing.  If you're truly paranoid,
  * however, you can force the driver into a write-protected state by setting
  * the WP enable bits in jumpshot_handle_mode_sense.  Basically this means
  * setting mode_param_header[3] = 0x80.  
  */

#include "transport.h"
#include "raw_bulk.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"
#include "jumpshot.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

static inline int jumpshot_bulk_read(struct us_data *us,
				     unsigned char *data, 
				     unsigned int len)
{
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("jumpshot_bulk_read:  len = %d\n", len);
	return usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
			data, len, NULL);
}


static inline int jumpshot_bulk_write(struct us_data *us,
				      unsigned char *data, 
				      unsigned int len)
{
	if (len == 0)
		return USB_STOR_XFER_GOOD;

	US_DEBUGP("jumpshot_bulk_write:  len = %d\n", len);
	return usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
			data, len, NULL);
}


static int jumpshot_get_status(struct us_data  *us)
{
	unsigned char reply;
	int rc;

	if (!us)
		return USB_STOR_TRANSPORT_ERROR;

	// send the setup
	rc = usb_stor_ctrl_transfer(us, us->recv_ctrl_pipe,
				   0, 0xA0, 0, 7, &reply, 1);

	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	if (reply != 0x50) {
		US_DEBUGP("jumpshot_get_status:  0x%2x\n",
			  (unsigned short) (reply));
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int jumpshot_read_data(struct us_data *us,
			      struct jumpshot_info *info,
			      u32 sector,
			      u32 sectors, 
			      unsigned char *dest, 
			      int use_sg)
{
	unsigned char command[] = { 0, 0, 0, 0, 0, 0xe0, 0x20 };
	unsigned char *buffer = NULL;
	unsigned char *ptr;
	unsigned char  thistime;
	int totallen, len, result;
	int sg_idx = 0, current_sg_offset = 0;

	// we're working in LBA mode.  according to the ATA spec, 
	// we can support up to 28-bit addressing.  I don't know if Jumpshot
	// supports beyond 24-bit addressing.  It's kind of hard to test 
	// since it requires > 8GB CF card.

	if (sector > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	totallen = sectors * info->ssize;

	do {
		// loop, never allocate or transfer more than 64k at once
		// (min(128k, 255*info->ssize) is the real limit)
		len = min_t(int, totallen, 65536);

		if (use_sg) {
			buffer = kmalloc(len, GFP_NOIO);
			if (buffer == NULL)
				return USB_STOR_TRANSPORT_ERROR;
			ptr = buffer;
		} else {
			ptr = dest;
		}

		thistime = (len / info->ssize) & 0xff;

		command[0] = 0;
		command[1] = thistime;
		command[2] = sector & 0xFF;
		command[3] = (sector >>  8) & 0xFF;
		command[4] = (sector >> 16) & 0xFF;

		command[5] |= (sector >> 24) & 0x0F;

		// send the setup + command
		result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
					       0, 0x20, 0, 1, command, 7);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// read the result
		result = jumpshot_bulk_read(us, ptr, len);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		US_DEBUGP("jumpshot_read_data:  %d bytes\n", len);
	
		sectors -= thistime;
		sector  += thistime;

		if (use_sg) {
			us_copy_to_sgbuf(buffer, len, dest,
					 &sg_idx, &current_sg_offset, use_sg);
			kfree(buffer);
		} else {
			dest += len;
		}

		totallen -= len;
	} while (totallen > 0);

	return USB_STOR_TRANSPORT_GOOD;

 leave:
	if (use_sg)
		kfree(buffer);
	return USB_STOR_TRANSPORT_ERROR;
}


static int jumpshot_write_data(struct us_data *us,
			       struct jumpshot_info *info,
			       u32 sector,
			       u32 sectors, 
			       unsigned char *src, 
			       int use_sg)
{
	unsigned char command[7] = { 0, 0, 0, 0, 0, 0xE0, 0x30 };
	unsigned char *buffer = NULL;
	unsigned char *ptr;
	unsigned char  thistime;
	int totallen, len, result, waitcount;
	int sg_idx = 0, sg_offset = 0;

	// we're working in LBA mode.  according to the ATA spec, 
	// we can support up to 28-bit addressing.  I don't know if Jumpshot
	// supports beyond 24-bit addressing.  It's kind of hard to test 
	// since it requires > 8GB CF card.
	//
	if (sector > 0x0FFFFFFF)
		return USB_STOR_TRANSPORT_ERROR;

	totallen = sectors * info->ssize;

	do {
		// loop, never allocate or transfer more than 64k at once
		// (min(128k, 255*info->ssize) is the real limit)

		len = min_t(int, totallen, 65536);

		// if we are using scatter-gather,
		// first copy all to one big buffer

		buffer = us_copy_from_sgbuf(src, len, &sg_idx,
					    &sg_offset, use_sg);
		if (buffer == NULL)
			return USB_STOR_TRANSPORT_ERROR;

		ptr = buffer;

		thistime = (len / info->ssize) & 0xff;

		command[0] = 0;
		command[1] = thistime;
		command[2] = sector & 0xFF;
		command[3] = (sector >>  8) & 0xFF;
		command[4] = (sector >> 16) & 0xFF;

		command[5] |= (sector >> 24) & 0x0F;

		// send the setup + command
		result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
			0, 0x20, 0, 1, command, 7);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// send the data
		result = jumpshot_bulk_write(us, ptr, len);
		if (result != USB_STOR_XFER_GOOD)
			goto leave;

		// read the result.  apparently the bulk write can complete
		// before the jumpshot drive is finished writing.  so we loop
		// here until we get a good return code
		waitcount = 0;
		do {
			result = jumpshot_get_status(us);
			if (result != USB_STOR_TRANSPORT_GOOD) {
				// I have not experimented to find the smallest value.
				//
				wait_ms(50); 
			}
		} while ((result != USB_STOR_TRANSPORT_GOOD) && (waitcount < 10));

		if (result != USB_STOR_TRANSPORT_GOOD)
			US_DEBUGP("jumpshot_write_data:  Gah!  Waitcount = 10.  Bad write!?\n");
		
		sectors -= thistime;
		sector  += thistime;

		if (use_sg)
			kfree(buffer);
		else
			src += len;

		totallen -= len;
	} while (totallen > 0);

	return result;

 leave:
	if (use_sg)
		kfree(buffer);
	return USB_STOR_TRANSPORT_ERROR;
}

static int jumpshot_id_device(struct us_data *us,
			      struct jumpshot_info *info)
{
	unsigned char command[2] = { 0xe0, 0xec };
	unsigned char reply[512];
	int 	 rc;

	if (!us || !info)
		return USB_STOR_TRANSPORT_ERROR;

	// send the setup
	rc = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
				   0, 0x20, 0, 6, command, 2);

	if (rc != USB_STOR_XFER_GOOD) {
		US_DEBUGP("jumpshot_id_device:  Gah! "
			  "send_control for read_capacity failed\n");
		return rc;
	}

	// read the reply
	rc = jumpshot_bulk_read(us, reply, sizeof(reply));
	if (rc != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	info->sectors = ((u32)(reply[117]) << 24) |
			((u32)(reply[116]) << 16) |
			((u32)(reply[115]) <<  8) |
			((u32)(reply[114])      );

	return USB_STOR_TRANSPORT_GOOD;
}

static int jumpshot_handle_mode_sense(struct us_data *us,
				      Scsi_Cmnd * srb, 
				      unsigned char *ptr,
				      int sense_6)
{
	unsigned char mode_param_header[8] = {
		0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char rw_err_page[12] = {
		0x1, 0xA, 0x21, 1, 0, 0, 0, 0, 1, 0, 0, 0
	};
	unsigned char cache_page[12] = {
		0x8, 0xA, 0x1, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char rbac_page[12] = {
		0x1B, 0xA, 0, 0x81, 0, 0, 0, 0, 0, 0, 0, 0
	};
	unsigned char timer_page[8] = {
		0x1C, 0x6, 0, 0, 0, 0
	};
	unsigned char pc, page_code;
	unsigned short total_len = 0;
	unsigned short param_len, i = 0;


	if (sense_6)
		param_len = srb->cmnd[4];
	else
		param_len = ((u32) (srb->cmnd[7]) >> 8) | ((u32) (srb->cmnd[8]));


	pc = srb->cmnd[2] >> 6;
	page_code = srb->cmnd[2] & 0x3F;

	switch (pc) {
	   case 0x0:
		US_DEBUGP("jumpshot_handle_mode_sense:  Current values\n");
		break;
	   case 0x1:
		US_DEBUGP("jumpshot_handle_mode_sense:  Changeable values\n");
		break;
	   case 0x2:
		US_DEBUGP("jumpshot_handle_mode_sense:  Default values\n");
		break;
	   case 0x3:
		US_DEBUGP("jumpshot_handle_mode_sense:  Saves values\n");
		break;
	}

	mode_param_header[3] = 0x80;	// write enable

	switch (page_code) {
	   case 0x0:
		// vendor-specific mode
		return USB_STOR_TRANSPORT_ERROR;

	   case 0x1:
		total_len = sizeof(rw_err_page);
		mode_param_header[0] = total_len >> 8;
		mode_param_header[1] = total_len & 0xFF;
		mode_param_header[3] = 0x00;	// WP enable: 0x80

		memcpy(ptr, mode_param_header, sizeof(mode_param_header));
		i += sizeof(mode_param_header);
		memcpy(ptr + i, rw_err_page, sizeof(rw_err_page));
		break;

	   case 0x8:
		total_len = sizeof(cache_page);
		mode_param_header[0] = total_len >> 8;
		mode_param_header[1] = total_len & 0xFF;
		mode_param_header[3] = 0x00;	// WP enable: 0x80

		memcpy(ptr, mode_param_header, sizeof(mode_param_header));
		i += sizeof(mode_param_header);
		memcpy(ptr + i, cache_page, sizeof(cache_page));
		break;

	   case 0x1B:
		total_len = sizeof(rbac_page);
		mode_param_header[0] = total_len >> 8;
		mode_param_header[1] = total_len & 0xFF;
		mode_param_header[3] = 0x00;	// WP enable: 0x80

		memcpy(ptr, mode_param_header, sizeof(mode_param_header));
		i += sizeof(mode_param_header);
		memcpy(ptr + i, rbac_page, sizeof(rbac_page));
		break;

	   case 0x1C:
		total_len = sizeof(timer_page);
		mode_param_header[0] = total_len >> 8;
		mode_param_header[1] = total_len & 0xFF;
		mode_param_header[3] = 0x00;	// WP enable: 0x80

		memcpy(ptr, mode_param_header, sizeof(mode_param_header));
		i += sizeof(mode_param_header);
		memcpy(ptr + i, timer_page, sizeof(timer_page));
		break;

	   case 0x3F:
		total_len = sizeof(timer_page) + sizeof(rbac_page) +
		    sizeof(cache_page) + sizeof(rw_err_page);
		mode_param_header[0] = total_len >> 8;
		mode_param_header[1] = total_len & 0xFF;
		mode_param_header[3] = 0x00;	// WP enable: 0x80

		memcpy(ptr, mode_param_header, sizeof(mode_param_header));
		i += sizeof(mode_param_header);
		memcpy(ptr + i, timer_page, sizeof(timer_page));
		i += sizeof(timer_page);
		memcpy(ptr + i, rbac_page, sizeof(rbac_page));
		i += sizeof(rbac_page);
		memcpy(ptr + i, cache_page, sizeof(cache_page));
		i += sizeof(cache_page);
		memcpy(ptr + i, rw_err_page, sizeof(rw_err_page));
		break;
	}

	return USB_STOR_TRANSPORT_GOOD;
}


void jumpshot_info_destructor(void *extra)
{
	// this routine is a placeholder...
	// currently, we don't allocate any extra blocks so we're okay
}



// Transport for the Lexar 'Jumpshot'
//
int jumpshot_transport(Scsi_Cmnd * srb, struct us_data *us)
{
	struct jumpshot_info *info;
	int rc;
	unsigned long block, blocks;
	unsigned char *ptr = NULL;
	unsigned char inquiry_response[36] = {
		0x00, 0x80, 0x00, 0x01, 0x1F, 0x00, 0x00, 0x00
	};

	if (!us->extra) {
		us->extra = kmalloc(sizeof(struct jumpshot_info), GFP_NOIO);
		if (!us->extra) {
			US_DEBUGP("jumpshot_transport:  Gah! Can't allocate storage for jumpshot info struct!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}
		memset(us->extra, 0, sizeof(struct jumpshot_info));
		us->extra_destructor = jumpshot_info_destructor;
	}

	info = (struct jumpshot_info *) (us->extra);
	ptr = (unsigned char *) srb->request_buffer;

	if (srb->cmnd[0] == INQUIRY) {
		US_DEBUGP("jumpshot_transport:  INQUIRY.  Returning bogus response.\n");
		memset(inquiry_response + 8, 0, 28);
		fill_inquiry_response(us, inquiry_response, 36);
		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == READ_CAPACITY) {
		info->ssize = 0x200;  // hard coded 512 byte sectors as per ATA spec

		rc = jumpshot_get_status(us);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		rc = jumpshot_id_device(us, info);
		if (rc != USB_STOR_TRANSPORT_GOOD)
			return rc;

		US_DEBUGP("jumpshot_transport:  READ_CAPACITY:  %ld sectors, %ld bytes per sector\n",
			  info->sectors, info->ssize);

		// build the reply
		//
		ptr[0] = (info->sectors >> 24) & 0xFF;
		ptr[1] = (info->sectors >> 16) & 0xFF;
		ptr[2] = (info->sectors >> 8) & 0xFF;
		ptr[3] = (info->sectors) & 0xFF;

		ptr[4] = (info->ssize >> 24) & 0xFF;
		ptr[5] = (info->ssize >> 16) & 0xFF;
		ptr[6] = (info->ssize >> 8) & 0xFF;
		ptr[7] = (info->ssize) & 0xFF;

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SELECT_10) {
		US_DEBUGP("jumpshot_transport:  Gah! MODE_SELECT_10.\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (srb->cmnd[0] == READ_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("jumpshot_transport:  READ_10: read block 0x%04lx  count %ld\n", block, blocks);
		return jumpshot_read_data(us, info, block, blocks, ptr, srb->use_sg);
	}

	if (srb->cmnd[0] == READ_12) {
		// I don't think we'll ever see a READ_12 but support it anyway...
		//
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
			 ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("jumpshot_transport:  READ_12: read block 0x%04lx  count %ld\n", block, blocks);
		return jumpshot_read_data(us, info, block, blocks, ptr, srb->use_sg);
	}

	if (srb->cmnd[0] == WRITE_10) {
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[7]) << 8) | ((u32)(srb->cmnd[8]));

		US_DEBUGP("jumpshot_transport:  WRITE_10: write block 0x%04lx  count %ld\n", block, blocks);
		return jumpshot_write_data(us, info, block, blocks, ptr, srb->use_sg);
	}

	if (srb->cmnd[0] == WRITE_12) {
		// I don't think we'll ever see a WRITE_12 but support it anyway...
		//
		block = ((u32)(srb->cmnd[2]) << 24) | ((u32)(srb->cmnd[3]) << 16) |
			((u32)(srb->cmnd[4]) <<  8) | ((u32)(srb->cmnd[5]));

		blocks = ((u32)(srb->cmnd[6]) << 24) | ((u32)(srb->cmnd[7]) << 16) |
			 ((u32)(srb->cmnd[8]) <<  8) | ((u32)(srb->cmnd[9]));

		US_DEBUGP("jumpshot_transport:  WRITE_12: write block 0x%04lx  count %ld\n", block, blocks);
		return jumpshot_write_data(us, info, block, blocks, ptr, srb->use_sg);
	}


	if (srb->cmnd[0] == TEST_UNIT_READY) {
		US_DEBUGP("jumpshot_transport:  TEST_UNIT_READY.\n");
		return jumpshot_get_status(us);
	}

	if (srb->cmnd[0] == REQUEST_SENSE) {
		US_DEBUGP("jumpshot_transport:  REQUEST_SENSE.  Returning NO SENSE for now\n");

		ptr[0] = 0xF0;
		ptr[2] = info->sense_key;
		ptr[7] = 11;
		ptr[12] = info->sense_asc;
		ptr[13] = info->sense_ascq;

		return USB_STOR_TRANSPORT_GOOD;
	}

	if (srb->cmnd[0] == MODE_SENSE) {
		US_DEBUGP("jumpshot_transport:  MODE_SENSE_6 detected\n");
		return jumpshot_handle_mode_sense(us, srb, ptr, TRUE);
	}

	if (srb->cmnd[0] == MODE_SENSE_10) {
		US_DEBUGP("jumpshot_transport:  MODE_SENSE_10 detected\n");
		return jumpshot_handle_mode_sense(us, srb, ptr, FALSE);
	}
	
	if (srb->cmnd[0] == ALLOW_MEDIUM_REMOVAL) {
		// sure.  whatever.  not like we can stop the user from popping
		// the media out of the device (no locking doors, etc)
		//
		return USB_STOR_TRANSPORT_GOOD;
	}
	
	if (srb->cmnd[0] == START_STOP) {
		/* this is used by sd.c'check_scsidisk_media_change to detect
		   media change */
		US_DEBUGP("jumpshot_transport:  START_STOP.\n");
		/* the first jumpshot_id_device after a media change returns
		   an error (determined experimentally) */
		rc = jumpshot_id_device(us, info);
		if (rc == USB_STOR_TRANSPORT_GOOD) {
			info->sense_key = NO_SENSE;
			srb->result = SUCCESS;
		} else {
			info->sense_key = UNIT_ATTENTION;
			srb->result = SAM_STAT_CHECK_CONDITION;
		}
		return rc;
	}

	US_DEBUGP("jumpshot_transport:  Gah! Unknown command: %d (0x%x)\n",
		  srb->cmnd[0], srb->cmnd[0]);
	return USB_STOR_TRANSPORT_ERROR;
}
