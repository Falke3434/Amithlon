/*
 * ioctl32.c: Conversion between 32bit and 64bit native ioctls.
 *
 * Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 * Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 * Copyright (C) 2003       Pavel Machek (pavel@suse.cz)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * ioctls.
 */

#ifdef INCLUDES
#include <linux/config.h>
#include <linux/types.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/if.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/raid/md.h>
#include <linux/kd.h>
#include <linux/dirent.h>
#include <linux/route.h>
#include <linux/in6.h>
#include <linux/ipv6_route.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/vt.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fd.h>
#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/mtio.h>
#include <linux/cdrom.h>
#include <linux/loop.h>
#include <linux/auto_fs.h>
#include <linux/auto_fs4.h>
#include <linux/devfs_fs.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>
#include <linux/fb.h>
#include <linux/ext2_fs.h>
#include <linux/videodev.h>
#include <linux/netdevice.h>
#include <linux/raw.h>
#include <linux/smb_fs.h>
#include <linux/blkpg.h>
#include <linux/blk.h>
#include <linux/elevator.h>
#include <linux/rtc.h>
#include <linux/pci.h>
#include <linux/rtc.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/reiserfs_fs.h>
#include <linux/if_tun.h>
#include <linux/dirent.h>
#include <linux/ctype.h>
#include <linux/ioctl32.h>
#include <linux/ncp_fs.h>

#include <net/sock.h>          /* siocdevprivate_ioctl */
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/rfcomm.h>
#include <net/bluetooth/hci.h>

#include <scsi/scsi.h>
/* Ugly hack. */
#undef __KERNEL__
#include <scsi/scsi_ioctl.h>
#define __KERNEL__
#include <scsi/sg.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_bonding.h>
#include <linux/watchdog.h>
#include <linux/dm-ioctl.h>

#include <asm/module.h>
#include <linux/soundcard.h>
#include <linux/lp.h>

#include <linux/atm.h>
#include <linux/atmarp.h>
#include <linux/atmclip.h>
#include <linux/atmdev.h>
#include <linux/atmioc.h>
#include <linux/atmlec.h>
#include <linux/atmmpc.h>
#include <linux/atmsvc.h>
#include <linux/atm_tcp.h>
#include <linux/sonet.h>
#include <linux/atm_suni.h>
#include <linux/mtd/mtd.h>

#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/nbd.h>
#include <linux/random.h>
#include <linux/filter.h>

#undef INCLUDES
#endif

#ifdef CODE

/* Aiee. Someone does not find a difference between int and long */
#define EXT2_IOC32_GETFLAGS               _IOR('f', 1, int)
#define EXT2_IOC32_SETFLAGS               _IOW('f', 2, int)
#define EXT2_IOC32_GETVERSION             _IOR('v', 1, int)
#define EXT2_IOC32_SETVERSION             _IOW('v', 2, int)

static int w_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)arg))
		return -EFAULT;
	return err;
}
 
static int rw_long(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	int err;
	unsigned long val;
	
	if(get_user(val, (u32 *)arg))
		return -EFAULT;
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&val);
	set_fs (old_fs);
	if (!err && put_user(val, (u32 *)arg))
		return -EFAULT;
	return err;
}

static int do_ext2_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT2_IOC32_GETFLAGS: cmd = EXT2_IOC_GETFLAGS; break;
	case EXT2_IOC32_SETFLAGS: cmd = EXT2_IOC_SETFLAGS; break;
	case EXT2_IOC32_GETVERSION: cmd = EXT2_IOC_GETVERSION; break;
	case EXT2_IOC32_SETVERSION: cmd = EXT2_IOC_SETVERSION; break;
	}
	return sys_ioctl(fd, cmd, arg);
}
 
struct video_tuner32 {
	compat_int_t tuner;
	char name[32];
	compat_ulong_t rangelow, rangehigh;
	u32 flags;	/* It is really u32 in videodev.h */
	u16 mode, signal;
};

static int get_video_tuner32(struct video_tuner *kp, struct video_tuner32 *up)
{
	int i;

	if(get_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__get_user(kp->name[i], &up->name[i]);
	__get_user(kp->rangelow, &up->rangelow);
	__get_user(kp->rangehigh, &up->rangehigh);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->mode, &up->mode);
	__get_user(kp->signal, &up->signal);
	return 0;
}

static int put_video_tuner32(struct video_tuner *kp, struct video_tuner32 *up)
{
	int i;

	if(put_user(kp->tuner, &up->tuner))
		return -EFAULT;
	for(i = 0; i < 32; i++)
		__put_user(kp->name[i], &up->name[i]);
	__put_user(kp->rangelow, &up->rangelow);
	__put_user(kp->rangehigh, &up->rangehigh);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->mode, &up->mode);
	__put_user(kp->signal, &up->signal);
	return 0;
}

struct video_buffer32 {
	compat_caddr_t base;
	compat_int_t height, width, depth, bytesperline;
};

static int get_video_buffer32(struct video_buffer *kp, struct video_buffer32 *up)
{
	u32 tmp;

	if(get_user(tmp, &up->base))
		return -EFAULT;
	kp->base = (void *) ((unsigned long)tmp);
	__get_user(kp->height, &up->height);
	__get_user(kp->width, &up->width);
	__get_user(kp->depth, &up->depth);
	__get_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

static int put_video_buffer32(struct video_buffer *kp, struct video_buffer32 *up)
{
	u32 tmp = (u32)((unsigned long)kp->base);

	if(put_user(tmp, &up->base))
		return -EFAULT;
	__put_user(kp->height, &up->height);
	__put_user(kp->width, &up->width);
	__put_user(kp->depth, &up->depth);
	__put_user(kp->bytesperline, &up->bytesperline);
	return 0;
}

struct video_clip32 {
	s32 x, y, width, height;	/* Its really s32 in videodev.h */
	compat_caddr_t next;
};

struct video_window32 {
	u32 x, y, width, height, chromakey, flags;
	compat_caddr_t clips;
	compat_int_t clipcount;
};

static void free_kvideo_clips(struct video_window *kp)
{
	struct video_clip *cp;

	cp = kp->clips;
	if(cp != NULL)
		kfree(cp);
}

static int get_video_window32(struct video_window *kp, struct video_window32 *up)
{
	struct video_clip32 *ucp;
	struct video_clip *kcp;
	int nclips, err, i;
	u32 tmp;

	if(get_user(kp->x, &up->x))
		return -EFAULT;
	__get_user(kp->y, &up->y);
	__get_user(kp->width, &up->width);
	__get_user(kp->height, &up->height);
	__get_user(kp->chromakey, &up->chromakey);
	__get_user(kp->flags, &up->flags);
	__get_user(kp->clipcount, &up->clipcount);
	__get_user(tmp, &up->clips);
	ucp = compat_ptr(tmp);
	kp->clips = NULL;

	nclips = kp->clipcount;
	if(nclips == 0)
		return 0;

	if(ucp == 0)
		return -EINVAL;

	/* Peculiar interface... */
	if(nclips < 0)
		nclips = VIDEO_CLIPMAP_SIZE;

	kcp = kmalloc(nclips * sizeof(struct video_clip), GFP_KERNEL);
	err = -ENOMEM;
	if(kcp == NULL)
		goto cleanup_and_err;

	kp->clips = kcp;
	for(i = 0; i < nclips; i++) {
		__get_user(kcp[i].x, &ucp[i].x);
		__get_user(kcp[i].y, &ucp[i].y);
		__get_user(kcp[i].width, &ucp[i].width);
		__get_user(kcp[i].height, &ucp[i].height);
		kcp[nclips].next = NULL;
	}

	return 0;

cleanup_and_err:
	free_kvideo_clips(kp);
	return err;
}

/* You get back everything except the clips... */
static int put_video_window32(struct video_window *kp, struct video_window32 *up)
{
	if(put_user(kp->x, &up->x))
		return -EFAULT;
	__put_user(kp->y, &up->y);
	__put_user(kp->width, &up->width);
	__put_user(kp->height, &up->height);
	__put_user(kp->chromakey, &up->chromakey);
	__put_user(kp->flags, &up->flags);
	__put_user(kp->clipcount, &up->clipcount);
	return 0;
}

#define VIDIOCGTUNER32		_IOWR('v',4, struct video_tuner32)
#define VIDIOCSTUNER32		_IOW('v',5, struct video_tuner32)
#define VIDIOCGWIN32		_IOR('v',9, struct video_window32)
#define VIDIOCSWIN32		_IOW('v',10, struct video_window32)
#define VIDIOCGFBUF32		_IOR('v',11, struct video_buffer32)
#define VIDIOCSFBUF32		_IOW('v',12, struct video_buffer32)
#define VIDIOCGFREQ32		_IOR('v',14, u32)
#define VIDIOCSFREQ32		_IOW('v',15, u32)

static int do_video_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	union {
		struct video_tuner vt;
		struct video_buffer vb;
		struct video_window vw;
		unsigned long vx;
	} karg;
	mm_segment_t old_fs = get_fs();
	void *up = (void *)arg;
	int err = 0;

	/* First, convert the command. */
	switch(cmd) {
	case VIDIOCGTUNER32: cmd = VIDIOCGTUNER; break;
	case VIDIOCSTUNER32: cmd = VIDIOCSTUNER; break;
	case VIDIOCGWIN32: cmd = VIDIOCGWIN; break;
	case VIDIOCSWIN32: cmd = VIDIOCSWIN; break;
	case VIDIOCGFBUF32: cmd = VIDIOCGFBUF; break;
	case VIDIOCSFBUF32: cmd = VIDIOCSFBUF; break;
	case VIDIOCGFREQ32: cmd = VIDIOCGFREQ; break;
	case VIDIOCSFREQ32: cmd = VIDIOCSFREQ; break;
	};

	switch(cmd) {
	case VIDIOCSTUNER:
	case VIDIOCGTUNER:
		err = get_video_tuner32(&karg.vt, up);
		break;

	case VIDIOCSWIN:
		err = get_video_window32(&karg.vw, up);
		break;

	case VIDIOCSFBUF:
		err = get_video_buffer32(&karg.vb, up);
		break;

	case VIDIOCSFREQ:
		err = get_user(karg.vx, (u32 *)up);
		break;
	};
	if(err)
		goto out;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&karg);
	set_fs(old_fs);

	if(cmd == VIDIOCSWIN)
		free_kvideo_clips(&karg.vw);

	if(err == 0) {
		switch(cmd) {
		case VIDIOCGTUNER:
			err = put_video_tuner32(&karg.vt, up);
			break;

		case VIDIOCGWIN:
			err = put_video_window32(&karg.vw, up);
			break;

		case VIDIOCGFBUF:
			err = put_video_buffer32(&karg.vb, up);
			break;

		case VIDIOCGFREQ:
			err = put_user(((u32)karg.vx), (u32 *)up);
			break;
		};
	}
