/* 
 * dvb_demux.c - DVB kernel demux API
 *
 * Copyright (C) 2000-2001 Ralph  Metzler <ralph@convergence.de>
 *		       & Marcus Metzler <marcus@convergence.de>
 *			 for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/string.h>
	#include <linux/crc32.h>
#include <asm/uaccess.h>

#include "dvb_demux.h"
#include "dvb_functions.h"

#define NOBUFS  

LIST_HEAD(dmx_muxs);


int dmx_register_demux(struct dmx_demux *demux) 
{
	demux->users = 0;
	list_add(&demux->reg_list, &dmx_muxs);
	return 0;
}

int dmx_unregister_demux(struct dmx_demux* demux)
{
	struct list_head *pos, *n, *head=&dmx_muxs;

	list_for_each_safe (pos, n, head) {
		if (DMX_DIR_ENTRY(pos) == demux) {
			if (demux->users>0)
				return -EINVAL;
			list_del(pos);
			return 0;
		}
	}

	return -ENODEV;
}


struct list_head *dmx_get_demuxes(void)
{
	if (list_empty(&dmx_muxs))
		return NULL;

	return &dmx_muxs;
}

/******************************************************************************
 * static inlined helper functions
 ******************************************************************************/


static inline u16 section_length(const u8 *buf)
{
	return 3+((buf[1]&0x0f)<<8)+buf[2];
}


static inline u16 ts_pid(const u8 *buf)
{
	return ((buf[1]&0x1f)<<8)+buf[2];
}


static inline int payload(const u8 *tsp)
{
	if (!(tsp[3]&0x10)) // no payload?
		return 0;
	if (tsp[3]&0x20) {  // adaptation field?
		if (tsp[4]>183)    // corrupted data?
			return 0;
		else
			return 184-1-tsp[4];
	}
	return 184;
}


void dvb_set_crc32(u8 *data, int length)
{
	u32 crc;

	crc = crc32_le(~0, data, length);

	data[length]   = (crc >> 24) & 0xff;
	data[length+1] = (crc >> 16) & 0xff;
	data[length+2] = (crc >>  8) & 0xff;
	data[length+3] = (crc)       & 0xff;
}


static u32 dvb_dmx_crc32 (struct dvb_demux_feed *f, const u8 *src, size_t len)
{
	return (f->feed.sec.crc_val = crc32_le (f->feed.sec.crc_val, src, len));
}


static void dvb_dmx_memcopy (struct dvb_demux_feed *f, u8 *d, const u8 *s, size_t len)
{
	memcpy (d, s, len);
}


/******************************************************************************
 * Software filter functions
 ******************************************************************************/

static inline int dvb_dmx_swfilter_payload (struct dvb_demux_feed *feed, const u8 *buf) 
{
	int count = payload(buf);
	int p;
	//int ccok;
	//u8 cc;

	if (count == 0)
		return -1;

	p = 188-count;

	/*
	cc=buf[3]&0x0f;
	ccok=((dvbdmxfeed->cc+1)&0x0f)==cc ? 1 : 0;
	dvbdmxfeed->cc=cc;
	if (!ccok)
		printk("missed packet!\n");
	*/

	if (buf[1] & 0x40)  // PUSI ?
		feed->peslen = 0xfffa;

	feed->peslen += count;

	return feed->cb.ts (&buf[p], count, 0, 0, &feed->feed.ts, DMX_OK); 
}


static int dvb_dmx_swfilter_sectionfilter (struct dvb_demux_feed *feed, 
				    struct dvb_demux_filter *f)
{
	u8 neq = 0;
	int i;

	for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
		u8 xor = f->filter.filter_value[i] ^ feed->feed.sec.secbuf[i];

		if (f->maskandmode[i] & xor)
			return 0;

		neq |= f->maskandnotmode[i] & xor;
	}

	if (f->doneq & !neq)
		return 0;

	return feed->cb.sec (feed->feed.sec.secbuf, feed->feed.sec.seclen, 
			     0, 0, &f->filter, DMX_OK); 
}


