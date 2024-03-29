/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Chuck Cole, Jyoti Shah, Tong Yu, Irene Zubarev, IBM Corporation
 *
 * Copyright (c) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001,2002 IBM Corp.
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
 * Send feedback to <gregkh@us.ibm.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>
#include "../../arch/i386/kernel/pci-i386.h"	/* for struct irq_routing_table */
#include "ibmphp.h"

#define attn_on(sl)  ibmphp_hpc_writeslot (sl, HPC_SLOT_ATTNON)
#define attn_off(sl) ibmphp_hpc_writeslot (sl, HPC_SLOT_ATTNOFF)
#define attn_LED_blink(sl) ibmphp_hpc_writeslot (sl, HPC_SLOT_BLINKLED)
#define get_ctrl_revision(sl, rev) ibmphp_hpc_readslot (sl, READ_REVLEVEL, rev)
#define get_hpc_options(sl, opt) ibmphp_hpc_readslot (sl, READ_HPCOPTIONS, opt)

#define DRIVER_VERSION	"0.1"
#define DRIVER_DESC	"IBM Hot Plug PCI Controller Driver"

int ibmphp_debug;

static int debug;
MODULE_PARM (debug, "i");
MODULE_PARM_DESC (debug, "Debugging mode enabled or not");
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION (DRIVER_DESC);

static int *ops[MAX_OPS + 1];
static struct pci_ops *ibmphp_pci_root_ops;
static int max_slots;

static int irqs[16];    /* PIC mode IRQ's we're using so far (in case MPS tables don't provide default info for empty slots */

static int init_flag;

/*
static int get_max_adapter_speed_1 (struct hotplug_slot *, u8 *, u8);

static inline int get_max_adapter_speed (struct hotplug_slot *hs, u8 *value)
{
	return get_max_adapter_speed_1 (hs, value, 1);
}
*/
static inline int get_cur_bus_info (struct slot **sl) 
{
	int rc = 1;
	struct slot * slot_cur = *sl;

	debug ("options = %x\n", slot_cur->ctrl->options);
	debug ("revision = %x\n", slot_cur->ctrl->revision);	

	if (READ_BUS_STATUS (slot_cur->ctrl)) 
		rc = ibmphp_hpc_readslot (slot_cur, READ_BUSSTATUS, NULL);
	
	if (rc) 
		return rc;
	  
	slot_cur->bus_on->current_speed = CURRENT_BUS_SPEED (slot_cur->busstatus);
	if (READ_BUS_MODE (slot_cur->ctrl))
		slot_cur->bus_on->current_bus_mode = CURRENT_BUS_MODE (slot_cur->busstatus);

	debug ("busstatus = %x, bus_speed = %x, bus_mode = %x\n", slot_cur->busstatus, slot_cur->bus_on->current_speed, slot_cur->bus_on->current_bus_mode);
	
	*sl = slot_cur;
	return 0;
}

static inline int slot_update (struct slot **sl)
{
	int rc;
 	rc = ibmphp_hpc_readslot (*sl, READ_ALLSTAT, NULL);
	if (rc) 
		return rc;
	if (!init_flag)
		return get_cur_bus_info (sl);
	return rc;
}

static int get_max_slots (void)
{
	struct list_head * tmp;
	int slot_count = 0;

	list_for_each (tmp, &ibmphp_slot_head) 
		++slot_count;
	return slot_count;
}

/* This routine will put the correct slot->device information per slot.  It's
 * called from initialization of the slot structures. It will also assign
 * interrupt numbers per each slot.
 * Parameters: struct slot
 * Returns 0 or errors
 */
int ibmphp_init_devno (struct slot **cur_slot)
{
	struct irq_routing_table *rtable;
	int len;
	int loop;
	int i;

	rtable = pcibios_get_irq_routing_table ();
	if (!rtable) {
		err ("no BIOS routing table...\n");
		return -ENOMEM;
	}

	len = (rtable->size - sizeof (struct irq_routing_table)) / sizeof (struct irq_info);

	if (!len)
		return -1;
	for (loop = 0; loop < len; loop++) {
		if ((*cur_slot)->number == rtable->slots[loop].slot) {
		if ((*cur_slot)->bus == rtable->slots[loop].bus) {
			(*cur_slot)->device = PCI_SLOT (rtable->slots[loop].devfn);
			for (i = 0; i < 4; i++)
				(*cur_slot)->irq[i] = IO_APIC_get_PCI_irq_vector ((int) (*cur_slot)->bus, (int) (*cur_slot)->device, i);

				debug ("(*cur_slot)->irq[0] = %x\n", (*cur_slot)->irq[0]);
				debug ("(*cur_slot)->irq[1] = %x\n", (*cur_slot)->irq[1]);
				debug ("(*cur_slot)->irq[2] = %x\n", (*cur_slot)->irq[2]);
				debug ("(*cur_slot)->irq[3] = %x\n", (*cur_slot)->irq[3]);

				debug ("rtable->exlusive_irqs = %x\n", rtable->exclusive_irqs);
				debug ("rtable->slots[loop].irq[0].bitmap = %x\n", rtable->slots[loop].irq[0].bitmap);
				debug ("rtable->slots[loop].irq[1].bitmap = %x\n", rtable->slots[loop].irq[1].bitmap);
				debug ("rtable->slots[loop].irq[2].bitmap = %x\n", rtable->slots[loop].irq[2].bitmap);
				debug ("rtable->slots[loop].irq[3].bitmap = %x\n", rtable->slots[loop].irq[3].bitmap);

				debug ("rtable->slots[loop].irq[0].link= %x\n", rtable->slots[loop].irq[0].link);
				debug ("rtable->slots[loop].irq[1].link = %x\n", rtable->slots[loop].irq[1].link);
				debug ("rtable->slots[loop].irq[2].link = %x\n", rtable->slots[loop].irq[2].link);
				debug ("rtable->slots[loop].irq[3].link = %x\n", rtable->slots[loop].irq[3].link);
				debug ("end of init_devno\n");
				return 0;
			}
		}
	}

	return -1;
}

