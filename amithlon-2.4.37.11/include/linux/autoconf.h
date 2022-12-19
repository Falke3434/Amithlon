/*
 * Automatically generated C config: don't edit
 */
#define AUTOCONF_INCLUDED
#define CONFIG_X86 1
#undef  CONFIG_SBUS
#define CONFIG_UID16 1

/*
 * Code maturity level options
 */
#define CONFIG_EXPERIMENTAL 1

/*
 * Loadable module support
 */
#define CONFIG_MODULES 1
#undef  CONFIG_MODVERSIONS
#undef  CONFIG_KMOD

/*
 * Processor type and features
 */
#undef  CONFIG_M386
#undef  CONFIG_M486
#undef  CONFIG_M586
#undef  CONFIG_M586TSC
#undef  CONFIG_M586MMX
#undef  CONFIG_M686
#define CONFIG_MPENTIUMIII 1
#undef  CONFIG_MPENTIUM4
#undef  CONFIG_MK6
#undef  CONFIG_MK7
#undef  CONFIG_MK8
#undef  CONFIG_MELAN
#undef  CONFIG_MCRUSOE
#undef  CONFIG_MGEODE_LX
#undef  CONFIG_MWINCHIPC6
#undef  CONFIG_MWINCHIP2
#undef  CONFIG_MWINCHIP3D
#undef  CONFIG_MCYRIXIII
#undef  CONFIG_MVIAC3_2
#define CONFIG_X86_WP_WORKS_OK 1
#define CONFIG_X86_INVLPG 1
#define CONFIG_X86_CMPXCHG 1
#define CONFIG_X86_XADD 1
#define CONFIG_X86_BSWAP 1
#define CONFIG_X86_POPAD_OK 1
#undef  CONFIG_RWSEM_GENERIC_SPINLOCK
#define CONFIG_RWSEM_XCHGADD_ALGORITHM 1
#define CONFIG_X86_L1_CACHE_SHIFT (5)
#define CONFIG_X86_HAS_TSC 1
#define CONFIG_X86_GOOD_APIC 1
#define CONFIG_X86_PGE 1
#define CONFIG_X86_USE_PPRO_CHECKSUM 1
#define CONFIG_X86_F00F_WORKS_OK 1
#define CONFIG_X86_MCE 1
#define CONFIG_TOSHIBA 1
#define CONFIG_I8K 1
#undef  CONFIG_MICROCODE
#define CONFIG_X86_MSR 1
#define CONFIG_X86_CPUID 1
#undef  CONFIG_EDD
#define CONFIG_NOHIGHMEM 1
#undef  CONFIG_HIGHMEM4G
#undef  CONFIG_HIGHMEM64G
#undef  CONFIG_HIGHMEM
#undef  CONFIG_MATH_EMULATION
#define CONFIG_MTRR 1
#undef  CONFIG_SMP
#undef  CONFIG_X86_UP_APIC
#undef  CONFIG_X86_UP_IOAPIC
#undef  CONFIG_X86_TSC
#undef  CONFIG_X86_TSC_DISABLE
#define CONFIG_X86_TSC 1

/*
 * General setup
 */
#define CONFIG_NET 1
#define CONFIG_PCI 1
#undef  CONFIG_PCI_GOBIOS
#undef  CONFIG_PCI_GODIRECT
#define CONFIG_PCI_GOANY 1
#define CONFIG_PCI_BIOS 1
#define CONFIG_PCI_DIRECT 1
#define CONFIG_ISA 1
#undef  CONFIG_PCI_NAMES
#undef  CONFIG_EISA
#undef  CONFIG_MCA
#undef  CONFIG_HOTPLUG
#undef  CONFIG_PCMCIA
#undef  CONFIG_HOTPLUG_PCI
#define CONFIG_SYSVIPC 1
#undef  CONFIG_BSD_PROCESS_ACCT
#define CONFIG_SYSCTL 1
#define CONFIG_KCORE_ELF 1
#undef  CONFIG_KCORE_AOUT
#undef  CONFIG_BINFMT_AOUT
#define CONFIG_BINFMT_ELF 1
#undef  CONFIG_BINFMT_MISC
#undef  CONFIG_OOM_KILLER
#define CONFIG_PM 1
#define CONFIG_APM 1
#undef  CONFIG_APM_IGNORE_USER_SUSPEND
#undef  CONFIG_APM_DO_ENABLE
#undef  CONFIG_APM_CPU_IDLE
#undef  CONFIG_APM_DISPLAY_BLANK
#undef  CONFIG_APM_RTC_IS_GMT
#undef  CONFIG_APM_ALLOW_INTS
#undef  CONFIG_APM_REAL_MODE_POWER_OFF

/*
 * ACPI Support
 */
#undef  CONFIG_ACPI

/*
 * Memory Technology Devices (MTD)
 */
#undef  CONFIG_MTD

/*
 * Parallel port support
 */
#undef  CONFIG_PARPORT

/*
 * Plug and Play configuration
 */
#define CONFIG_PNP 1
#define CONFIG_ISAPNP 1

/*
 * Block devices
 */
#define CONFIG_BLK_DEV_FD 1
#undef  CONFIG_BLK_DEV_XD
#undef  CONFIG_PARIDE
#undef  CONFIG_BLK_CPQ_DA
#undef  CONFIG_BLK_CPQ_CISS_DA
#undef  CONFIG_CISS_SCSI_TAPE
#undef  CONFIG_CISS_MONITOR_THREAD
#undef  CONFIG_BLK_DEV_DAC960
#undef  CONFIG_BLK_DEV_UMEM
#undef  CONFIG_BLK_DEV_SX8
#undef  CONFIG_BLK_DEV_LOOP
#undef  CONFIG_BLK_DEV_NBD
#define CONFIG_BLK_DEV_RAM 1
#define CONFIG_BLK_DEV_RAM_SIZE (4096)
#define CONFIG_BLK_DEV_INITRD 1
#undef  CONFIG_BLK_STATS

