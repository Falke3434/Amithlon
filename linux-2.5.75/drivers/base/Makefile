# Makefile for the Linux device tree

obj-y			:= core.o sys.o interface.o power.o bus.o \
			   driver.o class.o platform.o \
			   cpu.o firmware.o init.o map.o
obj-$(CONFIG_FW_LOADER)	+= firmware_class.o
obj-$(CONFIG_NUMA)	+= node.o  memblk.o
