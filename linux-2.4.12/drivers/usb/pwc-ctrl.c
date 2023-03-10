/* Driver for Philips webcam
   Functions that send various control messages to the webcam, including
   video modes.
   (C) 1999-2001 Nemosoft Unv. (webcam@smcc.demon.nl)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
   Changes
   2001/08/03  Alvarado   Added methods for changing white balance and 
                          red/green gains
 */

/* Control functions for the cam; brightness, contrast, video mode, etc. */

#ifdef __KERNEL__
#include <asm/uaccess.h> 
#endif
#include <asm/errno.h>
 
#include "pwc.h"
#include "pwc-ioctl.h"
#include "pwc-uncompress.h"

/* Request types: video */
#define SET_LUM_CTL			0x01
#define GET_LUM_CTL			0x02
#define SET_CHROM_CTL			0x03
#define GET_CHROM_CTL			0x04
#define SET_STATUS_CTL			0x05
#define GET_STATUS_CTL			0x06
#define SET_EP_STREAM_CTL		0x07
#define GET_EP_STREAM_CTL		0x08

/* Selectors for the Luminance controls [GS]ET_LUM_CTL */
#define AGC_MODE_FORMATTER			0x2000
#define PRESET_AGC_FORMATTER			0x2100
#define SHUTTER_MODE_FORMATTER			0x2200
#define PRESET_SHUTTER_FORMATTER		0x2300
#define PRESET_CONTOUR_FORMATTER		0x2400
#define AUTO_CONTOUR_FORMATTER			0x2500
#define BACK_LIGHT_COMPENSATION_FORMATTER	0x2600
#define CONTRAST_FORMATTER			0x2700
#define DYNAMIC_NOISE_CONTROL_FORMATTER		0x2800
#define FLICKERLESS_MODE_FORMATTER		0x2900
#define AE_CONTROL_SPEED			0x2A00
#define BRIGHTNESS_FORMATTER			0x2B00
#define GAMMA_FORMATTER				0x2C00

/* Selectors for the Chrominance controls [GS]ET_CHROM_CTL */
#define WB_MODE_FORMATTER			0x1000
#define AWB_CONTROL_SPEED_FORMATTER		0x1100
#define AWB_CONTROL_DELAY_FORMATTER		0x1200
#define PRESET_MANUAL_RED_GAIN_FORMATTER	0x1300
#define PRESET_MANUAL_BLUE_GAIN_FORMATTER	0x1400
#define COLOUR_MODE_FORMATTER			0x1500
#define SATURATION_MODE_FORMATTER1		0x1600
#define SATURATION_MODE_FORMATTER2		0x1700

/* Selectors for the Status controls [GS]ET_STATUS_CTL */
#define SAVE_USER_DEFAULTS_FORMATTER		0x0200
#define RESTORE_USER_DEFAULTS_FORMATTER		0x0300
#define RESTORE_FACTORY_DEFAULTS_FORMATTER	0x0400
#define READ_AGC_FORMATTER			0x0500
#define READ_SHUTTER_FORMATTER			0x0600
#define READ_RED_GAIN_FORMATTER			0x0700
#define READ_BLUE_GAIN_FORMATTER		0x0800
#define READ_RAW_Y_MEAN_FORMATTER		0x3100
#define SET_POWER_SAVE_MODE_FORMATTER		0x3200
#define MIRROR_IMAGE_FORMATTER			0x3300
#define LED_FORMATTER				0x3400

/* Formatters for the Video Endpoint controls [GS]ET_EP_STREAM_CTL */
#define VIDEO_OUTPUT_CONTROL_FORMATTER		0x0100

static char *size2name[PSZ_MAX] =
{
	"subQCIF",
	"QSIF",
	"QCIF",
	"SIF",
	"CIF",
	"VGA",
};  

/********/

/* Entries for the Nala (645/646) camera; the Nala doesn't have compression 
   preferences, so you either get compressed or non-compressed streams.
   
   An alternate value of 0 means this mode is not available at all.
 */

struct Nala_table_entry {
	char alternate;			/* USB alternate setting */
	int compressed;			/* Compressed yes/no */

	unsigned char mode[3];		/* precomputed mode table */
};

static struct Nala_table_entry Nala_table[PSZ_MAX][8] =
{
#include "pwc_nala.h"
};

