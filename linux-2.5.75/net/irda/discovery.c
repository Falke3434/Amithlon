/*********************************************************************
 *                
 * Filename:      discovery.c
 * Version:       0.1
 * Description:   Routines for handling discoveries at the IrLMP layer
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Apr  6 15:33:50 1999
 * Modified at:   Sat Oct  9 17:11:31 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Fri May 28  3:11 CST 1999
 * Modified by:   Horst von Brand <vonbrand@sleipnir.valparaiso.cl>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/string.h>
#include <linux/socket.h>

#include <net/irda/irda.h>
#include <net/irda/irlmp.h>

#include <net/irda/discovery.h>

/*
 * Function irlmp_add_discovery (cachelog, discovery)
 *
 *    Add a new discovery to the cachelog, and remove any old discoveries
 *    from the same device
 *
 * Note : we try to preserve the time this device was *first* discovered
 * (as opposed to the time of last discovery used for cleanup). This is
 * used by clients waiting for discovery events to tell if the device
 * discovered is "new" or just the same old one. They can't rely there
 * on a binary flag (new/old), because not all discovery events are
 * propagated to them, and they might not always listen, so they would
 * miss some new devices popping up...
 * Jean II
 */
void irlmp_add_discovery(hashbin_t *cachelog, discovery_t *new)
{
	discovery_t *discovery, *node;
	unsigned long flags;

	/* Set time of first discovery if node is new (see below) */
	new->firststamp = new->timestamp;

	spin_lock_irqsave(&cachelog->hb_spinlock, flags);

	/* 
	 * Remove all discoveries of devices that has previously been 
	 * discovered on the same link with the same name (info), or the 
	 * same daddr. We do this since some devices (mostly PDAs) change
	 * their device address between every discovery.
	 */
	discovery = (discovery_t *) hashbin_get_first(cachelog);
	while (discovery != NULL ) {
		node = discovery;

		/* Be sure to stay one item ahead */
		discovery = (discovery_t *) hashbin_get_next(cachelog);

		if ((node->data.saddr == new->data.saddr) &&
		    ((node->data.daddr == new->data.daddr) || 
		     (strcmp(node->data.info, new->data.info) == 0)))
		{
			/* This discovery is a previous discovery 
			 * from the same device, so just remove it
			 */
			hashbin_remove_this(cachelog, (irda_queue_t *) node);
			/* Check if hints bits are unchanged */
			if(u16ho(node->data.hints) == u16ho(new->data.hints))
				/* Set time of first discovery for this node */
				new->firststamp = node->firststamp;
			kfree(node);
		}
	}

	/* Insert the new and updated version */
	hashbin_insert(cachelog, (irda_queue_t *) new, new->data.daddr, NULL);

	spin_unlock_irqrestore(&cachelog->hb_spinlock, flags);
}

/*
 * Function irlmp_add_discovery_log (cachelog, log)
 *
 *    Merge a disovery log into the cachelog.
 *
 */
void irlmp_add_discovery_log(hashbin_t *cachelog, hashbin_t *log)
{
	discovery_t *discovery;

	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	/*
	 *  If log is missing this means that IrLAP was unable to perform the
	 *  discovery, so restart discovery again with just the half timeout
	 *  of the normal one.
	 */
	/* Well... It means that there was nobody out there - Jean II */
	if (log == NULL) {
		/* irlmp_start_discovery_timer(irlmp, 150); */
		return;
	}

	/*
	 * Locking : we are the only owner of this discovery log, so
	 * no need to lock it.
	 * We just need to lock the global log in irlmp_add_discovery().
	 */
	discovery = (discovery_t *) hashbin_remove_first(log);
	while (discovery != NULL) {
		irlmp_add_discovery(cachelog, discovery);

		discovery = (discovery_t *) hashbin_remove_first(log);
	}
	
	/* Delete the now empty log */
	hashbin_delete(log, (FREE_FUNC) kfree);
}

/*
 * Function irlmp_expire_discoveries (log, saddr, force)
 *
 *    Go through all discoveries and expire all that has stayed too long
 *
 * Note : this assume that IrLAP won't change its saddr, which
 * currently is a valid assumption...
 */
