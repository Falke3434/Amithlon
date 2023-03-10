/*
 * SMBus 2.0 driver for AMD-8111 IO-Hub.
 *
 * Copyright (c) 2002 Vojtech Pavlik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("AMD8111 SMBus 2.0 driver");

struct amd_smbus {
	struct pci_dev *dev;
	struct i2c_adapter adapter;
	int base;
	int size;
};

/*
 * AMD PCI control registers definitions.
 */

#define AMD_PCI_MISC	0x48

#define AMD_PCI_MISC_SCI	0x04	/* deliver SCI */
#define AMD_PCI_MISC_INT	0x02	/* deliver PCI IRQ */
#define AMD_PCI_MISC_SPEEDUP	0x01	/* 16x clock speedup */

/*
 * ACPI 2.0 chapter 13 PCI interface definitions.
 */

#define AMD_EC_DATA	0x00	/* data register */
#define AMD_EC_SC	0x04	/* status of controller */
#define AMD_EC_CMD	0x04	/* command register */
#define AMD_EC_ICR	0x08	/* interrupt control register */

#define AMD_EC_SC_SMI	0x04	/* smi event pending */
#define AMD_EC_SC_SCI	0x02	/* sci event pending */
#define AMD_EC_SC_BURST	0x01	/* burst mode enabled */
#define AMD_EC_SC_CMD	0x08	/* byte in data reg is command */
#define AMD_EC_SC_IBF	0x02	/* data ready for embedded controller */
#define AMD_EC_SC_OBF	0x01	/* data ready for host */

#define AMD_EC_CMD_RD	0x80	/* read EC */
#define AMD_EC_CMD_WR	0x81	/* write EC */
#define AMD_EC_CMD_BE	0x82	/* enable burst mode */
#define AMD_EC_CMD_BD	0x83	/* disable burst mode */
#define AMD_EC_CMD_QR	0x84	/* query EC */

/*
 * ACPI 2.0 chapter 13 access of registers of the EC
 */

unsigned int amd_ec_wait_write(struct amd_smbus *smbus)
{
	int timeout = 500;

	while (timeout-- && (inb(smbus->base + AMD_EC_SC) & AMD_EC_SC_IBF))
		udelay(1);

	if (!timeout) {
		dev_warn(&smbus->dev->dev, "Timeout while waiting for IBF to clear\n");
		return -1;
	}

	return 0;
}

unsigned int amd_ec_wait_read(struct amd_smbus *smbus)
{
	int timeout = 500;

	while (timeout-- && (~inb(smbus->base + AMD_EC_SC) & AMD_EC_SC_OBF))
		udelay(1);

	if (!timeout) {
		dev_warn(&smbus->dev->dev, "Timeout while waiting for OBF to set\n");
		return -1;
	}

	return 0;
}

unsigned int amd_ec_read(struct amd_smbus *smbus, unsigned char address, unsigned char *data)
{
	if (amd_ec_wait_write(smbus))
		return -1;
	outb(AMD_EC_CMD_RD, smbus->base + AMD_EC_CMD);

	if (amd_ec_wait_write(smbus))
		return -1;
	outb(address, smbus->base + AMD_EC_DATA);

	if (amd_ec_wait_read(smbus))
		return -1;
	*data = inb(smbus->base + AMD_EC_DATA);

	return 0;
}

unsigned int amd_ec_write(struct amd_smbus *smbus, unsigned char address, unsigned char data)
{
	if (amd_ec_wait_write(smbus))
		return -1;
	outb(AMD_EC_CMD_WR, smbus->base + AMD_EC_CMD);

	if (amd_ec_wait_write(smbus))
		return -1;
	outb(address, smbus->base + AMD_EC_DATA);

	if (amd_ec_wait_write(smbus))
		return -1;
	outb(data, smbus->base + AMD_EC_DATA);

	return 0;
}

/*
 * ACPI 2.0 chapter 13 SMBus 2.0 EC register model
 */

#define AMD_SMB_PRTCL	0x00	/* protocol, PEC */
#define AMD_SMB_STS	0x01	/* status */
#define AMD_SMB_ADDR	0x02	/* address */
#define AMD_SMB_CMD	0x03	/* command */
#define AMD_SMB_DATA	0x04	/* 32 data registers */
#define AMD_SMB_BCNT	0x24	/* number of data bytes */
#define AMD_SMB_ALRM_A	0x25	/* alarm address */
#define AMD_SMB_ALRM_D	0x26	/* 2 bytes alarm data */

#define AMD_SMB_STS_DONE	0x80
#define AMD_SMB_STS_ALRM	0x40
#define AMD_SMB_STS_RES		0x20
#define AMD_SMB_STS_STATUS	0x1f

