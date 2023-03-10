/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef __FreeBSD__
#include <dev/sym/sym_glue.h>
#else
#include "sym_glue.h"
#endif

/*
 *  Macros used for all firmwares.
 */
#define	SYM_GEN_A(s, label)	((short) offsetof(s, label)),
#define	SYM_GEN_B(s, label)	((short) offsetof(s, label)),
#define	SYM_GEN_Z(s, label)	((short) offsetof(s, label)),
#define	PADDR_A(label)		SYM_GEN_PADDR_A(struct SYM_FWA_SCR, label)
#define	PADDR_B(label)		SYM_GEN_PADDR_B(struct SYM_FWB_SCR, label)


#if	SYM_CONF_GENERIC_SUPPORT
/*
 *  Allocate firmware #1 script area.
 */
#define	SYM_FWA_SCR		sym_fw1a_scr
#define	SYM_FWB_SCR		sym_fw1b_scr
#define	SYM_FWZ_SCR		sym_fw1z_scr
#ifdef __FreeBSD__
#include <dev/sym/sym_fw1.h>
#else
#include "sym_fw1.h"
#endif
static struct sym_fwa_ofs sym_fw1a_ofs = {
	SYM_GEN_FW_A(struct SYM_FWA_SCR)
};
static struct sym_fwb_ofs sym_fw1b_ofs = {
	SYM_GEN_FW_B(struct SYM_FWB_SCR)
#ifdef SYM_OPT_HANDLE_DIR_UNKNOWN
	SYM_GEN_B(struct SYM_FWB_SCR, data_io)
#endif
};
static struct sym_fwz_ofs sym_fw1z_ofs = {
	SYM_GEN_FW_Z(struct SYM_FWZ_SCR)
#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	SYM_GEN_Z(struct SYM_FWZ_SCR, start_ram)
#endif
};
#undef	SYM_FWA_SCR
#undef	SYM_FWB_SCR
#undef	SYM_FWZ_SCR
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Allocate firmware #2 script area.
 */
#define	SYM_FWA_SCR		sym_fw2a_scr
#define	SYM_FWB_SCR		sym_fw2b_scr
#define	SYM_FWZ_SCR		sym_fw2z_scr
#ifdef __FreeBSD__
#include <dev/sym/sym_fw2.h>
#else
#include "sym_fw2.h"
#endif
static struct sym_fwa_ofs sym_fw2a_ofs = {
	SYM_GEN_FW_A(struct SYM_FWA_SCR)
};
static struct sym_fwb_ofs sym_fw2b_ofs = {
	SYM_GEN_FW_B(struct SYM_FWB_SCR)
#ifdef SYM_OPT_HANDLE_DIR_UNKNOWN
	SYM_GEN_B(struct SYM_FWB_SCR, data_io)
#endif
	SYM_GEN_B(struct SYM_FWB_SCR, start64)
	SYM_GEN_B(struct SYM_FWB_SCR, pm_handle)
};
static struct sym_fwz_ofs sym_fw2z_ofs = {
	SYM_GEN_FW_Z(struct SYM_FWZ_SCR)
#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	SYM_GEN_Z(struct SYM_FWZ_SCR, start_ram)
	SYM_GEN_Z(struct SYM_FWZ_SCR, start_ram64)
#endif
};
#undef	SYM_FWA_SCR
#undef	SYM_FWB_SCR
#undef	SYM_FWZ_SCR

#undef	SYM_GEN_A
#undef	SYM_GEN_B
#undef	SYM_GEN_Z
#undef	PADDR_A
#undef	PADDR_B

#if	SYM_CONF_GENERIC_SUPPORT
/*
 *  Patch routine for firmware #1.
 */
