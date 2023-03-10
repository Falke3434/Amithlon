/*
 *  OSS emulation layer for the mixer interface
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/mixer_oss.h>
#include <linux/soundcard.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Mixer OSS emulation for ALSA.");
MODULE_LICENSE("GPL");

static int snd_mixer_oss_open(struct inode *inode, struct file *file)
{
	int cardnum = SNDRV_MINOR_OSS_CARD(minor(inode->i_rdev));
	snd_card_t *card;
	snd_mixer_oss_file_t *fmixer;
	int err;

	if ((card = snd_cards[cardnum]) == NULL)
		return -ENODEV;
	if (card->mixer_oss == NULL)
		return -ENODEV;
	err = snd_card_file_add(card, file);
	if (err < 0)
		return err;
	fmixer = (snd_mixer_oss_file_t *)snd_kcalloc(sizeof(*fmixer), GFP_KERNEL);
	if (fmixer == NULL) {
		snd_card_file_remove(card, file);
		return -ENOMEM;
	}
	fmixer->card = card;
	fmixer->mixer = card->mixer_oss;
	file->private_data = fmixer;
	if (!try_module_get(card->module)) {
		kfree(fmixer);
		snd_card_file_remove(card, file);
		return -EFAULT;
	}
	return 0;
}

static int snd_mixer_oss_release(struct inode *inode, struct file *file)
{
	snd_mixer_oss_file_t *fmixer;

	if (file->private_data) {
		fmixer = (snd_mixer_oss_file_t *) file->private_data;
		module_put(fmixer->card->module);
		snd_card_file_remove(fmixer->card, file);
		kfree(fmixer);
	}
	return 0;
}

static int snd_mixer_oss_info(snd_mixer_oss_file_t *fmixer,
			      mixer_info *_info)
{
	snd_card_t *card = fmixer->card;
	snd_mixer_oss_t *mixer = fmixer->mixer;
	struct mixer_info info;
	
	memset(&info, 0, sizeof(info));
	strlcpy(info.id, mixer && mixer->id[0] ? mixer->id : card->driver, sizeof(info.id));
	strlcpy(info.name, mixer && mixer->name[0] ? mixer->name : card->mixername, sizeof(info.name));
	info.modify_counter = card->mixer_oss_change_count;
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_mixer_oss_info_obsolete(snd_mixer_oss_file_t *fmixer,
				       _old_mixer_info *_info)
{
	snd_card_t *card = fmixer->card;
	snd_mixer_oss_t *mixer = fmixer->mixer;
	_old_mixer_info info;
	
	memset(&info, 0, sizeof(info));
	strlcpy(info.id, mixer && mixer->id[0] ? mixer->id : card->driver, sizeof(info.id));
	strlcpy(info.name, mixer && mixer->name[0] ? mixer->name : card->mixername, sizeof(info.name));
	if (copy_to_user(_info, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int snd_mixer_oss_caps(snd_mixer_oss_file_t *fmixer)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	int result = 0;

	if (mixer == NULL)
		return -EIO;
	if (mixer->get_recsrc && mixer->put_recsrc)
		result |= SOUND_CAP_EXCL_INPUT;
	return result;
}

static int snd_mixer_oss_devmask(snd_mixer_oss_file_t *fmixer)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_mixer_oss_slot_t *pslot;
	int result = 0, chn;

	if (mixer == NULL)
		return -EIO;
	for (chn = 0; chn < 31; chn++) {
		pslot = &mixer->slots[chn];
		if (pslot->put_volume || pslot->put_recsrc)
			result |= 1 << chn;
	}
	return result;
}

static int snd_mixer_oss_stereodevs(snd_mixer_oss_file_t *fmixer)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_mixer_oss_slot_t *pslot;
	int result = 0, chn;

	if (mixer == NULL)
		return -EIO;
	for (chn = 0; chn < 31; chn++) {
		pslot = &mixer->slots[chn];
		if (pslot->put_volume && pslot->stereo)
			result |= 1 << chn;
	}
	return result;
}

static int snd_mixer_oss_recmask(snd_mixer_oss_file_t *fmixer)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	int result = 0;

	if (mixer == NULL)
		return -EIO;
	if (mixer->put_recsrc && mixer->get_recsrc) {	/* exclusive */
		result = mixer->mask_recsrc;
	} else {
		snd_mixer_oss_slot_t *pslot;
		int chn;
		for (chn = 0; chn < 31; chn++) {
			pslot = &mixer->slots[chn];
			if (pslot->put_recsrc)
				result |= 1 << chn;
		}
	}
	return result;
}

static int snd_mixer_oss_get_recsrc(snd_mixer_oss_file_t *fmixer)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	int result = 0;

	if (mixer == NULL)
		return -EIO;
	if (mixer->put_recsrc && mixer->get_recsrc) {	/* exclusive */
		int err;
		if ((err = mixer->get_recsrc(fmixer, &result)) < 0)
			return err;
		result = 1 << result;
	} else {
		snd_mixer_oss_slot_t *pslot;
		int chn;
		for (chn = 0; chn < 31; chn++) {
			pslot = &mixer->slots[chn];
			if (pslot->get_recsrc) {
				int active = 0;
				pslot->get_recsrc(fmixer, pslot, &active);
				if (active)
					result |= 1 << chn;
			}
		}
	}
	return mixer->oss_recsrc = result;
}

