/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001 IBM
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
 * Send feedback to <greg@kroah.com>
 *
 */
#ifndef _CPQPHP_H
#define _CPQPHP_H

#include "pci_hotplug.h"
#include <linux/interrupt.h>
#include <asm/io.h>		/* for read? and write? functions */
#include <linux/delay.h>	/* for delays */

#if !defined(CONFIG_HOTPLUG_PCI_COMPAQ_MODULE)
	#define MY_NAME	"cpqphp.o"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(fmt, arg...) do { if (cpqhp_debug) printk(KERN_DEBUG "%s: " fmt , MY_NAME , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format , MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format , MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format , MY_NAME , ## arg)



struct smbios_system_slot {
	u8 type;
	u8 length;
	u16 handle;
	u8 name_string_num;
	u8 slot_type;
	u8 slot_width;
	u8 slot_current_usage;
	u8 slot_length;
	u16 slot_number;
	u8 properties1;
	u8 properties2;
} __attribute__ ((packed));

/* offsets to the smbios generic type based on the above structure layout */
enum smbios_system_slot_offsets {
	SMBIOS_SLOT_GENERIC_TYPE =	offsetof(struct smbios_system_slot, type),
	SMBIOS_SLOT_GENERIC_LENGTH =	offsetof(struct smbios_system_slot, length),
	SMBIOS_SLOT_GENERIC_HANDLE =	offsetof(struct smbios_system_slot, handle),
	SMBIOS_SLOT_NAME_STRING_NUM =	offsetof(struct smbios_system_slot, name_string_num),
	SMBIOS_SLOT_TYPE =		offsetof(struct smbios_system_slot, slot_type),
	SMBIOS_SLOT_WIDTH =		offsetof(struct smbios_system_slot, slot_width),
	SMBIOS_SLOT_CURRENT_USAGE =	offsetof(struct smbios_system_slot, slot_current_usage),
	SMBIOS_SLOT_LENGTH =		offsetof(struct smbios_system_slot, slot_length),
	SMBIOS_SLOT_NUMBER =		offsetof(struct smbios_system_slot, slot_number),
	SMBIOS_SLOT_PROPERTIES1 =	offsetof(struct smbios_system_slot, properties1),
	SMBIOS_SLOT_PROPERTIES2 =	offsetof(struct smbios_system_slot, properties2),
};

struct smbios_generic {
	u8 type;
	u8 length;
	u16 handle;
} __attribute__ ((packed));

/* offsets to the smbios generic type based on the above structure layout */
enum smbios_generic_offsets {
	SMBIOS_GENERIC_TYPE =	offsetof(struct smbios_generic, type),
	SMBIOS_GENERIC_LENGTH =	offsetof(struct smbios_generic, length),
	SMBIOS_GENERIC_HANDLE =	offsetof(struct smbios_generic, handle),
};

struct smbios_entry_point {
	char anchor[4];
	u8 ep_checksum;
	u8 ep_length;
	u8 major_version;
	u8 minor_version;
	u16 max_size_entry;
	u8 ep_rev;
	u8 reserved[5];
	char int_anchor[5];
	u8 int_checksum;
	u16 st_length;
	u32 st_address;
	u16 number_of_entrys;
	u8 bcd_rev;
} __attribute__ ((packed));

/* offsets to the smbios entry point based on the above structure layout */
enum smbios_entry_point_offsets {
	ANCHOR =		offsetof(struct smbios_entry_point, anchor[0]),
	EP_CHECKSUM =		offsetof(struct smbios_entry_point, ep_checksum),
	EP_LENGTH =		offsetof(struct smbios_entry_point, ep_length),
	MAJOR_VERSION =		offsetof(struct smbios_entry_point, major_version),
	MINOR_VERSION =		offsetof(struct smbios_entry_point, minor_version),
	MAX_SIZE_ENTRY =	offsetof(struct smbios_entry_point, max_size_entry),
	EP_REV =		offsetof(struct smbios_entry_point, ep_rev),
	INT_ANCHOR =		offsetof(struct smbios_entry_point, int_anchor[0]),
	INT_CHECKSUM =		offsetof(struct smbios_entry_point, int_checksum),
	ST_LENGTH =		offsetof(struct smbios_entry_point, st_length),
	ST_ADDRESS =		offsetof(struct smbios_entry_point, st_address),
	NUMBER_OF_ENTRYS =	offsetof(struct smbios_entry_point, number_of_entrys),
	BCD_REV =		offsetof(struct smbios_entry_point, bcd_rev),
};

