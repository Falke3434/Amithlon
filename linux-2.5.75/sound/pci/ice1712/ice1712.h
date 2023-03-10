#ifndef __SOUND_ICE1712_H
#define __SOUND_ICE1712_H

/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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

#include <sound/control.h>
#include <sound/ac97_codec.h>
#include <sound/rawmidi.h>
#include <sound/i2c.h>
#include <sound/ak4xxx-adda.h>
#include <sound/pcm.h>


/*
 *  Direct registers
 */

#define ICEREG(ice, x) ((ice)->port + ICE1712_REG_##x)

#define ICE1712_REG_CONTROL		0x00	/* byte */
#define   ICE1712_RESET			0x80	/* reset whole chip */
#define   ICE1712_SERR_LEVEL		0x04	/* SERR# level otherwise edge */
#define   ICE1712_NATIVE		0x01	/* native mode otherwise SB */
#define ICE1712_REG_IRQMASK		0x01	/* byte */
#define   ICE1712_IRQ_MPU1		0x80
#define   ICE1712_IRQ_TIMER		0x40
#define   ICE1712_IRQ_MPU2		0x20
#define   ICE1712_IRQ_PROPCM		0x10
#define   ICE1712_IRQ_FM		0x08	/* FM/MIDI - legacy */
#define   ICE1712_IRQ_PBKDS		0x04	/* playback DS channels */
#define   ICE1712_IRQ_CONCAP		0x02	/* consumer capture */
#define   ICE1712_IRQ_CONPBK		0x01	/* consumer playback */
#define ICE1712_REG_IRQSTAT		0x02	/* byte */
/* look to ICE1712_IRQ_* */
#define ICE1712_REG_INDEX		0x03	/* byte - indirect CCIxx regs */
#define ICE1712_REG_DATA		0x04	/* byte - indirect CCIxx regs */
#define ICE1712_REG_NMI_STAT1		0x05	/* byte */
#define ICE1712_REG_NMI_DATA		0x06	/* byte */
#define ICE1712_REG_NMI_INDEX		0x07	/* byte */
#define ICE1712_REG_AC97_INDEX		0x08	/* byte */
#define ICE1712_REG_AC97_CMD		0x09	/* byte */
#define   ICE1712_AC97_COLD		0x80	/* cold reset */
#define   ICE1712_AC97_WARM		0x40	/* warm reset */
#define   ICE1712_AC97_WRITE		0x20	/* W: write, R: write in progress */
#define   ICE1712_AC97_READ		0x10	/* W: read, R: read in progress */
#define   ICE1712_AC97_READY		0x08	/* codec ready status bit */
#define   ICE1712_AC97_PBK_VSR		0x02	/* playback VSR */
#define   ICE1712_AC97_CAP_VSR		0x01	/* capture VSR */
#define ICE1712_REG_AC97_DATA		0x0a	/* word (little endian) */
#define ICE1712_REG_MPU1_CTRL		0x0c	/* byte */
#define ICE1712_REG_MPU1_DATA		0x0d	/* byte */
#define ICE1712_REG_I2C_DEV_ADDR	0x10	/* byte */
#define   ICE1712_I2C_WRITE		0x01	/* write direction */
#define ICE1712_REG_I2C_BYTE_ADDR	0x11	/* byte */
#define ICE1712_REG_I2C_DATA		0x12	/* byte */
#define ICE1712_REG_I2C_CTRL		0x13	/* byte */
#define   ICE1712_I2C_EEPROM		0x80	/* EEPROM exists */
#define   ICE1712_I2C_BUSY		0x01	/* busy bit */
#define ICE1712_REG_CONCAP_ADDR		0x14	/* dword - consumer capture */
#define ICE1712_REG_CONCAP_COUNT	0x18	/* word - current/base count */
#define ICE1712_REG_SERR_SHADOW		0x1b	/* byte */
#define ICE1712_REG_MPU2_CTRL		0x1c	/* byte */
#define ICE1712_REG_MPU2_DATA		0x1d	/* byte */
#define ICE1712_REG_TIMER		0x1e	/* word */

/*
 *  Indirect registers
 */

