/*
 * linux/drivers/video/neofb.c -- NeoMagic Framebuffer Driver
 *
 * Copyright (c) 2001-2002  Denis Oliver Kropp <dok@directfb.org>
 *
 *
 * Card specific code is based on XFree86's neomagic driver.
 * Framebuffer framework code is based on code of cyber2000fb.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 *
 * 0.4.1
 *  - Cosmetic changes (dok)
 *
 * 0.4
 *  - Toshiba Libretto support, allow modes larger than LCD size if
 *    LCD is disabled, keep BIOS settings if internal/external display
 *    haven't been enabled explicitly
 *                          (Thomas J. Moore <dark@mama.indstate.edu>)
 *
 * 0.3.3
 *  - Porting over to new fbdev api. (jsimmons)
 *  
 * 0.3.2
 *  - got rid of all floating point (dok) 
 *
 * 0.3.1
 *  - added module license (dok)
 *
 * 0.3
 *  - hardware accelerated clear and move for 2200 and above (dok)
 *  - maximum allowed dotclock is handled now (dok)
 *
 * 0.2.1
 *  - correct panning after X usage (dok)
 *  - added module and kernel parameters (dok)
 *  - no stretching if external display is enabled (dok)
 *
 * 0.2
 *  - initial version (dok)
 *
 *
 * TODO
 * - ioctl for internal/external switching
 * - blanking
 * - 32bit depth support, maybe impossible
 * - disable pan-on-sync, need specs
 *
 * BUGS
 * - white margin on bootup like with tdfxfb (colormap problem?)
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/init.h>
#ifdef CONFIG_TOSHIBA
#include <linux/toshiba.h>
extern int tosh_smm(SMMRegisters *regs);
#endif

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include <video/neomagic.h>

#define NEOFB_VERSION "0.4.1"

/* --------------------------------------------------------------------- */

static int disabled;
static int internal;
static int external;
static int libretto;
static int nostretch;
static int nopciburst;


#ifdef MODULE

MODULE_AUTHOR("(c) 2001-2002  Denis Oliver Kropp <dok@convergence.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("FBDev driver for NeoMagic PCI Chips");
MODULE_PARM(disabled, "i");
MODULE_PARM_DESC(disabled, "Disable this driver's initialization.");
MODULE_PARM(internal, "i");
MODULE_PARM_DESC(internal, "Enable output on internal LCD Display.");
MODULE_PARM(external, "i");
MODULE_PARM_DESC(external, "Enable output on external CRT.");
MODULE_PARM(libretto, "i");
MODULE_PARM_DESC(libretto, "Force Libretto 100/110 800x480 LCD.");
MODULE_PARM(nostretch, "i");
MODULE_PARM_DESC(nostretch,
		 "Disable stretching of modes smaller than LCD.");
MODULE_PARM(nopciburst, "i");
MODULE_PARM_DESC(nopciburst, "Disable PCI burst mode.");

#endif


/* --------------------------------------------------------------------- */

static biosMode bios8[] = {
	{320, 240, 0x40},
	{300, 400, 0x42},
	{640, 400, 0x20},
	{640, 480, 0x21},
	{800, 600, 0x23},
	{1024, 768, 0x25},
};

static biosMode bios16[] = {
	{320, 200, 0x2e},
	{320, 240, 0x41},
	{300, 400, 0x43},
	{640, 480, 0x31},
	{800, 600, 0x34},
	{1024, 768, 0x37},
};

static biosMode bios24[] = {
	{640, 480, 0x32},
	{800, 600, 0x35},
	{1024, 768, 0x38}
};

#ifdef NO_32BIT_SUPPORT_YET
/* FIXME: guessed values, wrong */
static biosMode bios32[] = {
	{640, 480, 0x33},
	{800, 600, 0x36},
	{1024, 768, 0x39}
};
#endif

static int neoFindMode(int xres, int yres, int depth)
{
	int xres_s;
	int i, size;
	biosMode *mode;

	switch (depth) {
	case 8:
		size = sizeof(bios8) / sizeof(biosMode);
		mode = bios8;
		break;
	case 16:
		size = sizeof(bios16) / sizeof(biosMode);
		mode = bios16;
		break;
	case 24:
		size = sizeof(bios24) / sizeof(biosMode);
		mode = bios24;
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
		size = sizeof(bios32) / sizeof(biosMode);
		mode = bios32;
		break;
#endif
	default:
		return 0;
	}

	for (i = 0; i < size; i++) {
		if (xres <= mode[i].x_res) {
			xres_s = mode[i].x_res;
			for (; i < size; i++) {
				if (mode[i].x_res != xres_s)
					return mode[i - 1].mode;
				if (yres <= mode[i].y_res)
					return mode[i].mode;
			}
		}
	}
	return mode[size - 1].mode;
}

/*
 * neoCalcVCLK --
 *
 * Determine the closest clock frequency to the one requested.
 */
#define REF_FREQ 0xe517		/* 14.31818 in 20.12 fixed point */
#define MAX_N 127
#define MAX_D 31
#define MAX_F 1

static void neoCalcVCLK(const struct fb_info *info,
			struct neofb_par *par, long freq)
{
	int n, d, f;
	int n_best = 0, d_best = 0, f_best = 0;
	long f_best_diff = (0x7ffff << 12);	/* 20.12 */
	long f_target = (freq << 12) / 1000;	/* 20.12 */

	for (f = 0; f <= MAX_F; f++)
		for (n = 0; n <= MAX_N; n++)
			for (d = 0; d <= MAX_D; d++) {
				long f_out;	/* 20.12 */
				long f_diff;	/* 20.12 */

				f_out =
				    ((((n + 1) << 12) / ((d +
							  1) *
							 (1 << f))) >> 12)
				    * REF_FREQ;
				f_diff = abs(f_out - f_target);
				if (f_diff < f_best_diff) {
					f_best_diff = f_diff;
					n_best = n;
					d_best = d;
					f_best = f;
				}
			}

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		/* NOT_DONE:  We are trying the full range of the 2200 clock.
		   We should be able to try n up to 2047 */
		par->VCLK3NumeratorLow = n_best;
		par->VCLK3NumeratorHigh = (f_best << 7);
	} else
		par->VCLK3NumeratorLow = n_best | (f_best << 7);

	par->VCLK3Denominator = d_best;

#ifdef NEOFB_DEBUG
	printk("neoVCLK: f:%d NumLow=%d NumHi=%d Den=%d Df=%d\n",
	       f_target >> 12,
	       par->VCLK3NumeratorLow,
	       par->VCLK3NumeratorHigh,
	       par->VCLK3Denominator, f_best_diff >> 12);
#endif
}

/*
 * vgaHWInit --
 *      Handle the initialization, etc. of a screen.
 *      Return FALSE on failure.
 */

static int vgaHWInit(const struct fb_var_screeninfo *var,
		     const struct fb_info *info,
		     struct neofb_par *par, struct xtimings *timings)
{
	par->MiscOutReg = 0x23;

	if (!(timings->sync & FB_SYNC_HOR_HIGH_ACT))
		par->MiscOutReg |= 0x40;

	if (!(timings->sync & FB_SYNC_VERT_HIGH_ACT))
		par->MiscOutReg |= 0x80;

	/*
	 * Time Sequencer
	 */
	par->Sequencer[0] = 0x00;
	par->Sequencer[1] = 0x01;
	par->Sequencer[2] = 0x0F;
	par->Sequencer[3] = 0x00;	/* Font select */
	par->Sequencer[4] = 0x0E;	/* Misc */

