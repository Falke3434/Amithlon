# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changes for PPC by Gary Thomas
# Rewritten by Cort Dougan and Paul Mackerras
#

# This must match PAGE_OFFSET in include/asm-ppc/page.h.
KERNELLOAD	:= $(CONFIG_KERNEL_START)

LDFLAGS_BLOB	:= --format binary --oformat elf32-powerpc
LDFLAGS_vmlinux	:= -Ttext $(KERNELLOAD) -Bstatic
CPPFLAGS	+= -Iarch/$(ARCH)
AFLAGS		+= -Iarch/$(ARCH)
cflags-y	+= -Iarch/$(ARCH) -msoft-float -pipe \
		-ffixed-r2 -Wno-uninitialized -mmultiple -mstring
CPP		= $(CC) -E $(CFLAGS)

cflags-$(CONFIG_4xx)		+= -Wa,-m405
cflags-$(CONFIG_PPC64BRIDGE)	+= -Wa,-mppc64bridge
# Use sed to remove the quotes.
cflags-$(CONFIG_MORE_COMPILE_OPTIONS) += \
	$(shell echo $(CONFIG_COMPILE_OPTIONS) | sed -e 's/"//g')

CFLAGS += $(cflags-y)


head-y				:= arch/ppc/kernel/head.o
head-$(CONFIG_8xx)		:= arch/ppc/kernel/head_8xx.o
head-$(CONFIG_4xx)		:= arch/ppc/kernel/head_4xx.o
head-$(CONFIG_440)		:= arch/ppc/kernel/head_44x.o

head-$(CONFIG_6xx)		+= arch/ppc/kernel/idle_6xx.o

core-y				+= arch/ppc/kernel/ arch/ppc/platforms/ \
				   arch/ppc/mm/ arch/ppc/lib/ arch/ppc/syslib/
core-$(CONFIG_4xx)		+= arch/ppc/platforms/4xx/
core-$(CONFIG_MATH_EMULATION)	+= arch/ppc/math-emu/
core-$(CONFIG_XMON)		+= arch/ppc/xmon/
core-$(CONFIG_APUS)		+= arch/ppc/amiga/
drivers-$(CONFIG_8xx)		+= arch/ppc/8xx_io/
drivers-$(CONFIG_4xx)		+= arch/ppc/4xx_io/
drivers-$(CONFIG_8260)		+= arch/ppc/8260_io/
drivers-$(CONFIG_OCP)		+= arch/ppc/ocp/

BOOT_TARGETS = zImage zImage.initrd znetboot znetboot.initrd pImage vmlinux.sm

.PHONY: $(BOOT_TARGETS)

all: zImage

AFLAGS_vmlinux.lds.o	:= -Upowerpc

# All the instructions talk about "make bzImage".
bzImage: zImage

$(BOOT_TARGETS): vmlinux
	$(Q)$(MAKE) $(build)=arch/ppc/boot $@

%_config: arch/ppc/configs/%_defconfig
	rm -f .config arch/ppc/defconfig
	cp -f arch/ppc/configs/$(@:config=defconfig) .config

archclean:
	$(Q)$(MAKE) $(clean)=arch/ppc/boot

prepare: include/asm-$(ARCH)/offsets.h checkbin

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offsets.h: arch/$(ARCH)/kernel/asm-offsets.s 
	$(call filechk,gen-asm-offsets)

ifdef CONFIG_6xx
# Ensure this is binutils 2.12.1 (or 2.12.90.0.7) or later
NEW_AS	:= $(shell echo dssall | $(AS) -o /dev/null >/dev/null 2>&1 ; echo $$?)
GOODVER	:= 2.12.1
else
NEW_AS	:= 0
endif

ifneq ($(NEW_AS),0)
checkbin:
	@echo -n '*** ${VERSION}.${PATCHLEVEL} kernels no longer build'
	@echo 'correctly with old versions of binutils.'
	@echo '*** Please upgrade your binutils to ${GOODVER} or newer'
	@false
else
checkbin:
	@true
endif

CLEAN_FILES +=	include/asm-$(ARCH)/offsets.h \
		arch/$(ARCH)/kernel/asm-offsets.s