/*
 * Multi-device support (RAID and LVM)
 */
#undef  CONFIG_MD
#undef  CONFIG_BLK_DEV_MD
#undef  CONFIG_MD_LINEAR
#undef  CONFIG_MD_RAID0
#undef  CONFIG_MD_RAID1
#undef  CONFIG_MD_RAID5
#undef  CONFIG_MD_MULTIPATH
#undef  CONFIG_BLK_DEV_LVM

/*
 * Networking options
 */
#define CONFIG_PACKET 1
#define CONFIG_PACKET_MMAP 1
#undef  CONFIG_NETLINK_DEV
#undef  CONFIG_NETFILTER
#undef  CONFIG_FILTER
#undef  CONFIG_UNIX
#undef  CONFIG_INET
#undef  CONFIG_ATM
#undef  CONFIG_VLAN_8021Q

/*
 *  
 */
#undef  CONFIG_IPX
#undef  CONFIG_ATALK
#undef  CONFIG_DECNET
#undef  CONFIG_BRIDGE
#undef  CONFIG_X25
#undef  CONFIG_LAPB
#undef  CONFIG_LLC
#undef  CONFIG_NET_DIVERT
#undef  CONFIG_WAN_ROUTER
#undef  CONFIG_NET_FASTROUTE
#undef  CONFIG_NET_HW_FLOWCONTROL

/*
 * QoS and/or fair queueing
 */
#undef  CONFIG_NET_SCHED

/*
 * Network testing
 */
#undef  CONFIG_NET_PKTGEN

/*
 * Telephony Support
 */
#undef  CONFIG_PHONE
#undef  CONFIG_PHONE_IXJ
#undef  CONFIG_PHONE_IXJ_PCMCIA

/*
 * ATA/IDE/MFM/RLL support
 */
#define CONFIG_IDE 1

/*
 * IDE, ATA and ATAPI Block devices
 */
#define CONFIG_BLK_DEV_IDE 1

/*
 * Please see Documentation/ide.txt for help/info on IDE drives
 */
#undef  CONFIG_BLK_DEV_HD_IDE
#undef  CONFIG_BLK_DEV_HD
#undef  CONFIG_BLK_DEV_IDE_SATA
#define CONFIG_BLK_DEV_IDEDISK 1
#define CONFIG_IDEDISK_MULTI_MODE 1
#undef  CONFIG_IDEDISK_STROKE
#undef  CONFIG_BLK_DEV_IDECS
#undef  CONFIG_BLK_DEV_DELKIN
#undef  CONFIG_BLK_DEV_IDECD
#undef  CONFIG_BLK_DEV_IDETAPE
#undef  CONFIG_BLK_DEV_IDEFLOPPY
#define CONFIG_BLK_DEV_IDESCSI 1
#undef  CONFIG_IDE_TASK_IOCTL

/*
 * IDE chipset support/bugfixes
 */
#define CONFIG_BLK_DEV_CMD640 1
#undef  CONFIG_BLK_DEV_CMD640_ENHANCED
#undef  CONFIG_BLK_DEV_ISAPNP
#define CONFIG_BLK_DEV_IDEPCI 1
#define CONFIG_BLK_DEV_GENERIC 1
#define CONFIG_IDEPCI_SHARE_IRQ 1
#define CONFIG_BLK_DEV_IDEDMA_PCI 1
#undef  CONFIG_BLK_DEV_OFFBOARD
#undef  CONFIG_BLK_DEV_IDEDMA_FORCED
#define CONFIG_IDEDMA_PCI_AUTO 1
#undef  CONFIG_IDEDMA_ONLYDISK
#define CONFIG_BLK_DEV_IDEDMA 1
#undef  CONFIG_IDEDMA_PCI_WIP
#define CONFIG_BLK_DEV_ADMA100 1
#define CONFIG_BLK_DEV_AEC62XX 1
#define CONFIG_BLK_DEV_ALI15X3 1
#undef  CONFIG_WDC_ALI15X3
#define CONFIG_BLK_DEV_AMD74XX 1
#undef  CONFIG_AMD74XX_OVERRIDE
#define CONFIG_BLK_DEV_ATIIXP 1
#define CONFIG_BLK_DEV_CMD64X 1
#define CONFIG_BLK_DEV_TRIFLEX 1
#define CONFIG_BLK_DEV_CY82C693 1
#define CONFIG_BLK_DEV_CS5530 1
#define CONFIG_BLK_DEV_HPT34X 1
#undef  CONFIG_HPT34X_AUTODMA
#define CONFIG_BLK_DEV_HPT366 1
#define CONFIG_BLK_DEV_PIIX 1
#define CONFIG_BLK_DEV_NS87415 1
#define CONFIG_BLK_DEV_OPTI621 1
#define CONFIG_BLK_DEV_PDC202XX_OLD 1
#undef  CONFIG_PDC202XX_BURST
#define CONFIG_BLK_DEV_PDC202XX_NEW 1
#undef  CONFIG_PDC202XX_FORCE
#define CONFIG_BLK_DEV_RZ1000 1
#define CONFIG_BLK_DEV_SC1200 1
#define CONFIG_BLK_DEV_SVWKS 1
#define CONFIG_BLK_DEV_SIIMAGE 1
#define CONFIG_BLK_DEV_SIS5513 1
#define CONFIG_BLK_DEV_SLC90E66 1
#define CONFIG_BLK_DEV_TRM290 1
#define CONFIG_BLK_DEV_VIA82CXXX 1
#define CONFIG_IDE_CHIPSETS 1

/*
 * Note: most of these also require special kernel boot parameters
 */