static inline int power_on (struct slot *slot_cur)
{
	u8 cmd = HPC_SLOT_ON;
	int retval;

	retval = ibmphp_hpc_writeslot (slot_cur, cmd);
	if (retval) {
		err ("power on failed\n");
		return retval;
	}
	if (CTLR_RESULT (slot_cur->ctrl->status)) {
		err ("command not completed successfully in power_on \n");
		return -EIO;
	}
	long_delay (3 * HZ); /* For ServeRAID cards, and some 66 PCI */
	return 0;
}

static inline int power_off (struct slot *slot_cur)
{
	u8 cmd = HPC_SLOT_OFF;
	int retval;

	retval = ibmphp_hpc_writeslot (slot_cur, cmd);
	if (retval) {
		err ("power off failed \n");
		return retval;
	}
	if (CTLR_RESULT (slot_cur->ctrl->status)) {
		err ("command not completed successfully in power_off \n");
		return -EIO;
	}
	return 0;
}

static int set_attention_status (struct hotplug_slot *hotplug_slot, u8 value)
{
	int rc = 0;
	struct slot *pslot;
	u8 cmd;
	int hpcrc = 0;

	debug ("set_attention_status - Entry hotplug_slot[%lx] value[%x]\n", (ulong) hotplug_slot, value);
	ibmphp_lock_operations ();
	cmd = 0x00;     // avoid compiler warning

	if (hotplug_slot) {
		switch (value) {
		case HPC_SLOT_ATTN_OFF:
			cmd = HPC_SLOT_ATTNOFF;
			break;
		case HPC_SLOT_ATTN_ON:
			cmd = HPC_SLOT_ATTNON;
			break;
		case HPC_SLOT_ATTN_BLINK:
			cmd = HPC_SLOT_BLINKLED;
			break;
		default:
			rc = -ENODEV;
			err ("set_attention_status - Error : invalid input [%x]\n", value);
			break;
		}
		if (rc == 0) {
			pslot = (struct slot *) hotplug_slot->private;
			if (pslot)
				hpcrc = ibmphp_hpc_writeslot (pslot, cmd);
			else
				rc = -ENODEV;
		}
	} else	
		rc = -ENODEV;

	if (hpcrc)
		rc = hpcrc;

	ibmphp_unlock_operations ();

	debug ("set_attention_status - Exit rc[%d]\n", rc);
	return rc;
}

static int get_attention_status (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	int hpcrc = 0;
	struct slot myslot;

	debug ("get_attention_status - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong) hotplug_slot, (ulong) value);
        
	ibmphp_lock_operations ();
	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
			hpcrc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &(myslot.status));
			if (!hpcrc)
				hpcrc = ibmphp_hpc_readslot (pslot, READ_EXTSLOTSTATUS, &(myslot.ext_status));
			if (!hpcrc) {
				*value = SLOT_ATTN (myslot.status, myslot.ext_status);
				rc = 0;
			}
		}
	} else
		rc = -ENODEV;

	if (hpcrc)
		rc = hpcrc;

	ibmphp_unlock_operations ();
	debug ("get_attention_status - Exit rc[%d] hpcrc[%x] value[%x]\n", rc, hpcrc, *value);
	return rc;
}

static int get_latch_status (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	int hpcrc = 0;
	struct slot myslot;

	debug ("get_latch_status - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations ();
	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
			hpcrc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &(myslot.status));
			if (!hpcrc) {
				*value = SLOT_LATCH (myslot.status);
				rc = 0;
			}
		}
	} else
		rc = -ENODEV;

	if (hpcrc)
		rc = hpcrc;

	ibmphp_unlock_operations ();
	debug ("get_latch_status - Exit rc[%d] hpcrc[%x] value[%x]\n", rc, hpcrc, *value);
	return rc;
}


static int get_power_status (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	int hpcrc = 0;
	struct slot myslot;

	debug ("get_power_status - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations ();
	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
			hpcrc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &(myslot.status));
			if (!hpcrc) {
				*value = SLOT_POWER (myslot.status);
				rc = 0;
			}
		}
	} else
		rc = -ENODEV;

	if (hpcrc)
		rc = hpcrc;

	ibmphp_unlock_operations ();
	debug ("get_power_status - Exit rc[%d] hpcrc[%x] value[%x]\n", rc, hpcrc, *value);
	return rc;
}

