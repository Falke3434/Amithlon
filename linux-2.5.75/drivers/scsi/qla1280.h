/******************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP1280 (Ultra2) /12160 (Ultra3) SCSI driver
* Copyright (C) 2000 Qlogic Corporation
* (www.qlogic.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************/

#ifndef	_IO_HBA_QLA1280_H	/* wrapper symbol for kernel use */
#define	_IO_HBA_QLA1280_H	/* subject to change without notice */

#ifndef HOSTS_C			/* included in hosts.c */
/*
 * Enable define statement to ignore Data Underrun Errors,
 * remove define statement to enable detection.
 */
/* #define  DATA_UNDERRUN_ERROR_DISABLE */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * Data bit definitions.
 */
#define BIT_0	0x1
#define BIT_1	0x2
#define BIT_2	0x4
#define BIT_3	0x8
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80
#define BIT_8	0x100
#define BIT_9	0x200
#define BIT_10	0x400
#define BIT_11	0x800
#define BIT_12	0x1000
#define BIT_13	0x2000
#define BIT_14	0x4000
#define BIT_15	0x8000
#define BIT_16	0x10000
#define BIT_17	0x20000
#define BIT_18	0x40000
#define BIT_19	0x80000
#define BIT_20	0x100000
#define BIT_21	0x200000
#define BIT_22	0x400000
#define BIT_23	0x800000
#define BIT_24	0x1000000
#define BIT_25	0x2000000
#define BIT_26	0x4000000
#define BIT_27	0x8000000
#define BIT_28	0x10000000
#define BIT_29	0x20000000
#define BIT_30	0x40000000
#define BIT_31	0x80000000

/*
 *  Local Macro Definitions.
 */
#if defined(QL_DEBUG_LEVEL_1) || defined(QL_DEBUG_LEVEL_2) || \
    defined(QL_DEBUG_LEVEL_3) || defined(QL_DEBUG_LEVEL_4) || \
    defined(QL_DEBUG_LEVEL_5) || defined(QL_DEBUG_LEVEL_6) || \
    defined(QL_DEBUG_LEVEL_7)
#define QL_DEBUG_ROUTINES
#endif

/*
 * I/O port macros
*/
/* #define MEMORY_MAPPED_IO *//* Enable memory mapped I/O */
#undef MEMORY_MAPPED_IO		/* Disable memory mapped I/O */

#ifdef QL_DEBUG_LEVEL_1
#define RD_REG_BYTE(addr)		qla1280_getbyte((u8 *)addr)
#define RD_REG_WORD(addr)		qla1280_getword((u16 *)addr)
#define RD_REG_DWORD(addr)		qla1280_getdword((u32 *)addr)
#define WRT_REG_BYTE(addr, data)	qla1280_putbyte((u8 *)addr, data)
#define WRT_REG_WORD(addr, data)	qla1280_putword((u16 *)addr, data)
#define WRT_REG_DWORD(addr, data)	qla1280_putdword((u32 *)addr, data)
#else				/* QL_DEBUG_LEVEL_1 */

#ifdef MEMORY_MAPPED_IO
#define RD_REG_BYTE(addr)		readb(addr)
#define RD_REG_WORD(addr)		readw(addr)
#define RD_REG_DWORD(addr)		readl(addr)
#define WRT_REG_BYTE(addr, data)	writeb(data, addr)
#define WRT_REG_WORD(addr, data)	writew(data, addr)
#define WRT_REG_DWORD(addr, data)	writel(data, addr)
#else				/* MEMORY_MAPPED_IO */
#define RD_REG_BYTE(addr)		inb((unsigned long)addr)
#define RD_REG_WORD(addr)		inw((unsigned long)addr)
#define RD_REG_DWORD(addr)		inl((unsigned long)addr)
/* Parameters are reversed in Linux */
#define WRT_REG_BYTE(addr, data)	outb(data, (unsigned long)addr)
#define WRT_REG_WORD(addr, data)	outw(data, (unsigned long)addr)
#define WRT_REG_DWORD(addr, data)	outl(data, (unsigned long)addr)
#endif				/* MEMORY_MAPPED_IO */
#endif				/* QL_DEBUG_LEVEL_1 */

/*
 * Host adapter default definitions.
 */
#define MAX_BUSES	2	/* 2 */
#define MAX_B_BITS	1

#define MAX_TARGETS	16	/* 16 */
#define MAX_T_BITS	4	/* 4 */

#define MAX_LUNS	8	/* 32 */
#define MAX_L_BITS	3	/* 5 */

/*
 * Watchdog time quantum
 */
#define QLA1280_WDG_TIME_QUANTUM	5	/* In seconds */

/* Command retry count (0-65535) */
#define COMMAND_RETRY_COUNT		255

/* Maximum outstanding commands in ISP queues (1-65535) */
#define MAX_OUTSTANDING_COMMANDS	512

/* ISP request and response entry counts (37-65535) */
#define REQUEST_ENTRY_CNT		256 /* Number of request entries. */
#define RESPONSE_ENTRY_CNT		16  /* Number of response entries. */

/* Maximum equipage per controller */
#define MAX_EQ				(MAX_BUSES * MAX_TARGETS * MAX_LUNS)

/* Number of segments 1 - 65535 */
#define SG_SEGMENTS			32  /* Cmd entry + 6 continuations */

/*
 * SCSI Request Block structure  (sp)  that is placed
 * on cmd->SCp location of every I/O
 */
typedef struct srb {
	Scsi_Cmnd *cmd;		/* (4/8) SCSI command block */
	struct srb *s_next;	/* (4/8) Next block on LU queue */
	struct srb *s_prev;	/* (4/8) Previous block on LU queue */
	uint8_t flags;		/* (1) Status flags. */
	uint8_t dir;		/* direction of transfer */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,18)
	dma_addr_t saved_dma_handle;	/* for unmap of single transfers */
	/* NOTE: the sp->cmd will be NULL when this completion is
	 * called, so you should know the scsi_cmnd when using this */
	struct completion *wait;
#endif

} srb_t;

/*
 * SRB flag definitions
 */
#define SRB_TIMEOUT		BIT_0	/* Command timed out */
#define SRB_SENT		BIT_1	/* Command sent to ISP */
#define SRB_ABORT_PENDING	BIT_2	/* Command abort sent to device */
#define SRB_ABORTED		BIT_3	/* Command aborted command already */

/*
 * Logical Unit Queue structure
 */
typedef struct scsi_lu {
	srb_t *q_first;		/* First block on LU queue */
	srb_t *q_last;		/* Last block on LU queue */
	uint8_t q_flag;		/* LU queue state flags */
	uint8_t q_sense[16];	/* sense data */
	unsigned long io_cnt;	/* total xfer count */
	unsigned long resp_time;/* total response time (start - finish) */
	unsigned long act_time;	/* total actived time (minus queuing time) */
	unsigned long w_cnt;	/* total writes */
	unsigned long r_cnt;	/* total reads */
	uint16_t q_outcnt;	/* Pending jobs for this LU */
#if QL1280_TARGET_MODE_SUPPORT
	void (*q_func)();	/* Target driver event handler */
	int32_t q_param;	/* Target driver event param */
	uint8_t q_lock;		/* Device Queue Lock */
#endif
} scsi_lu_t;

/*
 * Logical Unit flags
 */