static inline int dvb_dmx_swfilter_section_feed (struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct dvb_demux_filter *f = feed->filter;
	struct dmx_section_feed *sec = &feed->feed.sec;
	u8 *buf = sec->secbuf;

	if (sec->secbufp != sec->seclen)
		return -1;

	if (!sec->is_filtering)
		return 0;

	if (!f)
		return 0;

	if (sec->check_crc && demux->check_crc32(feed, sec->secbuf, sec->seclen))
		return -1;

	do {
		if (dvb_dmx_swfilter_sectionfilter(feed, f) < 0)
			return -1;
	} while ((f = f->next) && sec->is_filtering);

	sec->secbufp = sec->seclen = 0;

	memset(buf, 0, DVB_DEMUX_MASK_MAX);
 
	return 0;
}


static int dvb_dmx_swfilter_section_packet(struct dvb_demux_feed *feed, const u8 *buf) 
{
	struct dvb_demux *demux = feed->demux;
	struct dmx_section_feed *sec = &feed->feed.sec;
	int p, count;
	int ccok, rest;
	u8 cc;

	if (!(count = payload(buf)))
		return -1;

	p = 188-count;

	cc = buf[3] & 0x0f;
	ccok = ((feed->cc+1) & 0x0f) == cc ? 1 : 0;
	feed->cc = cc;

	if (buf[1] & 0x40) { // PUSI set
		// offset to start of first section is in buf[p] 
		if (p+buf[p]>187) // trash if it points beyond packet
			return -1;

		if (buf[p] && ccok) { // rest of previous section?
			// did we have enough data in last packet to calc length?
			int tmp = 3 - sec->secbufp;

			if (tmp > 0 && tmp != 3) {
				if (p + tmp >= 187)
					return -1;

				demux->memcopy (feed, sec->secbuf+sec->secbufp,
					       buf+p+1, tmp);

				sec->seclen = section_length(sec->secbuf);

				if (sec->seclen > 4096) 
					return -1;
			}

			rest = sec->seclen - sec->secbufp;

			if (rest == buf[p] && sec->seclen) {
				demux->memcopy (feed, sec->secbuf + sec->secbufp,
					       buf+p+1, buf[p]);
				sec->secbufp += buf[p];
				dvb_dmx_swfilter_section_feed(feed);
			}
		}

		p += buf[p] + 1; 		// skip rest of last section
		count = 188 - p;

		while (count) {

			sec->crc_val = ~0;

			if ((count>2) && // enough data to determine sec length?
			    ((sec->seclen = section_length(buf+p)) <= count)) {
				if (sec->seclen>4096) 
					return -1;

				demux->memcopy (feed, sec->secbuf, buf+p,
					       sec->seclen);

				sec->secbufp = sec->seclen;
				p += sec->seclen;
				count = 188 - p;

				dvb_dmx_swfilter_section_feed(feed);

				// filling bytes until packet end?
				if (count && buf[p]==0xff) 
					count=0;

			} else { // section continues to following TS packet
				demux->memcopy(feed, sec->secbuf, buf+p, count);
				sec->secbufp+=count;
				count=0;
			}
		}

		return 0;
	}

	// section continued below
	if (!ccok)
		return -1;

	if (!sec->secbufp) // any data in last ts packet?
		return -1;

	// did we have enough data in last packet to calc section length?
	if (sec->secbufp < 3) {
		int tmp = 3 - sec->secbufp;
		
		if (tmp>count)
			return -1;

		sec->crc_val = ~0;

		demux->memcopy (feed, sec->secbuf + sec->secbufp, buf+p, tmp);

		sec->seclen = section_length(sec->secbuf);

		if (sec->seclen > 4096) 
			return -1;
	}

	rest = sec->seclen - sec->secbufp;

	if (rest < 0)
		return -1;

	if (rest <= count) {	// section completed in this TS packet
		demux->memcopy (feed, sec->secbuf + sec->secbufp, buf+p, rest);
		sec->secbufp += rest;
		dvb_dmx_swfilter_section_feed(feed);
	} else 	{	// section continues in following ts packet
		demux->memcopy (feed, sec->secbuf + sec->secbufp, buf+p, count);
		sec->secbufp += count;
	}

	return 0;
}