out:
	return err;
}

static int do_siocgstamp(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct compat_timeval *up = (struct compat_timeval *)arg;
	struct timeval ktv;
	mm_segment_t old_fs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&ktv);
	set_fs(old_fs);
	if(!err) {
		err = put_user(ktv.tv_sec, &up->tv_sec);
		err |= __put_user(ktv.tv_usec, &up->tv_usec);
	}
	return err;
}

struct ifmap32 {
	compat_ulong_t mem_start;
	compat_ulong_t mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct ifreq32 {
#define IFHWADDRLEN     6
#define IFNAMSIZ        16
        union {
                char    ifrn_name[IFNAMSIZ];            /* if name, e.g. "en0" */
        } ifr_ifrn;
        union {
                struct  sockaddr ifru_addr;
                struct  sockaddr ifru_dstaddr;
                struct  sockaddr ifru_broadaddr;
                struct  sockaddr ifru_netmask;
                struct  sockaddr ifru_hwaddr;
                short   ifru_flags;
                compat_int_t     ifru_ivalue;
                compat_int_t     ifru_mtu;
                struct  ifmap32 ifru_map;
                char    ifru_slave[IFNAMSIZ];   /* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
                compat_caddr_t ifru_data;
	    /* XXXX? ifru_settings should be here */
        } ifr_ifru;
};

struct ifconf32 {
        compat_int_t	ifc_len;                        /* size of buffer       */
        compat_caddr_t  ifcbuf;
};

#ifdef CONFIG_NET
static int dev_ifname32(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct net_device *dev;
	struct ifreq32 ifr32;
	int err;

	if (copy_from_user(&ifr32, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
		return -EFAULT;

	dev = dev_get_by_index(ifr32.ifr_ifindex);
	if (!dev)
		return -ENODEV;

	strlcpy(ifr32.ifr_name, dev->name, sizeof(ifr32.ifr_name));
	dev_put(dev);
	
	err = copy_to_user((struct ifreq32 *)arg, &ifr32, sizeof(struct ifreq32));
	return (err ? -EFAULT : 0);
}
#endif

static int dev_ifconf(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifconf32 ifc32;
	struct ifconf ifc;
	struct ifreq32 *ifr32;
	struct ifreq *ifr;
	mm_segment_t old_fs;
	unsigned int i, j;
	int err;

	if (copy_from_user(&ifc32, (struct ifconf32 *)arg, sizeof(struct ifconf32)))
		return -EFAULT;

	if(ifc32.ifcbuf == 0) {
		ifc32.ifc_len = 0;
		ifc.ifc_len = 0;
		ifc.ifc_buf = NULL;
	} else {
		ifc.ifc_len = ((ifc32.ifc_len / sizeof (struct ifreq32)) + 1) *
			sizeof (struct ifreq);
		ifc.ifc_buf = kmalloc (ifc.ifc_len, GFP_KERNEL);
		if (!ifc.ifc_buf)
			return -ENOMEM;
	}
	ifr = ifc.ifc_req;
	ifr32 = compat_ptr(ifc32.ifcbuf);
	for (i = 0; i < ifc32.ifc_len; i += sizeof (struct ifreq32)) {
		if (copy_from_user(ifr, ifr32, sizeof (struct ifreq32))) {
			kfree (ifc.ifc_buf);
			return -EFAULT;
		}
		ifr++;
		ifr32++; 
	}
	old_fs = get_fs(); set_fs (KERNEL_DS);
	err = sys_ioctl (fd, SIOCGIFCONF, (unsigned long)&ifc);	
	set_fs (old_fs);
	if (!err) {
		ifr = ifc.ifc_req;
		ifr32 = compat_ptr(ifc32.ifcbuf);
		for (i = 0, j = 0; i < ifc32.ifc_len && j < ifc.ifc_len;
		     i += sizeof (struct ifreq32), j += sizeof (struct ifreq)) {
			int k = copy_to_user(ifr32, ifr, sizeof (struct ifreq32));
			ifr32++;
			ifr++;
			if (k) {
				err = -EFAULT;
				break;
			}
		       
		}
		if (!err) {
			if (ifc32.ifcbuf == 0) {
				/* Translate from 64-bit structure multiple to
				 * a 32-bit one.
				 */
				i = ifc.ifc_len;
				i = ((i / sizeof(struct ifreq)) * sizeof(struct ifreq32));
				ifc32.ifc_len = i;
			} else {
				if (i <= ifc32.ifc_len)
					ifc32.ifc_len = i;
				else
					ifc32.ifc_len = i - sizeof (struct ifreq32);
			}
			if (copy_to_user((struct ifconf32 *)arg, &ifc32, sizeof(struct ifconf32)))
				err = -EFAULT;
		}
	}
	if(ifc.ifc_buf != NULL)
		kfree (ifc.ifc_buf);
	return err;
}

static int ethtool_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq *ifr;
	struct ifreq32 *ifr32;
	u32 data;
	void *datap;
	
	ifr = compat_alloc_user_space(sizeof(*ifr));
	ifr32 = (struct ifreq32 *) arg;

	if (copy_in_user(&ifr->ifr_name, &ifr32->ifr_name, IFNAMSIZ))
		return -EFAULT;

	if (get_user(data, &ifr32->ifr_ifru.ifru_data))
		return -EFAULT;

	datap = (void *) (unsigned long) data;
	if (put_user(datap, &ifr->ifr_ifru.ifru_data))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) ifr);
}

static int bond_ioctl(unsigned long fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err, len;
	u32 data;
	
	if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
		return -EFAULT;
	ifr.ifr_data = (__kernel_caddr_t)get_zeroed_page(GFP_KERNEL);
	if (!ifr.ifr_data)
		return -EAGAIN;

	switch (cmd) {
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
		len = IFNAMSIZ * sizeof(char);
		break;
	case SIOCBONDSLAVEINFOQUERY:
		len = sizeof(struct ifslave);
		break;
	case SIOCBONDINFOQUERY:
		len = sizeof(struct ifbond);
		break;
	default:
		err = -EINVAL;
		goto out;
	};

	__get_user(data, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_data));
	if (copy_from_user(ifr.ifr_data, compat_ptr(data), len)) {
		err = -EFAULT;
		goto out;
	}

	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		len = copy_to_user(compat_ptr(data), ifr.ifr_data, len);
		if (len)
			err = -EFAULT;
	}

out:
	free_page((unsigned long)ifr.ifr_data);
	return err;
}

int siocdevprivate_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq *u_ifreq64;
	struct ifreq32 *u_ifreq32 = (struct ifreq32 *) arg;
	char tmp_buf[IFNAMSIZ];
	void *data64;
	u32 data32;

	if (copy_from_user(&tmp_buf[0], &(u_ifreq32->ifr_ifrn.ifrn_name[0]),
			   IFNAMSIZ))
		return -EFAULT;
	if (__get_user(data32, &u_ifreq32->ifr_ifru.ifru_data))
		return -EFAULT;
	data64 = compat_ptr(data32);

	u_ifreq64 = compat_alloc_user_space(sizeof(*u_ifreq64));

	/* Don't check these user accesses, just let that get trapped
	 * in the ioctl handler instead.
	 */
	copy_to_user(&u_ifreq64->ifr_ifrn.ifrn_name[0], &tmp_buf[0], IFNAMSIZ);
	__put_user(data64, &u_ifreq64->ifr_ifru.ifru_data);

	return sys_ioctl(fd, cmd, (unsigned long) u_ifreq64);
}

static int dev_ifsioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err;
	
	switch (cmd) {
	case SIOCSIFMAP:
		err = copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(ifr.ifr_name));
		err |= __get_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
		err |= __get_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
		err |= __get_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
		err |= __get_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
		err |= __get_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
		err |= __get_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
		if (err)
			return -EFAULT;
		break;
	default:
		if (copy_from_user(&ifr, (struct ifreq32 *)arg, sizeof(struct ifreq32)))
			return -EFAULT;
		break;
	}
	old_fs = get_fs();
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, cmd, (unsigned long)&ifr);
	set_fs (old_fs);
	if (!err) {
		switch (cmd) {
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFTXQLEN:
			if (copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(struct ifreq32)))
				return -EFAULT;
			break;
		case SIOCGIFMAP:
			err = copy_to_user((struct ifreq32 *)arg, &ifr, sizeof(ifr.ifr_name));
			err |= __put_user(ifr.ifr_map.mem_start, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_start));
			err |= __put_user(ifr.ifr_map.mem_end, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.mem_end));
			err |= __put_user(ifr.ifr_map.base_addr, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.base_addr));
			err |= __put_user(ifr.ifr_map.irq, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.irq));
			err |= __put_user(ifr.ifr_map.dma, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.dma));
			err |= __put_user(ifr.ifr_map.port, &(((struct ifreq32 *)arg)->ifr_ifru.ifru_map.port));
			if (err)
				err = -EFAULT;
			break;
		}
	}
	return err;
}

struct rtentry32 {
        u32   		rt_pad1;
        struct sockaddr rt_dst;         /* target address               */
        struct sockaddr rt_gateway;     /* gateway addr (RTF_GATEWAY)   */
        struct sockaddr rt_genmask;     /* target network mask (IP)     */
        unsigned short  rt_flags;
        short           rt_pad2;
        u32   		rt_pad3;
        unsigned char   rt_tos;
        unsigned char   rt_class;
        short           rt_pad4;
        short           rt_metric;      /* +1 for binary compatibility! */
        /* char * */ u32 rt_dev;        /* forcing the device at add    */
        u32   		rt_mtu;         /* per route MTU/Window         */
        u32   		rt_window;      /* Window clamping              */
        unsigned short  rt_irtt;        /* Initial RTT                  */

};

struct in6_rtmsg32 {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_src;
	struct in6_addr		rtmsg_gateway;
	u32			rtmsg_type;
	u16			rtmsg_dst_len;
	u16			rtmsg_src_len;
	u32			rtmsg_metric;
	u32			rtmsg_info;
	u32			rtmsg_flags;
	s32			rtmsg_ifindex;
};

