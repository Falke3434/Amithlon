#ifndef __SOUND_AC97_CODEC_H
#define __SOUND_AC97_CODEC_H

/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *  Universal interface for Audio Codec '97
 *
 *  For more details look to AC '97 component specification revision 2.1
 *  by Intel Corporation (http://developer.intel.com).
 *
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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/bitops.h>
#include "control.h"
#include "info.h"

/*
 *  AC'97 codec registers
 */

#define AC97_RESET		0x00	/* Reset */
#define AC97_MASTER		0x02	/* Master Volume */
#define AC97_HEADPHONE		0x04	/* Headphone Volume (optional) */
#define AC97_MASTER_MONO	0x06	/* Master Volume Mono (optional) */
#define AC97_MASTER_TONE	0x08	/* Master Tone (Bass & Treble) (optional) */
#define AC97_PC_BEEP		0x0a	/* PC Beep Volume (optinal) */
#define AC97_PHONE		0x0c	/* Phone Volume (optional) */
#define AC97_MIC		0x0e	/* MIC Volume */
#define AC97_LINE		0x10	/* Line In Volume */
#define AC97_CD			0x12	/* CD Volume */
#define AC97_VIDEO		0x14	/* Video Volume (optional) */
#define AC97_AUX		0x16	/* AUX Volume (optional) */
#define AC97_PCM		0x18	/* PCM Volume */
#define AC97_REC_SEL		0x1a	/* Record Select */
#define AC97_REC_GAIN		0x1c	/* Record Gain */
#define AC97_REC_GAIN_MIC	0x1e	/* Record Gain MIC (optional) */
#define AC97_GENERAL_PURPOSE	0x20	/* General Purpose (optional) */
#define AC97_3D_CONTROL		0x22	/* 3D Control (optional) */
#define AC97_RESERVED		0x24	/* Reserved */
#define AC97_POWERDOWN		0x26	/* Powerdown control / status */
/* range 0x28-0x3a - AUDIO AC'97 2.0 extensions */
#define AC97_EXTENDED_ID	0x28	/* Extended Audio ID */
#define AC97_EXTENDED_STATUS	0x2a	/* Extended Audio Status and Control */
#define AC97_PCM_FRONT_DAC_RATE 0x2c	/* PCM Front DAC Rate */
#define AC97_PCM_SURR_DAC_RATE	0x2e	/* PCM Surround DAC Rate */
#define AC97_PCM_LFE_DAC_RATE	0x30	/* PCM LFE DAC Rate */
#define AC97_PCM_LR_ADC_RATE	0x32	/* PCM LR ADC Rate */
#define AC97_PCM_MIC_ADC_RATE	0x34	/* PCM MIC ADC Rate */
#define AC97_CENTER_LFE_MASTER	0x36	/* Center + LFE Master Volume */
#define AC97_SURROUND_MASTER	0x38	/* Surround (Rear) Master Volume */
#define AC97_SPDIF		0x3a	/* S/PDIF control */
/* range 0x3c-0x58 - MODEM */
#define AC97_EXTENDED_MID	0x3c	/* Extended Modem ID */
#define AC97_EXTENDED_MSTATUS	0x3e	/* Extended Modem Status and Control */
#define AC97_LINE1_RATE		0x40	/* Line1 DAC/ADC Rate */
#define AC97_LINE2_RATE		0x42	/* Line2 DAC/ADC Rate */
#define AC97_HANDSET_RATE	0x44	/* Handset DAC/ADC Rate */
#define AC97_LINE1_LEVEL	0x46	/* Line1 DAC/ADC Level */
#define AC97_LINE2_LEVEL	0x48	/* Line2 DAC/ADC Level */
#define AC97_HANDSET_LEVEL	0x4a	/* Handset DAC/ADC Level */
#define AC97_GPIO_CFG		0x4c	/* GPIO Configuration */
#define AC97_GPIO_POLARITY	0x4e	/* GPIO Pin Polarity/Type, 0=low, 1=high active */
#define AC97_GPIO_STICKY	0x50	/* GPIO Pin Sticky, 0=not, 1=sticky */
#define AC97_GPIO_WAKEUP	0x52	/* GPIO Pin Wakeup, 0=no int, 1=yes int */
#define AC97_GPIO_STATUS	0x54	/* GPIO Pin Status, slot 12 */
#define AC97_MISC_AFE		0x56	/* Miscellaneous Modem AFE Status and Control */
/* range 0x5a-0x7b - Vendor Specific */
#define AC97_VENDOR_ID1		0x7c	/* Vendor ID1 */
#define AC97_VENDOR_ID2		0x7e	/* Vendor ID2 / revision */

/* basic capabilities (reset register) */
#define AC97_BC_DEDICATED_MIC	0x0001	/* Dedicated Mic PCM In Channel */
#define AC97_BC_RESERVED1	0x0002	/* Reserved (was Modem Line Codec support) */
#define AC97_BC_BASS_TREBLE	0x0004	/* Bass & Treble Control */
#define AC97_BC_SIM_STEREO	0x0008	/* Simulated stereo */
#define AC97_BC_HEADPHONE	0x0010	/* Headphone Out Support */
#define AC97_BC_LOUDNESS	0x0020	/* Loudness (bass boost) Support */
#define AC97_BC_16BIT_DAC	0x0000	/* 16-bit DAC resolution */
#define AC97_BC_18BIT_DAC	0x0040	/* 18-bit DAC resolution */
#define AC97_BC_20BIT_DAC	0x0080	/* 20-bit DAC resolution */
#define AC97_BC_DAC_MASK	0x00c0
#define AC97_BC_16BIT_ADC	0x0000	/* 16-bit ADC resolution */
#define AC97_BC_18BIT_ADC	0x0100	/* 18-bit ADC resolution */
#define AC97_BC_20BIT_ADC	0x0200	/* 20-bit ADC resolution */
#define AC97_BC_ADC_MASK	0x0300

/* extended audio ID bit defines */
#define AC97_EI_VRA		0x0001	/* Variable bit rate supported */
#define AC97_EI_DRA		0x0002	/* Double rate supported */
#define AC97_EI_SPDIF		0x0004	/* S/PDIF out supported */
#define AC97_EI_VRM		0x0008	/* Variable bit rate supported for MIC */
#define AC97_EI_DACS_SLOT_MASK	0x0030	/* DACs slot assignment */
#define AC97_EI_DACS_SLOT_SHIFT	4
#define AC97_EI_CDAC		0x0040	/* PCM Center DAC available */
#define AC97_EI_SDAC		0x0080	/* PCM Surround DACs available */
#define AC97_EI_LDAC		0x0100	/* PCM LFE DAC available */
#define AC97_EI_AMAP		0x0200	/* indicates optional slot/DAC mapping based on codec ID */
#define AC97_EI_REV_MASK	0x0c00	/* AC'97 revision mask */
#define AC97_EI_REV_22		0x0400	/* AC'97 revision 2.2 */
#define AC97_EI_REV_SHIFT	10
#define AC97_EI_ADDR_MASK	0xc000	/* physical codec ID (address) */
#define AC97_EI_ADDR_SHIFT	14

/* extended audio status and control bit defines */
#define AC97_EA_VRA		0x0001	/* Variable bit rate enable bit */
#define AC97_EA_DRA		0x0002	/* Double-rate audio enable bit */
#define AC97_EA_SPDIF		0x0004	/* S/PDIF out enable bit */
#define AC97_EA_VRM		0x0008	/* Variable bit rate for MIC enable bit */
#define AC97_EA_SPSA_SLOT_MASK	0x0030	/* Mask for slot assignment bits */
#define AC97_EA_SPSA_SLOT_SHIFT 4
#define AC97_EA_SPSA_3_4	0x0000	/* Slot assigned to 3 & 4 */
#define AC97_EA_SPSA_7_8	0x0010	/* Slot assigned to 7 & 8 */
#define AC97_EA_SPSA_6_9	0x0020	/* Slot assigned to 6 & 9 */
#define AC97_EA_SPSA_10_11	0x0030	/* Slot assigned to 10 & 11 */
#define AC97_EA_CDAC		0x0040	/* PCM Center DAC is ready (Read only) */
#define AC97_EA_SDAC		0x0080	/* PCM Surround DACs are ready (Read only) */
#define AC97_EA_LDAC		0x0100	/* PCM LFE DAC is ready (Read only) */
#define AC97_EA_MDAC		0x0200	/* MIC ADC is ready (Read only) */
#define AC97_EA_SPCV		0x0400	/* S/PDIF configuration valid (Read only) */
#define AC97_EA_PRI		0x0800	/* Turns the PCM Center DAC off */
#define AC97_EA_PRJ		0x1000	/* Turns the PCM Surround DACs off */
#define AC97_EA_PRK		0x2000	/* Turns the PCM LFE DAC off */
#define AC97_EA_PRL		0x4000	/* Turns the MIC ADC off */

/* S/PDIF control bit defines */
#define AC97_SC_PRO		0x0001	/* Professional status */
#define AC97_SC_NAUDIO		0x0002	/* Non audio stream */
#define AC97_SC_COPY		0x0004	/* Copyright status */
#define AC97_SC_PRE		0x0008	/* Preemphasis status */
#define AC97_SC_CC_MASK		0x07f0	/* Category Code mask */
#define AC97_SC_CC_SHIFT	4
#define AC97_SC_L		0x0800	/* Generation Level status */
#define AC97_SC_SPSR_MASK	0x3000	/* S/PDIF Sample Rate bits */
#define AC97_SC_SPSR_SHIFT	12
#define AC97_SC_SPSR_44K	0x0000	/* Use 44.1kHz Sample rate */
#define AC97_SC_SPSR_48K	0x2000	/* Use 48kHz Sample rate */
#define AC97_SC_SPSR_32K	0x3000	/* Use 32kHz Sample rate */
#define AC97_SC_DRS		0x4000	/* Double Rate S/PDIF */
#define AC97_SC_V		0x8000	/* Validity status */

/* extended modem ID bit defines */
#define AC97_MEI_LINE1		0x0001	/* Line1 present */
#define AC97_MEI_LINE2		0x0002	/* Line2 present */
#define AC97_MEI_HANDSET	0x0004	/* Handset present */
#define AC97_MEI_CID1		0x0008	/* caller ID decode for Line1 is supported */
#define AC97_MEI_CID2		0x0010	/* caller ID decode for Line2 is supported */
#define AC97_MEI_ADDR_MASK	0xc000	/* physical codec ID (address) */
#define AC97_MEI_ADDR_SHIFT	14

/* extended modem status and control bit defines */
#define AC97_MEA_GPIO		0x0001	/* GPIO is ready (ro) */
#define AC97_MEA_MREF		0x0002	/* Vref is up to nominal level (ro) */
#define AC97_MEA_ADC1		0x0004	/* ADC1 operational (ro) */
#define AC97_MEA_DAC1		0x0008	/* DAC1 operational (ro) */
#define AC97_MEA_ADC2		0x0010	/* ADC2 operational (ro) */
#define AC97_MEA_DAC2		0x0020	/* DAC2 operational (ro) */
#define AC97_MEA_HADC		0x0040	/* HADC operational (ro) */
#define AC97_MEA_HDAC		0x0080	/* HDAC operational (ro) */
#define AC97_MEA_PRA		0x0100	/* GPIO power down (high) */
#define AC97_MEA_PRB		0x0200	/* reserved */
#define AC97_MEA_PRC		0x0400	/* ADC1 power down (high) */
#define AC97_MEA_PRD		0x0800	/* DAC1 power down (high) */
#define AC97_MEA_PRE		0x1000	/* ADC2 power down (high) */
#define AC97_MEA_PRF		0x2000	/* DAC2 power down (high) */
#define AC97_MEA_PRG		0x4000	/* HADC power down (high) */
#define AC97_MEA_PRH		0x8000	/* HDAC power down (high) */

/* specific - SigmaTel */
#define AC97_SIGMATEL_ANALOG	0x6c	/* Analog Special */
#define AC97_SIGMATEL_DAC2INVERT 0x6e
#define AC97_SIGMATEL_BIAS1	0x70
#define AC97_SIGMATEL_BIAS2	0x72
#define AC97_SIGMATEL_MULTICHN	0x74	/* Multi-Channel programming */
#define AC97_SIGMATEL_CIC1	0x76
#define AC97_SIGMATEL_CIC2	0x78

/* specific - Analog Devices */
#define AC97_AD_TEST		0x5a	/* test register */
#define AC97_AD_CODEC_CFG	0x70	/* codec configuration */
#define AC97_AD_JACK_SPDIF	0x72	/* Jack Sense & S/PDIF */
#define AC97_AD_SERIAL_CFG	0x74	/* Serial Configuration */
#define AC97_AD_MISC		0x76	/* Misc Control Bits */

/* specific - Cirrus Logic */
#define AC97_CSR_ACMODE		0x5e	/* AC Mode Register */
#define AC97_CSR_MISC_CRYSTAL	0x60	/* Misc Crystal Control */
#define AC97_CSR_SPDIF		0x68	/* S/PDIF Register */
#define AC97_CSR_SERIAL		0x6a	/* Serial Port Control */
#define AC97_CSR_SPECF_ADDR	0x6c	/* Special Feature Address */
#define AC97_CSR_SPECF_DATA	0x6e	/* Special Feature Data */
#define AC97_CSR_BDI_STATUS	0x7a	/* BDI Status */

/* specific - Conexant */
#define AC97_CXR_AUDIO_MISC	0x5c
#define AC97_CXR_SPDIFEN	(1<<3)
#define AC97_CXR_COPYRGT	(1<<2)
#define AC97_CXR_SPDIF_MASK	(3<<0)
#define AC97_CXR_SPDIF_PCM	0x0
#define AC97_CXR_SPDIF_AC3	0x2

/* specific - ALC */
#define AC97_ALC650_SURR_DAC_VOL	0x64
#define AC97_ALC650_LFE_DAC_VOL		0x66
#define AC97_ALC650_MULTICH	0x6a
#define AC97_ALC650_CLOCK	0x7a

/* specific - Yamaha YMF753 */
#define AC97_YMF753_DIT_CTRL2	0x66	/* DIT Control 2 */
#define AC97_YMF753_3D_MODE_SEL	0x68	/* 3D Mode Select */

/* specific - C-Media */
#define AC97_CM9738_VENDOR_CTRL	0x5a
#define AC97_CM9739_MULTI_CHAN	0x64
#define AC97_CM9739_SPDIF_IN_STATUS	0x68 /* 32bit */
#define AC97_CM9739_SPDIF_CTRL	0x6c


/* ac97->scaps */
#define AC97_SCAP_AUDIO		(1<<0)	/* audio AC'97 codec */
#define AC97_SCAP_MODEM		(1<<1)	/* modem AC'97 codec */
#define AC97_SCAP_SURROUND_DAC	(1<<2)	/* surround L&R DACs are present */
#define AC97_SCAP_CENTER_LFE_DAC (1<<3)	/* center and LFE DACs are present */

/* ac97->flags */
#define AC97_HAS_PC_BEEP	(1<<0)	/* force PC Speaker usage */
#define AC97_AD_MULTI		(1<<1)	/* Analog Devices - multi codecs */
#define AC97_CS_SPDIF		(1<<2)	/* Cirrus Logic uses funky SPDIF */
#define AC97_CX_SPDIF		(1<<3)	/* Conexant's spdif interface */

/* rates indexes */
#define AC97_RATES_FRONT_DAC	0
#define AC97_RATES_SURR_DAC	1
#define AC97_RATES_LFE_DAC	2
#define AC97_RATES_ADC		3
#define AC97_RATES_MIC_ADC	4
#define AC97_RATES_SPDIF	5

/*
 *
 */

typedef struct _snd_ac97 ac97_t;

struct _snd_ac97 {
	void (*reset) (ac97_t *ac97);
	void (*write) (ac97_t *ac97, unsigned short reg, unsigned short val);
	unsigned short (*read) (ac97_t *ac97, unsigned short reg);
	void (*wait) (ac97_t *ac97);
	void (*init) (ac97_t *ac97);
	void *private_data;
	void (*private_free) (ac97_t *ac97);
	/* --- */
	snd_card_t *card;
	struct pci_dev *pci;	/* assigned PCI device - used for quirks */
	unsigned short subsystem_vendor;
	unsigned short subsystem_device;
	spinlock_t reg_lock;
	unsigned short num;	/* number of codec: 0 = primary, 1 = secondary */
	unsigned short addr;	/* physical address of codec [0-3] */
	unsigned int id;	/* identification of codec */
	unsigned short caps;	/* capabilities (register 0) */
	unsigned short ext_id;	/* extended feature identification (register 28) */
	unsigned short ext_mid;	/* extended modem ID (register 3C) */
	unsigned int scaps;	/* driver capabilities */
	unsigned int flags;	/* specific code */
	unsigned int clock;	/* AC'97 clock (usually 48000Hz) */
	unsigned int rates[6];	/* see AC97_RATES_* defines */
	unsigned int spdif_status;
	unsigned short regs[0x80]; /* register cache */
	unsigned int limited_regs; /* allow limited registers only */
	DECLARE_BITMAP(reg_accessed, 0x80); /* bit flags */
	union {			/* vendor specific code */
		struct {
			unsigned short unchained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short chained[3];	// 0 = C34, 1 = C79, 2 = C69
			unsigned short id[3];		// codec IDs (lower 16-bit word)
			unsigned short pcmreg[3];	// PCM registers
			unsigned short codec_cfg[3];	// CODEC_CFG bits
			struct semaphore mutex;
		} ad18xx;
		unsigned int dev_flags;		/* device specific */
	} spec;
};

/* conditions */
static inline int ac97_is_audio(ac97_t * ac97)
{
	return (ac97->scaps & AC97_SCAP_AUDIO);
}
static inline int ac97_is_modem(ac97_t * ac97)
{
	return (ac97->scaps & AC97_SCAP_MODEM);
}
static inline int ac97_is_rev22(ac97_t * ac97)
{
	return (ac97->ext_id & AC97_EI_REV_MASK) == AC97_EI_REV_22;
}
static inline int ac97_can_amap(ac97_t * ac97)
{
	return (ac97->ext_id & AC97_EI_AMAP) != 0;
}

/* functions */
int snd_ac97_mixer(snd_card_t * card, ac97_t * _ac97, ac97_t ** rac97);	/* create mixer controls */
int snd_ac97_modem(snd_card_t * card, ac97_t * _ac97, ac97_t ** rac97);	/* create modem controls */

void snd_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short value);
unsigned short snd_ac97_read(ac97_t *ac97, unsigned short reg);
void snd_ac97_write_cache(ac97_t *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update(ac97_t *ac97, unsigned short reg, unsigned short value);
int snd_ac97_update_bits(ac97_t *ac97, unsigned short reg, unsigned short mask, unsigned short value);
int snd_ac97_set_rate(ac97_t *ac97, int reg, unsigned short rate);
#ifdef CONFIG_PM
void snd_ac97_suspend(ac97_t *ac97);
void snd_ac97_resume(ac97_t *ac97);
#endif

enum { AC97_TUNE_HP_ONLY, AC97_TUNE_SWAP_HP, AC97_TUNE_SWAP_SURROUND };

struct ac97_quirk {
	unsigned short vendor;
	unsigned short device;
	const char *name;
	int type;
};

int snd_ac97_tune_hardware(ac97_t *ac97, struct ac97_quirk *quirk);

#endif /* __SOUND_AC97_CODEC_H */
