/*
 * BK Id: SCCS/s.prep_pci.c 1.26 09/08/01 15:47:42 paulus
 */
/*
 * PReP pci functions.
 * Originally by Gary Thomas
 * rewritten and updated by Cort Dougan (cort@cs.nmt.edu)
 *
 * The motherboard routes/maps will disappear shortly. -- Cort
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/sections.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/residual.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/machdep.h>

#include "pci.h"
#include "open_pic.h"

#define MAX_DEVNR 22

/* Which PCI interrupt line does a given device [slot] use? */
/* Note: This really should be two dimensional based in slot/pin used */
unsigned char *Motherboard_map;
unsigned char *Motherboard_map_name;

/* How is the 82378 PIRQ mapping setup? */
unsigned char *Motherboard_routes;

void (*Motherboard_non0)(struct pci_dev *);

void Powerplus_Map_Non0(struct pci_dev *);

/* Used for Motorola to store system config register */
static unsigned long	*ProcInfo;

/* Tables for known hardware */   

/* Motorola PowerStackII - Utah */
static char Utah_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        5,   /* Slot 2  - SCSI - NCR825A  */
        0,   /* Slot 3  - unused */
        1,   /* Slot 4  - Ethernet - DEC2114x */
        0,   /* Slot 5  - unused */
        3,   /* Slot 6  - PCI Card slot #1 */
        4,   /* Slot 7  - PCI Card slot #2 */
        5,   /* Slot 8  - PCI Card slot #3 */
        5,   /* Slot 9  - PCI Bridge */
             /* added here in case we ever support PCI bridges */
             /* Secondary PCI bus cards are at slot-9,6 & slot-9,7 */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        5,   /* Slot 12 - SCSI - NCR825A */
        0,   /* Slot 13 - unused */
        3,   /* Slot 14 - enet */
        0,   /* Slot 15 - unused */
        2,   /* Slot 16 - unused */
        3,   /* Slot 17 - unused */
        5,   /* Slot 18 - unused */
        0,   /* Slot 19 - unused */
        0,   /* Slot 20 - unused */
        0,   /* Slot 21 - unused */
        0,   /* Slot 22 - unused */
};

static char Utah_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
	10,  /* Line 2 */
        11,  /* Line 3 */
        14,  /* Line 4 */
        15,  /* Line 5 */
};

/* Motorola PowerStackII - Omaha */
/* no integrated SCSI or ethernet */
static char Omaha_pci_IRQ_map[23] __prepdata =
{
        0,   /* Slot 0  - unused */
        0,   /* Slot 1  - unused */
        3,   /* Slot 2  - Winbond EIDE */
        0,   /* Slot 3  - unused */
        0,   /* Slot 4  - unused */
        0,   /* Slot 5  - unused */
        1,   /* Slot 6  - PCI slot 1 */
        2,   /* Slot 7  - PCI slot 2  */
        3,   /* Slot 8  - PCI slot 3 */
        4,   /* Slot 9  - PCI slot 4 */ /* needs indirect access */
        0,   /* Slot 10 - unused */
        0,   /* Slot 11 - unused */
        0,   /* Slot 12 - unused */
        0,   /* Slot 13 - unused */
        0,   /* Slot 14 - unused */
        0,   /* Slot 15 - unused */
        1,   /* Slot 16  - PCI slot 1 */
        2,   /* Slot 17  - PCI slot 2  */
        3,   /* Slot 18  - PCI slot 3 */
        4,   /* Slot 19  - PCI slot 4 */ /* needs indirect access */
        0,
        0,
        0,
};

static char Omaha_pci_IRQ_routes[] __prepdata =
{
        0,   /* Line 0 - Unused */
        9,   /* Line 1 */
        11,  /* Line 2 */
        14,  /* Line 3 */
        15   /* Line 4 */
};

/* Motorola PowerStack */
static char Blackhawk_pci_IRQ_map[19] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
 	1,	/* Slot P7 */
 	2,	/* Slot P6 */
 	3,	/* Slot P5 */
};

static char Blackhawk_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	9,	/* Line 1 */
   	11,	/* Line 2 */
   	15,	/* Line 3 */
   	15	/* Line 4 */
};
   
/* Motorola Mesquite */
static char Mesquite_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	0,	/* Slot 12 - unused */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	3,	/* Slot 16 - PMC */
	0,	/* Slot 17 - unused */
	0,	/* Slot 18 - unused */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Sitka */
static char Sitka_pci_IRQ_map[21] __prepdata =
{
	0,      /* Slot 0  - unused */
	0,      /* Slot 1  - unused */
	0,      /* Slot 2  - unused */
	0,      /* Slot 3  - unused */
	0,      /* Slot 4  - unused */
	0,      /* Slot 5  - unused */
	0,      /* Slot 6  - unused */
	0,      /* Slot 7  - unused */
	0,      /* Slot 8  - unused */
	0,      /* Slot 9  - unused */
	0,      /* Slot 10 - unused */
	0,      /* Slot 11 - unused */
	0,      /* Slot 12 - unused */
	0,      /* Slot 13 - unused */
	2,      /* Slot 14 - Ethernet */
	0,      /* Slot 15 - unused */
	9,      /* Slot 16 - PMC 1  */
	12,     /* Slot 17 - PMC 2  */
	0,      /* Slot 18 - unused */
	0,      /* Slot 19 - unused */
	4,      /* Slot 20 - NT P2P bridge */
};