static int routing_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int ret;
	void *r = NULL;
	struct in6_rtmsg r6;
	struct rtentry r4;
	char devname[16];
	u32 rtdev;
	mm_segment_t old_fs = get_fs();
	
	struct socket *mysock = sockfd_lookup(fd, &ret);

	if (mysock && mysock->sk && mysock->sk->sk_family == AF_INET6) { /* ipv6 */
		ret = copy_from_user (&r6.rtmsg_dst, &(((struct in6_rtmsg32 *)arg)->rtmsg_dst),
			3 * sizeof(struct in6_addr));
		ret |= __get_user (r6.rtmsg_type, &(((struct in6_rtmsg32 *)arg)->rtmsg_type));
		ret |= __get_user (r6.rtmsg_dst_len, &(((struct in6_rtmsg32 *)arg)->rtmsg_dst_len));
		ret |= __get_user (r6.rtmsg_src_len, &(((struct in6_rtmsg32 *)arg)->rtmsg_src_len));
		ret |= __get_user (r6.rtmsg_metric, &(((struct in6_rtmsg32 *)arg)->rtmsg_metric));
		ret |= __get_user (r6.rtmsg_info, &(((struct in6_rtmsg32 *)arg)->rtmsg_info));
		ret |= __get_user (r6.rtmsg_flags, &(((struct in6_rtmsg32 *)arg)->rtmsg_flags));
		ret |= __get_user (r6.rtmsg_ifindex, &(((struct in6_rtmsg32 *)arg)->rtmsg_ifindex));
		
		r = (void *) &r6;
	} else { /* ipv4 */
		ret = copy_from_user (&r4.rt_dst, &(((struct rtentry32 *)arg)->rt_dst), 3 * sizeof(struct sockaddr));
		ret |= __get_user (r4.rt_flags, &(((struct rtentry32 *)arg)->rt_flags));
		ret |= __get_user (r4.rt_metric, &(((struct rtentry32 *)arg)->rt_metric));
		ret |= __get_user (r4.rt_mtu, &(((struct rtentry32 *)arg)->rt_mtu));
		ret |= __get_user (r4.rt_window, &(((struct rtentry32 *)arg)->rt_window));
		ret |= __get_user (r4.rt_irtt, &(((struct rtentry32 *)arg)->rt_irtt));
		ret |= __get_user (rtdev, &(((struct rtentry32 *)arg)->rt_dev));
		if (rtdev) {
			ret |= copy_from_user (devname, compat_ptr(rtdev), 15);
			r4.rt_dev = devname; devname[15] = 0;
		} else
			r4.rt_dev = 0;

		r = (void *) &r4;
	}

	if (ret)
		return -EFAULT;

	set_fs (KERNEL_DS);
	ret = sys_ioctl (fd, cmd, (long) r);
	set_fs (old_fs);

	if (mysock)
		sockfd_put(mysock);

	return ret;
}

struct hd_geometry32 {
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	u32 start;
};
                        
static int hdio_getgeo(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct hd_geometry geo;
	int err;
	
	set_fs (KERNEL_DS);
	err = sys_ioctl(fd, HDIO_GETGEO, (unsigned long)&geo);
	set_fs (old_fs);
	if (!err) {
		err = copy_to_user ((struct hd_geometry32 *)arg, &geo, 4);
		err |= __put_user (geo.start, &(((struct hd_geometry32 *)arg)->start));
	}
	return err ? -EFAULT : 0;
}

struct fb_fix_screeninfo32 {
	char			id[16];
        compat_caddr_t	smem_start;
	u32			smem_len;
	u32			type;
	u32			type_aux;
	u32			visual;
	u16			xpanstep;
	u16			ypanstep;
	u16			ywrapstep;
	u32			line_length;
        compat_caddr_t	mmio_start;
	u32			mmio_len;
	u32			accel;
	u16			reserved[3];
};

struct fb_cmap32 {
	u32			start;
	u32			len;
	compat_caddr_t	red;
	compat_caddr_t	green;
	compat_caddr_t	blue;
	compat_caddr_t	transp;
};

static int do_cmap_ptr(__u16 **ptr64, __u32 *ptr32)
{
	__u32 data;
	void *datap;

	if (get_user(data, ptr32))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(datap, ptr64))
		return -EFAULT;
	return 0;
}

static int fb_getput_cmap(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fb_cmap *cmap;
	struct fb_cmap32 *cmap32;
	int err;

	cmap = compat_alloc_user_space(sizeof(*cmap));
	cmap32 = (struct fb_cmap32 *) arg;

	if (copy_in_user(&cmap->start, &cmap32->start, 2 * sizeof(__u32)))
		return -EFAULT;

	if (do_cmap_ptr(&cmap->red, &cmap32->red) ||
	    do_cmap_ptr(&cmap->green, &cmap32->green) ||
	    do_cmap_ptr(&cmap->blue, &cmap32->blue) ||
	    do_cmap_ptr(&cmap->transp, &cmap32->transp))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) cmap);

	if (!err) {
		if (copy_in_user(&cmap32->start,
				 &cmap->start,
				 2 * sizeof(__u32)))
			err = -EFAULT;
	}
	return err;
}

static int do_fscreeninfo_to_user(struct fb_fix_screeninfo *fix,
				  struct fb_fix_screeninfo32 *fix32)
{
	__u32 data;
	int err;

	err = copy_to_user(&fix32->id, &fix->id, sizeof(fix32->id));

	data = (__u32) (unsigned long) fix->smem_start;
	err |= put_user(data, &fix32->smem_start);

	err |= put_user(fix->smem_len, &fix32->smem_len);
	err |= put_user(fix->type, &fix32->type);
	err |= put_user(fix->type_aux, &fix32->type_aux);
	err |= put_user(fix->visual, &fix32->visual);
	err |= put_user(fix->xpanstep, &fix32->xpanstep);
	err |= put_user(fix->ypanstep, &fix32->ypanstep);
	err |= put_user(fix->ywrapstep, &fix32->ywrapstep);
	err |= put_user(fix->line_length, &fix32->line_length);

	data = (__u32) (unsigned long) fix->mmio_start;
	err |= put_user(data, &fix32->mmio_start);

	err |= put_user(fix->mmio_len, &fix32->mmio_len);
	err |= put_user(fix->accel, &fix32->accel);
	err |= copy_to_user(fix32->reserved, fix->reserved,
			    sizeof(fix->reserved));

	return err;
}

static int fb_get_fscreeninfo(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs;
	struct fb_fix_screeninfo fix;
	struct fb_fix_screeninfo32 *fix32;
	int err;

	fix32 = (struct fb_fix_screeninfo32 *) arg;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long) &fix);
	set_fs(old_fs);

	if (!err)
		err = do_fscreeninfo_to_user(&fix, fix32);

	return err;
}

static int fb_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case FBIOGET_FSCREENINFO:
		err = fb_get_fscreeninfo(fd,cmd, arg);
		break;

  	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		err = fb_getput_cmap(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("%s: Unknown fb ioctl cmd fd(%d) "
				       "cmd(%08x) arg(%08lx)\n",
				       __FUNCTION__, fd, cmd, arg);
		} while(0);
		err = -ENOSYS;
		break;
	};

	return err;
}

static int hdio_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	unsigned long kval;
	unsigned int *uvp;
	int error;

	set_fs(KERNEL_DS);
	error = sys_ioctl(fd, cmd, (long)&kval);
	set_fs(old_fs);

	if(error == 0) {
		uvp = (unsigned int *)arg;
		if(put_user(kval, uvp))
			error = -EFAULT;
	}
	return error;
}


typedef struct sg_io_hdr32 {
	compat_int_t interface_id;	/* [i] 'S' for SCSI generic (required) */
	compat_int_t dxfer_direction;	/* [i] data transfer direction  */
	unsigned char cmd_len;		/* [i] SCSI command length ( <= 16 bytes) */
	unsigned char mx_sb_len;		/* [i] max length to write to sbp */
	unsigned short iovec_count;	/* [i] 0 implies no scatter gather */
	compat_uint_t dxfer_len;		/* [i] byte count of data transfer */
	compat_uint_t dxferp;		/* [i], [*io] points to data transfer memory
					      or scatter gather list */
	compat_uptr_t cmdp;		/* [i], [*i] points to command to perform */
	compat_uptr_t sbp;		/* [i], [*o] points to sense_buffer memory */
	compat_uint_t timeout;		/* [i] MAX_UINT->no timeout (unit: millisec) */
	compat_uint_t flags;		/* [i] 0 -> default, see SG_FLAG... */
	compat_int_t pack_id;		/* [i->o] unused internally (normally) */
	compat_uptr_t usr_ptr;		/* [i->o] unused internally */
	unsigned char status;		/* [o] scsi status */
	unsigned char masked_status;	/* [o] shifted, masked scsi status */
	unsigned char msg_status;		/* [o] messaging level data (optional) */
	unsigned char sb_len_wr;		/* [o] byte count actually written to sbp */
	unsigned short host_status;	/* [o] errors from host adapter */
	unsigned short driver_status;	/* [o] errors from software driver */
	compat_int_t resid;		/* [o] dxfer_len - actual_transferred */
	compat_uint_t duration;		/* [o] time taken by cmd (unit: millisec) */
	compat_uint_t info;		/* [o] auxiliary information */
} sg_io_hdr32_t;  /* 64 bytes long (on sparc32) */

typedef struct sg_iovec32 {
	compat_uint_t iov_base;
	compat_uint_t iov_len;
} sg_iovec32_t;

static int sg_build_iovec(sg_io_hdr_t *sgio, void *dxferp, u16 iovec_count)
{
	sg_iovec_t *iov = (sg_iovec_t *) (sgio + 1);
	sg_iovec32_t *iov32 = dxferp;
	int i;

	for (i = 0; i < iovec_count; i++) {
		u32 base, len;

		if (get_user(base, &iov32[i].iov_base) ||
		    get_user(len, &iov32[i].iov_len) ||
		    put_user((void *)(unsigned long)base, &iov[i].iov_base) ||
		    put_user(len, &iov[i].iov_len))
			return -EFAULT;
	}

	return 0;
}