/* This tables contains entries for the 675/680/690 (Timon) camera, with
   4 different qualities (no compression, low, medium, high).
   It lists the bandwidth requirements for said mode by its alternate interface 
   number. An alternate of 0 means that the mode is unavailable.
   
   There are 6 * 4 * 4 entries: 
     6 different resolutions subqcif, qsif, qcif, sif, cif, vga
     6 framerates: 5, 10, 15, 20, 25, 30
     4 compression modi: none, low, medium, high
     
   When an uncompressed mode is not available, the next available compressed mode 
   will be choosen (unless the decompressor is absent). Sometimes there are only
   1 or 2 compressed modes available; in that case entries are duplicated.
*/
struct Timon_table_entry 
{
	char alternate;			/* USB alternate interface */
	unsigned short packetsize;	/* Normal packet size */
	unsigned short bandlength;	/* Bandlength when decompressing */
	unsigned char mode[13];		/* precomputed mode settings for cam */
};

static struct Timon_table_entry Timon_table[PSZ_MAX][6][4] = 
{
#include "pwc_timon.h"
};

/* Entries for the Kiara (730/740/750) camera */

struct Kiara_table_entry
{
	char alternate;			/* USB alternate interface */
	unsigned short packetsize;	/* Normal packet size */
	unsigned short bandlength;	/* Bandlength when decompressing */
	unsigned char mode[12];		/* precomputed mode settings for cam */
};

static struct Kiara_table_entry Kiara_table[PSZ_MAX][6][4] =
{
#include "pwc_kiara.h"
};


/****************************************************************************/




#if PWC_DEBUG
void pwc_hexdump(void *p, int len)
{
	int i;
	unsigned char *s;
	char buf[100], *d;
	
	s = (unsigned char *)p;
	d = buf;
	*d = '\0';
	Debug("Doing hexdump @ %p, %d bytes.\n", p, len);
	for (i = 0; i < len; i++) {
		d += sprintf(d, "%02X ", *s++);
		if ((i & 0xF) == 0xF) {
			Debug("%s\n", buf);
			d = buf;
			*d = '\0';
		}
	}
	if ((i & 0xF) != 0)
		Debug("%s\n", buf);
}
#endif

static inline int send_video_command(struct usb_device *udev, int index, void *buf, int buflen)
{
	return usb_control_msg(udev,
		usb_sndctrlpipe(udev, 0),
		SET_EP_STREAM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		VIDEO_OUTPUT_CONTROL_FORMATTER,
		index,
		buf, buflen, HZ);
}



static inline int set_video_mode_Nala(struct pwc_device *pdev, int size, int frames)
{
	unsigned char buf[3];
	int ret, fps;
	struct Nala_table_entry *pEntry;
	int frames2frames[31] = 
	{ /* closest match of framerate */
	   0,  0,  0,  0,  4,  /*  0-4  */
	   5,  5,  7,  7, 10,  /*  5-9  */
          10, 10, 12, 12, 15,  /* 10-14 */
          15, 15, 15, 20, 20,  /* 15-19 */
          20, 20, 20, 24, 24,  /* 20-24 */
          24, 24, 24, 24, 24,  /* 25-29 */
          24                   /* 30    */
	};
	int frames2table[31] = 
	{ 0, 0, 0, 0, 0, /*  0-4  */
	  1, 1, 1, 2, 2, /*  5-9  */
	  3, 3, 4, 4, 4, /* 10-14 */
	  5, 5, 5, 5, 5, /* 15-19 */
	  6, 6, 6, 6, 7, /* 20-24 */
	  7, 7, 7, 7, 7, /* 25-29 */
	  7              /* 30    */
	};
	
	if (size < 0 || size > PSZ_CIF || frames < 4 || frames > 25)
		return -EINVAL;
	frames = frames2frames[frames];
	fps = frames2table[frames];
	pEntry = &Nala_table[size][fps];
	if (pEntry->alternate == 0)
		return -EINVAL;

	if (pEntry->compressed && pdev->decompressor == NULL)
		return -ENOENT; /* Not supported. */

	memcpy(buf, pEntry->mode, 3);	
	ret = send_video_command(pdev->udev, pdev->vendpoint, buf, 3);
	if (ret < 0)
		return ret;
	if (pEntry->compressed)
		pdev->decompressor->init(pdev->release, buf, pdev->decompress_data);
		
	/* Set various parameters */
	pdev->vframes = frames;
	pdev->vsize = size;
	pdev->valternate = pEntry->alternate;
	pdev->image = pwc_image_sizes[size];
	pdev->frame_size = (pdev->image.x * pdev->image.y * 3) / 2;
	if (pEntry->compressed) {
		if (pdev->release < 5) { /* 4 fold compression */
			pdev->vbandlength = 528;
			pdev->frame_size /= 4;
		}
		else {
			pdev->vbandlength = 704;
			pdev->frame_size /= 3;
		}
	}
	else
		pdev->vbandlength = 0;
	return 0;
}


