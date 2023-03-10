/*
 *  platinumfb.c -- frame buffer device for the PowerMac 'platinum' display
 *
 *  Copyright (C) 1998 Franz Sirl
 *
 *  Frame buffer structure from:
 *    drivers/video/controlfb.c -- frame buffer device for
 *    Apple 'control' display chip.
 *    Copyright (C) 1998 Dan Jacobowitz
 *
 *  Hardware information from:
 *    platinum.c: Console support for PowerMac "platinum" display adaptor.
 *    Copyright (C) 1996 Paul Mackerras and Mark Abene
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pgtable.h>

#include "macmodes.h"
#include "platinumfb.h"

static int default_vmode = VMODE_NVRAM;
static int default_cmode = CMODE_NVRAM;

struct fb_par_platinum {
	int	vmode, cmode;
	int	xres, yres;
	int	vxres, vyres;
	int	xoffset, yoffset;
};

struct fb_info_platinum {
	struct fb_info			info;
	struct fb_par_platinum		par;

	struct {
		__u8 red, green, blue;
	}				palette[256];
	u32				pseudo_palette[17];
	
	volatile struct cmap_regs	*cmap_regs;
	unsigned long			cmap_regs_phys;
	
	volatile struct platinum_regs	*platinum_regs;
	unsigned long			platinum_regs_phys;
	
	__u8				*frame_buffer;
	volatile __u8			*base_frame_buffer;
	unsigned long			frame_buffer_phys;
	
	unsigned long			total_vram;
	int				clktype;
	int				dactype;
};

/*
 * Frame buffer device API
 */

static int platinumfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
	u_int transp, struct fb_info *info);
static int platinumfb_blank(int blank_mode, struct fb_info *info);
static int platinumfb_set_par (struct fb_info *info);
static int platinumfb_check_var (struct fb_var_screeninfo *var, struct fb_info *info);

/*
 * internal functions
 */

static void platinum_of_init(struct device_node *dp);
static inline int platinum_vram_reqd(int video_mode, int color_mode);
static int read_platinum_sense(struct fb_info_platinum *info);
static void set_platinum_clock(struct fb_info_platinum *info);
static void platinum_set_hardware(struct fb_info_platinum *info,
				  const struct fb_par_platinum *par);
static int platinum_par_to_var(struct fb_var_screeninfo *var,
			       const struct fb_par_platinum *par,
			       const struct fb_info_platinum *info);
static int platinum_var_to_par(const struct fb_var_screeninfo *var,
			       struct fb_par_platinum *par,
			       const struct fb_info_platinum *info);

/*
 * Interface used by the world
 */

int platinum_init(void);
int platinum_setup(char*);

static struct fb_ops platinumfb_ops = {
	.owner =	THIS_MODULE,
	.fb_check_var	= platinumfb_check_var,
	.fb_set_par	= platinumfb_set_par,
	.fb_setcolreg	= platinumfb_setcolreg,
	.fb_blank	= platinumfb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
	.fb_cursor	= soft_cursor,
};

/*
 * Checks a var structure
 */
static int platinumfb_check_var (struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;
	struct fb_par_platinum par;
	int err;
	
	err = platinum_var_to_par(var, &par, p);
	if (err)
		return err;	
	platinum_par_to_var(var, &par, p);

	return 0;
}

/*
 * Applies current var to display
 */
