/*
 *  Generic driver for AD1848/AD1847/CS4248 chips (0.1 Alpha)
 *  Copyright (c) by Tugrul Galatali <galatalt@stuy.edu>,
 *                   Jaroslav Kysela <perex@suse.cz>
 *  Based on card-4232.c by Jaroslav Kysela <perex@suse.cz>
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
#include <linux/time.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/ad1848.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

#define chip_t ad1848_t

MODULE_AUTHOR("Tugrul Galatali <galatalt@stuy.edu>, Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("AD1848/AD1847/CS4248");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Analog Devices,AD1848},"
	        "{Analog Devices,AD1847},"
		"{Crystal Semiconductors,CS4248}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,11,12,15 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3,5,6,7 */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for AD1848 soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for AD1848 soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable AD1848 soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for AD1848 driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for AD1848 driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma1, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma1, "DMA1 # for AD1848 driver.");
MODULE_PARM_SYNTAX(dma1, SNDRV_DMA_DESC);

static snd_card_t *snd_ad1848_cards[SNDRV_CARDS] = SNDRV_DEFAULT_PTR;


static int __init snd_card_ad1848_probe(int dev)
{
	snd_card_t *card;
	ad1848_t *chip;
	snd_pcm_t *pcm;
	int err;

	if (port[dev] == SNDRV_AUTO_PORT) {
		snd_printk("specify port\n");
		return -EINVAL;
	}
	if (irq[dev] == SNDRV_AUTO_IRQ) {
		snd_printk("specify irq\n");
		return -EINVAL;
	}
	if (dma1[dev] == SNDRV_AUTO_DMA) {
		snd_printk("specify dma1\n");
		return -EINVAL;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	if ((err = snd_ad1848_create(card, port[dev],
				     irq[dev],
				     dma1[dev],
				     AD1848_HW_DETECT,
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	if ((err = snd_ad1848_pcm(chip, 0, &pcm)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_ad1848_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	strcpy(card->driver, "AD1848");
	strcpy(card->shortname, pcm->name);

	sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
		pcm->name, chip->port, irq[dev], dma1[dev]);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	snd_ad1848_cards[dev] = card;
	return 0;
}

static int __init alsa_card_ad1848_init(void)
{
	int dev, cards;

	for (dev = cards = 0; dev < SNDRV_CARDS && enable[dev]; dev++)
		if (snd_card_ad1848_probe(dev) >= 0)
			cards++;

	if (!cards) {
#ifdef MODULE
		printk(KERN_ERR "AD1848 soundcard not found or device busy\n");
#endif
		return -ENODEV;
	}
	return 0;
}

static void __exit alsa_card_ad1848_exit(void)
{
	int idx;

	for (idx = 0; idx < SNDRV_CARDS; idx++)
		snd_card_free(snd_ad1848_cards[idx]);
}

module_init(alsa_card_ad1848_init)
module_exit(alsa_card_ad1848_exit)

#ifndef MODULE

/* format is: snd-ad1848=enable,index,id,port,
			 irq,dma1 */

static int __init alsa_card_ad1848_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&dma1[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-ad1848=", alsa_card_ad1848_setup);

#endif /* ifndef MODULE */
