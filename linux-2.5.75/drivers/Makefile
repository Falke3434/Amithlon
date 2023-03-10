#
# Makefile for the Linux kernel device drivers.
#
# 15 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

obj-$(CONFIG_PCI)		+= pci/
obj-$(CONFIG_PARISC)		+= parisc/
obj-$(CONFIG_ACPI)		+= acpi/
# PnP must come after ACPI since it will eventually need to check if acpi
# was used and do nothing if so
obj-$(CONFIG_PNP)		+= pnp/

# char/ comes before serial/ etc so that the VT console is the boot-time
# default.
obj-y				+= char/
obj-y				+= serial/
obj-$(CONFIG_PARPORT)		+= parport/
obj-y				+= base/ block/ misc/ net/ media/
obj-$(CONFIG_NUBUS)		+= nubus/
obj-$(CONFIG_ATM)		+= atm/
obj-$(CONFIG_IDE)		+= ide/
obj-$(CONFIG_FC4)		+= fc4/
obj-$(CONFIG_SCSI)		+= scsi/
obj-$(CONFIG_FUSION)		+= message/
obj-$(CONFIG_IEEE1394)		+= ieee1394/
obj-y				+= cdrom/ video/
obj-$(CONFIG_MTD)		+= mtd/
obj-$(CONFIG_PCMCIA)		+= pcmcia/
obj-$(CONFIG_DIO)		+= dio/
obj-$(CONFIG_SBUS)		+= sbus/
obj-$(CONFIG_ZORRO)		+= zorro/
obj-$(CONFIG_PPC_PMAC)		+= macintosh/
obj-$(CONFIG_MAC)		+= macintosh/
obj-$(CONFIG_PARIDE) 		+= block/paride/
obj-$(CONFIG_TC)		+= tc/
obj-$(CONFIG_USB)		+= usb/
obj-$(CONFIG_USB_GADGET)	+= usb/gadget/
obj-$(CONFIG_INPUT)		+= input/
obj-$(CONFIG_GAMEPORT)		+= input/gameport/
obj-$(CONFIG_SERIO)		+= input/serio/
obj-$(CONFIG_I2O)		+= message/
obj-$(CONFIG_I2C)		+= i2c/
obj-$(CONFIG_PHONE)		+= telephony/
obj-$(CONFIG_MD)		+= md/
obj-$(CONFIG_BT)		+= bluetooth/
obj-$(CONFIG_ISDN_BOOL)		+= isdn/
obj-$(CONFIG_MCA)		+= mca/
obj-$(CONFIG_EISA)		+= eisa/
obj-$(CONFIG_CPU_FREQ)		+= cpufreq/