#define CONFIG_BLK_DEV_4DRIVES 1
#define CONFIG_BLK_DEV_ALI14XX 1
#define CONFIG_BLK_DEV_DTC2278 1
#define CONFIG_BLK_DEV_HT6560B 1
#define CONFIG_BLK_DEV_PDC4030 1
#define CONFIG_BLK_DEV_QD65XX 1
#define CONFIG_BLK_DEV_UMC8672 1
#define CONFIG_IDEDMA_AUTO 1
#undef  CONFIG_IDEDMA_IVB
#undef  CONFIG_DMA_NONPCI
#define CONFIG_BLK_DEV_PDC202XX 1
#undef  CONFIG_BLK_DEV_ATARAID
#undef  CONFIG_BLK_DEV_ATARAID_PDC
#undef  CONFIG_BLK_DEV_ATARAID_HPT
#undef  CONFIG_BLK_DEV_ATARAID_MEDLEY
#undef  CONFIG_BLK_DEV_ATARAID_SII

/*
 * SCSI support
 */
#define CONFIG_SCSI 1

/*
 * SCSI support type (disk, tape, CD-ROM)
 */
#define CONFIG_BLK_DEV_SD 1
#define CONFIG_SD_EXTRA_DEVS (40)
#undef  CONFIG_CHR_DEV_ST
#undef  CONFIG_CHR_DEV_OSST
#undef  CONFIG_BLK_DEV_SR
#define CONFIG_CHR_DEV_SG 1

/*
 * Some SCSI devices (e.g. CD jukebox) support multiple LUNs
 */
#undef  CONFIG_SCSI_DEBUG_QUEUES
#define CONFIG_SCSI_MULTI_LUN 1
#undef  CONFIG_SCSI_CONSTANTS
#undef  CONFIG_SCSI_LOGGING

/*
 * SCSI low-level drivers
 */
#undef  CONFIG_BLK_DEV_3W_XXXX_RAID
#undef  CONFIG_SCSI_7000FASST
#undef  CONFIG_SCSI_ACARD
#undef  CONFIG_SCSI_AHA152X
#undef  CONFIG_SCSI_AHA1542
#undef  CONFIG_SCSI_AHA1740
#undef  CONFIG_SCSI_AACRAID
#undef  CONFIG_SCSI_AIC7XXX
#undef  CONFIG_SCSI_AIC79XX
#undef  CONFIG_SCSI_AIC7XXX_OLD
#undef  CONFIG_SCSI_DPT_I2O
#undef  CONFIG_SCSI_ADVANSYS
#undef  CONFIG_SCSI_IN2000
#undef  CONFIG_SCSI_AM53C974
#undef  CONFIG_SCSI_MEGARAID
#undef  CONFIG_SCSI_MEGARAID2
#define CONFIG_SCSI_SATA 1
#define CONFIG_SCSI_SATA_AHCI 1
#define CONFIG_SCSI_SATA_SVW 1
#define CONFIG_SCSI_ATA_PIIX 1
#define CONFIG_SCSI_SATA_NV 1
#define CONFIG_SCSI_SATA_QSTOR 1
#define CONFIG_SCSI_SATA_PROMISE 1
#define CONFIG_SCSI_SATA_SX4 1
#define CONFIG_SCSI_SATA_SIL 1
#define CONFIG_SCSI_SATA_SIS 1
#define CONFIG_SCSI_SATA_ULI 1
#define CONFIG_SCSI_SATA_VIA 1
#define CONFIG_SCSI_SATA_VITESSE 1
#undef  CONFIG_SCSI_BUSLOGIC
#undef  CONFIG_SCSI_CPQFCTS
#undef  CONFIG_SCSI_DMX3191D
#undef  CONFIG_SCSI_DTC3280
#undef  CONFIG_SCSI_EATA
#undef  CONFIG_SCSI_EATA_DMA
#undef  CONFIG_SCSI_EATA_PIO
#undef  CONFIG_SCSI_FUTURE_DOMAIN
#undef  CONFIG_SCSI_GDTH
#undef  CONFIG_SCSI_GENERIC_NCR5380
#undef  CONFIG_SCSI_IPS
#undef  CONFIG_SCSI_INITIO
#undef  CONFIG_SCSI_INIA100
#undef  CONFIG_SCSI_NCR53C406A
#undef  CONFIG_SCSI_NCR53C7xx
#undef  CONFIG_SCSI_SYM53C8XX_2
#undef  CONFIG_SCSI_NCR53C8XX
#undef  CONFIG_SCSI_SYM53C8XX
#undef  CONFIG_SCSI_PAS16
#undef  CONFIG_SCSI_PCI2000
#undef  CONFIG_SCSI_PCI2220I
#undef  CONFIG_SCSI_PSI240I
#undef  CONFIG_SCSI_QLOGIC_FAS
#undef  CONFIG_SCSI_QLOGIC_ISP
#undef  CONFIG_SCSI_QLOGIC_FC
#undef  CONFIG_SCSI_QLOGIC_1280
#undef  CONFIG_SCSI_SEAGATE
#undef  CONFIG_SCSI_SIM710
#undef  CONFIG_SCSI_SYM53C416
#undef  CONFIG_SCSI_DC390T
#undef  CONFIG_SCSI_T128
#undef  CONFIG_SCSI_U14_34F
#undef  CONFIG_SCSI_ULTRASTOR
#undef  CONFIG_SCSI_NSP32
#undef  CONFIG_SCSI_DEBUG

/*
 * Fusion MPT device support
 */
#undef  CONFIG_FUSION
#undef  CONFIG_FUSION_BOOT
#undef  CONFIG_FUSION_ISENSE
#undef  CONFIG_FUSION_CTL
#undef  CONFIG_FUSION_LAN

/*
 * IEEE 1394 (FireWire) support (EXPERIMENTAL)
 */
#undef  CONFIG_IEEE1394