struct ctrl_reg {			/* offset */
	u8	slot_RST;		/* 0x00 */
	u8	slot_enable;		/* 0x01 */
	u16	misc;			/* 0x02 */
	u32	led_control;		/* 0x04 */
	u32	int_input_clear;	/* 0x08 */
	u32	int_mask;		/* 0x0a */
	u8	reserved0;		/* 0x10 */
	u8	reserved1;		/* 0x11 */
	u8	reserved2;		/* 0x12 */
	u8	gen_output_AB;		/* 0x13 */
	u32	non_int_input;		/* 0x14 */
	u32	reserved3;		/* 0x18 */
	u32	reserved4;		/* 0x1a */
	u32	reserved5;		/* 0x20 */
	u8	reserved6;		/* 0x24 */
	u8	reserved7;		/* 0x25 */
	u16	reserved8;		/* 0x26 */
	u8	slot_mask;		/* 0x28 */
	u8	reserved9;		/* 0x29 */
	u8	reserved10;		/* 0x2a */
	u8	reserved11;		/* 0x2b */
	u8	slot_SERR;		/* 0x2c */
	u8	slot_power;		/* 0x2d */
	u8	reserved12;		/* 0x2e */
	u8	reserved13;		/* 0x2f */
	u8	next_curr_freq;		/* 0x30 */
	u8	reset_freq_mode;	/* 0x31 */
} __attribute__ ((packed));

/* offsets to the controller registers based on the above structure layout */
enum ctrl_offsets {
	SLOT_RST = 		offsetof(struct ctrl_reg, slot_RST),
	SLOT_ENABLE =		offsetof(struct ctrl_reg, slot_enable),
	MISC =			offsetof(struct ctrl_reg, misc),
	LED_CONTROL =		offsetof(struct ctrl_reg, led_control),
	INT_INPUT_CLEAR =	offsetof(struct ctrl_reg, int_input_clear),
	INT_MASK = 		offsetof(struct ctrl_reg, int_mask),
	CTRL_RESERVED0 = 	offsetof(struct ctrl_reg, reserved0),
	CTRL_RESERVED1 =	offsetof(struct ctrl_reg, reserved1),
	CTRL_RESERVED2 =	offsetof(struct ctrl_reg, reserved1),
	GEN_OUTPUT_AB = 	offsetof(struct ctrl_reg, gen_output_AB),
	NON_INT_INPUT = 	offsetof(struct ctrl_reg, non_int_input),
	CTRL_RESERVED3 =	offsetof(struct ctrl_reg, reserved3),
	CTRL_RESERVED4 =	offsetof(struct ctrl_reg, reserved4),
	CTRL_RESERVED5 =	offsetof(struct ctrl_reg, reserved5),
	CTRL_RESERVED6 =	offsetof(struct ctrl_reg, reserved6),
	CTRL_RESERVED7 =	offsetof(struct ctrl_reg, reserved7),
	CTRL_RESERVED8 =	offsetof(struct ctrl_reg, reserved8),
	SLOT_MASK = 		offsetof(struct ctrl_reg, slot_mask),
	CTRL_RESERVED9 = 	offsetof(struct ctrl_reg, reserved9),
	CTRL_RESERVED10 =	offsetof(struct ctrl_reg, reserved10),
	CTRL_RESERVED11 =	offsetof(struct ctrl_reg, reserved11),
	SLOT_SERR =		offsetof(struct ctrl_reg, slot_SERR),
	SLOT_POWER =		offsetof(struct ctrl_reg, slot_power),
	NEXT_CURR_FREQ =	offsetof(struct ctrl_reg, next_curr_freq),
	RESET_FREQ_MODE =	offsetof(struct ctrl_reg, reset_freq_mode),
};

