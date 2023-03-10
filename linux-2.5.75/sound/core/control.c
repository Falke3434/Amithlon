/*
 *  Routines for driver control interface
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
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/control.h>

typedef struct _snd_kctl_ioctl {
	struct list_head list;		/* list of all ioctls */
	snd_kctl_ioctl_func_t fioctl;
} snd_kctl_ioctl_t;

#define snd_kctl_ioctl(n) list_entry(n, snd_kctl_ioctl_t, list)

static DECLARE_RWSEM(snd_ioctl_rwsem);
static LIST_HEAD(snd_control_ioctls);

static int snd_ctl_open(struct inode *inode, struct file *file)
{
	int cardnum = SNDRV_MINOR_CARD(minor(inode->i_rdev));
	unsigned long flags;
	snd_card_t *card;
	snd_ctl_file_t *ctl;
	int err;

	card = snd_cards[cardnum];
	if (!card) {
		err = -ENODEV;
		goto __error1;
	}
	err = snd_card_file_add(card, file);
	if (err < 0) {
		err = -ENODEV;
		goto __error1;
	}
	if (!try_module_get(card->module)) {
		err = -EFAULT;
		goto __error2;
	}
	ctl = snd_magic_kcalloc(snd_ctl_file_t, 0, GFP_KERNEL);
	if (ctl == NULL) {
		err = -ENOMEM;
		goto __error;
	}
	INIT_LIST_HEAD(&ctl->events);
	init_waitqueue_head(&ctl->change_sleep);
	spin_lock_init(&ctl->read_lock);
	ctl->card = card;
	ctl->pid = current->pid;
	file->private_data = ctl;
	write_lock_irqsave(&card->ctl_files_rwlock, flags);
	list_add_tail(&ctl->list, &card->ctl_files);
	write_unlock_irqrestore(&card->ctl_files_rwlock, flags);
	return 0;

      __error:
	module_put(card->module);
      __error2:
	snd_card_file_remove(card, file);
      __error1:
      	return err;
}

static void snd_ctl_empty_read_queue(snd_ctl_file_t * ctl)
{
	snd_kctl_event_t *cread;
	
	spin_lock(&ctl->read_lock);
	while (!list_empty(&ctl->events)) {
		cread = snd_kctl_event(ctl->events.next);
		list_del(&cread->list);
		kfree(cread);
	}
	spin_unlock(&ctl->read_lock);
}

static int snd_ctl_release(struct inode *inode, struct file *file)
{
	unsigned long flags;
	struct list_head *list;
	snd_card_t *card;
	snd_ctl_file_t *ctl;
	snd_kcontrol_t *control;
	unsigned int idx;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	fasync_helper(-1, file, 0, &ctl->fasync);
	file->private_data = NULL;
	card = ctl->card;
	write_lock_irqsave(&card->ctl_files_rwlock, flags);
	list_del(&ctl->list);
	write_unlock_irqrestore(&card->ctl_files_rwlock, flags);
	down_write(&card->controls_rwsem);
	list_for_each(list, &card->controls) {
		control = snd_kcontrol(list);
		for (idx = 0; idx < control->count; idx++)
			if (control->vd[idx].owner == ctl)
				control->vd[idx].owner = NULL;
	}
	up_write(&card->controls_rwsem);
	snd_ctl_empty_read_queue(ctl);
	snd_magic_kfree(ctl);
	module_put(card->module);
	snd_card_file_remove(card, file);
	return 0;
}

