#
# Makefile for linux/drivers/scsi
#
# 30 May 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#
# 20 Sep 2000, Torben Mathiasen <tmm@image.dk>
# Changed link order to reflect new scsi initialization.
#
# *!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!
# The link order must be, SCSI Core, SCSI HBA drivers, and
# lastly SCSI peripheral drivers (disk/tape/cdrom/etc.) to
# satisfy certain initialization assumptions in the SCSI layer.
# *!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!*!


CFLAGS_aha152x.o =   -DAHA152X_STAT -DAUTOCONF
CFLAGS_gdth.o    = # -DDEBUG_GDTH=2 -D__SERIAL__ -D__COM2__ -DGDTH_STATISTICS
CFLAGS_seagate.o =   -DARBITRATE -DPARITY -DSEAGATE_USE_ASM

subdir-$(CONFIG_PCMCIA)		+= pcmcia

obj-$(CONFIG_SCSI)		+= scsi_mod.o

obj-$(CONFIG_A4000T_SCSI)	+= amiga7xx.o	53c7xx.o
obj-$(CONFIG_A4091_SCSI)	+= amiga7xx.o	53c7xx.o
obj-$(CONFIG_BLZ603EPLUS_SCSI)	+= amiga7xx.o	53c7xx.o
obj-$(CONFIG_WARPENGINE_SCSI)	+= amiga7xx.o	53c7xx.o
obj-$(CONFIG_A3000_SCSI)	+= a3000.o	wd33c93.o
obj-$(CONFIG_A2091_SCSI)	+= a2091.o	wd33c93.o
obj-$(CONFIG_GVP11_SCSI)	+= gvp11.o	wd33c93.o
obj-$(CONFIG_SCSI_PC980155)	+= pc980155.o	wd33c93.o
obj-$(CONFIG_MVME147_SCSI)	+= mvme147.o	wd33c93.o
obj-$(CONFIG_SGIWD93_SCSI)	+= sgiwd93.o	wd33c93.o
obj-$(CONFIG_CYBERSTORM_SCSI)	+= NCR53C9x.o	cyberstorm.o
obj-$(CONFIG_CYBERSTORMII_SCSI)	+= NCR53C9x.o	cyberstormII.o
obj-$(CONFIG_BLZ2060_SCSI)	+= NCR53C9x.o	blz2060.o
obj-$(CONFIG_BLZ1230_SCSI)	+= NCR53C9x.o	blz1230.o
obj-$(CONFIG_FASTLANE_SCSI)	+= NCR53C9x.o	fastlane.o
obj-$(CONFIG_OKTAGON_SCSI)	+= NCR53C9x.o	oktagon_esp.o	oktagon_io.o
obj-$(CONFIG_ATARI_SCSI)	+= atari_scsi.o
obj-$(CONFIG_MAC_SCSI)		+= mac_scsi.o
obj-$(CONFIG_SCSI_MAC_ESP)	+= mac_esp.o	NCR53C9x.o
obj-$(CONFIG_SUN3_SCSI)		+= sun3_scsi.o  sun3_scsi_vme.o
obj-$(CONFIG_MVME16x_SCSI)	+= mvme16x.o	53c7xx.o
obj-$(CONFIG_BVME6000_SCSI)	+= bvme6000.o	53c7xx.o
obj-$(CONFIG_SCSI_SIM710)	+= sim710.o	53c700.o
obj-$(CONFIG_SCSI_ADVANSYS)	+= advansys.o
obj-$(CONFIG_SCSI_PCI2000)	+= pci2000.o
obj-$(CONFIG_SCSI_PCI2220I)	+= pci2220i.o
obj-$(CONFIG_SCSI_PSI240I)	+= psi240i.o
obj-$(CONFIG_SCSI_BUSLOGIC)	+= BusLogic.o
obj-$(CONFIG_SCSI_DPT_I2O)	+= dpt_i2o.o
obj-$(CONFIG_SCSI_U14_34F)	+= u14-34f.o
obj-$(CONFIG_SCSI_ULTRASTOR)	+= ultrastor.o
obj-$(CONFIG_SCSI_AHA152X)	+= aha152x.o
obj-$(CONFIG_SCSI_AHA1542)	+= aha1542.o
obj-$(CONFIG_SCSI_AHA1740)	+= aha1740.o
obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AIC79XX)	+= aic7xxx/
obj-$(CONFIG_SCSI_AACRAID)	+= aacraid/
obj-$(CONFIG_SCSI_AIC7XXX_OLD)	+= aic7xxx_old.o
obj-$(CONFIG_SCSI_IPS)		+= ips.o
obj-$(CONFIG_SCSI_FD_MCS)	+= fd_mcs.o
obj-$(CONFIG_SCSI_FUTURE_DOMAIN)+= fdomain.o
obj-$(CONFIG_SCSI_IN2000)	+= in2000.o
obj-$(CONFIG_SCSI_GENERIC_NCR5380) += g_NCR5380.o
obj-$(CONFIG_SCSI_GENERIC_NCR5380_MMIO) += g_NCR5380_mmio.o
obj-$(CONFIG_SCSI_NCR53C406A)	+= NCR53c406a.o
obj-$(CONFIG_SCSI_NCR_D700)	+= NCR_D700.o 53c700.o
obj-$(CONFIG_SCSI_NCR_Q720)	+= NCR_Q720_mod.o
obj-$(CONFIG_SCSI_SYM53C416)	+= sym53c416.o
obj-$(CONFIG_SCSI_QLOGIC_FAS)	+= qlogicfas.o
obj-$(CONFIG_SCSI_QLOGIC_ISP)	+= qlogicisp.o 
obj-$(CONFIG_SCSI_QLOGIC_FC)	+= qlogicfc.o 
obj-$(CONFIG_SCSI_QLOGIC_1280)	+= qla1280.o 
obj-$(CONFIG_SCSI_PAS16)	+= pas16.o
obj-$(CONFIG_SCSI_SEAGATE)	+= seagate.o
obj-$(CONFIG_SCSI_FD_8xx)	+= seagate.o
obj-$(CONFIG_SCSI_T128)		+= t128.o
obj-$(CONFIG_SCSI_DMX3191D)	+= dmx3191d.o
obj-$(CONFIG_SCSI_DTC3280)	+= dtc.o
obj-$(CONFIG_SCSI_SYM53C8XX_2)	+= sym53c8xx_2/
obj-$(CONFIG_SCSI_SYM53C8XX)	+= sym53c8xx.o 
obj-$(CONFIG_SCSI_ZALON)	+= zalon7xx.o
obj-$(CONFIG_SCSI_EATA_PIO)	+= eata_pio.o
obj-$(CONFIG_SCSI_7000FASST)	+= wd7000.o
obj-$(CONFIG_SCSI_MCA_53C9X)	+= NCR53C9x.o	mca_53c9x.o
obj-$(CONFIG_SCSI_IBMMCA)	+= ibmmca.o
obj-$(CONFIG_SCSI_EATA)		+= eata.o
obj-$(CONFIG_SCSI_DC395x)	+= dc395x.o
obj-$(CONFIG_SCSI_DC390T)	+= tmscsim.o
obj-$(CONFIG_SCSI_AM53C974)	+= AM53C974.o
obj-$(CONFIG_SCSI_MEGARAID)	+= megaraid.o
obj-$(CONFIG_SCSI_ACARD)	+= atp870u.o
obj-$(CONFIG_SCSI_SUNESP)	+= esp.o
obj-$(CONFIG_SCSI_GDTH)		+= gdth.o
obj-$(CONFIG_SCSI_INITIO)	+= initio.o
obj-$(CONFIG_SCSI_INIA100)	+= a100u2w.o
obj-$(CONFIG_SCSI_QLOGICPTI)	+= qlogicpti.o
obj-$(CONFIG_BLK_DEV_IDESCSI)	+= ide-scsi.o
obj-$(CONFIG_SCSI_MESH)		+= mesh.o
obj-$(CONFIG_SCSI_MAC53C94)	+= mac53c94.o
obj-$(CONFIG_SCSI_PLUTO)	+= pluto.o
obj-$(CONFIG_SCSI_DECNCR)	+= NCR53C9x.o	dec_esp.o
obj-$(CONFIG_BLK_DEV_3W_XXXX_RAID) += 3w-xxxx.o
obj-$(CONFIG_SCSI_PPA)		+= ppa.o
obj-$(CONFIG_SCSI_IMM)		+= imm.o
obj-$(CONFIG_JAZZ_ESP)		+= NCR53C9x.o	jazz_esp.o
obj-$(CONFIG_SUN3X_ESP)		+= NCR53C9x.o	sun3x_esp.o
obj-$(CONFIG_SCSI_DEBUG)	+= scsi_debug.o
obj-$(CONFIG_SCSI_FCAL)		+= fcal.o
obj-$(CONFIG_SCSI_CPQFCTS)	+= cpqfc.o
obj-$(CONFIG_SCSI_LASI700)	+= lasi700.o 53c700.o
obj-$(CONFIG_SCSI_NSP32)	+= nsp32.o