	/*
	 * CRTC Controller
	 */
	par->CRTC[0] = (timings->HTotal >> 3) - 5;
	par->CRTC[1] = (timings->HDisplay >> 3) - 1;
	par->CRTC[2] = (timings->HDisplay >> 3) - 1;
	par->CRTC[3] = (((timings->HTotal >> 3) - 1) & 0x1F) | 0x80;
	par->CRTC[4] = (timings->HSyncStart >> 3);
	par->CRTC[5] = ((((timings->HTotal >> 3) - 1) & 0x20) << 2)
	    | (((timings->HSyncEnd >> 3)) & 0x1F);
	par->CRTC[6] = (timings->VTotal - 2) & 0xFF;
	par->CRTC[7] = (((timings->VTotal - 2) & 0x100) >> 8)
	    | (((timings->VDisplay - 1) & 0x100) >> 7)
	    | ((timings->VSyncStart & 0x100) >> 6)
	    | (((timings->VDisplay - 1) & 0x100) >> 5)
	    | 0x10 | (((timings->VTotal - 2) & 0x200) >> 4)
	    | (((timings->VDisplay - 1) & 0x200) >> 3)
	    | ((timings->VSyncStart & 0x200) >> 2);
	par->CRTC[8] = 0x00;
	par->CRTC[9] = (((timings->VDisplay - 1) & 0x200) >> 4) | 0x40;

	if (timings->dblscan)
		par->CRTC[9] |= 0x80;

	par->CRTC[10] = 0x00;
	par->CRTC[11] = 0x00;
	par->CRTC[12] = 0x00;
	par->CRTC[13] = 0x00;
	par->CRTC[14] = 0x00;
	par->CRTC[15] = 0x00;
	par->CRTC[16] = timings->VSyncStart & 0xFF;
	par->CRTC[17] = (timings->VSyncEnd & 0x0F) | 0x20;
	par->CRTC[18] = (timings->VDisplay - 1) & 0xFF;
	par->CRTC[19] = var->xres_virtual >> 4;
	par->CRTC[20] = 0x00;
	par->CRTC[21] = (timings->VDisplay - 1) & 0xFF;
	par->CRTC[22] = (timings->VTotal - 1) & 0xFF;
	par->CRTC[23] = 0xC3;
	par->CRTC[24] = 0xFF;

	/*
	 * are these unnecessary?
	 * vgaHWHBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN | KGA_ENABLE_ON_ZERO);
	 * vgaHWVBlankKGA(mode, regp, 0, KGA_FIX_OVERSCAN | KGA_ENABLE_ON_ZERO);
	 */

	/*
	 * Graphics Display Controller
	 */
	par->Graphics[0] = 0x00;
	par->Graphics[1] = 0x00;
	par->Graphics[2] = 0x00;
	par->Graphics[3] = 0x00;
	par->Graphics[4] = 0x00;
	par->Graphics[5] = 0x40;
	par->Graphics[6] = 0x05;	/* only map 64k VGA memory !!!! */
	par->Graphics[7] = 0x0F;
	par->Graphics[8] = 0xFF;


	par->Attribute[0] = 0x00;	/* standard colormap translation */
	par->Attribute[1] = 0x01;
	par->Attribute[2] = 0x02;
	par->Attribute[3] = 0x03;
	par->Attribute[4] = 0x04;
	par->Attribute[5] = 0x05;
	par->Attribute[6] = 0x06;
	par->Attribute[7] = 0x07;
	par->Attribute[8] = 0x08;
	par->Attribute[9] = 0x09;
	par->Attribute[10] = 0x0A;
	par->Attribute[11] = 0x0B;
	par->Attribute[12] = 0x0C;
	par->Attribute[13] = 0x0D;
	par->Attribute[14] = 0x0E;
	par->Attribute[15] = 0x0F;
	par->Attribute[16] = 0x41;
	par->Attribute[17] = 0xFF;
	par->Attribute[18] = 0x0F;
	par->Attribute[19] = 0x00;
	par->Attribute[20] = 0x00;

	return 0;
}

static void vgaHWLock(void)
{
	/* Protect CRTC[0-7] */
	VGAwCR(0x11, VGArCR(0x11) | 0x80);
}

static void vgaHWUnlock(void)
{
	/* Unprotect CRTC[0-7] */
	VGAwCR(0x11, VGArCR(0x11) & ~0x80);
}

static void neoLock(void)
{
	VGAwGR(0x09, 0x00);
	vgaHWLock();
}

static void neoUnlock(void)
{
	vgaHWUnlock();
	VGAwGR(0x09, 0x26);
}

/*
 * vgaHWSeqReset
 *      perform a sequencer reset.
 */
void vgaHWSeqReset(int start)
{
	if (start)
		VGAwSEQ(0x00, 0x01);	/* Synchronous Reset */
	else
		VGAwSEQ(0x00, 0x03);	/* End Reset */
}

void vgaHWProtect(int on)
{
	unsigned char tmp;

	if (on) {
		/*
		 * Turn off screen and disable sequencer.
		 */
		tmp = VGArSEQ(0x01);

		vgaHWSeqReset(1);	/* start synchronous reset */
		VGAwSEQ(0x01, tmp | 0x20);	/* disable the display */

		VGAenablePalette();
	} else {
		/*
		 * Reenable sequencer, then turn on screen.
		 */

		tmp = VGArSEQ(0x01);

		VGAwSEQ(0x01, tmp & ~0x20);	/* reenable display */
		vgaHWSeqReset(0);	/* clear synchronousreset */

		VGAdisablePalette();
	}
}

static void vgaHWRestore(const struct fb_info *info,
			 const struct neofb_par *par)
{
	int i;

	VGAwMISC(par->MiscOutReg);

	for (i = 1; i < 5; i++)
		VGAwSEQ(i, par->Sequencer[i]);

	/* Ensure CRTC registers 0-7 are unlocked by clearing bit 7 or CRTC[17] */
	VGAwCR(17, par->CRTC[17] & ~0x80);

	for (i = 0; i < 25; i++)
		VGAwCR(i, par->CRTC[i]);

	for (i = 0; i < 9; i++)
		VGAwGR(i, par->Graphics[i]);

	VGAenablePalette();

	for (i = 0; i < 21; i++)
		VGAwATTR(i, par->Attribute[i]);

	VGAdisablePalette();
}


/* -------------------- Hardware specific routines ------------------------- */

/*
 * Hardware Acceleration for Neo2200+
 */
static inline int neo2200_sync(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	int waitcycles;

	while (par->neo2200->bltStat & 1)
		waitcycles++;
	return 0;
}

static inline void neo2200_wait_fifo(struct fb_info *info,
				     int requested_fifo_space)
{
	//  ndev->neo.waitfifo_calls++;
	//  ndev->neo.waitfifo_sum += requested_fifo_space;

	/* FIXME: does not work
	   if (neo_fifo_space < requested_fifo_space)
	   {
	   neo_fifo_waitcycles++;

	   while (1)
	   {
	   neo_fifo_space = (neo2200->bltStat >> 8);
	   if (neo_fifo_space >= requested_fifo_space)
	   break;
	   }
	   }
	   else
	   {
	   neo_fifo_cache_hits++;
	   }

	   neo_fifo_space -= requested_fifo_space;
	 */

	neo2200_sync(info);
}

static inline void neo2200_accel_init(struct fb_info *info,
				      struct fb_var_screeninfo *var)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	Neo2200 *neo2200 = par->neo2200;
	u32 bltMod, pitch;

	neo2200_sync(info);

	switch (var->bits_per_pixel) {
	case 8:
		bltMod = NEO_MODE1_DEPTH8;
		pitch = var->xres_virtual;
		break;
	case 15:
	case 16:
		bltMod = NEO_MODE1_DEPTH16;
		pitch = var->xres_virtual * 2;
		break;
	default:
		printk(KERN_ERR
		       "neofb: neo2200_accel_init: unexpected bits per pixel!\n");
		return;
	}

	neo2200->bltStat = bltMod << 16;
	neo2200->pitch = (pitch << 16) | pitch;
}

