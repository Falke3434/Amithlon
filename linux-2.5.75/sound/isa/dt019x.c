
/*
    dt019x.c - driver for Diamond Technologies DT-0197H based soundcards.
    Copyright (C) 1999, 2002 by Massimo Piccioni <dafastidio@libero.it>

    Generalised for soundcards based on DT-0196 and ALS-007 chips 
    by Jonathan Woithe <jwoithe@physics.adelaide.edu.au>: June 2002.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/pnp.h>
#include <sound/core.h>
#define SNDRV_GET_ID
#include <sound/initval.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#include <sound/sb.h>

#define chip_t sb_t

#define PFX "dt019x: "

MODULE_AUTHOR("Massimo Piccioni <dafastidio@libero.it>");
MODULE_DESCRIPTION("Diamond Technologies DT-019X / Avance Logic ALS-007");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Diamond Technologies DT-019X},"
	       "{Avance Logic ALS-007}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE;	/* Enable this card */
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* PnP setup */
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int mpu_irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* PnP setup */
static int dma8[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* PnP setup */

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for DT-019X based soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for DT-019X based soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable DT-019X based soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(port, "Port # for dt019x driver.");
MODULE_PARM_SYNTAX(port, SNDRV_PORT12_DESC);
MODULE_PARM(mpu_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for dt019x driver.");
MODULE_PARM_SYNTAX(mpu_port, SNDRV_PORT12_DESC);
MODULE_PARM(fm_port, "1-" __MODULE_STRING(SNDRV_CARDS) "l");
MODULE_PARM_DESC(fm_port, "FM port # for dt019x driver.");
MODULE_PARM_SYNTAX(fm_port, SNDRV_PORT12_DESC);
MODULE_PARM(irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(irq, "IRQ # for dt019x driver.");
MODULE_PARM_SYNTAX(irq, SNDRV_IRQ_DESC);
MODULE_PARM(mpu_irq, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ # for dt019x driver.");
MODULE_PARM_SYNTAX(mpu_irq, SNDRV_IRQ_DESC);
MODULE_PARM(dma8, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(dma8, "8-bit DMA # for dt019x driver.");
MODULE_PARM_SYNTAX(dma8, SNDRV_DMA8_DESC);

struct snd_card_dt019x {
	struct pnp_dev *dev;
	struct pnp_dev *devmpu;
	struct pnp_dev *devopl;
};

static struct pnp_card_device_id snd_dt019x_pnpids[] __devinitdata = {
	/* DT197A30 */
	{ .id = "RWB1688", .devs = { { "@@@0001" }, { "@X@0001" }, { "@H@0001" }, } },
	/* DT0196 / ALS-007 */
	{ .id = "ALS0007", .devs = { { "@@@0001" }, { "@X@0001" }, { "@H@0001" }, } },
	{ .id = "",  }
};

MODULE_DEVICE_TABLE(pnp_card, snd_dt019x_pnpids);


#define DRIVER_NAME	"snd-card-dt019x"


static int __devinit snd_card_dt019x_pnp(int dev, struct snd_card_dt019x *acard,
					 struct pnp_card_link *card,
					 const struct pnp_card_device_id *pid)
{
	struct pnp_dev *pdev;
	struct pnp_resource_table * cfg = kmalloc(sizeof(struct pnp_resource_table), GFP_KERNEL);
	int err;

	if (!cfg)
		return -ENOMEM;

	acard->dev = pnp_request_card_device(card, pid->devs[0].id, NULL);
	if (acard->dev == NULL) {
		kfree (cfg);
		return -ENODEV;
	}
	acard->devmpu = pnp_request_card_device(card, pid->devs[1].id, NULL);
	acard->devopl = pnp_request_card_device(card, pid->devs[2].id, NULL);

	pdev = acard->dev;
	pnp_init_resource_table(cfg);

	if (port[dev] != SNDRV_AUTO_PORT)
		pnp_resource_change(&cfg->port_resource[0], port[dev], 16);
	if (dma8[dev] != SNDRV_AUTO_DMA)
		pnp_resource_change(&cfg->dma_resource[0], dma8[dev], 1);
	if (irq[dev] != SNDRV_AUTO_IRQ)
		pnp_resource_change(&cfg->irq_resource[0], irq[dev], 1);

	if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
		snd_printk(KERN_ERR PFX "DT-019X AUDIO the requested resources are invalid, using auto config\n");
	err = pnp_activate_dev(pdev);
	if (err < 0) {
		snd_printk(KERN_ERR PFX "DT-019X AUDIO pnp configure failure\n");
		kfree(cfg);
		return err;
	}

	port[dev] = pnp_port_start(pdev, 0);
	dma8[dev] = pnp_dma(pdev, 0);
	irq[dev] = pnp_irq(pdev, 0);
	snd_printdd("dt019x: found audio interface: port=0x%lx, irq=0x%lx, dma=0x%lx\n",
			port[dev],irq[dev],dma8[dev]);

	pdev = acard->devmpu;

	if (pdev != NULL) {
		pnp_init_resource_table(cfg);
		if (mpu_port[dev] != SNDRV_AUTO_PORT)
			pnp_resource_change(&cfg->port_resource[0], mpu_port[dev], 2);
		if (mpu_irq[dev] != SNDRV_AUTO_IRQ)
			pnp_resource_change(&cfg->irq_resource[0], mpu_irq[dev], 1);
		if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
			snd_printk(KERN_ERR PFX "DT-019X MPU401 the requested resources are invalid, using auto config\n");
		err = pnp_activate_dev(pdev);
		if (err < 0) {
			pnp_release_card_device(pdev);
			snd_printk(KERN_ERR PFX "DT-019X MPU401 pnp configure failure, skipping\n");
			goto __mpu_error;
		}
		mpu_port[dev] = pnp_port_start(pdev, 0);
		mpu_irq[dev] = pnp_irq(pdev, 0);
		snd_printdd("dt019x: found MPU-401: port=0x%lx, irq=0x%lx\n",
			 	mpu_port[dev],mpu_irq[dev]);
	} else {
	__mpu_error:
		acard->devmpu = NULL;
		mpu_port[dev] = -1;
	}

	pdev = acard->devopl;
	if (pdev != NULL) {
		pnp_init_resource_table(cfg);
		if (fm_port[dev] != SNDRV_AUTO_PORT)
			pnp_resource_change(&cfg->port_resource[0], fm_port[dev], 4);
		if ((pnp_manual_config_dev(pdev, cfg, 0)) < 0)
			snd_printk(KERN_ERR PFX "DT-019X OPL3 the requested resources are invalid, using auto config\n");
		err = pnp_activate_dev(pdev);
		if (err < 0) {
			pnp_release_card_device(pdev);
			snd_printk(KERN_ERR PFX "DT-019X OPL3 pnp configure failure, skipping\n");
			goto __fm_error;
		}
		fm_port[dev] = pnp_port_start(pdev, 0);
		snd_printdd("dt019x: found OPL3 synth: port=0x%lx\n",fm_port[dev]);
	} else {
	__fm_error:
		acard->devopl = NULL;
		fm_port[dev] = -1;
	}

	kfree(cfg);
	return 0;
}

static int __devinit snd_card_dt019x_probe(int dev, struct pnp_card_link *pcard, const struct pnp_card_device_id *pid)
{
	int error;
	sb_t *chip;
	snd_card_t *card;
	struct snd_card_dt019x *acard;
	opl3_t *opl3;

	if ((card = snd_card_new(index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_card_dt019x))) == NULL)
		return -ENOMEM;
	acard = (struct snd_card_dt019x *)card->private_data;

	if ((error = snd_card_dt019x_pnp(dev, acard, pcard, pid))) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sbdsp_create(card, port[dev],
				      irq[dev],
				      snd_sb16dsp_interrupt,
				      dma8[dev],
				      -1,
				      SB_HW_DT019X,
				      &chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if ((error = snd_sb16dsp_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return error;
	}
	if ((error = snd_sbmixer_new(chip)) < 0) {
		snd_card_free(card);
		return error;
	}

	if (mpu_port[dev] > 0) {
		if (snd_mpu401_uart_new(card, 0,
/*					MPU401_HW_SB,*/
					MPU401_HW_MPU401,
					mpu_port[dev], 0,
					mpu_irq[dev],
					SA_INTERRUPT,
					NULL) < 0)
			snd_printk(KERN_ERR PFX "no MPU-401 device at 0x%lx ?\n", mpu_port[dev]);
	}

	if (fm_port[dev] > 0) {
		if (snd_opl3_create(card,
				    fm_port[dev],
				    fm_port[dev] + 2,
				    OPL3_HW_AUTO, 0, &opl3) < 0) {
			snd_printk(KERN_ERR PFX "no OPL device at 0x%lx-0x%lx ?\n",
				   fm_port[dev], fm_port[dev] + 2);
		} else {
			if ((error = snd_opl3_timer_new(opl3, 0, 1)) < 0) {
				snd_card_free(card);
				return error;
			}
			if ((error = snd_opl3_hwdep_new(opl3, 0, 1, NULL)) < 0) {
				snd_card_free(card);
				return error;
			}
		}
	}

	strcpy(card->driver, "DT-019X");
	strcpy(card->shortname, "Diamond Tech. DT-019X");
	sprintf(card->longname, "%s soundcard, %s at 0x%lx, irq %d, dma %d",
		card->shortname, chip->name, chip->port,
		irq[dev], dma8[dev]);
	if ((error = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return error;
	}
	pnp_set_card_drvdata(pcard, card);
	return 0;
}

static int __devinit snd_dt019x_pnp_probe(struct pnp_card_link *card,
					  const struct pnp_card_device_id *pid)
{
	static int dev;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (!enable[dev])
			continue;
		res = snd_card_dt019x_probe(dev, card, pid);
		if (res < 0)
			return res;
		dev++;
		return 0;
        }
        return -ENODEV;
}

static void __devexit snd_dt019x_pnp_remove(struct pnp_card_link * pcard)
{
	snd_card_t *card = (snd_card_t *) pnp_get_card_drvdata(pcard);
	snd_card_disconnect(card);
	snd_card_free_in_thread(card);
}

static struct pnp_card_driver dt019x_pnpc_driver = {
	.flags          = PNP_DRIVER_RES_DISABLE,
	.name           = "dt019x",
	.id_table       = snd_dt019x_pnpids,
	.probe          = snd_dt019x_pnp_probe,
	.remove         = __devexit_p(snd_dt019x_pnp_remove),
};

static int __init alsa_card_dt019x_init(void)
{
	int cards = 0;

	cards += pnp_register_card_driver(&dt019x_pnpc_driver);

#ifdef MODULE
	if (!cards)
		snd_printk(KERN_ERR "no DT-019X / ALS-007 based soundcards found\n");
#endif
	return cards ? 0 : -ENODEV;
}

static void __exit alsa_card_dt019x_exit(void)
{
	pnp_unregister_card_driver(&dt019x_pnpc_driver);
}

module_init(alsa_card_dt019x_init)
module_exit(alsa_card_dt019x_exit)

#ifndef MODULE

/* format is: snd-dt019x=enable,index,id,
			  port,mpu_port,fm_port,
			  irq,mpu_irq,dma8,dma8_size */

static int __init alsa_card_dt019x_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2 &&
	       get_option(&str,(int *)&port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&mpu_port[nr_dev]) == 2 &&
	       get_option(&str,(int *)&fm_port[nr_dev]) == 2 &&
	       get_option(&str,&irq[nr_dev]) == 2 &&
	       get_option(&str,&mpu_irq[nr_dev]) == 2 &&
	       get_option(&str,&dma8[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-dt019x=", alsa_card_dt019x_setup);

#endif /* ifndef MODULE */
