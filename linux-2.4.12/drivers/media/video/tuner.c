#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>

#include "tuner.h"
#include "audiochip.h"

/* Addresses to scan */
static unsigned short normal_i2c[] = {I2C_CLIENT_END};
static unsigned short normal_i2c_range[] = {0x60,0x6f,I2C_CLIENT_END};
static unsigned short probe[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short probe_range[2]  = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore[2]       = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short ignore_range[2] = { I2C_CLIENT_END, I2C_CLIENT_END };
static unsigned short force[2]        = { I2C_CLIENT_END, I2C_CLIENT_END };
static struct i2c_client_address_data addr_data = {
	normal_i2c, normal_i2c_range, 
	probe, probe_range, 
	ignore, ignore_range, 
	force
};

static int debug =  0; /* insmod parameter */
static int type  = -1; /* insmod parameter */

static int addr  =  0;
static char *pal =  "b";
static int this_adap;
static int tv_range[2]    = { 44, 958 };
static int radio_range[2] = { 65, 108 };

#define dprintk     if (debug) printk

MODULE_PARM(debug,"i");
MODULE_PARM(type,"i");
MODULE_PARM(addr,"i");
MODULE_PARM(tv_range,"2i");
MODULE_PARM(radio_range,"2i");
MODULE_PARM(pal,"s");
MODULE_LICENSE("GPL");

struct tuner
{
	int type;            /* chip type */
	int freq;            /* keep track of the current settings */
	int std;

	int radio;
	int mode;            /* PAL(0)/SECAM(1) mode (PHILIPS_SECAM only) */
};

static struct i2c_driver driver;
static struct i2c_client client_template;

/* tv standard selection for Temic 4046 FM5
   this value takes the low bits of control byte 2
   from datasheet Rev.01, Feb.00 
     standard     BG      I       L       L2      D
     picture IF   38.9    38.9    38.9    33.95   38.9
     sound 1      33.4    32.9    32.4    40.45   32.4
     sound 2      33.16   
     NICAM        33.05   32.348  33.05           33.05
 */
#define TEMIC_SET_PAL_I         0x05
#define TEMIC_SET_PAL_DK        0x09
#define TEMIC_SET_PAL_L         0x0a // SECAM ?
#define TEMIC_SET_PAL_L2        0x0b // change IF !
#define TEMIC_SET_PAL_BG        0x0c

/* tv tuner system standard selection for Philips FQ1216ME
   this value takes the low bits of control byte 2
   from datasheet "1999 Nov 16" (supersedes "1999 Mar 23")
     standard 		BG	DK	I	L	L`
     picture carrier	38.90	38.90	38.90	38.90	33.95
     colour		34.47	34.47	34.47	34.47	38.38
     sound 1		33.40	32.40	32.90	32.40	40.45
     sound 2		33.16	-	-	-	-
     NICAM		33.05	33.05	32.35	33.05	39.80
 */
#define PHILIPS_SET_PAL_I	0x01 /* Bit 2 always zero !*/
#define PHILIPS_SET_PAL_BGDK	0x09
#define PHILIPS_SET_PAL_L2	0x0a
#define PHILIPS_SET_PAL_L	0x0b	

/* system switching for Philips FI1216MF MK2
   from datasheet "1996 Jul 09",
 */
#define PHILIPS_MF_SET_BG	0x01 /* Bit 2 must be zero, Bit 3 is system output */
#define PHILIPS_MF_SET_PAL_L	0x03
#define PHILIPS_MF_SET_PAL_L2	0x02


/* ---------------------------------------------------------------------- */

struct tunertype 
{
	char *name;
	unsigned char Vendor;
	unsigned char Type;
  
	unsigned short thresh1;  /*  band switch VHF_LO <=> VHF_HI  */
	unsigned short thresh2;  /*  band switch VHF_HI <=> UHF     */
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config; 
	unsigned short IFPCoff; /* 622.4=16*38.90 MHz PAL, 732=16*45.75 NTSC */
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
        { "Temic PAL (4002 FH5)", TEMIC, PAL,
	  16*140.25,16*463.25,0x02,0x04,0x01,0x8e,623},
	{ "Philips PAL_I", Philips, PAL_I,
	  16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
	{ "Philips NTSC", Philips, NTSC,
	  16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,732},
	{ "Philips SECAM", Philips, SECAM,
	  16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,623},

	{ "NoTuner", NoTuner, NOTUNER,
	  0,0,0x00,0x00,0x00,0x00,0x00},
	{ "Philips PAL", Philips, PAL,
	  16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,623},
	{ "Temic NTSC (4032 FY5)", TEMIC, NTSC,
	  16*157.25,16*463.25,0x02,0x04,0x01,0x8e,732},
	{ "Temic PAL_I (4062 FY5)", TEMIC, PAL_I,
	  16*170.00,16*450.00,0x02,0x04,0x01,0x8e,623},

 	{ "Temic NTSC (4036 FY5)", TEMIC, NTSC,
	  16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,732},
        { "Alps HSBH1", TEMIC, NTSC,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
        { "Alps TSBE1",TEMIC,PAL,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
        { "Alps TSBB5", Alps, PAL_I, /* tested (UK UHF) with Modtec MM205 */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,632},

        { "Alps TSBE5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,622},
        { "Alps TSBC5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,608},
	{ "Temic PAL_I (4006FH5)", TEMIC, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623}, 
  	{ "Alps TSCH6",Alps,NTSC,
  	  16*137.25,16*385.25,0x14,0x12,0x11,0x8e,732},

  	{ "Temic PAL_DK (4016 FY5)",TEMIC,PAL,
  	  16*136.25,16*456.25,0xa0,0x90,0x30,0x8e,623},
  	{ "Philips NTSC_M (MK2)",Philips,NTSC,
  	  16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732},
        { "Temic PAL_I (4066 FY5)", TEMIC, PAL_I,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
        { "Temic PAL* auto (4006 FN5)", TEMIC, PAL,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},

        { "Temic PAL (4009 FR5)", TEMIC, PAL,
          16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
        { "Temic NTSC (4039 FR5)", TEMIC, NTSC,
          16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
        { "Temic PAL/SECAM multi (4046 FM5)", TEMIC, PAL,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
        { "Philips PAL_DK", Philips, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},

	{ "Philips PAL/SECAM multi (FQ1216ME)", Philips, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_I+FM (TAPC-I001D)", LGINNOTEK, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_I (TAPC-I701D)", LGINNOTEK, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG NTSC+FM (TPI8NSR01F)", LGINNOTEK, NTSC,
	  16*210.00,16*497.00,0xa0,0x90,0x30,0x8e,732},

	{ "LG PAL_BG+FM (TPI8PSB01D)", LGINNOTEK, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_BG (TPI8PSB11D)", LGINNOTEK, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "Temic PAL* auto + FM (4009 FN5)", TEMIC, PAL,
	  16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623}
};
#define TUNERS (sizeof(tuners)/sizeof(struct tunertype))

/* ---------------------------------------------------------------------- */

static int tuner_getstatus(struct i2c_client *c)
{
	unsigned char byte;

	if (1 != i2c_master_recv(c,&byte,1))
		return 0;
	return byte;
}

#define TUNER_POR       0x80
#define TUNER_FL        0x40
#define TUNER_MODE      0x38
#define TUNER_AFC       0x07

#define TUNER_STEREO    0x10 /* radio mode */
#define TUNER_SIGNAL    0x07 /* radio mode */

static int tuner_signal(struct i2c_client *c)
{
	return (tuner_getstatus(c) & TUNER_SIGNAL)<<13;
}

static int tuner_stereo(struct i2c_client *c)
{
	return (tuner_getstatus (c) & TUNER_STEREO);
}


static int tuner_islocked (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_FL);
}

static int tuner_afcstatus (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_AFC) - 2;
}

#if 0 /* unused */
static int tuner_mode (struct i2c_client *c)
{
        return (tuner_getstatus (c) & TUNER_MODE) >> 3;
}
#endif

// Set tuner frequency,  freq in Units of 62.5kHz = 1/16MHz
static void set_tv_freq(struct i2c_client *c, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
	struct tuner *t = c->data;
        unsigned char buffer[4];
	int rc;

	if (freq < tv_range[0]*16 || freq > tv_range[1]*16) {
		/* FIXME: better do that chip-specific, but
		   right now we don't have that in the config
		   struct and this way is still better than no
		   check at all */
		printk("tuner: TV freq (%d.%02d) out of range (%d-%d)\n",
		       freq/16,freq%16*100/16,tv_range[0],tv_range[1]);
	}

	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	if (freq < tun->thresh1) 
		config = tun->VHF_L;
	else if (freq < tun->thresh2) 
		config = tun->VHF_H;
	else
		config = tun->UHF;


	/* tv norm specific stuff for multi-norm tuners */
	switch (t->type) {
	case TUNER_PHILIPS_SECAM:
		/* 0x01 -> ??? no change ??? */
		/* 0x02 -> PAL BDGHI / SECAM L */
		/* 0x04 -> ??? PAL others / SECAM others ??? */
		config &= ~0x02;
		if (t->mode)
			config |= 0x02;
		break;

	case TUNER_TEMIC_4046FM5:
		config &= ~0x0f;
		switch (pal[0]) {
		case 'i':
		case 'I':
			config |= TEMIC_SET_PAL_I;
			break;
		case 'd':
		case 'D':
			config |= TEMIC_SET_PAL_DK;
			break;
		case 'l':
		case 'L':
			config |= TEMIC_SET_PAL_L;
			break;
		case 'b':
		case 'B':
		case 'g':
		case 'G':
		default:
			config |= TEMIC_SET_PAL_BG;
			break;
		break;
		}
	case TUNER_PHILIPS_FQ1216ME:
		config &= ~0x0f;
		switch (pal[0]) {
		case 'i':
		case 'I':
			config |= PHILIPS_SET_PAL_I;
			break;
		case 'l':
		case 'L':
			config |= PHILIPS_SET_PAL_L;
			break;
		case 'd':
		case 'D':
		case 'b':
		case 'B':
		case 'g':
		case 'G':
			config |= PHILIPS_SET_PAL_BGDK;
			break;
		break;
		}
	}

	
	/*
	 * Philips FI1216MK2 remark from specification :
	 * for channel selection involving band switching, and to ensure
	 * smooth tuning to the desired channel without causing
	 * unnecessary charge pump action, it is recommended to consider
	 * the difference between wanted channel frequency and the
	 * current channel frequency.  Unnecessary charge pump action
	 * will result in very low tuning voltage which may drive the
	 * oscillator to extreme conditions.
	 *
	 * Progfou: specification says to send config data before
	 * frequency in case (wanted frequency < current frequency).
	 */

	div=freq + tun->IFPCoff;
	if (t->type == TUNER_PHILIPS_SECAM && freq < t->freq) {
		buffer[0] = tun->config;
		buffer[1] = config;
		buffer[2] = (div>>8) & 0x7f;
		buffer[3] = div      & 0xff;
	} else {
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = tun->config;
		buffer[3] = config;
	}

        if (4 != (rc = i2c_master_send(c,buffer,4)))
                printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);

}

static void set_radio_freq(struct i2c_client *c, int freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun;
	struct tuner *t = (struct tuner*)c->data;
        unsigned char buffer[4];
	int rc;

	if (freq < radio_range[0]*16 || freq > radio_range[1]*16) {
		printk("tuner: radio freq (%d.%02d) out of range (%d-%d)\n",
		       freq/16,freq%16*100/16,
		       radio_range[0],radio_range[1]);
		return;
	}
	if (t->type == -1) {
		printk("tuner: tuner type not set\n");
		return;
	}

	tun=&tuners[t->type];
	config = 0xa4 /* 0xa5 */; /* bit 0 is AFC (set) vs. RF-Signal (clear) */
	div=freq + (int)(16*10.7);
  	div&=0x7fff;

        buffer[0] = (div>>8) & 0x7f;
        buffer[1] = div      & 0xff;
        buffer[2] = tun->config;
        buffer[3] = config;
        if (4 != (rc = i2c_master_send(c,buffer,4)))
                printk("tuner: i2c i/o error: rc == %d (should be 4)\n",rc);

	if (debug) {
		current->state   = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/10);
		
		if (tuner_islocked (c))
			printk ("tuner: PLL locked\n");
		else
			printk ("tuner: PLL not locked\n");

		if (config & 1) {
			printk ("tuner: AFC: %d\n", tuner_afcstatus(c));
		} else {
			printk ("tuner: Signal: %d\n", tuner_signal(c));
		}
	}
}
/* ---------------------------------------------------------------------- */


static int tuner_attach(struct i2c_adapter *adap, int addr,
			unsigned short flags, int kind)
{
	struct tuner *t;
	struct i2c_client *client;

	if (this_adap > 0)
		return -1;
	this_adap++;
	
        client_template.adapter = adap;
        client_template.addr = addr;

        printk("tuner: chip found @ 0x%x\n", addr<<1);

        if (NULL == (client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL)))
                return -ENOMEM;
        memcpy(client,&client_template,sizeof(struct i2c_client));
        client->data = t = kmalloc(sizeof(struct tuner),GFP_KERNEL);
        if (NULL == t) {
                kfree(client);
                return -ENOMEM;
        }
        memset(t,0,sizeof(struct tuner));
	if (type >= 0 && type < TUNERS) {
		t->type = type;
		strncpy(client->name, tuners[t->type].name, sizeof(client->name));
	} else {
		t->type = -1;
	}
        i2c_attach_client(client);
	MOD_INC_USE_COUNT;

	return 0;
}

static int tuner_probe(struct i2c_adapter *adap)
{
	if (0 != addr) {
		normal_i2c_range[0] = addr;
		normal_i2c_range[1] = addr;
	}
	this_adap = 0;
	if (adap->id == (I2C_ALGO_BIT | I2C_HW_B_BT848))
		return i2c_probe(adap, &addr_data, tuner_attach);
	return 0;
}

static int tuner_detach(struct i2c_client *client)
{
	struct tuner *t = (struct tuner*)client->data;

	i2c_detach_client(client);
	kfree(t);
	kfree(client);
	MOD_DEC_USE_COUNT;
	return 0;
}

static int
tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tuner *t = (struct tuner*)client->data;
        int   *iarg = (int*)arg;
#if 0
        __u16 *sarg = (__u16*)arg;
#endif

        switch (cmd) {

	/* --- configuration --- */
	case TUNER_SET_TYPE:
		if (t->type != -1)
			return 0;
		if (*iarg < 0 || *iarg >= TUNERS)
			return 0;
		t->type = *iarg;
		dprintk("tuner: type set to %d (%s)\n",
                        t->type,tuners[t->type].name);
		strncpy(client->name, tuners[t->type].name, sizeof(client->name));
		break;
	case AUDC_SET_RADIO:
		t->radio = 1;
		break;
		
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;
		
		t->radio = 0;
		if (t->type == TUNER_PHILIPS_SECAM) {
			t->mode = (vc->norm == VIDEO_MODE_SECAM) ? 1 : 0;
			set_tv_freq(client,t->freq);
		}
		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long *v = arg;

		if (t->radio) {
			dprintk("tuner: radio freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_radio_freq(client,*v);
		} else {
			dprintk("tuner: tv freq set to %d.%02d\n",
				(*iarg)/16,(*iarg)%16*100/16);
			set_tv_freq(client,*v);
		}
		t->freq = *v;
		return 0;
	}
	case VIDIOCGTUNER:
	{
		struct video_tuner *vt = arg;

		if (t->radio)
			vt->signal = tuner_signal(client);
		return 0;
	}
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;
		if (t->radio)
			va->mode = (tuner_stereo(client) ? VIDEO_SOUND_STEREO : VIDEO_SOUND_MONO);
		return 0;
	}
	
#if 0
	/* --- old, obsolete interface --- */
	case TUNER_SET_TVFREQ:
		dprintk("tuner: tv freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_tv_freq(client,*iarg);
		t->radio = 0;
		t->freq = *iarg;
		break;

	case TUNER_SET_RADIOFREQ:
		dprintk("tuner: radio freq set to %d.%02d\n",
			(*iarg)/16,(*iarg)%16*100/16);
		set_radio_freq(client,*iarg);
		t->radio = 1;
		t->freq = *iarg;
		break;
	case TUNER_SET_MODE:
		if (t->type != TUNER_PHILIPS_SECAM) {
			dprintk("tuner: trying to change mode for other than TUNER_PHILIPS_SECAM\n");
		} else {
			int mode=(*sarg==VIDEO_MODE_SECAM)?1:0;
			dprintk("tuner: mode set to %d\n", *sarg);
			t->mode = mode;
			set_tv_freq(client,t->freq);
		}
		break;
#endif
	default:
		/* nothing */
		break;
	}
	
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
        "i2c TV tuner driver",
        I2C_DRIVERID_TUNER,
        I2C_DF_NOTIFY,
        tuner_probe,
        tuner_detach,
        tuner_command,
};

static struct i2c_client client_template =
{
        "(unset)",		/* name       */
        -1,
        0,
        0,
        NULL,
        &driver
};

EXPORT_NO_SYMBOLS;

int tuner_init_module(void)
{
	i2c_add_driver(&driver);
	return 0;
}

void tuner_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(tuner_init_module);
module_exit(tuner_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