static int sg_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	sg_io_hdr_t *sgio;
	sg_io_hdr32_t *sgio32;
	u16 iovec_count;
	u32 data;
	void *dxferp;
	int err;

	sgio32 = (sg_io_hdr32_t *) arg;
	if (get_user(iovec_count, &sgio32->iovec_count))
		return -EFAULT;

	{
		void *new, *top;

		top = compat_alloc_user_space(0);
		new = compat_alloc_user_space(sizeof(sg_io_hdr_t) +
				       (iovec_count *
					sizeof(sg_iovec_t)));
		if (new > top)
			return -EINVAL;

		sgio = new;
	}

	/* Ok, now construct.  */
	if (copy_in_user(&sgio->interface_id, &sgio32->interface_id,
			 (2 * sizeof(int)) +
			 (2 * sizeof(unsigned char)) +
			 (1 * sizeof(unsigned short)) +
			 (1 * sizeof(unsigned int))))
		return -EFAULT;

	if (get_user(data, &sgio32->dxferp))
		return -EFAULT;
	dxferp = (void *) (unsigned long) data;
	if (iovec_count) {
		if (sg_build_iovec(sgio, dxferp, iovec_count))
			return -EFAULT;
	} else {
		if (put_user(dxferp, &sgio->dxferp))
			return -EFAULT;
	}

	{
		unsigned char *cmdp, *sbp;

		if (get_user(data, &sgio32->cmdp))
			return -EFAULT;
		cmdp = (unsigned char *) (unsigned long) data;

		if (get_user(data, &sgio32->sbp))
			return -EFAULT;
		sbp = (unsigned char *) (unsigned long) data;

		if (put_user(cmdp, &sgio->cmdp) ||
		    put_user(sbp, &sgio->sbp))
			return -EFAULT;
	}

	if (copy_in_user(&sgio->timeout, &sgio32->timeout,
			 3 * sizeof(int)))
		return -EFAULT;

	if (get_user(data, &sgio32->usr_ptr))
		return -EFAULT;
	if (put_user((void *)(unsigned long)data, &sgio->usr_ptr))
		return -EFAULT;

	if (copy_in_user(&sgio->status, &sgio32->status,
			 (4 * sizeof(unsigned char)) +
			 (2 * sizeof(unsigned (short))) +
			 (3 * sizeof(int))))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) sgio);

	if (err >= 0) {
		void *datap;

		if (copy_in_user(&sgio32->pack_id, &sgio->pack_id,
				 sizeof(int)) ||
		    get_user(datap, &sgio->usr_ptr) ||
		    put_user((u32)(unsigned long)datap,
			     &sgio32->usr_ptr) ||
		    copy_in_user(&sgio32->status, &sgio->status,
				 (4 * sizeof(unsigned char)) +
				 (2 * sizeof(unsigned short)) +
				 (3 * sizeof(int))))
			err = -EFAULT;
	}

	return err;
}

struct sock_fprog32 {
	unsigned short	len;
	compat_caddr_t	filter;
};

#define PPPIOCSPASS32	_IOW('t', 71, struct sock_fprog32)
#define PPPIOCSACTIVE32	_IOW('t', 70, struct sock_fprog32)

static int ppp_sock_fprog_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct sock_fprog32 *u_fprog32 = (struct sock_fprog32 *) arg;
	struct sock_fprog *u_fprog64 = compat_alloc_user_space(sizeof(struct sock_fprog));
	void *fptr64;
	u32 fptr32;
	u16 flen;

	if (get_user(flen, &u_fprog32->len) ||
	    get_user(fptr32, &u_fprog32->filter))
		return -EFAULT;

	fptr64 = compat_ptr(fptr32);

	if (put_user(flen, &u_fprog64->len) ||
	    put_user(fptr64, &u_fprog64->filter))
		return -EFAULT;

	if (cmd == PPPIOCSPASS32)
		cmd = PPPIOCSPASS;
	else
		cmd = PPPIOCSACTIVE;

	return sys_ioctl(fd, cmd, (unsigned long) u_fprog64);
}

struct ppp_option_data32 {
	compat_caddr_t	ptr;
	u32			length;
	compat_int_t		transmit;
};
#define PPPIOCSCOMPRESS32	_IOW('t', 77, struct ppp_option_data32)

struct ppp_idle32 {
	compat_time_t xmit_idle;
	compat_time_t recv_idle;
};
#define PPPIOCGIDLE32		_IOR('t', 63, struct ppp_idle32)

static int ppp_gidle(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ppp_idle *idle;
	struct ppp_idle32 *idle32;
	__kernel_time_t xmit, recv;
	int err;

	idle = compat_alloc_user_space(sizeof(*idle));
	idle32 = (struct ppp_idle32 *) arg;

	err = sys_ioctl(fd, PPPIOCGIDLE, (unsigned long) idle);

	if (!err) {
		if (get_user(xmit, &idle->xmit_idle) ||
		    get_user(recv, &idle->recv_idle) ||
		    put_user(xmit, &idle32->xmit_idle) ||
		    put_user(recv, &idle32->recv_idle))
			err = -EFAULT;
	}
	return err;
}

static int ppp_scompress(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct ppp_option_data *odata;
	struct ppp_option_data32 *odata32;
	__u32 data;
	void *datap;

	odata = compat_alloc_user_space(sizeof(*odata));
	odata32 = (struct ppp_option_data32 *) arg;

	if (get_user(data, &odata32->ptr))
		return -EFAULT;

	datap = (void *) (unsigned long) data;
	if (put_user(datap, &odata->ptr))
		return -EFAULT;

	if (copy_in_user(&odata->length, &odata32->length,
			 sizeof(__u32) + sizeof(int)))
		return -EFAULT;

	return sys_ioctl(fd, PPPIOCSCOMPRESS, (unsigned long) odata);
}

static int ppp_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch (cmd) {
	case PPPIOCGIDLE32:
		err = ppp_gidle(fd, cmd, arg);
		break;

	case PPPIOCSCOMPRESS32:
		err = ppp_scompress(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("ppp_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		err = -EINVAL;
		break;
	};

	return err;
}


struct mtget32 {
	compat_long_t	mt_type;
	compat_long_t	mt_resid;
	compat_long_t	mt_dsreg;
	compat_long_t	mt_gstat;
	compat_long_t	mt_erreg;
	compat_daddr_t	mt_fileno;
	compat_daddr_t	mt_blkno;
};
#define MTIOCGET32	_IOR('m', 2, struct mtget32)

struct mtpos32 {
	compat_long_t	mt_blkno;
};
#define MTIOCPOS32	_IOR('m', 3, struct mtpos32)

struct mtconfiginfo32 {
	compat_long_t	mt_type;
	compat_long_t	ifc_type;
	unsigned short	irqnr;
	unsigned short	dmanr;
	unsigned short	port;
	compat_ulong_t	debug;
	compat_uint_t	have_dens:1;
	compat_uint_t	have_bsf:1;
	compat_uint_t	have_fsr:1;
	compat_uint_t	have_bsr:1;
	compat_uint_t	have_eod:1;
	compat_uint_t	have_seek:1;
	compat_uint_t	have_tell:1;
	compat_uint_t	have_ras1:1;
	compat_uint_t	have_ras2:1;
	compat_uint_t	have_ras3:1;
	compat_uint_t	have_qfa:1;
	compat_uint_t	pad1:5;
	char	reserved[10];
};
#define	MTIOCGETCONFIG32	_IOR('m', 4, struct mtconfiginfo32)
#define	MTIOCSETCONFIG32	_IOW('m', 5, struct mtconfiginfo32)

static int mt_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct mtconfiginfo info;
	struct mtget get;
	struct mtpos pos;
	unsigned long kcmd;
	void *karg;
	int err = 0;

	switch(cmd) {
	case MTIOCPOS32:
		kcmd = MTIOCPOS;
		karg = &pos;
		break;
	case MTIOCGET32:
		kcmd = MTIOCGET;
		karg = &get;
		break;
	case MTIOCGETCONFIG32:
		kcmd = MTIOCGETCONFIG;
		karg = &info;
		break;
	case MTIOCSETCONFIG32:
		kcmd = MTIOCSETCONFIG;
		karg = &info;
		err = __get_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __get_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __get_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __get_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __get_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __get_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_from_user((char *)&info.debug + sizeof(info.debug),
				     (char *)&((struct mtconfiginfo32 *)arg)->debug
				     + sizeof(((struct mtconfiginfo32 *)arg)->debug), sizeof(__u32));
		if (err)
			return -EFAULT;
		break;
	default:
		do {
			static int count;
			if (++count <= 20)
				printk("mt_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		return err;
	switch (cmd) {
	case MTIOCPOS32:
		err = __put_user(pos.mt_blkno, &((struct mtpos32 *)arg)->mt_blkno);
		break;
	case MTIOCGET32:
		err = __put_user(get.mt_type, &((struct mtget32 *)arg)->mt_type);
		err |= __put_user(get.mt_resid, &((struct mtget32 *)arg)->mt_resid);
		err |= __put_user(get.mt_dsreg, &((struct mtget32 *)arg)->mt_dsreg);
		err |= __put_user(get.mt_gstat, &((struct mtget32 *)arg)->mt_gstat);
		err |= __put_user(get.mt_erreg, &((struct mtget32 *)arg)->mt_erreg);
		err |= __put_user(get.mt_fileno, &((struct mtget32 *)arg)->mt_fileno);
		err |= __put_user(get.mt_blkno, &((struct mtget32 *)arg)->mt_blkno);
		break;
	case MTIOCGETCONFIG32:
		err = __put_user(info.mt_type, &((struct mtconfiginfo32 *)arg)->mt_type);
		err |= __put_user(info.ifc_type, &((struct mtconfiginfo32 *)arg)->ifc_type);
		err |= __put_user(info.irqnr, &((struct mtconfiginfo32 *)arg)->irqnr);
		err |= __put_user(info.dmanr, &((struct mtconfiginfo32 *)arg)->dmanr);
		err |= __put_user(info.port, &((struct mtconfiginfo32 *)arg)->port);
		err |= __put_user(info.debug, &((struct mtconfiginfo32 *)arg)->debug);
		err |= __copy_to_user((char *)&((struct mtconfiginfo32 *)arg)->debug
			    		   + sizeof(((struct mtconfiginfo32 *)arg)->debug),
					   (char *)&info.debug + sizeof(info.debug), sizeof(__u32));
		break;
	case MTIOCSETCONFIG32:
		break;
	}
	return err ? -EFAULT: 0;
}

struct cdrom_read_audio32 {
	union cdrom_addr	addr;
	u8			addr_format;
	compat_int_t		nframes;
	compat_caddr_t		buf;
};

struct cdrom_generic_command32 {
	unsigned char	cmd[CDROM_PACKET_SIZE];
	compat_caddr_t	buffer;
	compat_uint_t	buflen;
	compat_int_t	stat;
	compat_caddr_t	sense;
	unsigned char	data_direction;
	compat_int_t	quiet;
	compat_int_t	timeout;
	compat_caddr_t	reserved[1];
};
  
static int cdrom_do_read_audio(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct cdrom_read_audio *cdread_audio;
	struct cdrom_read_audio32 *cdread_audio32;
	__u32 data;
	void *datap;

	cdread_audio = compat_alloc_user_space(sizeof(*cdread_audio));
	cdread_audio32 = (struct cdrom_read_audio32 *) arg;

	if (copy_in_user(&cdread_audio->addr,
			 &cdread_audio32->addr,
			 (sizeof(*cdread_audio32) -
			  sizeof(compat_caddr_t))))
	 	return -EFAULT;

	if (get_user(data, &cdread_audio32->buf))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(datap, &cdread_audio->buf))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) cdread_audio);
}

