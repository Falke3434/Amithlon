#
# Makefile for the kernel serial device drivers.
#
#  $Id: Makefile,v 1.8 2002/07/21 21:32:30 rmk Exp $
#

serial-8250-y :=
serial-8250-$(CONFIG_GSC) += 8250_gsc.o
serial-8250-$(CONFIG_PCI) += 8250_pci.o
serial-8250-$(CONFIG_PNP) += 8250_pnp.o
serial-8250-$(CONFIG_SERIAL_8250_HCDP) += 8250_hcdp.o
serial-8250-$(CONFIG_SERIAL_8250_ACPI) += 8250_acpi.o

obj-$(CONFIG_SERIAL_CORE) += core.o
obj-$(CONFIG_SERIAL_21285) += 21285.o
obj-$(CONFIG_SERIAL_8250) += 8250.o $(serial-8250-y)
obj-$(CONFIG_SERIAL_8250_CS) += 8250_cs.o
obj-$(CONFIG_SERIAL_8250_ACORN) += 8250_acorn.o
obj-$(CONFIG_SERIAL_ANAKIN) += anakin.o
obj-$(CONFIG_SERIAL_AMBA) += amba.o
obj-$(CONFIG_SERIAL_CLPS711X) += clps711x.o
obj-$(CONFIG_SERIAL_SA1100) += sa1100.o
obj-$(CONFIG_SERIAL_UART00) += uart00.o
obj-$(CONFIG_SERIAL_SUNCORE) += suncore.o
obj-$(CONFIG_SERIAL_SUNZILOG) += sunzilog.o
obj-$(CONFIG_SERIAL_SUNSU) += sunsu.o
obj-$(CONFIG_SERIAL_SUNSAB) += sunsab.o
obj-$(CONFIG_SERIAL_MUX) += mux.o
obj-$(CONFIG_SERIAL_68328) += 68328serial.o
obj-$(CONFIG_SERIAL_68360) += 68360serial.o
obj-$(CONFIG_SERIAL_COLDFIRE) += mcfserial.o
obj-$(CONFIG_V850E_NB85E_UART) += nb85e_uart.o
obj-$(CONFIG_SERIAL98) += serial98.o