struct hrt {
	char sig0;
	char sig1;
	char sig2;
	char sig3;
	u16 unused_IRQ;
	u16 PCIIRQ;
	u8 number_of_entries;
	u8 revision;
	u16 reserved1;
	u32 reserved2;
} __attribute__ ((packed));

/* offsets to the hotplug resource table registers based on the above structure layout */
enum hrt_offsets {
	SIG0 =			offsetof(struct hrt, sig0),
	SIG1 =			offsetof(struct hrt, sig1),
	SIG2 =			offsetof(struct hrt, sig2),
	SIG3 =			offsetof(struct hrt, sig3),
	UNUSED_IRQ =		offsetof(struct hrt, unused_IRQ),
	PCIIRQ =		offsetof(struct hrt, PCIIRQ),
	NUMBER_OF_ENTRIES =	offsetof(struct hrt, number_of_entries),
	REVISION =		offsetof(struct hrt, revision),
	HRT_RESERVED1 =		offsetof(struct hrt, reserved1),
	HRT_RESERVED2 =		offsetof(struct hrt, reserved2),
};

struct slot_rt {
	u8 dev_func;
	u8 primary_bus;
	u8 secondary_bus;
	u8 max_bus;
	u16 io_base;
	u16 io_length;
	u16 mem_base;
	u16 mem_length;
	u16 pre_mem_base;
	u16 pre_mem_length;
} __attribute__ ((packed));

/* offsets to the hotplug slot resource table registers based on the above structure layout */
enum slot_rt_offsets {
	DEV_FUNC =		offsetof(struct slot_rt, dev_func),
	PRIMARY_BUS = 		offsetof(struct slot_rt, primary_bus),
	SECONDARY_BUS = 	offsetof(struct slot_rt, secondary_bus),
	MAX_BUS = 		offsetof(struct slot_rt, max_bus),
	IO_BASE = 		offsetof(struct slot_rt, io_base),
	IO_LENGTH = 		offsetof(struct slot_rt, io_length),
	MEM_BASE = 		offsetof(struct slot_rt, mem_base),
	MEM_LENGTH = 		offsetof(struct slot_rt, mem_length),
	PRE_MEM_BASE = 		offsetof(struct slot_rt, pre_mem_base),
	PRE_MEM_LENGTH = 	offsetof(struct slot_rt, pre_mem_length),
};

struct pci_func {
	struct pci_func *next;
	u8 bus;
	u8 device;
	u8 function;
	u8 is_a_board;
	u16 status;
	u8 configured;
	u8 switch_save;
	u8 presence_save;
	u32 base_length[0x06];
	u8 base_type[0x06];
	u16 reserved2;
	u32 config_space[0x20];
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct timer_list *p_task_event;
	struct pci_dev* pci_dev;
};

#define SLOT_MAGIC	0x67267321
struct slot {
	u32 magic;
	struct slot *next;
	u8 bus;
	u8 device;
	u8 number;
	u8 is_a_board;
	u8 configured;
	u8 state;
	u8 switch_save;
	u8 presence_save;
	u32 capabilities;
	u16 reserved2;
	struct timer_list task_event;
	u8 hp_slot;
	struct controller *ctrl;
	void *p_sm_slot;
	struct hotplug_slot *hotplug_slot;
};

struct pci_resource {
	struct pci_resource * next;
	u32 base;
	u32 length;
};

struct event_info {
	u32 event_type;
	u8 hp_slot;
};

struct controller {
	struct controller *next;
	u32 ctrl_int_comp;
	struct semaphore crit_sect;	/* critical section semaphore */
	void *hpc_reg;			/* cookie for our pci controller location */
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct pci_dev *pci_dev;
	struct pci_bus *pci_bus;
	struct event_info event_queue[10];
	struct slot *slot;
	u8 next_event;
	u8 interrupt;
	u8 cfgspc_irq;
	u8 bus;				/* bus number for the pci hotplug controller */
	u8 rev;
	u8 slot_device_offset;
	u8 first_slot;
	u8 add_support;
	u8 push_flag;
	enum pci_bus_speed speed;
	enum pci_bus_speed speed_capability;
	u8 push_button;			/* 0 = no pushbutton, 1 = pushbutton present */
	u8 slot_switch_type;		/* 0 = no switch, 1 = switch present */
	u8 defeature_PHP;		/* 0 = PHP not supported, 1 = PHP supported */
	u8 alternate_base_address;	/* 0 = not supported, 1 = supported */
	u8 pci_config_space;		/* Index/data access to working registers 0 = not supported, 1 = supported */
	u8 pcix_speed_capability;	/* PCI-X */
	u8 pcix_support;		/* PCI-X */
	u16 vendor_id;
	struct work_struct int_task_event;
	wait_queue_head_t queue;	/* sleep & wake process */
};