/*
 * I2O device support
 */
#undef  CONFIG_I2O
#undef  CONFIG_I2O_PCI
#undef  CONFIG_I2O_BLOCK
#undef  CONFIG_I2O_LAN
#undef  CONFIG_I2O_SCSI
#undef  CONFIG_I2O_PROC

/*
 * Network device support
 */
#define CONFIG_NETDEVICES 1

/*
 * ARCnet devices
 */
#undef  CONFIG_ARCNET
#undef  CONFIG_DUMMY
#define CONFIG_DUMMY_MODULE 1
#undef  CONFIG_BONDING
#undef  CONFIG_EQUALIZER
#undef  CONFIG_TUN
#undef  CONFIG_ETHERTAP
#undef  CONFIG_NET_SB1000

/*
 * Ethernet (10 or 100Mbit)
 */
#define CONFIG_NET_ETHERNET 1
#undef  CONFIG_SUNLANCE
#undef  CONFIG_HAPPYMEAL
#define CONFIG_HAPPYMEAL_MODULE 1
#undef  CONFIG_SUNBMAC
#undef  CONFIG_SUNQE
#undef  CONFIG_SUNGEM
#define CONFIG_SUNGEM_MODULE 1
#define CONFIG_NET_VENDOR_3COM 1
#undef  CONFIG_EL1
#define CONFIG_EL1_MODULE 1
#undef  CONFIG_EL2
#define CONFIG_EL2_MODULE 1
#undef  CONFIG_ELPLUS
#define CONFIG_ELPLUS_MODULE 1
#undef  CONFIG_EL16
#define CONFIG_EL16_MODULE 1
#undef  CONFIG_EL3
#define CONFIG_EL3_MODULE 1
#undef  CONFIG_3C515
#define CONFIG_3C515_MODULE 1
#undef  CONFIG_ELMC
#undef  CONFIG_ELMC_II
#undef  CONFIG_VORTEX
#define CONFIG_VORTEX_MODULE 1
#undef  CONFIG_TYPHOON
#define CONFIG_TYPHOON_MODULE 1
#undef  CONFIG_LANCE
#define CONFIG_LANCE_MODULE 1
#define CONFIG_NET_VENDOR_SMC 1
#undef  CONFIG_WD80x3
#define CONFIG_WD80x3_MODULE 1
#undef  CONFIG_ULTRAMCA
#undef  CONFIG_ULTRA
#define CONFIG_ULTRA_MODULE 1
#undef  CONFIG_ULTRA32
#undef  CONFIG_SMC9194
#define CONFIG_SMC9194_MODULE 1
#define CONFIG_NET_VENDOR_RACAL 1
#undef  CONFIG_NI5010
#define CONFIG_NI5010_MODULE 1
#undef  CONFIG_NI52
#define CONFIG_NI52_MODULE 1
#undef  CONFIG_NI65
#define CONFIG_NI65_MODULE 1
#undef  CONFIG_AT1700
#define CONFIG_AT1700_MODULE 1
#undef  CONFIG_DEPCA
#define CONFIG_DEPCA_MODULE 1
#undef  CONFIG_HP100
#define CONFIG_HP100_MODULE 1
#undef  CONFIG_NET_ISA
#define CONFIG_NET_PCI 1
#undef  CONFIG_PCNET32
#define CONFIG_PCNET32_MODULE 1
#undef  CONFIG_AMD8111_ETH
#define CONFIG_AMD8111_ETH_MODULE 1
#undef  CONFIG_ADAPTEC_STARFIRE
#define CONFIG_ADAPTEC_STARFIRE_MODULE 1
#undef  CONFIG_AC3200
#define CONFIG_AC3200_MODULE 1
#undef  CONFIG_APRICOT
#define CONFIG_APRICOT_MODULE 1
#undef  CONFIG_B44
#define CONFIG_B44_MODULE 1
#undef  CONFIG_CS89x0
#define CONFIG_CS89x0_MODULE 1
#undef  CONFIG_TULIP
#define CONFIG_TULIP_MODULE 1
#define CONFIG_TULIP_MWI 1
#define CONFIG_TULIP_MMIO 1
#undef  CONFIG_DE4X5
#define CONFIG_DE4X5_MODULE 1
#undef  CONFIG_DGRS
#define CONFIG_DGRS_MODULE 1
#undef  CONFIG_DM9102
#define CONFIG_DM9102_MODULE 1
#undef  CONFIG_EEPRO100
#define CONFIG_EEPRO100_MODULE 1
#undef  CONFIG_EEPRO100_PIO
#undef  CONFIG_E100
#define CONFIG_E100_MODULE 1
#undef  CONFIG_LNE390
#undef  CONFIG_FEALNX
#define CONFIG_FEALNX_MODULE 1
#undef  CONFIG_NATSEMI
#define CONFIG_NATSEMI_MODULE 1
#undef  CONFIG_NE2K_PCI
#define CONFIG_NE2K_PCI_MODULE 1
#define CONFIG_FORCEDETH 1
#undef  CONFIG_NE3210
#undef  CONFIG_ES3210
#undef  CONFIG_8139CP
#define CONFIG_8139TOO 1
#undef  CONFIG_8139TOO_PIO
#undef  CONFIG_8139TOO_TUNE_TWISTER
#define CONFIG_8139TOO_8129 1
#undef  CONFIG_8139_OLD_RX_RESET
#undef  CONFIG_SIS900
#define CONFIG_SIS900_MODULE 1
#undef  CONFIG_EPIC100
#define CONFIG_EPIC100_MODULE 1
#undef  CONFIG_SUNDANCE
#define CONFIG_SUNDANCE_MODULE 1
#undef  CONFIG_SUNDANCE_MMIO
#undef  CONFIG_TLAN
#define CONFIG_TLAN_MODULE 1
#undef  CONFIG_VIA_RHINE
#define CONFIG_VIA_RHINE_MODULE 1
#undef  CONFIG_VIA_RHINE_MMIO
#undef  CONFIG_WINBOND_840
#define CONFIG_WINBOND_840_MODULE 1
#define CONFIG_NET_POCKET 1
#undef  CONFIG_ATP
#define CONFIG_ATP_MODULE 1
#undef  CONFIG_DE600
#define CONFIG_DE600_MODULE 1
#undef  CONFIG_DE620
#define CONFIG_DE620_MODULE 1