void irlmp_expire_discoveries(hashbin_t *log, __u32 saddr, int force)
{
	discovery_t *		discovery;
	discovery_t *		curr;
	unsigned long		flags;
	discinfo_t *		buffer = NULL;
	int			n;		/* Size of the full log */
	int			i = 0;		/* How many we expired */

	ASSERT(log != NULL, return;);
	IRDA_DEBUG(4, "%s()\n", __FUNCTION__);

	spin_lock_irqsave(&log->hb_spinlock, flags);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		/* Be sure to be one item ahead */
		curr = discovery;
		discovery = (discovery_t *) hashbin_get_next(log);

		/* Test if it's time to expire this discovery */
		if ((curr->data.saddr == saddr) &&
		    (force ||
		     ((jiffies - curr->timestamp) > DISCOVERY_EXPIRE_TIMEOUT)))
		{
			/* Create buffer as needed.
			 * As this function get called a lot and most time
			 * we don't have anything to put in the log (we are
			 * quite picky), we can save a lot of overhead
			 * by not calling kmalloc. Jean II */
			if(buffer == NULL) {
				/* Create the client specific buffer */
				n = HASHBIN_GET_SIZE(log);
				buffer = kmalloc(n * sizeof(struct irda_device_info), GFP_ATOMIC);
				if (buffer == NULL) {
					spin_unlock_irqrestore(&log->hb_spinlock, flags);
					return;
				}

			}

			/* Copy discovery information */
			memcpy(&(buffer[i]), &(curr->data),
			       sizeof(discinfo_t));
			i++;

			/* Remove it from the log */
			curr = hashbin_remove_this(log, (irda_queue_t *) curr);
			if (curr)
				kfree(curr);
		}
	}

	/* Drop the spinlock before calling the higher layers, as
	 * we can't guarantee they won't call us back and create a
	 * deadlock. We will work on our own private data, so we
	 * don't care to be interupted. - Jean II */
	spin_unlock_irqrestore(&log->hb_spinlock, flags);

	if(buffer == NULL)
		return;

	/* Tell IrLMP and registered clients about it */
	irlmp_discovery_expiry(buffer, i);

	/* Free up our buffer */
	kfree(buffer);
}

#if 0
/*
 * Function irlmp_dump_discoveries (log)
 *
 *    Print out all discoveries in log
 *
 */
void irlmp_dump_discoveries(hashbin_t *log)
{
	discovery_t *discovery;

	ASSERT(log != NULL, return;);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		IRDA_DEBUG(0, "Discovery:\n");
		IRDA_DEBUG(0, "  daddr=%08x\n", discovery->data.daddr);
		IRDA_DEBUG(0, "  saddr=%08x\n", discovery->data.saddr); 
		IRDA_DEBUG(0, "  nickname=%s\n", discovery->data.info);

		discovery = (discovery_t *) hashbin_get_next(log);
	}
}
#endif

/*
 * Function irlmp_copy_discoveries (log, pn, mask)
 *
 *    Copy all discoveries in a buffer
 *
 * This function implement a safe way for lmp clients to access the
 * discovery log. The basic problem is that we don't want the log
 * to change (add/remove) while the client is reading it. If the
 * lmp client manipulate directly the hashbin, he is sure to get
 * into troubles...
 * The idea is that we copy all the current discovery log in a buffer
 * which is specific to the client and pass this copy to him. As we
 * do this operation with the spinlock grabbed, we are safe...
 * Note : we don't want those clients to grab the spinlock, because
 * we have no control on how long they will hold it...
 * Note : we choose to copy the log in "struct irda_device_info" to
 * save space...
 * Note : the client must kfree himself() the log...
 * Jean II
 */
struct irda_device_info *irlmp_copy_discoveries(hashbin_t *log, int *pn,
						__u16 mask, int old_entries)
{
	discovery_t *		discovery;
	unsigned long		flags;
	discinfo_t *		buffer = NULL;
	int			j_timeout = (sysctl_discovery_timeout * HZ);
	int			n;		/* Size of the full log */
	int			i = 0;		/* How many we picked */

	ASSERT(pn != NULL, return NULL;);
	ASSERT(log != NULL, return NULL;);

	/* Save spin lock */
	spin_lock_irqsave(&log->hb_spinlock, flags);

	discovery = (discovery_t *) hashbin_get_first(log);
	while (discovery != NULL) {
		/* Mask out the ones we don't want :
		 * We want to match the discovery mask, and to get only
		 * the most recent one (unless we want old ones) */
		if ((u16ho(discovery->data.hints) & mask) &&
		    ((old_entries) ||
		     ((jiffies - discovery->firststamp) < j_timeout)) ) {
			/* Create buffer as needed.
			 * As this function get called a lot and most time
			 * we don't have anything to put in the log (we are
			 * quite picky), we can save a lot of overhead
			 * by not calling kmalloc. Jean II */
			if(buffer == NULL) {
				/* Create the client specific buffer */
				n = HASHBIN_GET_SIZE(log);
				buffer = kmalloc(n * sizeof(struct irda_device_info), GFP_ATOMIC);
				if (buffer == NULL) {
					spin_unlock_irqrestore(&log->hb_spinlock, flags);
					return NULL;
				}

			}

			/* Copy discovery information */
			memcpy(&(buffer[i]), &(discovery->data),
			       sizeof(discinfo_t));
			i++;
		}
		discovery = (discovery_t *) hashbin_get_next(log);
	}

	spin_unlock_irqrestore(&log->hb_spinlock, flags);

	/* Get the actual number of device in the buffer and return */
	*pn = i;
	return(buffer);
}