/* --------------------------------------------------------------------- */

static int
neofb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	unsigned int pixclock = var->pixclock;
	struct xtimings timings;
	int memlen, vramlen;
	int mode_ok = 0;

	DBG("neofb_check_var");

	if (!pixclock)
		pixclock = 10000;	/* 10ns = 100MHz */
	timings.pixclock = 1000000000 / pixclock;
	if (timings.pixclock < 1)
		timings.pixclock = 1;

	if (timings.pixclock > par->maxClock)
		return -EINVAL;

	timings.dblscan = var->vmode & FB_VMODE_DOUBLE;
	timings.interlaced = var->vmode & FB_VMODE_INTERLACED;
	timings.HDisplay = var->xres;
	timings.HSyncStart = timings.HDisplay + var->right_margin;
	timings.HSyncEnd = timings.HSyncStart + var->hsync_len;
	timings.HTotal = timings.HSyncEnd + var->left_margin;
	timings.VDisplay = var->yres;
	timings.VSyncStart = timings.VDisplay + var->lower_margin;
	timings.VSyncEnd = timings.VSyncStart + var->vsync_len;
	timings.VTotal = timings.VSyncEnd + var->upper_margin;
	timings.sync = var->sync;

	/* Is the mode larger than the LCD panel? */
	if (par->internal_display &&
            ((var->xres > par->NeoPanelWidth) ||
	     (var->yres > par->NeoPanelHeight))) {
		printk(KERN_INFO
		       "Mode (%dx%d) larger than the LCD panel (%dx%d)\n",
		       var->xres, var->yres, par->NeoPanelWidth,
		       par->NeoPanelHeight);
		return -EINVAL;
	}

	/* Is the mode one of the acceptable sizes? */
	if (!par->internal_display)
		mode_ok = 1;
	else {
		switch (var->xres) {
		case 1280:
			if (var->yres == 1024)
				mode_ok = 1;
			break;
		case 1024:
			if (var->yres == 768)
				mode_ok = 1;
			break;
		case 800:
			if (var->yres == (par->libretto ? 480 : 600))
				mode_ok = 1;
			break;
		case 640:
			if (var->yres == 480)
				mode_ok = 1;
			break;
		}
	}

	if (!mode_ok) {
		printk(KERN_INFO
		       "Mode (%dx%d) won't display properly on LCD\n",
		       var->xres, var->yres);
		return -EINVAL;
	}

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 8:		/* PSEUDOCOLOUR, 256 */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;

	case 16:		/* DIRECTCOLOUR, 64k */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		break;

	case 24:		/* TRUECOLOUR, 16m */
		var->transp.offset = 0;
		var->transp.length = 0;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		break;

#ifdef NO_32BIT_SUPPORT_YET
	case 32:		/* TRUECOLOUR, 16m */
		var->transp.offset = 24;
		var->transp.length = 8;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		break;
#endif
	default:
		printk(KERN_WARNING "neofb: no support for %dbpp\n",
		       var->bits_per_pixel);
		return -EINVAL;
	}

	vramlen = info->fix.smem_len;
	if (vramlen > 4 * 1024 * 1024)
		vramlen = 4 * 1024 * 1024;

	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;
	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;

	memlen =
	    var->xres_virtual * var->bits_per_pixel * var->yres_virtual /
	    8;
	if (memlen > vramlen) {
		var->yres_virtual =
		    vramlen * 8 / (var->xres_virtual *
				   var->bits_per_pixel);
		memlen =
		    var->xres_virtual * var->bits_per_pixel *
		    var->yres_virtual / 8;
	}

	/* we must round yres/xres down, we already rounded y/xres_virtual up
	   if it was possible. We should return -EINVAL, but I disagree */
	if (var->yres_virtual < var->yres)
		var->yres = var->yres_virtual;
	if (var->xres_virtual < var->xres)
		var->xres = var->xres_virtual;
	if (var->xoffset + var->xres > var->xres_virtual)
		var->xoffset = var->xres_virtual - var->xres;
	if (var->yoffset + var->yres > var->yres_virtual)
		var->yoffset = var->yres_virtual - var->yres;

	var->nonstd = 0;
	var->height = -1;
	var->width = -1;

	if (var->bits_per_pixel >= 24 || !par->neo2200)
		var->accel_flags &= ~FB_ACCELF_TEXT;
	return 0;
}