struct irq_mapping {
	u8 barber_pole;
	u8 valid_INT;
	u8 interrupt[4];
};

struct resource_lists {
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct irq_mapping *irqs;
};

#define ROM_PHY_ADDR			0x0F0000
#define ROM_PHY_LEN			0x00ffff

#define PCI_HPC_ID			0xA0F7
#define PCI_SUB_HPC_ID			0xA2F7
#define PCI_SUB_HPC_ID2			0xA2F8
#define PCI_SUB_HPC_ID3			0xA2F9
#define PCI_SUB_HPC_ID_INTC		0xA2FA
#define PCI_SUB_HPC_ID4			0xA2FD

#define INT_BUTTON_IGNORE		0
#define INT_PRESENCE_ON			1
#define INT_PRESENCE_OFF		2
#define INT_SWITCH_CLOSE		3
#define INT_SWITCH_OPEN			4
#define INT_POWER_FAULT			5
#define INT_POWER_FAULT_CLEAR		6
#define INT_BUTTON_PRESS		7
#define INT_BUTTON_RELEASE		8
#define INT_BUTTON_CANCEL		9

#define STATIC_STATE			0
#define BLINKINGON_STATE		1
#define BLINKINGOFF_STATE		2
#define POWERON_STATE			3
#define POWEROFF_STATE			4

#define PCISLOT_INTERLOCK_CLOSED	0x00000001
#define PCISLOT_ADAPTER_PRESENT		0x00000002
#define PCISLOT_POWERED			0x00000004
#define PCISLOT_66_MHZ_OPERATION	0x00000008
#define PCISLOT_64_BIT_OPERATION	0x00000010
#define PCISLOT_REPLACE_SUPPORTED	0x00000020
#define PCISLOT_ADD_SUPPORTED		0x00000040
#define PCISLOT_INTERLOCK_SUPPORTED	0x00000080
#define PCISLOT_66_MHZ_SUPPORTED	0x00000100
#define PCISLOT_64_BIT_SUPPORTED	0x00000200



#define PCI_TO_PCI_BRIDGE_CLASS		0x00060400


#define INTERLOCK_OPEN			0x00000002
#define ADD_NOT_SUPPORTED		0x00000003
#define CARD_FUNCTIONING		0x00000005
#define ADAPTER_NOT_SAME		0x00000006
#define NO_ADAPTER_PRESENT		0x00000009
#define NOT_ENOUGH_RESOURCES		0x0000000B
#define DEVICE_TYPE_NOT_SUPPORTED	0x0000000C
#define POWER_FAILURE			0x0000000E

#define REMOVE_NOT_SUPPORTED		0x00000003


/*
 * error Messages
 */
#define msg_initialization_err	"Initialization failure, error=%d\n"
#define msg_HPC_rev_error	"Unsupported revision of the PCI hot plug controller found.\n"
#define msg_HPC_non_compaq_or_intel	"The PCI hot plug controller is not supported by this driver.\n"
#define msg_HPC_not_supported	"this system is not supported by this version of cpqphpd. Upgrade to a newer version of cpqphpd\n"
#define msg_unable_to_save	"unable to store PCI hot plug add resource information. This system must be rebooted before adding any PCI devices.\n"
#define msg_button_on		"PCI slot #%d - powering on due to button press.\n"
#define msg_button_off		"PCI slot #%d - powering off due to button press.\n"
#define msg_button_cancel	"PCI slot #%d - action canceled due to button press.\n"
#define msg_button_ignore	"PCI slot #%d - button press ignored.  (action in progress...)\n"