static inline int set_video_mode_Timon(struct pwc_device *pdev, int size, int frames, int compression, int snapshot)
{
	unsigned char buf[13];
	struct Timon_table_entry *pChoose;
	int ret, fps;

	if (size >= PSZ_MAX || frames < 5 || frames > 30 || compression < 0 || compression > 3)
		return -EINVAL;
	if (size == PSZ_VGA && frames > 15)
		return -EINVAL;
	fps = (frames / 5) - 1;
	
	/* Find a supported framerate with progressively higher compression ratios
	   if the preferred ratio is not available.
	*/
	pChoose = NULL;
	if (pdev->decompressor == NULL) {
#if PWC_DEBUG	
		Debug("Trying to find uncompressed mode.\n");
#endif
		pChoose = &Timon_table[size][fps][0];
	}
	else {
		while (compression <= 3) {
			pChoose = &Timon_table[size][fps][compression];
			if (pChoose->alternate != 0)
				break;
			compression++;	
		}
	}
	if (pChoose == NULL || pChoose->alternate == 0)
		return -ENOENT; /* Not supported. */

	memcpy(buf, pChoose->mode, 13);
	if (snapshot)
		buf[0] |= 0x80;
	ret = send_video_command(pdev->udev, pdev->vendpoint, buf, 13);
	if (ret < 0)
		return ret;

	if (pChoose->bandlength > 0)
		pdev->decompressor->init(pdev->release, buf, pdev->decompress_data);
	
	/* Set various parameters */
	pdev->vframes = frames;
	pdev->vsize = size;
	pdev->vsnapshot = snapshot;
	pdev->valternate = pChoose->alternate;
	pdev->image = pwc_image_sizes[size];
	pdev->vbandlength = pChoose->bandlength;
	if (pChoose->bandlength > 0) 
		pdev->frame_size = (pChoose->bandlength * pdev->image.y) / 4;
	else
		pdev->frame_size = (pdev->image.x * pdev->image.y * 12) / 8;
	return 0;
}


static inline int set_video_mode_Kiara(struct pwc_device *pdev, int size, int frames, int compression, int snapshot)
{
	struct Kiara_table_entry *pChoose;
	int fps, ret;
	unsigned char buf[12];
	
	if (size >= PSZ_MAX || frames < 5 || frames > 30 || compression < 0 || compression > 3)
		return -EINVAL;
	if (size == PSZ_VGA && frames > 15)
		return -EINVAL;
	fps = (frames / 5) - 1;
	
	/* Find a supported framerate with progressively higher compression ratios
	   if the preferred ratio is not available.
	*/
	pChoose = NULL;
	if (pdev->decompressor == NULL) {
#if PWC_DEBUG	
		Debug("Trying to find uncompressed mode.\n");
#endif		
		pChoose = &Kiara_table[size][fps][0];
	}
	else {
		while (compression <= 3) {
			pChoose = &Kiara_table[size][fps][compression];
			if (pChoose->alternate != 0)
				break;
			compression++;	
		}
	}
	if (pChoose == NULL || pChoose->alternate == 0)
		return -ENOENT; /* Not supported. */

	/* usb_control_msg won't take staticly allocated arrays as argument?? */
	memcpy(buf, pChoose->mode, 12);
	if (snapshot)
		buf[0] |= 0x80;

	/* Firmware bug: video endpoint is 5, but commands are sent to endpoint 4 */
	ret = send_video_command(pdev->udev, 4 /* pdev->vendpoint */, buf, 12);
	if (ret < 0)
		return ret;

	if (pChoose->bandlength > 0)
		pdev->decompressor->init(pdev->release, buf, pdev->decompress_data);
		
	/* All set and go */
	pdev->vframes = frames;
	pdev->vsize = size;
	pdev->vsnapshot = snapshot;
	pdev->valternate = pChoose->alternate;
	pdev->image = pwc_image_sizes[size];
	pdev->vbandlength = pChoose->bandlength;
	if (pChoose->bandlength > 0)
		pdev->frame_size = (pChoose->bandlength * pdev->image.y) / 4;
	else 
		pdev->frame_size = (pdev->image.x * pdev->image.y * 12) / 8;
	pdev->frame_size += (pdev->frame_header_size + pdev->frame_trailer_size);
	return 0;
}


