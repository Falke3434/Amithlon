/*
 *  RTC based high-frequency timer
 *
 *  Copyright (C) 2000 Takashi Iwai
 *	based on rtctimer.c by Steve Ratcliffe
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
#include <linux/time.h>
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <sound/core.h>
#include <sound/timer.h>
#include <sound/info.h>

#if defined(CONFIG_RTC) || defined(CONFIG_RTC_MODULE)

#include <linux/mc146818rtc.h>

#define RTC_FREQ	1024		/* default frequency */
#define NANO_SEC	1000000000L	/* 10^9 in sec */

/*
 * prototypes
 */
static int rtctimer_open(snd_timer_t *t);
static int rtctimer_close(snd_timer_t *t);
static int rtctimer_start(snd_timer_t *t);
static int rtctimer_stop(snd_timer_t *t);


/*
 * The hardware dependent description for this timer.
 */
static struct _snd_timer_hardware rtc_hw = {
	.flags =	SNDRV_TIMER_HW_FIRST|SNDRV_TIMER_HW_AUTO,
	.ticks =	100000000L,		/* FIXME: XXX */
	.open =		rtctimer_open,
	.close =	rtctimer_close,
	.start =	rtctimer_start,
	.stop =		rtctimer_stop,
};

int rtctimer_freq = RTC_FREQ;		/* frequency */
static snd_timer_t *rtctimer;
static atomic_t rtc_inc = ATOMIC_INIT(0);
static rtc_task_t rtc_task;


static int
rtctimer_open(snd_timer_t *t)
{
	int err;

	err = rtc_register(&rtc_task);
	if (err < 0)
		return err;
	t->private_data = &rtc_task;
	return 0;
}

static int
rtctimer_close(snd_timer_t *t)
{
	rtc_task_t *rtc = t->private_data;
	if (rtc) {
		rtc_unregister(rtc);
		t->private_data = NULL;
	}
	return 0;
}

static int
rtctimer_start(snd_timer_t *timer)
{
	rtc_task_t *rtc = timer->private_data;
	snd_assert(rtc != NULL, return -EINVAL);
	rtc_control(rtc, RTC_IRQP_SET, rtctimer_freq);
	rtc_control(rtc, RTC_PIE_ON, 0);
	atomic_set(&rtc_inc, 0);
	return 0;
}

static int
rtctimer_stop(snd_timer_t *timer)
{
	rtc_task_t *rtc = timer->private_data;
	snd_assert(rtc != NULL, return -EINVAL);
	rtc_control(rtc, RTC_PIE_OFF, 0);
	return 0;
}

/*
 * interrupt
 */
static void rtctimer_interrupt(void *private_data)
{
	int ticks;

	atomic_inc(&rtc_inc);
	ticks = atomic_read(&rtc_inc);
	snd_timer_interrupt((snd_timer_t*)private_data, ticks);
	atomic_sub(ticks, &rtc_inc);
}


/*
 *  ENTRY functions
 */
static int __init rtctimer_init(void)
{
	int order, err;
	snd_timer_t *timer;

	if (rtctimer_freq < 2 || rtctimer_freq > 8192) {
		snd_printk(KERN_ERR "rtctimer: invalid frequency %d\n", rtctimer_freq);
		return -EINVAL;
	}
	for (order = 1; rtctimer_freq > order; order <<= 1)
		;
	if (rtctimer_freq != order) {
		snd_printk(KERN_ERR "rtctimer: invalid frequency %d\n", rtctimer_freq);
		return -EINVAL;
	}

	/* Create a new timer and set up the fields */
	err = snd_timer_global_new("rtc", SNDRV_TIMER_GLOBAL_RTC, &timer);
	if (err < 0)
		return err;

	strcpy(timer->name, "RTC timer");
	timer->hw = rtc_hw;
	timer->hw.resolution = NANO_SEC / rtctimer_freq;

	/* set up RTC callback */
	rtc_task.func = rtctimer_interrupt;
	rtc_task.private_data = timer;

	err = snd_timer_global_register(timer);
	if (err < 0) {
		snd_timer_global_free(timer);
		return err;
	}
	rtctimer = timer; /* remember this */

	return 0;
}

static void __exit rtctimer_exit(void)
{
	if (rtctimer) {
		snd_timer_global_unregister(rtctimer);
		rtctimer = NULL;
	}
}


/*
 * exported stuff
 */
module_init(rtctimer_init)
module_exit(rtctimer_exit)

MODULE_PARM(rtctimer_freq, "i");
MODULE_PARM_DESC(rtctimer_freq, "timer frequency in Hz");

MODULE_LICENSE("GPL");

#endif /* CONFIG_RTC || CONFIG_RTC_MODULE */
