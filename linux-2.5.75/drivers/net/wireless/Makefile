#
# Makefile for the Linux Wireless network device drivers.
#

obj-$(CONFIG_STRIP) += strip.o
obj-$(CONFIG_ARLAN) += arlan.o arlan-proc.o

# Obsolete cards
obj-$(CONFIG_WAVELAN)		+= wavelan.o
obj-$(CONFIG_PCMCIA_NETWAVE)	+= netwave_cs.o
obj-$(CONFIG_PCMCIA_WAVELAN)	+= wavelan_cs.o

obj-$(CONFIG_HERMES)		+= orinoco.o hermes.o
obj-$(CONFIG_PCMCIA_HERMES)	+= orinoco_cs.o
obj-$(CONFIG_APPLE_AIRPORT)	+= airport.o
obj-$(CONFIG_PLX_HERMES)	+= orinoco_plx.o
obj-$(CONFIG_PCI_HERMES)	+= orinoco_pci.o
obj-$(CONFIG_TMD_HERMES)	+= orinoco_tmd.o

obj-$(CONFIG_AIRO)		+= airo.o
obj-$(CONFIG_AIRO_CS)		+= airo_cs.o airo.o

# 16-bit wireless PCMCIA client drivers
obj-$(CONFIG_PCMCIA_RAYCS)	+= ray_cs.o
obj-$(CONFIG_PCMCIA_ATMEL)      += atmel_cs.o atmel.o 

