/*
 * PMac Burgundy lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 * code based on dmasound.c.
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
 */

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "pmac.h"
#include "burgundy.h"

#define chip_t pmac_t


/* Waits for busy flag to clear */
inline static void
snd_pmac_burgundy_busy_wait(pmac_t *chip)
{
	while (in_le32(&chip->awacs->codec_ctrl) & MASK_NEWECMD)
		;
}

inline static void
snd_pmac_burgundy_extend_wait(pmac_t *chip)
{
	while (!(in_le32(&chip->awacs->codec_stat) & MASK_EXTEND))
		;
	while (in_le32(&chip->awacs->codec_stat) & MASK_EXTEND)
		;
}

static void
snd_pmac_burgundy_wcw(pmac_t *chip, unsigned addr, unsigned val)
{
	out_le32(&chip->awacs->codec_ctrl, addr + 0x200c00 + (val & 0xff));
	snd_pmac_burgundy_busy_wait(chip);
	out_le32(&chip->awacs->codec_ctrl, addr + 0x200d00 +((val>>8) & 0xff));
	snd_pmac_burgundy_busy_wait(chip);
	out_le32(&chip->awacs->codec_ctrl, addr + 0x200e00 +((val>>16) & 0xff));
	snd_pmac_burgundy_busy_wait(chip);
	out_le32(&chip->awacs->codec_ctrl, addr + 0x200f00 +((val>>24) & 0xff));
	snd_pmac_burgundy_busy_wait(chip);
}

static unsigned
snd_pmac_burgundy_rcw(pmac_t *chip, unsigned addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	spin_lock_irqsave(&chip->reg_lock, flags);

	out_le32(&chip->awacs->codec_ctrl, addr + 0x100000);
	snd_pmac_burgundy_busy_wait(chip);
	snd_pmac_burgundy_extend_wait(chip);
	val += (in_le32(&chip->awacs->codec_stat) >> 4) & 0xff;

	out_le32(&chip->awacs->codec_ctrl, addr + 0x100100);
	snd_pmac_burgundy_busy_wait(chip);
	snd_pmac_burgundy_extend_wait(chip);
	val += ((in_le32(&chip->awacs->codec_stat)>>4) & 0xff) <<8;

	out_le32(&chip->awacs->codec_ctrl, addr + 0x100200);
	snd_pmac_burgundy_busy_wait(chip);
	snd_pmac_burgundy_extend_wait(chip);
	val += ((in_le32(&chip->awacs->codec_stat)>>4) & 0xff) <<16;

	out_le32(&chip->awacs->codec_ctrl, addr + 0x100300);
	snd_pmac_burgundy_busy_wait(chip);
	snd_pmac_burgundy_extend_wait(chip);
	val += ((in_le32(&chip->awacs->codec_stat)>>4) & 0xff) <<24;

	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return val;
}

static void
snd_pmac_burgundy_wcb(pmac_t *chip, unsigned int addr, unsigned int val)
{
	out_le32(&chip->awacs->codec_ctrl, addr + 0x300000 + (val & 0xff));
	snd_pmac_burgundy_busy_wait(chip);
}

static unsigned
snd_pmac_burgundy_rcb(pmac_t *chip, unsigned int addr)
{
	unsigned val = 0;
	unsigned long flags;

	/* should have timeouts here */
	spin_lock_irqsave(&chip->reg_lock, flags);

	out_le32(&chip->awacs->codec_ctrl, addr + 0x100000);
	snd_pmac_burgundy_busy_wait(chip);
	snd_pmac_burgundy_extend_wait(chip);
	val += (in_le32(&chip->awacs->codec_stat) >> 4) & 0xff;

	spin_unlock_irqrestore(&chip->reg_lock, flags);

	return val;
}

/*
 * Burgundy volume: 0 - 100, stereo
 */
static void
snd_pmac_burgundy_write_volume(pmac_t *chip, unsigned int address, long *volume, int shift)
{
	int hardvolume, lvolume, rvolume;

	lvolume = volume[0] ? volume[0] + BURGUNDY_VOLUME_OFFSET : 0;
	rvolume = volume[1] ? volume[1] + BURGUNDY_VOLUME_OFFSET : 0;

	hardvolume = lvolume + (rvolume << shift);
	if (shift == 8)
		hardvolume |= hardvolume << 16;

	snd_pmac_burgundy_wcw(chip, address, hardvolume);
}