/*
 * Ethernet (1000 Mbit)
 */
#undef  CONFIG_ACENIC
#define CONFIG_ACENIC_MODULE 1
#undef  CONFIG_ACENIC_OMIT_TIGON_I
#undef  CONFIG_DL2K
#define CONFIG_DL2K_MODULE 1
#undef  CONFIG_E1000
#define CONFIG_E1000_MODULE 1
#undef  CONFIG_E1000_NAPI
#undef  CONFIG_MYRI_SBUS
#undef  CONFIG_NS83820
#define CONFIG_NS83820_MODULE 1
#undef  CONFIG_HAMACHI
#define CONFIG_HAMACHI_MODULE 1
#undef  CONFIG_YELLOWFIN
#define CONFIG_YELLOWFIN_MODULE 1
#define CONFIG_R8169 1
#undef  CONFIG_SKGE
#define CONFIG_SKGE_MODULE 1
#undef  CONFIG_SKY2
#define CONFIG_SKY2_MODULE 1
#undef  CONFIG_SK98LIN
#define CONFIG_SK98LIN_MODULE 1
#undef  CONFIG_TIGON3
#define CONFIG_TIGON3_MODULE 1
#undef  CONFIG_FDDI
#undef  CONFIG_PLIP
#undef  CONFIG_PPP
#undef  CONFIG_SLIP

/*
 * Wireless LAN (non-hamradio)
 */
#undef  CONFIG_NET_RADIO

/*
 * Token Ring devices
 */
#undef  CONFIG_TR
#undef  CONFIG_NET_FC
#undef  CONFIG_RCPCI
#undef  CONFIG_SHAPER

/*
 * Wan interfaces
 */
#undef  CONFIG_WAN

/*
 * Amateur Radio support
 */
#undef  CONFIG_HAMRADIO

/*
 * IrDA (infrared) support
 */
#undef  CONFIG_IRDA

/*
 * ISDN subsystem
 */
#undef  CONFIG_ISDN

/*
 * Old CD-ROM drivers (not SCSI, not IDE)
 */
#undef  CONFIG_CD_NO_IDESCSI

/*
 * Input core support
 */
#undef  CONFIG_INPUT
#undef  CONFIG_INPUT_KEYBDEV
#undef  CONFIG_DUMMY_KEYB
#undef  CONFIG_INPUT_MOUSEDEV
#undef  CONFIG_INPUT_JOYDEV
#undef  CONFIG_INPUT_EVDEV
#undef  CONFIG_INPUT_UINPUT

/*
 * Character devices
 */
#define CONFIG_VT 1
#define CONFIG_VT_CONSOLE 1
#define CONFIG_SERIAL 1
#undef  CONFIG_SERIAL_CONSOLE
#define CONFIG_SERIAL_EXTENDED 1
#define CONFIG_SERIAL_MANY_PORTS 1
#define CONFIG_SERIAL_SHARE_IRQ 1
#define CONFIG_SERIAL_DETECT_IRQ 1
#define CONFIG_SERIAL_MULTIPORT 1
#define CONFIG_HUB6 1
#define CONFIG_SERIAL_NONSTANDARD 1
#undef  CONFIG_COMPUTONE
#define CONFIG_COMPUTONE_MODULE 1
#undef  CONFIG_ROCKETPORT
#define CONFIG_ROCKETPORT_MODULE 1
#undef  CONFIG_CYCLADES
#define CONFIG_CYCLADES_MODULE 1
#define CONFIG_CYZ_INTR 1
#undef  CONFIG_DIGIEPCA
#define CONFIG_DIGIEPCA_MODULE 1
#undef  CONFIG_ESPSERIAL
#define CONFIG_ESPSERIAL_MODULE 1
#undef  CONFIG_MOXA_INTELLIO
#define CONFIG_MOXA_INTELLIO_MODULE 1
#undef  CONFIG_MOXA_SMARTIO
#define CONFIG_MOXA_SMARTIO_MODULE 1
#undef  CONFIG_ISI
#define CONFIG_ISI_MODULE 1
#undef  CONFIG_SYNCLINK
#define CONFIG_SYNCLINK_MODULE 1
#undef  CONFIG_SYNCLINKMP
#define CONFIG_SYNCLINKMP_MODULE 1
#undef  CONFIG_N_HDLC
#define CONFIG_N_HDLC_MODULE 1
#undef  CONFIG_RISCOM8
#define CONFIG_RISCOM8_MODULE 1
#undef  CONFIG_SPECIALIX
#define CONFIG_SPECIALIX_MODULE 1
#define CONFIG_SPECIALIX_RTSCTS 1
#undef  CONFIG_SPECIALIX_BROKEN
#undef  CONFIG_SX
#undef  CONFIG_RIO
#define CONFIG_STALDRV 1
#undef  CONFIG_STALLION
#define CONFIG_STALLION_MODULE 1
#undef  CONFIG_ISTALLION
#define CONFIG_ISTALLION_MODULE 1
#undef  CONFIG_UNIX98_PTYS

/*
 * I2C support
 */
#undef  CONFIG_I2C

/*
 * Mice
 */
#undef  CONFIG_BUSMOUSE
#undef  CONFIG_MOUSE

/*
 * Joysticks
 */
#undef  CONFIG_INPUT_GAMEPORT