static inline void dvb_dmx_swfilter_packet_type(struct dvb_demux_feed *feed, const u8 *buf)
{
	switch(feed->type) {
	case DMX_TYPE_TS:
		if (!feed->feed.ts.is_filtering)
			break;
		if (feed->ts_type & TS_PACKET) {
			if (feed->ts_type & TS_PAYLOAD_ONLY)
				dvb_dmx_swfilter_payload(feed, buf);
			else
				feed->cb.ts(buf, 188, 0, 0, &feed->feed.ts, DMX_OK); 
		}
		if (feed->ts_type & TS_DECODER) 
			if (feed->demux->write_to_decoder)
				feed->demux->write_to_decoder(feed, buf, 188);
		break;

	case DMX_TYPE_SEC:
		if (!feed->feed.sec.is_filtering)
			break;
		if (dvb_dmx_swfilter_section_packet(feed, buf) < 0)
			feed->feed.sec.seclen = feed->feed.sec.secbufp=0;
		break;

	default:
		break;
	}
}


void dvb_dmx_swfilter_packet(struct dvb_demux *demux, const u8 *buf)
{
	struct dvb_demux_feed *feed;
	struct list_head *pos, *head=&demux->feed_list;
	u16 pid = ts_pid(buf);

	list_for_each(pos, head) {
		feed = list_entry(pos, struct dvb_demux_feed, list_head);
		if (feed->pid == pid)
			dvb_dmx_swfilter_packet_type (feed, buf);
		if (feed->pid == 0x2000)
			feed->cb.ts(buf, 188, 0, 0, &feed->feed.ts, DMX_OK);
	}
}


void dvb_dmx_swfilter_packets(struct dvb_demux *demux, const u8 *buf, size_t count)
{
	spin_lock(&demux->lock);

	while (count--) {
		dvb_dmx_swfilter_packet(demux, buf);
		buf += 188;
	}

	spin_unlock(&demux->lock);
}


void dvb_dmx_swfilter(struct dvb_demux *demux, const u8 *buf, size_t count)
{
	int p = 0,i, j;
	
	if ((i = demux->tsbufp)) {
		if (count < (j=188-i)) {
			memcpy(&demux->tsbuf[i], buf, count);
			demux->tsbufp += count;
			return;
		}
		memcpy(&demux->tsbuf[i], buf, j);
		dvb_dmx_swfilter_packet(demux, demux->tsbuf);
		demux->tsbufp = 0;
		p += j;
	}

	while (p < count) {
		if (buf[p] == 0x47) {
			if (count-p >= 188) {
				dvb_dmx_swfilter_packet(demux, buf+p);
				p += 188;
			} else {
				i = count-p;
				memcpy(demux->tsbuf, buf+p, i);
				demux->tsbufp=i;
				return;
			}
		} else 
			p++;
	}
}


static struct dvb_demux_filter * dvb_dmx_filter_alloc(struct dvb_demux *demux)
{
	int i;

	for (i=0; i<demux->filternum; i++)
		if (demux->filter[i].state == DMX_STATE_FREE)
			break;

	if (i == demux->filternum)
		return NULL;

	demux->filter[i].state = DMX_STATE_ALLOCATED;

	return &demux->filter[i];
}

static struct dvb_demux_feed * dvb_dmx_feed_alloc(struct dvb_demux *demux)
{
	int i;

	for (i=0; i<demux->feednum; i++)
		if (demux->feed[i].state == DMX_STATE_FREE)
			break;

	if (i == demux->feednum)
		return NULL;

	demux->feed[i].state = DMX_STATE_ALLOCATED;

	return &demux->feed[i];
}


static int dmx_pid_set (u16 pid, struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct list_head *pos, *n, *head=&demux->feed_list;

	if (pid > DMX_MAX_PID)
		return -EINVAL;

	if (pid == feed->pid)
		return 0;

	if (feed->pid <= DMX_MAX_PID) {
		list_for_each_safe(pos, n, head) {
			if (DMX_FEED_ENTRY(pos)->pid == feed->pid) {
				list_del(pos);
				break;
			}
		}
	}

	list_add(&feed->list_head, head);
	feed->pid = pid;

	return 0;
}


