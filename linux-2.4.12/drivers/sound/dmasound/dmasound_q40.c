
/*
 *  linux/drivers/sound/dmasound/dmasound_q40.c
 *
 *  Q40 DMA Sound Driver
 *
 *  See linux/drivers/sound/dmasound/dmasound_core.c for copyright and credits
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/soundcard.h>

#include <asm/uaccess.h>
#include <asm/q40_master.h>

#include "dmasound.h"


static int expand_bal;	/* Balance factor for expanding (not volume!) */
static int expand_data;	/* Data for expanding */


/*** Low level stuff *********************************************************/


static void Q40Open(void);
static void Q40Release(void);
static void *Q40Alloc(unsigned int size, int flags);
static void Q40Free(void *, unsigned int);
static int Q40IrqInit(void);
#ifdef MODULE
static void Q40IrqCleanUp(void);
#endif
static void Q40Silence(void);
static void Q40Init(void);
static int Q40SetFormat(int format);
static int Q40SetVolume(int volume);
static void Q40PlayNextFrame(int index);
static void Q40Play(void);
static void Q40StereoInterrupt(int irq, void *dummy, struct pt_regs *fp);
static void Q40MonoInterrupt(int irq, void *dummy, struct pt_regs *fp);
static void Q40Interrupt(void);


/*** Mid level stuff *********************************************************/


#if 1
/* userCount, frameUsed, frameLeft == byte counts */
static ssize_t q40_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	char *table = dmasound.soft.format == AFMT_MU_LAW ? dmasound_ulaw2dma8: dmasound_alaw2dma8;
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];

	used = count = min_t(size_t, userCount, frameLeft);
	if (copy_from_user(p,userPtr,count))
	  return -EFAULT;
	while (count > 0) {
		*p = table[*p]+128;
		p++;
		count--;
	}
	*frameUsed += used ;
	return used;
}
#else
static ssize_t q40_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	char *table = dmasound.soft.format == AFMT_MU_LAW ? dmasound_ulaw2dma8: dmasound_alaw2dma8;
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];
	u_char val;
	int stereo = sound.soft.stereo;


	frameLeft >>= 1;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(size_t, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = table[data]+128;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = table[data]+128;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 2;
	return stereo? used * 2: used;
}
#endif

#if 1
static ssize_t q40_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];

	used = count = min_t(size_t, userCount, frameLeft);
	if (copy_from_user(p,userPtr,count))
	  return -EFAULT;
	while (count > 0) {
		*p = *p + 128;
		p++;
		count--;
	}
	*frameUsed += used;
	return used;
}
#else
static ssize_t q40_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];
	u_char val;
	int stereo = dmasound.soft.stereo;

	frameLeft >>= 1;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(size_t, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = data + 128;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = data + 128;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 2;
	return stereo? used * 2: used;
}
#endif

#if 1
static ssize_t q40_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];

	used = count = min_t(size_t, userCount, frameLeft);
	if (copy_from_user(p,userPtr,count))
	  return -EFAULT;
	*frameUsed += used;
	return used;
}
#else
static ssize_t q40_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	u_char *p = (u_char *) &frame[*frameUsed];
	u_char val;
	int stereo = dmasound.soft.stereo;


	frameLeft >>= 1;
	if (stereo)
		userCount >>= 1;
	used = count = min_t(size_t, userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = data;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = data;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 2;
	return stereo? used * 2: used;
}
#endif

/* a bit too complicated to optimise right now ..*/
static ssize_t q40_ctx_law(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned char *table = (unsigned char *)
		(dmasound.soft.format == AFMT_MU_LAW ? dmasound_ulaw2dma8: dmasound_alaw2dma8);
	unsigned int data = expand_data;
	u_char *p = (u_char *) &frame[*frameUsed];
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;
 
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = table[c];
			data += 0x80;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft);
	utotal -= userCount;
	return utotal;
}