void snd_ctl_notify(snd_card_t *card, unsigned int mask, snd_ctl_elem_id_t *id)
{
	unsigned long flags;
	struct list_head *flist;
	snd_ctl_file_t *ctl;
	snd_kctl_event_t *ev;
	
	snd_runtime_check(card != NULL && id != NULL, return);
	read_lock(&card->ctl_files_rwlock);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	card->mixer_oss_change_count++;
#endif
	list_for_each(flist, &card->ctl_files) {
		struct list_head *elist;
		ctl = snd_ctl_file(flist);
		if (!ctl->subscribed)
			continue;
		spin_lock_irqsave(&ctl->read_lock, flags);
		list_for_each(elist, &ctl->events) {
			ev = snd_kctl_event(elist);
			if (ev->id.numid == id->numid) {
				ev->mask |= mask;
				goto _found;
			}
		}
		ev = snd_kcalloc(sizeof(*ev), GFP_ATOMIC);
		if (ev) {
			ev->id = *id;
			ev->mask = mask;
			list_add_tail(&ev->list, &ctl->events);
		} else {
			snd_printk(KERN_ERR "No memory available to allocate event\n");
		}
	_found:
		wake_up(&ctl->change_sleep);
		spin_unlock_irqrestore(&ctl->read_lock, flags);
		kill_fasync(&ctl->fasync, SIGIO, POLL_IN);
	}
	read_unlock(&card->ctl_files_rwlock);
}

/**
 * snd_ctl_new - create a control instance from the template
 * @control: the control template
 * @access: the default control access
 *
 * Allocates a new snd_kcontrol_t instance and copies the given template 
 * to the new instance. It does not copy volatile data (access).
 *
 * Returns the pointer of the new instance, or NULL on failure.
 */
snd_kcontrol_t *snd_ctl_new(snd_kcontrol_t * control, unsigned int access)
{
	snd_kcontrol_t *kctl;
	unsigned int idx;
	
	snd_runtime_check(control != NULL, return NULL);
	snd_runtime_check(control->count > 0, return NULL);
	kctl = (snd_kcontrol_t *)snd_magic_kcalloc(snd_kcontrol_t,
						   sizeof(snd_kcontrol_volatile_t) * control->count,
						   GFP_KERNEL);
	if (kctl == NULL)
		return NULL;
	*kctl = *control;
	for (idx = 0; idx < kctl->count; idx++)
		kctl->vd[idx].access = access;
	return kctl;
}

/**
 * snd_ctl_new1 - create a control instance from the template
 * @ncontrol: the initialization record
 * @private_data: the private data to set
 *
 * Allocates a new snd_kcontrol_t instance and initialize from the given 
 * template.  When the access field of ncontrol is 0, it's assumed as
 * READWRITE access. When the count field is 0, it's assumes as one.
 *
 * Returns the pointer of the newly generated instance, or NULL on failure.
 */
snd_kcontrol_t *snd_ctl_new1(snd_kcontrol_new_t * ncontrol, void *private_data)
{
	snd_kcontrol_t kctl;
	unsigned int access;
	
	snd_runtime_check(ncontrol != NULL, return NULL);
	snd_assert(ncontrol->info != NULL, return NULL);
	memset(&kctl, 0, sizeof(kctl));
	kctl.id.iface = ncontrol->iface;
	kctl.id.device = ncontrol->device;
	kctl.id.subdevice = ncontrol->subdevice;
	if (ncontrol->name)
		strlcpy(kctl.id.name, ncontrol->name, sizeof(kctl.id.name));
	kctl.id.index = ncontrol->index;
	kctl.count = ncontrol->count ? ncontrol->count : 1;
	access = ncontrol->access == 0 ? SNDRV_CTL_ELEM_ACCESS_READWRITE :
		 (ncontrol->access & (SNDRV_CTL_ELEM_ACCESS_READWRITE|SNDRV_CTL_ELEM_ACCESS_INACTIVE|
		 		      SNDRV_CTL_ELEM_ACCESS_DINDIRECT|SNDRV_CTL_ELEM_ACCESS_INDIRECT));
	kctl.info = ncontrol->info;
	kctl.get = ncontrol->get;
	kctl.put = ncontrol->put;
	kctl.private_value = ncontrol->private_value;
	kctl.private_data = private_data;
	return snd_ctl_new(&kctl, access);
}

/**
 * snd_ctl_free_one - release the control instance
 * @kcontrol: the control instance
 *
 * Releases the control instance created via snd_ctl_new()
 * or snd_ctl_new1().
 * Don't call this after the control was added to the card.
 */