static int __cgc_do_ptr(void **ptr64, __u32 *ptr32)
{
	u32 data;
	void *datap;

	if (get_user(data, ptr32))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(datap, ptr64))
		return -EFAULT;

	return 0;
}

static int cdrom_do_generic_command(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct cdrom_generic_command *cgc;
	struct cdrom_generic_command32 *cgc32;
	unsigned char dir;

	cgc = compat_alloc_user_space(sizeof(*cgc));
	cgc32 = (struct cdrom_generic_command32 *) arg;

	if (copy_in_user(&cgc->cmd, &cgc32->cmd, sizeof(cgc->cmd)) ||
	    __cgc_do_ptr((void **) &cgc->buffer, &cgc32->buffer) ||
	    copy_in_user(&cgc->buflen, &cgc32->buflen,
			 (sizeof(unsigned int) + sizeof(int))) ||
	    __cgc_do_ptr((void **) &cgc->sense, &cgc32->sense))
		return -EFAULT;

	if (get_user(dir, &cgc->data_direction) ||
	    put_user(dir, &cgc32->data_direction))
		return -EFAULT;

	if (copy_in_user(&cgc->quiet, &cgc32->quiet,
			 2 * sizeof(int)))
		return -EFAULT;

	if (__cgc_do_ptr(&cgc->reserved[0], &cgc32->reserved[0]))
		return -EFAULT;

	return sys_ioctl(fd, cmd, (unsigned long) cgc);
}

static int cdrom_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	int err;

	switch(cmd) {
	case CDROMREADAUDIO:
		err = cdrom_do_read_audio(fd, cmd, arg);
		break;

	case CDROM_SEND_PACKET:
		err = cdrom_do_generic_command(fd, cmd, arg);
		break;

	default:
		do {
			static int count;
			if (++count <= 20)
				printk("cdrom_ioctl: Unknown cmd fd(%d) "
				       "cmd(%08x) arg(%08x)\n",
				       (int)fd, (unsigned int)cmd, (unsigned int)arg);
		} while(0);
		err = -EINVAL;
		break;
	};

	return err;
}

struct loop_info32 {
	compat_int_t	lo_number;      /* ioctl r/o */
	compat_dev_t	lo_device;      /* ioctl r/o */
	compat_ulong_t	lo_inode;       /* ioctl r/o */
	compat_dev_t	lo_rdevice;     /* ioctl r/o */
	compat_int_t	lo_offset;
	compat_int_t	lo_encrypt_type;
	compat_int_t	lo_encrypt_key_size;    /* ioctl w/o */
	compat_int_t	lo_flags;       /* ioctl r/o */
	char		lo_name[LO_NAME_SIZE];
	unsigned char	lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	compat_ulong_t	lo_init[2];
	char		reserved[4];
};

static int loop_status(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	struct loop_info l;
	int err = -EINVAL;

	switch(cmd) {
	case LOOP_SET_STATUS:
		err = get_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
		err |= __get_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
		err |= __get_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
		err |= __get_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
		err |= __copy_from_user((char *)&l.lo_offset, (char *)&((struct loop_info32 *)arg)->lo_offset,
					   8 + (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
		if (err) {
			err = -EFAULT;
		} else {
			set_fs (KERNEL_DS);
			err = sys_ioctl (fd, cmd, (unsigned long)&l);
			set_fs (old_fs);
		}
		break;
	case LOOP_GET_STATUS:
		set_fs (KERNEL_DS);
		err = sys_ioctl (fd, cmd, (unsigned long)&l);
		set_fs (old_fs);
		if (!err) {
			err = put_user(l.lo_number, &((struct loop_info32 *)arg)->lo_number);
			err |= __put_user(l.lo_device, &((struct loop_info32 *)arg)->lo_device);
			err |= __put_user(l.lo_inode, &((struct loop_info32 *)arg)->lo_inode);
			err |= __put_user(l.lo_rdevice, &((struct loop_info32 *)arg)->lo_rdevice);
			err |= __copy_to_user((char *)&((struct loop_info32 *)arg)->lo_offset,
					   (char *)&l.lo_offset, (unsigned long)l.lo_init - (unsigned long)&l.lo_offset);
			if (err)
				err = -EFAULT;
		}
		break;
	default: {
		static int count;
		if (++count <= 20)
			printk("%s: Unknown loop ioctl cmd, fd(%d) "
			       "cmd(%08x) arg(%08lx)\n",
			       __FUNCTION__, fd, cmd, arg);
	}
	}
	return err;
}

extern int tty_ioctl(struct inode * inode, struct file * file, unsigned int cmd, unsigned long arg);

#ifdef CONFIG_VT

static int vt_check(struct file *file)
{
	struct tty_struct *tty;
	struct inode *inode = file->f_dentry->d_inode;
	
	if (file->f_op->ioctl != tty_ioctl)
		return -EINVAL;
	                
	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode->i_rdev, "tty_ioctl"))
		return -EINVAL;
	                                                
	if (tty->driver->ioctl != vt_ioctl)
		return -EINVAL;
	
	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or super-user.
	 */
	if (current->tty == tty || capable(CAP_SYS_ADMIN))
		return 1;
	return 0;                                                    
}

struct consolefontdesc32 {
	unsigned short charcount;       /* characters in font (256 or 512) */
	unsigned short charheight;      /* scan lines per character (1-32) */
	compat_caddr_t chardata;	/* font data in expanded form */
};

static int do_fontx_ioctl(unsigned int fd, int cmd, struct consolefontdesc32 *user_cfd, struct file *file)
{
	struct consolefontdesc cfdarg;
	struct console_font_op op;
	int i, perm;

	perm = vt_check(file);
	if (perm < 0) return perm;
	
	if (copy_from_user(&cfdarg, user_cfd, sizeof(struct consolefontdesc32)))
		return -EFAULT;
	
	cfdarg.chardata = compat_ptr(((struct consolefontdesc32 *)&cfdarg)->chardata);
 	
	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		op.flags = 0;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		return con_font_op(fg_console, &op);
	case GIO_FONTX:
		if (!cfdarg.chardata)
			return 0;
		op.op = KD_FONT_OP_GET;
		op.flags = 0;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		i = con_font_op(fg_console, &op);
		if (i)
			return i;
		cfdarg.charheight = op.height;
		cfdarg.charcount = op.charcount;
		((struct consolefontdesc32 *)&cfdarg)->chardata	= (unsigned long)cfdarg.chardata;
		if (copy_to_user(user_cfd, &cfdarg, sizeof(struct consolefontdesc32)))
			return -EFAULT;
		return 0;
	}
	return -EINVAL;
}

struct console_font_op32 {
	compat_uint_t op;        /* operation code KD_FONT_OP_* */
	compat_uint_t flags;     /* KD_FONT_FLAG_* */
	compat_uint_t width, height;     /* font size */
	compat_uint_t charcount;
	compat_caddr_t data;    /* font data with height fixed to 32 */
};
                                        
static int do_kdfontop_ioctl(unsigned int fd, unsigned int cmd, struct console_font_op32 *fontop, struct file *file)
{
	struct console_font_op op;
	int perm = vt_check(file), i;
	struct vt_struct *vt;
	
	if (perm < 0) return perm;
	
	if (copy_from_user(&op, (void *) fontop, sizeof(struct console_font_op32)))
		return -EFAULT;
	if (!perm && op.op != KD_FONT_OP_GET)
		return -EPERM;
	op.data = compat_ptr(((struct console_font_op32 *)&op)->data);
	op.flags |= KD_FONT_FLAG_OLD;
	vt = (struct vt_struct *)((struct tty_struct *)file->private_data)->driver_data;
	i = con_font_op(vt->vc_num, &op);
	if (i) return i;
	((struct console_font_op32 *)&op)->data = (unsigned long)op.data;
	if (copy_to_user((void *) fontop, &op, sizeof(struct console_font_op32)))
		return -EFAULT;
	return 0;
}

struct unimapdesc32 {
	unsigned short entry_ct;
	compat_caddr_t entries;
};

static int do_unimap_ioctl(unsigned int fd, unsigned int cmd, struct unimapdesc32 *user_ud, struct file *file)
{
	struct unimapdesc32 tmp;
	int perm = vt_check(file);
	
	if (perm < 0) return perm;
	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm) return -EPERM;
		return con_set_unimap(fg_console, tmp.entry_ct, compat_ptr(tmp.entries));
	case GIO_UNIMAP:
		return con_get_unimap(fg_console, tmp.entry_ct, &(user_ud->entry_ct), compat_ptr(tmp.entries));
	}
	return 0;
}

#endif /* CONFIG_VT */

static int do_smb_getmountuid(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	__kernel_uid_t kuid;
	int err;

	cmd = SMB_IOC_GETMOUNTUID;

	set_fs(KERNEL_DS);
	err = sys_ioctl(fd, cmd, (unsigned long)&kuid);
	set_fs(old_fs);

	if (err >= 0)
		err = put_user(kuid, (compat_pid_t *)arg);

	return err;
}

struct atmif_sioc32 {
        compat_int_t	number;
        compat_int_t	length;
        compat_caddr_t	arg;
};

struct atm_iobuf32 {
	compat_int_t	length;
	compat_caddr_t	buffer;
};