/* sysfs functions for the hotplug controller info */
extern void cpqhp_create_ctrl_files		(struct controller *ctrl);

/* controller functions */
extern void	cpqhp_pushbutton_thread		(unsigned long event_pointer);
extern irqreturn_t cpqhp_ctrl_intr		(int IRQ, void *data, struct pt_regs *regs);
extern int	cpqhp_find_available_resources	(struct controller *ctrl, void *rom_start);
extern int	cpqhp_event_start_thread	(void);
extern void	cpqhp_event_stop_thread		(void);
extern struct pci_func *cpqhp_slot_create	(unsigned char busnumber);
extern struct pci_func *cpqhp_slot_find		(unsigned char bus, unsigned char device, unsigned char index);
extern int	cpqhp_process_SI		(struct controller *ctrl, struct pci_func *func);
extern int	cpqhp_process_SS		(struct controller *ctrl, struct pci_func *func);
extern int	cpqhp_hardware_test		(struct controller *ctrl, int test_num);

/* resource functions */
extern int	cpqhp_resource_sort_and_combine	(struct pci_resource **head);

/* pci functions */
extern int	cpqhp_set_irq			(u8 bus_num, u8 dev_num, u8 int_pin, u8 irq_num);
extern int	cpqhp_get_bus_dev		(struct controller *ctrl, u8 *bus_num, u8 *dev_num, u8 slot);
extern int	cpqhp_save_config		(struct controller *ctrl, int busnumber, int is_hot_plug);
extern int	cpqhp_save_base_addr_length	(struct controller *ctrl, struct pci_func * func);
extern int	cpqhp_save_used_resources	(struct controller *ctrl, struct pci_func * func);
extern int	cpqhp_configure_board		(struct controller *ctrl, struct pci_func * func);
extern int	cpqhp_save_slot_config		(struct controller *ctrl, struct pci_func * new_slot);
extern int	cpqhp_valid_replace		(struct controller *ctrl, struct pci_func * func);
extern void	cpqhp_destroy_board_resources	(struct pci_func * func);
extern int	cpqhp_return_board_resources	(struct pci_func * func, struct resource_lists * resources);
extern void	cpqhp_destroy_resource_list	(struct resource_lists * resources);
extern int	cpqhp_configure_device		(struct controller* ctrl, struct pci_func* func);
extern int	cpqhp_unconfigure_device	(struct pci_func* func);
extern struct slot *cpqhp_find_slot		(struct controller *ctrl, u8 device);

/* Global variables */
extern int cpqhp_debug;
extern struct controller *cpqhp_ctrl_list;
extern struct pci_func *cpqhp_slot_list[256];

/* these can be gotten rid of, but for debugging they are purty */
extern u8 cpqhp_nic_irq;
extern u8 cpqhp_disk_irq;



/* inline functions */


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int slot_paranoia_check (struct slot *slot, const char *function)
{
	if (!slot) {
		dbg("%s - slot == NULL", function);
		return -1;
	}
	if (slot->magic != SLOT_MAGIC) {
		dbg("%s - bad magic number for slot", function);
		return -1;
	}
	if (!slot->hotplug_slot) {
		dbg("%s - slot->hotplug_slot == NULL!", function);
		return -1;
	}
	return 0;
}

static inline struct slot *get_slot (struct hotplug_slot *hotplug_slot, const char *function)
{ 
	struct slot *slot;

	if (!hotplug_slot) {
		dbg("%s - hotplug_slot == NULL\n", function);
		return NULL;
	}

	slot = (struct slot *)hotplug_slot->private;
	if (slot_paranoia_check (slot, function))
                return NULL;
	return slot;
}               

/*
 * return_resource
 *
 * Puts node back in the resource list pointed to by head
 *
 */
static inline void return_resource (struct pci_resource **head, struct pci_resource *node)
{
	if (!node || !head)
		return;
	node->next = *head;
	*head = node;
}

static inline void set_SOGO (struct controller *ctrl)
{
	u16 misc;
	
	misc = readw(ctrl->hpc_reg + MISC);
	misc = (misc | 0x0001) & 0xFFFB;
	writew(misc, ctrl->hpc_reg + MISC);
}