static int get_adapter_present (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	u8 present;
	int hpcrc = 0;
	struct slot myslot;

	debug ("get_adapter_status - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations ();
	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
			hpcrc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &(myslot.status));
			if (!hpcrc) {
				present = SLOT_PRESENT (myslot.status);
				if (present == HPC_SLOT_EMPTY)
					*value = 0;
				else
					*value = 1;
				rc = 0;
			}
		}
	} else
		rc = -ENODEV;
	if (hpcrc)
		rc = hpcrc;

	ibmphp_unlock_operations ();
	debug ("get_adapter_present - Exit rc[%d] hpcrc[%x] value[%x]\n", rc, hpcrc, *value);
	return rc;
}
/*
static int get_max_bus_speed (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	u8 mode = 0;

	debug ("get_max_bus_speed - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong)hotplug_slot, (ulong) value);

	ibmphp_lock_operations ();

	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			rc = 0;
			mode = pslot->bus_on->supported_bus_mode;
			*value = pslot->bus_on->supported_speed;
			*value &= 0x0f;

			if (mode == BUS_MODE_PCIX)
				*value |= 0x80;
			else if (mode == BUS_MODE_PCI)
				*value |= 0x40;
			else
				*value |= 0x20;
		}
	} else
		rc = -ENODEV;

	ibmphp_unlock_operations ();
	debug ("get_max_bus_speed - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

static int get_cur_bus_speed (struct hotplug_slot *hotplug_slot, u8 * value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	u8 mode = 0;

	debug ("get_cur_bus_speed - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong)hotplug_slot, (ulong) value);

	ibmphp_lock_operations ();

	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			rc = get_cur_bus_info (&pslot);
			if (!rc) {
				mode = pslot->bus_on->current_bus_mode;
				*value = pslot->bus_on->current_speed;
				*value &= 0x0f;
				
				if (mode == BUS_MODE_PCIX)
					*value |= 0x80;
				else if (mode == BUS_MODE_PCI)
					*value |= 0x40;
				else
					*value |= 0x20;	
			}
		}
	} else
		rc = -ENODEV;

	ibmphp_unlock_operations ();
	debug ("get_cur_bus_speed - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

static int get_max_adapter_speed_1 (struct hotplug_slot *hotplug_slot, u8 * value, u8 flag)
{
	int rc = -ENODEV;
	struct slot *pslot;
	int hpcrc = 0;
	struct slot myslot;

	debug ("get_max_adapter_speed - Entry hotplug_slot[%lx] pvalue[%lx]\n", (ulong)hotplug_slot, (ulong) value);

	if (flag)
		ibmphp_lock_operations ();

	if (hotplug_slot && value) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			memcpy ((void *) &myslot, (void *) pslot, sizeof (struct slot));
			hpcrc = ibmphp_hpc_readslot (pslot, READ_SLOTSTATUS, &(myslot.status));

			if (!(SLOT_LATCH (myslot.status)) && (SLOT_PRESENT (myslot.status))) {
				hpcrc = ibmphp_hpc_readslot (pslot, READ_EXTSLOTSTATUS, &(myslot.ext_status));
				if (!hpcrc) {
					*value = SLOT_SPEED (myslot.ext_status);
					rc = 0;
				}
			} else {
				*value = MAX_ADAPTER_NONE;
				rc = 0;
			}
                }
        } else
		rc = -ENODEV;

	if (hpcrc)
		rc = hpcrc;

	if (flag)
		ibmphp_unlock_operations ();

	debug ("get_adapter_present - Exit rc[%d] hpcrc[%x] value[%x]\n", rc, hpcrc, *value);
	return rc;
}

static int get_card_bus_names (struct hotplug_slot *hotplug_slot, char * value)
{
	int rc = -ENODEV;
	struct slot *pslot = NULL;
	struct pci_dev * dev = NULL;

	debug ("get_card_bus_names - Entry hotplug_slot[%lx] \n", (ulong)hotplug_slot);

	ibmphp_lock_operations ();

	if (hotplug_slot) {
		pslot = (struct slot *) hotplug_slot->private;
		if (pslot) {
			rc = 0;
			if (pslot->func)
				dev = pslot->func->dev;
			else
                		dev = pci_find_slot (pslot->bus, (pslot->device << 3) | (0x00 & 0x7));
			if (dev) 
				snprintf (value, 100, "Bus %d : %s", pslot->bus,dev->name);
			else
				snprintf (value, 100, "Bus %d", pslot->bus);
			
				
		}
	} else
		rc = -ENODEV;

	ibmphp_unlock_operations ();
	debug ("get_card_bus_names - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

*/
/*******************************************************************************
 * This routine will initialize the ops data structure used in the validate
 * function. It will also power off empty slots that are powered on since BIOS
 * leaves those on, albeit disconnected
 ******************************************************************************/
static int init_ops (void)
{
	struct slot *slot_cur;
	int retval;
	int j;
	int rc;

	for (j = 0; j < MAX_OPS; j++) {
		ops[j] = (int *) kmalloc ((max_slots + 1) * sizeof (int), GFP_KERNEL);
		if (!ops[j]) {
			err ("out of system memory \n");
			return -ENOMEM;
		}
	}

	ops[ADD][0] = 0;
	ops[REMOVE][0] = 0;
	ops[DETAIL][0] = 0;

	for (j = 1; j <= max_slots; j++) {

		slot_cur = ibmphp_get_slot_from_physical_num (j);

		debug ("BEFORE GETTING SLOT STATUS, slot # %x\n", slot_cur->number);

		if (slot_cur->ctrl->revision == 0xFF) 
			if (get_ctrl_revision (slot_cur, &slot_cur->ctrl->revision))
				return -1;

		if (slot_cur->bus_on->current_speed == 0xFF) 
			if (get_cur_bus_info (&slot_cur)) 
				return -1;

		if (slot_cur->ctrl->options == 0xFF)
			if (get_hpc_options (slot_cur, &slot_cur->ctrl->options))
				return -1;

		retval = slot_update (&slot_cur);
		if (retval)
			return retval;

		debug ("status = %x, ext_status = %x\n", slot_cur->status, slot_cur->ext_status);
		debug ("SLOT_POWER = %x, SLOT_PRESENT = %x, SLOT_LATCH = %x\n", SLOT_POWER (slot_cur->status), SLOT_PRESENT (slot_cur->status), SLOT_LATCH (slot_cur->status));

		if (!(SLOT_POWER (slot_cur->status)) && (SLOT_PRESENT (slot_cur->status)) && !(SLOT_LATCH (slot_cur->status)))
			/* No power, adapter, and latch closed */
			ops[ADD][j] = 1;
		else
			ops[ADD][j] = 0;

		ops[DETAIL][j] = 1;

		if ((SLOT_POWER (slot_cur->status)) && (SLOT_PRESENT (slot_cur->status)) && !(SLOT_LATCH (slot_cur->status)))
			/*Power,adapter,latch closed */
			ops[REMOVE][j] = 1;
		else
			ops[REMOVE][j] = 0;

		if ((SLOT_POWER (slot_cur->status)) && !(SLOT_PRESENT (slot_cur->status)) && !(SLOT_LATCH (slot_cur->status))) {
			debug ("BEFORE POWER OFF COMMAND\n");
				rc = power_off (slot_cur);
				if (rc)
					return rc;

	/*		retval = slot_update (&slot_cur);
	 *		if (retval)
	 *			return retval;
	 *		ibmphp_update_slot_info (slot_cur);
	 */
		}
	}
	init_flag = 0;
	return 0;
}

