#
# Makefile for the Linux IEEE 1394 implementation
#

O_TARGET := ieee1394drv.o

export-objs := ieee1394_syms.o ohci1394.o

list-multi := ieee1394.o
ieee1394-objs := ieee1394_core.o ieee1394_transactions.o hosts.o \
		 highlevel.o csr.o nodemgr.o ieee1394_syms.o

obj-$(CONFIG_IEEE1394) += ieee1394.o
obj-$(CONFIG_IEEE1394_PCILYNX) += pcilynx.o
obj-$(CONFIG_IEEE1394_OHCI1394) += ohci1394.o
obj-$(CONFIG_IEEE1394_VIDEO1394) += video1394.o
obj-$(CONFIG_IEEE1394_RAWIO) += raw1394.o
obj-$(CONFIG_IEEE1394_SBP2) += sbp2.o

include $(TOPDIR)/Rules.make

ieee1394.o: $(ieee1394-objs)
	$(LD) -r -o $@ $(ieee1394-objs)