void snd_ctl_free_one(snd_kcontrol_t * kcontrol)
{
	if (kcontrol) {
		if (kcontrol->private_free)
			kcontrol->private_free(kcontrol);
		snd_magic_kfree(kcontrol);
	}
}

/**
 * snd_ctl_add - add the control instance to the card
 * @card: the card instance
 * @kcontrol: the control instance to add
 *
 * Adds the control instance created via snd_ctl_new() or
 * snd_ctl_new1() to the given card.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ctl_add(snd_card_t * card, snd_kcontrol_t * kcontrol)
{
	snd_ctl_elem_id_t id;
	unsigned int idx;

	snd_runtime_check(card != NULL && kcontrol != NULL, return -EINVAL);
	snd_assert(kcontrol->info != NULL, return -EINVAL);
	down_write(&card->controls_rwsem);
	list_add_tail(&kcontrol->list, &card->controls);
	card->controls_count += kcontrol->count;
	kcontrol->id.numid = card->last_numid + 1;
	card->last_numid += kcontrol->count;
	up_write(&card->controls_rwsem);
	id = kcontrol->id;
	for (idx = 0; idx < kcontrol->count; idx++, id.index++, id.numid++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_ADD, &id);
	return 0;
}

/**
 * snd_ctl_remove - remove the control from the card and release it
 * @card: the card instance
 * @kcontrol: the control instance to remove
 *
 * Removes the control from the card and then releases the instance.
 * You don't need to call snd_ctl_free_one().
 * 
 * Returns 0 if successful, or a negative error code on failure.
 */
int snd_ctl_remove(snd_card_t * card, snd_kcontrol_t * kcontrol)
{
	snd_ctl_elem_id_t id;
	unsigned int idx;

	snd_runtime_check(card != NULL && kcontrol != NULL, return -EINVAL);
	down_write(&card->controls_rwsem);
	list_del(&kcontrol->list);
	card->controls_count -= kcontrol->count;
	up_write(&card->controls_rwsem);
	id = kcontrol->id;
	for (idx = 0; idx < kcontrol->count; idx++, id.index++, id.numid++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_REMOVE, &id);
	snd_ctl_free_one(kcontrol);
	return 0;
}

/**
 * snd_ctl_remove_id - remove the control of the given id and release it
 * @card: the card instance
 * @id: the control id to remove
 *
 * Finds the control instance with the given id, removes it from the
 * card list and releases it.
 * 
 * Returns 0 if successful, or a negative error code on failure.
 */
int snd_ctl_remove_id(snd_card_t * card, snd_ctl_elem_id_t *id)
{
	snd_kcontrol_t *kctl;

	kctl = snd_ctl_find_id(card, id);
	if (kctl == NULL)
		return -ENOENT;
	return snd_ctl_remove(card, kctl);
}

static snd_kcontrol_t *_ctl_find_id
(snd_card_t * card, snd_ctl_elem_id_t *id); /* w/o lock */

