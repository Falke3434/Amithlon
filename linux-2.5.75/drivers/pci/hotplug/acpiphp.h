/*
 * ACPI PCI Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001 IBM Corp.
 * Copyright (c) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (c) 2002,2003 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (c) 2002,2003 NEC Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <gregkh@us.ibm.com>,
 *		    <t-kochi@bq.jp.nec.com>
 *
 */

#ifndef _ACPIPHP_H
#define _ACPIPHP_H

#include <linux/acpi.h>
#include <linux/kobject.h>	/* for KOBJ_NAME_LEN */
#include "pci_hotplug.h"

#define dbg(format, arg...)					\
	do {							\
		if (acpiphp_debug)				\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME , ## arg)

#define SLOT_MAGIC	0x67267322
/* name size which is used for entries in pcihpfs */
#define SLOT_NAME_SIZE	KOBJ_NAME_LEN		/* {_SUN} */

struct acpiphp_bridge;
struct acpiphp_slot;
struct pci_resource;

/*
 * struct slot - slot information for each *physical* slot
 */
struct slot {
	u32 magic;
	u8 number;
	struct hotplug_slot	*hotplug_slot;
	struct list_head	slot_list;

	struct acpiphp_slot	*acpi_slot;
};

/*
 * struct pci_resource - describes pci resource (mem, pfmem, io, bus)
 */
struct pci_resource {
	struct pci_resource * next;
	u64 base;
	u32 length;
};

/**
 * struct hpp_param - ACPI 2.0 _HPP Hot Plug Parameters
 * @cache_line_size in DWORD
 * @latency_timer in PCI clock
 * @enable_SERR 0 or 1
 * @enable_PERR 0 or 1
 */
struct hpp_param {
	u8 cache_line_size;
	u8 latency_timer;
	u8 enable_SERR;
	u8 enable_PERR;
};


/**
 * struct acpiphp_bridge - PCI bridge information
 *
 * for each bridge device in ACPI namespace
 */
struct acpiphp_bridge {
	struct list_head list;
	acpi_handle handle;
	struct acpiphp_slot *slots;
	int type;
	int nr_slots;

	u8 seg;
	u8 bus;
	u8 sub;

	u32 flags;

	/* This bus (host bridge) or Secondary bus (PCI-to-PCI bridge) */
	struct pci_bus *pci_bus;

	/* PCI-to-PCI bridge device */
	struct pci_dev *pci_dev;

	/* ACPI 2.0 _HPP parameters */
	struct hpp_param hpp;

	spinlock_t res_lock;

	/* available resources on this bus */
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
};


/**
 * struct acpiphp_slot - PCI slot information
 *
 * PCI slot information for each *physical* PCI slot
 */
struct acpiphp_slot {
	struct acpiphp_slot *next;
	struct acpiphp_bridge *bridge;	/* parent */
	struct list_head funcs;		/* one slot may have different
					   objects (i.e. for each function) */
	struct semaphore crit_sect;

	u32		id;		/* slot id (serial #) for hotplug core */
	u8		device;		/* pci device# */

	u32		sun;		/* ACPI _SUN (slot unique number) */
	u32		slotno;		/* slot number relative to bridge */
	u32		flags;		/* see below */
};


/**
 * struct acpiphp_func - PCI function information
 *
 * PCI function information for each object in ACPI namespace
 * typically 8 objects per slot (i.e. for each PCI function)
 */
struct acpiphp_func {
	struct acpiphp_slot *slot;	/* parent */

	struct list_head sibling;
	struct pci_dev *pci_dev;

	acpi_handle	handle;

	u8		function;	/* pci function# */
	u32		flags;		/* see below */

	/* resources used for this function */
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
};


/* PCI bus bridge HID */
#define ACPI_PCI_HOST_HID		"PNP0A03"

/* PCI BRIDGE type */
#define BRIDGE_TYPE_HOST		0
#define BRIDGE_TYPE_P2P			1

/* ACPI _STA method value (ignore bit 4; battery present) */
#define ACPI_STA_PRESENT		(0x00000001)
#define ACPI_STA_ENABLED		(0x00000002)
#define ACPI_STA_SHOW_IN_UI		(0x00000004)
#define ACPI_STA_FUNCTIONING		(0x00000008)
#define ACPI_STA_ALL			(0x0000000f)

/* bridge flags */
#define BRIDGE_HAS_STA		(0x00000001)
#define BRIDGE_HAS_EJ0		(0x00000002)
#define BRIDGE_HAS_HPP		(0x00000004)
#define BRIDGE_HAS_PS0		(0x00000010)
#define BRIDGE_HAS_PS1		(0x00000020)
#define BRIDGE_HAS_PS2		(0x00000040)
#define BRIDGE_HAS_PS3		(0x00000080)

/* slot flags */

#define SLOT_POWEREDON		(0x00000001)
#define SLOT_ENABLED		(0x00000002)
#define SLOT_MULTIFUNCTION	(x000000004)

/* function flags */

#define FUNC_HAS_STA		(0x00000001)
#define FUNC_HAS_EJ0		(0x00000002)
#define FUNC_HAS_PS0		(0x00000010)
#define FUNC_HAS_PS1		(0x00000020)
#define FUNC_HAS_PS2		(0x00000040)
#define FUNC_HAS_PS3		(0x00000080)

#define FUNC_EXISTS		(0x10000000) /* to make sure we call _EJ0 only for existing funcs */

/* function prototypes */

/* acpiphp_glue.c */
extern int acpiphp_glue_init (void);
extern void acpiphp_glue_exit (void);
extern int acpiphp_get_num_slots (void);
extern struct acpiphp_slot *get_slot_from_id (int id);
typedef int (*acpiphp_callback)(struct acpiphp_slot *slot, void *data);
extern int acpiphp_for_each_slot (acpiphp_callback fn, void *data);

extern int acpiphp_check_bridge (struct acpiphp_bridge *bridge);
extern int acpiphp_enable_slot (struct acpiphp_slot *slot);
extern int acpiphp_disable_slot (struct acpiphp_slot *slot);
extern u8 acpiphp_get_power_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_attention_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_latch_status (struct acpiphp_slot *slot);
extern u8 acpiphp_get_adapter_status (struct acpiphp_slot *slot);

/* acpiphp_pci.c */
extern struct pci_dev *acpiphp_allocate_pcidev (struct pci_bus *pbus, int dev, int fn);
extern int acpiphp_configure_slot (struct acpiphp_slot *slot);
extern int acpiphp_configure_function (struct acpiphp_func *func);
extern int acpiphp_unconfigure_function (struct acpiphp_func *func);
extern int acpiphp_detect_pci_resource (struct acpiphp_bridge *bridge);
extern int acpiphp_init_func_resource (struct acpiphp_func *func);

/* acpiphp_res.c */
extern struct pci_resource *acpiphp_get_io_resource (struct pci_resource **head, u32 size);
extern struct pci_resource *acpiphp_get_max_resource (struct pci_resource **head, u32 size);
extern struct pci_resource *acpiphp_get_resource (struct pci_resource **head, u32 size);
extern struct pci_resource *acpiphp_get_resource_with_base (struct pci_resource **head, u64 base, u32 size);
extern int acpiphp_resource_sort_and_combine (struct pci_resource **head);
extern struct pci_resource *acpiphp_make_resource (u64 base, u32 length);
extern void acpiphp_move_resource (struct pci_resource **from, struct pci_resource **to);
extern void acpiphp_free_resource (struct pci_resource **res);
extern void acpiphp_dump_resource (struct acpiphp_bridge *bridge); /* debug */
extern void acpiphp_dump_func_resource (struct acpiphp_func *func); /* debug */

/* variables */
extern int acpiphp_debug;

#endif /* _ACPIPHP_H */
