#ifndef _ASM_M68K_PCI_H
#define _ASM_M68K_PCI_H

/*
 * asm-m68k/pci_m68k.h - m68k specific PCI declarations.
 *
 * Written by Wout Klaren.
 */

struct pci_ops;

/*
 * Structure with hardware dependent information and functions of the
 * PCI bus.
 */

struct pci_bus_info
{
	/*
	 * Resources of the PCI bus.
	 */

	struct resource mem_space;
	struct resource io_space;

	/*
	 * System dependent functions.
	 */

	struct pci_ops *m68k_pci_ops;

	void (*fixup)(int pci_modify);
	void (*conf_device)(struct pci_dev *dev);
};

#define pcibios_assign_all_busses()	0

extern inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

extern inline void pcibios_penalize_isa_irq(int irq)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

#endif /* _ASM_M68K_PCI_H */