/* This operation will check whether the slot is within the bounds and
 * the operation is valid to perform on that slot
 * Parameters: slot, operation
 * Returns: 0 or error codes
 */
static int validate (struct slot *slot_cur, int opn)
{
	int number;
	int retval;

	if (!slot_cur)
		return -ENODEV;
	number = slot_cur->number;
	if ((number > max_slots) || (number < 0))
		return -EBADSLT;
	debug ("slot_number in validate is %d\n", slot_cur->number);

	retval = slot_update (&slot_cur);
	if (retval)
		return retval;

	if (!(SLOT_POWER (slot_cur->status)) && (SLOT_PRESENT (slot_cur->status))
	    && !(SLOT_LATCH (slot_cur->status)))
		ops[ADD][number] = 1;
	else
		ops[ADD][number] = 0;

	ops[DETAIL][number] = 1;

	if ((SLOT_POWER (slot_cur->status)) && (SLOT_PRESENT (slot_cur->status))
	    && !(SLOT_LATCH (slot_cur->status)))
		ops[REMOVE][number] = 1;
	else
		ops[REMOVE][number] = 0;

	switch (opn) {
		case ENABLE:
			if (ops[ADD][number])
				return 0;
			break;
		case DISABLE:
			if (ops[REMOVE][number])
				return 0;
			break;
		case DETAIL:
			if (ops[DETAIL][number])
				return 0;
			break;
		default:
			return -EINVAL;
			break;
	}
	err ("validate failed....\n");
	return -EINVAL;
}

/********************************************************************************
 * This routine is for updating the data structures in the hotplug core
 * Parameters: struct slot
 * Returns: 0 or error
 *******************************************************************************/
int ibmphp_update_slot_info (struct slot *slot_cur)
{
	struct hotplug_slot_info *info;
	char buffer[10];
	int rc;
//	u8 bus_speed;

	info = kmalloc (sizeof (struct hotplug_slot_info), GFP_KERNEL);
	if (!info) {
		err ("out of system memory \n");
		return -ENOMEM;
	}
        
	snprintf (buffer, 10, "%d", slot_cur->number);
	info->power_status = SLOT_POWER (slot_cur->status);
	info->attention_status = SLOT_ATTN (slot_cur->status, slot_cur->ext_status);
	info->latch_status = SLOT_LATCH (slot_cur->status);
        if (!SLOT_PRESENT (slot_cur->status)) {
                info->adapter_status = 0;
//		info->max_adapter_speed_status = MAX_ADAPTER_NONE;
	} else {
                info->adapter_status = 1;
//		get_max_adapter_speed_1 (slot_cur->hotplug_slot, &info->max_adapter_speed_status, 0);
	}
/*
	bus_speed = slot_cur->bus_on->current_speed;
	bus_speed &= 0x0f;
                        
	if (slot_cur->bus_on->current_bus_mode == BUS_MODE_PCIX)
		bus_speed |= 0x80;
	else if (slot_cur->bus_on->current_bus_mode == BUS_MODE_PCI)
		bus_speed |= 0x40;
	else
		bus_speed |= 0x20;

	info->cur_bus_speed_status = bus_speed;
	info->max_bus_speed_status = slot_cur->hotplug_slot->info->max_bus_speed_status;
	// To do: card_bus_names 
*/	
	rc = pci_hp_change_slot_info (buffer, info);
	kfree (info);
	return rc;
}


/******************************************************************************
 * This function will return the pci_func, given bus and devfunc, or NULL.  It
 * is called from visit routines
 ******************************************************************************/

static struct pci_func *ibm_slot_find (u8 busno, u8 device, u8 function)
{
	struct pci_func *func_cur;
	struct slot *slot_cur;
	struct list_head * tmp;
	list_for_each (tmp, &ibmphp_slot_head) {
		slot_cur = list_entry (tmp, struct slot, ibm_slot_list);
		if (slot_cur->func) {
			func_cur = slot_cur->func;
			while (func_cur) {
				if ((func_cur->busno == busno) && (func_cur->device == device) && (func_cur->function == function))
					return func_cur;
				func_cur = func_cur->next;
			}
		}
	}
	return NULL;
}

/* This routine is to find the pci_bus from kernel structures.
 * Parameters: bus number
 * Returns : pci_bus *  or NULL if not found
 */
static struct pci_bus *find_bus (u8 busno)
{
	const struct list_head *tmp;
	struct pci_bus *bus;
	debug ("inside find_bus, busno = %x \n", busno);

	list_for_each (tmp, &pci_root_buses) {
		bus = (struct pci_bus *) pci_bus_b (tmp);
		if (bus)
			if (bus->number == busno)
				return bus;
	}
	return NULL;
}

/******************************************************************
 * This function is here because we can no longer use pci_root_ops
 ******************************************************************/
static struct pci_ops *get_root_pci_ops (void)
{
	struct pci_bus * bus;

	if ((bus = find_bus (0)))
		return bus->ops;
	return NULL;
}

/*************************************************************
 * This routine frees up memory used by struct slot, including
 * the pointers to pci_func, bus, hotplug_slot, controller,
 * and deregistering from the hotplug core
 *************************************************************/