obj-$(CONFIG_ARM)		+= arm/

obj-$(CONFIG_CHR_DEV_ST)	+= st.o
obj-$(CONFIG_CHR_DEV_OSST)	+= osst.o
obj-$(CONFIG_BLK_DEV_SD)	+= sd_mod.o
obj-$(CONFIG_BLK_DEV_SR)	+= sr_mod.o
obj-$(CONFIG_CHR_DEV_SG)	+= sg.o

scsi_mod-y			+= scsi.o hosts.o scsi_ioctl.o constants.o \
				   scsicam.o scsi_error.o scsi_lib.o \
				   scsi_scan.o scsi_syms.o scsi_sysfs.o \
				   scsi_devinfo.o
scsi_mod-$(CONFIG_PROC_FS)	+= scsi_proc.o
scsi_mod-$(CONFIG_X86_PC9800)	+= scsi_pc98.o
			
sd_mod-objs	:= sd.o
sr_mod-objs	:= sr.o sr_ioctl.o sr_vendor.o
initio-objs	:= ini9100u.o i91uscsi.o
a100u2w-objs	:= inia100.o i60uscsi.o
ncr53c8xx-flags-$(CONFIG_SCSI_ZALON) \
		:= -DCONFIG_NCR53C8XX_PREFETCH \
			-DCONFIG_SCSI_NCR53C8XX_NO_WORD_TRANSFERS \
			-DCONFIG_SCSI_NCR53C8XX_NO_NVRAM \
			-DSCSI_NCR_BIG_ENDIAN -DSIMULATED_INTFLY