static void
sym_fw1_patch(hcb_p np)
{
	struct sym_fw1a_scr *scripta0;
	struct sym_fw1b_scr *scriptb0;
#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	struct sym_fw1z_scr *scriptz0 = 
		(struct sym_fw1z_scr *) np->scriptz0;
#endif

	scripta0 = (struct sym_fw1a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw1b_scr *) np->scriptb0;

#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	/*
	 *  Set up BUS physical address of SCRIPTS that is to 
	 *  be copied to on-chip RAM by the SCRIPTS processor.
	 */
	scriptz0->scripta0_ba[0]	= cpu_to_scr(vtobus(scripta0));
#endif

	/*
	 *  Remove LED support if not needed.
	 */
	if (!(np->features & FE_LED0)) {
		scripta0->idle[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->reselected[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->start[0]	= cpu_to_scr(SCR_NO_OP);
	}

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *    If user does not want to use IMMEDIATE ARBITRATION
	 *    when we are reselected while attempting to arbitrate,
	 *    patch the SCRIPTS accordingly with a SCRIPT NO_OP.
	 */
	if (!SYM_CONF_SET_IARB_ON_ARB_LOST)
		scripta0->ungetjob[0] = cpu_to_scr(SCR_NO_OP);
#endif
	/*
	 *  Patch some data in SCRIPTS.
	 *  - start and done queue initial bus address.
	 *  - target bus address table bus address.
	 */
	scriptb0->startpos[0]	= cpu_to_scr(np->squeue_ba);
	scriptb0->done_pos[0]	= cpu_to_scr(np->dqueue_ba);
	scriptb0->targtbl[0]	= cpu_to_scr(np->targtbl_ba);
}
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Patch routine for firmware #2.
 */
static void
sym_fw2_patch(hcb_p np)
{
	struct sym_fw2a_scr *scripta0;
	struct sym_fw2b_scr *scriptb0;
#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	struct sym_fw2z_scr *scriptz0 = 
		(struct sym_fw2z_scr *) np->scriptz0;
#endif

	scripta0 = (struct sym_fw2a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw2b_scr *) np->scriptb0;

#ifdef SYM_OPT_NO_BUS_MEMORY_MAPPING
	/*
	 *  Set up BUS physical address of SCRIPTS that is to 
	 *  be copied to on-chip RAM by the SCRIPTS processor.
	 */
	scriptz0->scripta0_ba64[0]	= /* Nothing is missing here */
	scriptz0->scripta0_ba[0]	= cpu_to_scr(vtobus(scripta0));
	scriptz0->scriptb0_ba64[0]	= cpu_to_scr(vtobus(scriptb0));
	scriptz0->ram_seg64[0]		= np->scr_ram_seg;
#endif

	/*
	 *  Remove LED support if not needed.
	 */
	if (!(np->features & FE_LED0)) {
		scripta0->idle[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->reselected[0]	= cpu_to_scr(SCR_NO_OP);
		scripta0->start[0]	= cpu_to_scr(SCR_NO_OP);
	}

#if   SYM_CONF_DMA_ADDRESSING_MODE == 2
	/*
	 *  Remove useless 64 bit DMA specific SCRIPTS, 
	 *  when this feature is not available.
	 */
	if (!np->use_dac) {
		scripta0->is_dmap_dirty[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->is_dmap_dirty[1] = 0;
		scripta0->is_dmap_dirty[2] = cpu_to_scr(SCR_NO_OP);
		scripta0->is_dmap_dirty[3] = 0;
	}
#endif

#ifdef SYM_CONF_IARB_SUPPORT
	/*
	 *    If user does not want to use IMMEDIATE ARBITRATION
	 *    when we are reselected while attempting to arbitrate,
	 *    patch the SCRIPTS accordingly with a SCRIPT NO_OP.
	 */
	if (!SYM_CONF_SET_IARB_ON_ARB_LOST)
		scripta0->ungetjob[0] = cpu_to_scr(SCR_NO_OP);
#endif
	/*
	 *  Patch some variable in SCRIPTS.
	 *  - start and done queue initial bus address.
	 *  - target bus address table bus address.
	 */
	scriptb0->startpos[0]	= cpu_to_scr(np->squeue_ba);
	scriptb0->done_pos[0]	= cpu_to_scr(np->dqueue_ba);
	scriptb0->targtbl[0]	= cpu_to_scr(np->targtbl_ba);

	/*
	 *  Remove the load of SCNTL4 on reselection if not a C10.
	 */
	if (!(np->features & FE_C10)) {
		scripta0->resel_scntl4[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->resel_scntl4[1] = cpu_to_scr(0);
	}

	/*
	 *  Remove a couple of work-arounds specific to C1010 if 
	 *  they are not desirable. See `sym_fw2.h' for more details.
	 */
	if (!(np->device_id == PCI_ID_LSI53C1010_2 &&
	      np->revision_id < 0x1 &&
	      np->pciclk_khz < 60000)) {
		scripta0->datao_phase[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->datao_phase[1] = cpu_to_scr(0);
	}
	if (!(np->device_id == PCI_ID_LSI53C1010 &&
	      /* np->revision_id < 0xff */ 1)) {
		scripta0->sel_done[0] = cpu_to_scr(SCR_NO_OP);
		scripta0->sel_done[1] = cpu_to_scr(0);
	}

	/*
	 *  Patch some other variables in SCRIPTS.
	 *  These ones are loaded by the SCRIPTS processor.
	 */
	scriptb0->pm0_data_addr[0] =
		cpu_to_scr(np->scripta_ba + 
			   offsetof(struct sym_fw2a_scr, pm0_data));
	scriptb0->pm1_data_addr[0] =
		cpu_to_scr(np->scripta_ba + 
			   offsetof(struct sym_fw2a_scr, pm1_data));
}

/*
 *  Fill the data area in scripts.
 *  To be done for all firmwares.
 */
static void
sym_fw_fill_data (u32 *in, u32 *out)
{
	int	i;

	for (i = 0; i < SYM_CONF_MAX_SG; i++) {
		*in++  = SCR_CHMOV_TBL ^ SCR_DATA_IN;
		*in++  = offsetof (struct sym_dsb, data[i]);
		*out++ = SCR_CHMOV_TBL ^ SCR_DATA_OUT;
		*out++ = offsetof (struct sym_dsb, data[i]);
	}
}

/*
 *  Setup useful script bus addresses.
 *  To be done for all firmwares.
 */
static void 
sym_fw_setup_bus_addresses(hcb_p np, struct sym_fw *fw)
{
	u32 *pa;
	u_short *po;
	int i;

	/*
	 *  Build the bus address table for script A 
	 *  from the script A offset table.
	 */
	po = (u_short *) fw->a_ofs;
	pa = (u32 *) &np->fwa_bas;
	for (i = 0 ; i < sizeof(np->fwa_bas)/sizeof(u32) ; i++)
		pa[i] = np->scripta_ba + po[i];

	/*
	 *  Same for script B.
	 */
	po = (u_short *) fw->b_ofs;
	pa = (u32 *) &np->fwb_bas;
	for (i = 0 ; i < sizeof(np->fwb_bas)/sizeof(u32) ; i++)
		pa[i] = np->scriptb_ba + po[i];

	/*
	 *  Same for script Z.
	 */
	po = (u_short *) fw->z_ofs;
	pa = (u32 *) &np->fwz_bas;
	for (i = 0 ; i < sizeof(np->fwz_bas)/sizeof(u32) ; i++)
		pa[i] = np->scriptz_ba + po[i];
}

#if	SYM_CONF_GENERIC_SUPPORT
/*
 *  Setup routine for firmware #1.
 */
static void 
sym_fw1_setup(hcb_p np, struct sym_fw *fw)
{
	struct sym_fw1a_scr *scripta0;
	struct sym_fw1b_scr *scriptb0;

	scripta0 = (struct sym_fw1a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw1b_scr *) np->scriptb0;

	/*
	 *  Fill variable parts in scripts.
	 */
	sym_fw_fill_data(scripta0->data_in, scripta0->data_out);

	/*
	 *  Setup bus addresses used from the C code..
	 */
	sym_fw_setup_bus_addresses(np, fw);
}
#endif	/* SYM_CONF_GENERIC_SUPPORT */

/*
 *  Setup routine for firmware #2.
 */
static void 
sym_fw2_setup(hcb_p np, struct sym_fw *fw)
{
	struct sym_fw2a_scr *scripta0;
	struct sym_fw2b_scr *scriptb0;

	scripta0 = (struct sym_fw2a_scr *) np->scripta0;
	scriptb0 = (struct sym_fw2b_scr *) np->scriptb0;

	/*
	 *  Fill variable parts in scripts.
	 */
	sym_fw_fill_data(scripta0->data_in, scripta0->data_out);

	/*
	 *  Setup bus addresses used from the C code..
	 */
	sym_fw_setup_bus_addresses(np, fw);
}

/*
 *  Allocate firmware descriptors.
 */
#if	SYM_CONF_GENERIC_SUPPORT
static struct sym_fw sym_fw1 = SYM_FW_ENTRY(sym_fw1, "NCR-generic");
#endif	/* SYM_CONF_GENERIC_SUPPORT */
static struct sym_fw sym_fw2 = SYM_FW_ENTRY(sym_fw2, "LOAD/STORE-based");

/*
 *  Find the most appropriate firmware for a chip.
 */
struct sym_fw * 
sym_find_firmware(struct sym_pci_chip *chip)
{
	if (chip->features & FE_LDSTR)
		return &sym_fw2;
#if	SYM_CONF_GENERIC_SUPPORT
	else if (!(chip->features & (FE_PFEN|FE_NOPM|FE_DAC)))
		return &sym_fw1;
#endif
	else
		return 0;
}

/*
 *  Bind a script to physical addresses.
 */
void sym_fw_bind_script (hcb_p np, u32 *start, int len)
{
	u32 opcode, new, old, tmp1, tmp2;
	u32 *end, *cur;
	int relocs;

	cur = start;
	end = start + len/4;

	while (cur < end) {

		opcode = *cur;

		/*
		 *  If we forget to change the length
		 *  in scripts, a field will be
		 *  padded with 0. This is an illegal
		 *  command.
		 */
		if (opcode == 0) {
			printf ("%s: ERROR0 IN SCRIPT at %d.\n",
				sym_name(np), (int) (cur-start));
			MDELAY (10000);
			++cur;
			continue;
		};

		/*
		 *  We use the bogus value 0xf00ff00f ;-)
		 *  to reserve data area in SCRIPTS.
		 */
		if (opcode == SCR_DATA_ZERO) {
			*cur++ = 0;
			continue;
		}

		if (DEBUG_FLAGS & DEBUG_SCRIPT)
			printf ("%d:  <%x>\n", (int) (cur-start),
				(unsigned)opcode);

		/*
		 *  We don't have to decode ALL commands
		 */
		switch (opcode >> 28) {
		case 0xf:
			/*
			 *  LOAD / STORE DSA relative, don't relocate.
			 */
			relocs = 0;
			break;
		case 0xe:
			/*
			 *  LOAD / STORE absolute.
			 */
			relocs = 1;
			break;
		case 0xc:
			/*
			 *  COPY has TWO arguments.
			 */
			relocs = 2;
			tmp1 = cur[1];
			tmp2 = cur[2];
			if ((tmp1 ^ tmp2) & 3) {
				printf ("%s: ERROR1 IN SCRIPT at %d.\n",
					sym_name(np), (int) (cur-start));
				MDELAY (10000);
			}
			/*
			 *  If PREFETCH feature not enabled, remove 
			 *  the NO FLUSH bit if present.
			 */
			if ((opcode & SCR_NO_FLUSH) &&
			    !(np->features & FE_PFEN)) {
				opcode = (opcode & ~SCR_NO_FLUSH);
			}
			break;
		case 0x0:
			/*
			 *  MOVE/CHMOV (absolute address)
			 */
			if (!(np->features & FE_WIDE))
				opcode = (opcode | OPC_MOVE);
			relocs = 1;
			break;
		case 0x1:
			/*
			 *  MOVE/CHMOV (table indirect)
			 */
			if (!(np->features & FE_WIDE))
				opcode = (opcode | OPC_MOVE);
			relocs = 0;
			break;
#ifdef SYM_CONF_TARGET_ROLE_SUPPORT
		case 0x2:
			/*
			 *  MOVE/CHMOV in target role (absolute address)
			 */
			opcode &= ~0x20000000;
			if (!(np->features & FE_WIDE))
				opcode = (opcode & ~OPC_TCHMOVE);
			relocs = 1;
			break;
		case 0x3:
			/*
			 *  MOVE/CHMOV in target role (table indirect)
			 */
			opcode &= ~0x20000000;
			if (!(np->features & FE_WIDE))
				opcode = (opcode & ~OPC_TCHMOVE);
			relocs = 0;
			break;
#endif
		case 0x8:
			/*
			 *  JUMP / CALL
			 *  don't relocate if relative :-)
			 */
			if (opcode & 0x00800000)
				relocs = 0;
			else if ((opcode & 0xf8400000) == 0x80400000)/*JUMP64*/
				relocs = 2;
			else
				relocs = 1;
			break;
		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
			relocs = 1;
			break;
		default:
			relocs = 0;
			break;
		};

		/*
		 *  Scriptify:) the opcode.
		 */
		*cur++ = cpu_to_scr(opcode);

		/*
		 *  If no relocation, assume 1 argument 
		 *  and just scriptize:) it.
		 */
		if (!relocs) {
			*cur = cpu_to_scr(*cur);
			++cur;
			continue;
		}

		/*
		 *  Otherwise performs all needed relocations.
		 */
		while (relocs--) {
			old = *cur;

			switch (old & RELOC_MASK) {
			case RELOC_REGISTER:
				new = (old & ~RELOC_MASK) + np->mmio_ba;
				break;
			case RELOC_LABEL_A:
				new = (old & ~RELOC_MASK) + np->scripta_ba;
				break;
			case RELOC_LABEL_B:
				new = (old & ~RELOC_MASK) + np->scriptb_ba;
				break;
			case RELOC_SOFTC:
				new = (old & ~RELOC_MASK) + np->hcb_ba;
				break;
			case 0:
				/*
				 *  Don't relocate a 0 address.
				 *  They are mostly used for patched or 
				 *  script self-modified areas.
				 */
				if (old == 0) {
					new = old;
					break;
				}
				/* fall through */
			default:
				new = 0;
				panic("sym_fw_bind_script: "
				      "weird relocation %x\n", old);
				break;
			}

			*cur++ = cpu_to_scr(new);
		}
	};
}