#define ICE1712_IREG_PBK_COUNT_HI	0x00
#define ICE1712_IREG_PBK_COUNT_LO	0x01
#define ICE1712_IREG_PBK_CTRL		0x02
#define ICE1712_IREG_PBK_LEFT		0x03	/* left volume */
#define ICE1712_IREG_PBK_RIGHT		0x04	/* right volume */
#define ICE1712_IREG_PBK_SOFT		0x05	/* soft volume */
#define ICE1712_IREG_PBK_RATE_LO	0x06
#define ICE1712_IREG_PBK_RATE_MID	0x07
#define ICE1712_IREG_PBK_RATE_HI	0x08
#define ICE1712_IREG_CAP_COUNT_HI	0x10
#define ICE1712_IREG_CAP_COUNT_LO	0x11
#define ICE1712_IREG_CAP_CTRL		0x12
#define ICE1712_IREG_GPIO_DATA		0x20
#define ICE1712_IREG_GPIO_WRITE_MASK	0x21
#define ICE1712_IREG_GPIO_DIRECTION	0x22
#define ICE1712_IREG_CONSUMER_POWERDOWN	0x30
#define ICE1712_IREG_PRO_POWERDOWN	0x31

/*
 *  Consumer section direct DMA registers
 */

#define ICEDS(ice, x) ((ice)->dmapath_port + ICE1712_DS_##x)
 
#define ICE1712_DS_INTMASK		0x00	/* word - interrupt mask */
#define ICE1712_DS_INTSTAT		0x02	/* word - interrupt status */
#define ICE1712_DS_DATA			0x04	/* dword - channel data */
#define ICE1712_DS_INDEX		0x08	/* dword - channel index */

/*
 *  Consumer section channel registers
 */
 
#define ICE1712_DSC_ADDR0		0x00	/* dword - base address 0 */
#define ICE1712_DSC_COUNT0		0x01	/* word - count 0 */
#define ICE1712_DSC_ADDR1		0x02	/* dword - base address 1 */
#define ICE1712_DSC_COUNT1		0x03	/* word - count 1 */
#define ICE1712_DSC_CONTROL		0x04	/* byte - control & status */
#define   ICE1712_BUFFER1		0x80	/* buffer1 is active */
#define   ICE1712_BUFFER1_AUTO		0x40	/* buffer1 auto init */
#define   ICE1712_BUFFER0_AUTO		0x20	/* buffer0 auto init */
#define   ICE1712_FLUSH			0x10	/* flush FIFO */
#define   ICE1712_STEREO		0x08	/* stereo */
#define   ICE1712_16BIT			0x04	/* 16-bit data */
#define   ICE1712_PAUSE			0x02	/* pause */
#define   ICE1712_START			0x01	/* start */
#define ICE1712_DSC_RATE		0x05	/* dword - rate */
#define ICE1712_DSC_VOLUME		0x06	/* word - volume control */

/* 
 *  Professional multi-track direct control registers
 */

#define ICEMT(ice, x) ((ice)->profi_port + ICE1712_MT_##x)

#define ICE1712_MT_IRQ			0x00	/* byte - interrupt mask */
#define   ICE1712_MULTI_CAPTURE		0x80	/* capture IRQ */
#define   ICE1712_MULTI_PLAYBACK	0x40	/* playback IRQ */
#define   ICE1712_MULTI_CAPSTATUS	0x02	/* capture IRQ status */
#define   ICE1712_MULTI_PBKSTATUS	0x01	/* playback IRQ status */
#define ICE1712_MT_RATE			0x01	/* byte - sampling rate select */
#define   ICE1712_SPDIF_MASTER		0x10	/* S/PDIF input is master clock */
#define ICE1712_MT_I2S_FORMAT		0x02	/* byte - I2S data format */
#define ICE1712_MT_AC97_INDEX		0x04	/* byte - AC'97 index */
#define ICE1712_MT_AC97_CMD		0x05	/* byte - AC'97 command & status */
/* look to ICE1712_AC97_* */
#define ICE1712_MT_AC97_DATA		0x06	/* word - AC'97 data */
#define ICE1712_MT_PLAYBACK_ADDR	0x10	/* dword - playback address */
#define ICE1712_MT_PLAYBACK_SIZE	0x14	/* word - playback size */
#define ICE1712_MT_PLAYBACK_COUNT	0x16	/* word - playback count */
#define ICE1712_MT_PLAYBACK_CONTROL	0x18	/* byte - control */
#define   ICE1712_CAPTURE_START_SHADOW	0x04	/* capture start */
#define   ICE1712_PLAYBACK_PAUSE	0x02	/* playback pause */
#define   ICE1712_PLAYBACK_START	0x01	/* playback start */
#define ICE1712_MT_CAPTURE_ADDR		0x20	/* dword - capture address */
#define ICE1712_MT_CAPTURE_SIZE		0x24	/* word - capture size */
#define ICE1712_MT_CAPTURE_COUNT	0x26	/* word - capture count */
#define ICE1712_MT_CAPTURE_CONTROL	0x28	/* byte - control */
#define   ICE1712_CAPTURE_START		0x01	/* capture start */
#define ICE1712_MT_ROUTE_PSDOUT03	0x30	/* word */
#define ICE1712_MT_ROUTE_SPDOUT		0x32	/* word */
#define ICE1712_MT_ROUTE_CAPTURE	0x34	/* dword */
#define ICE1712_MT_MONITOR_VOLUME	0x38	/* word */
#define ICE1712_MT_MONITOR_INDEX	0x3a	/* byte */
#define ICE1712_MT_MONITOR_RATE		0x3b	/* byte */
#define ICE1712_MT_MONITOR_ROUTECTRL	0x3c	/* byte */
#define   ICE1712_ROUTE_AC97		0x01	/* route digital mixer output to AC'97 */
#define ICE1712_MT_MONITOR_PEAKINDEX	0x3e	/* byte */
#define ICE1712_MT_MONITOR_PEAKDATA	0x3f	/* byte */