/**
   @pdev: device structure
   @width: viewport width
   @height: viewport height
   @frame: framerate, in fps
   @compression: preferred compression ratio
   @snapshot: snapshot mode or streaming
 */
int pwc_set_video_mode(struct pwc_device *pdev, int width, int height, int frames, int compression, int snapshot)
{
	int ret, size;
	
	size = pwc_decode_size(pdev, width, height);
	if (size < 0) {
		Debug("Could not find suitable size.\n");
		return -ERANGE;
	}
	ret = -EINVAL;	
	switch(pdev->type) {
	case 645:
	case 646:
		ret = set_video_mode_Nala(pdev, size, frames);
		break;

	case 675:
	case 680:
	case 690:
		ret = set_video_mode_Timon(pdev, size, frames, compression, snapshot);
		break;
		
	case 730:
	case 740:
	case 750:
		ret = set_video_mode_Kiara(pdev, size, frames, compression, snapshot);
		break;
	}
	if (ret < 0) {
		if (ret == -ENOENT)
			Info("Video mode %s@%d fps is only supported with the decompressor module (pwcx).\n", size2name[size], frames);
		else {
			Err("Failed to set video mode %s@%d fps; return code = %d\n", size2name[size], frames, ret);
			return ret;
		}
	}
	pdev->view.x = width;
	pdev->view.y = height;
	pwc_set_image_buffer_size(pdev);
	Trace(TRACE_SIZE, "Set viewport to %dx%d, image size is %dx%d, palette = %d.\n", width, height, pwc_image_sizes[size].x, pwc_image_sizes[size].y, pdev->vpalette);
	return 0;
}


void pwc_set_image_buffer_size(struct pwc_device *pdev)
{
	int factor, i, filler = 0;

	switch(pdev->vpalette) {
	case VIDEO_PALETTE_RGB32 | 0x80:
	case VIDEO_PALETTE_RGB32:
		factor = 16;
		filler = 0;
		break;
	case VIDEO_PALETTE_RGB24 | 0x80:
	case VIDEO_PALETTE_RGB24:
		factor = 12;
		filler = 0;
		break;
	case VIDEO_PALETTE_YUYV:
	case VIDEO_PALETTE_YUV422:
		factor = 8;
		filler = 128;
		break;
	case VIDEO_PALETTE_YUV420:
	case VIDEO_PALETTE_YUV420P:
		factor = 6;
		filler = 128;
		break;
#if PWC_DEBUG		
	case VIDEO_PALETTE_RAW:
		pdev->image.size = pdev->frame_size;
		pdev->view.size = pdev->frame_size;
		return;
		break;
#endif	
	default:
		factor = 0;
		break;
	}

	/* Set sizes in bytes */
	pdev->image.size = pdev->image.x * pdev->image.y * factor / 4;
	pdev->view.size  = pdev->view.x  * pdev->view.y  * factor / 4;

	/* Align offset, or you'll get some very weird results in
	   YUV420 mode... x must be multiple of 4 (to get the Y's in 
	   place), and y even (or you'll mixup U & V). This is less of a
	   problem for YUV420P.
	 */
	pdev->offset.x = ((pdev->view.x - pdev->image.x) / 2) & 0xFFFC;
	pdev->offset.y = ((pdev->view.y - pdev->image.y) / 2) & 0xFFFE;
	
	/* Fill buffers with gray or black */
	for (i = 0; i < MAX_IMAGES; i++) {
		if (pdev->image_ptr[i] != NULL)
			memset(pdev->image_ptr[i], filler, pdev->view.size);
	}
}


#ifdef __KERNEL__
/* BRIGHTNESS */