#define QLA1280_QBUSY	BIT_0
#define QLA1280_QWAIT	BIT_1
#define QLA1280_QSUSP	BIT_2
#define QLA1280_QSENSE	BIT_3	/* Sense data cache valid */
#define QLA1280_QRESET	BIT_4
#define QLA1280_QHBA	BIT_5
#define QLA1280_BSUSP	BIT_6	/* controller is suspended */
#define QLA1280_BREM	BIT_7	/* controller is removed */

/*
 *  ISP I/O Register Set structure definitions.
 */
struct device_reg {
	uint16_t id_l;		/* ID low */
	uint16_t id_h;		/* ID high */
	uint16_t cfg_0;		/* Configuration 0 */
	uint16_t cfg_1;		/* Configuration 1 */
	uint16_t ictrl;		/* Interface control */
#define ISP_RESET        BIT_0	/* ISP soft reset */
#define ISP_EN_INT       BIT_1	/* ISP enable interrupts. */
#define ISP_EN_RISC      BIT_2	/* ISP enable RISC interrupts. */
#define ISP_FLASH_ENABLE BIT_8	/* Flash BIOS Read/Write enable */
#define ISP_FLASH_UPPER  BIT_9	/* Flash upper bank select */
	uint16_t istatus;	/* Interface status */
#define PCI_64BIT_SLOT   BIT_14	/* PCI 64-bit slot indicator. */
#define RISC_INT         BIT_2	/* RISC interrupt */
#define PCI_INT          BIT_1	/* PCI interrupt */
	uint16_t semaphore;	/* Semaphore */
	uint16_t nvram;		/* NVRAM register. */
#define NV_DESELECT     0
#define NV_CLOCK        BIT_0
#define NV_SELECT       BIT_1
#define NV_DATA_OUT     BIT_2
#define NV_DATA_IN      BIT_3
	uint16_t flash_data;	/* Flash BIOS data */
	uint16_t flash_address;	/* Flash BIOS address */

	uint16_t unused_1[0x2e];	/* 0x14-0x6f Gap */

	uint16_t mailbox0;	/* Mailbox 0 */
	uint16_t mailbox1;	/* Mailbox 1 */
	uint16_t mailbox2;	/* Mailbox 2 */
	uint16_t mailbox3;	/* Mailbox 3 */
	uint16_t mailbox4;	/* Mailbox 4 */
	uint16_t mailbox5;	/* Mailbox 5 */
	uint16_t mailbox6;	/* Mailbox 6 */
	uint16_t mailbox7;	/* Mailbox 7 */

	uint16_t unused_2[0x20];/* 0x80-0xbf Gap */

	uint16_t host_cmd;	/* Host command and control */
#define HOST_INT      BIT_7	/* host interrupt bit */
#define BIOS_ENABLE   BIT_0

	uint16_t unused_6[0x5];	/* 0xc2-0xcb Gap */

	uint16_t gpio_data;
	uint16_t gpio_enable;

	uint16_t unused_7[0x11];	/* d0-f0 */
	uint16_t scsiControlPins;	/* f2 */
};

#define MAILBOX_REGISTER_COUNT	8

/*
 *  ISP product identification definitions in mailboxes after reset.
 */
#define PROD_ID_1		0x4953
#define PROD_ID_2		0x0000
#define PROD_ID_2a		0x5020
#define PROD_ID_3		0x2020
#define PROD_ID_4		0x1

/*
 * ISP host command and control register command definitions
 */
#define HC_RESET_RISC		0x1000	/* Reset RISC */
#define HC_PAUSE_RISC		0x2000	/* Pause RISC */
#define HC_RELEASE_RISC		0x3000	/* Release RISC from reset. */
#define HC_SET_HOST_INT		0x5000	/* Set host interrupt */
#define HC_CLR_HOST_INT		0x6000	/* Clear HOST interrupt */
#define HC_CLR_RISC_INT		0x7000	/* Clear RISC interrupt */
#define HC_DISABLE_BIOS		0x9000	/* Disable BIOS. */

/*
 * ISP mailbox Self-Test status codes
 */
#define MBS_FRM_ALIVE		0	/* Firmware Alive. */
#define MBS_CHKSUM_ERR		1	/* Checksum Error. */
#define MBS_SHADOW_LD_ERR	2	/* Shadow Load Error. */
#define MBS_BUSY		4	/* Busy. */

/*
 * ISP mailbox command complete status codes
 */
#define MBS_CMD_CMP		0x4000	/* Command Complete. */
#define MBS_INV_CMD		0x4001	/* Invalid Command. */
#define MBS_HOST_INF_ERR	0x4002	/* Host Interface Error. */
#define MBS_TEST_FAILED		0x4003	/* Test Failed. */
#define MBS_CMD_ERR		0x4005	/* Command Error. */
#define MBS_CMD_PARAM_ERR	0x4006	/* Command Parameter Error. */

/*
 * ISP mailbox asynchronous event status codes
 */
#define MBA_ASYNC_EVENT		0x8000	/* Asynchronous event. */
#define MBA_BUS_RESET		0x8001	/* SCSI Bus Reset. */
#define MBA_SYSTEM_ERR		0x8002	/* System Error. */
#define MBA_REQ_TRANSFER_ERR	0x8003	/* Request Transfer Error. */
#define MBA_RSP_TRANSFER_ERR	0x8004	/* Response Transfer Error. */
#define MBA_WAKEUP_THRES	0x8005	/* Request Queue Wake-up. */
#define MBA_TIMEOUT_RESET	0x8006	/* Execution Timeout Reset. */
#define MBA_DEVICE_RESET	0x8007	/* Bus Device Reset. */
#define MBA_BUS_MODE_CHANGE	0x800E	/* SCSI bus mode transition. */
#define MBA_SCSI_COMPLETION	0x8020	/* Completion response. */

/*
 * ISP mailbox commands
 */
#define MBC_NOP                     0	/* No Operation. */
#define MBC_LOAD_RAM                1	/* Load RAM. */
#define MBC_EXECUTE_FIRMWARE        2	/* Execute firmware. */
#define MBC_DUMP_RAM		     3   /* Dump RAM contents */
#define MBC_WRITE_RAM_WORD          4	/* Write ram word. */
#define MBC_READ_RAM_WORD           5	/* Read ram word. */
#define MBC_MAILBOX_REGISTER_TEST   6	/* Wrap incoming mailboxes */
#define MBC_VERIFY_CHECKSUM         7	/* Verify checksum. */
#define MBC_ABOUT_FIRMWARE          8	/* Get firmware revision. */
#define MBC_INIT_REQUEST_QUEUE      0x10	/* Initialize request queue. */
#define MBC_INIT_RESPONSE_QUEUE     0x11	/* Initialize response queue. */
#define MBC_EXECUTE_IOCB            0x12	/* Execute IOCB command. */
#define MBC_ABORT_COMMAND           0x15	/* Abort IOCB command. */
#define MBC_ABORT_DEVICE            0x16	/* Abort device (ID/LUN). */
#define MBC_ABORT_TARGET            0x17	/* Abort target (ID). */
#define MBC_BUS_RESET               0x18	/* SCSI bus reset. */
#define MBC_GET_RETRY_COUNT         0x22	/* Get retry count and delay. */
#define MBC_GET_TARGET_PARAMETERS   0x28	/* Get target parameters. */
#define MBC_SET_INITIATOR_ID        0x30	/* Set initiator SCSI ID. */
#define MBC_SET_SELECTION_TIMEOUT   0x31	/* Set selection timeout. */
#define MBC_SET_RETRY_COUNT         0x32	/* Set retry count and delay. */
#define MBC_SET_TAG_AGE_LIMIT       0x33	/* Set tag age limit. */
#define MBC_SET_CLOCK_RATE          0x34	/* Set clock rate. */
#define MBC_SET_ACTIVE_NEGATION     0x35	/* Set active negation state. */
#define MBC_SET_ASYNC_DATA_SETUP    0x36	/* Set async data setup time. */
#define MBC_SET_PCI_CONTROL         0x37	/* Set BUS control parameters. */
#define MBC_SET_TARGET_PARAMETERS   0x38	/* Set target parameters. */
#define MBC_SET_DEVICE_QUEUE        0x39	/* Set device queue parameters */
#define MBC_SET_SYSTEM_PARAMETER    0x45	/* Set system parameter word. */
#define MBC_SET_FIRMWARE_FEATURES   0x4A	/* Set firmware feature word. */
#define MBC_INIT_REQUEST_QUEUE_A64  0x52	/* Initialize request queue A64 */
#define MBC_INIT_RESPONSE_QUEUE_A64 0x53	/* Initialize response q A64. */
#define MBC_ENABLE_TARGET_MODE      0x55	/* Enable target mode. */

