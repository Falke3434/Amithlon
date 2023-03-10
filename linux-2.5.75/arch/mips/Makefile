#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994, 1995, 1996 by Ralf Baechle
# DECStation modifications by Paul M. Antoine, 1996
# Copyright (C) 2002  Maciej W. Rozycki
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" cleaning up for this architecture.
#

#
# Select the object file format to substitute into the linker script.
#
ifdef CONFIG_CPU_LITTLE_ENDIAN
tool-prefix	= mipsel-linux-
JIFFIES32	= jiffies_64
LDFLAGS_BLOB   := --format binary --oformat elf32-tradlittlemips
else
tool-prefix	= mips-linux-
JIFFIES32	= jiffies_64 + 4
LDFLAGS_BLOB   := --format binary --oformat elf32-tradbigmips
endif

ifdef CONFIG_CROSSCOMPILE
CROSS_COMPILE	:= $(tool-prefix)
endif

#
# GCC uses -G 0 -mabicalls -fpic as default.  We don't want PIC in the kernel
# code since it only slows down the whole thing.  At some point we might make
# use of global pointer optimizations but their use of $28 conflicts with
# the current pointer optimization.
#
# The DECStation requires an ECOFF kernel for remote booting, other MIPS
# machines may also.  Since BFD is incredibly buggy with respect to
# crossformat linking we rely on the elf2ecoff tool for format conversion.
#
cflags-y			:= -I $(TOPDIR)/include/asm/gcc
cflags-y			+= -G 0 -mno-abicalls -fno-pic -pipe
LDFLAGS_vmalinux		+= -G 0 -static # -N
MODFLAGS			+= -mlong-calls

cflags-$(CONFIG_KGDB)		+= -g
cflags-$(CONFIG_SB1XXX_CORELIS)	+= -mno-sched-prolog -fno-omit-frame-pointer

check_gcc = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi)

#
# CPU-dependent compiler/assembler options for optimization.
#
cflags-$(CONFIG_CPU_R3000)	+= -mcpu=r3000 -mips1
cflags-$(CONFIG_CPU_TX39XX)	+= -mcpu=r3000 -mips1
cflags-$(CONFIG_CPU_R6000)	+= -mcpu=r6000 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_R4300)	+= -mcpu=r4300 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_VR41XX)	+= -mcpu=r4600 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_R4X00)	+= -mcpu=r4600 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_TX49XX)	+= -mcpu=r4600 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS32)	+= -mcpu=r4600 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_MIPS64)	+= -mcpu=r4600 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_R5000)	+= -mcpu=r5000 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_R5432)	+= -mcpu=r5000 -mips2 -Wa,--trap
# Cannot use -mmad with currently recommended tools
cflags-$(CONFIG_CPU_NEVADA)	+= -mcpu=r5000 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_RM7000)	+= -mcpu=r5000 -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_RM7000)	+= $(call check_gcc, -mcpu=r7000, -mcpu=r5000) \
				   -mips2 -Wa,--trap
cflags-$(CONFIG_CPU_SB1)	+= $(call check_gcc, -mcpu=sb1, -mcpu=r8000) \
				   -mips2 -Wa,--trap
ifdef CONFIG_CPU_SB1
ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
MODFLAGS	+= -msb1-pass1-workarounds
endif
endif

AFLAGS		+= $(cflags-y)
CFLAGS		+= $(cflags-y)


#
# ramdisk/initrd support
# You need a compressed ramdisk image, named ramdisk.gz in
# arch/mips/ramdisk
#
ifdef CONFIG_EMBEDDED_RAMDISK
CORE_FILES	+= arch/mips/ramdisk/ramdisk.o
SUBDIRS		+= arch/mips/ramdisk
endif

#
# Firmware support
#
libs-$(CONFIG_ARC)		+= arch/mips/arc/
libs-$(CONFIG_SIBYTE_CFE)	+= arch/mips/sibyte/cfe/

#
# Board-dependent options and extra files
#

#
# Acer PICA 61, Mips Magnum 4000 and Olivetti M700.
#
core-$(CONFIG_MIPS_JAZZ)	+= arch/mips/jazz/
load-$(CONFIG_MIPS_JAZZ)	+= 0x80080000


