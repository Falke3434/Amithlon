#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				:= cache.o extable.o init.o fault.o loadmmu.o \
				   pgtable.o tlbex-r4k.o

obj-$(CONFIG_CPU_R4300)		+= c-r4k.o pg-r4k.o tlb-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= c-r4k.o pg-r4k.o tlb-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5000)		+= c-r4k.o pg-r4k.o tlb-r4k.o sc-r5k.o \
				   tlb-glue-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= c-r4k.o pg-r4k.o tlb-r4k.o sc-r5k.o \
				   tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5432)		+= c-r4k.o pg-r4k.o tlb-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= c-r4k.o pg-r4k.o tlb-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R10000)	+= c-r4k.o pg-r4k.o tlb-andes.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_SB1)		+= c-sb1.o pg-sb1.o tlb-sb1.o tlb-glue-sb1.o \
				   cex-sb1.o cerr-sb1.o
obj-$(CONFIG_CPU_MIPS64)	+= c-r4k.o pg-r4k.o tlb-r4k.o tlb-glue-r4k.o

#
# Debug TLB exception handler, currently unused
#
#obj-y				+=  tlb-dbg-r4k.o

obj-$(CONFIG_CPU_RM7000)	+= sc-rm7k.o
obj-$(CONFIG_R5000_CPU_SCACHE)	+= sc-r5k.o
obj-$(CONFIG_SGI_IP22)		+= sc-ip22.o

AFLAGS_tlb-glue-r4k.o := -P

EXTRA_AFLAGS := $(CFLAGS)
