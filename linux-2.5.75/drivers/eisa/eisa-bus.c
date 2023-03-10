/*
 * EISA bus support functions for sysfs.
 *
 * (C) 2002, 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 *
 * This code is released under the GPL version 2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <asm/io.h>

#define SLOT_ADDRESS(r,n) (r->bus_base_addr + (0x1000 * n))

#define EISA_DEVINFO(i,s) { .id = { .sig = i }, .name = s }

struct eisa_device_info {
	struct eisa_device_id id;
	char name[DEVICE_NAME_SIZE];
};

static struct eisa_device_info __initdata eisa_table[] = {
#ifdef CONFIG_EISA_NAMES
#include "devlist.h"
#endif
};

#define EISA_INFOS (sizeof (eisa_table) / (sizeof (struct eisa_device_info)))

#define EISA_MAX_FORCED_DEV 16
#define EISA_FORCED_OFFSET  2

static int enable_dev[EISA_MAX_FORCED_DEV + EISA_FORCED_OFFSET]  = { 1, EISA_MAX_FORCED_DEV, };
static int disable_dev[EISA_MAX_FORCED_DEV + EISA_FORCED_OFFSET] = { 1, EISA_MAX_FORCED_DEV, };

static int is_forced_dev (int *forced_tab,
			  struct eisa_root_device *root,
			  struct eisa_device *edev)
{
	int i, x;

	for (i = 0; i < EISA_MAX_FORCED_DEV; i++) {
		if (!forced_tab[EISA_FORCED_OFFSET + i])
			return 0;

		x = (root->bus_nr << 8) | edev->slot;
		if (forced_tab[EISA_FORCED_OFFSET + i] == x)
			return 1;
	}

	return 0;
}

static void __init eisa_name_device (struct eisa_device *edev)
{
	int i;

	for (i = 0; i < EISA_INFOS; i++) {
		if (!strcmp (edev->id.sig, eisa_table[i].id.sig)) {
			strlcpy (edev->dev.name,
				 eisa_table[i].name,
				 DEVICE_NAME_SIZE);
			return;
		}
	}

	/* No name was found */
	sprintf (edev->dev.name, "EISA device %.7s", edev->id.sig);
}

static char __init *decode_eisa_sig(unsigned long addr)
{
        static char sig_str[EISA_SIG_LEN];
	u8 sig[4];
        u16 rev;
	int i;

	for (i = 0; i < 4; i++) {
#ifdef CONFIG_EISA_VLB_PRIMING
		/*
		 * This ugly stuff is used to wake up VL-bus cards
		 * (AHA-284x is the only known example), so we can
		 * read the EISA id.
		 *
		 * Thankfully, this only exists on x86...
		 */
		outb(0x80 + i, addr);
#endif
		sig[i] = inb (addr + i);

		if (!i && (sig[0] & 0x80))
			return NULL;
	}
	
        sig_str[0] = ((sig[0] >> 2) & 0x1f) + ('A' - 1);
        sig_str[1] = (((sig[0] & 3) << 3) | (sig[1] >> 5)) + ('A' - 1);
        sig_str[2] = (sig[1] & 0x1f) + ('A' - 1);
        rev = (sig[2] << 8) | sig[3];
        sprintf(sig_str + 3, "%04X", rev);

        return sig_str;
}

static int eisa_bus_match (struct device *dev, struct device_driver *drv)
{
	struct eisa_device *edev = to_eisa_device (dev);
	struct eisa_driver *edrv = to_eisa_driver (drv);
	const struct eisa_device_id *eids = edrv->id_table;

	if (!eids)
		return 0;

	while (strlen (eids->sig)) {
		if (!strcmp (eids->sig, edev->id.sig) &&
		    edev->state & EISA_CONFIG_ENABLED) {
			edev->id.driver_data = eids->driver_data;
			return 1;
		}

		eids++;
	}

	return 0;
}

struct bus_type eisa_bus_type = {
	.name  = "eisa",
	.match = eisa_bus_match,
};

int eisa_driver_register (struct eisa_driver *edrv)
{
	int r;
	
	edrv->driver.bus = &eisa_bus_type;
	if ((r = driver_register (&edrv->driver)) < 0)
		return r;

	return 0;
}