static int neofb_set_par(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	struct xtimings timings;
	unsigned char temp;
	int i, clock_hi = 0;
	int lcd_stretch;
	int hoffset, voffset;

	DBG("neofb_set_par");

	neoUnlock();

	vgaHWProtect(1);	/* Blank the screen */

	timings.dblscan = info->var.vmode & FB_VMODE_DOUBLE;
	timings.interlaced = info->var.vmode & FB_VMODE_INTERLACED;
	timings.HDisplay = info->var.xres;
	timings.HSyncStart = timings.HDisplay + info->var.right_margin;
	timings.HSyncEnd = timings.HSyncStart + info->var.hsync_len;
	timings.HTotal = timings.HSyncEnd + info->var.left_margin;
	timings.VDisplay = info->var.yres;
	timings.VSyncStart = timings.VDisplay + info->var.lower_margin;
	timings.VSyncEnd = timings.VSyncStart + info->var.vsync_len;
	timings.VTotal = timings.VSyncEnd + info->var.upper_margin;
	timings.sync = info->var.sync;
	timings.pixclock = PICOS2KHZ(info->var.pixclock);

	if (timings.pixclock < 1)
		timings.pixclock = 1;

	/*
	 * This will allocate the datastructure and initialize all of the
	 * generic VGA registers.
	 */

	if (vgaHWInit(&info->var, info, par, &timings))
		return -EINVAL;

	/*
	 * The default value assigned by vgaHW.c is 0x41, but this does
	 * not work for NeoMagic.
	 */
	par->Attribute[16] = 0x01;

	switch (info->var.bits_per_pixel) {
	case 8:
		par->CRTC[0x13] = info->var.xres_virtual >> 3;
		par->ExtCRTOffset = info->var.xres_virtual >> 11;
		par->ExtColorModeSelect = 0x11;
		break;
	case 16:
		par->CRTC[0x13] = info->var.xres_virtual >> 2;
		par->ExtCRTOffset = info->var.xres_virtual >> 10;
		par->ExtColorModeSelect = 0x13;
		break;
	case 24:
		par->CRTC[0x13] = (info->var.xres_virtual * 3) >> 3;
		par->ExtCRTOffset = (info->var.xres_virtual * 3) >> 11;
		par->ExtColorModeSelect = 0x14;
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:		/* FIXME: guessed values */
		par->CRTC[0x13] = info->var.xres_virtual >> 1;
		par->ExtCRTOffset = info->var.xres_virtual >> 9;
		par->ExtColorModeSelect = 0x15;
		break;
#endif
	default:
		break;
	}

	par->ExtCRTDispAddr = 0x10;

	/* Vertical Extension */
	par->VerticalExt = (((timings.VTotal - 2) & 0x400) >> 10)
	    | (((timings.VDisplay - 1) & 0x400) >> 9)
	    | (((timings.VSyncStart) & 0x400) >> 8)
	    | (((timings.VSyncStart) & 0x400) >> 7);

	/* Fast write bursts on unless disabled. */
	if (par->pci_burst)
		par->SysIfaceCntl1 = 0x30;
	else
		par->SysIfaceCntl1 = 0x00;

	par->SysIfaceCntl2 = 0xc0;	/* VESA Bios sets this to 0x80! */

	/* Enable any user specified display devices. */
	par->PanelDispCntlReg1 = 0x00;
	if (par->internal_display)
		par->PanelDispCntlReg1 |= 0x02;
	if (par->external_display)
		par->PanelDispCntlReg1 |= 0x01;

	/* If the user did not specify any display devices, then... */
	if (par->PanelDispCntlReg1 == 0x00) {
		/* Default to internal (i.e., LCD) only. */
		par->PanelDispCntlReg1 |= 0x02;
	}

	/* If we are using a fixed mode, then tell the chip we are. */
	switch (info->var.xres) {
	case 1280:
		par->PanelDispCntlReg1 |= 0x60;
		break;
	case 1024:
		par->PanelDispCntlReg1 |= 0x40;
		break;
	case 800:
		par->PanelDispCntlReg1 |= 0x20;
		break;
	case 640:
	default:
		break;
	}

	/* Setup shadow register locking. */
	switch (par->PanelDispCntlReg1 & 0x03) {
	case 0x01:		/* External CRT only mode: */
		par->GeneralLockReg = 0x00;
		/* We need to program the VCLK for external display only mode. */
		par->ProgramVCLK = 1;
		break;
	case 0x02:		/* Internal LCD only mode: */
	case 0x03:		/* Simultaneous internal/external (LCD/CRT) mode: */
		par->GeneralLockReg = 0x01;
		/* Don't program the VCLK when using the LCD. */
		par->ProgramVCLK = 0;
		break;
	}

	/*
	 * If the screen is to be stretched, turn on stretching for the
	 * various modes.
	 *
	 * OPTION_LCD_STRETCH means stretching should be turned off!
	 */
	par->PanelDispCntlReg2 = 0x00;
	par->PanelDispCntlReg3 = 0x00;

	if (par->lcd_stretch && (par->PanelDispCntlReg1 == 0x02) &&	/* LCD only */
	    (info->var.xres != par->NeoPanelWidth)) {
		switch (info->var.xres) {
		case 320:	/* Needs testing.  KEM -- 24 May 98 */
		case 400:	/* Needs testing.  KEM -- 24 May 98 */
		case 640:
		case 800:
		case 1024:
			lcd_stretch = 1;
			par->PanelDispCntlReg2 |= 0xC6;
			break;
		default:
			lcd_stretch = 0;
			/* No stretching in these modes. */
		}
	} else
		lcd_stretch = 0;

	/*
	 * If the screen is to be centerd, turn on the centering for the
	 * various modes.
	 */
	par->PanelVertCenterReg1 = 0x00;
	par->PanelVertCenterReg2 = 0x00;
	par->PanelVertCenterReg3 = 0x00;
	par->PanelVertCenterReg4 = 0x00;
	par->PanelVertCenterReg5 = 0x00;
	par->PanelHorizCenterReg1 = 0x00;
	par->PanelHorizCenterReg2 = 0x00;
	par->PanelHorizCenterReg3 = 0x00;
	par->PanelHorizCenterReg4 = 0x00;
	par->PanelHorizCenterReg5 = 0x00;


	if (par->PanelDispCntlReg1 & 0x02) {
		if (info->var.xres == par->NeoPanelWidth) {
			/*
			 * No centering required when the requested display width
			 * equals the panel width.
			 */
		} else {
			par->PanelDispCntlReg2 |= 0x01;
			par->PanelDispCntlReg3 |= 0x10;

			/* Calculate the horizontal and vertical offsets. */
			if (!lcd_stretch) {
				hoffset =
				    ((par->NeoPanelWidth -
				      info->var.xres) >> 4) - 1;
				voffset =
				    ((par->NeoPanelHeight -
				      info->var.yres) >> 1) - 2;
			} else {
				/* Stretched modes cannot be centered. */
				hoffset = 0;
				voffset = 0;
			}

			switch (info->var.xres) {
			case 320:	/* Needs testing.  KEM -- 24 May 98 */
				par->PanelHorizCenterReg3 = hoffset;
				par->PanelVertCenterReg2 = voffset;
				break;
			case 400:	/* Needs testing.  KEM -- 24 May 98 */
				par->PanelHorizCenterReg4 = hoffset;
				par->PanelVertCenterReg1 = voffset;
				break;
			case 640:
				par->PanelHorizCenterReg1 = hoffset;
				par->PanelVertCenterReg3 = voffset;
				break;
			case 800:
				par->PanelHorizCenterReg2 = hoffset;
				par->PanelVertCenterReg4 = voffset;
				break;
			case 1024:
				par->PanelHorizCenterReg5 = hoffset;
				par->PanelVertCenterReg5 = voffset;
				break;
			case 1280:
			default:
				/* No centering in these modes. */
				break;
			}
		}
	}

	par->biosMode =
	    neoFindMode(info->var.xres, info->var.yres,
			info->var.bits_per_pixel);

	/*
	 * Calculate the VCLK that most closely matches the requested dot
	 * clock.
	 */
	neoCalcVCLK(info, par, timings.pixclock);

	/* Since we program the clocks ourselves, always use VCLK3. */
	par->MiscOutReg |= 0x0C;

	/* linear colormap for non palettized modes */
	switch (info->var.bits_per_pixel) {
	case 8:
		/* PseudoColor, 256 */
		info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	case 16:
		/* DirectColor, 64k */
		info->fix.visual = FB_VISUAL_DIRECTCOLOR;

		for (i = 0; i < 64; i++) {
			outb(i, 0x3c8);

			outb(i << 1, 0x3c9);
			outb(i, 0x3c9);
			outb(i << 1, 0x3c9);
		}
		break;
	case 24:
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
#endif
		/* TrueColor, 16m */
		info->fix.visual = FB_VISUAL_TRUECOLOR;

		for (i = 0; i < 256; i++) {
			outb(i, 0x3c8);

			outb(i, 0x3c9);
			outb(i, 0x3c9);
			outb(i, 0x3c9);
		}
		break;
	}

	/* alread unlocked above */
	/* BOGUS  VGAwGR (0x09, 0x26); */

	/* don't know what this is, but it's 0 from bootup anyway */
	VGAwGR(0x15, 0x00);

	/* was set to 0x01 by my bios in text and vesa modes */
	VGAwGR(0x0A, par->GeneralLockReg);

	/*
	 * The color mode needs to be set before calling vgaHWRestore
	 * to ensure the DAC is initialized properly.
	 *
	 * NOTE: Make sure we don't change bits make sure we don't change
	 * any reserved bits.
	 */
	temp = VGArGR(0x90);
	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		temp &= 0xF0;	/* Save bits 7:4 */
		temp |= (par->ExtColorModeSelect & ~0xF0);
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
	case FB_ACCEL_NEOMAGIC_NM2097:
	case FB_ACCEL_NEOMAGIC_NM2160:
	case FB_ACCEL_NEOMAGIC_NM2200:
	case FB_ACCEL_NEOMAGIC_NM2230:
	case FB_ACCEL_NEOMAGIC_NM2360:
	case FB_ACCEL_NEOMAGIC_NM2380:
		temp &= 0x70;	/* Save bits 6:4 */
		temp |= (par->ExtColorModeSelect & ~0x70);
		break;
	}

	VGAwGR(0x90, temp);

	/*
	 * In some rare cases a lockup might occur if we don't delay
	 * here. (Reported by Miles Lane)
	 */
	//mdelay(200);

	/*
	 * Disable horizontal and vertical graphics and text expansions so
	 * that vgaHWRestore works properly.
	 */
	temp = VGArGR(0x25);
	temp &= 0x39;
	VGAwGR(0x25, temp);

	/*
	 * Sleep for 200ms to make sure that the two operations above have
	 * had time to take effect.
	 */
	mdelay(200);

	/*
	 * This function handles restoring the generic VGA registers.  */
	vgaHWRestore(info, par);


	VGAwGR(0x0E, par->ExtCRTDispAddr);
	VGAwGR(0x0F, par->ExtCRTOffset);
	temp = VGArGR(0x10);
	temp &= 0x0F;		/* Save bits 3:0 */
	temp |= (par->SysIfaceCntl1 & ~0x0F);	/* VESA Bios sets bit 1! */
	VGAwGR(0x10, temp);

	VGAwGR(0x11, par->SysIfaceCntl2);
	VGAwGR(0x15, 0 /*par->SingleAddrPage */ );
	VGAwGR(0x16, 0 /*par->DualAddrPage */ );

	temp = VGArGR(0x20);
	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		temp &= 0xFC;	/* Save bits 7:2 */
		temp |= (par->PanelDispCntlReg1 & ~0xFC);
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
	case FB_ACCEL_NEOMAGIC_NM2097:
	case FB_ACCEL_NEOMAGIC_NM2160:
		temp &= 0xDC;	/* Save bits 7:6,4:2 */
		temp |= (par->PanelDispCntlReg1 & ~0xDC);
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
	case FB_ACCEL_NEOMAGIC_NM2230:
	case FB_ACCEL_NEOMAGIC_NM2360:
	case FB_ACCEL_NEOMAGIC_NM2380:
		temp &= 0x98;	/* Save bits 7,4:3 */
		temp |= (par->PanelDispCntlReg1 & ~0x98);
		break;
	}
	VGAwGR(0x20, temp);

	temp = VGArGR(0x25);
	temp &= 0x38;		/* Save bits 5:3 */
	temp |= (par->PanelDispCntlReg2 & ~0x38);
	VGAwGR(0x25, temp);

	if (info->fix.accel != FB_ACCEL_NEOMAGIC_NM2070) {
		temp = VGArGR(0x30);
		temp &= 0xEF;	/* Save bits 7:5 and bits 3:0 */
		temp |= (par->PanelDispCntlReg3 & ~0xEF);
		VGAwGR(0x30, temp);
	}

	VGAwGR(0x28, par->PanelVertCenterReg1);
	VGAwGR(0x29, par->PanelVertCenterReg2);
	VGAwGR(0x2a, par->PanelVertCenterReg3);

	if (info->fix.accel != FB_ACCEL_NEOMAGIC_NM2070) {
		VGAwGR(0x32, par->PanelVertCenterReg4);
		VGAwGR(0x33, par->PanelHorizCenterReg1);
		VGAwGR(0x34, par->PanelHorizCenterReg2);
		VGAwGR(0x35, par->PanelHorizCenterReg3);
	}

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2160)
		VGAwGR(0x36, par->PanelHorizCenterReg4);

	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		VGAwGR(0x36, par->PanelHorizCenterReg4);
		VGAwGR(0x37, par->PanelVertCenterReg5);
		VGAwGR(0x38, par->PanelHorizCenterReg5);

		clock_hi = 1;
	}

	/* Program VCLK3 if needed. */
	if (par->ProgramVCLK && ((VGArGR(0x9B) != par->VCLK3NumeratorLow)
				 || (VGArGR(0x9F) != par->VCLK3Denominator)
				 || (clock_hi && ((VGArGR(0x8F) & ~0x0f)
						  != (par->
						      VCLK3NumeratorHigh &
						      ~0x0F))))) {
		VGAwGR(0x9B, par->VCLK3NumeratorLow);
		if (clock_hi) {
			temp = VGArGR(0x8F);
			temp &= 0x0F;	/* Save bits 3:0 */
			temp |= (par->VCLK3NumeratorHigh & ~0x0F);
			VGAwGR(0x8F, temp);
		}
		VGAwGR(0x9F, par->VCLK3Denominator);
	}

	if (par->biosMode)
		VGAwCR(0x23, par->biosMode);

	VGAwGR(0x93, 0xc0);	/* Gives 5x faster framebuffer writes !!! */

	/* Program vertical extension register */
	if (info->fix.accel == FB_ACCEL_NEOMAGIC_NM2200 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2230 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2360 ||
	    info->fix.accel == FB_ACCEL_NEOMAGIC_NM2380) {
		VGAwCR(0x70, par->VerticalExt);
	}

	vgaHWProtect(0);	/* Turn on screen */

	/* Calling this also locks offset registers required in update_start */
	neoLock();

	info->fix.line_length =
	    info->var.xres_virtual * (info->var.bits_per_pixel >> 3);

	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_accel_init(info, &info->var);
			break;
		default:
			break;
	}	
	return 0;
}