/* Motorola MTX */
static char MTX_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - Ethernet */
	0,	/* Slot 15 - unused */
	9,      /* Slot 16 - PCI/PMC slot 1 */
	10,     /* Slot 17 - PCI/PMC slot 2 */
	11,     /* Slot 18 - PCI slot 3 */
	0,	/* Slot 19 - unused */
	0,	/* Slot 20 - unused */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola MTX Plus */
/* Secondary bus interrupt routing is not supported yet */
static char MTXplus_pci_IRQ_map[23] __prepdata =
{
        0,      /* Slot 0  - unused */
        0,      /* Slot 1  - unused */
        0,      /* Slot 2  - unused */
        0,      /* Slot 3  - unused */
        0,      /* Slot 4  - unused */
        0,      /* Slot 5  - unused */
        0,      /* Slot 6  - unused */
        0,      /* Slot 7  - unused */
        0,      /* Slot 8  - unused */
        0,      /* Slot 9  - unused */
        0,      /* Slot 10 - unused */
        0,      /* Slot 11 - unused */
        3,      /* Slot 12 - SCSI */
        0,      /* Slot 13 - unused */
        2,      /* Slot 14 - Ethernet 1 */
        0,      /* Slot 15 - unused */
        9,      /* Slot 16 - PCI slot 1P */
        10,     /* Slot 17 - PCI slot 2P */
        11,     /* Slot 18 - PCI slot 3P */
        10,     /* Slot 19 - Ethernet 2 */
        0,      /* Slot 20 - P2P Bridge */
        0,      /* Slot 21 - unused */
        0,      /* Slot 22 - unused */
};

static char Raven_pci_IRQ_routes[] __prepdata =
{
   	0,	/* This is a dummy structure */
};
   
/* Motorola MVME16xx */
static char Genesis_pci_IRQ_map[16] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
};

static char Genesis_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};
   
static char Genesis2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	0,	/* Slot 2  - unused */
	0,	/* Slot 3  - unused */
	0,	/* Slot 4  - unused */
	0,	/* Slot 5  - unused */
	0,	/* Slot 6  - unused */
	0,	/* Slot 7  - unused */
	0,	/* Slot 8  - unused */
	0,	/* Slot 9  - unused */
	0,	/* Slot 10 - Ethernet */
	0,	/* Slot 11 - Universe PCI - VME Bridge */
	3,	/* Slot 12 - unused */
	0,	/* Slot 13 - unused */
	2,	/* Slot 14 - SCSI */
	0,	/* Slot 15 - unused */
	9,	/* Slot 16 - PMC 1 */
	12,	/* Slot 17 - pci */
	11,	/* Slot 18 - pci */
	10,	/* Slot 19 - pci */
	0,	/* Slot 20 - pci */
	0,	/* Slot 21 - unused */
	0,	/* Slot 22 - unused */
};

/* Motorola Series-E */
static char Comet_pci_IRQ_map[23] __prepdata =
{
  	0,	/* Slot 0  - unused */
  	0,	/* Slot 1  - unused */
  	0,	/* Slot 2  - unused */
  	0,	/* Slot 3  - unused */
  	0,	/* Slot 4  - unused */
  	0,	/* Slot 5  - unused */
  	0,	/* Slot 6  - unused */
  	0,	/* Slot 7  - unused */
  	0,	/* Slot 8  - unused */
  	0,	/* Slot 9  - unused */
  	0,	/* Slot 10 - unused */
  	0,	/* Slot 11 - unused */
  	3,	/* Slot 12 - SCSI */
  	0,	/* Slot 13 - unused */
  	1,	/* Slot 14 - Ethernet */
  	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet_pci_IRQ_routes[] __prepdata =
{
   	0,	/* Line 0 - Unused */
   	10,	/* Line 1 */
   	11,	/* Line 2 */
   	14,	/* Line 3 */
   	15	/* Line 4 */
};

/* Motorola Series-EX */
static char Comet2_pci_IRQ_map[23] __prepdata =
{
	0,	/* Slot 0  - unused */
	0,	/* Slot 1  - unused */
	3,	/* Slot 2  - SCSI - NCR825A */
	0,	/* Slot 3  - unused */
	1,	/* Slot 4  - Ethernet - DEC2104X */
	0,	/* Slot 5  - unused */
	1,	/* Slot 6  - PCI slot 1 */
	2,	/* Slot 7  - PCI slot 2 */
	3,	/* Slot 8  - PCI slot 3 */
	4,	/* Slot 9  - PCI bridge  */
	0,	/* Slot 10 - unused */
	0,	/* Slot 11 - unused */
	3,	/* Slot 12 - SCSI - NCR825A */
	0,	/* Slot 13 - unused */
	1,	/* Slot 14 - Ethernet - DEC2104X */
	0,	/* Slot 15 - unused */
	1,	/* Slot 16 - PCI slot 1 */
	2,	/* Slot 17 - PCI slot 2 */
	3,	/* Slot 18 - PCI slot 3 */
	4,	/* Slot 19 - PCI bridge */
	0,
	0,
	0,
};

static char Comet2_pci_IRQ_routes[] __prepdata =
{
	0,	/* Line 0 - Unused */
	10,	/* Line 1 */
	11,	/* Line 2 */
	14,	/* Line 3 */
	15,	/* Line 4 */
};

/*
 * ibm 830 (and 850?).
 * This is actually based on the Carolina motherboard
 * -- Cort
 */
static char ibm8xx_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral */
        4, /* Slot 12 - Ethernet  PCIINTD# */
        2, /* Slot 13 - PCI Slot #2 */
        2, /* Slot 14 - S3 Video PCIINTD# */
        0, /* Slot 15 - onboard SCSI (INDI) [1] */
        3, /* Slot 16 - NCR58C810 RS6000 Only PCIINTC# */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        2, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};

