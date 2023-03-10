#
# Makefile for the linux kernel.
#

ENTRY_OBJ = entry-$(PROCESSOR).o

AFLAGS_head.o := -DTEXTADDR=$(TEXTADDR)

# Object file lists.

obj-y		:= arch.o compat.o dma.o $(ENTRY_OBJ) entry-common.o irq.o   \
		   process.o ptrace.o semaphore.o setup.o signal.o sys_arm.o \
		   time.o traps.o

obj-$(CONFIG_APM)		+= apm.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_ARCH_ACORN)	+= ecard.o time-acorn.o
obj-$(CONFIG_ARCH_CLPS7500)	+= time-acorn.o
obj-$(CONFIG_FOOTBRIDGE)	+= isa.o
obj-$(CONFIG_FIQ)		+= fiq.o
obj-$(CONFIG_MODULES)		+= armksyms.o module.o
obj-$(CONFIG_ARTHUR)		+= arthur.o
obj-$(CONFIG_ISA_DMA)		+= dma-isa.o
obj-$(CONFIG_PCI)		+= bios32.o

ifneq ($(MACHINE),ebsa110)
  obj-y		+= io.o
endif

head-y			:= head.o
obj-$(CONFIG_DEBUG_LL)	+= debug.o

extra-y := $(head-y) init_task.o

# Spell out some dependencies that `make dep' doesn't spot
$(obj)/entry-armv.o: 	$(obj)/entry-header.S include/asm-arm/constants.h
$(obj)/entry-armo.o: 	$(obj)/entry-header.S include/asm-arm/constants.h
$(obj)/entry-common.o: 	$(obj)/entry-header.S include/asm-arm/constants.h \
			$(obj)/calls.S