static void neofb_update_start(struct fb_info *info,
			       struct fb_var_screeninfo *var)
{
	int oldExtCRTDispAddr;
	int Base;

	DBG("neofb_update_start");

	Base = (var->yoffset * var->xres_virtual + var->xoffset) >> 2;
	Base *= (var->bits_per_pixel + 7) / 8;

	neoUnlock();

	/*
	 * These are the generic starting address registers.
	 */
	VGAwCR(0x0C, (Base & 0x00FF00) >> 8);
	VGAwCR(0x0D, (Base & 0x00FF));

	/*
	 * Make sure we don't clobber some other bits that might already
	 * have been set. NOTE: NM2200 has a writable bit 3, but it shouldn't
	 * be needed.
	 */
	oldExtCRTDispAddr = VGArGR(0x0E);
	VGAwGR(0x0E, (((Base >> 16) & 0x0f) | (oldExtCRTDispAddr & 0xf0)));

	neoLock();
}

/*
 *    Pan or Wrap the Display
 */
static int neofb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	u_int y_bottom;

	y_bottom = var->yoffset;

	if (!(var->vmode & FB_VMODE_YWRAP))
		y_bottom += var->yres;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (y_bottom > info->var.yres_virtual)
		return -EINVAL;

	neofb_update_start(info, var);

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

static int neofb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			   u_int transp, struct fb_info *fb)
{
	if (regno >= NR_PALETTE)
		return -EINVAL;

	switch (fb->var.bits_per_pixel) {
	case 8:
		outb(regno, 0x3c8);

		outb(red >> 10, 0x3c9);
		outb(green >> 10, 0x3c9);
		outb(blue >> 10, 0x3c9);
		break;
	case 16:
		if (regno < 16)
			((u16 *) fb->pseudo_palette)[regno] =
			    ((red & 0xf800)) | ((green & 0xfc00) >> 5) |
			    ((blue & 0xf800) >> 11);
		break;
	case 24:
		if (regno < 16)
			((u32 *) fb->pseudo_palette)[regno] =
			    ((red & 0xff00) << 8) | ((green & 0xff00)) |
			    ((blue & 0xff00) >> 8);
		break;
#ifdef NO_32BIT_SUPPORT_YET
	case 32:
		if (regno < 16)
			((u32 *) fb->pseudo_palette)[regno] =
			    ((transp & 0xff00) << 16) | ((red & 0xff00) <<
							 8) | ((green &
								0xff00)) |
			    ((blue & 0xff00) >> 8);
		break;
#endif
	default:
		return 1;
	}
	return 0;
}

/*
 *    (Un)Blank the display.
 */