static int snd_mixer_oss_set_recsrc(snd_mixer_oss_file_t *fmixer, int recsrc)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_mixer_oss_slot_t *pslot;
	int chn, active;
	int result = 0;

	if (mixer == NULL)
		return -EIO;
	if (mixer->get_recsrc && mixer->put_recsrc) {	/* exclusive input */
		if (recsrc & ~mixer->oss_recsrc)
			recsrc &= ~mixer->oss_recsrc;
		mixer->put_recsrc(fmixer, ffz(~recsrc));
		mixer->get_recsrc(fmixer, &result);
		result = 1 << result;
	}
	for (chn = 0; chn < 31; chn++) {
		pslot = &mixer->slots[chn];
		if (pslot->put_recsrc) {
			active = (recsrc & (1 << chn)) ? 1 : 0;
			pslot->put_recsrc(fmixer, pslot, active);
		}
	}
	if (! result) {
		for (chn = 0; chn < 31; chn++) {
			pslot = &mixer->slots[chn];
			if (pslot->get_recsrc) {
				active = 0;
				pslot->get_recsrc(fmixer, pslot, &active);
				if (active)
					result |= 1 << chn;
			}
		}
	}
	return result;
}

static int snd_mixer_oss_get_volume(snd_mixer_oss_file_t *fmixer, int slot)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_mixer_oss_slot_t *pslot;
	int result = 0, left, right;

	if (mixer == NULL || slot > 30)
		return -EIO;
	pslot = &mixer->slots[slot];
	left = pslot->volume[0];
	right = pslot->volume[1];
	if (pslot->get_volume)
		result = pslot->get_volume(fmixer, pslot, &left, &right);
	if (!pslot->stereo)
		right = left;
	snd_assert(left >= 0 && left <= 100, return -EIO);
	snd_assert(right >= 0 && right <= 100, return -EIO);
	if (result >= 0) {
		pslot->volume[0] = left;
		pslot->volume[1] = right;
	 	result = (left & 0xff) | ((right & 0xff) << 8);
	}
	return result;
}

static int snd_mixer_oss_set_volume(snd_mixer_oss_file_t *fmixer,
				    int slot, int volume)
{
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_mixer_oss_slot_t *pslot;
	int result = 0, left = volume & 0xff, right = (volume >> 8) & 0xff;

	if (mixer == NULL || slot > 30)
		return -EIO;
	pslot = &mixer->slots[slot];
	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;
	if (!pslot->stereo)
		right = left;
	if (pslot->put_volume)
		result = pslot->put_volume(fmixer, pslot, left, right);
	if (result < 0)
		return result;
	pslot->volume[0] = left;
	pslot->volume[1] = right;
 	return (left & 0xff) | ((right & 0xff) << 8);
}

static int snd_mixer_oss_ioctl1(snd_mixer_oss_file_t *fmixer, unsigned int cmd, unsigned long arg)
{
	int tmp;

	snd_assert(fmixer != NULL, return -ENXIO);
	if (((cmd >> 8) & 0xff) == 'M') {
		switch (cmd) {
		case SOUND_MIXER_INFO:
			return snd_mixer_oss_info(fmixer, (mixer_info *)arg);
		case SOUND_OLD_MIXER_INFO:
 			return snd_mixer_oss_info_obsolete(fmixer, (_old_mixer_info *)arg);
		case SOUND_MIXER_WRITE_RECSRC:
			if (get_user(tmp, (int *)arg))
				return -EFAULT;
			tmp = snd_mixer_oss_set_recsrc(fmixer, tmp);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		case OSS_GETVERSION:
			return put_user(SNDRV_OSS_VERSION, (int *) arg);
		case SOUND_MIXER_READ_DEVMASK:
			tmp = snd_mixer_oss_devmask(fmixer);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		case SOUND_MIXER_READ_STEREODEVS:
			tmp = snd_mixer_oss_stereodevs(fmixer);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		case SOUND_MIXER_READ_RECMASK:
			tmp = snd_mixer_oss_recmask(fmixer);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		case SOUND_MIXER_READ_CAPS:
			tmp = snd_mixer_oss_caps(fmixer);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		case SOUND_MIXER_READ_RECSRC:
			tmp = snd_mixer_oss_get_recsrc(fmixer);
			if (tmp < 0)
				return tmp;
			return put_user(tmp, (int *)arg) ? -EFAULT : 0;
		}
	}
	if (cmd & SIOC_IN) {
		if (get_user(tmp, (int *)arg))
			return -EFAULT;
		tmp = snd_mixer_oss_set_volume(fmixer, cmd & 0xff, tmp);
		if (tmp < 0)
			return tmp;
		return put_user(tmp, (int *)arg) ? -EFAULT : 0;
	} else if (cmd & SIOC_OUT) {
		tmp = snd_mixer_oss_get_volume(fmixer, cmd & 0xff);
		if (tmp < 0)
			return tmp;
		return put_user(tmp, (int *)arg) ? -EFAULT : 0;
	}
	return -ENXIO;
}

int snd_mixer_oss_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return snd_mixer_oss_ioctl1((snd_mixer_oss_file_t *) file->private_data, cmd, arg);
}

int snd_mixer_oss_ioctl_card(snd_card_t *card, unsigned int cmd, unsigned long arg)
{
	snd_mixer_oss_file_t fmixer;
	
	snd_assert(card != NULL, return -ENXIO);
	if (card->mixer_oss == NULL)
		return -ENXIO;
	memset(&fmixer, 0, sizeof(fmixer));
	fmixer.card = card;
	fmixer.mixer = card->mixer_oss;
	return snd_mixer_oss_ioctl1(&fmixer, cmd, arg);
}

/*
 *  REGISTRATION PART
 */

static struct file_operations snd_mixer_oss_f_ops =
{
	.owner =	THIS_MODULE,
	.open =		snd_mixer_oss_open,
	.release =	snd_mixer_oss_release,
	.ioctl =	snd_mixer_oss_ioctl,
};

static snd_minor_t snd_mixer_oss_reg =
{
	.comment =	"mixer",
	.f_ops =	&snd_mixer_oss_f_ops,
};

/*
 *  utilities
 */