static int platinumfb_set_par (struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;
	struct platinum_regvals *init;
	struct fb_par_platinum par;
	int err;

	if((err = platinum_var_to_par(&info->var, &par, p))) {
		printk (KERN_ERR "platinumfb_set_par: error calling"
				 " platinum_var_to_par: %d.\n", err);
		return err;
	}

	platinum_set_hardware(p, &par);

	init = platinum_reg_init[p->par.vmode-1];
	
	info->screen_base = (char *) p->frame_buffer + init->fb_offset + 0x20;
	info->fix.smem_start = (p->frame_buffer_phys) + init->fb_offset + 0x20;
	info->fix.visual = (p->par.cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	info->fix.line_length = vmode_attrs[p->par.vmode-1].hres * (1<<p->par.cmode) + 0x20;

	return 0;
}

static int platinumfb_blank(int blank,  struct fb_info *fb)
{
/*
 *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
 *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
 *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
 *  to e.g. a video mode which doesn't support it. Implements VESA suspend
 *  and powerdown modes on hardware that supports disabling hsync/vsync:
 *    blank_mode == 2: suspend vsync
 *    blank_mode == 3: suspend hsync
 *    blank_mode == 4: powerdown
 */
/* [danj] I think there's something fishy about those constants... */
/*
	struct fb_info_platinum *info = (struct fb_info_platinum *) fb;
	int	ctrl;

	ctrl = ld_le32(&info->platinum_regs->ctrl.r) | 0x33;
	if (blank)
		--blank_mode;
	if (blank & VESA_VSYNC_SUSPEND)
		ctrl &= ~3;
	if (blank & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x30;
	out_le32(&info->platinum_regs->ctrl.r, ctrl);
*/
/* TODO: Figure out how the heck to powerdown this thing! */
	return 0;
}

static int platinumfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	struct fb_info_platinum *p = (struct fb_info_platinum *) info;
	volatile struct cmap_regs *cmap_regs = p->cmap_regs;

	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

	p->palette[regno].red = red;
	p->palette[regno].green = green;
	p->palette[regno].blue = blue;

	out_8(&cmap_regs->addr, regno);		/* tell clut what addr to fill	*/
	out_8(&cmap_regs->lut, red);		/* send one color channel at	*/
	out_8(&cmap_regs->lut, green);		/* a time...			*/
	out_8(&cmap_regs->lut, blue);

	if (regno < 16) {
		int i;
		u32 *pal = info->pseudo_palette;
		switch (p->par.cmode) {
		case CMODE_16:
			pal[regno] = (regno << 10) | (regno << 5) | regno;
			break;
		case CMODE_32:
			i = (regno << 8) | regno;
			pal[regno] = (i << 16) | i;
			break;
		}
	}
	
	return 0;
}

static inline int platinum_vram_reqd(int video_mode, int color_mode)
{
	return vmode_attrs[video_mode-1].vres *
	       (vmode_attrs[video_mode-1].hres * (1<<color_mode) + 0x20) +0x1000;
}

#define STORE_D2(a, d) { \
	out_8(&cmap_regs->addr, (a+32)); \
	out_8(&cmap_regs->d2, (d)); \
}

static void set_platinum_clock(struct fb_info_platinum *info)
{
	volatile struct cmap_regs *cmap_regs = info->cmap_regs;
	struct platinum_regvals	*init;

	init = platinum_reg_init[info->par.vmode-1];

	STORE_D2(6, 0xc6);
	out_8(&cmap_regs->addr,3+32);

	if (in_8(&cmap_regs->d2) == 2) {
		STORE_D2(7, init->clock_params[info->clktype][0]);
		STORE_D2(8, init->clock_params[info->clktype][1]);
		STORE_D2(3, 3);
	} else {
		STORE_D2(4, init->clock_params[info->clktype][0]);
		STORE_D2(5, init->clock_params[info->clktype][1]);
		STORE_D2(3, 2);
	}

	__delay(5000);
	STORE_D2(9, 0xa6);
}


