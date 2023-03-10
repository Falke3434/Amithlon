/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   AK4524 / AK4528 / AK4529 / AK4355 / AK4381 interface
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@suse.cz>
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
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <sound/core.h>
#include "ice1712.h"

static void snd_ice1712_akm4xxx_lock(akm4xxx_t *ak, int chip)
{
	ice1712_t *ice = ak->private_data[0];

	snd_ice1712_save_gpio_status(ice);
}

static void snd_ice1712_akm4xxx_unlock(akm4xxx_t *ak, int chip)
{
	ice1712_t *ice = ak->private_data[0];

	snd_ice1712_restore_gpio_status(ice);
}

/*
 * write AK4xxx register
 */
static void snd_ice1712_akm4xxx_write(akm4xxx_t *ak, int chip,
				      unsigned char addr, unsigned char data)
{
	unsigned int tmp;
	int idx;
	unsigned int addrdata;
	struct snd_ak4xxx_private *priv = (void *)ak->private_value[0];
	ice1712_t *ice = ak->private_data[0];

	snd_assert(chip >= 0 && chip < 4, return);

	tmp = snd_ice1712_gpio_read(ice);
	tmp |= priv->add_flags;
	tmp &= ~priv->mask_flags;
	if (priv->cs_mask == priv->cs_addr) {
		if (priv->cif) {
			tmp |= priv->cs_mask; /* start without chip select */
		}  else {
			tmp &= ~priv->cs_mask; /* chip select low */
			snd_ice1712_gpio_write(ice, tmp);
			udelay(1);
		}
	} else {
		/* doesn't handle cf=1 yet */
		tmp &= ~priv->cs_mask;
		tmp |= priv->cs_addr;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	/* build I2C address + data byte */
	addrdata = (priv->caddr << 6) | 0x20 | (addr & 0x1f);
	addrdata = (addrdata << 8) | data;
	for (idx = 15; idx >= 0; idx--) {
		/* drop clock */
		tmp &= ~priv->clk_mask;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		/* set data */
		if (addrdata & (1 << idx))
			tmp |= priv->data_mask;
		else
			tmp &= ~priv->data_mask;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		/* raise clock */
		tmp |= priv->clk_mask;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	if (priv->cs_mask == priv->cs_addr) {
		if (priv->cif) {
			/* assert a cs pulse to trigger */
			tmp &= ~priv->cs_mask;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(1);
		}
		tmp |= priv->cs_mask; /* chip select high to trigger */
	} else {
		tmp &= ~priv->cs_mask;
		tmp |= priv->cs_none; /* deselect address */
	}
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
}

/*
 * initialize the akm4xxx_t record with the template
 */
int __devinit snd_ice1712_akm4xxx_init(akm4xxx_t *ak, const akm4xxx_t *temp,
				       const struct snd_ak4xxx_private *_priv, ice1712_t *ice)
{
	struct snd_ak4xxx_private *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	*ak = *temp;
	ak->card = ice->card;
	*priv = *_priv;
        ak->private_value[0] = (unsigned long)priv;
	ak->private_data[0] = ice;
	if (ak->ops.lock == NULL)
		ak->ops.lock = snd_ice1712_akm4xxx_lock;
	if (ak->ops.unlock == NULL)
		ak->ops.unlock = snd_ice1712_akm4xxx_unlock;
	if (ak->ops.write == NULL)
		ak->ops.write = snd_ice1712_akm4xxx_write;
	snd_akm4xxx_init(ak);
	return 0;
}

void __devexit snd_ice1712_akm4xxx_free(ice1712_t *ice)
{
	unsigned int akidx;
	if (ice->akm == NULL)
		return;
	for (akidx = 0; akidx < ice->akm_codecs; akidx++) {
		akm4xxx_t *ak = &ice->akm[akidx];
		if (ak->private_value[0])
			kfree((void *)ak->private_value[0]);
	}
	kfree(ice->akm);
}

/*
 * build AK4xxx controls
 */
int __devinit snd_ice1712_akm4xxx_build_controls(ice1712_t *ice)
{
	unsigned int akidx;
	int err;

	for (akidx = 0; akidx < ice->akm_codecs; akidx++) {
		akm4xxx_t *ak = &ice->akm[akidx];
		err = snd_akm4xxx_build_controls(ak);
		if (err < 0)
			return err;
	}
	return 0;
}

EXPORT_SYMBOL(snd_ice1712_akm4xxx_init);
EXPORT_SYMBOL(snd_ice1712_akm4xxx_free);
EXPORT_SYMBOL(snd_ice1712_akm4xxx_build_controls);