/*
 * ISP Get/Set Target Parameters mailbox command control flags.
 */
#define TP_RENEGOTIATE          BIT_8	/* Renegotiate on error. */
#define TP_STOP_QUEUE           BIT_9	/* Stop que on check condition */
#define TP_AUTO_REQUEST_SENSE   BIT_10	/* Automatic request sense. */
#define TP_TAGGED_QUEUE         BIT_11	/* Tagged queuing. */
#define TP_SYNC                 BIT_12	/* Synchronous data transfers. */
#define TP_WIDE                 BIT_13	/* Wide data transfers. */
#define TP_PARITY               BIT_14	/* Parity checking. */
#define TP_DISCONNECT           BIT_15	/* Disconnect privilege. */

/*
 * NVRAM Command values.
 */
#define NV_START_BIT		BIT_2
#define NV_WRITE_OP		(BIT_26 | BIT_24)
#define NV_READ_OP		(BIT_26 | BIT_25)
#define NV_ERASE_OP		(BIT_26 | BIT_25 | BIT_24)
#define NV_MASK_OP		(BIT_26 | BIT_25 | BIT_24)
#define NV_DELAY_COUNT		10

/*
 *  QLogic ISP1280 NVRAM structure definition.
 *
 * NOTE: the firmware structure is byte reversed on big-endian systems
 * because it is read a word at a time from the chip, so the in-memory
 * representation becomes correct
 */
typedef struct {
#if defined(__BIG_ENDIAN)
	uint8_t id1;		/* 1 */
	uint8_t id0;		/* 0 */

	uint8_t id3;		/* 3 */
	uint8_t id2;		/* 2 */

	struct {
		uint8_t bios_configuration_mode:2;
		uint8_t bios_disable:1;
		uint8_t selectable_scsi_boot_enable:1;
		uint8_t cd_rom_boot_enable:1;
		uint8_t disable_loading_risc_code:1;
		uint8_t enable_64bit_addressing:1;
		uint8_t unused_7:1;
	} cntr_flags_1;		/* 5 */
	uint8_t version;	/* 4 */

	struct {
		uint8_t boot_lun_number:5;
		uint8_t scsi_bus_number:1;
		uint8_t unused_6:1;
		uint8_t unused_7:1;
		uint8_t boot_target_number:4;
		uint8_t unused_12:1;
		uint8_t unused_13:1;
		uint8_t unused_14:1;
		uint8_t unused_15:1;
	} cntr_flags_2;		/* 6, 7 */

	uint16_t unused_8;	/* 8, 9 */
	uint16_t unused_10;	/* 10, 11 */
	uint16_t unused_12;	/* 12, 13 */
	uint16_t unused_14;	/* 14, 15 */

	/* Termination
	 * 0 = Disable, 1 = high only, 3 = Auto term
	 */
	union {
		uint8_t c;
		struct {
			uint8_t scsi_bus_1_control:2;
			uint8_t scsi_bus_0_control:2;
			uint8_t unused_0:1;
			uint8_t unused_1:1;
			uint8_t unused_2:1;
			uint8_t auto_term_support:1;
		} f;
	} termination;		/* 17 */
	union {
		uint8_t c;
		struct {
			uint8_t reserved:2;
			uint8_t burst_enable:1;
			uint8_t reserved_1:1;
			uint8_t fifo_threshold:4;
		} f;
	} isp_config;		/* 16 */


	uint16_t isp_parameter;	/* 18, 19 */

	union {
		uint16_t w;
		struct {
			uint16_t enable_fast_posting:1;
			uint16_t report_lvd_bus_transition:1;
			uint16_t unused_2:1;
			uint16_t unused_3:1;
			uint16_t unused_4:1;
			uint16_t unused_5:1;
			uint16_t unused_6:1;
			uint16_t unused_7:1;
			uint16_t unused_8:1;
			uint16_t unused_9:1;
			uint16_t unused_10:1;
			uint16_t unused_11:1;
			uint16_t unused_12:1;
			uint16_t unused_13:1;
			uint16_t unused_14:1;
			uint16_t unused_15:1;
		} f;
	} firmware_feature;	/* 20, 21 */

	uint16_t unused_22;	/* 22, 23 */

	struct {
		uint8_t bus_reset_delay;	/* 25 */
		struct {
			uint8_t initiator_id:4;
			uint8_t scsi_reset_disable:1;
			uint8_t scsi_bus_size:1;
			uint8_t scsi_bus_type:1;
			uint8_t unused_7:1;
		} config_1;	/* 24 */

		uint8_t retry_delay;	/* 27 */
		uint8_t retry_count;	/* 26 */

		uint8_t unused_29;	/* 29 */
		struct {
			uint8_t async_data_setup_time:4;
			uint8_t req_ack_active_negation:1;
			uint8_t data_line_active_negation:1;
			uint8_t unused_6:1;
			uint8_t unused_7:1;
		} config_2;	/* 28 */


		uint16_t selection_timeout;	/* 30, 31 */
		uint16_t max_queue_depth;	/* 32, 33 */

		uint16_t unused_34;	/* 34, 35 */
		uint16_t unused_36;	/* 36, 37 */
		uint16_t unused_38;	/* 38, 39 */

		struct {
			uint8_t execution_throttle;	/* 41 */
			union {
				uint8_t c;
				struct {
					uint8_t renegotiate_on_error:1;
					uint8_t stop_queue_on_check:1;
					uint8_t auto_request_sense:1;
					uint8_t tag_queuing:1;
					uint8_t sync_data_transfers:1;
					uint8_t wide_data_transfers:1;
					uint8_t parity_checking:1;
					uint8_t disconnect_allowed:1;
				} f;
			} parameter;	/* 40 */

			struct {
				uint8_t sync_offset:4;
				uint8_t device_enable:1;
				uint8_t lun_disable:1;
				uint8_t unused_6:1;
				uint8_t unused_7:1;
			} flags;	/* 43 */
			uint8_t sync_period;	/* 42 */


			uint16_t unused_44;	/* 44, 45 */
		} target[MAX_TARGETS];
	} bus[MAX_BUSES];

	uint16_t unused_248;	/* 248, 249 */

	uint16_t subsystem_id[2];	/* 250, 251, 252, 253 */

	uint8_t chksum;		/* 255 */
	uint8_t unused_254;	/* 254 */

#elif defined(__LITTLE_ENDIAN)
	uint8_t id0;		/* 0 */
	uint8_t id1;		/* 1 */
	uint8_t id2;		/* 2 */
	uint8_t id3;		/* 3 */
	uint8_t version;	/* 4 */

	struct {
		uint8_t bios_configuration_mode:2;
		uint8_t bios_disable:1;
		uint8_t selectable_scsi_boot_enable:1;
		uint8_t cd_rom_boot_enable:1;
		uint8_t disable_loading_risc_code:1;
		uint8_t enable_64bit_addressing:1;
		uint8_t unused_7:1;
	} cntr_flags_1;		/* 5 */

	struct {
		uint8_t boot_lun_number:5;
		uint8_t scsi_bus_number:1;
		uint8_t unused_6:1;
		uint8_t unused_7:1;
		uint8_t boot_target_number:4;
		uint8_t unused_12:1;
		uint8_t unused_13:1;
		uint8_t unused_14:1;
		uint8_t unused_15:1;
	} cntr_flags_2;		/* 6, 7 */

	uint16_t unused_8;	/* 8, 9 */
	uint16_t unused_10;	/* 10, 11 */
	uint16_t unused_12;	/* 12, 13 */
	uint16_t unused_14;	/* 14, 15 */

	union {
		uint8_t c;
		struct {
			uint8_t reserved:2;
			uint8_t burst_enable:1;
			uint8_t reserved_1:1;
			uint8_t fifo_threshold:4;
		} f;
	} isp_config;		/* 16 */

	/* Termination
	 * 0 = Disable, 1 = high only, 3 = Auto term
	 */
	union {
		uint8_t c;
		struct {
			uint8_t scsi_bus_1_control:2;
			uint8_t scsi_bus_0_control:2;
			uint8_t unused_0:1;
			uint8_t unused_1:1;
			uint8_t unused_2:1;
			uint8_t auto_term_support:1;
		} f;
	} termination;		/* 17 */

	uint16_t isp_parameter;	/* 18, 19 */

	union {
		uint16_t w;
		struct {
			uint8_t enable_fast_posting:1;
			uint8_t report_lvd_bus_transition:1;
			uint8_t unused_2:1;
			uint8_t unused_3:1;
			uint8_t unused_4:1;
			uint8_t unused_5:1;
			uint8_t unused_6:1;
			uint8_t unused_7:1;
			uint8_t unused_8:1;
			uint8_t unused_9:1;
			uint8_t unused_10:1;
			uint8_t unused_11:1;
			uint8_t unused_12:1;
			uint8_t unused_13:1;
			uint8_t unused_14:1;
			uint8_t unused_15:1;
		} f;
	} firmware_feature;	/* 20, 21 */

	uint16_t unused_22;	/* 22, 23 */

	struct {
		struct {
			uint8_t initiator_id:4;
			uint8_t scsi_reset_disable:1;
			uint8_t scsi_bus_size:1;
			uint8_t scsi_bus_type:1;
			uint8_t unused_7:1;
		} config_1;	/* 24 */

		uint8_t bus_reset_delay;	/* 25 */
		uint8_t retry_count;	/* 26 */
		uint8_t retry_delay;	/* 27 */

		struct {
			uint8_t async_data_setup_time:4;
			uint8_t req_ack_active_negation:1;
			uint8_t data_line_active_negation:1;
			uint8_t unused_6:1;
			uint8_t unused_7:1;
		} config_2;	/* 28 */

		uint8_t unused_29;	/* 29 */

		uint16_t selection_timeout;	/* 30, 31 */
		uint16_t max_queue_depth;	/* 32, 33 */

		uint16_t unused_34;	/* 34, 35 */
		uint16_t unused_36;	/* 36, 37 */
		uint16_t unused_38;	/* 38, 39 */

		struct {
			union {
				uint8_t c;
				struct {
					uint8_t renegotiate_on_error:1;
					uint8_t stop_queue_on_check:1;
					uint8_t auto_request_sense:1;
					uint8_t tag_queuing:1;
					uint8_t sync_data_transfers:1;
					uint8_t wide_data_transfers:1;
					uint8_t parity_checking:1;
					uint8_t disconnect_allowed:1;
				} f;
			} parameter;	/* 40 */

			uint8_t execution_throttle;	/* 41 */
			uint8_t sync_period;	/* 42 */

			struct {
				uint8_t sync_offset:4;
				uint8_t device_enable:1;
				uint8_t lun_disable:1;
				uint8_t unused_6:1;
				uint8_t unused_7:1;
			} flags;	/* 43 */

			uint16_t unused_44;	/* 44, 45 */
		} target[MAX_TARGETS];
	} bus[MAX_BUSES];

	uint16_t unused_248;	/* 248, 249 */

	uint16_t subsystem_id[2];	/* 250, 251, 252, 253 */

	uint8_t unused_254;	/* 254 */

	uint8_t chksum;		/* 255 */
#else
#error neither __BIG_ENDIAN nor __LITTLE_ENDIAN is defined
#endif
} nvram_t;