#define AMD_SMB_STATUS_OK	0x00
#define AMD_SMB_STATUS_FAIL	0x07
#define AMD_SMB_STATUS_DNAK	0x10
#define AMD_SMB_STATUS_DERR	0x11
#define AMD_SMB_STATUS_CMD_DENY	0x12
#define AMD_SMB_STATUS_UNKNOWN	0x13
#define AMD_SMB_STATUS_ACC_DENY	0x17
#define AMD_SMB_STATUS_TIMEOUT	0x18
#define AMD_SMB_STATUS_NOTSUP	0x19
#define AMD_SMB_STATUS_BUSY	0x1A
#define AMD_SMB_STATUS_PEC	0x1F

#define AMD_SMB_PRTCL_WRITE		0x00
#define AMD_SMB_PRTCL_READ		0x01
#define AMD_SMB_PRTCL_QUICK		0x02
#define AMD_SMB_PRTCL_BYTE		0x04
#define AMD_SMB_PRTCL_BYTE_DATA		0x06
#define AMD_SMB_PRTCL_WORD_DATA		0x08
#define AMD_SMB_PRTCL_BLOCK_DATA	0x0a
#define AMD_SMB_PRTCL_PROC_CALL		0x0c
#define AMD_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define AMD_SMB_PRTCL_I2C_BLOCK_DATA	0x4a
#define AMD_SMB_PRTCL_PEC		0x80


s32 amd8111_access(struct i2c_adapter * adap, u16 addr, unsigned short flags,
		char read_write, u8 command, int size, union i2c_smbus_data * data)
{
	struct amd_smbus *smbus = adap->algo_data;
	unsigned char protocol, len, pec, temp[2];
	int i;

	protocol = (read_write == I2C_SMBUS_READ) ? AMD_SMB_PRTCL_READ : AMD_SMB_PRTCL_WRITE;
	pec = (flags & I2C_CLIENT_PEC) ? AMD_SMB_PRTCL_PEC : 0;

	switch (size) {

		case I2C_SMBUS_QUICK:
			protocol |= AMD_SMB_PRTCL_QUICK;
			read_write = I2C_SMBUS_WRITE;
			break;

		case I2C_SMBUS_BYTE:
			if (read_write == I2C_SMBUS_WRITE)
				amd_ec_write(smbus, AMD_SMB_DATA, data->byte);
			protocol |= AMD_SMB_PRTCL_BYTE;
			break;

		case I2C_SMBUS_BYTE_DATA:
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (read_write == I2C_SMBUS_WRITE)
				amd_ec_write(smbus, AMD_SMB_DATA, data->byte);
			protocol |= AMD_SMB_PRTCL_BYTE_DATA;
			break;

		case I2C_SMBUS_WORD_DATA:
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (read_write == I2C_SMBUS_WRITE) {
				amd_ec_write(smbus, AMD_SMB_DATA, data->word);
				amd_ec_write(smbus, AMD_SMB_DATA + 1, data->word >> 8);
			}
			protocol |= AMD_SMB_PRTCL_WORD_DATA | pec;
			break;

		case I2C_SMBUS_BLOCK_DATA:
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			if (read_write == I2C_SMBUS_WRITE) {
				len = min_t(u8, data->block[0], 32);
				amd_ec_write(smbus, AMD_SMB_BCNT, len);
				for (i = 0; i < len; i++)
					amd_ec_write(smbus, AMD_SMB_DATA + i, data->block[i + 1]);
			}
			protocol |= AMD_SMB_PRTCL_BLOCK_DATA | pec;
			break;

		case I2C_SMBUS_I2C_BLOCK_DATA:
			len = min_t(u8, data->block[0], 32);
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			amd_ec_write(smbus, AMD_SMB_BCNT, len);
			if (read_write == I2C_SMBUS_WRITE)
				for (i = 0; i < len; i++)
					amd_ec_write(smbus, AMD_SMB_DATA + i, data->block[i + 1]);
			protocol |= AMD_SMB_PRTCL_I2C_BLOCK_DATA;
			break;

		case I2C_SMBUS_PROC_CALL:
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			amd_ec_write(smbus, AMD_SMB_DATA, data->word);
			amd_ec_write(smbus, AMD_SMB_DATA + 1, data->word >> 8);
			protocol = AMD_SMB_PRTCL_PROC_CALL | pec;
			read_write = I2C_SMBUS_READ;
			break;

		case I2C_SMBUS_BLOCK_PROC_CALL:
			protocol |= pec;
			len = min_t(u8, data->block[0], 31);
			amd_ec_write(smbus, AMD_SMB_CMD, command);
			amd_ec_write(smbus, AMD_SMB_BCNT, len);
			for (i = 0; i < len; i++)
				amd_ec_write(smbus, AMD_SMB_DATA + i, data->block[i + 1]);
			protocol = AMD_SMB_PRTCL_BLOCK_PROC_CALL | pec;
			read_write = I2C_SMBUS_READ;
			break;

		case I2C_SMBUS_WORD_DATA_PEC:
		case I2C_SMBUS_BLOCK_DATA_PEC:
		case I2C_SMBUS_PROC_CALL_PEC:
		case I2C_SMBUS_BLOCK_PROC_CALL_PEC:
			dev_warn(&adap->dev, "Unexpected software PEC transaction %d\n.", size);
			return -1;

		default:
			dev_warn(&adap->dev, "Unsupported transaction %d\n", size);
			return -1;
	}

	amd_ec_write(smbus, AMD_SMB_ADDR, addr << 1);
	amd_ec_write(smbus, AMD_SMB_PRTCL, protocol);

	amd_ec_read(smbus, AMD_SMB_STS, temp + 0);

	if (~temp[0] & AMD_SMB_STS_DONE) {
		udelay(500);
		amd_ec_read(smbus, AMD_SMB_STS, temp + 0);
	}

	if (~temp[0] & AMD_SMB_STS_DONE) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/100);
		amd_ec_read(smbus, AMD_SMB_STS, temp + 0);
	}

	if ((~temp[0] & AMD_SMB_STS_DONE) || (temp[0] & AMD_SMB_STS_STATUS))
		return -1;

	if (read_write == I2C_SMBUS_WRITE)
		return 0;

	switch (size) {

		case I2C_SMBUS_BYTE:
		case I2C_SMBUS_BYTE_DATA:
			amd_ec_read(smbus, AMD_SMB_DATA, &data->byte);
			break;

		case I2C_SMBUS_WORD_DATA:
		case I2C_SMBUS_PROC_CALL:
			amd_ec_read(smbus, AMD_SMB_DATA, temp + 0);
			amd_ec_read(smbus, AMD_SMB_DATA + 1, temp + 1);
			data->word = (temp[1] << 8) | temp[0];
			break;

		case I2C_SMBUS_BLOCK_DATA:
		case I2C_SMBUS_BLOCK_PROC_CALL:
			amd_ec_read(smbus, AMD_SMB_BCNT, &len);
			len = min_t(u8, len, 32);
		case I2C_SMBUS_I2C_BLOCK_DATA:
			for (i = 0; i < len; i++)
				amd_ec_read(smbus, AMD_SMB_DATA + i, data->block + i + 1);
			data->block[0] = len;
			break;
	}

	return 0;
}


