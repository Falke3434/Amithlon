/*
 *	linux/arch/alpha/kernel/pci.c
 *
 * Extruded from code written by
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 */

/* 2.3.x PCI/resources, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     PCI-PCI bridges cleanup
 */
#include <linux/config.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/cache.h>
#include <asm/machvec.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * Some string constants used by the various core logics. 
 */

const char *const pci_io_names[] = {
  "PCI IO bus 0", "PCI IO bus 1", "PCI IO bus 2", "PCI IO bus 3",
  "PCI IO bus 4", "PCI IO bus 5", "PCI IO bus 6", "PCI IO bus 7"
};

const char *const pci_mem_names[] = {
  "PCI mem bus 0", "PCI mem bus 1", "PCI mem bus 2", "PCI mem bus 3",
  "PCI mem bus 4", "PCI mem bus 5", "PCI mem bus 6", "PCI mem bus 7"
};

const char pci_hae0_name[] = "HAE0";

/* Indicate whether we respect the PCI setup left by console. */
int __initdata pci_probe_only;

/*
 * The PCI controller list.
 */

struct pci_controller *hose_head, **hose_tail = &hose_head;
struct pci_controller *pci_isa_hose;

/*
 * Quirks.
 */

static void __init
quirk_isa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_ISA << 8;
}

static void __init
quirk_cypress(struct pci_dev *dev)
{
	/* The Notorious Cy82C693 chip.  */

	/* The Cypress IDE controller doesn't support native mode, but it
	   has programmable addresses of IDE command/control registers.
	   This violates PCI specifications, confuses the IDE subsystem and
	   causes resource conflicts between the primary HD_CMD register and
	   the floppy controller.  Ugh.  Fix that.  */
	if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE) {
		dev->resource[0].flags = 0;
		dev->resource[1].flags = 0;
	}

	/* The Cypress bridge responds on the PCI bus in the address range
	   0xffff0000-0xffffffff (conventional x86 BIOS ROM).  There is no
	   way to turn this off.  The bridge also supports several extended
	   BIOS ranges (disabled after power-up), and some consoles do turn
	   them on.  So if we use a large direct-map window, or a large SG
	   window, we must avoid the entire 0xfff00000-0xffffffff region.  */
	else if (dev->class >> 8 == PCI_CLASS_BRIDGE_ISA) {
		if (__direct_map_base + __direct_map_size >= 0xfff00000UL)
			__direct_map_size = 0xfff00000UL - __direct_map_base;
		else {
			struct pci_controller *hose = dev->sysdata;
			struct pci_iommu_arena *pci = hose->sg_pci;
			if (pci && pci->dma_base + pci->size >= 0xfff00000UL)
				pci->size = 0xfff00000UL - pci->dma_base;
		}
	}
}

/* Called for each device after PCI setup is done. */
static void __init
pcibios_fixup_final(struct pci_dev *dev)
{
	unsigned int class = dev->class >> 8;

	if (class == PCI_CLASS_BRIDGE_ISA || class == PCI_CLASS_BRIDGE_EISA) {
		dev->dma_mask = MAX_ISA_DMA_ADDRESS - 1;
		isa_bridge = dev;
	}
}

struct pci_fixup pcibios_fixups[] __initdata = {
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82378,
	  quirk_isa_bridge },
	{ PCI_FIXUP_HEADER, PCI_VENDOR_ID_CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693,
	  quirk_cypress },
	{ PCI_FIXUP_FINAL,  PCI_ANY_ID,	PCI_ANY_ID,
	  pcibios_fixup_final },
	{ 0 }
};

#define KB			1024
#define MB			(1024*KB)
#define GB			(1024*MB)

void
pcibios_align_resource(void *data, struct resource *res,
		       unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;
	struct pci_controller *hose = dev->sysdata;
	unsigned long alignto;
	unsigned long start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->io_space->start < PCIBIOS_MIN_IO)
			start = PCIBIOS_MIN_IO + hose->io_space->start;

		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}
	else if	(res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->mem_space->start < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM + hose->mem_space->start;

		/*
		 * The following holds at least for the Low Cost
		 * Alpha implementation of the PCI interface:
		 *
		 * In sparse memory address space, the first
		 * octant (16MB) of every 128MB segment is
		 * aliased to the very first 16 MB of the
		 * address space (i.e., it aliases the ISA
		 * memory address space).  Thus, we try to
		 * avoid allocating PCI devices in that range.
		 * Can be allocated in 2nd-7th octant only.
		 * Devices that need more than 112MB of
		 * address space must be accessed through
		 * dense memory space only!
		 */

		/* Align to multiple of size of minimum base.  */
		alignto = max(0x1000UL, align);
		start = ALIGN(start, alignto);
		if (hose->sparse_mem_base && size <= 7 * 16*MB) {
			if (((start / (16*MB)) & 0x7) == 0) {
				start &= ~(128*MB - 1);
				start += 16*MB;
				start  = ALIGN(start, alignto);
			}
			if (start/(128*MB) != (start + size - 1)/(128*MB)) {
				start &= ~(128*MB - 1);
				start += (128 + 16)*MB;
				start  = ALIGN(start, alignto);
			}
		}
	}

	res->start = start;
}
#undef KB
#undef MB
#undef GB

static int __init
pcibios_init(void)
{
	if (alpha_mv.init_pci)
		alpha_mv.init_pci();
	return 0;
}

subsys_initcall(pcibios_init);

char * __init
pcibios_setup(char *str)
{
	return str;
}