static ssize_t q40_ctx_s8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	u_char *p = (u_char *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;


	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = c ;
			data += 0x80;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft);
	utotal -= userCount;
	return utotal;
}


static ssize_t q40_ctx_u8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	u_char *p = (u_char *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = dmasound.hard.speed, sSpeed = dmasound.soft.speed;
	int utotal, ftotal;

	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = c ;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) ;
	utotal -= userCount;
	return utotal;
}


static TRANS transQ40Normal = {
	q40_ct_law, q40_ct_law, q40_ct_s8, q40_ct_u8, NULL, NULL, NULL, NULL
};

static TRANS transQ40Expanding = {
	q40_ctx_law, q40_ctx_law, q40_ctx_s8, q40_ctx_u8, NULL, NULL, NULL, NULL
};


/*** Low level stuff *********************************************************/


static void Q40Open(void)
{
	MOD_INC_USE_COUNT;
}

static void Q40Release(void)
{
	MOD_DEC_USE_COUNT;
}


static void *Q40Alloc(unsigned int size, int flags)
{
         return kmalloc(size, flags); /* change to vmalloc */
}

static void Q40Free(void *ptr, unsigned int size)
{
	kfree(ptr);
}

static int __init Q40IrqInit(void)
{
	/* Register interrupt handler. */
	request_irq(Q40_IRQ_SAMPLE, Q40StereoInterrupt, 0,
		    "DMA sound", Q40Interrupt);

	return(1);
}


#ifdef MODULE
static void Q40IrqCleanUp(void)
{
        master_outb(0,SAMPLE_ENABLE_REG);
	free_irq(Q40_IRQ_SAMPLE, Q40Interrupt);
}
#endif /* MODULE */


static void Q40Silence(void)
{
        master_outb(0,SAMPLE_ENABLE_REG);
	*DAC_LEFT=*DAC_RIGHT=0;
}

static char *q40_pp=NULL;
static unsigned int q40_sc=0;

static void Q40PlayNextFrame(int index)
{
	u_char *start;
	u_long size;
	u_char speed;

	/* used by Q40Play() if all doubts whether there really is something
	 * to be played are already wiped out.
	 */
	start = write_sq.buffers[write_sq.front];
	size = (write_sq.count == index ? write_sq.rear_size : write_sq.block_size);

	q40_pp=start;
	q40_sc=size;
		
	write_sq.front = (write_sq.front+1) % write_sq.max_count;
	write_sq.active++;

	speed=(dmasound.hard.speed==10000 ? 0 : 1);

	master_outb( 0,SAMPLE_ENABLE_REG);
	free_irq(Q40_IRQ_SAMPLE, Q40Interrupt);
	if (dmasound.soft.stereo)
	  	request_irq(Q40_IRQ_SAMPLE, Q40StereoInterrupt, 0,
		    "Q40 sound", Q40Interrupt);
	  else
	        request_irq(Q40_IRQ_SAMPLE, Q40MonoInterrupt, 0,
		    "Q40 sound", Q40Interrupt);

	master_outb( speed, SAMPLE_RATE_REG);
	master_outb( 1,SAMPLE_CLEAR_REG);
	master_outb( 1,SAMPLE_ENABLE_REG);
}

static void Q40Play(void)
{
        unsigned long flags;

	if (write_sq.active || write_sq.count<=0 ) {
		/* There's already a frame loaded */
		return;
	}

	/* nothing in the queue */
	if (write_sq.count <= 1 && write_sq.rear_size < write_sq.block_size && !write_sq.syncing) {
	         /* hmmm, the only existing frame is not
		  * yet filled and we're not syncing?
		  */
	         return;
	}
	save_flags(flags); cli();
	Q40PlayNextFrame(1);
	restore_flags(flags);
}