#
# Au1500 (Alchemy Semi PB1500) eval board
#
core-$(CONFIG_MIPS_PB1500)	+= arch/mips/au1000/common/
libs-$(CONFIG_MIPS_PB1500)	+= arch/mips/au1000/pb1500/
load-$(CONFIG_MIPS_PB1500)	+= 0x80100000

#
# Baget/MIPS
#
libs-$(CONFIG_BAGET_MIPS)	+= arch/mips/baget/ arch/mips/baget/prom/
load-$(CONFIG_BAGET_MIPS)	+= 0x80001000

#
# Cobalt Server
#
core-$(CONFIG_MIPS_COBALT)	+= arch/mips/cobalt/
load-$(CONFIG_MIPS_COBALT)	+= 0x80080000

#
# DECstation family
#
core-$(CONFIG_DECSTATION)	+= arch/mips/dec/
libs-$(CONFIG_DECSTATION)	+= arch/mips/dec/prom/
load-$(CONFIG_DECSTATION)	+= 0x80040000
CLEAN_FILES			+= drivers/tc/lk201-map.c

#
# Galileo EV64120 Board
#
core-$(CONFIG_MIPS_EV64120)	+= arch/mips/galileo-boards/ev64120/
load-$(CONFIG_MIPS_EV64120)	+= 0x80100000

#
# Galileo EV96100 Board
#
core-$(CONFIG_MIPS_EV96100)	+= arch/mips/galileo-boards/generic/ \
				   arch/mips/galileo-boards/ev96100/
load-$(CONFIG_MIPS_EV96100)	+= 0x80100000

#
# Globespan IVR eval board with QED 5231 CPU
#
core-$(CONFIG_ITE_BOARD_GEN)	+= arch/mips/ite-boards/generic/
core-$(CONFIG_MIPS_IVR)		+= arch/mips/ite-boards/ivr/
load-$(CONFIG_MIPS_IVR)		+= 0x80100000

#
# HP LaserJet
#
core-$(CONFIG_HP_LASERJET)	+= arch/mips/hp-lj/
load-$(CONFIG_HP_LASERJET)	+= 0x80030000

#
# ITE 8172 eval board with QED 5231 CPU
#
core-$(CONFIG_MIPS_ITE8172)	+= arch/mips/ite-boards/qed-4n-s01b/
load-$(CONFIG_MIPS_ITE8172)	+= 0x80100000

#
# MIPS Atlas board
#
core-$(CONFIG_MIPS_BOARDS_GEN)	+= arch/mips/mips-boards/generic/
core-$(CONFIG_MIPS_ATLAS)	+= arch/mips/mips-boards/atlas/
load-$(CONFIG_MIPS_ATLAS)	+= 0x80100000

#
# MIPS Malta board
#
core-$(CONFIG_MIPS_MALTA)	+= arch/mips/mips-boards/malta/
load-$(CONFIG_MIPS_MALTA)	:= 0x80100000

#
# MIPS SEAD board
#
core-$(CONFIG_MIPS_SEAD)	+= arch/mips/mips-boards/sead/
load-$(CONFIG_MIPS_SEAD)	+= 0x80100000

#
# Momentum Ocelot board
#
# The Ocelot setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
#
core-$(CONFIG_MOMENCO_OCELOT)	+= arch/mips/gt64120/common/ \
				   arch/mips/gt64120/momenco_ocelot/
load-$(CONFIG_MOMENCO_OCELOT)	+= 0x80100000

#
# Momentum Ocelot-G board
#
# The Ocelot-G setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
#
core-$(CONFIG_MOMENCO_OCELOT_G)	+= arch/mips/momentum/ocelot_g/
load-$(CONFIG_MOMENCO_OCELOT_G)	+= 0x80100000

#
# Momentum Ocelot-C and -CS boards
#
# The Ocelot-C[S] setup.o must be linked early - it does the ioremap() for the
# mips_io_port_base.
core-$(CONFIG_MOMENCO_OCELOT_C)	+= arch/mips/momentum/ocelot_c/
load-$(CONFIG_MOMENCO_OCELOT_C)	+= 0x80100000

