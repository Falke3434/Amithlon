# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras
# Simplified for ppc64 by Todd Inglett
#
# NOTE:	this code is built for 32 bit in ELF32 format even though
#	it packages a 64 bit kernel.  We do this to simplify the
#	bootloader and increase compatibility with OpenFirmware.
#
#	To this end we need to define BOOTCC, etc, as the tools
#	needed to build the 32 bit image.  These are normally HOSTCC,
#	but may be a third compiler if, for example, you are cross
#	compiling from an intel box.  Once the 64bit ppc gcc is
#	stable it will probably simply be a compiler switch to
#	compile for 32bit mode.
#	To make it easier to setup a cross compiler,
#	CROSS32_COMPILE is setup as a prefix just like CROSS_COMPILE
#	in the toplevel makefile.

CROSS32_COMPILE =
#CROSS32_COMPILE = /usr/local/ppc/bin/powerpc-linux-

BOOTCC		:= $(CROSS32_COMPILE)gcc
HOSTCC		:= gcc
BOOTCFLAGS	:= $(HOSTCFLAGS) -Iinclude -fno-builtin 
BOOTAS		:= $(CROSS32_COMPILE)as
BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -traditional
BOOTLD		:= $(CROSS32_COMPILE)ld
BOOTLFLAGS	:= -Ttext 0x00400000 -e _start -T $(obj)/zImage.lds
BOOTOBJCOPY	:= $(CROSS32_COMPILE)objcopy
OBJCOPYFLAGS    := contents,alloc,load,readonly,data

src-boot := crt0.S string.S prom.c main.c zlib.c imagesize.c
src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S
	$(call if_changed_dep,bootas)

#-----------------------------------------------------------
# ELF sections within the zImage bootloader/wrapper
#-----------------------------------------------------------
required := vmlinux .config System.map
initrd   := initrd

obj-sec = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.o, $(section)))
src-sec = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.c, $(section)))
gz-sec  = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.gz, $(section)))

host-progs		:= piggy addnote addSystemMap addRamDisk
targets 		+= zImage zImage.initrd imagesize.c \
			   $(patsubst $(obj)/%,%, $(call obj-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call src-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call gz-sec, $(required) $(initrd))) \
			   vmlinux.sm vmlinux.initrd vmlinux.sminitrd
extra-y			:= sysmap.o initrd.o

quiet_cmd_sysmap = SYSMAP   $@
      cmd_sysmap = $(obj)/addSystemMap System.map $< $@
$(obj)/vmlinux.sm: vmlinux $(obj)/addSystemMap System.map FORCE
	$(call if_changed,sysmap)

quiet_cmd_ramdisk = RAMDISK $@
      cmd_ramdisk = $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map $< $@
$(obj)/vmlinux.initrd: vmlinux $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map FORCE
	$(call if_changed,ramdisk)

$(obj)/vmlinux.sminitrd: $(obj)/vmlinux.sm $(obj)/addRamDisk $(obj)/ramdisk.image.gz FORCE
	$(call if_changed,ramdisk)

$(obj)/sysmap.o: System.map $(obj)/piggyback FORCE
	$(call if_changed,piggy)

addsection = $(BOOTOBJCOPY) $(1) \
		--add-section=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $(1)))=$(patsubst %.o,%.gz, $(1)) \
		--set-section-flags=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $(1)))=$(OBJCOPYFLAGS)

quiet_cmd_addnote = ADDNOTE $@ 
      cmd_addnote = $(BOOTLD) $(BOOTLFLAGS) -o $@ $(obj-boot) && $(obj)/addnote $@

quiet_cmd_piggy = PIGGY   $@
      cmd_piggy = $(obj)/piggyback $(@:.o=) < $< | $(BOOTAS) -o $@

$(call gz-sec, $(required)): $(obj)/kernel-%.gz: % FORCE
	$(call if_changed,gzip)

$(obj)/kernel-initrd.gz: $(obj)/ramdisk.image.gz
	cp -f $(obj)/ramdisk.image.gz $@

$(call src-sec, $(required) $(initrd)): $(obj)/kernel-%.c: $(obj)/kernel-%.gz FORCE
	touch $@

$(call obj-sec, $(required) $(initrd)): $(obj)/kernel-%.o: $(obj)/kernel-%.c FORCE
	$(call if_changed_dep,bootcc)
	$(call addsection, $@)

$(obj)/zImage: obj-boot += $(call obj-sec, $(required))
$(obj)/zImage: $(call obj-sec, $(required)) $(obj-boot) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/zImage.initrd: obj-boot += $(call obj-sec, $(required) $(initrd))
$(obj)/zImage.initrd: $(call obj-sec, $(required) $(initrd)) $(obj-boot) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/imagesize.c: vmlinux
	@echo Generating $@
	ls -l vmlinux | \
	awk '{printf "/* generated -- do not edit! */\n" \
		"unsigned long vmlinux_filesize = %d;\n", $$5}' > $(obj)/imagesize.c
	$(CROSS_COMPILE)nm -n vmlinux | tail -1 | \
	awk '{printf "unsigned long vmlinux_memsize = 0x%s;\n", substr($$1,8)}' \
		>> $(obj)/imagesize.c


clean-files := $(patsubst $(obj)/%,%, $(obj-boot))