/**
 * snd_ctl_rename_id - replace the id of a control on the card
 * @card: the card instance
 * @src_id: the old id
 * @dst_id: the new id
 *
 * Finds the control with the old id from the card, and replaces the
 * id with the new one.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ctl_rename_id(snd_card_t * card, snd_ctl_elem_id_t *src_id, snd_ctl_elem_id_t *dst_id)
{
	snd_kcontrol_t *kctl;

	down_write(&card->controls_rwsem);
	kctl = _ctl_find_id(card, src_id);
	if (kctl == NULL) {
		up_write(&card->controls_rwsem);
		return -ENOENT;
	}
	kctl->id = *dst_id;
	kctl->id.numid = card->last_numid + 1;
	card->last_numid += kctl->count;
	up_write(&card->controls_rwsem);
	return 0;
}

static snd_kcontrol_t *_ctl_find_numid(snd_card_t * card, unsigned int numid)
{
	struct list_head *list;
	snd_kcontrol_t *kctl;

	snd_runtime_check(card != NULL && numid != 0, return NULL);
	list_for_each(list, &card->controls) {
		kctl = snd_kcontrol(list);
		if (kctl->id.numid <= numid && kctl->id.numid + kctl->count > numid)
			return kctl;
	}
	return NULL;
}

static snd_kcontrol_t *_ctl_find_id(snd_card_t * card, snd_ctl_elem_id_t *id)
{
	struct list_head *list;
	snd_kcontrol_t *kctl;

	snd_runtime_check(card != NULL && id != NULL, return NULL);
	if (id->numid != 0)
		return _ctl_find_numid(card, id->numid);
	list_for_each(list, &card->controls) {
		kctl = snd_kcontrol(list);
		if (kctl->id.iface != id->iface)
			continue;
		if (kctl->id.device != id->device)
			continue;
		if (kctl->id.subdevice != id->subdevice)
			continue;
		if (strncmp(kctl->id.name, id->name, sizeof(kctl->id.name)))
			continue;
		if (kctl->id.index > id->index)
			continue;
		if (kctl->id.index + kctl->count <= id->index)
			continue;
		return kctl;
	}
	return NULL;
}

/**
 * snd_ctl_find_id - find the control instance with the given id
 * @card: the card instance
 * @id: the id to search
 *
 * Finds the control instance with the given id from the card.
 *
 * Returns the pointer of the instance if found, or NULL if not.
 */
snd_kcontrol_t *snd_ctl_find_id(snd_card_t * card, snd_ctl_elem_id_t *id)
{
	snd_kcontrol_t *kctl;
	down_read(&card->controls_rwsem);
	kctl = _ctl_find_id(card, id);
	up_read(&card->controls_rwsem);
	return kctl;
}

/**
 * snd_ctl_find_numid - find the control instance with the given number-id
 * @card: the card instance
 * @numid: the number-id to search
 *
 * Finds the control instance with the given number-id from the card.
 *
 * Returns the pointer of the instance if found, or NULL if not.
 */
snd_kcontrol_t *snd_ctl_find_numid(snd_card_t * card, unsigned int numid)
{
	snd_kcontrol_t *kctl;
	down_read(&card->controls_rwsem);
	kctl = _ctl_find_numid(card, numid);
	up_read(&card->controls_rwsem);
	return kctl;
}