void eisa_driver_unregister (struct eisa_driver *edrv)
{
	driver_unregister (&edrv->driver);
}

static ssize_t eisa_show_sig (struct device *dev, char *buf)
{
        struct eisa_device *edev = to_eisa_device (dev);
        return sprintf (buf,"%s\n", edev->id.sig);
}

static DEVICE_ATTR(signature, S_IRUGO, eisa_show_sig, NULL);

static ssize_t eisa_show_state (struct device *dev, char *buf)
{
        struct eisa_device *edev = to_eisa_device (dev);
        return sprintf (buf,"%d\n", edev->state & EISA_CONFIG_ENABLED);
}

static DEVICE_ATTR(enabled, S_IRUGO, eisa_show_state, NULL);

static int __init eisa_init_device (struct eisa_root_device *root,
				    struct eisa_device *edev,
				    int slot)
{
	char *sig;
        unsigned long sig_addr;
	int i;

	sig_addr = SLOT_ADDRESS (root, slot) + EISA_VENDOR_ID_OFFSET;

	if (!(sig = decode_eisa_sig (sig_addr)))
		return -1;	/* No EISA device here */
	
	memcpy (edev->id.sig, sig, EISA_SIG_LEN);
	edev->slot = slot;
	edev->state = inb (SLOT_ADDRESS (root, slot) + EISA_CONFIG_OFFSET) & EISA_CONFIG_ENABLED;
	edev->base_addr = SLOT_ADDRESS (root, slot);
	edev->dma_mask = root->dma_mask; /* Default DMA mask */
	eisa_name_device (edev);
	edev->dev.parent = root->dev;
	edev->dev.bus = &eisa_bus_type;
	edev->dev.dma_mask = &edev->dma_mask;
	sprintf (edev->dev.bus_id, "%02X:%02X", root->bus_nr, slot);

	for (i = 0; i < EISA_MAX_RESOURCES; i++)
		edev->res[i].name  = edev->dev.name;

	if (is_forced_dev (enable_dev, root, edev))
		edev->state = EISA_CONFIG_ENABLED | EISA_CONFIG_FORCED;
	
	if (is_forced_dev (disable_dev, root, edev))
		edev->state = EISA_CONFIG_FORCED;

	return 0;
}

static int __init eisa_register_device (struct eisa_device *edev)
{
	if (device_register (&edev->dev))
		return -1;

	device_create_file (&edev->dev, &dev_attr_signature);
	device_create_file (&edev->dev, &dev_attr_enabled);

	return 0;
}

static int __init eisa_request_resources (struct eisa_root_device *root,
					  struct eisa_device *edev,
					  int slot)
{
	int i;

	for (i = 0; i < EISA_MAX_RESOURCES; i++) {
		/* Don't register resource for slot 0, since this is
		 * very likely to fail... :-( Instead, grab the EISA
		 * id, now we can display something in /proc/ioports.
		 */

		/* Only one region for mainboard */
		if (!slot && i > 0) {
			edev->res[i].start = edev->res[i].end = 0;
			continue;
		}
		
		if (slot) {
			edev->res[i].name  = NULL;
			edev->res[i].start = SLOT_ADDRESS (root, slot) + (i * 0x400);
			edev->res[i].end   = edev->res[i].start + 0xff;
			edev->res[i].flags = IORESOURCE_IO;
		} else {
			edev->res[i].name  = NULL;
			edev->res[i].start = SLOT_ADDRESS (root, slot) + EISA_VENDOR_ID_OFFSET;
			edev->res[i].end   = edev->res[i].start + 3;
			edev->res[i].flags = IORESOURCE_BUSY;
		}

		if (request_resource (root->res, &edev->res[i]))
			goto failed;
	}

	return 0;
	
 failed:
	while (--i >= 0)
		release_resource (&edev->res[i]);

	return -1;
}

static void __init eisa_release_resources (struct eisa_device *edev)
{
	int i;

	for (i = 0; i < EISA_MAX_RESOURCES; i++)
		if (edev->res[i].start || edev->res[i].end)
			release_resource (&edev->res[i]);
}