static int neofb_blank(int blank, struct fb_info *info)
{
	/*
	 *  Blank the screen if blank_mode != 0, else unblank. If
	 *  blank == NULL then the caller blanks by setting the CLUT
	 *  (Color Look Up Table) to all black. Return 0 if blanking
	 *  succeeded, != 0 if un-/blanking failed due to e.g. a
	 *  video mode which doesn't support it. Implements VESA
	 *  suspend and powerdown modes on hardware that supports
	 *  disabling hsync/vsync:
	 *    blank_mode == 2: suspend vsync
	 *    blank_mode == 3: suspend hsync
	 *    blank_mode == 4: powerdown
	 *
	 *  wms...Enable VESA DMPS compatible powerdown mode
	 *  run "setterm -powersave powerdown" to take advantage
	 */

	switch (blank) {
	case 4:		/* powerdown - both sync lines down */
#ifdef CONFIG_TOSHIBA
		/* attempt to turn off backlight on toshiba; also turns off external */
		{
			SMMRegisters regs;

			regs.eax = 0xff00; /* HCI_SET */
			regs.ebx = 0x0002; /* HCI_BACKLIGHT */
			regs.ecx = 0x0000; /* HCI_DISABLE */
			tosh_smm(&regs);
		}
#endif
		break;
	case 3:		/* hsync off */
		break;
	case 2:		/* vsync off */
		break;
	case 1:		/* just software blanking of screen */
		break;
	default:		/* case 0, or anything else: unblank */
#ifdef CONFIG_TOSHIBA
		/* attempt to re-enable backlight/external on toshiba */
		{
			SMMRegisters regs;

			regs.eax = 0xff00; /* HCI_SET */
			regs.ebx = 0x0002; /* HCI_BACKLIGHT */
			regs.ecx = 0x0001; /* HCI_ENABLE */
			tosh_smm(&regs);
		}
#endif
		break;
	}
	return 0;
}

static void
neo2200_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	u_long dst, rop;

	dst = rect->dx + rect->dy * info->var.xres_virtual;
	rop = rect->rop ? 0x060000 : 0x0c0000;

	neo2200_wait_fifo(info, 4);

	/* set blt control */
	par->neo2200->bltCntl = NEO_BC3_FIFO_EN |
	    NEO_BC0_SRC_IS_FG | NEO_BC3_SKIP_MAPPING |
	    //               NEO_BC3_DST_XY_ADDR  |
	    //               NEO_BC3_SRC_XY_ADDR  |
	    rop;

	switch (info->var.bits_per_pixel) {
	case 8:
		par->neo2200->fgColor = rect->color;
		break;
	case 16:
		par->neo2200->fgColor =
		    ((u16 *) (info->pseudo_palette))[rect->color];
		break;
	}

	par->neo2200->dstStart =
	    dst * ((info->var.bits_per_pixel + 7) / 8);
	par->neo2200->xyExt =
	    (rect->height << 16) | (rect->width & 0xffff);
}

static void
neo2200_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	u32 sx = area->sx, sy = area->sy, dx = area->dx, dy = area->dy;
	u_long src, dst, bltCntl;

	bltCntl = NEO_BC3_FIFO_EN | NEO_BC3_SKIP_MAPPING | 0x0C0000;

	if (sy < dy) {
		sy += (area->height - 1);
		dy += (area->height - 1);

		bltCntl |= NEO_BC0_DST_Y_DEC | NEO_BC0_SRC_Y_DEC;
	}

	if (area->sx < area->dx) {
		sx += (area->width - 1);
		dx += (area->width - 1);

		bltCntl |= NEO_BC0_X_DEC;
	}

	src = sx * (info->var.bits_per_pixel >> 3) + sy*info->fix.line_length;
	dst = dx * (info->var.bits_per_pixel >> 3) + dy*info->fix.line_length;

	neo2200_wait_fifo(info, 4);

	/* set blt control */
	par->neo2200->bltCntl = bltCntl;

	par->neo2200->srcStart = src;
	par->neo2200->dstStart = dst;
	par->neo2200->xyExt =
	    (area->height << 16) | (area->width & 0xffff);
}

static void
neo2200_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	neo2200_sync(info);

	switch (info->var.bits_per_pixel) {
	case 8:
		par->neo2200->fgColor = image->fg_color;
		par->neo2200->bgColor = image->bg_color;
		break;
	case 16:
		par->neo2200->fgColor =
		    ((u16 *) (info->pseudo_palette))[image->fg_color];
		par->neo2200->bgColor =
		    ((u16 *) (info->pseudo_palette))[image->bg_color];
		break;
	}

	par->neo2200->bltCntl = NEO_BC0_SYS_TO_VID |
	    NEO_BC0_SRC_MONO | NEO_BC3_SKIP_MAPPING |
	    //                      NEO_BC3_DST_XY_ADDR |
	    0x0c0000;

	par->neo2200->srcStart = 0;
//      par->neo2200->dstStart = (image->dy << 16) | (image->dx & 0xffff);
	par->neo2200->dstStart =
	    ((image->dx & 0xffff) * (info->var.bits_per_pixel >> 3) +
	     image->dy * info->fix.line_length);
	par->neo2200->xyExt =
	    (image->height << 16) | (image->width & 0xffff);

	memcpy(par->mmio_vbase + 0x100000, image->data,
	       (image->width * image->height) >> 3);
}

static void
neofb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_fillrect(info, rect);
			break;
		default:
			cfb_fillrect(info, rect);
			break;
	}	
}

static void
neofb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_copyarea(info, area);
			break;
		default:
			cfb_copyarea(info, area);
			break;
	}	
}

static void
neofb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_imageblit(info, image);
			break;
		default:
			cfb_imageblit(info, image);
			break;
	}
}	

static int 
neofb_sync(struct fb_info *info)
{
	switch (info->fix.accel) {
		case FB_ACCEL_NEOMAGIC_NM2200:
		case FB_ACCEL_NEOMAGIC_NM2230: 
		case FB_ACCEL_NEOMAGIC_NM2360: 
		case FB_ACCEL_NEOMAGIC_NM2380: 
			neo2200_sync(info);
			break;
		default:
			break;
	}
	return 0;		
}

static struct fb_ops neofb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= neofb_check_var,
	.fb_set_par	= neofb_set_par,
	.fb_setcolreg	= neofb_setcolreg,
	.fb_pan_display	= neofb_pan_display,
	.fb_blank	= neofb_blank,
	.fb_sync	= neofb_sync,
	.fb_fillrect	= neofb_fillrect,
	.fb_copyarea	= neofb_copyarea,
	.fb_imageblit	= neofb_imageblit,
	.fb_cursor	= soft_cursor,
};

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo __devinitdata neofb_var640x480x8 = {
	.accel_flags    = FB_ACCELF_TEXT,
	.xres           = 640,
	.yres           = 480,
	.xres_virtual   = 640,
	.yres_virtual   = 30000,
	.bits_per_pixel = 8,
	.pixclock       = 39722,
	.left_margin    = 48,
	.right_margin   = 16,
	.upper_margin   = 33,
	.lower_margin   = 10,
	.hsync_len      = 96,
	.vsync_len      = 2,
	.vmode          = FB_VMODE_NONINTERLACED
};

static struct fb_var_screeninfo __devinitdata neofb_var800x600x8 = {
	.accel_flags    = FB_ACCELF_TEXT,
	.xres           = 800,
	.yres           = 600,
	.xres_virtual   = 800,
	.yres_virtual   = 30000,
	.bits_per_pixel = 8,
	.pixclock       = 25000,
	.left_margin    = 88,
	.right_margin   = 40,
	.upper_margin   = 23,
	.lower_margin   = 1,
	.hsync_len      = 128,
	.vsync_len      = 4,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};

static struct fb_var_screeninfo __devinitdata neofb_var800x480x8 = {
	.accel_flags    = FB_ACCELF_TEXT,
	.xres           = 800,
	.yres           = 480,
	.xres_virtual   = 800,
	.yres_virtual   = 30000,
	.bits_per_pixel = 8,
	.pixclock       = 25000,
	.left_margin    = 88,
	.right_margin   = 40,
	.upper_margin   = 23,
	.lower_margin   = 1,
	.hsync_len      = 128,
	.vsync_len      = 4,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};