int pwc_get_brightness(struct pwc_device *pdev)
{
	char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_LUM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		BRIGHTNESS_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	if (ret < 0)
		return ret;
	return buf << 9;
}

int pwc_set_brightness(struct pwc_device *pdev, int value)
{
	char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;
	buf = (value >> 9) & 0x7f;
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_LUM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		BRIGHTNESS_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

/* CONTRAST */

int pwc_get_contrast(struct pwc_device *pdev)
{
	char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_LUM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		CONTRAST_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	if (ret < 0)
		return ret;
	return buf << 10;
}

int pwc_set_contrast(struct pwc_device *pdev, int value)
{
	char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;
	buf = (value >> 10) & 0x3f;
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_LUM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		CONTRAST_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

/* GAMMA */

int pwc_get_gamma(struct pwc_device *pdev)
{
	char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_LUM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		GAMMA_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	if (ret < 0)
		return ret;
	return buf << 11;
}

int pwc_set_gamma(struct pwc_device *pdev, int value)
{
	char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;
	buf = (value >> 11) & 0x1f;
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_LUM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		GAMMA_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}


/* SATURATION */

int pwc_get_saturation(struct pwc_device *pdev)
{
	char buf;
	int ret;

	if (pdev->type < 675)
		return -1;
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_CHROM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		pdev->type < 730 ? SATURATION_MODE_FORMATTER2 : SATURATION_MODE_FORMATTER1,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	if (ret < 0)
		return ret;
	return 32768 + buf * 327;
}

int pwc_set_saturation(struct pwc_device *pdev, int value)
{
	char buf;

	if (pdev->type < 675)
		return -EINVAL;
	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;
	/* saturation ranges from -100 to +100 */
	buf = (value - 32768) / 327;
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_CHROM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		pdev->type < 730 ? SATURATION_MODE_FORMATTER2 : SATURATION_MODE_FORMATTER1,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

/* AGC */

static inline int pwc_set_agc(struct pwc_device *pdev, int mode, int value)
{
	char buf;
	int ret;
	
	if (mode)
		buf = 0x0; /* auto */
	else
		buf = 0xff; /* fixed */

	ret = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_LUM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		AGC_MODE_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	
	if (!mode && ret >= 0) {
		if (value < 0)
			value = 0;
		if (value > 0xffff)
			value = 0xffff;
		buf = (value >> 10) & 0x3F;
		ret = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
			SET_LUM_CTL,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			PRESET_AGC_FORMATTER,
			pdev->vcinterface,
			&buf, 1, HZ / 2);
	}
	if (ret < 0)
		return ret;
	return 0;
}

static inline int pwc_get_agc(struct pwc_device *pdev, int *value)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_LUM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		AGC_MODE_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	if (ret < 0)
		return ret;

	if (buf != 0) { /* fixed */
		ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
			GET_LUM_CTL,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			PRESET_AGC_FORMATTER,
			pdev->vcinterface,
			&buf, 1, HZ / 2);
		if (ret < 0)
			return ret;
		if (buf > 0x3F)
			buf = 0x3F;
		*value = (buf << 10);		
	}
	else { /* auto */
		ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
			GET_STATUS_CTL,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			READ_AGC_FORMATTER,
			pdev->vcinterface,
			&buf, 1, HZ / 2);
		if (ret < 0)
			return ret;
		/* Gah... this value ranges from 0x00 ... 0x9F */
		if (buf > 0x9F)
			buf = 0x9F;
		*value = -(48 + buf * 409);
	}

	return 0;
}

static inline int pwc_set_shutter_speed(struct pwc_device *pdev, int mode, int value)
{
	char buf[2];
	int speed, ret;


	if (mode)
		buf[0] = 0x0;	/* auto */
	else
		buf[0] = 0xff; /* fixed */
	
	ret = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_LUM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		SHUTTER_MODE_FORMATTER,
		pdev->vcinterface,
		buf, 1, HZ / 2);

	if (!mode && ret >= 0) {
		if (value < 0)
			value = 0;
		if (value > 0xffff)
			value = 0xffff;
		switch(pdev->type) {
		case 675:
		case 680:
		case 690:
			/* speed ranges from 0x0 to 0x290 (656) */
			speed = (value / 100);
			buf[1] = speed >> 8;
			buf[0] = speed & 0xff;
			break;
		case 730:
		case 740:
		case 750:
			/* speed seems to range from 0x0 to 0xff */
			buf[1] = 0;
			buf[0] = value >> 8;
			break;
		}

		ret = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
			SET_LUM_CTL,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			PRESET_SHUTTER_FORMATTER,
			pdev->vcinterface,
			&buf, 2, HZ / 2);
	}
	return ret;
}	