static int __init eisa_probe (struct eisa_root_device *root)
{
        int i, c;
	struct eisa_device *edev;

        printk (KERN_INFO "EISA: Probing bus %d at %s\n",
		root->bus_nr, root->dev->name);

	/* First try to get hold of slot 0. If there is no device
	 * here, simply fail, unless root->force_probe is set. */
	
	if (!(edev = kmalloc (sizeof (*edev), GFP_KERNEL))) {
		printk (KERN_ERR "EISA: Couldn't allocate mainboard slot\n");
		return -ENOMEM;
	}
		
	memset (edev, 0, sizeof (*edev));

	if (eisa_request_resources (root, edev, 0)) {
		printk (KERN_WARNING \
			"EISA: Cannot allocate resource for mainboard\n");
		kfree (edev);
		if (!root->force_probe)
			return -EBUSY;
		goto force_probe;
	}

	if (eisa_init_device (root, edev, 0)) {
		eisa_release_resources (edev);
		kfree (edev);
		if (!root->force_probe)
			return -ENODEV;
		goto force_probe;
	}

	printk (KERN_INFO "EISA: Mainboard %s detected.\n", edev->id.sig);

	if (eisa_register_device (edev)) {
		printk (KERN_ERR "EISA: Failed to register %s\n",
			edev->id.sig);
		eisa_release_resources (edev);
		kfree (edev);
	}
	
 force_probe:
	
        for (c = 0, i = 1; i <= root->slots; i++) {
		if (!(edev = kmalloc (sizeof (*edev), GFP_KERNEL))) {
			printk (KERN_ERR "EISA: Out of memory for slot %d\n",
				i);
			continue;
		}
		
		memset (edev, 0, sizeof (*edev));

		if (eisa_request_resources (root, edev, i)) {
			printk (KERN_WARNING \
				"Cannot allocate resource for EISA slot %d\n",
				i);
			kfree (edev);
			continue;
		}

                if (eisa_init_device (root, edev, i)) {
			eisa_release_resources (edev);
			kfree (edev);
			continue;
		}
		
		printk (KERN_INFO "EISA: slot %d : %s detected",
			i, edev->id.sig);
			
		switch (edev->state) {
		case EISA_CONFIG_ENABLED | EISA_CONFIG_FORCED:
			printk (" (forced enabled)");
			break;

		case EISA_CONFIG_FORCED:
			printk (" (forced disabled)");
			break;

		case 0:
			printk (" (disabled)");
			break;
		}
			
		printk (".\n");

		c++;

		if (eisa_register_device (edev)) {
			printk (KERN_ERR "EISA: Failed to register %s\n",
				edev->id.sig);
			eisa_release_resources (edev);
			kfree (edev);
		}
        }

        printk (KERN_INFO "EISA: Detected %d card%s.\n", c, c == 1 ? "" : "s");

	return 0;
}

static struct resource eisa_root_res = {
	.name  = "EISA root resource",
	.start = 0,
	.end   = 0xffffffff,
	.flags = IORESOURCE_IO,
};

static int eisa_bus_count;

int __init eisa_root_register (struct eisa_root_device *root)
{
	int err;

	/* Use our own resources to check if this bus base address has
	 * been already registered. This prevents the virtual root
	 * device from registering after the real one has, for
	 * example... */
	
	root->eisa_root_res.name  = eisa_root_res.name;
	root->eisa_root_res.start = root->res->start;
	root->eisa_root_res.end   = root->res->end;
	root->eisa_root_res.flags = IORESOURCE_BUSY;

	if ((err = request_resource (&eisa_root_res, &root->eisa_root_res)))
		return err;
	
	root->bus_nr = eisa_bus_count++;

	if ((err = eisa_probe (root)))
		release_resource (&root->eisa_root_res);

	return err;
}

static int __init eisa_init (void)
{
	int r;
	
	if ((r = bus_register (&eisa_bus_type)))
		return r;

	printk (KERN_INFO "EISA bus registered\n");
	return 0;
}

/* Couldn't use intarray with checking on... :-( */
#undef  param_check_intarray
#define param_check_intarray(name, p)

module_param(enable_dev,  intarray, 0444);
module_param(disable_dev, intarray, 0444);

postcore_initcall (eisa_init);

EXPORT_SYMBOL (eisa_bus_type);
EXPORT_SYMBOL (eisa_driver_register);
EXPORT_SYMBOL (eisa_driver_unregister);