/*
 * Input core support is needed for gameports
 */

/*
 * Input core support is needed for joysticks
 */
#undef  CONFIG_QIC02_TAPE
#undef  CONFIG_IPMI_HANDLER
#undef  CONFIG_IPMI_PANIC_EVENT
#undef  CONFIG_IPMI_DEVICE_INTERFACE
#undef  CONFIG_IPMI_KCS
#undef  CONFIG_IPMI_WATCHDOG

/*
 * Watchdog Cards
 */
#undef  CONFIG_WATCHDOG
#undef  CONFIG_SCx200
#undef  CONFIG_SCx200_GPIO
#undef  CONFIG_AMD_RNG
#undef  CONFIG_INTEL_RNG
#undef  CONFIG_HW_RANDOM
#undef  CONFIG_GEODE_RNG
#undef  CONFIG_AMD_PM768
#undef  CONFIG_NVRAM
#define CONFIG_RTC 1
#undef  CONFIG_MKBD
#undef  CONFIG_DTLK
#undef  CONFIG_R3964
#undef  CONFIG_APPLICOM
#undef  CONFIG_SONYPI

/*
 * Ftape, the floppy tape device driver
 */
#undef  CONFIG_FTAPE
#undef  CONFIG_AGP

/*
 * Direct Rendering Manager (XFree86 DRI support)
 */
#undef  CONFIG_DRM
#undef  CONFIG_MWAVE
#undef  CONFIG_OBMOUSE

/*
 * Multimedia devices
 */
#undef  CONFIG_VIDEO_DEV

/*
 * File systems
 */
#undef  CONFIG_QUOTA
#undef  CONFIG_QFMT_V2
#undef  CONFIG_AUTOFS_FS
#undef  CONFIG_AUTOFS4_FS
#undef  CONFIG_REISERFS_FS
#undef  CONFIG_REISERFS_CHECK
#undef  CONFIG_REISERFS_PROC_INFO
#undef  CONFIG_ADFS_FS
#undef  CONFIG_ADFS_FS_RW
#undef  CONFIG_AFFS_FS
#undef  CONFIG_HFS_FS
#undef  CONFIG_HFSPLUS_FS
#undef  CONFIG_BEFS_FS
#undef  CONFIG_BEFS_DEBUG
#undef  CONFIG_BFS_FS
#undef  CONFIG_EXT3_FS
#undef  CONFIG_JBD
#undef  CONFIG_JBD_DEBUG
#undef  CONFIG_FAT_FS
#undef  CONFIG_MSDOS_FS
#undef  CONFIG_UMSDOS_FS
#undef  CONFIG_VFAT_FS
#undef  CONFIG_EFS_FS
#undef  CONFIG_JFFS_FS
#undef  CONFIG_JFFS2_FS
#undef  CONFIG_CRAMFS
#undef  CONFIG_TMPFS
#define CONFIG_RAMFS 1
#undef  CONFIG_ISO9660_FS
#undef  CONFIG_JOLIET
#undef  CONFIG_ZISOFS
#undef  CONFIG_JFS_FS
#undef  CONFIG_JFS_DEBUG
#undef  CONFIG_JFS_STATISTICS
#undef  CONFIG_MINIX_FS
#undef  CONFIG_VXFS_FS
#undef  CONFIG_NTFS_FS
#undef  CONFIG_NTFS_RW
#undef  CONFIG_HPFS_FS
#define CONFIG_PROC_FS 1
#undef  CONFIG_DEVFS_FS
#undef  CONFIG_DEVFS_MOUNT
#undef  CONFIG_DEVFS_DEBUG
#undef  CONFIG_DEVPTS_FS
#undef  CONFIG_QNX4FS_FS
#undef  CONFIG_QNX4FS_RW
#undef  CONFIG_ROMFS_FS
#define CONFIG_EXT2_FS 1
#undef  CONFIG_SYSV_FS
#undef  CONFIG_UDF_FS
#undef  CONFIG_UDF_RW
#undef  CONFIG_UFS_FS
#undef  CONFIG_UFS_FS_WRITE
#undef  CONFIG_XFS_FS
#undef  CONFIG_XFS_QUOTA
#undef  CONFIG_XFS_RT
#undef  CONFIG_XFS_TRACE
#undef  CONFIG_XFS_DEBUG

/*
 * Network File Systems
 */
#undef  CONFIG_CODA_FS
#undef  CONFIG_INTERMEZZO_FS
#undef  CONFIG_NFS_FS
#undef  CONFIG_NFS_V3
#undef  CONFIG_NFS_DIRECTIO
#undef  CONFIG_ROOT_NFS
#undef  CONFIG_NFSD
#undef  CONFIG_NFSD_V3
#undef  CONFIG_NFSD_TCP
#undef  CONFIG_SUNRPC
#undef  CONFIG_LOCKD
#undef  CONFIG_SMB_FS
#undef  CONFIG_NCPFS_NLS
#undef  CONFIG_ZISOFS_FS
#undef  CONFIG_ZLIB_FS_INFLATE

/*
 * Partition Types
 */
#undef  CONFIG_PARTITION_ADVANCED
#define CONFIG_MSDOS_PARTITION 1
#undef  CONFIG_SMB_NLS
#undef  CONFIG_NLS

/*
 * Console drivers
 */
#define CONFIG_VGA_CONSOLE 1
#define CONFIG_VIDEO_SELECT 1
#undef  CONFIG_MDA_CONSOLE

/*
 * Frame-buffer support
 */