static int dmx_ts_feed_set (struct dmx_ts_feed* ts_feed, u16 pid, int ts_type, 
		     enum dmx_ts_pes pes_type, size_t callback_length, 
		     size_t circular_buffer_size, int descramble, 
		     struct timespec timeout)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *) ts_feed;
	struct dvb_demux *demux = feed->demux;
	int ret;
	
	if (down_interruptible (&demux->mutex))
		return -ERESTARTSYS;

	if (ts_type & TS_DECODER) {
		if (pes_type >= DMX_TS_PES_OTHER) {
			up(&demux->mutex);
			return -EINVAL;
		}

		if (demux->pesfilter[pes_type] && 
		    demux->pesfilter[pes_type] != feed) {
			up(&demux->mutex);
			return -EINVAL;
		}

		if ((pes_type != DMX_TS_PES_PCR0) && 
		    (pes_type != DMX_TS_PES_PCR1) && 
		    (pes_type != DMX_TS_PES_PCR2) && 
		    (pes_type != DMX_TS_PES_PCR3)) {
			if ((ret = dmx_pid_set(pid, feed))<0) {
				up(&demux->mutex);
				return ret;
			}
		} else
			feed->pid = pid;
				
		demux->pesfilter[pes_type] = feed;
		demux->pids[pes_type] = feed->pid;
	} else {
		if ((ret = dmx_pid_set(pid, feed))<0) {
			up(&demux->mutex);
			return ret;
		}
	}

	feed->buffer_size = circular_buffer_size;
	feed->descramble = descramble;
	feed->timeout = timeout;
	feed->cb_length = callback_length;
	feed->ts_type = ts_type;
	feed->pes_type = pes_type;

	if (feed->descramble) {
		up(&demux->mutex);
		return -ENOSYS;
	}

	if (feed->buffer_size) {
#ifdef NOBUFS
		feed->buffer=0;
#else
		feed->buffer = vmalloc(feed->buffer_size);
		if (!feed->buffer) {
			up(&demux->mutex);
			return -ENOMEM;
		}
#endif
	}
	
	feed->state = DMX_STATE_READY;
	up(&demux->mutex);

	return 0;
}


static int dmx_ts_feed_start_filtering(struct dmx_ts_feed* ts_feed)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *) ts_feed;
	struct dvb_demux *demux = feed->demux;
	int ret;

	if (down_interruptible (&demux->mutex))
		return -ERESTARTSYS;

	if (feed->state != DMX_STATE_READY || feed->type != DMX_TYPE_TS) {
		up(&demux->mutex);
		return -EINVAL;
	}

	if (!demux->start_feed) {
		up(&demux->mutex);
		return -ENODEV;
	}

	if ((ret = demux->start_feed(feed)) < 0) {
		up(&demux->mutex);
		return ret;
	}

	spin_lock_irq(&demux->lock);
	ts_feed->is_filtering = 1;
	feed->state = DMX_STATE_GO;
	spin_unlock_irq(&demux->lock);
	up(&demux->mutex);

	return 0;
}
 
static int dmx_ts_feed_stop_filtering(struct dmx_ts_feed* ts_feed)
{
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *) ts_feed;
	struct dvb_demux *demux = feed->demux;
	int ret;

	if (down_interruptible (&demux->mutex))
		return -ERESTARTSYS;

	if (feed->state<DMX_STATE_GO) {
		up(&demux->mutex);
		return -EINVAL;
	}

	if (!demux->stop_feed) {
		up(&demux->mutex);
		return -ENODEV;
	}

	ret = demux->stop_feed(feed); 

	spin_lock_irq(&demux->lock);
	ts_feed->is_filtering = 0;
	feed->state = DMX_STATE_ALLOCATED;
	spin_unlock_irq(&demux->lock);
	up(&demux->mutex);

	return ret;
}