static void free_slots (void)
{
	struct slot *slot_cur;
	struct list_head * tmp;
	struct list_head * next;

	list_for_each_safe (tmp, next, &ibmphp_slot_head) {

		slot_cur = list_entry (tmp, struct slot, ibm_slot_list);

		pci_hp_deregister (slot_cur->hotplug_slot);

		if (slot_cur->hotplug_slot) {
			kfree (slot_cur->hotplug_slot);
			slot_cur->hotplug_slot = NULL;
		}

		if (slot_cur->ctrl) 
			slot_cur->ctrl = NULL;
		
		if (slot_cur->bus_on) 
			slot_cur->bus_on = NULL;

		ibmphp_unconfigure_card (&slot_cur, -1);  /* we don't want to actually remove the resources, since free_resources will do just that */

		kfree (slot_cur);
	}
}

static int ibm_is_pci_dev_in_use (struct pci_dev *dev)
{
	int i = 0;
	int inuse = 0;

	if (dev->driver)
		return 1;

	for (i = 0; !dev->driver && !inuse && (i < 6); i++) {

		if (!pci_resource_start (dev, i))
			continue;

		if (pci_resource_flags (dev, i) & IORESOURCE_IO)
			inuse = check_region (pci_resource_start (dev, i), pci_resource_len (dev, i));

		else if (pci_resource_flags (dev, i) & IORESOURCE_MEM)
			inuse = check_mem_region (pci_resource_start (dev, i), pci_resource_len (dev, i));
	}

	return inuse;
}

static int ibm_pci_hp_remove_device (struct pci_dev *dev)
{
	if (ibm_is_pci_dev_in_use (dev)) {
		err ("***Cannot safely power down device -- it appears to be in use***\n");
		return -EBUSY;
	}
	pci_remove_device (dev);
	return 0;
}

static int ibm_unconfigure_visit_pci_dev_phase2 (struct pci_dev_wrapped *wrapped_dev, struct pci_bus_wrapped *wrapped_bus)
{
	struct pci_dev *dev = wrapped_dev->dev;
	struct pci_func *temp_func;
	int i = 0;

	do {
		temp_func = ibm_slot_find (dev->bus->number, dev->devfn >> 3, i++);
	} while (temp_func && (temp_func->function != (dev->devfn & 0x07)));

	if (dev) {
		if (ibm_pci_hp_remove_device (dev) == 0)
			kfree (dev);    /* Now, remove */
		else
			return -1;
	}

	if (temp_func)
		temp_func->dev = NULL;
	else
		err ("No pci_func representation for bus, devfn = %d, %x\n", dev->bus->number, dev->devfn);

	return 0;
}

static int ibm_unconfigure_visit_pci_bus_phase2 (struct pci_bus_wrapped *wrapped_bus, struct pci_dev_wrapped *wrapped_dev)
{
	struct pci_bus *bus = wrapped_bus->bus;

	pci_proc_detach_bus (bus);
	/* The cleanup code should live in the kernel... */
	bus->self->subordinate = NULL;
	/* unlink from parent bus */
	list_del (&bus->node);

	/* Now, remove */
	if (bus)
		kfree (bus);

	return 0;
}

static int ibm_unconfigure_visit_pci_dev_phase1 (struct pci_dev_wrapped *wrapped_dev, struct pci_bus_wrapped *wrapped_bus)
{
	struct pci_dev *dev = wrapped_dev->dev;

	debug ("attempting removal of driver for device (%x, %x, %x)\n", dev->bus->number, PCI_SLOT (dev->devfn), PCI_FUNC (dev->devfn));

	/* Now, remove the Linux Driver Representation */
	if (dev->driver) {
		debug ("is there a driver?\n");
		if (dev->driver->remove) {
			dev->driver->remove (dev);
			debug ("driver was properly removed\n");
		}
		dev->driver = NULL;
	}

	return ibm_is_pci_dev_in_use (dev);
}

static struct pci_visit ibm_unconfigure_functions_phase1 = {
	post_visit_pci_dev:	ibm_unconfigure_visit_pci_dev_phase1,
};

static struct pci_visit ibm_unconfigure_functions_phase2 = {
	post_visit_pci_bus:	ibm_unconfigure_visit_pci_bus_phase2,
	post_visit_pci_dev:	ibm_unconfigure_visit_pci_dev_phase2,
};

static int ibm_unconfigure_device (struct pci_func *func)
{
	int rc = 0;
	struct pci_dev_wrapped wrapped_dev;
	struct pci_bus_wrapped wrapped_bus;
	struct pci_dev *temp;
	u8 j;

	memset (&wrapped_dev, 0, sizeof (struct pci_dev_wrapped));
	memset (&wrapped_bus, 0, sizeof (struct pci_bus_wrapped));

	debug ("inside ibm_unconfigure_device\n");
	debug ("func->device = %x, func->function = %x\n", func->device, func->function);
	debug ("func->device << 3 | 0x0  = %x\n", func->device << 3 | 0x0);

	for (j = 0; j < 0x08; j++) {
		temp = pci_find_slot (func->busno, (func->device << 3) | j);
		if (temp) {
			wrapped_dev.dev = temp;
			wrapped_bus.bus = temp->bus;
			rc = pci_visit_dev (&ibm_unconfigure_functions_phase1, &wrapped_dev, &wrapped_bus);
			if (rc)
				break;

			rc = pci_visit_dev (&ibm_unconfigure_functions_phase2, &wrapped_dev, &wrapped_bus);
			if (rc)
				break;
		}
	}
	debug ("rc in ibm_unconfigure_device b4 returning is %d \n", rc);
	return rc;
}

static int configure_visit_pci_dev (struct pci_dev_wrapped *wrapped_dev, struct pci_bus_wrapped *wrapped_bus)
{
	//      struct pci_bus *bus = wrapped_bus->bus; /* We don't need this, since we don't create in the else statement */
	struct pci_dev *dev = wrapped_dev->dev;
	struct pci_func *temp_func;
	int i = 0;

	do {
		temp_func = ibm_slot_find (dev->bus->number, dev->devfn >> 3, i++);
	} while (temp_func && (temp_func->function != (dev->devfn & 0x07)));

	if (temp_func)
		temp_func->dev = dev;
	else {
		/* This should not really happen, since we create functions
		   first and then call to configure */
		debug (" We shouldn't come here \n");
	}

	if (temp_func->dev) {
		pci_proc_attach_device (temp_func->dev);
		pci_announce_device_to_drivers (temp_func->dev);
	}

	return 0;
}