/*
 * Function irlmp_find_device (name, saddr)
 *
 *    Look through the discovery log at each of the links and try to find 
 *    the device with the given name. Return daddr and saddr. If saddr is
 *    specified, that look at that particular link only (not impl).
 */
__u32 irlmp_find_device(hashbin_t *cachelog, char *name, __u32 *saddr)
{
	unsigned long flags;
	discovery_t *d;

	spin_lock_irqsave(&cachelog->hb_spinlock, flags);

	/* Look at all discoveries for that link */
	d = (discovery_t *) hashbin_get_first(cachelog);
	while (d != NULL) {
		IRDA_DEBUG(1, "Discovery:\n");
		IRDA_DEBUG(1, "  daddr=%08x\n", d->data.daddr);
		IRDA_DEBUG(1, "  nickname=%s\n", d->data.info);

		if (strcmp(name, d->data.info) == 0) {
			*saddr = d->data.saddr;
			
			spin_unlock_irqrestore(&cachelog->hb_spinlock, flags);
			return d->data.daddr;
		}
		d = (discovery_t *) hashbin_get_next(cachelog);
	}

	spin_unlock_irqrestore(&cachelog->hb_spinlock, flags);

	return 0;
}

/*
 * Function proc_discovery_read (buf, start, offset, len, unused)
 *
 *    Print discovery information in /proc file system
 *
 */
int discovery_proc_read(char *buf, char **start, off_t offset, int length, 
			int unused)
{
	discovery_t *discovery;
	unsigned long flags;
	hashbin_t *cachelog = irlmp->cachelog;
	int		len = 0;

	if (!irlmp)
		return len;

	len = sprintf(buf, "IrLMP: Discovery log:\n\n");	
	
	spin_lock_irqsave(&cachelog->hb_spinlock, flags);

	discovery = (discovery_t *) hashbin_get_first(cachelog);
	while (( discovery != NULL) && (len < length)) {
		len += sprintf(buf+len, "nickname: %s,", discovery->data.info);
		
		len += sprintf(buf+len, " hint: 0x%02x%02x", 
			       discovery->data.hints[0], 
			       discovery->data.hints[1]);
#if 0
		if ( discovery->data.hints[0] & HINT_PNP)
			len += sprintf( buf+len, "PnP Compatible ");
		if ( discovery->data.hints[0] & HINT_PDA)
			len += sprintf( buf+len, "PDA/Palmtop ");
		if ( discovery->data.hints[0] & HINT_COMPUTER)
			len += sprintf( buf+len, "Computer ");
		if ( discovery->data.hints[0] & HINT_PRINTER)
			len += sprintf( buf+len, "Printer ");
		if ( discovery->data.hints[0] & HINT_MODEM)
			len += sprintf( buf+len, "Modem ");
		if ( discovery->data.hints[0] & HINT_FAX)
			len += sprintf( buf+len, "Fax ");
		if ( discovery->data.hints[0] & HINT_LAN)
			len += sprintf( buf+len, "LAN Access ");
		
		if ( discovery->data.hints[1] & HINT_TELEPHONY)
			len += sprintf( buf+len, "Telephony ");
		if ( discovery->data.hints[1] & HINT_FILE_SERVER)
			len += sprintf( buf+len, "File Server ");       
		if ( discovery->data.hints[1] & HINT_COMM)
			len += sprintf( buf+len, "IrCOMM ");
		if ( discovery->data.hints[1] & HINT_OBEX)
			len += sprintf( buf+len, "IrOBEX ");
#endif		
		len += sprintf(buf+len, ", saddr: 0x%08x", 
			       discovery->data.saddr);

		len += sprintf(buf+len, ", daddr: 0x%08x\n", 
			       discovery->data.daddr);
		
		len += sprintf(buf+len, "\n");
		
		discovery = (discovery_t *) hashbin_get_next(cachelog);
	}
	spin_unlock_irqrestore(&cachelog->hb_spinlock, flags);

	return len;
}