static long snd_mixer_oss_conv(long val, long omin, long omax, long nmin, long nmax)
{
	long orange = omax - omin, nrange = nmax - nmin;
	
	if (orange == 0)
		return 0;
	return ((nrange * (val - omin)) + (orange / 2)) / orange + nmin;
}

/* convert from alsa native to oss values (0-100) */
static long snd_mixer_oss_conv1(long val, long min, long max, int *old)
{
	if (val == snd_mixer_oss_conv(*old, 0, 100, min, max))
		return *old;
	return snd_mixer_oss_conv(val, min, max, 0, 100);
}

/* convert from oss to alsa native values */
static long snd_mixer_oss_conv2(long val, long min, long max)
{
	return snd_mixer_oss_conv(val, 0, 100, min, max);
}

#if 0
static void snd_mixer_oss_recsrce_set(snd_card_t *card, int slot)
{
	snd_mixer_oss_t *mixer = card->mixer_oss;
	if (mixer)
		mixer->mask_recsrc |= 1 << slot;
}

static int snd_mixer_oss_recsrce_get(snd_card_t *card, int slot)
{
	snd_mixer_oss_t *mixer = card->mixer_oss;
	if (mixer && (mixer->mask_recsrc & (1 << slot)))
		return 1;
	return 0;
}
#endif

#define SNDRV_MIXER_OSS_SIGNATURE		0x65999250

#define SNDRV_MIXER_OSS_ITEM_GLOBAL	0
#define SNDRV_MIXER_OSS_ITEM_GSWITCH	1
#define SNDRV_MIXER_OSS_ITEM_GROUTE	2
#define SNDRV_MIXER_OSS_ITEM_GVOLUME	3
#define SNDRV_MIXER_OSS_ITEM_PSWITCH	4
#define SNDRV_MIXER_OSS_ITEM_PROUTE	5
#define SNDRV_MIXER_OSS_ITEM_PVOLUME	6
#define SNDRV_MIXER_OSS_ITEM_CSWITCH	7
#define SNDRV_MIXER_OSS_ITEM_CROUTE	8
#define SNDRV_MIXER_OSS_ITEM_CVOLUME	9
#define SNDRV_MIXER_OSS_ITEM_CAPTURE	10

#define SNDRV_MIXER_OSS_ITEM_COUNT	11

#define SNDRV_MIXER_OSS_PRESENT_GLOBAL	(1<<0)
#define SNDRV_MIXER_OSS_PRESENT_GSWITCH	(1<<1)
#define SNDRV_MIXER_OSS_PRESENT_GROUTE	(1<<2)
#define SNDRV_MIXER_OSS_PRESENT_GVOLUME	(1<<3)
#define SNDRV_MIXER_OSS_PRESENT_PSWITCH	(1<<4)
#define SNDRV_MIXER_OSS_PRESENT_PROUTE	(1<<5)
#define SNDRV_MIXER_OSS_PRESENT_PVOLUME	(1<<6)
#define SNDRV_MIXER_OSS_PRESENT_CSWITCH	(1<<7)
#define SNDRV_MIXER_OSS_PRESENT_CROUTE	(1<<8)
#define SNDRV_MIXER_OSS_PRESENT_CVOLUME	(1<<9)
#define SNDRV_MIXER_OSS_PRESENT_CAPTURE	(1<<10)

struct slot {
	unsigned int signature;
	unsigned int present;
	unsigned int channels;
	snd_kcontrol_t *kcontrol[SNDRV_MIXER_OSS_ITEM_COUNT];
	unsigned int capture_item;
	struct snd_mixer_oss_assign_table *assigned;
	unsigned int allocated: 1;
};

static snd_kcontrol_t *snd_mixer_oss_test_id(snd_mixer_oss_t *mixer, const char *name, int index)
{
	snd_card_t * card = mixer->card;
	snd_ctl_elem_id_t id;
	
	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strcpy(id.name, name);
	id.index = index;
	return snd_ctl_find_id(card, &id);
}

static void snd_mixer_oss_get_volume1_vol(snd_mixer_oss_file_t *fmixer,
					  snd_mixer_oss_slot_t *pslot,
					  snd_kcontrol_t *kctl,
					  int *left, int *right)
{
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;

	snd_runtime_check(kctl != NULL, return);
	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_ATOMIC);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_ATOMIC);
	if (uinfo == NULL || uctl == NULL)
		goto __unalloc;
	snd_runtime_check(!kctl->info(kctl, uinfo), goto __unalloc);
	snd_runtime_check(!kctl->get(kctl, uctl), goto __unalloc);
	snd_runtime_check(uinfo->type != SNDRV_CTL_ELEM_TYPE_BOOLEAN || uinfo->value.integer.min != 0 || uinfo->value.integer.max != 1, return);
	*left = snd_mixer_oss_conv1(uctl->value.integer.value[0], uinfo->value.integer.min, uinfo->value.integer.max, &pslot->volume[0]);
	if (uinfo->count > 1)
		*right = snd_mixer_oss_conv1(uctl->value.integer.value[1], uinfo->value.integer.min, uinfo->value.integer.max, &pslot->volume[1]);
      __unalloc:
      	if (uctl)
      		kfree(uctl);
      	if (uinfo)
      		kfree(uinfo);
}

static void snd_mixer_oss_get_volume1_sw(snd_mixer_oss_file_t *fmixer,
					 snd_mixer_oss_slot_t *pslot,
					 snd_kcontrol_t *kctl,
					 int *left, int *right,
					 int route)
{
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;

	snd_runtime_check(kctl != NULL, return);
	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_ATOMIC);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_ATOMIC);
	if (uinfo == NULL || uctl == NULL)
		goto __unalloc;
	snd_runtime_check(!kctl->info(kctl, uinfo), goto __unalloc);
	snd_runtime_check(!kctl->get(kctl, uctl), goto __unalloc);
	if (!uctl->value.integer.value[0]) {
		*left = 0;
		if (uinfo->count == 1)
			*right = 0;
	}
	if (uinfo->count > 1 && !uctl->value.integer.value[route ? 3 : 1])
		*right = 0;
      __unalloc:
      	if (uctl)
      		kfree(uctl);
      	if (uinfo)
		kfree(uinfo);
}