/* POWER */

int pwc_camera_power(struct pwc_device *pdev, int power)
{
	char buf;

	if (pdev->type < 675 || pdev->release < 6)
		return 0;	/* Not supported by Nala or Timon < release 6 */

	if (power)
		buf = 0x00; /* active */
	else
		buf = 0xFF; /* power save */
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_STATUS_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		SET_POWER_SAVE_MODE_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}



/* private calls */

static inline int pwc_restore_user(struct pwc_device *pdev)
{
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_STATUS_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		RESTORE_USER_DEFAULTS_FORMATTER,
		pdev->vcinterface,
		NULL, 0, HZ / 2);
}

static inline int pwc_save_user(struct pwc_device *pdev)
{
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_STATUS_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		SAVE_USER_DEFAULTS_FORMATTER,
		pdev->vcinterface,
		NULL, 0, HZ / 2);
}

static inline int pwc_restore_factory(struct pwc_device *pdev)
{
	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_STATUS_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		RESTORE_FACTORY_DEFAULTS_FORMATTER,
		pdev->vcinterface,
		NULL, 0, HZ / 2);
}

 /* ************************************************* */
 /* Patch by Alvarado: (not in the original version   */

 /*
  * the camera recognizes modes from 0 to 4:
  *
  * 00: indoor (incandescant lighting)
  * 01: outdoor (sunlight)
  * 02: fluorescent lighting
  * 03: manual
  * 04: auto
  */ 
static inline int pwc_set_awb(struct pwc_device *pdev, int mode)
{
	char buf;
	int ret;
	
	if (mode < 0)
	    mode = 0;
	
	if (mode > 4)
	    mode = 4;
	
	buf = mode & 0x07; /* just the lowest three bits */
	
	ret = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_CHROM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		WB_MODE_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
	
	if (ret < 0)
		return ret;
	return 0;
}

static inline int pwc_get_awb(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		GET_CHROM_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		WB_MODE_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0) 
		return ret;
	return buf;
}

static inline int pwc_set_red_gain(struct pwc_device *pdev, int value)
{
        unsigned char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;

	/* only the msb are considered */
	buf = value >> 8;

	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_CHROM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		PRESET_MANUAL_RED_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

static inline int pwc_get_red_gain(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
 	        GET_STATUS_CTL, 
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	        PRESET_MANUAL_RED_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0)
	    return ret;
	
	return (buf << 8);
}


static inline int pwc_set_blue_gain(struct pwc_device *pdev, int value)
{
	unsigned char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;

	/* linear mapping of 0..0xffff to -0x80..0x7f */
	buf = (value >> 8);

	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_CHROM_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		PRESET_MANUAL_BLUE_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

static inline int pwc_get_blue_gain(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
   	        GET_STATUS_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		PRESET_MANUAL_BLUE_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0)
	    return ret;
	
	return (buf << 8);
}

/* The following two functions are different, since they only read the
   internal red/blue gains, which may be different from the manual 
   gains set or read above.
 */   
static inline int pwc_read_red_gain(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
 	        GET_STATUS_CTL, 
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
	        READ_RED_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0)
	    return ret;
	
	return (buf << 8);
}
static inline int pwc_read_blue_gain(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
   	        GET_STATUS_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		READ_BLUE_GAIN_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0)
	    return ret;
	
	return (buf << 8);
}

/* still unused (it doesn't work yet...) */
static inline int pwc_set_led(struct pwc_device *pdev, int value)
{
	unsigned char buf;

	if (value < 0)
		value = 0;
	if (value > 0xffff)
		value = 0xffff;

	buf = (value >> 8);

	return usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		SET_STATUS_CTL,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		LED_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);
}

/* still unused (it doesn't work yet...) */
static inline int pwc_get_led(struct pwc_device *pdev)
{
	unsigned char buf;
	int ret;
	
	ret = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
   	        GET_STATUS_CTL,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		LED_FORMATTER,
		pdev->vcinterface,
		&buf, 1, HZ / 2);

	if (ret < 0)
	    return ret;
	
	return (buf << 8);
}

 /* End of Add-Ons                                    */
 /* ************************************************* */