#define ATM_GETLINKRATE32 _IOW('a', ATMIOC_ITF+1, struct atmif_sioc32)
#define ATM_GETNAMES32    _IOW('a', ATMIOC_ITF+3, struct atm_iobuf32)
#define ATM_GETTYPE32     _IOW('a', ATMIOC_ITF+4, struct atmif_sioc32)
#define ATM_GETESI32	  _IOW('a', ATMIOC_ITF+5, struct atmif_sioc32)
#define ATM_GETADDR32	  _IOW('a', ATMIOC_ITF+6, struct atmif_sioc32)
#define ATM_RSTADDR32	  _IOW('a', ATMIOC_ITF+7, struct atmif_sioc32)
#define ATM_ADDADDR32	  _IOW('a', ATMIOC_ITF+8, struct atmif_sioc32)
#define ATM_DELADDR32	  _IOW('a', ATMIOC_ITF+9, struct atmif_sioc32)
#define ATM_GETCIRANGE32  _IOW('a', ATMIOC_ITF+10, struct atmif_sioc32)
#define ATM_SETCIRANGE32  _IOW('a', ATMIOC_ITF+11, struct atmif_sioc32)
#define ATM_SETESI32      _IOW('a', ATMIOC_ITF+12, struct atmif_sioc32)
#define ATM_SETESIF32     _IOW('a', ATMIOC_ITF+13, struct atmif_sioc32)
#define ATM_GETSTAT32     _IOW('a', ATMIOC_SARCOM+0, struct atmif_sioc32)
#define ATM_GETSTATZ32    _IOW('a', ATMIOC_SARCOM+1, struct atmif_sioc32)
#define ATM_GETLOOP32	  _IOW('a', ATMIOC_SARCOM+2, struct atmif_sioc32)
#define ATM_SETLOOP32	  _IOW('a', ATMIOC_SARCOM+3, struct atmif_sioc32)
#define ATM_QUERYLOOP32	  _IOW('a', ATMIOC_SARCOM+4, struct atmif_sioc32)

static struct {
        unsigned int cmd32;
        unsigned int cmd;
} atm_ioctl_map[] = {
        { ATM_GETLINKRATE32, ATM_GETLINKRATE },
	{ ATM_GETNAMES32,    ATM_GETNAMES },
        { ATM_GETTYPE32,     ATM_GETTYPE },
        { ATM_GETESI32,      ATM_GETESI },
        { ATM_GETADDR32,     ATM_GETADDR },
        { ATM_RSTADDR32,     ATM_RSTADDR },
        { ATM_ADDADDR32,     ATM_ADDADDR },
        { ATM_DELADDR32,     ATM_DELADDR },
        { ATM_GETCIRANGE32,  ATM_GETCIRANGE },
	{ ATM_SETCIRANGE32,  ATM_SETCIRANGE },
	{ ATM_SETESI32,      ATM_SETESI },
	{ ATM_SETESIF32,     ATM_SETESIF },
	{ ATM_GETSTAT32,     ATM_GETSTAT },
	{ ATM_GETSTATZ32,    ATM_GETSTATZ },
	{ ATM_GETLOOP32,     ATM_GETLOOP },
	{ ATM_SETLOOP32,     ATM_SETLOOP },
	{ ATM_QUERYLOOP32,   ATM_QUERYLOOP }
};

#define NR_ATM_IOCTL (sizeof(atm_ioctl_map)/sizeof(atm_ioctl_map[0]))


static int do_atm_iobuf(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct atm_iobuf   *iobuf;
	struct atm_iobuf32 *iobuf32;
	u32 data;
	void *datap;
	int len, err;

	iobuf = compat_alloc_user_space(sizeof(*iobuf));
	iobuf32 = (struct atm_iobuf32 *) arg;

	if (get_user(len, &iobuf32->length) ||
	    get_user(data, &iobuf32->buffer))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(len, &iobuf->length) ||
	    put_user(datap, &iobuf->buffer))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long)iobuf);

	if (!err) {
		if (copy_in_user(&iobuf32->length, &iobuf->length,
				 sizeof(int)))
			err = -EFAULT;
	}

	return err;
}

static int do_atmif_sioc(unsigned int fd, unsigned int cmd, unsigned long arg)
{
        struct atmif_sioc   *sioc;
	struct atmif_sioc32 *sioc32;
	u32 data;
	void *datap;
	int err;
        
	sioc = compat_alloc_user_space(sizeof(*sioc));
	sioc32 = (struct atmif_sioc32 *) arg;

	if (copy_in_user(&sioc->number, &sioc32->number, 2 * sizeof(int)) ||
	    get_user(data, &sioc32->arg))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(datap, &sioc->arg))
		return -EFAULT;

	err = sys_ioctl(fd, cmd, (unsigned long) sioc);

	if (!err) {
		if (copy_in_user(&sioc32->length, &sioc->length,
				 sizeof(int)))
			err = -EFAULT;
	}
	return err;
}

static int do_atm_ioctl(unsigned int fd, unsigned int cmd32, unsigned long arg)
{
        int i;
        unsigned int cmd = 0;
        
	switch (cmd32) {
	case SONET_GETSTAT:
	case SONET_GETSTATZ:
	case SONET_GETDIAG:
	case SONET_SETDIAG:
	case SONET_CLRDIAG:
	case SONET_SETFRAMING:
	case SONET_GETFRAMING:
	case SONET_GETFRSENSE:
		return do_atmif_sioc(fd, cmd32, arg);
	}

	for (i = 0; i < NR_ATM_IOCTL; i++) {
		if (cmd32 == atm_ioctl_map[i].cmd32) {
			cmd = atm_ioctl_map[i].cmd;
			break;
		}
	}
	if (i == NR_ATM_IOCTL)
	        return -EINVAL;
        
        switch (cmd) {
	case ATM_GETNAMES:
		return do_atm_iobuf(fd, cmd, arg);
	    
	case ATM_GETLINKRATE:
        case ATM_GETTYPE:
        case ATM_GETESI:
        case ATM_GETADDR:
        case ATM_RSTADDR:
        case ATM_ADDADDR:
        case ATM_DELADDR:
        case ATM_GETCIRANGE:
	case ATM_SETCIRANGE:
	case ATM_SETESI:
	case ATM_SETESIF:
	case ATM_GETSTAT:
	case ATM_GETSTATZ:
	case ATM_GETLOOP:
	case ATM_SETLOOP:
	case ATM_QUERYLOOP:
                return do_atmif_sioc(fd, cmd, arg);
        }

        return -EINVAL;
}

