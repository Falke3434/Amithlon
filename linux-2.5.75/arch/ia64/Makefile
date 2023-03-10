#
# ia64/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1998-2003 by David Mosberger-Tang <davidm@hpl.hp.com>
#

NM := $(CROSS_COMPILE)nm -B

export AWK

OBJCOPYFLAGS	:= --strip-all
LDFLAGS_vmlinux	:= -static
LDFLAGS_MODULE	+= -T arch/ia64/module.lds
AFLAGS_KERNEL	:= -mconstant-gp
EXTRA		:=

cflags-y	:= -pipe $(EXTRA) -ffixed-r13 -mfixed-range=f12-f15,f32-f127 \
		   -falign-functions=32 -frename-registers
CFLAGS_KERNEL	:= -mconstant-gp

GCC_VERSION=$(shell $(CC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f1 -d'.')
GCC_MINOR_VERSION=$(shell $(CC) -v 2>&1 | fgrep 'gcc version' | cut -f3 -d' ' | cut -f2 -d'.')

GAS_STATUS=$(shell arch/ia64/scripts/check-gas $(CC) $(OBJDUMP))

arch-cppflags	:= $(shell arch/ia64/scripts/toolchain-flags $(CC) $(OBJDUMP))
cflags-y	+= $(arch-cppflags)
AFLAGS		+= $(arch-cppflags)

ifeq ($(GAS_STATUS),buggy)
$(error Sorry, you need a newer version of the assember, one that is built from	\
	a source-tree that post-dates 18-Dec-2002.  You can find a pre-compiled	\
	static binary of such an assembler at:					\
										\
		ftp://ftp.hpl.hp.com/pub/linux-ia64/gas-030124.tar.gz)
endif

ifeq ($(GCC_VERSION),2)
$(error Sorry, your compiler is too old.  GCC v2.96 is known to generate bad code.)
endif

ifeq ($(GCC_VERSION),3)
 ifeq ($(GCC_MINOR_VERSION),4)
	cflags-$(CONFIG_ITANIUM)	+= -mtune=merced
	cflags-$(CONFIG_MCKINLEY)	+= -mtune=mckinley
 endif
endif

cflags-$(CONFIG_ITANIUM_BSTEP_SPECIFIC)	+= -mb-step

CFLAGS += $(cflags-y)
head-y := arch/ia64/kernel/head.o arch/ia64/kernel/init_task.o

libs-y				+= arch/ia64/lib/
core-y				+= arch/ia64/kernel/ arch/ia64/mm/
core-$(CONFIG_IA32_SUPPORT)	+= arch/ia64/ia32/
core-$(CONFIG_IA64_DIG) 	+= arch/ia64/dig/
core-$(CONFIG_IA64_GENERIC) 	+= arch/ia64/dig/
core-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/dig/
core-$(CONFIG_IA64_SGI_SN2)	+= arch/ia64/sn/

drivers-$(CONFIG_PCI)		+= arch/ia64/pci/
drivers-$(CONFIG_IA64_HP_SIM)	+= arch/ia64/hp/sim/
drivers-$(CONFIG_IA64_HP_ZX1)	+= arch/ia64/hp/common/ arch/ia64/hp/zx1/
drivers-$(CONFIG_IA64_GENERIC)	+= arch/ia64/hp/common/ arch/ia64/hp/zx1/ arch/ia64/hp/sim/ \
				   arch/ia64/sn/

boot := arch/ia64/boot

.PHONY: boot compressed check

compressed: vmlinux.gz

vmlinux.gz: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

check: vmlinux
	arch/ia64/scripts/unwcheck.sh $<

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

CLEAN_FILES += include/asm-ia64/.offsets.h.stamp include/asm-ia64/offsets.h vmlinux.gz bootloader

prepare: include/asm-ia64/offsets.h

include/asm-$(ARCH)/offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

arch/ia64/kernel/asm-offsets.s: include/asm-ia64/.offsets.h.stamp

include/asm-ia64/.offsets.h.stamp:
	[ -s include/asm-ia64/offsets.h ] \
	 || echo "#define IA64_TASK_SIZE 0" > include/asm-ia64/offsets.h
	touch $@

boot:	lib/lib.a vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@


define archhelp
  echo '  compressed	- Build compressed kernel image'
  echo '  boot		- Build vmlinux and bootloader for Ski simulator'
endef