static char ibm8xx_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        15,     /* Line 1 */
        15,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/*
 * a 6015 ibm board
 * -- Cort
 */
static char ibm6015_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 -  */
        1, /* Slot 12 - SCSI */
        2, /* Slot 13 -  */
        2, /* Slot 14 -  */
        1, /* Slot 15 -  */
        1, /* Slot 16 -  */
        0, /* Slot 17 -  */
        2, /* Slot 18 -  */
        0, /* Slot 19 -  */
        0, /* Slot 20 -  */
        0, /* Slot 21 -  */
        2, /* Slot 22 -  */
};
static char ibm6015_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        13,     /* Line 1 */
        10,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};


/* IBM Nobis and 850 */
static char Nobis_pci_IRQ_map[23] __prepdata ={
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - unused */
        3, /* Slot 12 - SCSI */
        0, /* Slot 13 - unused */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
};

static char Nobis_pci_IRQ_routes[] __prepdata = {
        0, /* Line 0 - Unused */
        13, /* Line 1 */
        13, /* Line 2 */
        13, /* Line 3 */
        13      /* Line 4 */
};

/*
 * IBM RS/6000 43p/140  -- paulus
 * XXX we should get all this from the residual data
 */
static char ibm43p_pci_IRQ_map[23] __prepdata = {
        0, /* Slot 0  - unused */
        0, /* Slot 1  - unused */
        0, /* Slot 2  - unused */
        0, /* Slot 3  - unused */
        0, /* Slot 4  - unused */
        0, /* Slot 5  - unused */
        0, /* Slot 6  - unused */
        0, /* Slot 7  - unused */
        0, /* Slot 8  - unused */
        0, /* Slot 9  - unused */
        0, /* Slot 10 - unused */
        0, /* Slot 11 - FireCoral ISA bridge */
        6, /* Slot 12 - Ethernet  */
        0, /* Slot 13 - openpic */
        0, /* Slot 14 - unused */
        0, /* Slot 15 - unused */
        7, /* Slot 16 - NCR58C825a onboard scsi */
        0, /* Slot 17 - unused */
        2, /* Slot 18 - PCI Slot 2 PCIINTx# (See below) */
        0, /* Slot 19 - unused */
        0, /* Slot 20 - unused */
        0, /* Slot 21 - unused */
        1, /* Slot 22 - PCI slot 1 PCIINTx# (See below) */
};

static char ibm43p_pci_IRQ_routes[] __prepdata = {
        0,      /* Line 0 - unused */
        15,     /* Line 1 */
        15,     /* Line 2 */
        15,     /* Line 3 */
        15,     /* Line 4 */
};

/* Motorola PowerPlus architecture PCI IRQ tables */
/* Interrupt line values for INTA-D on primary/secondary MPIC inputs */

struct powerplus_irq_list
{
	unsigned char primary[4];       /* INT A-D */
	unsigned char secondary[4];     /* INT A-D */
};

/*
 * For standard PowerPlus boards, bus 0 PCI INTs A-D are routed to
 * OpenPIC inputs 9-12.  PCI INTs A-D from the on board P2P bridge
 * are routed to OpenPIC inputs 5-8.  These values are offset by
 * 16 in the table to reflect the Linux kernel interrupt value.
 */
struct powerplus_irq_list Powerplus_pci_IRQ_list =
{
	{25, 26, 27, 28},
	{21, 22, 23, 24}
};

/*
 * For the MCP750 (system slot board), cPCI INTs A-D are routed to
 * OpenPIC inputs 8-11 and the PMC INTs A-D are routed to OpenPIC
 * input 3.  On a hot swap MCP750, the companion card PCI INTs A-D
 * are routed to OpenPIC inputs 12-15. These values are offset by
 * 16 in the table to reflect the Linux kernel interrupt value.
 */
struct powerplus_irq_list Mesquite_pci_IRQ_list =
{
	{24, 25, 26, 27},
	{28, 29, 30, 31}
};

/*
 * This table represents the standard PCI swizzle defined in the
 * PCI bus specification.
 */