/*
 *  Codec configuration bits
 */

/* PCI[60] System Configuration */
#define ICE1712_CFG_CLOCK	0xc0
#define   ICE1712_CFG_CLOCK512	0x00	/* 22.5692Mhz, 44.1kHz*512 */
#define   ICE1712_CFG_CLOCK384  0x40	/* 16.9344Mhz, 44.1kHz*384 */
#define   ICE1712_CFG_EXT	0x80	/* external clock */
#define ICE1712_CFG_2xMPU401	0x20	/* two MPU401 UARTs */
#define ICE1712_CFG_NO_CON_AC97 0x10	/* consumer AC'97 codec is not present */
#define ICE1712_CFG_ADC_MASK	0x0c	/* one, two, three, four stereo ADCs */
#define ICE1712_CFG_DAC_MASK	0x03	/* one, two, three, four stereo DACs */
/* PCI[61] AC-Link Configuration */
#define ICE1712_CFG_PRO_I2S	0x80	/* multitrack converter: I2S or AC'97 */
#define ICE1712_CFG_AC97_PACKED	0x01	/* split or packed mode - AC'97 */
/* PCI[62] I2S Features */
#define ICE1712_CFG_I2S_VOLUME	0x80	/* volume/mute capability */
#define ICE1712_CFG_I2S_96KHZ	0x40	/* supports 96kHz sampling */
#define ICE1712_CFG_I2S_RESMASK	0x30	/* resolution mask, 16,18,20,24-bit */
#define ICE1712_CFG_I2S_OTHER	0x0f	/* other I2S IDs */
/* PCI[63] S/PDIF Configuration */
#define ICE1712_CFG_I2S_CHIPID	0xfc	/* I2S chip ID */
#define ICE1712_CFG_SPDIF_IN	0x02	/* S/PDIF input is present */
#define ICE1712_CFG_SPDIF_OUT	0x01	/* S/PDIF output is present */

/*
 * DMA mode values
 * identical with DMA_XXX on i386 architecture.
 */
#define ICE1712_DMA_MODE_WRITE		0x48
#define ICE1712_DMA_AUTOINIT		0x10


/*
 *  
 */

typedef struct _snd_ice1712 ice1712_t;

typedef struct {
	unsigned int subvendor;	/* PCI[2c-2f] */
	unsigned char size;	/* size of EEPROM image in bytes */
	unsigned char version;	/* must be 1 (or 2 for vt1724) */
	unsigned char data[32];
	unsigned int gpiomask;
	unsigned int gpiostate;
	unsigned int gpiodir;
} ice1712_eeprom_t;

