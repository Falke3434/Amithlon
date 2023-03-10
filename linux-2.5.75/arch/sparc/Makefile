#
# sparc/Makefile
#
# Makefile for the architecture dependent flags and dependencies on the
# Sparc.
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#

# If the solaris /bin/sh wasn't so broken, I wouldn't need the following
# line...
SHELL  =/bin/bash

#
# Uncomment the first CFLAGS if you are doing kgdb source level
# debugging of the kernel to get the proper debugging information.

IS_EGCS := $(shell if $(CC) -m32 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo y; else echo n; fi; )
NEW_GAS := $(shell if $(LD) --version 2>&1 | grep 'elf64_sparc' > /dev/null; then echo y; else echo n; fi)

ifeq ($(NEW_GAS),y)
AS              := $(AS) -32
LDFLAGS		:= -m elf32_sparc
endif

LDFLAGS_BLOB	:= --format binary --oformat elf32-sparc

#CFLAGS := $(CFLAGS) -g -pipe -fcall-used-g5 -fcall-used-g7
ifneq ($(IS_EGCS),y)
CFLAGS := $(CFLAGS) -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
else
CFLAGS := $(CFLAGS) -m32 -pipe -mno-fpu -fcall-used-g5 -fcall-used-g7
AFLAGS := $(AFLAGS) -m32
endif

#LDFLAGS_vmlinux = -N -Ttext 0xf0004000
#  Since 2.5.40, the first stage is left not btfix-ed.
#  Actual linking is done with "make image".
LDFLAGS_vmlinux = -r

head-y := arch/sparc/kernel/head.o arch/sparc/kernel/init_task.o
HEAD_Y := $(head-y)

core-y += arch/sparc/kernel/ arch/sparc/mm/ arch/sparc/math-emu/
libs-y += arch/sparc/prom/ arch/sparc/lib/

# Export what is needed by arch/sparc/boot/Makefile
# Renaming is done to avoid confusing pattern matching rules in 2.5.45 (multy-)
INIT_Y		:= $(patsubst %/, %/built-in.o, $(init-y))
CORE_Y		:= $(core-y)
CORE_Y		+= kernel/ mm/ fs/ ipc/ security/ crypto/
CORE_Y		:= $(patsubst %/, %/built-in.o, $(CORE_Y))
DRIVERS_Y	:= $(patsubst %/, %/built-in.o, $(drivers-y))
NET_Y		:= $(patsubst %/, %/built-in.o, $(net-y))
LIBS_Y1		:= $(patsubst %/, %/lib.a, $(libs-y))
LIBS_Y2		:= $(patsubst %/, %/built-in.o, $(libs-y))
LIBS_Y		:= $(LIBS_Y1) $(LIBS_Y2)
export INIT_Y CORE_Y DRIVERS_Y NET_Y LIBS_Y HEAD_Y

# Default target
all: image

boot := arch/sparc/boot

image tftpboot.img: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	$(Q)$(MAKE) $(clean)=$(boot)

prepare: include/asm-$(ARCH)/asm_offsets.h

arch/$(ARCH)/kernel/asm-offsets.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/asm_offsets.h: arch/$(ARCH)/kernel/asm-offsets.s
	$(call filechk,gen-asm-offsets)

CLEAN_FILES +=	include/asm-$(ARCH)/asm_offsets.h	\
		arch/$(ARCH)/kernel/asm-offsets.s

# Don't use tabs in echo arguments.
define archhelp
  echo  '* image        - kernel image ($(boot)/image)'
  echo  '  tftpboot.img - image prepared for tftp'
endef
