#
# arch/m68knommu/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# (C) Copyright 2002, Greg Ungerer <gerg@snapgear.com>
#

platform-$(CONFIG_M68328)	:= 68328
platform-$(CONFIG_M68EZ328)	:= 68EZ328
platform-$(CONFIG_M68VZ328)	:= 68VZ328
platform-$(CONFIG_M68360)	:= 68360
platform-$(CONFIG_M5206)	:= 5206
platform-$(CONFIG_M5206e)	:= 5206e
platform-$(CONFIG_M5249)	:= 5249
platform-$(CONFIG_M5272)	:= 5272
platform-$(CONFIG_M5282)	:= 5282
platform-$(CONFIG_M5307)	:= 5307
platform-$(CONFIG_M5407)	:= 5407
PLATFORM := $(platform-y)

board-$(CONFIG_PILOT)		:= pilot
board-$(CONFIG_UCSIMM)		:= ucsimm
board-$(CONFIG_UCDIMM)		:= ucdimm
board-$(CONFIG_UCQUICC)		:= uCquicc
board-$(CONFIG_DRAGEN2)		:= de2
board-$(CONFIG_ARNEWSH)		:= ARNEWSH
board-$(CONFIG_MOTOROLA)	:= MOTOROLA
board-$(CONFIG_ELITE)		:= eLITE
board-$(CONFIG_eLIA)		:= eLIA
board-$(CONFIG_NETtel)		:= NETtel
board-$(CONFIG_SECUREEDGEMP3)	:= MP3
board-$(CONFIG_CLEOPATRA)	:= CLEOPATRA
BOARD := $(board-y)

model-$(CONFIG_RAMKERNEL)	:= ram
model-$(CONFIG_ROMKERNEL)	:= rom
model-$(CONFIG_HIMEMKERNEL)	:= himem
MODEL := $(model-y)

#
# Some code support is grouped together for a common cpu-subclass (for
# example all ColdFire cpu's are very similar). Determine the sub-class
# for the selected cpu. ONLY need to define this for the non-base member
# of the family.
#
cpuclass-$(CONFIG_M5206)	:= 5307
cpuclass-$(CONFIG_M5206e)	:= 5307
cpuclass-$(CONFIG_M5249)	:= 5307
cpuclass-$(CONFIG_M5272)	:= 5307
cpuclass-$(CONFIG_M5282)	:= 5307
cpuclass-$(CONFIG_M5407)	:= 5307
cpuclass-$(CONFIG_M68EZ328)	:= 68328
cpuclass-$(CONFIG_M68VZ328)	:= 68328
CPUCLASS := $(cpuclass-y)

ifneq ($(CPUCLASS),)
CLASSDIR := arch/m68knommu/platform/$(cpuclass-y)/
endif

export PLATFORM BOARD MODEL CPUCLASS

#
# Some CFLAG additions based on specific CPU type.
#
cflags-$(CONFIG_M5206)		:= -m5200 -Wa,-S -Wa,-m5200
cflags-$(CONFIG_M5206e)		:= -m5200 -Wa,-S -Wa,-m5200
cflags-$(CONFIG_M5249)		:= -m5200 -Wa,-S -Wa,-m5200
cflags-$(CONFIG_M5272)		:= -m5307 -Wa,-S -Wa,-m5307
cflags-$(CONFIG_M5282)		:= -m5307 -Wa,-S -Wa,-m5307
cflags-$(CONFIG_M5307)		:= -m5307 -Wa,-S -Wa,-m5307
cflags-$(CONFIG_M5407)		:= -m5200 -Wa,-S -Wa,-m5200
cflags-$(CONFIG_M68328)		:= -m68000
cflags-$(CONFIG_M68EZ328)	:= -m68000
cflags-$(CONFIG_M68VZ328)	:= -m68000
cflags-$(CONFIG_M68360)		:= -m68332

AFLAGS += $(cflags-y)

CFLAGS += $(cflags-y)
CFLAGS += -fno-builtin
CFLAGS += -O2 -g
CFLAGS += -D__linux__
CFLAGS += -DUTS_SYSNAME=\"uClinux\"

LDFLAGS_BLOB	:= --format binary --oformat elf32-m68k

head-y := arch/m68knommu/platform/$(platform-y)/$(board-y)/crt0_$(model-y).o

CLEAN_FILES := include/asm-$(ARCH)/asm-offsets.h \
	       arch/$(ARCH)/kernel/asm-offsets.s

core-y	+= arch/m68knommu/kernel/ \
	   arch/m68knommu/mm/ \
	   $(CLASSDIR) \
	   arch/m68knommu/platform/$(PLATFORM)/
libs-y	+= arch/m68knommu/lib/

prepare: include/asm-$(ARCH)/asm-offsets.h

archclean:
	$(call descend arch/$(ARCH)/boot, subdirclean)

include/asm-$(ARCH)/asm-offsets.h: arch/$(ARCH)/kernel/asm-offsets.s \
				   include/asm include/linux/version.h \
				   include/config/MARKER
	$(call filechk,gen-asm-offsets)