static int snd_mixer_oss_get_volume1(snd_mixer_oss_file_t *fmixer,
				     snd_mixer_oss_slot_t *pslot,
				     int *left, int *right)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	
	*left = *right = 100;
	down_read(&card->controls_rwsem);
	if (slot->present & SNDRV_MIXER_OSS_PRESENT_PVOLUME) {
		snd_mixer_oss_get_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PVOLUME], left, right);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GVOLUME) {
		snd_mixer_oss_get_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GVOLUME], left, right);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GLOBAL) {
		snd_mixer_oss_get_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GLOBAL], left, right);
	}
	if (slot->present & SNDRV_MIXER_OSS_PRESENT_PSWITCH) {
		snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PSWITCH], left, right, 0);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GSWITCH) {
		snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GSWITCH], left, right, 0);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_PROUTE) {
		snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PROUTE], left, right, 1);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GROUTE) {
		snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GROUTE], left, right, 1);
	}
	up_read(&card->controls_rwsem);
	return 0;
}

static void snd_mixer_oss_put_volume1_vol(snd_mixer_oss_file_t *fmixer,
					  snd_mixer_oss_slot_t *pslot,
					  snd_kcontrol_t *kctl,
					  int left, int right)
{
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;
	int res;

	snd_runtime_check(kctl != NULL, return);
	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_ATOMIC);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_ATOMIC);
	if (uinfo == NULL || uctl == NULL)
		goto __unalloc;
	snd_runtime_check(!kctl->info(kctl, uinfo), goto __unalloc);
	snd_runtime_check(uinfo->type != SNDRV_CTL_ELEM_TYPE_BOOLEAN || uinfo->value.integer.min != 0 || uinfo->value.integer.max != 1, return);
	uctl->value.integer.value[0] = snd_mixer_oss_conv2(left, uinfo->value.integer.min, uinfo->value.integer.max);
	if (uinfo->count > 1)
		uctl->value.integer.value[1] = snd_mixer_oss_conv2(right, uinfo->value.integer.min, uinfo->value.integer.max);
	snd_runtime_check((res = kctl->put(kctl, uctl)) >= 0, goto __unalloc);
	if (res > 0)
		snd_ctl_notify(fmixer->card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
      __unalloc:
      	if (uctl)
      		kfree(uctl);
      	if (uinfo)
		kfree(uinfo);
}

static void snd_mixer_oss_put_volume1_sw(snd_mixer_oss_file_t *fmixer,
					 snd_mixer_oss_slot_t *pslot,
					 snd_kcontrol_t *kctl,
					 int left, int right,
					 int route)
{
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;
	int res;

	snd_runtime_check(kctl != NULL, return);
	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_ATOMIC);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_ATOMIC);
	if (uinfo == NULL || uctl == NULL)
		goto __unalloc;
	snd_runtime_check(!kctl->info(kctl, uinfo), goto __unalloc);
	if (uinfo->count > 1) {
		uctl->value.integer.value[0] = left > 0 ? 1 : 0;
		uctl->value.integer.value[route ? 3 : 1] = right > 0 ? 1 : 0;
		if (route) {
			uctl->value.integer.value[1] =
			uctl->value.integer.value[2] = 0;
		}
	} else {
		uctl->value.integer.value[0] = (left > 0 || right > 0) ? 1 : 0;
	}
	snd_runtime_check((res = kctl->put(kctl, uctl)) >= 0, goto __unalloc);
	if (res > 0)
		snd_ctl_notify(fmixer->card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
      __unalloc:
      	if (uctl)
      		kfree(uctl);
      	if (uinfo)
		kfree(uinfo);
}

static int snd_mixer_oss_put_volume1(snd_mixer_oss_file_t *fmixer,
				     snd_mixer_oss_slot_t *pslot,
				     int left, int right)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	
	down_read(&card->controls_rwsem);
	if (slot->present & SNDRV_MIXER_OSS_PRESENT_PVOLUME) {
		snd_mixer_oss_put_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PVOLUME], left, right);
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_CVOLUME)
			snd_mixer_oss_put_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_CVOLUME], left, right);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GVOLUME) {
		snd_mixer_oss_put_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GVOLUME], left, right);
	} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GLOBAL) {
		snd_mixer_oss_put_volume1_vol(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GLOBAL], left, right);
	}
	if (left || right) {
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_PSWITCH)
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PSWITCH], left, right, 0);
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_GSWITCH)
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GSWITCH], left, right, 0);
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_PROUTE)
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PROUTE], left, right, 1);
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_GROUTE)
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GROUTE], left, right, 1);
	} else {
		if (slot->present & SNDRV_MIXER_OSS_PRESENT_PSWITCH) {
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PSWITCH], left, right, 0);
		} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GSWITCH) {
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GSWITCH], left, right, 0);
		} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_PROUTE) {
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_PROUTE], left, right, 1);
		} else if (slot->present & SNDRV_MIXER_OSS_PRESENT_GROUTE) {
			snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_GROUTE], left, right, 1);
		}
	}
	up_read(&card->controls_rwsem);
	return 0;
}

static int snd_mixer_oss_get_recsrc1_sw(snd_mixer_oss_file_t *fmixer,
					snd_mixer_oss_slot_t *pslot,
					int *active)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	int left, right;
	
	left = right = 1;
	down_read(&card->controls_rwsem);
	snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_CSWITCH], &left, &right, 0);
	up_read(&card->controls_rwsem);
	*active = (left || right) ? 1 : 0;
	return 0;
}