/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void platinum_set_hardware(struct fb_info_platinum *info, const struct fb_par_platinum *par)
{
	volatile struct platinum_regs	*platinum_regs = info->platinum_regs;
	volatile struct cmap_regs	*cmap_regs = info->cmap_regs;
	struct platinum_regvals		*init;
	int				i;
	int				vmode, cmode;
	
	info->par = *par;

	vmode = par->vmode;
	cmode = par->cmode;

	init = platinum_reg_init[vmode - 1];

	/* Initialize display timing registers */
	out_be32(&platinum_regs->reg[24].r, 7);	/* turn display off */

	for (i = 0; i < 26; ++i)
		out_be32(&platinum_regs->reg[i+32].r, init->regs[i]);

	out_be32(&platinum_regs->reg[26+32].r, (info->total_vram == 0x100000 ?
						init->offset[cmode] + 4 - cmode :
						init->offset[cmode]));
	out_be32(&platinum_regs->reg[16].r, (unsigned) info->frame_buffer_phys+init->fb_offset+0x10);
	out_be32(&platinum_regs->reg[18].r, init->pitch[cmode]);
	out_be32(&platinum_regs->reg[19].r, (info->total_vram == 0x100000 ?
					     init->mode[cmode+1] :
					     init->mode[cmode]));
	out_be32(&platinum_regs->reg[20].r, (info->total_vram == 0x100000 ? 0x11 : 0x1011));
	out_be32(&platinum_regs->reg[21].r, 0x100);
	out_be32(&platinum_regs->reg[22].r, 1);
	out_be32(&platinum_regs->reg[23].r, 1);
	out_be32(&platinum_regs->reg[26].r, 0xc00);
	out_be32(&platinum_regs->reg[27].r, 0x235);
	/* out_be32(&platinum_regs->reg[27].r, 0x2aa); */

	STORE_D2(0, (info->total_vram == 0x100000 ?
		     init->dacula_ctrl[cmode] & 0xf :
		     init->dacula_ctrl[cmode]));
	STORE_D2(1, 4);
	STORE_D2(2, 0);

	set_platinum_clock(info);

	out_be32(&platinum_regs->reg[24].r, 0);	/* turn display on */
}

/*
 * Set misc info vars for this driver
 */
static void __init platinum_init_info(struct fb_info *info, struct fb_info_platinum *p)
{
	/* Fill fb_info */
	info->par = &p->par;
	info->fbops = &platinumfb_ops;
	info->pseudo_palette = p->pseudo_palette;
        info->flags = FBINFO_FLAG_DEFAULT;
	info->screen_base = (char *) p->frame_buffer + 0x20;

	fb_alloc_cmap(&info->cmap, 256, 0);

	/* Fill fix common fields */
	strcpy(info->fix.id, "platinum");
	info->fix.mmio_start = p->platinum_regs_phys;
	info->fix.mmio_len = 0x1000;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.smem_start = p->frame_buffer_phys + 0x20; /* will be updated later */
	info->fix.smem_len = p->total_vram - 0x20;
        info->fix.ywrapstep = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
        info->fix.type_aux = 0;
        info->fix.accel = FB_ACCEL_NONE;
}


static int __init init_platinum(struct fb_info_platinum *p)
{
	struct fb_var_screeninfo var;
	int sense, rc;

	sense = read_platinum_sense(p);
	printk(KERN_INFO "Monitor sense value = 0x%x, ", sense);

	if (default_vmode == VMODE_NVRAM) {
		default_vmode = nvram_read_byte(NV_VMODE);
		if (default_vmode <= 0 || default_vmode > VMODE_MAX ||
		    !platinum_reg_init[default_vmode-1])
			default_vmode = VMODE_CHOOSE;
	}
	if (default_vmode == VMODE_CHOOSE) {
		default_vmode = mac_map_monitor_sense(sense);
	}
	if (default_vmode <= 0 || default_vmode > VMODE_MAX)
		default_vmode = VMODE_640_480_60;
	if (default_cmode == CMODE_NVRAM)
		default_cmode = nvram_read_byte(NV_CMODE);
	if (default_cmode < CMODE_8 || default_cmode > CMODE_32)
		default_cmode = CMODE_8;
	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	while(default_cmode > CMODE_8 && platinum_vram_reqd(default_vmode, default_cmode) > p->total_vram)
		default_cmode--;

	printk("using video mode %d and color mode %d.\n", default_vmode, default_cmode);

	/* Setup default var */
	if (mac_vmode_to_var(default_vmode, default_cmode, &var) < 0) {
		/* This shouldn't happen! */
		printk("mac_vmode_to_var(%d, %d,) failed\n", default_vmode, default_cmode);
try_again:
		default_vmode = VMODE_640_480_60;
		default_cmode = CMODE_8;
		if (mac_vmode_to_var(default_vmode, default_cmode, &var) < 0) {
			printk(KERN_ERR "platinumfb: mac_vmode_to_var() failed\n");
			return -ENXIO;
		}
	}

	/* Initialize info structure */
	platinum_init_info(&p->info, p);

	/* Apply default var */
	p->info.var = var;
	var.activate = FB_ACTIVATE_NOW;
	rc = fb_set_var(&var, &p->info);
	if (rc && (default_vmode != VMODE_640_480_60 || default_cmode != CMODE_8))
		goto try_again;

	/* Register with fbdev layer */
	if (register_framebuffer(&p->info) < 0)
		return 0;

	printk(KERN_INFO "fb%d: platinum frame buffer device\n",
	       p->info.node);

	return 1;
}