/*
 *  QLogic ISP12160 NVRAM structure definition.
 *
 * NOTE: the firmware structure is byte reversed on big-endian systems
 * because it is read a word at a time from the chip, so the in-memory
 * representation becomes correct
 */
typedef struct {
#if defined(__BIG_ENDIAN)
	uint8_t id1;		/* 1 */
	uint8_t id0;		/* 0 */

	uint8_t id3;		/* 3 */
	uint8_t id2;		/* 2 */

	/* Host/Bios Flags */
	struct {
		uint8_t bios_configuration_mode:2;
		uint8_t bios_disable:1;
		uint8_t selectable_scsi_boot_enable:1;
		uint8_t cd_rom_boot_enable:1;
		uint8_t disable_loading_risc_code:1;
		uint8_t unused_6:1;
		uint8_t unused_7:1;
	} cntr_flags_1;		/* 5 */
	uint8_t version;	/* 4 */

	/* Selectable Boot Support */
	struct {
		uint16_t boot_lun_number:5;
		uint16_t scsi_bus_number:1;
		uint16_t unused_6:1;
		uint16_t unused_7:1;
		uint16_t boot_target_number:4;
		uint16_t unused_12:1;
		uint16_t unused_13:1;
		uint16_t unused_14:1;
		uint16_t unused_15:1;
	} cntr_flags_2;		/* 6, 7 */

	uint16_t unused_8;	/* 8, 9 */
	uint16_t unused_10;	/* 10, 11 */
	uint16_t unused_12;	/* 12, 13 */
	uint16_t unused_14;	/* 14, 15 */

	/* Termination
	 * 0 = Disable, 1 = high only, 3 = Auto term
	 */
	union {
		uint8_t c;
		struct {
			uint8_t scsi_bus_1_control:2;
			uint8_t scsi_bus_0_control:2;
			uint8_t unused_0:1;
			uint8_t unused_1:1;
			uint8_t unused_2:1;
			uint8_t auto_term_support:1;
		} f;
	} termination;		/* 17 */
	/* Auto Term - 3                          */
	/* High Only - 1 (GPIO2 = 1 & GPIO3 = 0)  */
	/* Disable - 0 (GPIO2 = 0 & GPIO3 = X)    */
	/* ISP Config Parameters */
	union {
		uint8_t c;
		struct {
			uint8_t reserved:2;
			uint8_t burst_enable:1;
			uint8_t reserved_1:1;
			uint8_t fifo_threshold:4;
		} f;
	} isp_config;		/* 16 */

	uint16_t isp_parameter;	/* 18, 19 */

	union {
		uint16_t w;
		struct {
			uint16_t enable_fast_posting:1;
			uint16_t report_lvd_bus_transition:1;
			uint16_t unused_2:1;
			uint16_t unused_3:1;
			uint16_t unused_4:1;
			uint16_t unused_5:1;
			uint16_t unused_6:1;
			uint16_t unused_7:1;
			uint16_t unused_8:1;
			uint16_t unused_9:1;
			uint16_t unused_10:1;
			uint16_t unused_11:1;
			uint16_t unused_12:1;
			uint16_t unused_13:1;
			uint16_t unused_14:1;
			uint16_t unused_15:1;
		} f;
	} firmware_feature;	/* 20, 21 */

	uint16_t unused_22;	/* 22, 23 */

	struct {
		uint8_t bus_reset_delay;	/* 25 */
		struct {
			uint8_t initiator_id:4;
			uint8_t scsi_reset_disable:1;
			uint8_t scsi_bus_size:1;
			uint8_t scsi_bus_type:1;
			uint8_t unused_7:1;
		} config_1;	/* 24 */

		uint8_t retry_delay;	/* 27 */
		uint8_t retry_count;	/* 26 */

		uint8_t unused_29;	/* 29 */
		/* Adapter Capabilities bits */
		struct {
			uint8_t async_data_setup_time:4;
			uint8_t req_ack_active_negation:1;
			uint8_t data_line_active_negation:1;
			uint8_t unused_6:1;
			uint8_t unused_7:1;
		} config_2;	/* 28 */

		uint16_t selection_timeout;	/* 30, 31 */
		uint16_t max_queue_depth;	/* 32, 33 */

		uint16_t unused_34;	/* 34, 35 */
		uint16_t unused_36;	/* 36, 37 */
		uint16_t unused_38;	/* 38, 39 */

		struct {
			uint8_t execution_throttle;	/* 41 */
			union {
				uint8_t c;
				struct {
					uint8_t renegotiate_on_error:1;
					uint8_t stop_queue_on_check:1;
					uint8_t auto_request_sense:1;
					uint8_t tag_queuing:1;
					uint8_t sync_data_transfers:1;
					uint8_t wide_data_transfers:1;
					uint8_t parity_checking:1;
					uint8_t disconnect_allowed:1;
				} f;
			} parameter;	/* 40 */

			struct {
				uint8_t sync_offset:5;
				uint8_t device_enable:1;
				uint8_t unused_6:1;
				uint8_t unused_7:1;
			} flags1; /* 43 */
			uint8_t sync_period;	/* 42 */

			uint8_t unused_45;	/* 45 */
			struct {
				uint8_t ppr_options:4;
				uint8_t ppr_bus_width:2;
				uint8_t unused_8:1;
				uint8_t enable_ppr:1;
			} flags2;	/* 44 */

		} target[MAX_TARGETS];
	} bus[MAX_BUSES];

	uint16_t unused_248;	/* 248, 249 */

	uint16_t subsystem_id[2];	/* 250, 251, 252, 253 */

	uint8_t chksum;		/* 255 */
	uint8_t System_Id_Pointer;	/* 254 */

#elif defined(__LITTLE_ENDIAN)
	uint8_t id0;		/* 0 */
	uint8_t id1;		/* 1 */
	uint8_t id2;		/* 2 */
	uint8_t id3;		/* 3 */
	uint8_t version;	/* 4 */
	/* Host/Bios Flags */
	struct {
		uint8_t bios_configuration_mode:2;
		uint8_t bios_disable:1;
		uint8_t selectable_scsi_boot_enable:1;
		uint8_t cd_rom_boot_enable:1;
		uint8_t disable_loading_risc_code:1;
		uint8_t unused_6:1;
		uint8_t unused_7:1;
	} cntr_flags_1;		/* 5 */
	/* Selectable Boot Support */
	struct {
		uint16_t boot_lun_number:5;
		uint16_t scsi_bus_number:1;
		uint16_t unused_6:1;
		uint16_t unused_7:1;
		uint16_t boot_target_number:4;
		uint16_t unused_12:1;
		uint16_t unused_13:1;
		uint16_t unused_14:1;
		uint16_t unused_15:1;
	} cntr_flags_2;		/* 6, 7 */

	uint16_t unused_8;	/* 8, 9 */
	uint16_t unused_10;	/* 10, 11 */
	uint16_t unused_12;	/* 12, 13 */
	uint16_t unused_14;	/* 14, 15 */

	/* ISP Config Parameters */
	union {
		uint8_t c;
		struct {
			uint8_t reserved:2;
			uint8_t burst_enable:1;
			uint8_t reserved_1:1;
			uint8_t fifo_threshold:4;
		} f;
	} isp_config;		/* 16 */

	/* Termination
	 * 0 = Disable, 1 = high only, 3 = Auto term
	 */
	union {
		uint8_t c;
		struct {
			uint8_t scsi_bus_1_control:2;
			uint8_t scsi_bus_0_control:2;
			uint8_t unused_0:1;
			uint8_t unused_1:1;
			uint8_t unused_2:1;
			uint8_t auto_term_support:1;
		} f;
	} termination;		/* 17 */
	/* Auto Term - 3                          */
	/* High Only - 1 (GPIO2 = 1 & GPIO3 = 0)  */
	/* Disable - 0 (GPIO2 = 0 & GPIO3 = X)    */

	uint16_t isp_parameter;	/* 18, 19 */

	union {
		uint16_t w;
		struct {
			uint16_t enable_fast_posting:1;
			uint16_t report_lvd_bus_transition:1;
			uint16_t unused_2:1;
			uint16_t unused_3:1;
			uint16_t unused_4:1;
			uint16_t unused_5:1;
			uint16_t unused_6:1;
			uint16_t unused_7:1;
			uint16_t unused_8:1;
			uint16_t unused_9:1;
			uint16_t unused_10:1;
			uint16_t unused_11:1;
			uint16_t unused_12:1;
			uint16_t unused_13:1;
			uint16_t unused_14:1;
			uint16_t unused_15:1;
		} f;
	} firmware_feature;	/* 20, 21 */

	uint16_t unused_22;	/* 22, 23 */

	struct {
		struct {
			uint8_t initiator_id:4;
			uint8_t scsi_reset_disable:1;
			uint8_t scsi_bus_size:1;
			uint8_t scsi_bus_type:1;
			uint8_t unused_7:1;
		} config_1;	/* 24 */

		uint8_t bus_reset_delay;	/* 25 */
		uint8_t retry_count;	/* 26 */
		uint8_t retry_delay;	/* 27 */
		/* Adapter Capabilities bits */
		struct {
			uint8_t async_data_setup_time:4;
			uint8_t req_ack_active_negation:1;
			uint8_t data_line_active_negation:1;
			uint8_t unused_6:1;
			uint8_t unused_7:1;
		} config_2;	/* 28 */

		uint8_t unused_29;	/* 29 */

		uint16_t selection_timeout;	/* 30, 31 */
		uint16_t max_queue_depth;	/* 32, 33 */

		uint16_t unused_34;	/* 34, 35 */
		uint16_t unused_36;	/* 36, 37 */
		uint16_t unused_38;	/* 38, 39 */

		struct {
			union {
				uint8_t c;
				struct {
					uint8_t renegotiate_on_error:1;
					uint8_t stop_queue_on_check:1;
					uint8_t auto_request_sense:1;
					uint8_t tag_queuing:1;
					uint8_t sync_data_transfers:1;
					uint8_t wide_data_transfers:1;
					uint8_t parity_checking:1;
					uint8_t disconnect_allowed:1;
				} f;
			} parameter;	/* 40 */

			uint8_t execution_throttle;	/* 41 */
			uint8_t sync_period;	/* 42 */

			struct {
				uint8_t sync_offset:5;
				uint8_t device_enable:1;
				uint8_t unused_6:1;
				uint8_t unused_7:1;
			} flags1; /* 43 */

			struct {
				uint8_t ppr_options:4;
				uint8_t ppr_bus_width:2;
				uint8_t unused_8:1;
				uint8_t enable_ppr:1;
			} flags2;	/* 43 */

			uint8_t unused_45;	/* 45 */
		} target[MAX_TARGETS];
	} bus[MAX_BUSES];

	uint16_t unused_248;	/* 248, 249 */

	uint16_t subsystem_id[2];	/* 250, 251, 252, 253 */

	uint8_t System_Id_Pointer;	/* 254 */

	uint8_t chksum;		/* 255 */
#else
#error neither __BIG_ENDIAN nor __LITTLE_ENDIAN is defined
#endif
} nvram160_t;