static int snd_mixer_oss_get_recsrc1_route(snd_mixer_oss_file_t *fmixer,
					   snd_mixer_oss_slot_t *pslot,
					   int *active)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	int left, right;
	
	left = right = 1;
	down_read(&card->controls_rwsem);
	snd_mixer_oss_get_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_CROUTE], &left, &right, 1);
	up_read(&card->controls_rwsem);
	*active = (left || right) ? 1 : 0;
	return 0;
}

static int snd_mixer_oss_put_recsrc1_sw(snd_mixer_oss_file_t *fmixer,
					snd_mixer_oss_slot_t *pslot,
					int active)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	
	down_read(&card->controls_rwsem);
	snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_CSWITCH], active, active, 0);
	up_read(&card->controls_rwsem);
	return 0;
}

static int snd_mixer_oss_put_recsrc1_route(snd_mixer_oss_file_t *fmixer,
					   snd_mixer_oss_slot_t *pslot,
					   int active)
{
	snd_card_t *card = fmixer->card;
	struct slot *slot = (struct slot *)pslot->private_data;
	
	down_read(&card->controls_rwsem);
	snd_mixer_oss_put_volume1_sw(fmixer, pslot, slot->kcontrol[SNDRV_MIXER_OSS_ITEM_CROUTE], active, active, 1);
	up_read(&card->controls_rwsem);
	return 0;
}

static int snd_mixer_oss_get_recsrc2(snd_mixer_oss_file_t *fmixer, unsigned int *active_index)
{
	snd_card_t *card = fmixer->card;
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_kcontrol_t *kctl;
	snd_mixer_oss_slot_t *pslot;
	struct slot *slot;
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;
	int err, idx;
	
	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_KERNEL);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_KERNEL);
	if (uinfo == NULL || uctl == NULL) {
		err = -ENOMEM;
		goto __unlock;
	}
	down_read(&card->controls_rwsem);
	kctl = snd_mixer_oss_test_id(mixer, "Capture Source", 0);
	snd_runtime_check(kctl != NULL, err = -ENOENT; goto __unlock);
	snd_runtime_check(!(err = kctl->info(kctl, uinfo)), goto __unlock);
	snd_runtime_check(!(err = kctl->get(kctl, uctl)), goto __unlock);
	for (idx = 0; idx < 32; idx++) {
		if (!(mixer->mask_recsrc & (1 << idx)))
			continue;
		pslot = &mixer->slots[idx];
		slot = (struct slot *)pslot->private_data;
		if (slot->signature != SNDRV_MIXER_OSS_SIGNATURE)
			continue;
		if (!(slot->present & SNDRV_MIXER_OSS_PRESENT_CAPTURE))
			continue;
		if (slot->capture_item == uctl->value.enumerated.item[0]) {
			*active_index = idx;
			break;
		}
	}
	err = 0;
      __unlock:
     	up_read(&card->controls_rwsem);
      	if (uctl)
      		kfree(uctl);
      	if (uinfo)
      		kfree(uinfo);
      	return err;
}