static unsigned char prep_pci_intpins[4][4] =
{
	{ 1, 2, 3, 4},  /* Buses 0, 4, 8, ... */
	{ 2, 3, 4, 1},  /* Buses 1, 5, 9, ... */
	{ 3, 4, 1, 2},  /* Buses 2, 6, 10 ... */
	{ 4, 1, 2, 3},  /* Buses 3, 7, 11 ... */
};

/* We have to turn on LEVEL mode for changed IRQ's */
/* All PCI IRQ's need to be level mode, so this should be something
 * other than hard-coded as well... IRQ's are individually mappable
 * to either edge or level.
 */
#define CAROLINA_IRQ_EDGE_MASK_LO   0x00  /* IRQ's 0-7  */
#define CAROLINA_IRQ_EDGE_MASK_HI   0xA4  /* IRQ's 8-15 [10,13,15] */

/*
 * 8259 edge/level control definitions
 */
#define ISA8259_M_ELCR 0x4d0
#define ISA8259_S_ELCR 0x4d1

#define ELCRS_INT15_LVL         0x80
#define ELCRS_INT14_LVL         0x40
#define ELCRS_INT12_LVL         0x10
#define ELCRS_INT11_LVL         0x08
#define ELCRS_INT10_LVL         0x04
#define ELCRS_INT9_LVL          0x02
#define ELCRS_INT8_LVL          0x01
#define ELCRM_INT7_LVL          0x80
#define ELCRM_INT5_LVL          0x20

#define CFGPTR(dev) (0x80800000 | (1<<(dev>>3)) | ((dev&7)<<8) | offset)
#define DEVNO(dev)  (dev>>3)                                  

#define cfg_read(val, addr, type, op)	*val = op((type)(addr))
#define cfg_write(val, addr, type, op)	op((type *)(addr), (val))

#define cfg_read_bad(val, size)		*val = bad_##size;
#define cfg_write_bad(val, size)

#define bad_byte	0xff
#define bad_word	0xffff
#define bad_dword	0xffffffffU

#define PREP_PCI_OP(rw, size, type, op)					\
static int __prep							\
prep_##rw##_config_##size(struct pci_dev *dev, int offset, type val)	\
{									\
	if ((dev->bus->number != 0) || (DEVNO(dev->devfn) > MAX_DEVNR))	\
	{                   						\
		cfg_##rw##_bad(val, size)				\
		return PCIBIOS_DEVICE_NOT_FOUND;    			\
	}								\
	cfg_##rw(val, CFGPTR(dev->devfn), type, op);			\
	return PCIBIOS_SUCCESSFUL;					\
}

PREP_PCI_OP(read, byte, u8 *, in_8)
PREP_PCI_OP(read, word, u16 *, in_le16)
PREP_PCI_OP(read, dword, u32 *, in_le32)
PREP_PCI_OP(write, byte, u8, out_8)
PREP_PCI_OP(write, word, u16, out_le16)
PREP_PCI_OP(write, dword, u32, out_le32)

static struct pci_ops prep_pci_ops =
{
	prep_read_config_byte,
	prep_read_config_word,
	prep_read_config_dword,
	prep_write_config_byte,
	prep_write_config_word,
	prep_write_config_dword
};

#define MOTOROLA_CPUTYPE_REG	0x800
#define MOTOROLA_BASETYPE_REG	0x803
#define MPIC_RAVEN_ID		0x48010000
#define	MPIC_HAWK_ID		0x48030000
#define	MOT_PROC2_BIT		0x800

static u_char mvme2600_openpic_initsenses[] __initdata = {
    1,	/* MVME2600_INT_SIO */
    0,	/* MVME2600_INT_FALCN_ECC_ERR */
    1,	/* MVME2600_INT_PCI_ETHERNET */
    1,	/* MVME2600_INT_PCI_SCSI */
    1,	/* MVME2600_INT_PCI_GRAPHICS */
    1,	/* MVME2600_INT_PCI_VME0 */
    1,	/* MVME2600_INT_PCI_VME1 */
    1,	/* MVME2600_INT_PCI_VME2 */
    1,	/* MVME2600_INT_PCI_VME3 */
    1,	/* MVME2600_INT_PCI_INTA */
    1,	/* MVME2600_INT_PCI_INTB */
    1,	/* MVME2600_INT_PCI_INTC */
    1,	/* MVME2600_INT_PCI_INTD */
    1,	/* MVME2600_INT_LM_SIG0 */
    1,	/* MVME2600_INT_LM_SIG1 */
};

#define MOT_RAVEN_PRESENT	0x1
#define MOT_HAWK_PRESENT	0x2

int mot_entry = -1;
int prep_keybd_present = 1;
int MotMPIC;
int mot_multi;