static int dvbdmx_allocate_ts_feed (struct dmx_demux *dmx, struct dmx_ts_feed **ts_feed, 
			     dmx_ts_cb callback)
{
	struct dvb_demux *demux = (struct dvb_demux *) dmx;
	struct dvb_demux_feed *feed;

	if (down_interruptible (&demux->mutex))
		return -ERESTARTSYS;

	if (!(feed = dvb_dmx_feed_alloc(demux))) {
		up(&demux->mutex);
		return -EBUSY;
	}

	feed->type = DMX_TYPE_TS;
	feed->cb.ts = callback;
	feed->demux = demux;
	feed->pid = 0xffff;
	feed->peslen = 0xfffa;
	feed->buffer = 0;

	(*ts_feed) = &feed->feed.ts;
	(*ts_feed)->parent = dmx;
	(*ts_feed)->priv = 0;
	(*ts_feed)->is_filtering = 0;
	(*ts_feed)->start_filtering = dmx_ts_feed_start_filtering;
	(*ts_feed)->stop_filtering = dmx_ts_feed_stop_filtering;
	(*ts_feed)->set = dmx_ts_feed_set;


	if (!(feed->filter = dvb_dmx_filter_alloc(demux))) {
		feed->state = DMX_STATE_FREE;
		up(&demux->mutex);
		return -EBUSY;
	}

	feed->filter->type = DMX_TYPE_TS;
	feed->filter->feed = feed;
	feed->filter->state = DMX_STATE_READY;
	
	up(&demux->mutex);

	return 0;
}

static int dvbdmx_release_ts_feed(struct dmx_demux *dmx, struct dmx_ts_feed *ts_feed)
{
	struct dvb_demux *demux = (struct dvb_demux *) dmx;
	struct dvb_demux_feed *feed = (struct dvb_demux_feed *) ts_feed;
	struct list_head *pos, *n, *head=&demux->feed_list;

	if (down_interruptible (&demux->mutex))
		return -ERESTARTSYS;

	if (feed->state == DMX_STATE_FREE) {
		up(&demux->mutex);
		return -EINVAL;
	}

#ifndef NOBUFS
	if (feed->buffer) { 
		vfree(feed->buffer);
		feed->buffer=0;
	}
#endif

	feed->state = DMX_STATE_FREE;
	feed->filter->state = DMX_STATE_FREE;

	if (feed->pid <= DMX_MAX_PID) {
		list_for_each_safe(pos, n, head)
			if (DMX_FEED_ENTRY(pos)->pid == feed->pid) {
				list_del(pos);
				break;
			}
		feed->pid = 0xffff;
	}
	
	if (feed->ts_type & TS_DECODER)
		demux->pesfilter[feed->pes_type] = NULL;

	up(&demux->mutex);
	return 0;
}


/******************************************************************************
 * dmx_section_feed API calls
 ******************************************************************************/

static int dmx_section_feed_allocate_filter(struct dmx_section_feed* feed, 
				     struct dmx_section_filter** filter) 
{
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdemux=dvbdmxfeed->demux;
	struct dvb_demux_filter *dvbdmxfilter;

	if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

	dvbdmxfilter=dvb_dmx_filter_alloc(dvbdemux);
	if (!dvbdmxfilter) {
		up(&dvbdemux->mutex);
		return -EBUSY;
	}

	spin_lock_irq(&dvbdemux->lock);
	*filter=&dvbdmxfilter->filter;
	(*filter)->parent=feed;
	(*filter)->priv=0;
	dvbdmxfilter->feed=dvbdmxfeed;
	dvbdmxfilter->type=DMX_TYPE_SEC;
	dvbdmxfilter->state=DMX_STATE_READY;

	dvbdmxfilter->next=dvbdmxfeed->filter;
	dvbdmxfeed->filter=dvbdmxfilter;
	spin_unlock_irq(&dvbdemux->lock);
	up(&dvbdemux->mutex);
	return 0;
}