#
# NEC DDB Vrc-5074
#
core-$(CONFIG_DDB5XXX_COMMON)	+= arch/mips/ddb5xxx/common/
core-$(CONFIG_DDB5074)		+= arch/mips/ddb5xxx/ddb5074/
load-$(CONFIG_DDB5074)		+= 0x80080000

#
# NEC DDB Vrc-5476
#
core-$(CONFIG_DDB5476)		+= arch/mips/ddb5xxx/ddb5476/
load-$(CONFIG_DDB5476)		+= 0x80080000

#
# NEC DDB Vrc-5477
#
core-$(CONFIG_DDB5477)		+= arch/mips/ddb5xxx/ddb5477/
load-$(CONFIG_DDB5477)		+= 0x80100000

core-$(CONFIG_LASAT)		+= arch/mips/lasat/
load-$(CONFIG_LASAT)		+= 0x80000000

#
# NEC Osprey (vr4181) board
#
core-$(CONFIG_NEC_OSPREY)	+= arch/mips/vr4181/common/ \
				   arch/mips/vr4181/osprey/
load-$(CONFIG_NEC_OSPREY)	+= 0x80002000

#
# NEC Eagle/Hawk (VR4122/VR4131) board
#
core-$(CONFIG_VR41XX_COMMON)	+= arch/mips/vr41xx/common/
core-$(CONFIG_NEC_EAGLE)	+= arch/mips/vr41xx/nec-eagle/
load-$(CONFIG_NEC_EAGLE)	+= 0x80000000

#
# ZAO Networks Capcella (VR4131)
#
core-$(CONFIG_ZAO_CAPCELLA)	+= arch/mips/vr41xx/zao-capcella/
load-$(CONFIG_ZAO_CAPCELLA)	+= 0x80000000

#
# Victor MP-C303/304 (VR4122)
#
core-$(CONFIG_VICTOR_MPC30X)	+= arch/mips/vr41xx/victor-mpc30x/
load-$(CONFIG_VICTOR_MPC30X)	+= 0x80001000

#
# IBM WorkPad z50 (VR4121)
#
core-$(CONFIG_IBM_WORKPAD)	+= arch/mips/vr41xx/ibm-workpad/
load-$(CONFIG_IBM_WORKPAD)	+= 0x80004000

#
# CASIO CASSIPEIA E-55/65 (VR4111)
#
core-$(CONFIG_CASIO_E55)	+= arch/mips/vr41xx/casio-e55/
load-$(CONFIG_CASIO_E55)	+= 0x80004000

#
# TANBAC TB0226 Mbase (VR4131)
#
core-$(CONFIG_TANBAC_TB0226)	+= arch/mips/vr41xx/tanbac-tb0226/
load-$(CONFIG_TANBAC_TB0226)	+= 0x80000000

#
# TANBAC TB0229 VR4131DIMM (VR4131)
#
core-$(CONFIG_TANBAC_TB0229)	+= arch/mips/vr41xx/tanbac-tb0229/
load-$(CONFIG_TANBAC_TB0229)	+= 0x80000000

#
# SGI IP22 (Indy/Indigo2)
#
# Set the load address to >= 0x88069000 if you want to leave space for symmon,
# 0x88002000 for production kernels.  Note that the value must be 8kb aligned
# or the handling of the current variable will break.
#
core-$(CONFIG_SGI_IP22)		+= arch/mips/sgi-ip22/
load-$(CONFIG_SGI_IP22)		+= 0x88002000

#
# Sibyte SB1250 SOC
#
# This is a LIB so that it links at the end, and initcalls are later
# the sequence; but it is built as an object so that modules don't get
# removed (as happens, even if they have __initcall/module_init)
#
core-$(CONFIG_SIBYTE_BCM112X)	+= arch/mips/sibyte/sb1250/
core-$(CONFIG_SIBYTE_SB1250)	+= arch/mips/sibyte/sb1250/