int __init platinum_init(void)
{
	struct device_node *dp;

	dp = find_devices("platinum");
	if (dp != 0)
		platinum_of_init(dp);
	return 0;
}

#ifdef __powerpc__
#define invalidate_cache(addr) \
	asm volatile("eieio; dcbf 0,%1" \
	: "=m" (*(addr)) : "r" (addr) : "memory");
#else
#define invalidate_cache(addr)
#endif

static void __init platinum_of_init(struct device_node *dp)
{
	struct fb_info_platinum	*info;
	unsigned long		addr, size;
	volatile __u8		*fbuffer;
	int			i, bank0, bank1, bank2, bank3;

	if(dp->n_addrs != 2) {
		printk(KERN_ERR "expecting 2 address for platinum (got %d)", dp->n_addrs);
		return;
	}

	info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (info == 0)
		return;
	memset(info, 0, sizeof(*info));

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		/* Let's assume we can request either all or nothing */
		if (!request_mem_region(addr, size, "platinumfb")) {
			kfree(info);
			return;
		}
		if (size >= 0x400000) {
			/* frame buffer - map only 4MB */
			info->frame_buffer_phys = addr;
			info->frame_buffer = __ioremap(addr, 0x400000, _PAGE_WRITETHRU);
			info->base_frame_buffer = info->frame_buffer;
		} else {
			/* registers */
			info->platinum_regs_phys = addr;
			info->platinum_regs = ioremap(addr, size);
		}
	}

	info->cmap_regs_phys = 0xf301b000;	/* XXX not in prom? */
	request_mem_region(info->cmap_regs_phys, 0x1000, "platinumfb cmap");
	info->cmap_regs = ioremap(info->cmap_regs_phys, 0x1000);

	/* Grok total video ram */
	out_be32(&info->platinum_regs->reg[16].r, (unsigned)info->frame_buffer_phys);
	out_be32(&info->platinum_regs->reg[20].r, 0x1011);	/* select max vram */
	out_be32(&info->platinum_regs->reg[24].r, 0);	/* switch in vram */

	fbuffer = info->base_frame_buffer;
	fbuffer[0x100000] = 0x34;
	fbuffer[0x100008] = 0x0;
	invalidate_cache(&fbuffer[0x100000]);
	fbuffer[0x200000] = 0x56;
	fbuffer[0x200008] = 0x0;
	invalidate_cache(&fbuffer[0x200000]);
	fbuffer[0x300000] = 0x78;
	fbuffer[0x300008] = 0x0;
	invalidate_cache(&fbuffer[0x300000]);
	bank0 = 1; /* builtin 1MB vram, always there */
	bank1 = fbuffer[0x100000] == 0x34;
	bank2 = fbuffer[0x200000] == 0x56;
	bank3 = fbuffer[0x300000] == 0x78;
	info->total_vram = (bank0 + bank1 + bank2 + bank3) * 0x100000;
	printk(KERN_INFO "Total VRAM = %dMB %d%d%d%d\n", (int) (info->total_vram / 1024 / 1024), bank3, bank2, bank1, bank0);

	/*
	 * Try to determine whether we have an old or a new DACula.
	 */
	out_8(&info->cmap_regs->addr, 0x40);
	info->dactype = in_8(&info->cmap_regs->d2);
	switch (info->dactype) {
	case 0x3c:
		info->clktype = 1;
		break;
	case 0x84:
		info->clktype = 0;
		break;
	default:
		info->clktype = 0;
		printk(KERN_INFO "Unknown DACula type: %x\n", info->dactype);
		break;
	}

	if (!init_platinum(info)) {
		kfree(info);
		return;
	}
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int read_platinum_sense(struct fb_info_platinum *info)
{
	volatile struct platinum_regs *platinum_regs = info->platinum_regs;
	int sense;

	out_be32(&platinum_regs->reg[23].r, 7);	/* turn off drivers */
	__delay(2000);
	sense = (~in_be32(&platinum_regs->reg[23].r) & 7) << 8;

	/* drive each sense line low in turn and collect the other 2 */
	out_be32(&platinum_regs->reg[23].r, 3);	/* drive A low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 3) << 4;
	out_be32(&platinum_regs->reg[23].r, 5);	/* drive B low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 4) << 1;
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 1) << 2;
	out_be32(&platinum_regs->reg[23].r, 6);	/* drive C low */
	__delay(2000);
	sense |= (~in_be32(&platinum_regs->reg[23].r) & 6) >> 1;

	out_be32(&platinum_regs->reg[23].r, 7);	/* turn off drivers */

	return sense;
}