static struct pci_visit configure_functions = {
	visit_pci_dev:	configure_visit_pci_dev,
};

static int ibm_configure_device (struct pci_func *func)
{
	unsigned char bus;
	struct pci_dev dev0;
	struct pci_bus *child;
	struct pci_dev *temp;
	int rc = 0;

	struct pci_dev_wrapped wrapped_dev;
	struct pci_bus_wrapped wrapped_bus;

	memset (&wrapped_dev, 0, sizeof (struct pci_dev_wrapped));
	memset (&wrapped_bus, 0, sizeof (struct pci_bus_wrapped));
	memset (&dev0, 0, sizeof (struct pci_dev));

	if (func->dev == NULL)
		func->dev = pci_find_slot (func->busno, (func->device << 3) | (func->function & 0x7));

	if (func->dev == NULL) {
		dev0.bus = find_bus (func->busno);
		dev0.devfn = ((func->device << 3) + (func->function & 0x7));
		dev0.sysdata = dev0.bus->sysdata;

		func->dev = pci_scan_slot (&dev0);

		if (func->dev == NULL) {
			err ("ERROR... : pci_dev still NULL \n");
			return 0;
		}
	}
	if (func->dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		pci_read_config_byte (func->dev, PCI_SECONDARY_BUS, &bus);
		child = (struct pci_bus *) pci_add_new_bus (func->dev->bus, (func->dev), bus);
		pci_do_scan_bus (child);
	}

	temp = func->dev;
	if (temp) {
		wrapped_dev.dev = temp;
		wrapped_bus.bus = temp->bus;
		rc = pci_visit_dev (&configure_functions, &wrapped_dev, &wrapped_bus);
	}
	return rc;
}
/*******************************************************
 * Returns whether the bus is empty or not 
 *******************************************************/
static int is_bus_empty (struct slot * slot_cur)
{
	int rc;
	struct slot * tmp_slot;
	u8 i = slot_cur->bus_on->slot_min;

	while (i <= slot_cur->bus_on->slot_max) {
		if (i == slot_cur->number) {
			i++;
			continue;
		}
		tmp_slot = ibmphp_get_slot_from_physical_num (i);
		rc = slot_update (&tmp_slot);
		if (rc)
			return 0;
		if (SLOT_PRESENT (tmp_slot->status) && SLOT_POWER (tmp_slot->status))
			return 0;
		i++;
	}
	return 1;
}

/***********************************************************
 * If the HPC permits and the bus currently empty, tries to set the 
 * bus speed and mode at the maximum card capability
 ***********************************************************/
static int set_bus (struct slot * slot_cur)
{
	int rc;
	u8 speed;
	u8 cmd = 0x0;

	debug ("%s - entry slot # %d \n", __FUNCTION__, slot_cur->number);
	if (SET_BUS_STATUS (slot_cur->ctrl) && is_bus_empty (slot_cur)) {
		rc = slot_update (&slot_cur);
		if (rc)
			return rc;
		speed = SLOT_SPEED (slot_cur->ext_status);
		debug ("ext_status = %x, speed = %x\n", slot_cur->ext_status, speed);
		switch (speed) {
		case HPC_SLOT_SPEED_33:
			cmd = HPC_BUS_33CONVMODE;
			break;
		case HPC_SLOT_SPEED_66:
			if (SLOT_PCIX (slot_cur->ext_status))
				cmd = HPC_BUS_66PCIXMODE;
			else
				cmd = HPC_BUS_66CONVMODE;
			break;
		case HPC_SLOT_SPEED_133:
			if (slot_cur->bus_on->slot_count > 1)
				cmd = HPC_BUS_100PCIXMODE;
			else
				cmd = HPC_BUS_133PCIXMODE;
			break;
		default:
			err ("wrong slot speed \n");
			return -ENODEV;
		}
		debug ("setting bus speed for slot %d, cmd %x\n", slot_cur->number, cmd);
		rc = ibmphp_hpc_writeslot (slot_cur, cmd);
		if (rc)
			return rc;
	}
	debug ("%s -Exit \n", __FUNCTION__);
	return 0;
}

static inline void print_card_capability (struct slot *slot_cur)
{
	info ("capability of the card is ");
	if ((slot_cur->ext_status & CARD_INFO) == PCIX133) 
		info ("   133 MHz PCI-X \n");
	else if ((slot_cur->ext_status & CARD_INFO) == PCIX66)
		info ("    66 MHz PCI-X \n");
	else if ((slot_cur->ext_status & CARD_INFO) == PCI66)
		info ("    66 MHz PCI \n");
	else
		info ("    33 MHz PCI \n");

}

/* This routine will power on the slot, configure the device(s) and find the
 * drivers for them.
 * Parameters: hotplug_slot
 * Returns: 0 or failure codes
 */