static inline void amber_LED_on (struct controller *ctrl, u8 slot)
{
	u32 led_control;
	
	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control |= (0x01010000L << slot);
	writel(led_control, ctrl->hpc_reg + LED_CONTROL);
}


static inline void amber_LED_off (struct controller *ctrl, u8 slot)
{
	u32 led_control;
	
	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control &= ~(0x01010000L << slot);
	writel(led_control, ctrl->hpc_reg + LED_CONTROL);
}


static inline int read_amber_LED (struct controller *ctrl, u8 slot)
{
	u32 led_control;

	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control &= (0x01010000L << slot);
	
	return led_control ? 1 : 0;
}


static inline void green_LED_on (struct controller *ctrl, u8 slot)
{
	u32 led_control;
	
	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control |= 0x0101L << slot;
	writel(led_control, ctrl->hpc_reg + LED_CONTROL);
}

static inline void green_LED_off (struct controller *ctrl, u8 slot)
{
	u32 led_control;
	
	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control &= ~(0x0101L << slot);
	writel(led_control, ctrl->hpc_reg + LED_CONTROL);
}


static inline void green_LED_blink (struct controller *ctrl, u8 slot)
{
	u32 led_control;
	
	led_control = readl(ctrl->hpc_reg + LED_CONTROL);
	led_control &= ~(0x0101L << slot);
	led_control |= (0x0001L << slot);
	writel(led_control, ctrl->hpc_reg + LED_CONTROL);
}


static inline void slot_disable (struct controller *ctrl, u8 slot)
{
	u8 slot_enable;

	slot_enable = readb(ctrl->hpc_reg + SLOT_ENABLE);
	slot_enable &= ~(0x01 << slot);
	writeb(slot_enable, ctrl->hpc_reg + SLOT_ENABLE);
}


static inline void slot_enable (struct controller *ctrl, u8 slot)
{
	u8 slot_enable;

	slot_enable = readb(ctrl->hpc_reg + SLOT_ENABLE);
	slot_enable |= (0x01 << slot);
	writeb(slot_enable, ctrl->hpc_reg + SLOT_ENABLE);
}


static inline u8 is_slot_enabled (struct controller *ctrl, u8 slot)
{
	u8 slot_enable;

	slot_enable = readb(ctrl->hpc_reg + SLOT_ENABLE);
	slot_enable &= (0x01 << slot);
	return slot_enable ? 1 : 0;
}


static inline u8 read_slot_enable (struct controller *ctrl)
{
	return readb(ctrl->hpc_reg + SLOT_ENABLE);
}


/*
 * get_controller_speed - find the current frequency/mode of controller.
 *
 * @ctrl: controller to get frequency/mode for.
 *
 * Returns controller speed.
 *
 */
static inline u8 get_controller_speed (struct controller *ctrl)
{
	u8 curr_freq;
 	u16 misc;
 	
	if (ctrl->pcix_support) {
		curr_freq = readb(ctrl->hpc_reg + NEXT_CURR_FREQ);
		if ((curr_freq & 0xB0) == 0xB0) 
			return PCI_SPEED_133MHz_PCIX;
		if ((curr_freq & 0xA0) == 0xA0)
			return PCI_SPEED_100MHz_PCIX;
		if ((curr_freq & 0x90) == 0x90)
			return PCI_SPEED_66MHz_PCIX;
		if (curr_freq & 0x10)
			return PCI_SPEED_66MHz;

		return PCI_SPEED_33MHz;
	}

 	misc = readw(ctrl->hpc_reg + MISC);
 	return (misc & 0x0800) ? PCI_SPEED_66MHz : PCI_SPEED_33MHz;
}
 

/*
 * get_adapter_speed - find the max supported frequency/mode of adapter.
 *
 * @ctrl: hotplug controller.
 * @hp_slot: hotplug slot where adapter is installed.
 *
 * Returns adapter speed.
 *
 */