u32 amd8111_func(struct i2c_adapter *adapter)
{
	return	I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_PROC_CALL | I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
		I2C_FUNC_SMBUS_I2C_BLOCK | I2C_FUNC_SMBUS_HWPEC_CALC;
}

static struct i2c_algorithm smbus_algorithm = {
	.name = "Non-I2C SMBus 2.0 adapter",
	.id = I2C_ALGO_SMBUS,
	.smbus_xfer = amd8111_access,
	.functionality = amd8111_func,
};


static struct pci_device_id amd8111_ids[] __devinitdata = {
	{ 0x1022, 0x746a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};

static int __devinit amd8111_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct amd_smbus *smbus;
	int error = -ENODEV;

	if (~pci_resource_flags(dev, 0) & IORESOURCE_IO)
		return -ENODEV;

	smbus = kmalloc(sizeof(struct amd_smbus), GFP_KERNEL);
	if (!smbus)
		return -ENOMEM;
	memset(smbus, 0, sizeof(struct amd_smbus));

	smbus->dev = dev;
	smbus->base = pci_resource_start(dev, 0);
	smbus->size = pci_resource_len(dev, 0);

	if (!request_region(smbus->base, smbus->size, "amd8111 SMBus 2.0"))
		goto out_kfree;

	smbus->adapter.owner = THIS_MODULE;
	snprintf(smbus->adapter.dev.name, DEVICE_NAME_SIZE,
		"SMBus2 AMD8111 adapter at %04x", smbus->base);
	smbus->adapter.id = I2C_ALGO_SMBUS | I2C_HW_SMBUS_AMD8111;
	smbus->adapter.class = I2C_ADAP_CLASS_SMBUS;
	smbus->adapter.algo = &smbus_algorithm;
	smbus->adapter.algo_data = smbus;

	/* set up the driverfs linkage to our parent device */
	smbus->adapter.dev.parent = &dev->dev;

	error = i2c_add_adapter(&smbus->adapter);
	if (error)
		goto out_release_region;

	pci_write_config_dword(smbus->dev, AMD_PCI_MISC, 0);
	pci_set_drvdata(dev, smbus);
	return 0;

 out_release_region:
	release_region(smbus->base, smbus->size);
 out_kfree:
	kfree(smbus);
	return -1;
}


static void __devexit amd8111_remove(struct pci_dev *dev)
{
	struct amd_smbus *smbus = pci_get_drvdata(dev);

	i2c_del_adapter(&smbus->adapter);
	release_region(smbus->base, smbus->size);
	kfree(smbus);
}

static struct pci_driver amd8111_driver = {
	.name		= "amd8111 smbus 2",
	.id_table	= amd8111_ids,
	.probe		= amd8111_probe,
	.remove		= __devexit_p(amd8111_remove),
};

static int __init i2c_amd8111_init(void)
{
	return pci_module_init(&amd8111_driver);
}


static void __exit i2c_amd8111_exit(void)
{
	pci_unregister_driver(&amd8111_driver);
}

module_init(i2c_amd8111_init);
module_exit(i2c_amd8111_exit);