static void
snd_pmac_burgundy_read_volume(pmac_t *chip, unsigned int address, long *volume, int shift)
{
	int wvolume;

	wvolume = snd_pmac_burgundy_rcw(chip, address);

	volume[0] = wvolume & 0xff;
	if (volume[0] >= BURGUNDY_VOLUME_OFFSET)
		volume[0] -= BURGUNDY_VOLUME_OFFSET;
	else
		volume[0] = 0;
	volume[1] = (wvolume >> shift) & 0xff;
	if (volume[1] >= BURGUNDY_VOLUME_OFFSET)
		volume[1] -= BURGUNDY_VOLUME_OFFSET;
	else
		volume[1] = 0;
}


/*
 */

#define BASE2ADDR(base)	((base) << 12)
#define ADDR2BASE(addr)	((addr) >> 12)

static int snd_pmac_burgundy_info_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}

static int snd_pmac_burgundy_get_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int addr = BASE2ADDR(kcontrol->private_value & 0xff);
	int shift = (kcontrol->private_value >> 8) & 0xff;
	snd_pmac_burgundy_read_volume(chip, addr, ucontrol->value.integer.value, shift);
	return 0;
}

static int snd_pmac_burgundy_put_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int addr = BASE2ADDR(kcontrol->private_value & 0xff);
	int shift = (kcontrol->private_value >> 8) & 0xff;
	long nvoices[2];

	snd_pmac_burgundy_write_volume(chip, addr, ucontrol->value.integer.value, shift);
	snd_pmac_burgundy_read_volume(chip, addr, nvoices, shift);
	return (nvoices[0] != ucontrol->value.integer.value[0] ||
		nvoices[1] != ucontrol->value.integer.value[1]);
}

#define BURGUNDY_VOLUME(xname, xindex, addr, shift) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex,\
  .info = snd_pmac_burgundy_info_volume,\
  .get = snd_pmac_burgundy_get_volume,\
  .put = snd_pmac_burgundy_put_volume,\
  .private_value = ((ADDR2BASE(addr) & 0xff) | ((shift) << 8)) }

/* lineout/speaker */

static int snd_pmac_burgundy_info_switch_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	int stereo = (kcontrol->private_value >> 24) & 1;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_pmac_burgundy_get_switch_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int lmask = kcontrol->private_value & 0xff;
	int rmask = (kcontrol->private_value >> 8) & 0xff;
	int stereo = (kcontrol->private_value >> 24) & 1;
	int val = snd_pmac_burgundy_rcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES);
	ucontrol->value.integer.value[0] = (val & lmask) ? 1 : 0;
	if (stereo)
		ucontrol->value.integer.value[1] = (val & rmask) ? 1 : 0;
	return 0;
}

static int snd_pmac_burgundy_put_switch_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	int lmask = kcontrol->private_value & 0xff;
	int rmask = (kcontrol->private_value >> 8) & 0xff;
	int stereo = (kcontrol->private_value >> 24) & 1;
	int val, oval;
	oval = snd_pmac_burgundy_rcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES);
	val = oval & ~(lmask | rmask);
	if (ucontrol->value.integer.value[0])
		val |= lmask;
	if (stereo && ucontrol->value.integer.value[1])
		val |= rmask;
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES, val);
	return val != oval;
}

#define BURGUNDY_OUTPUT_SWITCH(xname, xindex, lmask, rmask, stereo) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex,\
  .info = snd_pmac_burgundy_info_switch_out,\
  .get = snd_pmac_burgundy_get_switch_out,\
  .put = snd_pmac_burgundy_put_switch_out,\
  .private_value = ((lmask) | ((rmask) << 8) | ((stereo) << 24)) }

/* line/speaker output volume */
static int snd_pmac_burgundy_info_volume_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	int stereo = (kcontrol->private_value >> 24) & 1;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = stereo + 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 15;
	return 0;
}