#
# Sibyte BCM91120x (Carmel) board
# Sibyte BCM91120C (CRhine) board
# Sibyte BCM91125C (CRhone) board
# Sibyte BCM91125E (Rhone) board
# Sibyte SWARM board
#
libs-$(CONFIG_SIBYTE_CARMEL)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CARMEL)	:= 0x80100000
libs-$(CONFIG_SIBYTE_CRHINE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHINE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_CRHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_CRHONE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_RHONE)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_RHONE)	:= 0x80100000
libs-$(CONFIG_SIBYTE_SENTOSA)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SENTOSA)	:= 0x80100000
libs-$(CONFIG_SIBYTE_SWARM)	+= arch/mips/sibyte/swarm/
load-$(CONFIG_SIBYTE_SWARM)	:= 0x80100000

#
# SNI RM200 PCI
#
core-$(CONFIG_SNI_RM200_PCI)	+= arch/mips/sni/
load-$(CONFIG_SNI_RM200_PCI)	+= 0x80080000

#
# Toshiba JMR-TX3927 board
#
core-$(CONFIG_TOSHIBA_JMR3927)	+= arch/mips/jmr3927/rbhma3100/ \
				   arch/mips/jmr3927/common/
load-$(CONFIG_TOSHIBA_JMR3927)	+= 0x80050000

#
# Toshiba RBTX4927 board or
# Toshiba RBTX4937 board
#
core-$(CONFIG_TOSHIBA_RBTX4927)	+= arch/mips/tx4927/toshiba_rbtx4927/ 
core-$(CONFIG_TOSHIBA_RBTX4927)	+= arch/mips/tx4927/common/
load-$(CONFIG_TOSHIBA_RBTX4927)	:= 0x80020000

drivers-$(CONFIG_PCI)		+= arch/mips/pci/


#
# Choosing incompatible machines durings configuration will result in
# error messages during linking.  Select a default linkscript if
# none has been choosen above.
#

AFLAGS_vmlinux.lds.o := -D"LOADADDR=$(load-y)" -D"JIFFIES32=$(JIFFIES32)"

head-y := arch/mips/kernel/head.o arch/mips/kernel/init_task.o

libs-y		+= arch/mips/lib/
core-y		+= arch/mips/kernel/ arch/mips/mm/ arch/mips/math-emu/

ifdef CONFIG_BAGET_MIPS

BAGETBOOT = $(MAKE) -C arch/$(ARCH)/baget

balo: vmlinux
	$(BAGETBOOT) balo

endif

ifdef CONFIG_LASAT
rom.bin rom.sw: vmlinux
	$(call descend,arch/mips/lasat/image,$@)
endif

makeboot =$(Q)$(MAKE) -f scripts/Makefile.build obj=arch/mips/boot $(1)

#
# SNI firmware is f*cked in interesting ways ...
#
ifdef CONFIG_SNI_RM200_PCI
all:	vmlinux.rm200
endif

vmlinux.ecoff vmlinux.rm200: vmlinux
	+@$(call makeboot,$@)

CLEAN_FILES += vmlinux.ecoff \
	       vmlinux.rm200.tmp \
	       vmlinux.rm200

archclean:
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/boot
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/baget
	@$(MAKE) -f scripts/Makefile.clean obj=arch/mips/lasat

archmrproper:

# Generate <asm/offset.h 
#
# The default rule is suffering from funny problems on MIPS so we using our
# own ...
#
# ---------------------------------------------------------------------------

define filechk_gen-asm-offset.h
	(set -e; \
	 echo "#ifndef __ASM_OFFSET_H"; \
	 echo "#define __ASM_OFFSET_H"; \
	 echo "/*"; \
	 echo " * DO NOT MODIFY."; \
	 echo " *"; \
	 echo " * This file was generated by arch/$(ARCH)/Makefile"; \
	 echo " *"; \
	 echo " */"; \
	 echo ""; \
	 sed -ne "/^@@@/s///p"; \
	 echo "#endif /* __ASM_OFFSET_H */" )
endef

prepare: include/asm-$(ARCH)/offset.h

arch/$(ARCH)/kernel/offset.s: include/asm include/linux/version.h \
				   include/config/MARKER

include/asm-$(ARCH)/offset.h: arch/$(ARCH)/kernel/offset.s
	$(call filechk,gen-asm-offset.h)

CLEAN_FILES += include/asm-$(ARCH)/offset.h.tmp \
	       include/asm-$(ARCH)/offset.h
