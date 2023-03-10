/*
    i2c-isa.c - Part of lm_sensors, Linux kernel modules for hardware
            monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* This implements an i2c algorithm/adapter for ISA bus. Not that this is
   on first sight very useful; almost no functionality is preserved.
   Except that it makes writing drivers for chips which can be on both
   the SMBus and the ISA bus very much easier. See lm78.c for an example
   of this. */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>

/* This is the actual algorithm we define */
static struct i2c_algorithm isa_algorithm = {
	.name		= "ISA bus algorithm",
	.id		= I2C_ALGO_ISA,
};

/* There can only be one... */
static struct i2c_adapter isa_adapter = {
	.owner		= THIS_MODULE,
	.id		= I2C_ALGO_ISA | I2C_HW_ISA,
	.class          = I2C_ADAP_CLASS_SMBUS,
	.algo		= &isa_algorithm,
	.dev		= {
		.name	= "ISA main adapter",
	},
};

static int __init i2c_isa_init(void)
{
	return i2c_add_adapter(&isa_adapter);
}

static void __exit i2c_isa_exit(void)
{
	i2c_del_adapter(&isa_adapter);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("ISA bus access through i2c");
MODULE_LICENSE("GPL");

module_init(i2c_isa_init);
module_exit(i2c_isa_exit);