/*
 * ISP queue - command entry structure definition.
 */
#define MAX_CMDSZ	12		/* SCSI maximum CDB size. */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
#define COMMAND_TYPE    1		/* Command entry */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t handle;		/* System handle. */
	uint8_t lun;			/* SCSI LUN */
	uint8_t target;			/* SCSI ID */
	uint16_t cdb_len;		/* SCSI command length. */
	uint16_t control_flags;		/* Control flags. */
	uint16_t reserved;
	uint16_t timeout;		/* Command timeout. */
	uint16_t dseg_count;		/* Data segment count. */
	uint8_t scsi_cdb[MAX_CMDSZ];	/* SCSI command words. */
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
	uint32_t dseg_3_address;	/* Data segment 3 address. */
	uint32_t dseg_3_length;		/* Data segment 3 length. */
} cmd_entry_t;

/*
 * ISP queue - continuation entry structure definition.
 */
typedef struct {
	uint8_t entry_type;		/* Entry type. */
#define CONTINUE_TYPE   2		/* Continuation entry. */
	uint8_t entry_count;		/* Entry count. */
	uint8_t sys_define;		/* System defined. */
	uint8_t entry_status;		/* Entry Status. */
	uint32_t reserved;		/* Reserved */
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;		/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;		/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;		/* Data segment 2 length. */
	uint32_t dseg_3_address;	/* Data segment 3 address. */
	uint32_t dseg_3_length;		/* Data segment 3 length. */
	uint32_t dseg_4_address;	/* Data segment 4 address. */
	uint32_t dseg_4_length;		/* Data segment 4 length. */
	uint32_t dseg_5_address;	/* Data segment 5 address. */
	uint32_t dseg_5_length;		/* Data segment 5 length. */
	uint32_t dseg_6_address;	/* Data segment 6 address. */
	uint32_t dseg_6_length;		/* Data segment 6 length. */
} cont_entry_t;

