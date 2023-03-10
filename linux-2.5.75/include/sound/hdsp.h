#ifndef __SOUND_HDSP_H
#define __SOUND_HDSP_H

/*
 *   Copyright (C) 2003 Thomas Charbonnel (thomas@undata.org)
 *    
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define HDSP_MATRIX_MIXER_SIZE 2048

typedef enum {
	Digiface,
	Multiface,
	Undefined,
} HDSP_IO_Type;

typedef struct _snd_hdsp_peak_rms hdsp_peak_rms_t;

struct _snd_hdsp_peak_rms {
	unsigned int playback_peaks[26];
	unsigned int input_peaks[26];
	unsigned int output_peaks[28];
	unsigned long long playback_rms[26];
	unsigned long long input_rms[26];
};

#define SNDRV_HDSP_IOCTL_GET_PEAK_RMS _IOR('H', 0x40, hdsp_peak_rms_t)

typedef struct _snd_hdsp_config_info hdsp_config_info_t;

struct _snd_hdsp_config_info {
	unsigned char pref_sync_ref;
	unsigned char wordclock_sync_check;
	unsigned char spdif_sync_check;
	unsigned char adatsync_sync_check;
	unsigned char adat_sync_check[3];
	unsigned char spdif_in;
	unsigned char spdif_out;
	unsigned char spdif_professional;
	unsigned char spdif_emphasis;
	unsigned char spdif_nonaudio;
	unsigned int spdif_sample_rate;
	unsigned int system_sample_rate;
	unsigned int autosync_sample_rate;
	unsigned char system_clock_mode;
	unsigned char clock_source;
	unsigned char autosync_ref;
	unsigned char line_out;
	unsigned char passthru; 
};

#define SNDRV_HDSP_IOCTL_GET_CONFIG_INFO _IOR('H', 0x41, hdsp_config_info_t)

typedef struct _snd_hdsp_firmware hdsp_firmware_t;

struct _snd_hdsp_firmware {
	unsigned long firmware_data[24413];
};

#define SNDRV_HDSP_IOCTL_UPLOAD_FIRMWARE _IOW('H', 0x42, hdsp_firmware_t)

typedef struct _snd_hdsp_version hdsp_version_t;

struct _snd_hdsp_version {
	HDSP_IO_Type io_type;
	unsigned short firmware_rev;
};

#define SNDRV_HDSP_IOCTL_GET_VERSION _IOR('H', 0x43, hdsp_version_t)

typedef struct _snd_hdsp_mixer hdsp_mixer_t;

struct _snd_hdsp_mixer {
	unsigned short matrix[HDSP_MATRIX_MIXER_SIZE];
};

#define SNDRV_HDSP_IOCTL_GET_MIXER _IOR('H', 0x44, hdsp_mixer_t)

#endif /* __SOUND_HDSP_H */