static inline u8 get_adapter_speed (struct controller *ctrl, u8 hp_slot)
{
	u32 temp_dword = readl(ctrl->hpc_reg + NON_INT_INPUT);
	dbg("slot: %d, PCIXCAP: %8x\n", hp_slot, temp_dword);
	if (ctrl->pcix_support) {
		if (temp_dword & (0x10000 << hp_slot))
			return PCI_SPEED_133MHz_PCIX;
		if (temp_dword & (0x100 << hp_slot))
			return PCI_SPEED_66MHz_PCIX;
	}

	if (temp_dword & (0x01 << hp_slot))
		return PCI_SPEED_66MHz;

	return PCI_SPEED_33MHz;
}

static inline void enable_slot_power (struct controller *ctrl, u8 slot)
{
	u8 slot_power;

	slot_power = readb(ctrl->hpc_reg + SLOT_POWER);
	slot_power |= (0x01 << slot);
	writeb(slot_power, ctrl->hpc_reg + SLOT_POWER);
}

static inline void disable_slot_power (struct controller *ctrl, u8 slot)
{
	u8 slot_power;

	slot_power = readb(ctrl->hpc_reg + SLOT_POWER);
	slot_power &= ~(0x01 << slot);
	writeb(slot_power, ctrl->hpc_reg + SLOT_POWER);
}


static inline int cpq_get_attention_status (struct controller *ctrl, struct slot *slot)
{
	u8 hp_slot;

	if (slot == NULL)
		return 1;

	hp_slot = slot->device - ctrl->slot_device_offset;

	return read_amber_LED (ctrl, hp_slot);
}


static inline int get_slot_enabled (struct controller *ctrl, struct slot *slot)
{
	u8 hp_slot;

	if (slot == NULL)
		return 1;

	hp_slot = slot->device - ctrl->slot_device_offset;

	return is_slot_enabled (ctrl, hp_slot);
}


static inline int cpq_get_latch_status (struct controller *ctrl, struct slot *slot)
{
	u32 status;
	u8 hp_slot;

	if (slot == NULL)
		return 1;

	hp_slot = slot->device - ctrl->slot_device_offset;
	dbg("%s: slot->device = %d, ctrl->slot_device_offset = %d \n",
	    __FUNCTION__, slot->device, ctrl->slot_device_offset);

	status = (readl(ctrl->hpc_reg + INT_INPUT_CLEAR) & (0x01L << hp_slot));

	return(status == 0) ? 1 : 0;
}


static inline int get_presence_status (struct controller *ctrl, struct slot *slot)
{
	int presence_save = 0;
	u8 hp_slot;
	u32 tempdword;

	if (slot == NULL)
		return 0;

	hp_slot = slot->device - ctrl->slot_device_offset;

	tempdword = readl(ctrl->hpc_reg + INT_INPUT_CLEAR);
	presence_save = (int) ((((~tempdword) >> 23) | ((~tempdword) >> 15)) >> hp_slot) & 0x02;

	return presence_save;
}

#define SLOT_NAME_SIZE 10

static inline void make_slot_name (char *buffer, int buffer_size, struct slot *slot)
{
	snprintf (buffer, buffer_size, "%d", slot->number);
}


static inline int wait_for_ctrl_irq (struct controller *ctrl)
{
        DECLARE_WAITQUEUE(wait, current);
	int retval = 0;

	dbg("%s - start\n", __FUNCTION__);
	add_wait_queue(&ctrl->queue, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	/* Sleep for up to 1 second to wait for the LED to change. */
	schedule_timeout(1*HZ);
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ctrl->queue, &wait);
	if (signal_pending(current))
		retval =  -EINTR;

	dbg("%s - end\n", __FUNCTION__);
	return retval;
}


/**
 * set_controller_speed - set the frequency and/or mode of a specific
 * controller segment.
 *
 * @ctrl: controller to change frequency/mode for.
 * @adapter_speed: the speed of the adapter we want to match.
 * @hp_slot: the slot number where the adapter is installed.
 *
 * Returns 0 if we successfully change frequency and/or mode to match the
 * adapter speed.
 * 
 */
