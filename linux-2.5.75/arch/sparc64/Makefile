# $Id: Makefile,v 1.52 2002/02/09 19:49:31 davem Exp $
# sparc64/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# 64-bit Sparc.
#
# Copyright (C) 1996,1998 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1998 Jakub Jelinek (jj@ultra.linux.cz)
#

AFLAGS_vmlinux.lds.o += -Usparc

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

CC		:= $(shell if gcc -m64 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo gcc; else echo sparc64-linux-gcc; fi )

NEW_GCC := $(shell if $(CC) -m64 -mcmodel=medlow -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo y; else echo n; fi; )
NEW_GAS := $(shell if $(LD) -V 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)
UNDECLARED_REGS := $(shell if $(CC) -c -x assembler /dev/null -Wa,--help | grep undeclared-regs > /dev/null; then echo y; else echo n; fi; )
INLINE_LIMIT := $(shell if $(CC) -m64 -finline-limit=100000 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo y; else echo n; fi; )

export NEW_GCC

ifneq ($(NEW_GAS),y)
AS		= sparc64-linux-as
LD		= sparc64-linux-ld
NM		= sparc64-linux-nm
AR		= sparc64-linux-ar
RANLIB		= sparc64-linux-ranlib
else
AS		:= $(AS) -64
LDFLAGS		:= -m elf64_sparc
LDFLAGS_BLOB	:= --format binary --oformat elf64-sparc
endif

ifneq ($(UNDECLARED_REGS),y)
CC_UNDECL	=
else
CC_UNDECL	= -Wa,--undeclared-regs
AS		:= $(AS) --undeclared-regs
endif

ifneq ($(NEW_GCC),y)
  CFLAGS := $(CFLAGS) -pipe -mno-fpu -mtune=ultrasparc -mmedlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare
else
  CFLAGS := $(CFLAGS) -m64 -pipe -mno-fpu -mcpu=ultrasparc -mcmodel=medlow \
	    -ffixed-g4 -fcall-used-g5 -fcall-used-g7 -Wno-sign-compare \
	    $(CC_UNDECL)
  AFLAGS += -m64 -mcpu=ultrasparc $(CC_UNDECL)
endif

ifeq ($(INLINE_LIMIT),y)
  CFLAGS := $(CFLAGS) -finline-limit=100000
endif

ifeq ($(CONFIG_MCOUNT),y)
  CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS))
  CFLAGS := $(CFLAGS) -pg
endif

head-y := arch/sparc64/kernel/head.o arch/sparc64/kernel/init_task.o

core-y				+= arch/sparc64/kernel/ arch/sparc64/mm/
core-$(CONFIG_SOLARIS_EMUL)	+= arch/sparc64/solaris/
core-y				+= arch/sparc64/math-emu/
libs-y				+= arch/sparc64/prom/ arch/sparc64/lib/

# FIXME: is drivers- right?
drivers-$(CONFIG_OPROFILE)	+= arch/sparc64/oprofile/

boot := arch/sparc64/boot

image tftpboot.img vmlinux.aout: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

define archhelp
  echo  '* vmlinux       - Standard sparc64 kernel'
  echo  '  vmlinux.aout  - a.out kernel for sparc64'
  echo  '  tftpboot.img  - Image prepared for tftp'
endef

