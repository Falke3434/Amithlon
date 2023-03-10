/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

/*
  NOTES:
  - sometimes the sound is metallic and sibilant, unloading and 
    reloading the module may solve this.
*/

#include <sound/driver.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/cs46xx.h>
#define SNDRV_GET_ID
#include <sound/initval.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Cirrus Logic Sound Fusion CS46XX");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{Cirrus Logic,Sound Fusion (CS4280)},"
		"{Cirrus Logic,Sound Fusion (CS4610)},"
		"{Cirrus Logic,Sound Fusion (CS4612)},"
		"{Cirrus Logic,Sound Fusion (CS4615)},"
		"{Cirrus Logic,Sound Fusion (CS4622)},"
		"{Cirrus Logic,Sound Fusion (CS4624)},"
		"{Cirrus Logic,Sound Fusion (CS4630)}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static int external_amp[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int thinkpad[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};
static int mmap_valid[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = 0};

MODULE_PARM(index, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(index, "Index value for the CS46xx soundcard.");
MODULE_PARM_SYNTAX(index, SNDRV_INDEX_DESC);
MODULE_PARM(id, "1-" __MODULE_STRING(SNDRV_CARDS) "s");
MODULE_PARM_DESC(id, "ID string for the CS46xx soundcard.");
MODULE_PARM_SYNTAX(id, SNDRV_ID_DESC);
MODULE_PARM(enable, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(enable, "Enable CS46xx soundcard.");
MODULE_PARM_SYNTAX(enable, SNDRV_ENABLE_DESC);
MODULE_PARM(external_amp, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(external_amp, "Force to enable external amplifer.");
MODULE_PARM_SYNTAX(external_amp, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(thinkpad, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(thinkpad, "Force to enable Thinkpad's CLKRUN control.");
MODULE_PARM_SYNTAX(thinkpad, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);
MODULE_PARM(mmap_valid, "1-" __MODULE_STRING(SNDRV_CARDS) "i");
MODULE_PARM_DESC(mmap_valid, "Support OSS mmap.");
MODULE_PARM_SYNTAX(mmap_valid, SNDRV_ENABLED "," SNDRV_BOOLEAN_FALSE_DESC);

static struct pci_device_id snd_cs46xx_ids[] __devinitdata = {
        { 0x1013, 0x6001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4280 */
        { 0x1013, 0x6003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4612 */
        { 0x1013, 0x6004, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0, },   /* CS4615 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_cs46xx_ids);

static int __devinit snd_card_cs46xx_probe(struct pci_dev *pci,
					   const struct pci_device_id *pci_id)
{
	static int dev;
	snd_card_t *card;
	cs46xx_t *chip;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;
	if ((err = snd_cs46xx_create(card, pci,
				     external_amp[dev], thinkpad[dev],
				     &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	chip->accept_valid = mmap_valid[dev];
	if ((err = snd_cs46xx_pcm(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if ((err = snd_cs46xx_pcm_rear(chip,1, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs46xx_pcm_iec958(chip,2,NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
#endif
	if ((err = snd_cs46xx_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
#ifdef CONFIG_SND_CS46XX_NEW_DSP
	if (chip->nr_ac97_codecs ==2) {
		if ((err = snd_cs46xx_pcm_center_lfe(chip,3,NULL)) < 0) {
			snd_card_free(card);
			return err;
		}
	}
#endif
	if ((err = snd_cs46xx_midi(chip, 0, NULL)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_cs46xx_start_dsp(chip)) < 0) {
		snd_card_free(card);
		return err;
	}


	snd_cs46xx_gameport(chip);

	strcpy(card->driver, "CS46xx");
	strcpy(card->shortname, "Sound Fusion CS46xx");
	sprintf(card->longname, "%s at 0x%lx/0x%lx, irq %i",
		card->shortname,
		chip->ba0_addr,
		chip->ba1_addr,
		chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, chip);
	dev++;
	return 0;
}

#ifdef CONFIG_PM
#ifndef PCI_OLD_SUSPEND
static int snd_card_cs46xx_suspend(struct pci_dev *pci, u32 state)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pci_get_drvdata(pci), return -ENXIO);
	snd_cs46xx_suspend(chip);
	return 0;
}
static int snd_card_cs46xx_resume(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pci_get_drvdata(pci), return -ENXIO);
	snd_cs46xx_resume(chip);
	return 0;
}
#else
static void snd_card_cs46xx_suspend(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pci_get_drvdata(pci), return);
	snd_cs46xx_suspend(chip);
}
static void snd_card_cs46xx_resume(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pci_get_drvdata(pci), return);
	snd_cs46xx_resume(chip);
}
#endif
#endif

static void __devexit snd_card_cs46xx_remove(struct pci_dev *pci)
{
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, pci_get_drvdata(pci), return);
	if (chip)
		snd_card_free(chip->card);
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name = "Sound Fusion CS46xx",
	.id_table = snd_cs46xx_ids,
	.probe = snd_card_cs46xx_probe,
	.remove = __devexit_p(snd_card_cs46xx_remove),
#ifdef CONFIG_PM
	.suspend = snd_card_cs46xx_suspend,
	.resume = snd_card_cs46xx_resume,
#endif
};

static int __init alsa_card_cs46xx_init(void)
{
	int err;

	if ((err = pci_module_init(&driver)) < 0) {
#ifdef MODULE
		printk(KERN_ERR "Sound Fusion CS46xx soundcard not found or device busy\n");
#endif
		return err;
	}
	return 0;
}

static void __exit alsa_card_cs46xx_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_cs46xx_init)
module_exit(alsa_card_cs46xx_exit)

#ifndef MODULE

/* format is: snd-cs46xx=enable,index,id */

static int __init alsa_card_cs46xx_setup(char *str)
{
	static unsigned __initdata nr_dev = 0;

	if (nr_dev >= SNDRV_CARDS)
		return 0;
	(void)(get_option(&str,&enable[nr_dev]) == 2 &&
	       get_option(&str,&index[nr_dev]) == 2 &&
	       get_id(&str,&id[nr_dev]) == 2);
	nr_dev++;
	return 1;
}

__setup("snd-cs46xx=", alsa_card_cs46xx_setup);

#endif /* ifndef MODULE */