static inline u8 set_controller_speed(struct controller *ctrl, u8 adapter_speed, u8 hp_slot)
{
	struct slot *slot;
	u8 reg;
	u8 slot_power = readb(ctrl->hpc_reg + SLOT_POWER);
	u16 reg16;
	u32 leds = readl(ctrl->hpc_reg + LED_CONTROL);
	
	if (ctrl->speed == adapter_speed)
		return 0;
	
	/* We don't allow freq/mode changes if we find another adapter running
	 * in another slot on this controller */
	for(slot = ctrl->slot; slot; slot = slot->next) {
		if (slot->device == (hp_slot + ctrl->slot_device_offset)) 
			continue;
		if (!slot->hotplug_slot && !slot->hotplug_slot->info) 
			continue;
		if (slot->hotplug_slot->info->adapter_status == 0) 
			continue;
		/* If another adapter is running on the same segment but at a
		 * lower speed/mode, we allow the new adapter to function at
		 * this rate if supported */
		if (ctrl->speed < adapter_speed) 
			return 0;

		return 1;
	}
	
	/* If the controller doesn't support freq/mode changes and the
	 * controller is running at a higher mode, we bail */
	if ((ctrl->speed > adapter_speed) && (!ctrl->pcix_speed_capability))
		return 1;
	
	/* But we allow the adapter to run at a lower rate if possible */
	if ((ctrl->speed < adapter_speed) && (!ctrl->pcix_speed_capability))
		return 0;

	/* We try to set the max speed supported by both the adapter and
	 * controller */
	if (ctrl->speed_capability < adapter_speed) {
		if (ctrl->speed == ctrl->speed_capability)
			return 0;
		adapter_speed = ctrl->speed_capability;
	}

	writel(0x0L, ctrl->hpc_reg + LED_CONTROL);
	writeb(0x00, ctrl->hpc_reg + SLOT_ENABLE);
	
	set_SOGO(ctrl); 
	wait_for_ctrl_irq(ctrl);
	
	if (adapter_speed != PCI_SPEED_133MHz_PCIX)
		reg = 0xF5;
	else
		reg = 0xF4;	
	pci_write_config_byte(ctrl->pci_dev, 0x41, reg);
	
	reg16 = readw(ctrl->hpc_reg + NEXT_CURR_FREQ);
	reg16 &= ~0x000F;
	switch(adapter_speed) {
		case(PCI_SPEED_133MHz_PCIX): 
			reg = 0x75;
			reg16 |= 0xB; 
			break;
		case(PCI_SPEED_100MHz_PCIX):
			reg = 0x74;
			reg16 |= 0xA;
			break;
		case(PCI_SPEED_66MHz_PCIX):
			reg = 0x73;
			reg16 |= 0x9;
			break;
		case(PCI_SPEED_66MHz):
			reg = 0x73;
			reg16 |= 0x1;
			break;
		default: /* 33MHz PCI 2.2 */
			reg = 0x71;
			break;
			
	}
	reg16 |= 0xB << 12;
	writew(reg16, ctrl->hpc_reg + NEXT_CURR_FREQ);
	
	mdelay(5); 
	
	/* Reenable interrupts */
	writel(0, ctrl->hpc_reg + INT_MASK);

	pci_write_config_byte(ctrl->pci_dev, 0x41, reg); 
	
	/* Restart state machine */
	reg = ~0xF;
	pci_read_config_byte(ctrl->pci_dev, 0x43, &reg);
	pci_write_config_byte(ctrl->pci_dev, 0x43, reg);
	
	/* Only if mode change...*/
	if (((ctrl->speed == PCI_SPEED_66MHz) && (adapter_speed == PCI_SPEED_66MHz_PCIX)) ||
		((ctrl->speed == PCI_SPEED_66MHz_PCIX) && (adapter_speed == PCI_SPEED_66MHz))) 
			set_SOGO(ctrl);
	
	wait_for_ctrl_irq(ctrl);
	mdelay(1100);
	
	/* Restore LED/Slot state */
	writel(leds, ctrl->hpc_reg + LED_CONTROL);
	writeb(slot_power, ctrl->hpc_reg + SLOT_ENABLE);
	
	set_SOGO(ctrl);
	wait_for_ctrl_irq(ctrl);

	ctrl->speed = adapter_speed;
	slot = cpqhp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	info("Successfully changed frequency/mode for adapter in slot %d\n", 
			slot->number);
	return 0;
}

#endif