static void Q40StereoInterrupt(int irq, void *dummy, struct pt_regs *fp)
{
        if (q40_sc>1){
            *DAC_LEFT=*q40_pp++;
	    *DAC_RIGHT=*q40_pp++;
	    q40_sc -=2;
	    master_outb(1,SAMPLE_CLEAR_REG);
	}else Q40Interrupt();
}
static void Q40MonoInterrupt(int irq, void *dummy, struct pt_regs *fp)
{
        if (q40_sc>0){
            *DAC_LEFT=*q40_pp;
	    *DAC_RIGHT=*q40_pp++;
	    q40_sc --;
	    master_outb(1,SAMPLE_CLEAR_REG);	    
	}else Q40Interrupt();
}
static void Q40Interrupt(void)
{
	if (!write_sq.active) {
	          /* playing was interrupted and sq_reset() has already cleared
		   * the sq variables, so better don't do anything here.
		   */
	           WAKE_UP(write_sq.sync_queue);
		   master_outb(0,SAMPLE_ENABLE_REG); /* better safe */
		   goto exit;
	} else write_sq.active=0;
	write_sq.count--;
	Q40Play();

	if (q40_sc<2)
	      { /* there was nothing to play, disable irq */
		master_outb(0,SAMPLE_ENABLE_REG);
		*DAC_LEFT=*DAC_RIGHT=0;
	      }
	WAKE_UP(write_sq.action_queue);

 exit:
	master_outb(1,SAMPLE_CLEAR_REG);
}


static void Q40Init(void)
{
	int i, idx;
	const int freq[] = {10000, 20000};

	/* search a frequency that fits into the allowed error range */

	idx = -1;
	for (i = 0; i < 2; i++)
		if ((100 * abs(dmasound.soft.speed - freq[i]) / freq[i]) <= catchRadius)
			idx = i;

	dmasound.hard = dmasound.soft;
	/*sound.hard.stereo=1;*/ /* no longer true */
	dmasound.hard.size=8;

	if (idx > -1) {
		dmasound.soft.speed = freq[idx];
		dmasound.trans_write = &transQ40Normal;
	} else
		dmasound.trans_write = &transQ40Expanding;

	Q40Silence();

	if (dmasound.hard.speed > 20000) {
		/* we would need to squeeze the sound, but we won't do that */
		dmasound.hard.speed = 20000;
		dmasound.trans_write = &transQ40Normal;
	} else if (dmasound.hard.speed > 10000) {
		dmasound.hard.speed = 20000;
	} else {
		dmasound.hard.speed = 10000;
	}
	expand_bal = -dmasound.soft.speed;
}


static int Q40SetFormat(int format)
{
	/* Q40 sound supports only 8bit modes */

	switch (format) {
	case AFMT_QUERY:
		return(dmasound.soft.format);
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_S8:
	case AFMT_U8:
		break;
	default:
		format = AFMT_S8;
	}

	dmasound.soft.format = format;
	dmasound.soft.size = 8;
	if (dmasound.minDev == SND_DEV_DSP) {
		dmasound.dsp.format = format;
		dmasound.dsp.size = 8;
	}
	Q40Init();

	return(format);
}

static int Q40SetVolume(int volume)
{
    return 0;
}


/*** Machine definitions *****************************************************/


static MACHINE machQ40 = {
	name:		"Q40",
	name2:		"Q40",
	open:		Q40Open,
	release:	Q40Release,
	dma_alloc:	Q40Alloc,
	dma_free:	Q40Free,
	irqinit:	Q40IrqInit,
#ifdef MODULE
	irqcleanup:	Q40IrqCleanUp,
#endif /* MODULE */
	init:		Q40Init,
	silence:	Q40Silence, 
	setFormat:	Q40SetFormat, 
	setVolume:	Q40SetVolume,
	play:		Q40Play
};


/*** Config & Setup **********************************************************/


int __init dmasound_q40_init(void)
{
	if (MACH_IS_Q40) {
	    dmasound.mach = machQ40;
	    return dmasound_init();
	} else
	    return -ENODEV;
}

static void __exit dmasound_q40_cleanup(void)
{
	dmasound_deinit();
}

module_init(dmasound_q40_init);
module_exit(dmasound_q40_cleanup);