enum {
	ICE_EEP1_CODEC = 0,	/* 06 */
	ICE_EEP1_ACLINK,	/* 07 */
	ICE_EEP1_I2SID,		/* 08 */
	ICE_EEP1_SPDIF,		/* 09 */
	ICE_EEP1_GPIO_MASK,	/* 0a */
	ICE_EEP1_GPIO_STATE,	/* 0b */
	ICE_EEP1_GPIO_DIR,	/* 0c */
	ICE_EEP1_AC97_MAIN_LO,	/* 0d */
	ICE_EEP1_AC97_MAIN_HI,	/* 0e */
	ICE_EEP1_AC97_PCM_LO,	/* 0f */
	ICE_EEP1_AC97_PCM_HI,	/* 10 */
	ICE_EEP1_AC97_REC_LO,	/* 11 */
	ICE_EEP1_AC97_REC_HI,	/* 12 */
	ICE_EEP1_AC97_RECSRC,	/* 13 */
	ICE_EEP1_DAC_ID,	/* 14 */
	ICE_EEP1_DAC_ID1,
	ICE_EEP1_DAC_ID2,
	ICE_EEP1_DAC_ID3,
	ICE_EEP1_ADC_ID,	/* 18 */
	ICE_EEP1_ADC_ID1,
	ICE_EEP1_ADC_ID2,
	ICE_EEP1_ADC_ID3
};
	
#define ice_has_con_ac97(ice)	(!((ice)->eeprom.data[ICE_EEP1_CODEC] & ICE1712_CFG_NO_CON_AC97))


struct snd_ak4xxx_private {
	unsigned int cif: 1;		/* CIF mode */
	unsigned char caddr;		/* C0 and C1 bits */
	unsigned int data_mask;		/* DATA gpio bit */
	unsigned int clk_mask;		/* CLK gpio bit */
	unsigned int cs_mask;		/* bit mask for select/deselect address */
	unsigned int cs_addr;		/* bits to select address */
	unsigned int cs_none;		/* bits to deselect address */
	unsigned int add_flags;		/* additional bits at init */
	unsigned int mask_flags;	/* total mask bits */
	struct snd_akm4xxx_ops {
		void (*set_rate_val)(akm4xxx_t *ak, unsigned int rate);
	} ops;
};

struct snd_ice1712_spdif {
	unsigned char cs8403_bits;
	unsigned char cs8403_stream_bits;
	snd_kcontrol_t *stream_ctl;

	struct snd_ice1712_spdif_ops {
		void (*open)(ice1712_t *, snd_pcm_substream_t *);
		void (*setup_rate)(ice1712_t *, int rate);
		void (*close)(ice1712_t *, snd_pcm_substream_t *);
		void (*default_get)(ice1712_t *, snd_ctl_elem_value_t * ucontrol);
		int (*default_put)(ice1712_t *, snd_ctl_elem_value_t * ucontrol);
		void (*stream_get)(ice1712_t *, snd_ctl_elem_value_t * ucontrol);
		int (*stream_put)(ice1712_t *, snd_ctl_elem_value_t * ucontrol);
	} ops;
};


struct _snd_ice1712 {
	unsigned long conp_dma_size;
	unsigned long conc_dma_size;
	unsigned long prop_dma_size;
	unsigned long proc_dma_size;
	int irq;

	unsigned long port;
	struct resource *res_port;
	unsigned long ddma_port;
	struct resource *res_ddma_port;
	unsigned long dmapath_port;
	struct resource *res_dmapath_port;
	unsigned long profi_port;
	struct resource *res_profi_port;

	struct pci_dev *pci;
	snd_card_t *card;
	snd_pcm_t *pcm;
	snd_pcm_t *pcm_ds;
	snd_pcm_t *pcm_pro;
        snd_pcm_substream_t *playback_con_substream;
        snd_pcm_substream_t *playback_con_substream_ds[6];
        snd_pcm_substream_t *capture_con_substream;
        snd_pcm_substream_t *playback_pro_substream;
        snd_pcm_substream_t *capture_pro_substream;
	unsigned int playback_pro_size;
	unsigned int capture_pro_size;
	unsigned int playback_con_virt_addr[6];
	unsigned int playback_con_active_buf[6];
	unsigned int capture_con_virt_addr;
	unsigned int ac97_ext_id;
	ac97_t *ac97;
	snd_rawmidi_t *rmidi[2];

	spinlock_t reg_lock;
	snd_info_entry_t *proc_entry;

	ice1712_eeprom_t eeprom;

	unsigned int pro_volumes[20];
	unsigned int omni: 1;		/* Delta Omni I/O */
	unsigned int vt1724: 1;
	unsigned int num_total_dacs;	/* total DACs */
	unsigned char hoontech_boxbits[4];
	unsigned int hoontech_config;
	unsigned short hoontech_boxconfig[4];
	unsigned int cur_rate;		/* current rate */

	unsigned int akm_codecs;
	akm4xxx_t *akm;
	struct snd_ice1712_spdif spdif;