ncr53c8xx-flags-$(CONFIG_SCSI_NCR_Q720) \
		:= -DCONFIG_SCSI_NCR53C8XX_NO_NVRAM \
			-DSIMULATED_INTFLY
CFLAGS_ncr53c8xx.o	:= $(ncr53c8xx-flags-y) $(ncr53c8xx-flags-m)
zalon7xx-objs	:= zalon.o ncr53c8xx.o
NCR_Q720_mod-objs	:= NCR_Q720.o ncr53c8xx.o
cpqfc-objs	:= cpqfcTSinit.o cpqfcTScontrol.o cpqfcTSi2c.o \
		   cpqfcTSworker.o cpqfcTStrigger.o

# Files generated that shall be removed upon make clean
clean-files :=	53c7xx_d.h 53c700_d.h	\
		53c7xx_u.h 53c700_u.h

$(obj)/53c7xx.o:   $(obj)/53c7xx_d.h $(obj)/53c7xx_u.h
$(obj)/53c700.o $(MODVERDIR)/$(obj)/53c700.ver: $(obj)/53c700_d.h

# If you want to play with the firmware, uncomment
# GENERATE_FIRMWARE := 1

ifdef GENERATE_FIRMWARE

$(obj)/53c7xx_d.h: $(src)/53c7xx.scr $(src)/script_asm.pl
	$(CPP) -traditional -DCHIP=710 - < $< | grep -v '^#' | $(PERL) -s $(src)/script_asm.pl -ncr7x0_family $@ $(@:_d.h=_u.h)

$(obj)/53c7xx_u.h: $(obj)/53c7xx_d.h

$(obj)/53c700_d.h: $(src)/53c700.scr $(src)/script_asm.pl
	$(PERL) -s $(src)/script_asm.pl -ncr7x0_family $@ $(@:_d.h=_u.h) < $<

endif