/*
 * ISP queue - status entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define STATUS_TYPE     3	/* Status entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t sys_define;	/* System defined. */
	uint8_t entry_status;	/* Entry Status. */
#define RF_CONT         BIT_0	/* Continuation. */
#define RF_FULL         BIT_1	/* Full */
#define RF_BAD_HEADER   BIT_2	/* Bad header. */
#define RF_BAD_PAYLOAD  BIT_3	/* Bad payload. */
	uint32_t handle;	/* System handle. */
	uint16_t scsi_status;	/* SCSI status. */
	uint16_t comp_status;	/* Completion status. */
	uint16_t state_flags;	/* State flags. */
#define SF_TRANSFER_CMPL BIT_14	/* Transfer Complete. */
#define SF_GOT_SENSE    BIT_13	/* Got Sense */
#define SF_GOT_STATUS    BIT_12	/* Got Status */
#define SF_TRANSFERRED_DATA BIT_11	/* Transferred data */
#define SF_SENT_CDB   BIT_10	/* Send CDB */
#define SF_GOT_TARGET  BIT_9	/*  */
#define SF_GOT_BUS     BIT_8	/*  */
	uint16_t status_flags;	/* Status flags. */
	uint16_t time;		/* Time. */
	uint16_t req_sense_length;	/* Request sense data length. */
	uint32_t residual_length;	/* Residual transfer length. */
	uint16_t reserved[4];
	uint8_t req_sense_data[32];	/* Request sense data. */
} sts_entry_t, response_t;

/*
 * ISP queue - marker entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define MARKER_TYPE     4	/* Marker entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t sys_define;	/* System defined. */
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved;
	uint8_t lun;		/* SCSI LUN */
	uint8_t target;		/* SCSI ID */
	uint8_t modifier;	/* Modifier (7-0). */
#define MK_SYNC_ID_LUN      0	/* Synchronize ID/LUN */
#define MK_SYNC_ID          1	/* Synchronize ID */
#define MK_SYNC_ALL         2	/* Synchronize all ID/LUN */
	uint8_t reserved_1[53];
} mrk_entry_t;

/*
 * ISP queue - extended command entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define EXTENDED_CMD_TYPE  5	/* Extended command entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t sys_define;	/* System defined. */
	uint8_t entry_status;	/* Entry Status. */
	uint32_t handle;	/* System handle. */
	uint8_t lun;		/* SCSI LUN */
	uint8_t target;		/* SCSI ID */
	uint16_t cdb_len;	/* SCSI command length. */
	uint16_t control_flags;	/* Control flags. */
	uint16_t reserved;
	uint16_t timeout;	/* Command timeout. */
	uint16_t dseg_count;	/* Data segment count. */
	uint8_t scsi_cdb[88];	/* SCSI command words. */
} ecmd_entry_t;

/*
 * ISP queue - 64-Bit addressing, command entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define COMMAND_A64_TYPE 9	/* Command A64 entry */
	uint8_t entry_count;	/* Entry count. */
	uint8_t sys_define;	/* System defined. */
	uint8_t entry_status;	/* Entry Status. */
	uint32_t handle;	/* System handle. */
	uint8_t lun;		/* SCSI LUN */
	uint8_t target;		/* SCSI ID */
	uint16_t cdb_len;	/* SCSI command length. */
	uint16_t control_flags;	/* Control flags. */
	uint16_t reserved;
	uint16_t timeout;	/* Command timeout. */
	uint16_t dseg_count;	/* Data segment count. */
	uint8_t scsi_cdb[MAX_CMDSZ];	/* SCSI command words. */
	uint32_t reserved_1[2];	/* unused */
	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_length;	/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_length;	/* Data segment 1 length. */
} cmd_a64_entry_t, request_t;

/*
 * ISP queue - 64-Bit addressing, continuation entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define CONTINUE_A64_TYPE 0xA	/* Continuation A64 entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t sys_define;	/* System defined. */
	uint8_t entry_status;	/* Entry Status. */
	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_length;	/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_length;	/* Data segment 1 length. */
	uint32_t dseg_2_address[2];	/* Data segment 2 address. */
	uint32_t dseg_2_length;	/* Data segment 2 length. */
	uint32_t dseg_3_address[2];	/* Data segment 3 address. */
	uint32_t dseg_3_length;	/* Data segment 3 length. */
	uint32_t dseg_4_address[2];	/* Data segment 4 address. */
	uint32_t dseg_4_length;	/* Data segment 4 length. */
} cont_a64_entry_t;