static int snd_mixer_oss_put_recsrc2(snd_mixer_oss_file_t *fmixer, unsigned int active_index)
{
	snd_card_t *card = fmixer->card;
	snd_mixer_oss_t *mixer = fmixer->mixer;
	snd_kcontrol_t *kctl;
	snd_mixer_oss_slot_t *pslot;
	struct slot *slot = NULL;
	snd_ctl_elem_info_t *uinfo;
	snd_ctl_elem_value_t *uctl;
	int err;
	unsigned int idx;

	uinfo = snd_kcalloc(sizeof(*uinfo), GFP_KERNEL);
	uctl = snd_kcalloc(sizeof(*uctl), GFP_KERNEL);
	if (uinfo == NULL || uctl == NULL) {
		err = -ENOMEM;
		goto __unlock;
	}
	down_read(&card->controls_rwsem);
	kctl = snd_mixer_oss_test_id(mixer, "Capture Source", 0);
	snd_runtime_check(kctl != NULL, err = -ENOENT; goto __unlock);
	snd_runtime_check(!(err = kctl->info(kctl, uinfo)), goto __unlock);
	for (idx = 0; idx < 32; idx++) {
		if (!(mixer->mask_recsrc & (1 << idx)))
			continue;
		pslot = &mixer->slots[idx];
		slot = (struct slot *)pslot->private_data;
		if (slot->signature != SNDRV_MIXER_OSS_SIGNATURE)
			continue;
		if (!(slot->present & SNDRV_MIXER_OSS_PRESENT_CAPTURE))
			continue;
		if (idx == active_index)
			break;
		slot = NULL;
	}
	snd_runtime_check(slot != NULL, goto __unlock);
	for (idx = 0; idx < uinfo->count; idx++)
		uctl->value.enumerated.item[idx] = slot->capture_item;
	snd_runtime_check((err = kctl->put(kctl, uctl)) >= 0, );
	if (err > 0)
		snd_ctl_notify(fmixer->card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
	err = 0;
      __unlock:
	up_read(&card->controls_rwsem);
	if (uctl)
		kfree(uctl);
	if (uinfo)
		kfree(uinfo);
	return err;
}

struct snd_mixer_oss_assign_table {
	int oss_id;
	const char *name;
	int index;
};

static int snd_mixer_oss_build_test(snd_mixer_oss_t *mixer, struct slot *slot, const char *name, int index, int item)
{
	snd_ctl_elem_info_t info;
	snd_kcontrol_t *kcontrol;
	int err;

	kcontrol = snd_mixer_oss_test_id(mixer, name, index);
	if (kcontrol == NULL)
		return 0;
	snd_runtime_check((err = kcontrol->info(kcontrol, &info)) >= 0, return err);
	slot->kcontrol[item] = kcontrol;
	if (info.count > slot->channels)
		slot->channels = info.count;
	slot->present |= 1 << item;
	return 0;
}

static void snd_mixer_oss_slot_free(snd_mixer_oss_slot_t *chn)
{
	struct slot *p = (struct slot *)chn->private_data;
	if (p) {
		if (p->allocated && p->assigned) {
			kfree(p->assigned->name);
			kfree(p->assigned);
		}
		kfree(p);
	}
}

static void mixer_slot_clear(snd_mixer_oss_slot_t *rslot)
{
	int idx = rslot->number; /* remember this */
	if (rslot->private_free)
		rslot->private_free(rslot);
	memset(rslot, 0, sizeof(*rslot));
	rslot->number = idx;
}

static int snd_mixer_oss_build_input(snd_mixer_oss_t *mixer, struct snd_mixer_oss_assign_table *ptr, int ptr_allocated)
{
	struct slot slot;
	struct slot *pslot;
	snd_kcontrol_t *kctl;
	snd_mixer_oss_slot_t *rslot;
	char str[64];	
	
	memset(&slot, 0, sizeof(slot));
	if (snd_mixer_oss_build_test(mixer, &slot, ptr->name, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_GLOBAL))
		return 0;
	sprintf(str, "%s Switch", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_GSWITCH))
		return 0;
	sprintf(str, "%s Route", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_GROUTE))
		return 0;
	sprintf(str, "%s Volume", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_GVOLUME))
		return 0;
	sprintf(str, "%s Playback Switch", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_PSWITCH))
		return 0;
	sprintf(str, "%s Playback Route", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_PROUTE))
		return 0;
	sprintf(str, "%s Playback Volume", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_PVOLUME))
		return 0;
	sprintf(str, "%s Capture Switch", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_CSWITCH))
		return 0;
	sprintf(str, "%s Capture Route", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_CROUTE))
		return 0;
	sprintf(str, "%s Capture Volume", ptr->name);
	if (snd_mixer_oss_build_test(mixer, &slot, str, ptr->index,
				     SNDRV_MIXER_OSS_ITEM_CVOLUME))
		return 0;
	if (ptr->index == 0 && (kctl = snd_mixer_oss_test_id(mixer, "Capture Source", 0)) != NULL) {
		snd_ctl_elem_info_t uinfo;

		memset(&uinfo, 0, sizeof(uinfo));
		if (kctl->info(kctl, &uinfo))
			return 0;
		strcpy(str, ptr->name);
		if (!strcmp(str, "Master"))
			strcpy(str, "Mix");
		if (!strcmp(str, "Master Mono"))
			strcpy(str, "Mix Mono");
		slot.capture_item = 0;
		if (!strcmp(uinfo.value.enumerated.name, str)) {
			slot.present |= SNDRV_MIXER_OSS_PRESENT_CAPTURE;
		} else {
			for (slot.capture_item = 1; slot.capture_item < uinfo.value.enumerated.items; slot.capture_item++) {
				uinfo.value.enumerated.item = slot.capture_item;
				if (kctl->info(kctl, &uinfo))
					return 0;
				if (!strcmp(uinfo.value.enumerated.name, str)) {
					slot.present |= SNDRV_MIXER_OSS_PRESENT_CAPTURE;
					break;
				}
			}
		}
	}
	if (slot.present != 0) {
		pslot = (struct slot *)kmalloc(sizeof(slot), GFP_KERNEL);
		snd_runtime_check(pslot != NULL, return -ENOMEM);
		*pslot = slot;
		pslot->signature = SNDRV_MIXER_OSS_SIGNATURE;
		pslot->assigned = ptr;
		pslot->allocated = ptr_allocated;
		rslot = &mixer->slots[ptr->oss_id];
		mixer_slot_clear(rslot);
		rslot->stereo = slot.channels > 1 ? 1 : 0;
		rslot->get_volume = snd_mixer_oss_get_volume1;
		rslot->put_volume = snd_mixer_oss_put_volume1;
		/* note: ES18xx have both Capture Source and XX Capture Volume !!! */
		if (slot.present & SNDRV_MIXER_OSS_PRESENT_CSWITCH) {
			rslot->get_recsrc = snd_mixer_oss_get_recsrc1_sw;
			rslot->put_recsrc = snd_mixer_oss_put_recsrc1_sw;
		} else if (slot.present & SNDRV_MIXER_OSS_PRESENT_CROUTE) {
			rslot->get_recsrc = snd_mixer_oss_get_recsrc1_route;
			rslot->put_recsrc = snd_mixer_oss_put_recsrc1_route;
		} else if (slot.present & SNDRV_MIXER_OSS_PRESENT_CAPTURE) {
			mixer->mask_recsrc |= 1 << ptr->oss_id;
		}
		rslot->private_data = pslot;
		rslot->private_free = snd_mixer_oss_slot_free;
		return 1;
	}
	return 0;
}

/*
 */
#define MIXER_VOL(name) [SOUND_MIXER_##name] = #name
static char *oss_mixer_names[SNDRV_OSS_MAX_MIXERS] = {
	MIXER_VOL(VOLUME),
	MIXER_VOL(BASS),
	MIXER_VOL(TREBLE),
	MIXER_VOL(SYNTH),
	MIXER_VOL(PCM),
	MIXER_VOL(SPEAKER),
	MIXER_VOL(LINE),
	MIXER_VOL(MIC),
	MIXER_VOL(CD),
	MIXER_VOL(IMIX),
	MIXER_VOL(ALTPCM),
	MIXER_VOL(RECLEV),
	MIXER_VOL(IGAIN),
	MIXER_VOL(OGAIN),
	MIXER_VOL(LINE1),
	MIXER_VOL(LINE2),
	MIXER_VOL(LINE3),
	MIXER_VOL(DIGITAL1),
	MIXER_VOL(DIGITAL2),
	MIXER_VOL(DIGITAL3),
	MIXER_VOL(PHONEIN),
	MIXER_VOL(PHONEOUT),
	MIXER_VOL(VIDEO),
	MIXER_VOL(RADIO),
	MIXER_VOL(MONITOR),
};
	