static int snd_ctl_card_info(snd_card_t * card, snd_ctl_file_t * ctl,
			     unsigned int cmd, unsigned long arg)
{
	snd_ctl_card_info_t info;

	memset(&info, 0, sizeof(info));
	down_read(&snd_ioctl_rwsem);
	info.card = card->number;
	strlcpy(info.id, card->id, sizeof(info.id));
	strlcpy(info.driver, card->driver, sizeof(info.driver));
	strlcpy(info.name, card->shortname, sizeof(info.name));
	strlcpy(info.longname, card->longname, sizeof(info.longname));
	strlcpy(info.mixername, card->mixername, sizeof(info.mixername));
	strlcpy(info.components, card->components, sizeof(info.components));
	up_read(&snd_ioctl_rwsem);
	if (copy_to_user((void *) arg, &info, sizeof(snd_ctl_card_info_t)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_list(snd_card_t *card, snd_ctl_elem_list_t *_list)
{
	struct list_head *plist;
	snd_ctl_elem_list_t list;
	snd_kcontrol_t *kctl;
	snd_ctl_elem_id_t *dst, *id;
	unsigned int offset, space, first, jidx;
	
	if (copy_from_user(&list, _list, sizeof(list)))
		return -EFAULT;
	offset = list.offset;
	space = list.space;
	first = 0;
	/* try limit maximum space */
	if (space > 16384)
		return -ENOMEM;
	if (space > 0) {
		/* allocate temporary buffer for atomic operation */
		dst = vmalloc(space * sizeof(snd_ctl_elem_id_t));
		if (dst == NULL)
			return -ENOMEM;
		down_read(&card->controls_rwsem);
		list.count = card->controls_count;
		plist = card->controls.next;
		while (plist != &card->controls) {
			if (offset == 0)
				break;
			kctl = snd_kcontrol(plist);
			if (offset < kctl->count)
				break;
			offset -= kctl->count;
			plist = plist->next;
		}
		list.used = 0;
		id = dst;
		while (space > 0 && plist != &card->controls) {
			kctl = snd_kcontrol(plist);
			for (jidx = offset; space > 0 && jidx < kctl->count; jidx++) {
				snd_ctl_build_ioff(id, kctl, jidx);
				id++;
				space--;
				list.used++;
			}
			plist = plist->next;
			offset = 0;
		}
		up_read(&card->controls_rwsem);
		if (list.used > 0 && copy_to_user(list.pids, dst, list.used * sizeof(snd_ctl_elem_id_t)))
			return -EFAULT;
		vfree(dst);
	} else {
		down_read(&card->controls_rwsem);
		list.count = card->controls_count;
		up_read(&card->controls_rwsem);
	}
	if (copy_to_user(_list, &list, sizeof(list)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_info(snd_ctl_file_t *ctl, snd_ctl_elem_info_t *_info)
{
	snd_card_t *card = ctl->card;
	snd_ctl_elem_info_t info;
	snd_kcontrol_t *kctl;
	snd_kcontrol_volatile_t *vd;
	unsigned int index_offset;
	int result;
	
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	down_read(&card->controls_rwsem);
	kctl = _ctl_find_id(card, &info.id);
	if (kctl == NULL) {
		up_read(&card->controls_rwsem);
		return -ENOENT;
	}
#ifdef CONFIG_SND_DEBUG
	info.access = 0;
#endif
	result = kctl->info(kctl, &info);
	if (result >= 0) {
		snd_assert(info.access == 0, );
		index_offset = snd_ctl_get_ioff(kctl, &info.id);
		vd = &kctl->vd[index_offset];
		snd_ctl_build_ioff(&info.id, kctl, index_offset);
		info.access = vd->access;
		if (vd->owner) {
			info.access |= SNDRV_CTL_ELEM_ACCESS_LOCK;
			if (vd->owner == ctl)
				info.access |= SNDRV_CTL_ELEM_ACCESS_OWNER;
			info.owner = vd->owner_pid;
		} else {
			info.owner = -1;
		}
	}
	up_read(&card->controls_rwsem);
	if (result >= 0)
		if (copy_to_user(_info, &info, sizeof(info)))
			return -EFAULT;
	return result;
}

static int snd_ctl_elem_read(snd_card_t *card, snd_ctl_elem_value_t *_control)
{

	snd_ctl_elem_value_t *control;
	snd_kcontrol_t *kctl;
	snd_kcontrol_volatile_t *vd;
	unsigned int index_offset;
	int result, indirect;
	
	control = kmalloc(sizeof(*control), GFP_KERNEL);
	if (control == NULL)
		return -ENOMEM;	
	if (copy_from_user(control, _control, sizeof(*control)))
		return -EFAULT;
	down_read(&card->controls_rwsem);
	kctl = _ctl_find_id(card, &control->id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		index_offset = snd_ctl_get_ioff(kctl, &control->id);
		vd = &kctl->vd[index_offset];
		indirect = vd->access & SNDRV_CTL_ELEM_ACCESS_INDIRECT ? 1 : 0;
		if (control->indirect != indirect) {
			result = -EACCES;
		} else {
			if ((vd->access & SNDRV_CTL_ELEM_ACCESS_READ) && kctl->get != NULL) {
				snd_ctl_build_ioff(&control->id, kctl, index_offset);
				result = kctl->get(kctl, control);
			} else {
				result = -EPERM;
			}
		}
	}
	up_read(&card->controls_rwsem);
	if (result >= 0)
		if (copy_to_user(_control, control, sizeof(*control)))
			return -EFAULT;
	kfree(control);
	return result;
}

static int snd_ctl_elem_write(snd_ctl_file_t *file, snd_ctl_elem_value_t *_control)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_value_t *control;
	snd_kcontrol_t *kctl;
	snd_kcontrol_volatile_t *vd;
	unsigned int index_offset;
	int result, indirect;

	control = kmalloc(sizeof(*control), GFP_KERNEL);
	if (control == NULL)
		return -ENOMEM;	
	if (copy_from_user(control, _control, sizeof(*control)))
		return -EFAULT;
	down_read(&card->controls_rwsem);
	kctl = _ctl_find_id(card, &control->id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		index_offset = snd_ctl_get_ioff(kctl, &control->id);
		vd = &kctl->vd[index_offset];
		indirect = vd->access & SNDRV_CTL_ELEM_ACCESS_INDIRECT ? 1 : 0;
		if (control->indirect != indirect) {
			result = -EACCES;
		} else {
			if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
			    kctl->put == NULL ||
			    (vd->owner != NULL && vd->owner != file)) {
				result = -EPERM;
			} else {
				snd_ctl_build_ioff(&control->id, kctl, index_offset);
				result = kctl->put(kctl, control);
			}
			if (result > 0) {
				up_read(&card->controls_rwsem);
				snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE, &control->id);
				result = 0;
				goto __unlocked;
			}
		}
	}
	up_read(&card->controls_rwsem);
      __unlocked:
	if (result >= 0)
		if (copy_to_user(_control, control, sizeof(*control)))
			return -EFAULT;
	kfree(control);
	return result;
}

static int snd_ctl_elem_lock(snd_ctl_file_t *file, snd_ctl_elem_id_t *_id)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_id_t id;
	snd_kcontrol_t *kctl;
	snd_kcontrol_volatile_t *vd;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	down_write(&card->controls_rwsem);
	kctl = _ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
		if (vd->owner != NULL)
			result = -EBUSY;
		else {
			vd->owner = file;
			vd->owner_pid = current->pid;
			result = 0;
		}
	}
	up_write(&card->controls_rwsem);
	return result;
}

static int snd_ctl_elem_unlock(snd_ctl_file_t *file, snd_ctl_elem_id_t *_id)
{
	snd_card_t *card = file->card;
	snd_ctl_elem_id_t id;
	snd_kcontrol_t *kctl;
	snd_kcontrol_volatile_t *vd;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	down_write(&card->controls_rwsem);
	kctl = _ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
		if (vd->owner == NULL)
			result = -EINVAL;
		else if (vd->owner != file)
			result = -EPERM;
		else {
			vd->owner = NULL;
			vd->owner_pid = 0;
			result = 0;
		}
	}
	up_write(&card->controls_rwsem);
	return result;
}

