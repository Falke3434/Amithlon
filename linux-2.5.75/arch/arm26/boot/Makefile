#
# arch/arm/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2002 Russell King
#

# Note: the following conditions must always be true:
#   ZRELADDR == virt_to_phys(TEXTADDR)
#   PARAMS_PHYS must be with 4MB of ZRELADDR
#   INITRD_PHYS must be in RAM

   zreladdr-y		:= 0x02080000 
params_phys-y		:= 0x0207c000
initrd_phys-y		:= 0x02180000

ZRELADDR    := 0x02080000
ZTEXTADDR   := 0x0207c000
PARAMS_PHYS := $(params_phys-y)
INITRD_PHYS := 0x02180000

# We now have a PIC decompressor implementation.  Decompressors running
# from RAM should not define ZTEXTADDR.  Decompressors running directly
# from ROM or Flash must define ZTEXTADDR (preferably via the config)
# FIXME: Previous assignment to ztextaddr-y is lost here. See SHARK
ifeq ($(CONFIG_ZBOOT_ROM),y)
ZTEXTADDR	:= $(CONFIG_ZBOOT_ROM_TEXT)
ZBSSADDR	:= $(CONFIG_ZBOOT_ROM_BSS)
else
ZTEXTADDR	:= 0
ZBSSADDR	:= ALIGN(4)
endif

export	ZTEXTADDR ZBSSADDR ZRELADDR INITRD_PHYS PARAMS_PHYS

targets := Image zImage bootpImage

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/zImage:	$(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Kernel: $@ is ready'

$(obj)/compressed/vmlinux: vmlinux FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

.PHONY: initrd
initrd:
	@test "$(INITRD_PHYS)" != "" || \
	(echo This machine does not support INITRD; exit -1)
	@test "$(INITRD)" != "" || \
	(echo You must specify INITRD; exit -1)

install: $(obj)/Image
	$(CONFIG_SHELL) $(obj)/install.sh \
	$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall: $(obj)/zImage
	$(CONFIG_SHELL) $(obj)/install.sh \
	$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION) \
	$(obj)/zImage System.map "$(INSTALL_PATH)"

subdir-	    := compressed