/*
 * ISP queue - enable LUN entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define ENABLE_LUN_TYPE 0xB	/* Enable LUN entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status not used. */
	uint32_t reserved_2;
	uint16_t lun;		/* Bit 15 is bus number. */
	uint16_t reserved_4;
	uint32_t option_flags;
	uint8_t status;
	uint8_t reserved_5;
	uint8_t command_count;	/* Number of ATIOs allocated. */
	uint8_t immed_notify_count;	/* Number of Immediate Notify */
	/* entries allocated. */
	uint8_t group_6_length;	/* SCSI CDB length for group 6 */
	/* commands (2-26). */
	uint8_t group_7_length;	/* SCSI CDB length for group 7 */
	/* commands (2-26). */
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t reserved_6[20];
} elun_entry_t;

/*
 * ISP queue - modify LUN entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define MODIFY_LUN_TYPE 0xC	/* Modify LUN entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;		/* SCSI LUN */
	uint8_t reserved_3;
	uint8_t operators;
	uint8_t reserved_4;
	uint32_t option_flags;
	uint8_t status;
	uint8_t reserved_5;
	uint8_t command_count;	/* Number of ATIOs allocated. */
	uint8_t immed_notify_count;	/* Number of Immediate Notify */
	/* entries allocated. */
	uint16_t reserved_6;
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t reserved_7[20];
} modify_lun_entry_t;

/*
 * ISP queue - immediate notify entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define IMMED_NOTIFY_TYPE 0xD	/* Immediate notify entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t reserved_4;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	/* entries allocated. */
	uint16_t seq_id;
	uint8_t scsi_msg[8];	/* SCSI message not handled by ISP */
	uint16_t reserved_5[8];
	uint8_t sense_data[18];
} notify_entry_t;

/*
 * ISP queue - notify acknowledge entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define NOTIFY_ACK_TYPE 0xE	/* Notify acknowledge entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t event;
	uint16_t seq_id;
	uint16_t reserved_4[22];
} nack_entry_t;

/*
 * ISP queue - Accept Target I/O (ATIO) entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define ACCEPT_TGT_IO_TYPE 6	/* Accept target I/O entry. */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;
	uint8_t initiator_id;
	uint8_t cdb_len;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t scsi_status;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	uint8_t cdb[26];
	uint8_t sense_data[18];
} atio_entry_t;

/*
 * ISP queue - Continue Target I/O (CTIO) entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define CONTINUE_TGT_IO_TYPE 7	/* CTIO entry */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;		/* SCSI LUN */
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t scsi_status;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	uint32_t transfer_length;
	uint32_t residual;
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t dseg_count;	/* Data segment count. */
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;	/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint32_t dseg_1_length;	/* Data segment 1 length. */
	uint32_t dseg_2_address;	/* Data segment 2 address. */
	uint32_t dseg_2_length;	/* Data segment 2 length. */
	uint32_t dseg_3_address;	/* Data segment 3 address. */
	uint32_t dseg_3_length;	/* Data segment 3 length. */
} ctio_entry_t;

/*
 * ISP queue - CTIO returned entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define CTIO_RET_TYPE   7	/* CTIO return entry */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;		/* SCSI LUN */
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t scsi_status;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	uint32_t transfer_length;
	uint32_t residual;
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t dseg_count;	/* Data segment count. */
	uint32_t dseg_0_address;	/* Data segment 0 address. */
	uint32_t dseg_0_length;	/* Data segment 0 length. */
	uint32_t dseg_1_address;	/* Data segment 1 address. */
	uint16_t dseg_1_length;	/* Data segment 1 length. */
	uint8_t sense_data[18];
} ctio_ret_entry_t;

/*
 * ISP queue - CTIO A64 entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define CTIO_A64_TYPE 0xF	/* CTIO A64 entry */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;		/* SCSI LUN */
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t scsi_status;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	uint32_t transfer_length;
	uint32_t residual;
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t dseg_count;	/* Data segment count. */
	uint32_t reserved_4[2];
	uint32_t dseg_0_address[2];	/* Data segment 0 address. */
	uint32_t dseg_0_length;	/* Data segment 0 length. */
	uint32_t dseg_1_address[2];	/* Data segment 1 address. */
	uint32_t dseg_1_length;	/* Data segment 1 length. */
} ctio_a64_entry_t;

/*
 * ISP queue - CTIO returned entry structure definition.
 */
typedef struct {
	uint8_t entry_type;	/* Entry type. */
#define CTIO_A64_RET_TYPE 0xF	/* CTIO A64 returned entry */
	uint8_t entry_count;	/* Entry count. */
	uint8_t reserved_1;
	uint8_t entry_status;	/* Entry Status. */
	uint32_t reserved_2;
	uint8_t lun;		/* SCSI LUN */
	uint8_t initiator_id;
	uint8_t reserved_3;
	uint8_t target_id;
	uint32_t option_flags;
	uint8_t status;
	uint8_t scsi_status;
	uint8_t tag_value;	/* Received queue tag message value */
	uint8_t tag_type;	/* Received queue tag message type */
	uint32_t transfer_length;
	uint32_t residual;
	uint16_t timeout;	/* 0 = 30 seconds, 0xFFFF = disable */
	uint16_t dseg_count;	/* Data segment count. */
	uint16_t reserved_4[7];
	uint8_t sense_data[18];
} ctio_a64_ret_entry_t;

/*
 * ISP request and response queue entry sizes
 */
#define RESPONSE_ENTRY_SIZE	(sizeof(response_t))
#define REQUEST_ENTRY_SIZE	(sizeof(request_t))

/*
 * ISP status entry - completion status definitions.
 */
#define CS_COMPLETE         0x0	/* No errors */
#define CS_INCOMPLETE       0x1	/* Incomplete transfer of cmd. */
#define CS_DMA              0x2	/* A DMA direction error. */
#define CS_TRANSPORT        0x3	/* Transport error. */
#define CS_RESET            0x4	/* SCSI bus reset occurred */
#define CS_ABORTED          0x5	/* System aborted command. */
#define CS_TIMEOUT          0x6	/* Timeout error. */
#define CS_DATA_OVERRUN     0x7	/* Data overrun. */
#define CS_COMMAND_OVERRUN  0x8	/* Command Overrun. */
#define CS_STATUS_OVERRUN   0x9	/* Status Overrun. */
#define CS_BAD_MSG          0xA	/* Bad msg after status phase. */
#define CS_NO_MSG_OUT       0xB	/* No msg out after selection. */
#define CS_EXTENDED_ID      0xC	/* Extended ID failed. */
#define CS_IDE_MSG          0xD	/* Target rejected IDE msg. */
#define CS_ABORT_MSG        0xE	/* Target rejected abort msg. */
#define CS_REJECT_MSG       0xF	/* Target rejected reject msg. */
#define CS_NOP_MSG          0x10	/* Target rejected NOP msg. */
#define CS_PARITY_MSG       0x11	/* Target rejected parity msg. */
#define CS_DEV_RESET_MSG    0x12	/* Target rejected dev rst msg. */
#define CS_ID_MSG           0x13	/* Target rejected ID msg. */
#define CS_FREE             0x14	/* Unexpected bus free. */
#define CS_DATA_UNDERRUN    0x15	/* Data Underrun. */
#define CS_TRANACTION_1     0x18	/* Transaction error 1 */
#define CS_TRANACTION_2     0x19	/* Transaction error 2 */
#define CS_TRANACTION_3     0x1a	/* Transaction error 3 */
#define CS_INV_ENTRY_TYPE   0x1b	/* Invalid entry type */
#define CS_DEV_QUEUE_FULL   0x1c	/* Device queue full */
#define CS_PHASED_SKIPPED   0x1d	/* SCSI phase skipped */
#define CS_ARS_FAILED       0x1e	/* ARS failed */
#define CS_LVD_BUS_ERROR    0x21	/* LVD bus error */
#define CS_BAD_PAYLOAD      0x80	/* Driver defined */
#define CS_UNKNOWN          0x81	/* Driver defined */
#define CS_RETRY            0x82	/* Driver defined */