void __init
pcibios_fixup_resource(struct resource *res, struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

void __init
pcibios_fixup_device_resources(struct pci_dev *dev, struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_controller *hose = (struct pci_controller *)bus->sysdata;
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;
		if (dev->resource[i].flags & IORESOURCE_IO)
			pcibios_fixup_resource(&dev->resource[i],
					       hose->io_space);
		else if (dev->resource[i].flags & IORESOURCE_MEM)
			pcibios_fixup_resource(&dev->resource[i],
					       hose->mem_space);
	}
}

void __init
pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propagate hose info into the subordinate devices.  */

	struct pci_controller *hose = bus->sysdata;
	struct list_head *ln;
	struct pci_dev *dev = bus->self;

	if (!dev) {
		/* Root bus. */
		u32 pci_mem_end;
		u32 sg_base = hose->sg_pci ? hose->sg_pci->dma_base : ~0;
		unsigned long end;

		bus->resource[0] = hose->io_space;
		bus->resource[1] = hose->mem_space;

		/* Adjust hose mem_space limit to prevent PCI allocations
		   in the iommu windows. */
		pci_mem_end = min((u32)__direct_map_base, sg_base) - 1;
		end = hose->mem_space->start + pci_mem_end;
		if (hose->mem_space->end > end)
			hose->mem_space->end = end;
 	} else if (pci_probe_only &&
 		   (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
 		pci_read_bridge_bases(bus);
 		pcibios_fixup_device_resources(dev, bus);
	} 

	for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
		struct pci_dev *dev = pci_dev_b(ln);
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

/* Most Alphas have straight-forward swizzling needs.  */

u8 __init
common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	struct pci_controller *hose = dev->sysdata;

	if (dev->bus->number != hose->first_busno) {
		u8 pin = *pinp;
		do {
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
			/* Move up the chain of bridges. */
			dev = dev->bus->self;
		} while (dev->bus->parent);
		*pinp = pin;

		/* The slot is the slot of the last bridge. */
	}

	return PCI_SLOT(dev->devfn);
}

void __devinit
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res)
{
	struct pci_controller *hose = (struct pci_controller *)dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = hose->io_space->start;
	else if (res->flags & IORESOURCE_MEM)
		offset = hose->mem_space->start;

	region->start = res->start - offset;
	region->end = res->end - offset;
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
#endif

int
pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, oldcmd;
	int i;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	oldcmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		else if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != oldcmd) {
		printk(KERN_DEBUG "PCI: Enabling device: (%s), cmd %x\n",
		       dev->slot_name, cmd);
		/* Enable the appropriate bits in the PCI command register.  */
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/*
 *  If we set up a device for bus mastering, we need to check the latency
 *  timer as certain firmware forgets to set it properly, as seen
 *  on SX164 and LX164 with SRM.
 */
void
pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat >= 16) return;
	printk("PCI: Setting latency timer of device %s to 64\n",
							dev->slot_name);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
}

static void __init
pcibios_claim_one_bus(struct pci_bus *b)
{
	struct list_head *ld;
	struct pci_bus *child_bus;

	for (ld = b->devices.next; ld != &b->devices; ld = ld->next) {
		struct pci_dev *dev = pci_dev_b(ld);
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			pci_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		pcibios_claim_one_bus(child_bus);
}

static void __init
pcibios_claim_console_setup(void)
{
	struct list_head *lb;

	for(lb = pci_root_buses.next; lb != &pci_root_buses; lb = lb->next) {
		struct pci_bus *b = pci_bus_b(lb);
		pcibios_claim_one_bus(b);
	}
}

void __init
common_init_pci(void)
{
	struct pci_controller *hose;
	struct pci_bus *bus;
	int next_busno;

	/* Scan all of the recorded PCI controllers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {
		hose->first_busno = next_busno;
		hose->last_busno = 0xff;
		bus = pci_scan_bus(next_busno, alpha_mv.pci_ops, hose);
		hose->bus = bus;
		next_busno = hose->last_busno = bus->subordinate;
		next_busno += 1;
	}

	if (pci_probe_only)
		pcibios_claim_console_setup();

	pci_assign_unassigned_resources();
	pci_fixup_irqs(alpha_mv.pci_swizzle, alpha_mv.pci_map_irq);
}


struct pci_controller * __init
alloc_pci_controller(void)
{
	struct pci_controller *hose;

	hose = alloc_bootmem(sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(void)
{
	struct resource *res;

	res = alloc_bootmem(sizeof(*res));

	return res;
}


/* Provide information on locations of various I/O regions in physical
   memory.  Do this on a per-card basis so that we choose the right hose.  */

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long dfn)
{
	struct pci_controller *hose;
	struct pci_dev *dev;

	/* from hose or from bus.devfn */
	if (which & IOBASE_FROM_HOSE) {
		for(hose = hose_head; hose; hose = hose->next) 
			if (hose->index == bus) break;
		if (!hose) return -ENODEV;
	} else {
		/* Special hook for ISA access.  */
		if (bus == 0 && dfn == 0) {
			hose = pci_isa_hose;
		} else {
			dev = pci_find_slot(bus, dfn);
			if (!dev)
				return -ENODEV;
			hose = dev->sysdata;
		}
	}

	switch (which & ~IOBASE_FROM_HOSE) {
	case IOBASE_HOSE:
		return hose->index;
	case IOBASE_SPARSE_MEM:
		return hose->sparse_mem_base;
	case IOBASE_DENSE_MEM:
		return hose->dense_mem_base;
	case IOBASE_SPARSE_IO:
		return hose->sparse_io_base;
	case IOBASE_DENSE_IO:
		return hose->dense_io_base;
	case IOBASE_ROOT_BUS:
		return hose->bus->number;
	}

	return -EOPNOTSUPP;
}