static int dmx_section_feed_set(struct dmx_section_feed* feed, 
		     u16 pid, size_t circular_buffer_size, 
		     int descramble, int check_crc) 
{
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	struct list_head *pos, *n, *head=&dvbdmx->feed_list;

	if (pid>0x1fff)
		return -EINVAL;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;
	
	if (dvbdmxfeed->pid <= DMX_MAX_PID) {
		list_for_each_safe(pos, n, head) {
			if (DMX_FEED_ENTRY(pos)->pid == dvbdmxfeed->pid) {
				list_del(pos);
				break;
			}
		}
	}

	list_add(&dvbdmxfeed->list_head, head);

	dvbdmxfeed->pid = pid;
	dvbdmxfeed->buffer_size=circular_buffer_size;
	dvbdmxfeed->descramble=descramble;
	if (dvbdmxfeed->descramble) {
		up(&dvbdmx->mutex);
		return -ENOSYS;
	}

	dvbdmxfeed->feed.sec.check_crc=check_crc;
#ifdef NOBUFS
	dvbdmxfeed->buffer=0;
#else
	dvbdmxfeed->buffer=vmalloc(dvbdmxfeed->buffer_size);
	if (!dvbdmxfeed->buffer) {
		up(&dvbdmx->mutex);
		return -ENOMEM;
	}
#endif
	dvbdmxfeed->state=DMX_STATE_READY;
	up(&dvbdmx->mutex);
	return 0;
}

static void prepare_secfilters(struct dvb_demux_feed *dvbdmxfeed)
{
	int i;
	struct dvb_demux_filter *f;
	struct dmx_section_filter *sf;
	u8 mask, mode, doneq;
		
	if (!(f=dvbdmxfeed->filter))
		return;
	do {
		sf=&f->filter;
		doneq=0;
		for (i=0; i<DVB_DEMUX_MASK_MAX; i++) {
			mode=sf->filter_mode[i];
			mask=sf->filter_mask[i];
			f->maskandmode[i]=mask&mode;
			doneq|=f->maskandnotmode[i]=mask&~mode;
		}
		f->doneq=doneq ? 1 : 0;
	} while ((f=f->next));
}


static int dmx_section_feed_start_filtering(struct dmx_section_feed *feed)
{
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;
	
	if (feed->is_filtering) {
		up(&dvbdmx->mutex);
		return -EBUSY;
	}
	if (!dvbdmxfeed->filter) {
		up(&dvbdmx->mutex);
		return -EINVAL;
	}
	dvbdmxfeed->feed.sec.secbufp=0;
	dvbdmxfeed->feed.sec.seclen=0;
	
	if (!dvbdmx->start_feed) {
		up(&dvbdmx->mutex);
		return -ENODEV;
	}

	prepare_secfilters(dvbdmxfeed);

	if ((ret = dvbdmx->start_feed(dvbdmxfeed)) < 0) {
		up(&dvbdmx->mutex);
		return ret;
	}

	spin_lock_irq(&dvbdmx->lock);
	feed->is_filtering=1;
	dvbdmxfeed->state=DMX_STATE_GO;
	spin_unlock_irq(&dvbdmx->lock);
	up(&dvbdmx->mutex);
	return 0;
}


static int dmx_section_feed_stop_filtering(struct dmx_section_feed* feed)
{
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdmx=dvbdmxfeed->demux;
	int ret;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (!dvbdmx->stop_feed) {
		up(&dvbdmx->mutex);
		return -ENODEV;
	}
	ret=dvbdmx->stop_feed(dvbdmxfeed); 
	spin_lock_irq(&dvbdmx->lock);
	dvbdmxfeed->state=DMX_STATE_READY;
	feed->is_filtering=0;
	spin_unlock_irq(&dvbdmx->lock);
	up(&dvbdmx->mutex);
	return ret;
}


static int dmx_section_feed_release_filter(struct dmx_section_feed *feed, 
				struct dmx_section_filter* filter)
{
	struct dvb_demux_filter *dvbdmxfilter=(struct dvb_demux_filter *) filter, *f;
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdmx=dvbdmxfeed->demux;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (dvbdmxfilter->feed!=dvbdmxfeed) {
		up(&dvbdmx->mutex);
		return -EINVAL;
	}
	if (feed->is_filtering) 
		feed->stop_filtering(feed);
	
	spin_lock_irq(&dvbdmx->lock);
	f=dvbdmxfeed->filter;

	if (f == dvbdmxfilter) {
		dvbdmxfeed->filter=dvbdmxfilter->next;
	} else {
		while(f->next!=dvbdmxfilter)
			f=f->next;
		f->next=f->next->next;
	}
	dvbdmxfilter->state=DMX_STATE_FREE;
	spin_unlock_irq(&dvbdmx->lock);
	up(&dvbdmx->mutex);
	return 0;
}