	snd_i2c_bus_t *i2c;		/* I2C bus */
	snd_i2c_device_t *cs8404;	/* CS8404A I2C device */
	snd_i2c_device_t *cs8427;	/* CS8427 I2C device */
	snd_i2c_device_t *i2cdevs[2];	/* additional i2c devices */
	
	struct ice1712_gpio {
		unsigned int direction;		/* current direction bits */
		unsigned int write_mask;	/* current mask bits */
		unsigned int saved[2];		/* for ewx_i2c */
		/* operators */
		void (*set_mask)(ice1712_t *ice, unsigned int data);
		void (*set_dir)(ice1712_t *ice, unsigned int data);
		void (*set_data)(ice1712_t *ice, unsigned int data);
		unsigned int (*get_data)(ice1712_t *ice);
	} gpio;
	struct semaphore gpio_mutex;
};

#define chip_t ice1712_t


/*
 * gpio access functions
 */
static inline void snd_ice1712_gpio_set_dir(ice1712_t *ice, unsigned int bits)
{
	ice->gpio.set_dir(ice, bits);
}

static inline void snd_ice1712_gpio_set_mask(ice1712_t *ice, unsigned int bits)
{
	ice->gpio.set_mask(ice, bits);
}

static inline void snd_ice1712_gpio_write(ice1712_t *ice, unsigned int val)
{
	ice->gpio.set_data(ice, val);
}

static inline unsigned int snd_ice1712_gpio_read(ice1712_t *ice)
{
	return ice->gpio.get_data(ice);
}

/*
 * save and restore gpio status
 * The access to gpio will be protected by mutex, so don't forget to
 * restore!
 */
static inline void snd_ice1712_save_gpio_status(ice1712_t *ice)
{
	down(&ice->gpio_mutex);
	ice->gpio.saved[0] = ice->gpio.direction;
	ice->gpio.saved[1] = ice->gpio.write_mask;
}

static inline void snd_ice1712_restore_gpio_status(ice1712_t *ice)
{
	ice->gpio.set_dir(ice, ice->gpio.saved[0]);
	ice->gpio.set_mask(ice, ice->gpio.saved[1]);
	ice->gpio.direction = ice->gpio.saved[0];
	ice->gpio.write_mask = ice->gpio.saved[1];
	up(&ice->gpio_mutex);
}

/* for bit controls */
#define ICE1712_GPIO(xiface, xname, xindex, mask, invert, xaccess) \
{ .iface = xiface, .name = xname, .access = xaccess, .info = snd_ice1712_gpio_info, \
  .get = snd_ice1712_gpio_get, .put = snd_ice1712_gpio_put, \
  .private_value = mask | (invert << 24) }

int snd_ice1712_gpio_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo);
int snd_ice1712_gpio_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);
int snd_ice1712_gpio_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol);

/*
 * set gpio direction, write mask and data
 */
static inline void snd_ice1712_gpio_write_bits(ice1712_t *ice, unsigned int mask, unsigned int bits)
{
	ice->gpio.direction |= mask;
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
	snd_ice1712_gpio_set_mask(ice, ~mask);
	snd_ice1712_gpio_write(ice, mask & bits);
}

int snd_ice1712_spdif_build_controls(ice1712_t *ice);

int snd_ice1712_akm4xxx_init(akm4xxx_t *ak, const akm4xxx_t *template, const struct snd_ak4xxx_private *priv, ice1712_t *ice);
void snd_ice1712_akm4xxx_free(ice1712_t *ice);
int snd_ice1712_akm4xxx_build_controls(ice1712_t *ice);

int snd_ice1712_init_cs8427(ice1712_t *ice, int addr);

static inline void snd_ice1712_write(ice1712_t * ice, u8 addr, u8 data)
{
	outb(addr, ICEREG(ice, INDEX));
	outb(data, ICEREG(ice, DATA));
}

static inline u8 snd_ice1712_read(ice1712_t * ice, u8 addr)
{
	outb(addr, ICEREG(ice, INDEX));
	return inb(ICEREG(ice, DATA));
}


/*
 * entry pointer
 */

struct snd_ice1712_card_info {
	unsigned int subvendor;
	char *name;
	int (*chip_init)(ice1712_t *);
	int (*build_controls)(ice1712_t *);
	int no_mpu401: 1;
	unsigned int eeprom_size;
	unsigned char *eeprom_data;
};


#endif /* __SOUND_ICE1712_H */