static struct fb_var_screeninfo __devinitdata neofb_var1024x768x8 = {
	.accel_flags    = FB_ACCELF_TEXT,
	.xres           = 1024,
	.yres           = 768,
	.xres_virtual   = 1024,
	.yres_virtual   = 30000,
	.bits_per_pixel = 8,
	.pixclock       = 15385,
	.left_margin    = 160,
	.right_margin   = 24,
	.upper_margin   = 29,
	.lower_margin   = 3,
	.hsync_len      = 136,
	.vsync_len      = 6,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};

#ifdef NOT_DONE
static struct fb_var_screeninfo __devinitdata neofb_var1280x1024x8 = {
	.accel_flags    = FB_ACCELF_TEXT,
	.xres           = 1280,
	.yres           = 1024,
	.xres_virtual   = 1280,
	.yres_virtual   = 30000,
	.bits_per_pixel = 8,
	.pixclock       = 9260,
	.left_margin    = 248,
	.right_margin   = 48,
	.upper_margin   = 38,
	.lower_margin   = 1,
	.hsync_len      = 112,
	.vsync_len      = 3,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};
#endif

static int __devinit neo_map_mmio(struct fb_info *info,
				  struct pci_dev *dev)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_map_mmio");

	info->fix.mmio_start = pci_resource_start(dev, 1);
	info->fix.mmio_len = MMIO_SIZE;

	if (!request_mem_region
	    (info->fix.mmio_start, MMIO_SIZE, "memory mapped I/O")) {
		printk("neofb: memory mapped IO in use\n");
		return -EBUSY;
	}

	par->mmio_vbase = ioremap(info->fix.mmio_start, MMIO_SIZE);
	if (!par->mmio_vbase) {
		printk("neofb: unable to map memory mapped IO\n");
		release_mem_region(info->fix.mmio_start,
				   info->fix.mmio_len);
		return -ENOMEM;
	} else
		printk(KERN_INFO "neofb: mapped io at %p\n",
		       par->mmio_vbase);
	return 0;
}

static void __devinit neo_unmap_mmio(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_unmap_mmio");

	if (par->mmio_vbase) {
		iounmap(par->mmio_vbase);
		par->mmio_vbase = NULL;

		release_mem_region(info->fix.mmio_start,
				   info->fix.mmio_len);
	}
}

static int __devinit neo_map_video(struct fb_info *info,
				   struct pci_dev *dev, int video_len)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_map_video");

	info->fix.smem_start = pci_resource_start(dev, 0);
	info->fix.smem_len = video_len;

	if (!request_mem_region
	    (info->fix.smem_start, info->fix.smem_len, "frame buffer")) {
		printk("neofb: frame buffer in use\n");
		return -EBUSY;
	}

	info->screen_base =
	    ioremap(info->fix.smem_start, info->fix.smem_len);
	if (!info->screen_base) {
		printk("neofb: unable to map screen memory\n");
		release_mem_region(info->fix.smem_start,
				   info->fix.smem_len);
		return -ENOMEM;
	} else
		printk(KERN_INFO "neofb: mapped framebuffer at %p\n",
		       info->screen_base);

#ifdef CONFIG_MTRR
	par->mtrr =
	    mtrr_add(info->fix.smem_start, pci_resource_len(dev, 0),
		     MTRR_TYPE_WRCOMB, 1);
#endif

	/* Clear framebuffer, it's all white in memory after boot */
	memset(info->screen_base, 0, info->fix.smem_len);
	return 0;
}

static void __devinit neo_unmap_video(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;

	DBG("neo_unmap_video");

	if (info->screen_base) {
#ifdef CONFIG_MTRR
		mtrr_del(par->mtrr, info->fix.smem_start,
			 info->fix.smem_len);
#endif

		iounmap(info->screen_base);
		info->screen_base = NULL;

		release_mem_region(info->fix.smem_start,
				   info->fix.smem_len);
	}
}

static int __devinit neo_init_hw(struct fb_info *info)
{
	struct neofb_par *par = (struct neofb_par *) info->par;
	unsigned char type, display;
	int videoRam = 896;
	int maxClock = 65000;
	int CursorMem = 1024;
	int CursorOff = 0x100;
	int linearSize = 1024;
	int maxWidth = 1024;
	int maxHeight = 1024;
	int w;

	DBG("neo_init_hw");

	neoUnlock();

#if 0
	printk(KERN_DEBUG "--- Neo extended register dump ---\n");
	for (w = 0; w < 0x85; w++)
		printk(KERN_DEBUG "CR %p: %p\n", (void *) w,
		       (void *) VGArCR(w));
	for (w = 0; w < 0xC7; w++)
		printk(KERN_DEBUG "GR %p: %p\n", (void *) w,
		       (void *) VGArGR(w));
#endif

	/* Determine the panel type */
	VGAwGR(0x09, 0x26);
	type = VGArGR(0x21);
	display = VGArGR(0x20);
	if (!par->internal_display && !par->external_display) {
		par->internal_display = display & 2 || !(display & 3) ? 1 : 0;
		par->external_display = display & 1;
		printk (KERN_INFO "Autodetected %s display\n",
			par->internal_display && par->external_display ? "simultaneous" :
			par->internal_display ? "internal" : "external");
	}

	/* Determine panel width -- used in NeoValidMode. */
	w = VGArGR(0x20);
	VGAwGR(0x09, 0x00);
	switch ((w & 0x18) >> 3) {
	case 0x00:
		par->NeoPanelWidth = 640;
		par->NeoPanelHeight = 480;
		info->var = neofb_var640x480x8;
		break;
	case 0x01:
		par->NeoPanelWidth = 800;
		par->NeoPanelHeight = par->libretto ? 480 : 600;
		info->var = par->libretto ? neofb_var800x480x8 : neofb_var800x600x8;
		break;
	case 0x02:
		par->NeoPanelWidth = 1024;
		par->NeoPanelHeight = 768;
		info->var = neofb_var1024x768x8;
		break;
	case 0x03:
		/* 1280x1024 panel support needs to be added */
#ifdef NOT_DONE
		par->NeoPanelWidth = 1280;
		par->NeoPanelHeight = 1024;
		info->var = neofb_var1280x1024x8;
		break;
#else
		printk(KERN_ERR
		       "neofb: Only 640x480, 800x600/480 and 1024x768 panels are currently supported\n");
		return -1;
#endif
	default:
		par->NeoPanelWidth = 640;
		par->NeoPanelHeight = 480;
		info->var = neofb_var640x480x8;
		break;
	}

	printk(KERN_INFO "Panel is a %dx%d %s %s display\n",
	       par->NeoPanelWidth,
	       par->NeoPanelHeight,
	       (type & 0x02) ? "color" : "monochrome",
	       (type & 0x10) ? "TFT" : "dual scan");

	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		videoRam = 896;
		maxClock = 65000;
		CursorMem = 2048;
		CursorOff = 0x100;
		linearSize = 1024;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
	case FB_ACCEL_NEOMAGIC_NM2093:
		videoRam = 1152;
		maxClock = 80000;
		CursorMem = 2048;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2097:
		videoRam = 1152;
		maxClock = 80000;
		CursorMem = 1024;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2160:
		videoRam = 2048;
		maxClock = 90000;
		CursorMem = 1024;
		CursorOff = 0x100;
		linearSize = 2048;
		maxWidth = 1024;
		maxHeight = 1024;
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
		videoRam = 2560;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2230:
		videoRam = 3008;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2360:
		videoRam = 4096;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 4096;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 *) par->mmio_vbase;
		break;
	case FB_ACCEL_NEOMAGIC_NM2380:
		videoRam = 6144;
		maxClock = 110000;
		CursorMem = 1024;
		CursorOff = 0x1000;
		linearSize = 8192;
		maxWidth = 1280;
		maxHeight = 1024;	/* ???? */

		par->neo2200 = (Neo2200 *) par->mmio_vbase;
		break;
	}

	par->maxClock = maxClock;

	return videoRam * 1024;
}