int __init raven_init(void)
{
	unsigned int	devid;
	unsigned int	pci_membase;
	unsigned char	base_mod;

	/* Check to see if the Raven chip exists. */
	if ( _prep_type != _PREP_Motorola) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Check to see if this board is a type that might have a Raven. */
	if ((inb(MOTOROLA_CPUTYPE_REG) & 0xF0) != 0xE0) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Check the first PCI device to see if it is a Raven. */
	early_read_config_dword(0, 0, 0, PCI_VENDOR_ID, &devid);

	switch (devid & 0xffff0000) {
	case MPIC_RAVEN_ID:
		MotMPIC = MOT_RAVEN_PRESENT;
		break;
	case MPIC_HAWK_ID:
		MotMPIC = MOT_HAWK_PRESENT;
		break;
	default:
		OpenPIC_Addr = NULL;
		return 0;
	}


	/* Read the memory base register. */
	early_read_config_dword(0, 0, 0, PCI_BASE_ADDRESS_1, &pci_membase);

	if (pci_membase == 0) {
		OpenPIC_Addr = NULL;
		return 0;
	}

	/* Map the Raven MPIC registers to virtual memory. */
	OpenPIC_Addr = ioremap(pci_membase+0xC0000000, 0x22000);

	OpenPIC_InitSenses = mvme2600_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(mvme2600_openpic_initsenses);

	ppc_md.get_irq = openpic_get_irq;
	
	/* If raven is present on Motorola store the system config register
	 * for later use.
	 */
	ProcInfo = (unsigned long *)ioremap(0xfef80400, 4);

	/* Indicate to system if this is a multiprocessor board */
	if (!(*ProcInfo & MOT_PROC2_BIT)) {
		mot_multi = 1;
	}

	/* This is a hack.  If this is a 2300 or 2400 mot board then there is
	 * no keyboard controller and we have to indicate that.
	 */
	base_mod = inb(MOTOROLA_BASETYPE_REG);
	if ((MotMPIC == MOT_HAWK_PRESENT) || (base_mod == 0xF9) ||
	    (base_mod == 0xFA) || (base_mod == 0xE1))
		prep_keybd_present = 0;

	return 1;
}

struct mot_info {
	int		cpu_type;	/* 0x100 mask assumes for Raven and Hawk boards that the level/edge are set */
					/* 0x200 if this board has a Hawk chip. */
	int		base_type;
	int		max_cpu;	/* ored with 0x80 if this board should be checked for multi CPU */
	const char	*name;
	unsigned char	*map;
	unsigned char	*routes;
	void            (*map_non0_bus)(struct pci_dev *);      /* For boards with more than bus 0 devices. */
	struct powerplus_irq_list *pci_irq_list; /* List of PCI MPIC inputs */
	unsigned char   secondary_bridge_devfn; /* devfn of secondary bus transparent bridge */
} mot_info[] = {
	{0x300, 0x00, 0x00, "MVME 2400",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x010, 0x00, 0x00, "Genesis",				Genesis_pci_IRQ_map,	Genesis_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x020, 0x00, 0x00, "Powerstack (Series E)",		Comet_pci_IRQ_map,	Comet_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x040, 0x00, 0x00, "Blackhawk (Powerstack)",		Blackhawk_pci_IRQ_map,	Blackhawk_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x050, 0x00, 0x00, "Omaha (PowerStack II Pro3000)",	Omaha_pci_IRQ_map,	Omaha_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x060, 0x00, 0x00, "Utah (Powerstack II Pro4000)",	Utah_pci_IRQ_map,	Utah_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x0A0, 0x00, 0x00, "Powerstack (Series EX)",		Comet2_pci_IRQ_map,	Comet2_pci_IRQ_routes, NULL, NULL, 0x00},
	{0x1E0, 0xE0, 0x00, "Mesquite cPCI (MCP750)",		Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Mesquite_pci_IRQ_list, 0xFF},
	{0x1E0, 0xE1, 0x00, "Sitka cPCI (MCPN750)",		Sitka_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xE2, 0x00, "Mesquite cPCI (MCP750) w/ HAC",	Mesquite_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Mesquite_pci_IRQ_list, 0xC0},
	{0x1E0, 0xF6, 0x80, "MTX Plus",				MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xA0},
	{0x1E0, 0xF6, 0x81, "Dual MTX Plus",			MTXplus_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xA0},
	{0x1E0, 0xF7, 0x80, "MTX wo/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF7, 0x81, "Dual MTX wo/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF8, 0x80, "MTX w/ Parallel Port",		MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF8, 0x81, "Dual MTX w/ Parallel Port",	MTX_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xF9, 0x00, "MVME 2300",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFA, 0x00, "MVME 2300SC/2600",			Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFB, 0x00, "MVME 2600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFC, 0x00, "MVME 2600/2700 with MVME761",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFD, 0x80, "MVME 3600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0x00},
	{0x1E0, 0xFD, 0x81, "MVME 4600 with MVME712M",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFE, 0x80, "MVME 3600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFE, 0x81, "MVME 4600 with MVME761",		Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x1E0, 0xFF, 0x00, "MVME 1600-001 or 1600-011",	Genesis2_pci_IRQ_map,	Raven_pci_IRQ_routes, Powerplus_Map_Non0, &Powerplus_pci_IRQ_list, 0xFF},
	{0x000, 0x00, 0x00, "",					NULL,			NULL, NULL, NULL, 0x00}
};

void ibm_prep_init(void)
{
	u32 addr;
#ifdef CONFIG_PREP_RESIDUAL
	PPC_DEVICE *mpic;
#endif

	if (inb(0x0852) == 0xd5) {
		/* This is for the 43p-140 */
		early_read_config_dword(0, 0, PCI_DEVFN(13, 0),
					PCI_BASE_ADDRESS_0, &addr);
		if (addr != 0xffffffff
		    && !(addr & PCI_BASE_ADDRESS_SPACE_IO)
		    && (addr &= PCI_BASE_ADDRESS_MEM_MASK) != 0) {
			addr += PREP_ISA_MEM_BASE;
			OpenPIC_Addr = ioremap(addr, 0x40000);
			ppc_md.get_irq = openpic_get_irq;
		}
	}

#ifdef CONFIG_PREP_RESIDUAL
	mpic = residual_find_device(-1, NULL, SystemPeripheral,
				    ProgrammableInterruptController, MPIC, 0);
	if (mpic != NULL) {
		printk("mpic = %p\n", mpic);
	}
#endif
}

void
ibm43p_pci_map_non0(struct pci_dev *dev)
{
	unsigned char intpin;
	static unsigned char bridge_intrs[4] = { 3, 4, 5, 8 };

	if (dev == NULL)
		return;
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &intpin);
	if (intpin < 1 || intpin > 4)
		return;
	intpin = (PCI_SLOT(dev->devfn) + intpin - 1) & 3;
	dev->irq = openpic_to_irq(bridge_intrs[intpin]);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
}