static int dvbdmx_allocate_section_feed(struct dmx_demux *demux, 
					struct dmx_section_feed **feed,
					dmx_section_cb callback)
{
	struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;
	struct dvb_demux_feed *dvbdmxfeed;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (!(dvbdmxfeed=dvb_dmx_feed_alloc(dvbdmx))) {
		up(&dvbdmx->mutex);
		return -EBUSY;
	}
	dvbdmxfeed->type=DMX_TYPE_SEC;
	dvbdmxfeed->cb.sec=callback;
	dvbdmxfeed->demux=dvbdmx;
	dvbdmxfeed->pid=0xffff;
	dvbdmxfeed->feed.sec.secbufp=0;
	dvbdmxfeed->filter=0;
	dvbdmxfeed->buffer=0;

	(*feed)=&dvbdmxfeed->feed.sec;
	(*feed)->is_filtering=0;
	(*feed)->parent=demux;
	(*feed)->priv=0;

	(*feed)->set=dmx_section_feed_set;
	(*feed)->allocate_filter=dmx_section_feed_allocate_filter;
	(*feed)->start_filtering=dmx_section_feed_start_filtering;
	(*feed)->stop_filtering=dmx_section_feed_stop_filtering;
	(*feed)->release_filter = dmx_section_feed_release_filter;

	up(&dvbdmx->mutex);
	return 0;
}

static int dvbdmx_release_section_feed(struct dmx_demux *demux, 
				       struct dmx_section_feed *feed)
{
	struct dvb_demux_feed *dvbdmxfeed=(struct dvb_demux_feed *) feed;
	struct dvb_demux *dvbdmx=(struct dvb_demux *) demux;
	struct list_head *pos, *n, *head=&dvbdmx->feed_list;

	if (down_interruptible (&dvbdmx->mutex))
		return -ERESTARTSYS;

	if (dvbdmxfeed->state==DMX_STATE_FREE) {
		up(&dvbdmx->mutex);
		return -EINVAL;
	}
#ifndef NOBUFS
	if (dvbdmxfeed->buffer) {
		vfree(dvbdmxfeed->buffer);
		dvbdmxfeed->buffer=0;
	}
#endif
	dvbdmxfeed->state=DMX_STATE_FREE;

	if (dvbdmxfeed->pid <= DMX_MAX_PID) {
		list_for_each_safe(pos, n, head) {
			if (DMX_FEED_ENTRY(pos)->pid == dvbdmxfeed->pid) {
				list_del(pos);
				break;
			}
		}
		dvbdmxfeed->pid = 0xffff;
	}

	up(&dvbdmx->mutex);
	return 0;
}


/******************************************************************************
 * dvb_demux kernel data API calls
 ******************************************************************************/

static int dvbdmx_open(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if (dvbdemux->users>=MAX_DVB_DEMUX_USERS)
		return -EUSERS;
	dvbdemux->users++;
	return 0;
}


static int dvbdmx_close(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if (dvbdemux->users==0)
		return -ENODEV;
	dvbdemux->users--;
	//FIXME: release any unneeded resources if users==0
	return 0;
}


static int dvbdmx_write(struct dmx_demux *demux, const char *buf, size_t count)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if ((!demux->frontend) || (demux->frontend->source != DMX_MEMORY_FE))
		return -EINVAL;

	if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

	dvb_dmx_swfilter(dvbdemux, buf, count);
	up(&dvbdemux->mutex);
	return count;
}


static int dvbdmx_add_frontend(struct dmx_demux *demux, struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;
	struct list_head *head = &dvbdemux->frontend_list;

	list_add(&(frontend->connectivity_list), head);

	return 0;
}


static int dvbdmx_remove_frontend(struct dmx_demux *demux, struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;
	struct list_head *pos, *n, *head=&dvbdemux->frontend_list;

	list_for_each_safe (pos, n, head) {
		if (DMX_FE_ENTRY(pos) == frontend) {
			list_del(pos);
			return 0;
		}
	}
	return -ENODEV;
}


static struct list_head * dvbdmx_get_frontends(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if (list_empty(&dvbdemux->frontend_list))
		return NULL;
	return &dvbdemux->frontend_list;
}