static struct fb_info *__devinit neo_alloc_fb_info(struct pci_dev *dev, const struct
						   pci_device_id *id)
{
	struct fb_info *info;
	struct neofb_par *par;

	info = kmalloc(sizeof(struct fb_info) + sizeof(struct neofb_par) + 
		       sizeof(u32) * 17, GFP_KERNEL);

	if (!info)
		return NULL;

	memset(info, 0, sizeof(struct fb_info) + sizeof(struct neofb_par) + sizeof(u32) * 17);

	par = (struct neofb_par *) (info + 1);

	info->fix.accel = id->driver_data;

	par->pci_burst = !nopciburst;
	par->lcd_stretch = !nostretch;
	par->libretto = libretto;

	par->internal_display = internal;
	par->external_display = external;

	switch (info->fix.accel) {
	case FB_ACCEL_NEOMAGIC_NM2070:
		sprintf(info->fix.id, "MagicGraph 128");
		break;
	case FB_ACCEL_NEOMAGIC_NM2090:
		sprintf(info->fix.id, "MagicGraph 128V");
		break;
	case FB_ACCEL_NEOMAGIC_NM2093:
		sprintf(info->fix.id, "MagicGraph 128ZV");
		break;
	case FB_ACCEL_NEOMAGIC_NM2097:
		sprintf(info->fix.id, "MagicGraph 128ZV+");
		break;
	case FB_ACCEL_NEOMAGIC_NM2160:
		sprintf(info->fix.id, "MagicGraph 128XD");
		break;
	case FB_ACCEL_NEOMAGIC_NM2200:
		sprintf(info->fix.id, "MagicGraph 256AV");
		break;
	case FB_ACCEL_NEOMAGIC_NM2230:
		sprintf(info->fix.id, "MagicGraph 256AV+");
		break;
	case FB_ACCEL_NEOMAGIC_NM2360:
		sprintf(info->fix.id, "MagicGraph 256ZX");
		break;
	case FB_ACCEL_NEOMAGIC_NM2380:
		sprintf(info->fix.id, "MagicGraph 256XL+");
		break;
	}

	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 4;
	info->fix.ywrapstep = 0;
	info->fix.accel = id->driver_data;

	info->fbops = &neofb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->par = par;
	info->pseudo_palette = (void *) (par + 1);

	fb_alloc_cmap(&info->cmap, NR_PALETTE, 0);

	return info;
}

static void __devinit neo_free_fb_info(struct fb_info *info)
{
	if (info) {
		/*
		 * Free the colourmap
		 */
		fb_alloc_cmap(&info->cmap, 0, 0);

		kfree(info);
	}
}

/* --------------------------------------------------------------------- */

static int __devinit neofb_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	struct fb_info *info;
	u_int h_sync, v_sync;
	int err;
	int video_len;

	DBG("neofb_probe");

	err = pci_enable_device(dev);
	if (err)
		return err;

	err = -ENOMEM;
	info = neo_alloc_fb_info(dev, id);
	if (!info)
		goto failed;

	err = neo_map_mmio(info, dev);
	if (err)
		goto failed;

	video_len = neo_init_hw(info);
	if (video_len < 0) {
		err = video_len;
		goto failed;
	}

	err = neo_map_video(info, dev, video_len);
	if (err)
		goto failed;

	/*
	 * Calculate the hsync and vsync frequencies.  Note that
	 * we split the 1e12 constant up so that we can preserve
	 * the precision and fit the results into 32-bit registers.
	 *  (1953125000 * 512 = 1e12)
	 */
	h_sync = 1953125000 / info->var.pixclock;
	h_sync =
	    h_sync * 512 / (info->var.xres + info->var.left_margin +
			    info->var.right_margin + info->var.hsync_len);
	v_sync =
	    h_sync / (info->var.yres + info->var.upper_margin +
		      info->var.lower_margin + info->var.vsync_len);

	printk(KERN_INFO "neofb v" NEOFB_VERSION
	       ": %dkB VRAM, using %dx%d, %d.%03dkHz, %dHz\n",
	       info->fix.smem_len >> 10, info->var.xres,
	       info->var.yres, h_sync / 1000, h_sync % 1000, v_sync);


	err = register_framebuffer(info);
	if (err < 0)
		goto failed;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       info->node, info->fix.id);

	/*
	 * Our driver data
	 */
	pci_set_drvdata(dev, info);
	return 0;

      failed:
	neo_unmap_video(info);
	neo_unmap_mmio(info);
	neo_free_fb_info(info);

	return err;
}

static void __devexit neofb_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);

	DBG("neofb_remove");

	if (info) {
		/*
		 * If unregister_framebuffer fails, then
		 * we will be leaving hooks that could cause
		 * oopsen laying around.
		 */
		if (unregister_framebuffer(info))
			printk(KERN_WARNING
			       "neofb: danger danger!  Oopsen imminent!\n");

		neo_unmap_video(info);
		neo_unmap_mmio(info);
		neo_free_fb_info(info);

		/*
		 * Ensure that the driver data is no longer
		 * valid.
		 */
		pci_set_drvdata(dev, NULL);
	}
}

static struct pci_device_id neofb_devices[] __devinitdata = {
	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2070,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2070},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2090,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2090},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2093,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2093},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2097,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2097},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2160,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2160},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2200,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2200},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2230,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2230},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2360,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2360},

	{PCI_VENDOR_ID_NEOMAGIC, PCI_CHIP_NM2380,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, FB_ACCEL_NEOMAGIC_NM2380},

	{0, 0, 0, 0, 0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, neofb_devices);

static struct pci_driver neofb_driver = {
	.name =		"neofb",
	.id_table =	neofb_devices,
	.probe =	neofb_probe,
	.remove =	__devexit_p(neofb_remove)
};

/* **************************** init-time only **************************** */

static void __init neo_init(void)
{
	DBG("neo_init");
	pci_register_driver(&neofb_driver);
}

/* **************************** exit-time only **************************** */

static void __exit neo_done(void)
{
	DBG("neo_done");
	pci_unregister_driver(&neofb_driver);
}

#ifndef MODULE

/* ************************* init in-kernel code ************************** */

int __init neofb_setup(char *options)
{
	char *this_opt;

	DBG("neofb_setup");

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "disabled", 8))
			disabled = 1;
		if (!strncmp(this_opt, "internal", 8))
			internal = 1;
		if (!strncmp(this_opt, "external", 8))
			external = 1;
		if (!strncmp(this_opt, "nostretch", 9))
			nostretch = 1;
		if (!strncmp(this_opt, "nopciburst", 10))
			nopciburst = 1;
		if (!strncmp(this_opt, "libretto", 8))
			libretto = 1;
	}

	return 0;
}

static int __initdata initialized = 0;

int __init neofb_init(void)
{
	DBG("neofb_init");

	if (disabled)
		return -ENXIO;

	if (!initialized) {
		initialized = 1;
		neo_init();
	}

	/* never return failure, user can hotplug card later... */
	return 0;
}

#else

/* *************************** init module code **************************** */

int __init init_module(void)
{
	DBG("init_module");

	if (disabled)
		return -ENXIO;

	neo_init();

	/* never return failure; user can hotplug card later... */
	return 0;
}

#endif				/* MODULE */

module_exit(neo_done);