static int snd_pmac_burgundy_get_volume_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int addr = BASE2ADDR(kcontrol->private_value & 0xff);
	int stereo = (kcontrol->private_value >> 24) & 1;
	int oval;

	oval = ~snd_pmac_burgundy_rcb(chip, addr) & 0xff;
	ucontrol->value.integer.value[0] = oval & 0xf;
	if (stereo)
		ucontrol->value.integer.value[1] = (oval >> 4) & 0xf;
	return 0;
}

static int snd_pmac_burgundy_put_volume_out(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	unsigned int addr = BASE2ADDR(kcontrol->private_value & 0xff);
	int stereo = (kcontrol->private_value >> 24) & 1;
	int oval, val;

	oval = ~snd_pmac_burgundy_rcb(chip, addr) & 0xff;
	val = ucontrol->value.integer.value[0];
	if (stereo)
		val |= ucontrol->value.integer.value[1] << 4;
	else
		val |= ucontrol->value.integer.value[0] << 4;
	val = ~val & 0xff;
	snd_pmac_burgundy_wcb(chip, addr, val);
	return val != oval;
}

#define BURGUNDY_OUTPUT_VOLUME(xname, xindex, addr, stereo) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex,\
  .info = snd_pmac_burgundy_info_volume_out,\
  .get = snd_pmac_burgundy_get_volume_out,\
  .put = snd_pmac_burgundy_put_volume_out,\
  .private_value = (ADDR2BASE(addr) | ((stereo) << 24)) }

static snd_kcontrol_new_t snd_pmac_burgundy_mixers[] __initdata = {
	BURGUNDY_VOLUME("Master Playback Volume", 0, MASK_ADDR_BURGUNDY_MASTER_VOLUME, 8),
	BURGUNDY_VOLUME("Line Playback Volume", 0, MASK_ADDR_BURGUNDY_VOLLINE, 16),
	BURGUNDY_VOLUME("CD Playback Volume", 0, MASK_ADDR_BURGUNDY_VOLCD, 16),
	BURGUNDY_VOLUME("Mic Playback Volume", 0, MASK_ADDR_BURGUNDY_VOLMIC, 16),
	BURGUNDY_OUTPUT_VOLUME("PC Speaker Playback Volume", 0, MASK_ADDR_BURGUNDY_ATTENHP, 0),
	/*BURGUNDY_OUTPUT_VOLUME("PCM Playback Volume", 0, MASK_ADDR_BURGUNDY_ATTENLINEOUT, 1),*/
	BURGUNDY_OUTPUT_VOLUME("Headphone Playback Volume", 0, MASK_ADDR_BURGUNDY_ATTENSPEAKER, 1),
};	
static snd_kcontrol_new_t snd_pmac_burgundy_master_sw __initdata = 
BURGUNDY_OUTPUT_SWITCH("Headphone Playback Switch", 0, BURGUNDY_OUTPUT_LEFT, BURGUNDY_OUTPUT_RIGHT, 1);
static snd_kcontrol_new_t snd_pmac_burgundy_speaker_sw __initdata = 
BURGUNDY_OUTPUT_SWITCH("PC Speaker Playback Switch", 0, BURGUNDY_OUTPUT_INTERN, 0, 0);

#define num_controls(ary) (sizeof(ary) / sizeof(snd_kcontrol_new_t))


#ifdef PMAC_SUPPORT_AUTOMUTE
/*
 * auto-mute stuffs
 */
static int snd_pmac_burgundy_detect_headphone(pmac_t *chip)
{
	return (in_le32(&chip->awacs->codec_stat) & chip->hp_stat_mask) ? 1 : 0;
}