static int enable_slot (struct hotplug_slot *hs)
{
	int rc, i, rcpr;
	struct slot *slot_cur;
	u8 function;
	u8 faulted = 0;
	struct pci_func *tmp_func;

	ibmphp_lock_operations ();

	debug ("ENABLING SLOT........ \n");
	slot_cur = (struct slot *) hs->private;

	if ((rc = validate (slot_cur, ENABLE))) {
		err ("validate function failed \n");
		attn_off (slot_cur);	/* need to turn off if was blinking b4 */
		attn_on (slot_cur);
		rc = slot_update (&slot_cur);
		if (rc) {
			ibmphp_unlock_operations();
			return rc;
		}
		ibmphp_update_slot_info (slot_cur);
                ibmphp_unlock_operations ();
		return rc;
	}

	attn_LED_blink (slot_cur);
	
	rc = set_bus (slot_cur);
	if (rc) {
		err ("was not able to set the bus \n");
		attn_off (slot_cur);
		attn_on (slot_cur);
		ibmphp_unlock_operations ();
		return -ENODEV;
	}
		
	rc = power_on (slot_cur);

	if (rc) {
		err ("something wrong when powering up... please see below for details\n");
		/* need to turn off before on, otherwise, blinking overwrites */
		attn_off(slot_cur);
		attn_on (slot_cur);
		if (slot_update (&slot_cur)) {
			attn_off (slot_cur);
			attn_on (slot_cur);
			ibmphp_unlock_operations ();
			return -ENODEV;
		}
		/* Check to see the error of why it failed */
		if (!(SLOT_PWRGD (slot_cur->status)))
			err ("power fault occured trying to power up \n");
		else if (SLOT_BUS_SPEED (slot_cur->status)) {
			err ("bus speed mismatch occured.  please check current bus speed and card capability \n");
			print_card_capability (slot_cur);
		} else if (SLOT_BUS_MODE (slot_cur->ext_status)) 
			err ("bus mode mismatch occured.  please check current bus mode and card capability \n");

		ibmphp_update_slot_info (slot_cur);
		ibmphp_unlock_operations (); 
		return rc;
	}
	debug ("after power_on\n");

	rc = slot_update (&slot_cur);
	if (rc) {
		attn_off (slot_cur);
		attn_on (slot_cur);
		rcpr = power_off (slot_cur);
		if (rcpr) {
			ibmphp_unlock_operations ();
			return rcpr;
		}
		ibmphp_unlock_operations ();
		return rc;
	}
	
	if (SLOT_POWER (slot_cur->status) && !(SLOT_PWRGD (slot_cur->status))) {
		faulted = 1;
		err ("power fault occured trying to power up...\n");
	} else if (SLOT_POWER (slot_cur->status) && (SLOT_BUS_SPEED (slot_cur->status))) {
		faulted = 1;
		err ("bus speed mismatch occured.  please check current bus speed and card capability \n");
		print_card_capability (slot_cur);
	} 
	/* Don't think this case will happen after above checks... but just in case, for paranoia sake */
	else if (!(SLOT_POWER (slot_cur->status))) {
		err ("power on failed... \n");
		faulted = 1;
	}
	if (faulted) {
		attn_off (slot_cur);	/* need to turn off b4 on */
		attn_on (slot_cur);
		rcpr = power_off (slot_cur);
		if (rcpr) {
			ibmphp_unlock_operations ();
			return rcpr;
		}
			
		if (slot_update (&slot_cur)) {
			ibmphp_unlock_operations ();
			return -ENODEV;
		}
		ibmphp_update_slot_info (slot_cur);
		ibmphp_unlock_operations ();
		return -EINVAL;
	}

	slot_cur->func = (struct pci_func *) kmalloc (sizeof (struct pci_func), GFP_KERNEL);
	if (!slot_cur->func) { /* We cannot do update_slot_info here, since no memory for kmalloc n.e.ways, and update_slot_info allocates some */
		err ("out of system memory \n");
		attn_off (slot_cur);
		attn_on (slot_cur);
		rcpr = power_off (slot_cur);
		if (rcpr) {
			ibmphp_unlock_operations ();
			return rcpr;
		}
		ibmphp_unlock_operations ();
		return -ENOMEM;
	}
	memset (slot_cur->func, 0, sizeof (struct pci_func));
	slot_cur->func->busno = slot_cur->bus;
	slot_cur->func->device = slot_cur->device;
	for (i = 0; i < 4; i++)
		slot_cur->func->irq[i] = slot_cur->irq[i];

	debug ("b4 configure_card, slot_cur->bus = %x, slot_cur->device = %x\n", slot_cur->bus, slot_cur->device);

	if (ibmphp_configure_card (slot_cur->func, slot_cur->number)) {
		err ("configure_card was unsuccessful... \n");
		ibmphp_unconfigure_card (&slot_cur, 1); /* true because don't need to actually deallocate resources, just remove references */
		debug ("after unconfigure_card\n");
		slot_cur->func = NULL;
		attn_off (slot_cur);	/* need to turn off in case was blinking */
		attn_on (slot_cur);
		rcpr = power_off (slot_cur);
		if (rcpr) {
			ibmphp_unlock_operations ();
			return rcpr;
		}
		if (slot_update (&slot_cur)) {
			ibmphp_unlock_operations();
			return -ENODEV;
		}
		ibmphp_update_slot_info (slot_cur);
		ibmphp_unlock_operations ();
		return -ENOMEM;
	}
	function = 0x00;
	do {
		tmp_func = ibm_slot_find (slot_cur->bus, slot_cur->func->device, function++);
		if (tmp_func && !(tmp_func->dev))
			ibm_configure_device (tmp_func);
	} while (tmp_func);

	attn_off (slot_cur);
	if (slot_update (&slot_cur)) {
		ibmphp_unlock_operations ();
		return -EFAULT;
	}
	ibmphp_print_test ();
	rc = ibmphp_update_slot_info (slot_cur);
	ibmphp_unlock_operations(); 
	return rc;
}