/*
 * ISP status entry - SCSI status byte bit definitions.
 */
#define SS_CHECK_CONDITION  BIT_1
#define SS_CONDITION_MET    BIT_2
#define SS_BUSY_CONDITION   BIT_3
#define SS_RESERVE_CONFLICT (BIT_4 | BIT_3)

/*
 * ISP target entries - Option flags bit definitions.
 */
#define OF_ENABLE_TAG       BIT_1	/* Tagged queue action enable */
#define OF_DATA_IN          BIT_6	/* Data in to initiator */
					/*  (data from target to initiator) */
#define OF_DATA_OUT         BIT_7	/* Data out from initiator */
					/*  (data from initiator to target) */
#define OF_NO_DATA          (BIT_7 | BIT_6)
#define OF_DISC_DISABLED    BIT_15	/* Disconnects disabled */
#define OF_DISABLE_SDP      BIT_24	/* Disable sending save data ptr */
#define OF_SEND_RDP         BIT_26	/* Send restore data pointers msg */
#define OF_FORCE_DISC       BIT_30	/* Disconnects mandatory */
#define OF_SSTS             BIT_31	/* Send SCSI status */

#if QL1280_TARGET_MODE_SUPPORT
/*
 * Target Read/Write buffer structure.
 */
#define TARGET_DATA_OFFSET	4
#define TARGET_DATA_SIZE	0x2000	/* 8K */
#define TARGET_INQ_OFFSET	(TARGET_DATA_OFFSET + TARGET_DATA_SIZE)
#define TARGET_SENSE_SIZE	18
#define TARGET_BUF_SIZE		36

typedef struct {
	uint8_t hdr[4];
	uint8_t data[TARGET_DATA_SIZE];
	struct ident inq;
} tgt_t;
#endif

/*
 * BUS parameters/settings structure
 */
typedef struct {
	uint8_t id;		/* Host adapter SCSI id */
	uint8_t bus_reset_delay;	/* SCSI bus reset delay. */
	uint8_t failed_reset_count;	/* number of time reset failed */
	uint8_t unused;
	uint16_t device_enables;	/* Device enable bits. */
	uint16_t lun_disables;	/* LUN disable bits. */
	uint16_t qtag_enables;	/* Tag queue enables. */
	uint16_t hiwat;		/* High water mark per device. */
	uint8_t reset_marker:1;
	uint8_t disable_scsi_reset:1;
	uint8_t scsi_bus_dead:1;	/* SCSI Bus is Dead, when 5 back to back resets failed */

} bus_param_t;

/*
 * Linux Host Adapter structure
 */
struct scsi_qla_host {
	/* Linux adapter configuration data */
	struct Scsi_Host *host;	/* pointer to host data */
	struct scsi_qla_host *next;
	struct device_reg *iobase;	/* Base Memory-mapped I/O address */
	uint8_t pci_bus;
	uint8_t pci_device_fn;
	uint8_t devnum;
	struct pci_dev *pdev;

	volatile unsigned char *mmpbase;	/* memory mapped address */
	unsigned long host_no;
	unsigned long instance;
	uint8_t revision;
	uint8_t ports;

	unsigned long actthreads;
	unsigned long qthreads;
	unsigned long isr_count;	/* Interrupt count */
	unsigned long spurious_int;

	uint32_t device_id;

	/* Outstandings ISP commands. */
	srb_t *outstanding_cmds[MAX_OUTSTANDING_COMMANDS];

	/* BUS configuration data */
	bus_param_t bus_settings[MAX_BUSES];

	/* Device LUN queues. */
	struct scsi_lu *dev[MAX_EQ];	/* Logical unit queues */

	/* bottom half run queue */
	struct work_struct run_qla_bh;

	/* Received ISP mailbox data. */
	volatile uint16_t mailbox_out[MAILBOX_REGISTER_COUNT];

#ifdef UNUSED
	struct timer_list dev_timer[MAX_TARGETS];
#endif

	dma_addr_t request_dma;		/* Physical Address */
	request_t *request_ring;	/* Base virtual address */
	request_t *request_ring_ptr;	/* Current address. */
	uint16_t req_ring_index;	/* Current index. */
	uint16_t req_q_cnt;		/* Number of available entries. */

	dma_addr_t response_dma;	/* Physical address. */
	response_t *response_ring;	/* Base virtual address */
	response_t *response_ring_ptr;	/* Current address. */
	uint16_t rsp_ring_index;	/* Current index. */

#if QL1280_TARGET_MODE_SUPPORT
	/* Target buffer and sense data. */
	dma_addr_t tbuf_dma;	/* Physical address. */
	dma_addr_t tsense_dma;	/* Physical address. */
	tgt_t *tbuf;
	uint8_t *tsense;
#endif

#if WATCHDOGTIMER
	/* Watchdog queue, lock and total timer */
	uint8_t watchdog_q_lock;	/* Lock for watchdog queue */
	srb_t *wdg_q_first;	/* First job on watchdog queue */
	srb_t *wdg_q_last;	/* Last job on watchdog queue */
	uint32_t total_timeout;	/* Total timeout (quantum count) */
	uint32_t watchdogactive;
#endif

	srb_t *done_q_first;	/* First job on done queue */
	srb_t *done_q_last;	/* Last job on done queue */

	volatile struct {
		uint32_t mbox_int:1;	/* 0 */
		uint32_t mbox_busy:1;	/* 1 */
		uint32_t online:1;	/* 2 */
		uint32_t reset_marker:1;	/* 3 */
		uint32_t isp_abort_needed:1;	/* 4 */
		uint32_t disable_host_adapter:1;	/* 5 */
		uint32_t reset_active:1;	/* 6 */
		uint32_t abort_isp_active:1;	/* 7 */
		uint32_t disable_risc_code_load:1;	/* 8 */
		uint32_t enable_64bit_addressing:1;	/* 9 */
		uint32_t in_reset:1;	/* 10 */
		uint32_t ints_enabled:1;
	} flags;

	/* needed holders for PCI ordered list of hosts */
	unsigned long io_port;
	uint32_t irq;

};

/*
 * Macros to help code, maintain, etc.
 */
#define SUBDEV(b, t, l)		((b << (MAX_T_BITS + MAX_L_BITS)) | (t << MAX_L_BITS) | l)
#define LU_Q(ha, b, t, l)	(ha->dev[SUBDEV(b, t, l)])

#endif				/* HOSTS_C */

/*
 *  Linux - SCSI Driver Interface Function Prototypes.
 */
const char *qla1280_info(struct Scsi_Host *host);
int qla1280_detect(Scsi_Host_Template *);
int qla1280_release(struct Scsi_Host *);
int qla1280_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int qla1280_abort(Scsi_Cmnd *);
int qla1280_reset(Scsi_Cmnd *, unsigned int);
int qla1280_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
static int qla1280_slave_configure(Scsi_Device *);
irqreturn_t qla1280_intr_handler(int, void *, struct pt_regs *);
void qla1280_setup(char *s, int *dummy);

#endif				/* _IO_HBA_QLA1280_H */