#define CONFIG_FB 1
#define CONFIG_DUMMY_CONSOLE 1
#define CONFIG_FB_RIVA 1
#define CONFIG_FB_CLGEN 1
#undef  CONFIG_FB_PM2
#undef  CONFIG_FB_PM3
#undef  CONFIG_FB_CYBER2000
#define CONFIG_FB_VESA 1
#undef  CONFIG_FB_VGA16
#undef  CONFIG_FB_HGA
#define CONFIG_VIDEO_SELECT 1
#define CONFIG_FB_MATROX 1
#define CONFIG_FB_MATROX_MILLENIUM 1
#define CONFIG_FB_MATROX_MYSTIQUE 1
#define CONFIG_FB_MATROX_G450 1
#define CONFIG_FB_MATROX_G100 1
#undef  CONFIG_FB_MATROX_PROC
#define CONFIG_FB_MATROX_MULTIHEAD 1
#define CONFIG_FB_ATY 1
#define CONFIG_FB_ATY_GX 1
#define CONFIG_FB_ATY_CT 1
#define CONFIG_FB_ATY_GENERIC_LCD 1
#define CONFIG_FB_RADEON 1
#define CONFIG_FB_ATY128 1
#define CONFIG_FB_INTEL 1
#undef  CONFIG_FB_SIS
#undef  CONFIG_FB_NEOMAGIC
#undef  CONFIG_FB_3DFX
#undef  CONFIG_FB_VOODOO1
#define CONFIG_FB_TRIDENT 1
#undef  CONFIG_FB_IT8181
#undef  CONFIG_FB_VIRTUAL
#define CONFIG_FBCON_ADVANCED 1
#undef  CONFIG_FBCON_MFB
#undef  CONFIG_FBCON_CFB2
#undef  CONFIG_FBCON_CFB4
#define CONFIG_FBCON_CFB8 1
#define CONFIG_FBCON_CFB16 1
#undef  CONFIG_FBCON_CFB24
#define CONFIG_FBCON_CFB32 1
#undef  CONFIG_FBCON_AFB
#undef  CONFIG_FBCON_ILBM
#undef  CONFIG_FBCON_IPLAN2P2
#undef  CONFIG_FBCON_IPLAN2P4
#undef  CONFIG_FBCON_IPLAN2P8
#undef  CONFIG_FBCON_MAC
#undef  CONFIG_FBCON_VGA_PLANES
#undef  CONFIG_FBCON_VGA
#undef  CONFIG_FBCON_HGA
#define CONFIG_FBCON_FONTWIDTH8_ONLY 1
#define CONFIG_FBCON_FONTS 1
#define CONFIG_FONT_8x8 1
#define CONFIG_FONT_8x16 1
#undef  CONFIG_FONT_SUN8x16
#define CONFIG_FONT_PEARL_8x8 1
#undef  CONFIG_FONT_ACORN_8x8

/*
 * Sound
 */
