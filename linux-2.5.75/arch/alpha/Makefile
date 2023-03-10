#
# alpha/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

NM := $(NM) -B

LDFLAGS_vmlinux	:= -static -N #-relax
LDFLAGS_BLOB	:= --format binary --oformat elf64-alpha
cflags-y	:= -pipe -mno-fp-regs -ffixed-8

# Determine if we can use the BWX instructions with GAS.
old_gas := $(shell if $(AS) --version 2>&1 | grep 'version 2.7' > /dev/null; then echo y; else echo n; fi)

ifeq ($(old_gas),y)
$(error The assembler '$(AS)' does not support the BWX instruction)
endif

# Determine if GCC understands the -mcpu= option.
have_mcpu := $(shell if $(CC) -mcpu=ev5 -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi)

have_mcpu_pca56 := $(shell if $(CC) -mcpu=pca56 -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi)

have_mcpu_ev6 := $(shell if $(CC) -mcpu=ev6 -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi)

have_mcpu_ev67 := $(shell if $(CC) -mcpu=ev67 -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi)

have_msmall_data := $(shell if $(CC) -msmall-data -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo y; else echo n; fi)

cflags-$(have_msmall_data) += -msmall-data

# Turn on the proper cpu optimizations.
ifeq ($(have_mcpu),y)
  mcpu_done := n
  # If GENERIC, make sure to turn off any instruction set extensions that
  # the host compiler might have on by default.  Given that EV4 and EV5
  # have the same instruction set, prefer EV5 because an EV5 schedule is
  # more likely to keep an EV4 processor busy than vice-versa.
  ifeq ($(CONFIG_ALPHA_GENERIC),y)
    mcpu := ev5
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_SX164)$(have_mcpu_pca56),nyy)
    mcpu := pca56
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_POLARIS)$(have_mcpu_pca56),nyy)
    mcpu := pca56
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_EV4),ny)
    mcpu := ev4
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_EV56),ny)
    mcpu := ev56
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_EV5),ny)
    mcpu := ev5
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_EV67)$(have_mcpu_ev67),nyy)
    mcpu := ev67
    mcpu_done := y
  endif
  ifeq ($(mcpu_done)$(CONFIG_ALPHA_EV6),ny)
    ifeq ($(have_mcpu_ev6),y)
      mcpu := ev6
    else
      ifeq ($(have_mcpu_pca56),y)
        mcpu := pca56
      else
        mcpu=ev56
      endif
    endif
    mcpu_done := y
  endif
  cflags-$(mcpu_done) += -mcpu=$(mcpu)
endif


# For TSUNAMI, we must have the assembler not emulate our instructions.
# The same is true for IRONGATE, POLARIS, PYXIS.
# BWX is most important, but we don't really want any emulation ever.
CFLAGS += $(cflags-y) -Wa,-mev6

head-y := arch/alpha/kernel/head.o

core-y				+= arch/alpha/kernel/ arch/alpha/mm/
core-$(CONFIG_MATHEMU)		+= arch/alpha/math-emu/
drivers-$(CONFIG_OPROFILE)	+= arch/alpha/oprofile/
libs-y				+= arch/alpha/lib/

# export what is needed by arch/alpha/boot/Makefile
LIBS_Y := $(patsubst %/, %/lib.a, $(libs-y))
export LIBS_Y

boot := arch/alpha/boot

#Default target when executing make with no arguments
all boot: $(boot)/vmlinux.gz

$(boot)/vmlinux.gz: vmlinux
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(boot) $@

bootimage bootpfile: vmlinux
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(boot) $(boot)/$@


prepare: include/asm-$(ARCH)/asm_offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm_offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

archclean:
	$(Q)$(MAKE) -f scripts/Makefile.clean obj=$(boot)

CLEAN_FILES += include/asm-$(ARCH)/asm_offsets.h

define archhelp
  echo '* boot		- Compressed kernel image (arch/alpha/boot/vmlinux.gz)'
  echo '  bootimage	- SRM bootable image (arch/alpha/boot/bootimage)'
  echo '  bootpfile	- BOOTP bootable image (arch/alpha/boot/bootpfile)'
endef