int pwc_ioctl(struct pwc_device *pdev, unsigned int cmd, void *arg)
{
	switch(cmd) {
	case VIDIOCPWCRUSER:
	{
		if (pwc_restore_user(pdev))
			return -EINVAL;
		break;
	}
	
	case VIDIOCPWCSUSER:
	{
		if (pwc_save_user(pdev))
			return -EINVAL;
		break;
	}
		
	case VIDIOCPWCFACTORY:
	{
		if (pwc_restore_factory(pdev))
			return -EINVAL;
		break;
	}
	
	case VIDIOCPWCSCQUAL:
	{	
		int qual, ret;

		if (copy_from_user(&qual, arg, sizeof(int)))
			return -EFAULT;
			
		if (qual < 0 || qual > 3)
			return -EINVAL;
		ret = pwc_try_video_mode(pdev, pdev->view.x, pdev->view.y, pdev->vframes, qual, pdev->vsnapshot);
		if (ret < 0)
			return ret;
		pdev->vcompression = qual;
		break;
	}
	
	case VIDIOCPWCGCQUAL:
	{
		if (copy_to_user(arg, &pdev->vcompression, sizeof(int)))
			return -EFAULT;
		break;
	}

	case VIDIOCPWCSAGC:
	{
		int agc;
		
		if (copy_from_user(&agc, arg, sizeof(agc)))
			return -EFAULT;	
		else {
			if (pwc_set_agc(pdev, agc < 0 ? 1 : 0, agc))
				return -EINVAL;
		}
		break;
	}
	
	case VIDIOCPWCGAGC:
	{
		int agc;
		
		if (pwc_get_agc(pdev, &agc))
			return -EINVAL;
		if (copy_to_user(arg, &agc, sizeof(agc)))
			return -EFAULT;
		break;
	}
	
	case VIDIOCPWCSSHUTTER:
	{
		int shutter_speed, ret;

		if (copy_from_user(&shutter_speed, arg, sizeof(shutter_speed)))
			return -EFAULT;
		else {
			ret = pwc_set_shutter_speed(pdev, shutter_speed < 0 ? 1 : 0, shutter_speed);
			if (ret < 0)
				return ret;
		}
		break;
	}
	

 /* ************************************************* */
 /* Begin of Add-Ons for color compensation           */

        case VIDIOCPWCSAWB:
	{
		struct pwc_whitebalance wb;
		int ret;
		
		if (copy_from_user(&wb, arg, sizeof(wb)))
			return -EFAULT;
	
		ret = pwc_set_awb(pdev, wb.mode);
		if (ret >= 0 && wb.mode == PWC_WB_MANUAL) {
			pwc_set_red_gain(pdev, wb.manual_red);
			pwc_set_blue_gain(pdev, wb.manual_blue);
		}
		break;
	}

	case VIDIOCPWCGAWB:
	{
		struct pwc_whitebalance wb;
		
		memset(&wb, 0, sizeof(wb));
		wb.mode = pwc_get_awb(pdev);
		if (wb.mode < 0)
			return -EINVAL;
		wb.manual_red = pwc_get_red_gain(pdev);
		wb.manual_blue = pwc_get_blue_gain(pdev);
		if (wb.mode == PWC_WB_AUTO) {
			wb.read_red = pwc_read_red_gain(pdev);
			wb.read_blue = pwc_read_blue_gain(pdev);
		}
		break;
	}

        case VIDIOCPWCSLED:
	{
	    int led, ret;
	    if (copy_from_user(&led,arg,sizeof(led)))
		return -EFAULT;
	    else {
		/* ret = pwc_set_led(pdev, led); */
		ret = 0;
		if (ret<0)
		    return ret;
	    }
	    break;
	}



	case VIDIOCPWCGLED:
	{
		int led;
		
		led = pwc_get_led(pdev); 
		if (led < 0)
			return -EINVAL;
		if (copy_to_user(arg, &led, sizeof(led)))
			return -EFAULT;
		break;
	}

 /* End of Add-Ons                                    */
 /* ************************************************* */

	default:
		return -ENOIOCTLCMD;
		break;
	}
	return 0;
}

#endif