int dvbdmx_connect_frontend(struct dmx_demux *demux, struct dmx_frontend *frontend)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if (demux->frontend)
		return -EINVAL;
	
	if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

	demux->frontend=frontend;
	up(&dvbdemux->mutex);
	return 0;
}


int dvbdmx_disconnect_frontend(struct dmx_demux *demux)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	if (down_interruptible (&dvbdemux->mutex))
		return -ERESTARTSYS;

	demux->frontend=NULL;
	up(&dvbdemux->mutex);
	return 0;
}


static int dvbdmx_get_pes_pids(struct dmx_demux *demux, u16 *pids)
{
	struct dvb_demux *dvbdemux=(struct dvb_demux *) demux;

	memcpy(pids, dvbdemux->pids, 5*sizeof(u16));
	return 0;
}

int 
dvb_dmx_init(struct dvb_demux *dvbdemux)
{
	int i, err;
	struct dmx_demux *dmx = &dvbdemux->dmx;

	dvbdemux->users=0;
	dvbdemux->filter=vmalloc(dvbdemux->filternum*sizeof(struct dvb_demux_filter));
	if (!dvbdemux->filter)
		return -ENOMEM;

	dvbdemux->feed=vmalloc(dvbdemux->feednum*sizeof(struct dvb_demux_feed));
	if (!dvbdemux->feed) {
		vfree(dvbdemux->filter);
		return -ENOMEM;
	}
	for (i=0; i<dvbdemux->filternum; i++) {
		dvbdemux->filter[i].state=DMX_STATE_FREE;
		dvbdemux->filter[i].index=i;
	}
	for (i=0; i<dvbdemux->feednum; i++)
		dvbdemux->feed[i].state=DMX_STATE_FREE;
	dvbdemux->frontend_list.next=
	  dvbdemux->frontend_list.prev=
	    &dvbdemux->frontend_list;
	for (i=0; i<DMX_TS_PES_OTHER; i++) {
		dvbdemux->pesfilter[i]=NULL;
		dvbdemux->pids[i]=0xffff;
	}

	INIT_LIST_HEAD(&dvbdemux->feed_list);

	dvbdemux->playing = 0;
	dvbdemux->recording = 0;
	dvbdemux->tsbufp=0;

	if (!dvbdemux->check_crc32)
		dvbdemux->check_crc32 = dvb_dmx_crc32;

	 if (!dvbdemux->memcopy)
		 dvbdemux->memcopy = dvb_dmx_memcopy;

	dmx->frontend=0;
	dmx->reg_list.prev = dmx->reg_list.next = &dmx->reg_list;
	dmx->priv=(void *) dvbdemux;
	dmx->open=dvbdmx_open;
	dmx->close=dvbdmx_close;
	dmx->write=dvbdmx_write;
	dmx->allocate_ts_feed=dvbdmx_allocate_ts_feed;
	dmx->release_ts_feed=dvbdmx_release_ts_feed;
	dmx->allocate_section_feed=dvbdmx_allocate_section_feed;
	dmx->release_section_feed=dvbdmx_release_section_feed;

	dmx->descramble_mac_address=NULL;
	dmx->descramble_section_payload=NULL;
	
	dmx->add_frontend=dvbdmx_add_frontend;
	dmx->remove_frontend=dvbdmx_remove_frontend;
	dmx->get_frontends=dvbdmx_get_frontends;
	dmx->connect_frontend=dvbdmx_connect_frontend;
	dmx->disconnect_frontend=dvbdmx_disconnect_frontend;
	dmx->get_pes_pids=dvbdmx_get_pes_pids;
	sema_init(&dvbdemux->mutex, 1);
	spin_lock_init(&dvbdemux->lock);

	if ((err = dmx_register_demux(dmx)) < 0) 
		return err;

	return 0;
}

int 
dvb_dmx_release(struct dvb_demux *dvbdemux)
{
	struct dmx_demux *dmx = &dvbdemux->dmx;

	dmx_unregister_demux(dmx);
	if (dvbdemux->filter)
		vfree(dvbdemux->filter);
	if (dvbdemux->feed)
		vfree(dvbdemux->feed);
	return 0;
}