/*
 *  /proc interface
 */

static void snd_mixer_oss_proc_read(snd_info_entry_t *entry,
				    snd_info_buffer_t * buffer)
{
	snd_mixer_oss_t *mixer = snd_magic_cast(snd_mixer_oss_t, entry->private_data, return);
	int i;

	down(&mixer->reg_mutex);
	for (i = 0; i < SNDRV_OSS_MAX_MIXERS; i++) {
		struct slot *p;

		if (! oss_mixer_names[i])
			continue;
		p = (struct slot *)mixer->slots[i].private_data;
		snd_iprintf(buffer, "%s ", oss_mixer_names[i]);
		if (p && p->assigned)
			snd_iprintf(buffer, "\"%s\" %d\n",
				    p->assigned->name,
				    p->assigned->index);
		else
			snd_iprintf(buffer, "\"\" 0\n");
	}
	up(&mixer->reg_mutex);
}

static void snd_mixer_oss_proc_write(snd_info_entry_t *entry,
				     snd_info_buffer_t * buffer)
{
	snd_mixer_oss_t *mixer = snd_magic_cast(snd_mixer_oss_t, entry->private_data, return);
	char line[128], str[32], idxstr[16], *cptr;
	int ch, idx;
	struct snd_mixer_oss_assign_table *tbl;
	struct slot *slot;

	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		cptr = snd_info_get_str(str, line, sizeof(str));
		for (ch = 0; ch < SNDRV_OSS_MAX_MIXERS; ch++)
			if (oss_mixer_names[ch] && strcmp(oss_mixer_names[ch], str) == 0)
				break;
		if (ch >= SNDRV_OSS_MAX_MIXERS) {
			snd_printk(KERN_ERR "mixer_oss: invalid OSS volume '%s'\n", str);
			continue;
		}
		cptr = snd_info_get_str(str, cptr, sizeof(str));
		if (! *str) {
			/* remove the entry */
			down(&mixer->reg_mutex);
			mixer_slot_clear(&mixer->slots[ch]);
			up(&mixer->reg_mutex);
			continue;
		}
		snd_info_get_str(idxstr, cptr, sizeof(idxstr));
		idx = simple_strtoul(idxstr, NULL, 10);
		if (idx >= 0x4000) { /* too big */
			snd_printk(KERN_ERR "mixer_oss: invalid index %d\n", idx);
			continue;
		}
		down(&mixer->reg_mutex);
		slot = (struct slot *)mixer->slots[ch].private_data;
		if (slot && slot->assigned &&
		    slot->assigned->index == idx && ! strcmp(slot->assigned->name, str))
			/* not changed */
			goto __unlock;
		tbl = kmalloc(sizeof(*tbl), GFP_KERNEL);
		if (! tbl) {
			snd_printk(KERN_ERR "mixer_oss: no memory\n");
			goto __unlock;
		}
		tbl->oss_id = ch;
		tbl->name = snd_kmalloc_strdup(str, GFP_KERNEL);
		if (! tbl->name) {
			kfree(tbl);
			goto __unlock;
		}
		tbl->index = idx;
		if (snd_mixer_oss_build_input(mixer, tbl, 1) <= 0) {
			kfree(tbl->name);
			kfree(tbl);
		}
	__unlock:
		up(&mixer->reg_mutex);
	}
}

static void snd_mixer_oss_proc_init(snd_mixer_oss_t *mixer)
{
	snd_info_entry_t *entry;

	entry = snd_info_create_card_entry(mixer->card, "oss_mixer",
					   mixer->card->proc_root);
	if (! entry)
		return;
	entry->content = SNDRV_INFO_CONTENT_TEXT;
	entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
	entry->c.text.read_size = 8192;
	entry->c.text.read = snd_mixer_oss_proc_read;
	entry->c.text.write_size = 8192;
	entry->c.text.write = snd_mixer_oss_proc_write;
	entry->private_data = mixer;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		entry = NULL;
	}
	mixer->proc_entry = entry;
}

static void snd_mixer_oss_proc_done(snd_mixer_oss_t *mixer)
{
	if (mixer->proc_entry) {
		snd_info_unregister(mixer->proc_entry);
		mixer->proc_entry = NULL;
	}
}