static int ret_einval(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int broken_blkgetsize(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	/* The mkswap binary hard codes it to Intel value :-((( */
	return w_long(fd, BLKGETSIZE, arg);
}

struct blkpg_ioctl_arg32 {
	compat_int_t op;
	compat_int_t flags;
	compat_int_t datalen;
	compat_caddr_t data;
};
                                
static int blkpg_ioctl_trans(unsigned int fd, unsigned int cmd, struct blkpg_ioctl_arg32 *arg)
{
	struct blkpg_ioctl_arg a;
	struct blkpg_partition p;
	int err;
	mm_segment_t old_fs = get_fs();
	
	err = get_user(a.op, &arg->op);
	err |= __get_user(a.flags, &arg->flags);
	err |= __get_user(a.datalen, &arg->datalen);
	err |= __get_user((long)a.data, &arg->data);
	if (err) return err;
	switch (a.op) {
	case BLKPG_ADD_PARTITION:
	case BLKPG_DEL_PARTITION:
		if (a.datalen < sizeof(struct blkpg_partition))
			return -EINVAL;
                if (copy_from_user(&p, a.data, sizeof(struct blkpg_partition)))
			return -EFAULT;
		a.data = &p;
		set_fs (KERNEL_DS);
		err = sys_ioctl(fd, cmd, (unsigned long)&a);
		set_fs (old_fs);
	default:
		return -EINVAL;
	}                                        
	return err;
}

static int ioc_settimeout(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	return rw_long(fd, AUTOFS_IOC_SETTIMEOUT, arg);
}

/* Fix sizeof(sizeof()) breakage */
#define BLKBSZGET_32   _IOR(0x12,112,int)
#define BLKBSZSET_32   _IOW(0x12,113,int)
#define BLKGETSIZE64_32        _IOR(0x12,114,int)

static int do_blkbszget(unsigned int fd, unsigned int cmd, unsigned long arg)
{
       return sys_ioctl(fd, BLKBSZGET, arg);
}

static int do_blkbszset(unsigned int fd, unsigned int cmd, unsigned long arg)
{
       return sys_ioctl(fd, BLKBSZSET, arg);
}

static int do_blkgetsize64(unsigned int fd, unsigned int cmd,
                          unsigned long arg)
{
       return sys_ioctl(fd, BLKGETSIZE64, arg);
}

/* Bluetooth ioctls */
#define HCIUARTSETPROTO        _IOW('U', 200, int)
#define HCIUARTGETPROTO        _IOR('U', 201, int)

#define BNEPCONNADD    _IOW('B', 200, int)
#define BNEPCONNDEL    _IOW('B', 201, int)
#define BNEPGETCONNLIST        _IOR('B', 210, int)
#define BNEPGETCONNINFO        _IOR('B', 211, int)

struct floppy_struct32 {
	compat_uint_t	size;
	compat_uint_t	sect;
	compat_uint_t	head;
	compat_uint_t	track;
	compat_uint_t	stretch;
	unsigned char	gap;
	unsigned char	rate;
	unsigned char	spec1;
	unsigned char	fmt_gap;
	const compat_caddr_t name;
};

struct floppy_drive_params32 {
	char		cmos;
	compat_ulong_t	max_dtr;
	compat_ulong_t	hlt;
	compat_ulong_t	hut;
	compat_ulong_t	srt;
	compat_ulong_t	spinup;
	compat_ulong_t	spindown;
	unsigned char	spindown_offset;
	unsigned char	select_delay;
	unsigned char	rps;
	unsigned char	tracks;
	compat_ulong_t	timeout;
	unsigned char	interleave_sect;
	struct floppy_max_errors max_errors;
	char		flags;
	char		read_track;
	short		autodetect[8];
	compat_int_t	checkfreq;
	compat_int_t	native_format;
};

struct floppy_drive_struct32 {
	signed char	flags;
	compat_ulong_t	spinup_date;
	compat_ulong_t	select_date;
	compat_ulong_t	first_read_date;
	short		probed_format;
	short		track;
	short		maxblock;
	short		maxtrack;
	compat_int_t	generation;
	compat_int_t	keep_data;
	compat_int_t	fd_ref;
	compat_int_t	fd_device;
	compat_int_t	last_checked;
	compat_caddr_t dmabuf;
	compat_int_t	bufblocks;
};

struct floppy_fdc_state32 {
	compat_int_t	spec1;
	compat_int_t	spec2;
	compat_int_t	dtr;
	unsigned char	version;
	unsigned char	dor;
	compat_ulong_t	address;
	unsigned int	rawcmd:2;
	unsigned int	reset:1;
	unsigned int	need_configure:1;
	unsigned int	perp_mode:2;
	unsigned int	has_fifo:1;
	unsigned int	driver_version;
	unsigned char	track[4];
};

struct floppy_write_errors32 {
	unsigned int	write_errors;
	compat_ulong_t	first_error_sector;
	compat_int_t	first_error_generation;
	compat_ulong_t	last_error_sector;
	compat_int_t	last_error_generation;
	compat_uint_t	badness;
};

#define FDSETPRM32 _IOW(2, 0x42, struct floppy_struct32)
#define FDDEFPRM32 _IOW(2, 0x43, struct floppy_struct32)
#define FDGETPRM32 _IOR(2, 0x04, struct floppy_struct32)
#define FDSETDRVPRM32 _IOW(2, 0x90, struct floppy_drive_params32)
#define FDGETDRVPRM32 _IOR(2, 0x11, struct floppy_drive_params32)
#define FDGETDRVSTAT32 _IOR(2, 0x12, struct floppy_drive_struct32)
#define FDPOLLDRVSTAT32 _IOR(2, 0x13, struct floppy_drive_struct32)
#define FDGETFDCSTAT32 _IOR(2, 0x15, struct floppy_fdc_state32)
#define FDWERRORGET32  _IOR(2, 0x17, struct floppy_write_errors32)

static struct {
	unsigned int	cmd32;
	unsigned int	cmd;
} fd_ioctl_trans_table[] = {
	{ FDSETPRM32, FDSETPRM },
	{ FDDEFPRM32, FDDEFPRM },
	{ FDGETPRM32, FDGETPRM },
	{ FDSETDRVPRM32, FDSETDRVPRM },
	{ FDGETDRVPRM32, FDGETDRVPRM },
	{ FDGETDRVSTAT32, FDGETDRVSTAT },
	{ FDPOLLDRVSTAT32, FDPOLLDRVSTAT },
	{ FDGETFDCSTAT32, FDGETFDCSTAT },
	{ FDWERRORGET32, FDWERRORGET }
};

#define NR_FD_IOCTL_TRANS (sizeof(fd_ioctl_trans_table)/sizeof(fd_ioctl_trans_table[0]))

static int fd_ioctl_trans(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	mm_segment_t old_fs = get_fs();
	void *karg = NULL;
	unsigned int kcmd = 0;
	int i, err;

	for (i = 0; i < NR_FD_IOCTL_TRANS; i++)
		if (cmd == fd_ioctl_trans_table[i].cmd32) {
			kcmd = fd_ioctl_trans_table[i].cmd;
			break;
		}
	if (!kcmd)
		return -EINVAL;

	switch (cmd) {
		case FDSETPRM32:
		case FDDEFPRM32:
		case FDGETPRM32:
		{
			struct floppy_struct *f;

			f = karg = kmalloc(sizeof(struct floppy_struct), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETPRM32)
				break;
			err = __get_user(f->size, &((struct floppy_struct32 *)arg)->size);
			err |= __get_user(f->sect, &((struct floppy_struct32 *)arg)->sect);
			err |= __get_user(f->head, &((struct floppy_struct32 *)arg)->head);
			err |= __get_user(f->track, &((struct floppy_struct32 *)arg)->track);
			err |= __get_user(f->stretch, &((struct floppy_struct32 *)arg)->stretch);
			err |= __get_user(f->gap, &((struct floppy_struct32 *)arg)->gap);
			err |= __get_user(f->rate, &((struct floppy_struct32 *)arg)->rate);
			err |= __get_user(f->spec1, &((struct floppy_struct32 *)arg)->spec1);
			err |= __get_user(f->fmt_gap, &((struct floppy_struct32 *)arg)->fmt_gap);
			err |= __get_user((u64)f->name, &((struct floppy_struct32 *)arg)->name);
			if (err) {
				err = -EFAULT;
				goto out;
			}
			break;
		}
		case FDSETDRVPRM32:
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params *f;

			f = karg = kmalloc(sizeof(struct floppy_drive_params), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			if (cmd == FDGETDRVPRM32)
				break;
			err = __get_user(f->cmos, &((struct floppy_drive_params32 *)arg)->cmos);
			err |= __get_user(f->max_dtr, &((struct floppy_drive_params32 *)arg)->max_dtr);
			err |= __get_user(f->hlt, &((struct floppy_drive_params32 *)arg)->hlt);
			err |= __get_user(f->hut, &((struct floppy_drive_params32 *)arg)->hut);
			err |= __get_user(f->srt, &((struct floppy_drive_params32 *)arg)->srt);
			err |= __get_user(f->spinup, &((struct floppy_drive_params32 *)arg)->spinup);
			err |= __get_user(f->spindown, &((struct floppy_drive_params32 *)arg)->spindown);
			err |= __get_user(f->spindown_offset, &((struct floppy_drive_params32 *)arg)->spindown_offset);
			err |= __get_user(f->select_delay, &((struct floppy_drive_params32 *)arg)->select_delay);
			err |= __get_user(f->rps, &((struct floppy_drive_params32 *)arg)->rps);
			err |= __get_user(f->tracks, &((struct floppy_drive_params32 *)arg)->tracks);
			err |= __get_user(f->timeout, &((struct floppy_drive_params32 *)arg)->timeout);
			err |= __get_user(f->interleave_sect, &((struct floppy_drive_params32 *)arg)->interleave_sect);
			err |= __copy_from_user(&f->max_errors, &((struct floppy_drive_params32 *)arg)->max_errors, sizeof(f->max_errors));
			err |= __get_user(f->flags, &((struct floppy_drive_params32 *)arg)->flags);
			err |= __get_user(f->read_track, &((struct floppy_drive_params32 *)arg)->read_track);
			err |= __copy_from_user(f->autodetect, ((struct floppy_drive_params32 *)arg)->autodetect, sizeof(f->autodetect));
			err |= __get_user(f->checkfreq, &((struct floppy_drive_params32 *)arg)->checkfreq);
			err |= __get_user(f->native_format, &((struct floppy_drive_params32 *)arg)->native_format);
			if (err) {
				err = -EFAULT;
				goto out;
			}
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
			karg = kmalloc(sizeof(struct floppy_drive_struct), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		case FDGETFDCSTAT32:
			karg = kmalloc(sizeof(struct floppy_fdc_state), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		case FDWERRORGET32:
			karg = kmalloc(sizeof(struct floppy_write_errors), GFP_KERNEL);
			if (!karg)
				return -ENOMEM;
			break;
		default:
			return -EINVAL;
	}
	set_fs (KERNEL_DS);
	err = sys_ioctl (fd, kcmd, (unsigned long)karg);
	set_fs (old_fs);
	if (err)
		goto out;
	switch (cmd) {
		case FDGETPRM32:
		{
			struct floppy_struct *f = karg;

			err = __put_user(f->size, &((struct floppy_struct32 *)arg)->size);
			err |= __put_user(f->sect, &((struct floppy_struct32 *)arg)->sect);
			err |= __put_user(f->head, &((struct floppy_struct32 *)arg)->head);
			err |= __put_user(f->track, &((struct floppy_struct32 *)arg)->track);
			err |= __put_user(f->stretch, &((struct floppy_struct32 *)arg)->stretch);
			err |= __put_user(f->gap, &((struct floppy_struct32 *)arg)->gap);
			err |= __put_user(f->rate, &((struct floppy_struct32 *)arg)->rate);
			err |= __put_user(f->spec1, &((struct floppy_struct32 *)arg)->spec1);
			err |= __put_user(f->fmt_gap, &((struct floppy_struct32 *)arg)->fmt_gap);
			err |= __put_user((u64)f->name, &((struct floppy_struct32 *)arg)->name);
			break;
		}
		case FDGETDRVPRM32:
		{
			struct floppy_drive_params *f = karg;

			err = __put_user(f->cmos, &((struct floppy_drive_params32 *)arg)->cmos);
			err |= __put_user(f->max_dtr, &((struct floppy_drive_params32 *)arg)->max_dtr);
			err |= __put_user(f->hlt, &((struct floppy_drive_params32 *)arg)->hlt);
			err |= __put_user(f->hut, &((struct floppy_drive_params32 *)arg)->hut);
			err |= __put_user(f->srt, &((struct floppy_drive_params32 *)arg)->srt);
			err |= __put_user(f->spinup, &((struct floppy_drive_params32 *)arg)->spinup);
			err |= __put_user(f->spindown, &((struct floppy_drive_params32 *)arg)->spindown);
			err |= __put_user(f->spindown_offset, &((struct floppy_drive_params32 *)arg)->spindown_offset);
			err |= __put_user(f->select_delay, &((struct floppy_drive_params32 *)arg)->select_delay);
			err |= __put_user(f->rps, &((struct floppy_drive_params32 *)arg)->rps);
			err |= __put_user(f->tracks, &((struct floppy_drive_params32 *)arg)->tracks);
			err |= __put_user(f->timeout, &((struct floppy_drive_params32 *)arg)->timeout);
			err |= __put_user(f->interleave_sect, &((struct floppy_drive_params32 *)arg)->interleave_sect);
			err |= __copy_to_user(&((struct floppy_drive_params32 *)arg)->max_errors, &f->max_errors, sizeof(f->max_errors));
			err |= __put_user(f->flags, &((struct floppy_drive_params32 *)arg)->flags);
			err |= __put_user(f->read_track, &((struct floppy_drive_params32 *)arg)->read_track);
			err |= __copy_to_user(((struct floppy_drive_params32 *)arg)->autodetect, f->autodetect, sizeof(f->autodetect));
			err |= __put_user(f->checkfreq, &((struct floppy_drive_params32 *)arg)->checkfreq);
			err |= __put_user(f->native_format, &((struct floppy_drive_params32 *)arg)->native_format);
			break;
		}
		case FDGETDRVSTAT32:
		case FDPOLLDRVSTAT32:
		{
			struct floppy_drive_struct *f = karg;

			err = __put_user(f->flags, &((struct floppy_drive_struct32 *)arg)->flags);
			err |= __put_user(f->spinup_date, &((struct floppy_drive_struct32 *)arg)->spinup_date);
			err |= __put_user(f->select_date, &((struct floppy_drive_struct32 *)arg)->select_date);
			err |= __put_user(f->first_read_date, &((struct floppy_drive_struct32 *)arg)->first_read_date);
			err |= __put_user(f->probed_format, &((struct floppy_drive_struct32 *)arg)->probed_format);
			err |= __put_user(f->track, &((struct floppy_drive_struct32 *)arg)->track);
			err |= __put_user(f->maxblock, &((struct floppy_drive_struct32 *)arg)->maxblock);
			err |= __put_user(f->maxtrack, &((struct floppy_drive_struct32 *)arg)->maxtrack);
			err |= __put_user(f->generation, &((struct floppy_drive_struct32 *)arg)->generation);
			err |= __put_user(f->keep_data, &((struct floppy_drive_struct32 *)arg)->keep_data);
			err |= __put_user(f->fd_ref, &((struct floppy_drive_struct32 *)arg)->fd_ref);
			err |= __put_user(f->fd_device, &((struct floppy_drive_struct32 *)arg)->fd_device);
			err |= __put_user(f->last_checked, &((struct floppy_drive_struct32 *)arg)->last_checked);
			err |= __put_user((u64)f->dmabuf, &((struct floppy_drive_struct32 *)arg)->dmabuf);
			err |= __put_user((u64)f->bufblocks, &((struct floppy_drive_struct32 *)arg)->bufblocks);
			break;
		}
		case FDGETFDCSTAT32:
		{
			struct floppy_fdc_state *f = karg;

			err = __put_user(f->spec1, &((struct floppy_fdc_state32 *)arg)->spec1);
			err |= __put_user(f->spec2, &((struct floppy_fdc_state32 *)arg)->spec2);
			err |= __put_user(f->dtr, &((struct floppy_fdc_state32 *)arg)->dtr);
			err |= __put_user(f->version, &((struct floppy_fdc_state32 *)arg)->version);
			err |= __put_user(f->dor, &((struct floppy_fdc_state32 *)arg)->dor);
			err |= __put_user(f->address, &((struct floppy_fdc_state32 *)arg)->address);
			err |= __copy_to_user((char *)&((struct floppy_fdc_state32 *)arg)->address
			    		   + sizeof(((struct floppy_fdc_state32 *)arg)->address),
					   (char *)&f->address + sizeof(f->address), sizeof(int));
			err |= __put_user(f->driver_version, &((struct floppy_fdc_state32 *)arg)->driver_version);
			err |= __copy_to_user(((struct floppy_fdc_state32 *)arg)->track, f->track, sizeof(f->track));
			break;
		}
		case FDWERRORGET32:
		{
			struct floppy_write_errors *f = karg;

			err = __put_user(f->write_errors, &((struct floppy_write_errors32 *)arg)->write_errors);
			err |= __put_user(f->first_error_sector, &((struct floppy_write_errors32 *)arg)->first_error_sector);
			err |= __put_user(f->first_error_generation, &((struct floppy_write_errors32 *)arg)->first_error_generation);
			err |= __put_user(f->last_error_sector, &((struct floppy_write_errors32 *)arg)->last_error_sector);
			err |= __put_user(f->last_error_generation, &((struct floppy_write_errors32 *)arg)->last_error_generation);
			err |= __put_user(f->badness, &((struct floppy_write_errors32 *)arg)->badness);
			break;
		}
		default:
			break;
	}
	if (err)
		err = -EFAULT;

out:	if (karg) kfree(karg);
	return err;
}

struct mtd_oob_buf32 {
	u_int32_t start;
	u_int32_t length;
	compat_caddr_t ptr;	/* unsigned char* */
};

#define MEMWRITEOOB32 	_IOWR('M',3,struct mtd_oob_buf32)
#define MEMREADOOB32 	_IOWR('M',4,struct mtd_oob_buf32)

static int mtd_rw_oob(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct mtd_oob_buf	*buf = compat_alloc_user_space(sizeof(*buf));
	struct mtd_oob_buf32	*buf32 = (struct mtd_oob_buf32 *) arg;
	u32 data;
	char *datap;
	unsigned int real_cmd;
	int err;

	real_cmd = (cmd == MEMREADOOB32) ?
		MEMREADOOB : MEMWRITEOOB;

	if (copy_in_user(&buf->start, &buf32->start,
			 2 * sizeof(u32)) ||
	    get_user(data, &buf32->ptr))
		return -EFAULT;
	datap = (void *) (unsigned long) data;
	if (put_user(datap, &buf->ptr))
		return -EFAULT;

	err = sys_ioctl(fd, real_cmd, (unsigned long) buf);

	if (!err) {
		if (copy_in_user(&buf32->start, &buf->start,
				 2 * sizeof(u32)))
			err = -EFAULT;
	}

	return err;
}	

#undef CODE
#endif

#ifdef DECLARES
HANDLE_IOCTL(MEMREADOOB32, mtd_rw_oob)
HANDLE_IOCTL(MEMWRITEOOB32, mtd_rw_oob)
#ifdef CONFIG_NET
HANDLE_IOCTL(SIOCGIFNAME, dev_ifname32)
#endif
HANDLE_IOCTL(SIOCGIFCONF, dev_ifconf)
HANDLE_IOCTL(SIOCGIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMETRIC, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMTU, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMEM, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFHWADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCADDMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCDELMULTI, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFINDEX, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFMAP, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFBRDADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFDSTADDR, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFNETMASK, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFPFLAGS, dev_ifsioc)
HANDLE_IOCTL(SIOCGIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCSIFTXQLEN, dev_ifsioc)
HANDLE_IOCTL(SIOCETHTOOL, ethtool_ioctl)
HANDLE_IOCTL(SIOCBONDENSLAVE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDRELEASE, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSETHWADDR, bond_ioctl)
HANDLE_IOCTL(SIOCBONDSLAVEINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDINFOQUERY, bond_ioctl)
HANDLE_IOCTL(SIOCBONDCHANGEACTIVE, bond_ioctl)
HANDLE_IOCTL(SIOCADDRT, routing_ioctl)
HANDLE_IOCTL(SIOCDELRT, routing_ioctl)
/* Note SIOCRTMSG is no longer, so this is safe and * the user would have seen just an -EINVAL anyways. */
HANDLE_IOCTL(SIOCRTMSG, ret_einval)
HANDLE_IOCTL(SIOCGSTAMP, do_siocgstamp)
HANDLE_IOCTL(HDIO_GETGEO, hdio_getgeo)
HANDLE_IOCTL(BLKRAGET, w_long)
HANDLE_IOCTL(BLKGETSIZE, w_long)
HANDLE_IOCTL(0x1260, broken_blkgetsize)
HANDLE_IOCTL(BLKFRAGET, w_long)
HANDLE_IOCTL(BLKSECTGET, w_long)
HANDLE_IOCTL(FBIOGET_FSCREENINFO, fb_ioctl_trans)
HANDLE_IOCTL(BLKPG, blkpg_ioctl_trans)
HANDLE_IOCTL(FBIOGETCMAP, fb_ioctl_trans)
HANDLE_IOCTL(FBIOPUTCMAP, fb_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_KEEPSETTINGS, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_UNMASKINTR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_DMA, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_32BIT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_MULTCOUNT, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NOWERR, hdio_ioctl_trans)
HANDLE_IOCTL(HDIO_GET_NICE, hdio_ioctl_trans)
HANDLE_IOCTL(FDSETPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDDEFPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDSETDRVPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETDRVPRM32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETDRVSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDPOLLDRVSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDGETFDCSTAT32, fd_ioctl_trans)
HANDLE_IOCTL(FDWERRORGET32, fd_ioctl_trans)
HANDLE_IOCTL(SG_IO,sg_ioctl_trans)
HANDLE_IOCTL(PPPIOCGIDLE32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSCOMPRESS32, ppp_ioctl_trans)
HANDLE_IOCTL(PPPIOCSPASS32, ppp_sock_fprog_ioctl_trans)
HANDLE_IOCTL(PPPIOCSACTIVE32, ppp_sock_fprog_ioctl_trans)
HANDLE_IOCTL(MTIOCGET32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCPOS32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCGETCONFIG32, mt_ioctl_trans)
HANDLE_IOCTL(MTIOCSETCONFIG32, mt_ioctl_trans)
HANDLE_IOCTL(CDROMREADAUDIO, cdrom_ioctl_trans)
HANDLE_IOCTL(CDROM_SEND_PACKET, cdrom_ioctl_trans)
HANDLE_IOCTL(LOOP_SET_STATUS, loop_status)
HANDLE_IOCTL(LOOP_GET_STATUS, loop_status)
#define AUTOFS_IOC_SETTIMEOUT32 _IOWR(0x93,0x64,unsigned int)
HANDLE_IOCTL(AUTOFS_IOC_SETTIMEOUT32, ioc_settimeout)
#ifdef CONFIG_VT
HANDLE_IOCTL(PIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(GIO_FONTX, do_fontx_ioctl)
HANDLE_IOCTL(PIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(GIO_UNIMAP, do_unimap_ioctl)
HANDLE_IOCTL(KDFONTOP, do_kdfontop_ioctl)
#endif
HANDLE_IOCTL(EXT2_IOC32_GETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETFLAGS, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_GETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(EXT2_IOC32_SETVERSION, do_ext2_ioctl)
HANDLE_IOCTL(VIDIOCGTUNER32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSTUNER32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCGWIN32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSWIN32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCGFBUF32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSFBUF32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCGFREQ32, do_video_ioctl)
HANDLE_IOCTL(VIDIOCSFREQ32, do_video_ioctl)
/* One SMB ioctl needs translations. */
#define SMB_IOC_GETMOUNTUID_32 _IOR('u', 1, compat_pid_t)
HANDLE_IOCTL(SMB_IOC_GETMOUNTUID_32, do_smb_getmountuid)
HANDLE_IOCTL(ATM_GETLINKRATE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETNAMES32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETTYPE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETESI32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_RSTADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_ADDADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_DELADDR32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETCIRANGE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETCIRANGE32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETESI32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETESIF32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETSTAT32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETSTATZ32, do_atm_ioctl)
HANDLE_IOCTL(ATM_GETLOOP32, do_atm_ioctl)
HANDLE_IOCTL(ATM_SETLOOP32, do_atm_ioctl)
HANDLE_IOCTL(ATM_QUERYLOOP32, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETSTAT, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETSTATZ, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_SETDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_CLRDIAG, do_atm_ioctl)
HANDLE_IOCTL(SONET_SETFRAMING, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETFRAMING, do_atm_ioctl)
HANDLE_IOCTL(SONET_GETFRSENSE, do_atm_ioctl)
/* block stuff */
HANDLE_IOCTL(BLKBSZGET_32, do_blkbszget)
HANDLE_IOCTL(BLKBSZSET_32, do_blkbszset)
HANDLE_IOCTL(BLKGETSIZE64_32, do_blkgetsize64)

#undef DECLARES
#endif