static int snd_ctl_subscribe_events(snd_ctl_file_t *file, int *ptr)
{
	int subscribe;
	if (get_user(subscribe, ptr))
		return -EFAULT;
	if (subscribe < 0) {
		subscribe = file->subscribed;
		if (put_user(subscribe, ptr))
			return -EFAULT;
		return 0;
	}
	if (subscribe) {
		file->subscribed = 1;
		return 0;
	} else if (file->subscribed) {
		snd_ctl_empty_read_queue(file);
		file->subscribed = 0;
	}
	return 0;
}

static int snd_ctl_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	snd_ctl_file_t *ctl;
	snd_card_t *card;
	struct list_head *list;
	snd_kctl_ioctl_t *p;
	int err;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	card = ctl->card;
	snd_assert(card != NULL, return -ENXIO);
	switch (cmd) {
	case SNDRV_CTL_IOCTL_PVERSION:
		return put_user(SNDRV_CTL_VERSION, (int *)arg) ? -EFAULT : 0;
	case SNDRV_CTL_IOCTL_CARD_INFO:
		return snd_ctl_card_info(card, ctl, cmd, arg);
	case SNDRV_CTL_IOCTL_ELEM_LIST:
		return snd_ctl_elem_list(ctl->card, (snd_ctl_elem_list_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_INFO:
		return snd_ctl_elem_info(ctl, (snd_ctl_elem_info_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_READ:
		return snd_ctl_elem_read(ctl->card, (snd_ctl_elem_value_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_WRITE:
		return snd_ctl_elem_write(ctl, (snd_ctl_elem_value_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_LOCK:
		return snd_ctl_elem_lock(ctl, (snd_ctl_elem_id_t *) arg);
	case SNDRV_CTL_IOCTL_ELEM_UNLOCK:
		return snd_ctl_elem_unlock(ctl, (snd_ctl_elem_id_t *) arg);
	case SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS:
		return snd_ctl_subscribe_events(ctl, (int *) arg);
	case SNDRV_CTL_IOCTL_POWER:
		if (get_user(err, (int *)arg))
			return -EFAULT;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
#ifdef CONFIG_PM
		if (card->set_power_state) {
			snd_power_lock(card);
			err = card->set_power_state(card, err);
			snd_power_unlock(card);
		} else
#endif
			err = -ENOPROTOOPT;
		return err;
	case SNDRV_CTL_IOCTL_POWER_STATE:
#ifdef CONFIG_PM
		return put_user(card->power_state, (int *)arg) ? -EFAULT : 0;
#else
		return put_user(SNDRV_CTL_POWER_D0, (int *)arg) ? -EFAULT : 0;
#endif
	}
	down_read(&snd_ioctl_rwsem);
	list_for_each(list, &snd_control_ioctls) {
		p = list_entry(list, snd_kctl_ioctl_t, list);
		err = p->fioctl(card, ctl, cmd, arg);
		if (err != -ENOIOCTLCMD) {
			up_read(&snd_ioctl_rwsem);
			return err;
		}
	}
	up_read(&snd_ioctl_rwsem);
	snd_printd("unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static ssize_t snd_ctl_read(struct file *file, char *buffer, size_t count, loff_t * offset)
{
	snd_ctl_file_t *ctl;
	int err = 0;
	ssize_t result = 0;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	snd_assert(ctl != NULL && ctl->card != NULL, return -ENXIO);
	if (!ctl->subscribed)
		return -EBADFD;
	if (count < sizeof(snd_ctl_event_t))
		return -EINVAL;
	spin_lock_irq(&ctl->read_lock);
	while (count >= sizeof(snd_ctl_event_t)) {
		snd_ctl_event_t ev;
		snd_kctl_event_t *kev;
		while (list_empty(&ctl->events)) {
			wait_queue_t wait;
			if ((file->f_flags & O_NONBLOCK) != 0 || result > 0) {
				err = -EAGAIN;
				goto __end;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&ctl->change_sleep, &wait);
			spin_unlock_irq(&ctl->read_lock);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&ctl->change_sleep, &wait);
			if (signal_pending(current))
				return result > 0 ? result : -ERESTARTSYS;
			spin_lock_irq(&ctl->read_lock);
		}
		kev = snd_kctl_event(ctl->events.next);
		ev.type = SNDRV_CTL_EVENT_ELEM;
		ev.data.elem.mask = kev->mask;
		ev.data.elem.id = kev->id;
		list_del(&kev->list);
		spin_unlock_irq(&ctl->read_lock);
		kfree(kev);
		if (copy_to_user(buffer, &ev, sizeof(snd_ctl_event_t))) {
			err = -EFAULT;
			goto __end;
		}
		spin_lock_irq(&ctl->read_lock);
		buffer += sizeof(snd_ctl_event_t);
		count -= sizeof(snd_ctl_event_t);
		result += sizeof(snd_ctl_event_t);
	}
      __end:
	spin_unlock_irq(&ctl->read_lock);
      	return result > 0 ? result : err;
}

static unsigned int snd_ctl_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	snd_ctl_file_t *ctl;

	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return 0);
	if (!ctl->subscribed)
		return 0;
	poll_wait(file, &ctl->change_sleep, wait);

	mask = 0;
	if (!list_empty(&ctl->events))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

/*
 * register the device-specific control-ioctls.
 * called from each device manager like pcm.c, hwdep.c, etc.
 */
int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn)
{
	snd_kctl_ioctl_t *pn;

	pn = (snd_kctl_ioctl_t *)
		snd_kcalloc(sizeof(snd_kctl_ioctl_t), GFP_KERNEL);
	if (pn == NULL)
		return -ENOMEM;
	pn->fioctl = fcn;
	down_write(&snd_ioctl_rwsem);
	list_add_tail(&pn->list, &snd_control_ioctls);
	up_write(&snd_ioctl_rwsem);
	return 0;
}

/*
 * de-register the device-specific control-ioctls.
 */
int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn)
{
	struct list_head *list;
	snd_kctl_ioctl_t *p;

	snd_runtime_check(fcn != NULL, return -EINVAL);
	down_write(&snd_ioctl_rwsem);
	list_for_each(list, &snd_control_ioctls) {
		p = list_entry(list, snd_kctl_ioctl_t, list);
		if (p->fioctl == fcn) {
			list_del(&p->list);
			up_write(&snd_ioctl_rwsem);
			kfree(p);
			return 0;
		}
	}
	up_write(&snd_ioctl_rwsem);
	snd_BUG();
	return -EINVAL;
}

static int snd_ctl_fasync(int fd, struct file * file, int on)
{
	snd_ctl_file_t *ctl;
	int err;
	ctl = snd_magic_cast(snd_ctl_file_t, file->private_data, return -ENXIO);
	err = fasync_helper(fd, file, on, &ctl->fasync);
	if (err < 0)
		return err;
	return 0;
}

/*
 *  INIT PART
 */

static struct file_operations snd_ctl_f_ops =
{
	.owner =	THIS_MODULE,
	.read =		snd_ctl_read,
	.open =		snd_ctl_open,
	.release =	snd_ctl_release,
	.poll =		snd_ctl_poll,
	.ioctl =	snd_ctl_ioctl,
	.fasync =	snd_ctl_fasync,
};

static snd_minor_t snd_ctl_reg =
{
	.comment =	"ctl",
	.f_ops =	&snd_ctl_f_ops,
};

/*
 * registration of the control device:
 * called from init.c
 */
int snd_ctl_register(snd_card_t *card)
{
	int err, cardnum;
	char name[16];

	snd_assert(card != NULL, return -ENXIO);
	cardnum = card->number;
	snd_assert(cardnum >= 0 && cardnum < SNDRV_CARDS, return -ENXIO);
	sprintf(name, "controlC%i", cardnum);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_CONTROL,
					card, 0, &snd_ctl_reg, name)) < 0)
		return err;
	return 0;
}

/*
 * disconnection of the control device:
 * called from init.c
 */
int snd_ctl_disconnect(snd_card_t *card)
{
	struct list_head *flist;
	snd_ctl_file_t *ctl;

	down_read(&card->controls_rwsem);
	list_for_each(flist, &card->ctl_files) {
		ctl = snd_ctl_file(flist);
		wake_up(&ctl->change_sleep);
		kill_fasync(&ctl->fasync, SIGIO, POLL_ERR);
	}
	up_read(&card->controls_rwsem);
	return 0;
}

/*
 * de-registration of the control device:
 * called from init.c
 */
int snd_ctl_unregister(snd_card_t *card)
{
	int err, cardnum;
	snd_kcontrol_t *control;

	snd_assert(card != NULL, return -ENXIO);
	cardnum = card->number;
	snd_assert(cardnum >= 0 && cardnum < SNDRV_CARDS, return -ENXIO);
	if ((err = snd_unregister_device(SNDRV_DEVICE_TYPE_CONTROL, card, 0)) < 0)
		return err;
	while (!list_empty(&card->controls)) {
		control = snd_kcontrol(card->controls.next);
		snd_ctl_remove(card, control);
	}
	return 0;
}