void __init prep_route_pci_interrupts(void)
{
	unsigned char *ibc_pirq = (unsigned char *)0x80800860;
	unsigned char *ibc_pcicon = (unsigned char *)0x80800840;
	int i;
	
	if ( _prep_type == _PREP_Motorola)
	{
		unsigned short irq_mode;
		unsigned char  cpu_type;
		unsigned char  base_mod;
		int	       entry;

		cpu_type = inb(MOTOROLA_CPUTYPE_REG) & 0xF0;
		base_mod = inb(MOTOROLA_BASETYPE_REG);

		for (entry = 0; mot_info[entry].cpu_type != 0; entry++) {
			if (mot_info[entry].cpu_type & 0x200) {		 	/* Check for Hawk chip */
				if (!(MotMPIC & MOT_HAWK_PRESENT))
					continue;
			} else {						/* Check non hawk boards */
				if ((mot_info[entry].cpu_type & 0xff) != cpu_type)
					continue;

				if (mot_info[entry].base_type == 0) {
					mot_entry = entry;
					break;
				}

				if (mot_info[entry].base_type != base_mod)
					continue;
			}

			if (!(mot_info[entry].max_cpu & 0x80)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 not present and max processor zero indicated */
			if ((*ProcInfo & MOT_PROC2_BIT) && !(mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}

			/* processor 1 present and max processor zero indicated */
			if (!(*ProcInfo & MOT_PROC2_BIT) && (mot_info[entry].max_cpu & 0x7f)) {
				mot_entry = entry;
				break;
			}
		}

		if (mot_entry == -1) 	/* No particular cpu type found - assume Blackhawk */
			mot_entry = 3;

		Motherboard_map_name = (unsigned char *)mot_info[mot_entry].name;
		Motherboard_map = mot_info[mot_entry].map;
		Motherboard_routes = mot_info[mot_entry].routes;
		Motherboard_non0 = mot_info[mot_entry].map_non0_bus;

		if (!(mot_info[entry].cpu_type & 0x100)) {
			/* AJF adjust level/edge control according to routes */
			irq_mode = 0;
			for (i = 1;  i <= 4;  i++)
			{
				irq_mode |= ( 1 << Motherboard_routes[i] );
			}
			outb( irq_mode & 0xff, 0x4d0 );
			outb( (irq_mode >> 8) & 0xff, 0x4d1 );
		}
	} else if ( _prep_type == _PREP_IBM )
	{
		unsigned char pl_id;
		/*
		 * my carolina is 0xf0
		 * 6015 has 0xfc
		 * -- Cort
		 */
		printk("IBM ID: %08x\n", inb(0x0852));
		switch(inb(0x0852))
		{
		case 0xff:
			Motherboard_map_name = "IBM 850/860 Portable";
			Motherboard_map = Nobis_pci_IRQ_map;
			Motherboard_routes = Nobis_pci_IRQ_routes;
			break;
		case 0xfc:
			Motherboard_map_name = "IBM 6015";
			Motherboard_map = ibm6015_pci_IRQ_map;
			Motherboard_routes = ibm6015_pci_IRQ_routes;
			break;
		case 0xd5:
			Motherboard_map_name = "IBM 43p/140";
			Motherboard_map = ibm43p_pci_IRQ_map;
			Motherboard_routes = ibm43p_pci_IRQ_routes;
			Motherboard_non0 = ibm43p_pci_map_non0;
			break;
		default:
			Motherboard_map_name = "IBM 8xx (Carolina)";
			Motherboard_map = ibm8xx_pci_IRQ_map;
			Motherboard_routes = ibm8xx_pci_IRQ_routes;
			break;
		}

		/*printk("Changing IRQ mode\n");*/
		pl_id=inb(0x04d0);
		/*printk("Low mask is %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_LO, 0x04d0);
		
		pl_id=inb(0x04d1);
		/*printk("Hi mask is  %#0x\n", pl_id);*/
		outb(pl_id|CAROLINA_IRQ_EDGE_MASK_HI, 0x04d1);
		pl_id=inb(0x04d1);
		/*printk("Hi mask now %#0x\n", pl_id);*/
	}
	else
	{
		printk("No known machine pci routing!\n");
		return;
	}
	
	/* Set up mapping from slots */
	for (i = 1;  i <= 4;  i++)
	{
		ibc_pirq[i-1] = Motherboard_routes[i];
	}
	/* Enable PCI interrupts */
	*ibc_pcicon |= 0x20;
}

void __init
prep_pib_init(void)
{
	unsigned char   reg;
	unsigned short  short_reg;

	struct pci_dev *dev = NULL;

	if (( _prep_type == _PREP_Motorola) && (OpenPIC_Addr)) {
		/*
		 * Perform specific configuration for the Via Tech or
		 * or Winbond PCI-ISA-Bridge part.
		 */
		if ((dev = pci_find_device(PCI_VENDOR_ID_VIA, 
					PCI_DEVICE_ID_VIA_82C586_1, dev))) {
			/*
			 * PPCBUG does not set the enable bits
			 * for the IDE device. Force them on here.
			 */
			pci_read_config_byte(dev, 0x40, &reg);

			reg |= 0x03; /* IDE: Chip Enable Bits */
			pci_write_config_byte(dev, 0x40, reg);
		}
		if ((dev = pci_find_device(PCI_VENDOR_ID_VIA,
						PCI_DEVICE_ID_VIA_82C586_2,
						dev)) && (dev->devfn = 0x5a)) {
			/* Force correct USB interrupt */
			dev->irq = 11;
			pci_write_config_byte(dev,
					PCI_INTERRUPT_LINE,
					dev->irq);
		}
		if ((dev = pci_find_device(PCI_VENDOR_ID_WINBOND,
					PCI_DEVICE_ID_WINBOND_83C553, dev))) {
			 /* Clear PCI Interrupt Routing Control Register. */
			short_reg = 0x0000;
			pci_write_config_word(dev, 0x44, short_reg);
			if (OpenPIC_Addr){
				/* Route IDE interrupts to IRQ 14 */
				reg = 0xEE;
				pci_write_config_byte(dev, 0x43, reg);
			}
		}
	}

	if ((dev = pci_find_device(PCI_VENDOR_ID_WINBOND,
				   PCI_DEVICE_ID_WINBOND_82C105, dev))){
		if (OpenPIC_Addr){
			/*
			 * Disable LEGIRQ mode so PCI INTS are routed
			 * directly to the 8259 and enable both channels
			 */
			pci_write_config_dword(dev, 0x40, 0x10ff0033);

			/* Force correct IDE interrupt */
			dev->irq = 14;
			pci_write_config_byte(dev,
					PCI_INTERRUPT_LINE,
					dev->irq);
		}else{
			/* Enable LEGIRQ for PCI INT -> 8259 IRQ routing */
			pci_write_config_dword(dev, 0x40, 0x10ff08a1);
		}
	}
}

void
Powerplus_Map_Non0(struct pci_dev *dev)
{
	struct pci_bus  *pbus;          /* Parent bus structure pointer */
	struct pci_dev  *tdev = dev;    /* Temporary device structure */
	unsigned int    devnum;         /* Accumulated device number */
	unsigned char   intline;        /* Linux interrupt value */
	unsigned char   intpin;         /* PCI interrupt pin */

	/* Check for valid PCI dev pointer */
	if (dev == NULL) return;

	/* Initialize bridge IDSEL variable */
	devnum = PCI_SLOT(tdev->devfn);

	/* Read the interrupt pin of the device and adjust for indexing */
	pcibios_read_config_byte(dev->bus->number, dev->devfn,
			PCI_INTERRUPT_PIN, &intpin);

	/* If device doesn't request an interrupt, return */
	if ( (intpin < 1) || (intpin > 4) )
		return;

	intpin--;

	/*
	 * Walk up to bus 0, adjusting the interrupt pin for the standard
	 * PCI bus swizzle.
	 */
	do {
		intpin = (prep_pci_intpins[devnum % 4][intpin]) - 1;
		pbus = tdev->bus;        /* up one level */
		tdev = pbus->self;
		devnum = PCI_SLOT(tdev->devfn);
	} while(tdev->bus->number);

	/* Use the primary interrupt inputs by default */
	intline = mot_info[mot_entry].pci_irq_list->primary[intpin];

	/*
	 * If the board has secondary interrupt inputs, walk the bus and
	 * note the devfn of the bridge from bus 0.  If it is the same as
	 * the devfn of the bus bridge with secondary inputs, use those.
	 * Otherwise, assume it's a PMC site and get the interrupt line
	 * value from the interrupt routing table.
	 */ 
	if (mot_info[mot_entry].secondary_bridge_devfn)
	{
		pbus = dev->bus;

		while (pbus->primary != 0)
			pbus = pbus->parent;

		if ((pbus->self)->devfn != 0xA0)
		{
			if ((pbus->self)->devfn == mot_info[mot_entry].secondary_bridge_devfn)
				intline = mot_info[mot_entry].pci_irq_list->secondary[intpin];
			else
			{
				if ((char *)(mot_info[mot_entry].map) == (char *)Mesquite_pci_IRQ_map)
					intline = mot_info[mot_entry].map[((pbus->self)->devfn)/8] + 16;
				else
				{
					int i;
					for (i=0;i<3;i++)
						intpin = (prep_pci_intpins[devnum % 4][intpin]) - 1;
					intline = mot_info[mot_entry].pci_irq_list->primary[intpin];
				}
			}
		}
	}

	/* Write calculated interrupt value to header and device list */
	dev->irq = intline;
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, (u8)dev->irq);
}

void __init
prep_pcibios_fixup(void)
{
        struct pci_dev *dev;
        extern unsigned char *Motherboard_map;
        extern unsigned char *Motherboard_routes;
        unsigned char i;

	prep_route_pci_interrupts();

	printk("Setting PCI interrupts for a \"%s\"\n", Motherboard_map_name);
	if (OpenPIC_Addr) {
		/* PCI interrupts are controlled by the OpenPIC */
		pci_for_each_dev(dev) {
			if (dev->bus->number == 0)
			{
                       		dev->irq = openpic_to_irq(Motherboard_map[PCI_SLOT(dev->devfn)]);
				pcibios_write_config_byte(dev->bus->number, dev->devfn, PCI_INTERRUPT_LINE, dev->irq);
			}
			else
			{
				if (Motherboard_non0 != NULL)
					Motherboard_non0(dev);
			}
		}

		/* Setup the Winbond or Via PIB */
		prep_pib_init();

		return;
	}

	pci_for_each_dev(dev) {
		/*
		 * Use our old hard-coded kludge to figure out what
		 * irq this device uses.  This is necessary on things
		 * without residual data. -- Cort
		 */
		unsigned char d = PCI_SLOT(dev->devfn);
		dev->irq = Motherboard_routes[Motherboard_map[d]];

		for ( i = 0 ; i <= 5 ; i++ )
		{
			/*
			 * Relocate PCI I/O resources if necessary so the
			 * standard 256MB BAT covers them.
			 */
			if ( (pci_resource_flags(dev, i) & IORESOURCE_IO) &&
				(dev->resource[i].start > 0x10000000) ) 
		        {
		                printk("Relocating PCI address %lx -> %lx\n",
		                       dev->resource[i].start,
		                       (dev->resource[i].start & 0x00FFFFFF)
		                       | 0x01000000);
		                dev->resource[i].start =
		                  (dev->resource[i].start & 0x00FFFFFF) | 0x01000000;
		                pci_write_config_dword(dev,
		                        PCI_BASE_ADDRESS_0+(i*0x4),
		                       dev->resource[i].start );
				dev->resource[i].end =
					(dev->resource[i].end & 0x00FFFFFF) | 0x01000000;
		        }
		}
#if 0
		/*
		 * If we have residual data and if it knows about this
		 * device ask it what the irq is.
		 *  -- Cort
		 */
		ppcd = residual_find_device_id( ~0L, dev->device,
		                                -1,-1,-1, 0);
#endif
	}
}

static void __init
prep_init_resource(struct resource *res, unsigned long start,
		   unsigned long end, int flags)
{
	res->flags = flags;
	res->start = start;
	res->end = end;
	res->name = "PCI host bridge";
	res->parent = NULL;
	res->sibling = NULL;
	res->child = NULL;
}

void __init
prep_find_bridges(void)
{
	struct pci_controller* hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->pci_mem_offset = PREP_ISA_MEM_BASE;
	hose->io_base_virt = (void *)PREP_ISA_IO_BASE;
	prep_init_resource(&hose->io_resource, 0, 0x0fffffff, IORESOURCE_IO);
	prep_init_resource(&hose->mem_resources[0], 0xc0000000, 0xfeffffff,
			   IORESOURCE_MEM);
	
	printk("PReP architecture\n");
	{
#ifdef CONFIG_PREP_RESIDUAL	  
		PPC_DEVICE *hostbridge;

		hostbridge = residual_find_device(PROCESSORDEVICE, NULL,
			BridgeController, PCIBridge, -1, 0);
		if (hostbridge &&
			hostbridge->DeviceId.Interface == PCIBridgeIndirect) {
			PnP_TAG_PACKET * pkt;
			pkt = PnP_find_large_vendor_packet(
				res->DevicePnPHeap+hostbridge->AllocatedOffset,
				3, 0);
			if(pkt)
			{
#define p pkt->L4_Pack.L4_Data.L4_PPCPack
				setup_indirect_pci(hose, 
					ld_le32((unsigned *) (p.PPCData)),
					ld_le32((unsigned *) (p.PPCData+8)));
			}
			else
			{
				setup_indirect_pci(hose, 0x80000cf8, 0x80000cfc);
			}
		}
		else
#endif /* CONFIG_PREP_RESIDUAL */
		{
			hose->ops = &prep_pci_ops;
		}
	}

	ppc_md.pcibios_fixup = prep_pcibios_fixup;
}