static void snd_mixer_oss_build(snd_mixer_oss_t *mixer)
{
	static struct snd_mixer_oss_assign_table table[] = {
		{ SOUND_MIXER_VOLUME, 	"Master",		0 },
		{ SOUND_MIXER_BASS,	"Tone Control - Bass",	0 },
		{ SOUND_MIXER_TREBLE,	"Tone Control - Treble", 0 },
		{ SOUND_MIXER_SYNTH,	"Synth",		0 },
		{ SOUND_MIXER_PCM,	"PCM",			0 },
		{ SOUND_MIXER_SPEAKER,	"PC Speaker", 		0 },
		{ SOUND_MIXER_LINE,	"Line", 		0 },
		{ SOUND_MIXER_MIC,	"Mic", 			0 },
		{ SOUND_MIXER_CD,	"CD", 			0 },
		{ SOUND_MIXER_IMIX,	"Monitor Mix", 		0 },
		{ SOUND_MIXER_ALTPCM,	"PCM",			1 },
		{ SOUND_MIXER_RECLEV,	"-- nothing --",	0 },
		{ SOUND_MIXER_IGAIN,	"Capture",		0 },
		{ SOUND_MIXER_OGAIN,	"Playback",		0 },
		{ SOUND_MIXER_LINE1,	"Aux",			0 },
		{ SOUND_MIXER_LINE2,	"Aux",			1 },
		{ SOUND_MIXER_LINE3,	"Aux",			2 },
		{ SOUND_MIXER_DIGITAL1,	"Digital",		0 },
		{ SOUND_MIXER_DIGITAL2,	"Digital",		1 },
		{ SOUND_MIXER_DIGITAL3,	"Digital",		2 },
		{ SOUND_MIXER_PHONEIN,	"Phone",		0 },
		{ SOUND_MIXER_PHONEOUT,	"Phone",		1 },
		{ SOUND_MIXER_VIDEO,	"Video",		0 },
		{ SOUND_MIXER_RADIO,	"Radio",		0 },
		{ SOUND_MIXER_MONITOR,	"Monitor",		0 }
	};
	static struct snd_mixer_oss_assign_table fm_table = {
		SOUND_MIXER_SYNTH,	"FM",			0
	};
	unsigned int idx;
	
	for (idx = 0; idx < sizeof(table) / sizeof(struct snd_mixer_oss_assign_table); idx++)
		snd_mixer_oss_build_input(mixer, &table[idx], 0);
	if (mixer->slots[SOUND_MIXER_SYNTH].get_volume == NULL)
		snd_mixer_oss_build_input(mixer, &fm_table, 0);
	if (mixer->mask_recsrc) {
		mixer->get_recsrc = snd_mixer_oss_get_recsrc2;
		mixer->put_recsrc = snd_mixer_oss_put_recsrc2;
	}
}

/*
 *
 */

static int snd_mixer_oss_free1(void *private)
{
	snd_mixer_oss_t *mixer = snd_magic_cast(snd_mixer_oss_t, private, return -ENXIO);
	snd_card_t * card;
	int idx;
 
	snd_assert(mixer != NULL, return -ENXIO);
	card = mixer->card;
	snd_assert(mixer == card->mixer_oss, return -ENXIO);
	card->mixer_oss = NULL;
	for (idx = 0; idx < SNDRV_OSS_MAX_MIXERS; idx++) {
		snd_mixer_oss_slot_t *chn = &mixer->slots[idx];
		if (chn->private_free)
			chn->private_free(chn);
	}
	snd_magic_kfree(mixer);
	return 0;
}

static int snd_mixer_oss_notify_handler(snd_card_t * card, int cmd)
{
	snd_mixer_oss_t *mixer;

	if (cmd == SND_MIXER_OSS_NOTIFY_REGISTER) {
		char name[128];
		int idx, err;

		mixer = snd_magic_kcalloc(snd_mixer_oss_t, sizeof(snd_mixer_oss_t), GFP_KERNEL);
		if (mixer == NULL)
			return -ENOMEM;
		init_MUTEX(&mixer->reg_mutex);
		sprintf(name, "mixer%i%i", card->number, 0);
		if ((err = snd_register_oss_device(SNDRV_OSS_DEVICE_TYPE_MIXER,
						   card, 0,
						   &snd_mixer_oss_reg,
						   name)) < 0) {
			snd_printk("unable to register OSS mixer device %i:%i\n", card->number, 0);
			snd_magic_kfree(mixer);
			return err;
		}
		mixer->oss_dev_alloc = 1;
		mixer->card = card;
		if (*card->mixername)
			strlcpy(mixer->name, card->mixername, sizeof(mixer->name));
		else
			strlcpy(mixer->name, name, sizeof(mixer->name));
#ifdef SNDRV_OSS_INFO_DEV_MIXERS
		snd_oss_info_register(SNDRV_OSS_INFO_DEV_MIXERS,
				      card->number,
				      mixer->name);
#endif
		for (idx = 0; idx < SNDRV_OSS_MAX_MIXERS; idx++)
			mixer->slots[idx].number = idx;
		card->mixer_oss = mixer;
		snd_mixer_oss_build(mixer);
		snd_mixer_oss_proc_init(mixer);
	} else if (cmd == SND_MIXER_OSS_NOTIFY_DISCONNECT) {
		mixer = card->mixer_oss;
		if (mixer == NULL || !mixer->oss_dev_alloc)
			return 0;
		snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIXER, mixer->card, 0);
		mixer->oss_dev_alloc = 0;
	} else {		/* free */
		mixer = card->mixer_oss;
		if (mixer == NULL)
			return 0;
#ifdef SNDRV_OSS_INFO_DEV_MIXERS
		snd_oss_info_unregister(SNDRV_OSS_INFO_DEV_MIXERS, mixer->card->number);
#endif
		if (mixer->oss_dev_alloc)
			snd_unregister_oss_device(SNDRV_OSS_DEVICE_TYPE_MIXER, mixer->card, 0);
		snd_mixer_oss_proc_done(mixer);
		return snd_mixer_oss_free1(mixer);
	}
	return 0;
}

static int __init alsa_mixer_oss_init(void)
{
	int idx;
	
	snd_mixer_oss_notify_callback = snd_mixer_oss_notify_handler;
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (snd_cards[idx])
			snd_mixer_oss_notify_handler(snd_cards[idx], SND_MIXER_OSS_NOTIFY_REGISTER);
	}
	return 0;
}

static void __exit alsa_mixer_oss_exit(void)
{
	int idx;

	snd_mixer_oss_notify_callback = NULL;
	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		if (snd_cards[idx])
			snd_mixer_oss_notify_handler(snd_cards[idx], SND_MIXER_OSS_NOTIFY_FREE);
	}
}

module_init(alsa_mixer_oss_init)
module_exit(alsa_mixer_oss_exit)

EXPORT_SYMBOL(snd_mixer_oss_ioctl_card);
