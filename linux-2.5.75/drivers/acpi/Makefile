#
# Makefile for the Linux ACPI interpreter
# 

export ACPI_CFLAGS

ACPI_CFLAGS	:= -Os

ifdef CONFIG_ACPI_DEBUG
  ACPI_CFLAGS	+= -DACPI_DEBUG_OUTPUT
endif

EXTRA_CFLAGS	+= $(ACPI_CFLAGS)

obj-y		:= acpi_ksyms.o 

#
# ACPI Boot-Time Table Parsing
#
obj-$(CONFIG_ACPI_BOOT)		+= tables.o blacklist.o

#
# ACPI Core Subsystem (Interpreter)
#
obj-$(CONFIG_ACPI_INTERPRETER)	+= osl.o utils.o \
				   dispatcher/ events/ executer/ hardware/ \
				   namespace/ parser/ resources/ tables/ \
				   utilities/

#
# ACPI Bus and Device Drivers
#
obj-$(CONFIG_ACPI_BUS)		+= sleep/
obj-$(CONFIG_ACPI_BUS)		+= bus.o
obj-$(CONFIG_ACPI_AC) 		+= ac.o
obj-$(CONFIG_ACPI_BATTERY)	+= battery.o
obj-$(CONFIG_ACPI_BUTTON)	+= button.o
obj-$(CONFIG_ACPI_EC)		+= ec.o
obj-$(CONFIG_ACPI_FAN)		+= fan.o
obj-$(CONFIG_ACPI_PCI)		+= pci_root.o pci_link.o pci_irq.o pci_bind.o
obj-$(CONFIG_ACPI_POWER)	+= power.o
obj-$(CONFIG_ACPI_PROCESSOR)	+= processor.o
obj-$(CONFIG_ACPI_THERMAL)	+= thermal.o
obj-$(CONFIG_ACPI_SYSTEM)	+= system.o event.o
obj-$(CONFIG_ACPI_DEBUG)	+= debug.o
obj-$(CONFIG_ACPI_NUMA)		+= numa.o
obj-$(CONFIG_ACPI_ASUS)		+= asus_acpi.o
obj-$(CONFIG_ACPI_TOSHIBA)	+= toshiba_acpi.o
obj-$(CONFIG_ACPI_BUS)		+= scan.o 