static void snd_pmac_burgundy_update_automute(pmac_t *chip, int do_notify)
{
	if (chip->auto_mute) {
		int reg, oreg;
		reg = oreg = snd_pmac_burgundy_rcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES);
		reg &= ~(BURGUNDY_OUTPUT_LEFT | BURGUNDY_OUTPUT_RIGHT | BURGUNDY_OUTPUT_INTERN);
		if (snd_pmac_burgundy_detect_headphone(chip))
			reg |= BURGUNDY_OUTPUT_LEFT | BURGUNDY_OUTPUT_RIGHT;
		else
			reg |= BURGUNDY_OUTPUT_INTERN;
		if (do_notify && reg == oreg)
			return;
		snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES, reg);
		if (do_notify) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_sw_ctl->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->speaker_sw_ctl->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->hp_detect_ctl->id);
		}
	}
}
#endif /* PMAC_SUPPORT_AUTOMUTE */


/*
 * initialize burgundy
 */
int __init snd_pmac_burgundy_init(pmac_t *chip)
{
	int i, err;

	/* Checks to see the chip is alive and kicking */
	if ((in_le32(&chip->awacs->codec_ctrl) & MASK_ERRCODE) == 0xf0000) {
		printk(KERN_WARNING "pmac burgundy: disabled by MacOS :-(\n");
		return 1;
	}

	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_OUTPUTENABLES,
			   DEF_BURGUNDY_OUTPUTENABLES);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_MORE_OUTPUTENABLES,
			   DEF_BURGUNDY_MORE_OUTPUTENABLES);
	snd_pmac_burgundy_wcw(chip, MASK_ADDR_BURGUNDY_OUTPUTSELECTS,
			   DEF_BURGUNDY_OUTPUTSELECTS);

	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_INPSEL21,
			   DEF_BURGUNDY_INPSEL21);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_INPSEL3,
			   DEF_BURGUNDY_INPSEL3);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_GAINCD,
			   DEF_BURGUNDY_GAINCD);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_GAINLINE,
			   DEF_BURGUNDY_GAINLINE);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_GAINMIC,
			   DEF_BURGUNDY_GAINMIC);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_GAINMODEM,
			   DEF_BURGUNDY_GAINMODEM);

	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_ATTENSPEAKER,
			   DEF_BURGUNDY_ATTENSPEAKER);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_ATTENLINEOUT,
			   DEF_BURGUNDY_ATTENLINEOUT);
	snd_pmac_burgundy_wcb(chip, MASK_ADDR_BURGUNDY_ATTENHP,
			   DEF_BURGUNDY_ATTENHP);

	snd_pmac_burgundy_wcw(chip, MASK_ADDR_BURGUNDY_MASTER_VOLUME,
			   DEF_BURGUNDY_MASTER_VOLUME);
	snd_pmac_burgundy_wcw(chip, MASK_ADDR_BURGUNDY_VOLCD,
			   DEF_BURGUNDY_VOLCD);
	snd_pmac_burgundy_wcw(chip, MASK_ADDR_BURGUNDY_VOLLINE,
			   DEF_BURGUNDY_VOLLINE);
	snd_pmac_burgundy_wcw(chip, MASK_ADDR_BURGUNDY_VOLMIC,
			   DEF_BURGUNDY_VOLMIC);

	if (chip->hp_stat_mask == 0)
		/* set headphone-jack detection bit */
		chip->hp_stat_mask = 0x04;

	/*
	 * build burgundy mixers
	 */
	strcpy(chip->card->mixername, "PowerMac Burgundy");

	for (i = 0; i < num_controls(snd_pmac_burgundy_mixers); i++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snd_pmac_burgundy_mixers[i], chip))) < 0)
			return err;
	}
	chip->master_sw_ctl = snd_ctl_new1(&snd_pmac_burgundy_master_sw, chip);
	if ((err = snd_ctl_add(chip->card, chip->master_sw_ctl)) < 0)
		return err;
	chip->speaker_sw_ctl = snd_ctl_new1(&snd_pmac_burgundy_speaker_sw, chip);
	if ((err = snd_ctl_add(chip->card, chip->speaker_sw_ctl)) < 0)
		return err;
#ifdef PMAC_SUPPORT_AUTOMUTE
	if ((err = snd_pmac_add_automute(chip)) < 0)
		return err;

	chip->detect_headphone = snd_pmac_burgundy_detect_headphone;
	chip->update_automute = snd_pmac_burgundy_update_automute;
	snd_pmac_burgundy_update_automute(chip, 0); /* update the status only */
#endif

	return 0;
}