#define CONFIG_SOUND 1
#undef  CONFIG_SOUND_ALI5455
#define CONFIG_SOUND_ALI5455_MODULE 1
#define CONFIG_SOUND_ALI5455_CODECSPDIFOUT_PCMOUTSHARE 1
#define CONFIG_SOUND_ALI5455_CODECSPDIFOUT_CODECINDEPENDENTDMA 1
#define CONFIG_SOUND_ALI5455_CONTROLLERSPDIFOUT_PCMOUTSHARE 1
#define CONFIG_SOUND_ALI5455_CONTROLLERSPDIFOUT_CONTROLLERINDEPENDENTDMA 1
#undef  CONFIG_SOUND_BT878
#define CONFIG_SOUND_BT878_MODULE 1
#undef  CONFIG_SOUND_CMPCI
#define CONFIG_SOUND_CMPCI_MODULE 1
#define CONFIG_SOUND_CMPCI_FM 1
#define CONFIG_SOUND_CMPCI_FMIO 0x388
#define CONFIG_SOUND_CMPCI_FMIO 0x388
#define CONFIG_SOUND_CMPCI_MIDI 1
#define CONFIG_SOUND_CMPCI_MPUIO 0x330
#define CONFIG_SOUND_CMPCI_JOYSTICK 1
#define CONFIG_SOUND_CMPCI_CM8738 1
#define CONFIG_SOUND_CMPCI_SPDIFINVERSE 1
#define CONFIG_SOUND_CMPCI_SPDIFLOOP 1
#define CONFIG_SOUND_CMPCI_SPEAKERS (2)
#undef  CONFIG_SOUND_EMU10K1
#define CONFIG_SOUND_EMU10K1_MODULE 1
#define CONFIG_MIDI_EMU10K1 1
#undef  CONFIG_SOUND_FUSION
#define CONFIG_SOUND_FUSION_MODULE 1
#undef  CONFIG_SOUND_CS4281
#define CONFIG_SOUND_CS4281_MODULE 1
#undef  CONFIG_SOUND_ES1370
#define CONFIG_SOUND_ES1370_MODULE 1
#undef  CONFIG_SOUND_ES1371
#define CONFIG_SOUND_ES1371_MODULE 1
#undef  CONFIG_SOUND_ESSSOLO1
#define CONFIG_SOUND_ESSSOLO1_MODULE 1
#undef  CONFIG_SOUND_MAESTRO
#define CONFIG_SOUND_MAESTRO_MODULE 1
#undef  CONFIG_SOUND_MAESTRO3
#define CONFIG_SOUND_MAESTRO3_MODULE 1
#undef  CONFIG_SOUND_FORTE
#define CONFIG_SOUND_FORTE_MODULE 1
#undef  CONFIG_SOUND_ICH
#define CONFIG_SOUND_ICH_MODULE 1
#undef  CONFIG_SOUND_RME96XX
#define CONFIG_SOUND_RME96XX_MODULE 1
#undef  CONFIG_SOUND_SONICVIBES
#define CONFIG_SOUND_SONICVIBES_MODULE 1
#undef  CONFIG_SOUND_TRIDENT
#define CONFIG_SOUND_TRIDENT_MODULE 1
#undef  CONFIG_SOUND_MSNDCLAS
#define CONFIG_SOUND_MSNDCLAS_MODULE 1
#undef  CONFIG_MSNDCLAS_HAVE_BOOT
#define CONFIG_MSNDCLAS_INIT_FILE "/etc/sound/msndinit.bin"
#define CONFIG_MSNDCLAS_PERM_FILE "/etc/sound/msndperm.bin"
#undef  CONFIG_SOUND_MSNDPIN
#define CONFIG_SOUND_MSNDPIN_MODULE 1
#undef  CONFIG_MSNDPIN_HAVE_BOOT
#define CONFIG_MSNDPIN_INIT_FILE "/etc/sound/pndspini.bin"
#define CONFIG_MSNDPIN_PERM_FILE "/etc/sound/pndsperm.bin"
#undef  CONFIG_SOUND_VIA82CXXX
#define CONFIG_SOUND_VIA82CXXX_MODULE 1
#define CONFIG_MIDI_VIA82CXXX 1
#undef  CONFIG_SOUND_OSS
#define CONFIG_SOUND_OSS_MODULE 1
#undef  CONFIG_SOUND_TRACEINIT
#undef  CONFIG_SOUND_DMAP
#undef  CONFIG_SOUND_AD1816
#define CONFIG_SOUND_AD1816_MODULE 1
#undef  CONFIG_SOUND_AD1889
#define CONFIG_SOUND_AD1889_MODULE 1
#undef  CONFIG_SOUND_SGALAXY
#define CONFIG_SOUND_SGALAXY_MODULE 1
#undef  CONFIG_SOUND_ADLIB
#define CONFIG_SOUND_ADLIB_MODULE 1
#undef  CONFIG_SOUND_ACI_MIXER
#define CONFIG_SOUND_ACI_MIXER_MODULE 1
#undef  CONFIG_SOUND_CS4232
#define CONFIG_SOUND_CS4232_MODULE 1
#undef  CONFIG_SOUND_SSCAPE
#define CONFIG_SOUND_SSCAPE_MODULE 1
#undef  CONFIG_SOUND_GUS
#define CONFIG_SOUND_GUS_MODULE 1
#undef  CONFIG_SOUND_GUS16
#undef  CONFIG_SOUND_GUSMAX
#undef  CONFIG_SOUND_VMIDI
#define CONFIG_SOUND_VMIDI_MODULE 1
#undef  CONFIG_SOUND_TRIX
#define CONFIG_SOUND_TRIX_MODULE 1
#undef  CONFIG_SOUND_MSS
#define CONFIG_SOUND_MSS_MODULE 1
#undef  CONFIG_SOUND_MPU401
#define CONFIG_SOUND_MPU401_MODULE 1
#undef  CONFIG_SOUND_NM256
#define CONFIG_SOUND_NM256_MODULE 1
#undef  CONFIG_SOUND_MAD16
#define CONFIG_SOUND_MAD16_MODULE 1
#define CONFIG_MAD16_OLDCARD 1
#undef  CONFIG_SOUND_PAS
#define CONFIG_SOUND_PAS_MODULE 1
#undef  CONFIG_PAS_JOYSTICK
#undef  CONFIG_SOUND_PSS
#define CONFIG_SOUND_PSS_MODULE 1
#undef  CONFIG_PSS_MIXER
#undef  CONFIG_PSS_HAVE_BOOT
#undef  CONFIG_SOUND_SB
#define CONFIG_SOUND_SB_MODULE 1
#undef  CONFIG_SOUND_AWE32_SYNTH
#define CONFIG_SOUND_AWE32_SYNTH_MODULE 1
#undef  CONFIG_SOUND_KAHLUA
#define CONFIG_SOUND_KAHLUA_MODULE 1
#undef  CONFIG_SOUND_WAVEFRONT
#define CONFIG_SOUND_WAVEFRONT_MODULE 1
#undef  CONFIG_SOUND_MAUI
#define CONFIG_SOUND_MAUI_MODULE 1
#undef  CONFIG_SOUND_YM3812
#define CONFIG_SOUND_YM3812_MODULE 1
#undef  CONFIG_SOUND_OPL3SA1
#define CONFIG_SOUND_OPL3SA1_MODULE 1
#undef  CONFIG_SOUND_OPL3SA2
#define CONFIG_SOUND_OPL3SA2_MODULE 1
#undef  CONFIG_SOUND_YMFPCI
#define CONFIG_SOUND_YMFPCI_MODULE 1
#define CONFIG_SOUND_YMFPCI_LEGACY 1
#undef  CONFIG_SOUND_UART6850
#define CONFIG_SOUND_UART6850_MODULE 1
#undef  CONFIG_SOUND_AEDSP16
#define CONFIG_SOUND_AEDSP16_MODULE 1
#undef  CONFIG_SC6600
#undef  CONFIG_AEDSP16_SBPRO
#undef  CONFIG_AEDSP16_MSS
#undef  CONFIG_AEDSP16_MPU401
#undef  CONFIG_SOUND_TVMIXER
#undef  CONFIG_SOUND_AD1980
#define CONFIG_SOUND_AD1980_MODULE 1
#undef  CONFIG_SOUND_WM97XX
#define CONFIG_SOUND_WM97XX_MODULE 1

/*
 * USB support
 */
#undef  CONFIG_USB

/*
 * Support for USB gadgets
 */
#undef  CONFIG_USB_GADGET

/*
 * Bluetooth support
 */
#undef  CONFIG_BLUEZ

/*
 * Kernel hacking
 */
#undef  CONFIG_DEBUG_KERNEL
#define CONFIG_LOG_BUF_SHIFT (0)

/*
 * Cryptographic options
 */
#undef  CONFIG_CRYPTO

/*
 * Library routines
 */
#undef  CONFIG_CRC32
#undef  CONFIG_ZLIB_INFLATE
#undef  CONFIG_ZLIB_DEFLATE