/**************************************************************
* HOT REMOVING ADAPTER CARD                                   *
* INPUT: POINTER TO THE HOTPLUG SLOT STRUCTURE                *
* OUTPUT: SUCCESS 0 ; FAILURE: UNCONFIGURE , VALIDATE         *
          DISABLE POWER ,                                    *
**************************************************************/
int ibmphp_disable_slot (struct hotplug_slot *hotplug_slot)
{
	int rc;
	struct slot *slot_cur = (struct slot *) hotplug_slot->private;
	u8 flag = slot_cur->flag;

	slot_cur->flag = TRUE;
	debug ("DISABLING SLOT... \n"); 
		
	ibmphp_lock_operations (); 
	if (slot_cur == NULL) {
		ibmphp_unlock_operations ();
		return -ENODEV;
	}
	if (slot_cur->ctrl == NULL) {
		ibmphp_unlock_operations ();
		return -ENODEV;
	}
	if (flag == TRUE) {
		rc = validate (slot_cur, DISABLE);	/* checking if powered off already & valid slot # */
		if (rc) {
			/*  Need to turn off if was blinking b4 */
			attn_off (slot_cur);
			attn_on (slot_cur);
			if (slot_update (&slot_cur)) {
				ibmphp_unlock_operations ();
				return -EFAULT;
			}
		
			ibmphp_update_slot_info (slot_cur);
			ibmphp_unlock_operations ();
			return rc;
		}
	}
	attn_LED_blink (slot_cur);

	if (slot_cur->func == NULL) {
		/* We need this for fncs's that were there on bootup */
		slot_cur->func = (struct pci_func *) kmalloc (sizeof (struct pci_func), GFP_KERNEL);
		if (!slot_cur->func) {
			err ("out of system memory \n");
			attn_off (slot_cur);
			attn_on (slot_cur);
			ibmphp_unlock_operations ();
			return -ENOMEM;
		}
		memset (slot_cur->func, 0, sizeof (struct pci_func));
		slot_cur->func->busno = slot_cur->bus;
		slot_cur->func->device = slot_cur->device;
	}

	if ((rc = ibm_unconfigure_device (slot_cur->func))) {
		err ("removing from kernel failed... \n");
		err ("Please check to see if it was statically linked or is in use otherwise. (perhaps the driver is not 'hot-removable')\n");
		attn_off (slot_cur);
		attn_on (slot_cur);
		ibmphp_unlock_operations ();
		return rc;
	}

	rc = ibmphp_unconfigure_card (&slot_cur, 0);
	slot_cur->func = NULL;
	debug ("in disable_slot. after unconfigure_card \n");
	if (rc) {
		err ("could not unconfigure card.\n");
		attn_off (slot_cur);	/* need to turn off if was blinking b4 */
		attn_on (slot_cur);

		if (slot_update (&slot_cur)) {
			ibmphp_unlock_operations ();
			return -EFAULT;
		}

		if (flag) 
			ibmphp_update_slot_info (slot_cur);

		ibmphp_unlock_operations ();
		return -EFAULT;
	}

	rc = ibmphp_hpc_writeslot (hotplug_slot->private, HPC_SLOT_OFF);
	if (rc) {
		attn_off (slot_cur);
		attn_on (slot_cur);
		if (slot_update (&slot_cur)) {
			ibmphp_unlock_operations ();
			return -EFAULT;
		}

		if (flag)
			ibmphp_update_slot_info (slot_cur);

		ibmphp_unlock_operations ();
		return rc;
	}

	attn_off (slot_cur);
	if (slot_update (&slot_cur)) {
		ibmphp_unlock_operations ();
		return -EFAULT;
	}
	if (flag) 
		rc = ibmphp_update_slot_info (slot_cur);
	else
		rc = 0;

	ibmphp_print_test ();
	ibmphp_unlock_operations();
	return rc;
}

struct hotplug_slot_ops ibmphp_hotplug_slot_ops = {
	owner:				THIS_MODULE,
	set_attention_status:		set_attention_status,
	enable_slot:			enable_slot,
	disable_slot:			ibmphp_disable_slot,
	hardware_test:			NULL,
	get_power_status:		get_power_status,
	get_attention_status:		get_attention_status,
	get_latch_status:		get_latch_status,
	get_adapter_status:		get_adapter_present,
/*	get_max_bus_speed_status:	get_max_bus_speed,
	get_max_adapter_speed_status:	get_max_adapter_speed,
	get_cur_bus_speed_status:	get_cur_bus_speed,
	get_card_bus_names_status:	get_card_bus_names,
*/
};

static void ibmphp_unload (void)
{
	free_slots ();
	debug ("after slots \n");
	ibmphp_free_resources ();
	debug ("after resources \n");
	ibmphp_free_bus_info_queue ();
	debug ("after bus info \n");
	ibmphp_free_ebda_hpc_queue ();
	debug ("after ebda hpc \n");
	ibmphp_free_ebda_pci_rsrc_queue ();
	debug ("after ebda pci rsrc \n");
}

static int __init ibmphp_init (void)
{
	int i = 0;
	int rc = 0;

	init_flag = 1;
	ibmphp_pci_root_ops = get_root_pci_ops ();
	if (ibmphp_pci_root_ops == NULL) {
		err ("cannot read bus operations... will not be able to read the cards.  Please check your system \n");
		return -ENODEV;	
	}

	ibmphp_debug = debug;

	ibmphp_hpc_initvars ();

	for (i = 0; i < 16; i++)
		irqs[i] = 0;

	if ((rc = ibmphp_access_ebda ())) {
		ibmphp_unload ();
		return rc;
	}
	debug ("after ibmphp_access_ebda () \n");

	if ((rc = ibmphp_rsrc_init ())) {
		ibmphp_unload ();
		return rc;
	}
	debug ("AFTER Resource & EBDA INITIALIZATIONS \n");

	max_slots = get_max_slots ();

	if (init_ops ()) {
		ibmphp_unload ();
		return -ENODEV;
	}
	ibmphp_print_test ();
	if ((rc = ibmphp_hpc_start_poll_thread ())) {
		ibmphp_unload ();
		return -ENODEV;
	}

	/* lock ourselves into memory with a module count of -1 
	 * so that no one can unload us. */
	MOD_DEC_USE_COUNT;

	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");

	return 0;
}

static void __exit ibmphp_exit (void)
{
	ibmphp_hpc_stop_poll_thread ();
	debug ("after polling \n");
	ibmphp_unload ();
	debug ("done \n");
}

module_init (ibmphp_init);
module_exit (ibmphp_exit);