/* This routine takes a user-supplied var, and picks the best vmode/cmode from it. */
static int platinum_var_to_par(const struct fb_var_screeninfo *var, 
			       struct fb_par_platinum *par,
			       const struct fb_info_platinum *info)
{
	if(mac_var_to_vmode(var, &par->vmode, &par->cmode) != 0) {
		printk(KERN_ERR "platinum_var_to_par: mac_var_to_vmode unsuccessful.\n");
		printk(KERN_ERR "platinum_var_to_par: var->xres = %d\n", var->xres);
		printk(KERN_ERR "platinum_var_to_par: var->yres = %d\n", var->yres);
		printk(KERN_ERR "platinum_var_to_par: var->xres_virtual = %d\n", var->xres_virtual);
		printk(KERN_ERR "platinum_var_to_par: var->yres_virtual = %d\n", var->yres_virtual);
		printk(KERN_ERR "platinum_var_to_par: var->bits_per_pixel = %d\n", var->bits_per_pixel);
		printk(KERN_ERR "platinum_var_to_par: var->pixclock = %d\n", var->pixclock);
		printk(KERN_ERR "platinum_var_to_par: var->vmode = %d\n", var->vmode);
		return -EINVAL;
	}

	if(!platinum_reg_init[par->vmode-1]) {
		printk(KERN_ERR "platinum_var_to_par, vmode %d not valid.\n", par->vmode);
		return -EINVAL;
	}

	if (platinum_vram_reqd(par->vmode, par->cmode) > info->total_vram) {
		printk(KERN_ERR "platinum_var_to_par, not enough ram for vmode %d, cmode %d.\n", par->vmode, par->cmode);
		return -EINVAL;
	}

	par->xres = vmode_attrs[par->vmode-1].hres;
	par->yres = vmode_attrs[par->vmode-1].vres;
	par->xoffset = 0;
	par->yoffset = 0;
	par->vxres = par->xres;
	par->vyres = par->yres;
	
	return 0;
}

static int platinum_par_to_var(struct fb_var_screeninfo *var,
			       const struct fb_par_platinum *par,
			       const struct fb_info_platinum *info)
{
	return mac_vmode_to_var(par->vmode, par->cmode, var);
}

/* 
 * Parse user speficied options (`video=platinumfb:')
 */
int __init platinum_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "vmode:", 6)) {
	    		int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX)
				default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			int depth = simple_strtoul(this_opt+6, NULL, 0);
			switch (depth) {
			 case 0:
			 case 8:
			    default_cmode = CMODE_8;
			    break;
			 case 15:
			 case 16:
			    default_cmode = CMODE_16;
			    break;
			 case 24:
			 case 32:
			    default_cmode = CMODE_32;
			    break;
			}
		}
	}
	return 0;
}

MODULE_LICENSE("GPL");
