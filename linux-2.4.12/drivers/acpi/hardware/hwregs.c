
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *              $Revision: 109 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "acpi.h"
#include "achware.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_HARDWARE
	 MODULE_NAME         ("hwregs")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_bit_shift
 *
 * PARAMETERS:  Mask            - Input mask to determine bit shift from.
 *                                Must have at least 1 bit set.
 *
 * RETURN:      Bit location of the lsb of the mask
 *
 * DESCRIPTION: Returns the bit number for the low order bit that's set.
 *
 ******************************************************************************/

u32
acpi_hw_get_bit_shift (
	u32                     mask)
{
	u32                     shift;


	FUNCTION_TRACE ("Hw_get_bit_shift");


	for (shift = 0; ((mask >> shift) & 1) == 0; shift++) { ; }

	return_VALUE (shift);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *
 ******************************************************************************/

void
acpi_hw_clear_acpi_status (void)
{
	u16                     gpe_length;
	u16                     index;


	FUNCTION_TRACE ("Hw_clear_acpi_status");


	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %04X\n",
		ALL_FIXED_STS_BITS,
		(u16) ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm1a_evt_blk.address)));


	acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);

	acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, PM1_STS, ALL_FIXED_STS_BITS);


	if (ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm1b_evt_blk.address)) {
		acpi_os_write_port ((ACPI_IO_ADDRESS)
			ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm1b_evt_blk.address),
			ALL_FIXED_STS_BITS, 16);
	}

	/* now clear the GPE Bits */

	if (acpi_gbl_FADT->gpe0blk_len) {
		gpe_length = (u16) DIV_2 (acpi_gbl_FADT->gpe0blk_len);

		for (index = 0; index < gpe_length; index++) {
			acpi_os_write_port ((ACPI_IO_ADDRESS) (
				ACPI_GET_ADDRESS (acpi_gbl_FADT->Xgpe0blk.address) + index),
					0xFF, 8);
		}
	}

	if (acpi_gbl_FADT->gpe1_blk_len) {
		gpe_length = (u16) DIV_2 (acpi_gbl_FADT->gpe1_blk_len);

		for (index = 0; index < gpe_length; index++) {
			acpi_os_write_port ((ACPI_IO_ADDRESS) (
				ACPI_GET_ADDRESS (acpi_gbl_FADT->Xgpe1_blk.address) + index),
				0xFF, 8);
		}
	}

	acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_obtain_sleep_type_register_data
 *
 * PARAMETERS:  Sleep_state       - Numeric state requested
 *              *Slp_Typ_a         - Pointer to byte to receive SLP_TYPa value
 *              *Slp_Typ_b         - Pointer to byte to receive SLP_TYPb value
 *
 * RETURN:      Status - ACPI status
 *
 * DESCRIPTION: Acpi_hw_obtain_sleep_type_register_data() obtains the SLP_TYP and
 *              SLP_TYPb values for the sleep state requested.
 *
 ******************************************************************************/

acpi_status
acpi_hw_obtain_sleep_type_register_data (
	u8                      sleep_state,
	u8                      *slp_typ_a,
	u8                      *slp_typ_b)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE ("Hw_obtain_sleep_type_register_data");


	/*
	 *  Validate parameters
	 */
	if ((sleep_state > ACPI_S_STATES_MAX) ||
		!slp_typ_a || !slp_typ_b) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 *  Acpi_evaluate the namespace object containing the values for this state
	 */
	status = acpi_ns_evaluate_by_name ((NATIVE_CHAR *) acpi_gbl_db_sleep_states[sleep_state],
			  NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (!obj_desc) {
		REPORT_ERROR (("Missing Sleep State object\n"));
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/*
	 *  We got something, now ensure it is correct.  The object must
	 *  be a package and must have at least 2 numeric values as the
	 *  two elements
	 */

	/* Even though Acpi_evaluate_object resolves package references,
	 * Ns_evaluate dpesn't. So, we do it here.
	 */
	status = acpi_ut_resolve_package_references(obj_desc);

	if (obj_desc->package.count < 2) {
		/* Must have at least two elements */

		REPORT_ERROR (("Sleep State package does not have at least two elements\n"));
		status = AE_ERROR;
	}

	else if (((obj_desc->package.elements[0])->common.type !=
			 ACPI_TYPE_INTEGER) ||
			 ((obj_desc->package.elements[1])->common.type !=
				ACPI_TYPE_INTEGER)) {
		/* Must have two  */

		REPORT_ERROR (("Sleep State package elements are not both of type Number\n"));
		status = AE_ERROR;
	}

	else {
		/*
		 *  Valid _Sx_ package size, type, and value
		 */
		*slp_typ_a = (u8) (obj_desc->package.elements[0])->integer.value;

		*slp_typ_b = (u8) (obj_desc->package.elements[1])->integer.value;
	}


	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Bad Sleep object %p type %X\n",
			obj_desc, obj_desc->common.type));
	}

	acpi_ut_remove_reference (obj_desc);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_bit_access
 *
 * PARAMETERS:  Read_write      - Either ACPI_READ or ACPI_WRITE.
 *              Use_lock        - Lock the hardware
 *              Register_id     - index of ACPI Register to access
 *              Value           - (only used on write) value to write to the
 *                                Register.  Shifted all the way right.
 *
 * RETURN:      Value written to or read from specified Register.  This value
 *              is shifted all the way right.
 *
 * DESCRIPTION: Generic ACPI Register read/write function.
 *
 ******************************************************************************/

u32
acpi_hw_register_bit_access (
	NATIVE_UINT             read_write,
	u8                      use_lock,
	u32                     register_id,
	...)                    /* Value (only used on write) */
{
	u32                     register_value = 0;
	u32                     mask = 0;
	u32                     value = 0;
	va_list                 marker;


	FUNCTION_TRACE ("Hw_register_bit_access");


	if (read_write == ACPI_WRITE) {
		va_start (marker, register_id);
		value = va_arg (marker, u32);
		va_end (marker);
	}

	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
	}

	/*
	 * Decode the Register ID
	 * Register id = Register block id | bit id
	 *
	 * Check bit id to fine locate Register offset.
	 * Check Mask to determine Register offset, and then read-write.
	 */
	switch (REGISTER_BLOCK_ID (register_id)) {
	case PM1_STS:

		switch (register_id) {
		case TMR_STS:
			mask = TMR_STS_MASK;
			break;

		case BM_STS:
			mask = BM_STS_MASK;
			break;

		case GBL_STS:
			mask = GBL_STS_MASK;
			break;

		case PWRBTN_STS:
			mask = PWRBTN_STS_MASK;
			break;

		case SLPBTN_STS:
			mask = SLPBTN_STS_MASK;
			break;

		case RTC_STS:
			mask = RTC_STS_MASK;
			break;

		case WAK_STS:
			mask = WAK_STS_MASK;
			break;

		default:
			mask = 0;
			break;
		}

		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM1_STS);

		if (read_write == ACPI_WRITE) {
			/*
			 * Status Registers are different from the rest.  Clear by
			 * writing 1, writing 0 has no effect.  So, the only relevent
			 * information is the single bit we're interested in, all
			 * others should be written as 0 so they will be left
			 * unchanged
			 */
			value <<= acpi_hw_get_bit_shift (mask);
			value &= mask;

			if (value) {
				acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, PM1_STS,
					(u16) value);
				register_value = 0;
			}
		}

		break;


	case PM1_EN:

		switch (register_id) {
		case TMR_EN:
			mask = TMR_EN_MASK;
			break;

		case GBL_EN:
			mask = GBL_EN_MASK;
			break;

		case PWRBTN_EN:
			mask = PWRBTN_EN_MASK;
			break;

		case SLPBTN_EN:
			mask = SLPBTN_EN_MASK;
			break;

		case RTC_EN:
			mask = RTC_EN_MASK;
			break;

		default:
			mask = 0;
			break;
		}

		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM1_EN);

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, PM1_EN, (u16) register_value);
		}

		break;


	case PM1_CONTROL:

		switch (register_id) {
		case SCI_EN:
			mask = SCI_EN_MASK;
			break;

		case BM_RLD:
			mask = BM_RLD_MASK;
			break;

		case GBL_RLS:
			mask = GBL_RLS_MASK;
			break;

		case SLP_TYPE_A:
		case SLP_TYPE_B:
			mask = SLP_TYPE_X_MASK;
			break;

		case SLP_EN:
			mask = SLP_EN_MASK;
			break;

		default:
			mask = 0;
			break;
		}


		/*
		 * Read the PM1 Control register.
		 * Note that at this level, the fact that there are actually TWO
		 * registers (A and B) and that B may not exist, are abstracted.
		 */
		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM1_CONTROL);

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM1 control: Read %X\n", register_value));

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			/*
			 * SLP_TYPE_x Registers are written differently
			 * than any other control Registers with
			 * respect to A and B Registers.  The value
			 * for A may be different than the value for B
			 *
			 * Therefore, pass the Register_id, not just generic PM1_CONTROL,
			 * because we need to do different things. Yuck.
			 */
			acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, register_id,
					(u16) register_value);
		}
		break;


	case PM2_CONTROL:

		switch (register_id) {
		case ARB_DIS:
			mask = ARB_DIS_MASK;
			break;

		default:
			mask = 0;
			break;
		}

		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM2_CONTROL);

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM2 control: Read %X from %p\n",
			register_value, ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm2_cnt_blk.address)));

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %p\n", register_value,
				acpi_gbl_FADT->Xpm2_cnt_blk.address));

			acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK,
					   PM2_CONTROL, (u8) (register_value));
		}
		break;


	case PM_TIMER:

		mask = TMR_VAL_MASK;
		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK,
				 PM_TIMER);
		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM_TIMER: Read %X from %p\n",
			register_value, ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm_tmr_blk.address)));

		break;


	case GPE1_EN_BLOCK:
	case GPE1_STS_BLOCK:
	case GPE0_EN_BLOCK:
	case GPE0_STS_BLOCK:

		/* Determine the bit to be accessed
		 *
		 *  (u32) Register_id:
		 *      31      24       16       8        0
		 *      +--------+--------+--------+--------+
		 *      |  gpe_block_id   |  gpe_bit_number |
		 *      +--------+--------+--------+--------+
		 *
		 *     gpe_block_id is one of GPE[01]_EN_BLOCK and GPE[01]_STS_BLOCK
		 *     gpe_bit_number is relative from the gpe_block (0x00~0xFF)
		 */
		mask = REGISTER_BIT_ID(register_id); /* gpe_bit_number */
		register_id = REGISTER_BLOCK_ID(register_id) | (mask >> 3);
		mask = acpi_gbl_decode_to8bit [mask % 8];

		/*
		 * The base address of the GPE 0 Register Block
		 * Plus 1/2 the length of the GPE 0 Register Block
		 * The enable Register is the Register following the Status Register
		 * and each Register is defined as 1/2 of the total Register Block
		 */

		/*
		 * This sets the bit within Enable_bit that needs to be written to
		 * the Register indicated in Mask to a 1, all others are 0
		 */

		/* Now get the current Enable Bits in the selected Reg */

		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, register_id);
		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "GPE Enable bits: Read %X from %X\n",
			register_value, register_id));

		if (read_write == ACPI_WRITE) {
			register_value &= ~mask;
			value          <<= acpi_hw_get_bit_shift (mask);
			value          &= mask;
			register_value |= value;

			/*
			 * This write will put the Action state into the General Purpose
			 * Enable Register indexed by the value in Mask
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %04X\n",
				register_value, register_id));
			acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, register_id,
				(u8) register_value);
			register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK,
					   register_id);
		}
		break;


	case SMI_CMD_BLOCK:
	case PROCESSOR_BLOCK:

		/* Not used by any callers at this time - therefore, not implemented */

	default:

		mask = 0;
		break;
	}

	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}


	register_value &= mask;
	register_value >>= acpi_hw_get_bit_shift (mask);

	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "Register I/O: returning %X\n", register_value));
	return_VALUE (register_value);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_read
 *
 * PARAMETERS:  Use_lock               - Mutex hw access.
 *              Register_id            - Register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register read function.  Registers are read at the
 *              given offset.
 *
 ******************************************************************************/

u32
acpi_hw_register_read (
	u8                      use_lock,
	u32                     register_id)
{
	u32                     value = 0;
	u32                     bank_offset;


	FUNCTION_TRACE ("Hw_register_read");


	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
	}


	switch (REGISTER_BLOCK_ID(register_id)) {
	case PM1_STS: /* 16-bit access */

		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_evt_blk, 0);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_evt_blk, 0);
		break;


	case PM1_EN: /* 16-bit access*/

		bank_offset = DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_evt_blk, bank_offset);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_evt_blk, bank_offset);
		break;


	case PM1_CONTROL: /* 16-bit access */

		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case PM2_CONTROL: /* 8-bit access */

		value =  acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xpm2_cnt_blk, 0);
		break;


	case PM_TIMER: /* 32-bit access */

		value =  acpi_hw_low_level_read (32, &acpi_gbl_FADT->Xpm_tmr_blk, 0);
		break;


	/*
	 * For the GPE? Blocks, the lower word of Register_id contains the
	 * byte offset for which to read, as each part of each block may be
	 * several bytes long.
	 */
	case GPE0_STS_BLOCK: /* 8-bit access */

		bank_offset = REGISTER_BIT_ID(register_id);
		value = acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xgpe0blk, bank_offset);
		break;

	case GPE0_EN_BLOCK: /* 8-bit access */

		bank_offset = DIV_2 (acpi_gbl_FADT->gpe0blk_len) + REGISTER_BIT_ID(register_id);
		value = acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xgpe0blk, bank_offset);
		break;

	case GPE1_STS_BLOCK: /* 8-bit access */

		bank_offset = REGISTER_BIT_ID(register_id);
		value = acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xgpe1_blk, bank_offset);
		break;

	case GPE1_EN_BLOCK: /* 8-bit access */

		bank_offset = DIV_2 (acpi_gbl_FADT->gpe1_blk_len) + REGISTER_BIT_ID(register_id);
		value = acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xgpe1_blk, bank_offset);
		break;

	case SMI_CMD_BLOCK: /* 8bit */

		acpi_os_read_port (acpi_gbl_FADT->smi_cmd, &value, 8);
		break;

	default:
		/* Value will be returned as 0 */
		break;
	}


	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	return_VALUE (value);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_write
 *
 * PARAMETERS:  Use_lock               - Mutex hw access.
 *              Register_id            - Register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register Write function.  Registers are written at the
 *              given offset.
 *
 ******************************************************************************/

void
acpi_hw_register_write (
	u8                      use_lock,
	u32                     register_id,
	u32                     value)
{
	u32                     bank_offset;


	FUNCTION_TRACE ("Hw_register_write");


	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
	}


	switch (REGISTER_BLOCK_ID (register_id)) {
	case PM1_STS: /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_evt_blk, 0);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_evt_blk, 0);
		break;


	case PM1_EN: /* 16-bit access*/

		bank_offset = DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_evt_blk, bank_offset);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_evt_blk, bank_offset);
		break;


	case PM1_CONTROL: /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case PM1_a_CONTROL: /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		break;


	case PM1_b_CONTROL: /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case PM2_CONTROL: /* 8-bit access */

		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xpm2_cnt_blk, 0);
		break;


	case PM_TIMER: /* 32-bit access */

		acpi_hw_low_level_write (32, value, &acpi_gbl_FADT->Xpm_tmr_blk, 0);
		break;


	case GPE0_STS_BLOCK: /* 8-bit access */

		bank_offset = REGISTER_BIT_ID(register_id);
		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xgpe0blk, bank_offset);
		break;


	case GPE0_EN_BLOCK: /* 8-bit access */

		bank_offset = DIV_2 (acpi_gbl_FADT->gpe0blk_len) + REGISTER_BIT_ID(register_id);
		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xgpe0blk, bank_offset);
		break;


	case GPE1_STS_BLOCK: /* 8-bit access */

		bank_offset = REGISTER_BIT_ID(register_id);
		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xgpe1_blk, bank_offset);
		break;


	case GPE1_EN_BLOCK: /* 8-bit access */

		bank_offset = DIV_2 (acpi_gbl_FADT->gpe1_blk_len) + REGISTER_BIT_ID(register_id);
		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xgpe1_blk, bank_offset);
		break;


	case SMI_CMD_BLOCK: /* 8bit */

		/* For 2.0, SMI_CMD is always in IO space */
		/* TBD: what about 1.0? 0.71? */

		acpi_os_write_port (acpi_gbl_FADT->smi_cmd, value, 8);
		break;


	default:
		value = 0;
		break;
	}


	if (ACPI_MTX_LOCK == use_lock) {
		acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	return_VOID;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_low_level_read
 *
 * PARAMETERS:  Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *              Width               - 8, 16, or 32
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

u32
acpi_hw_low_level_read (
	u32                     width,
	acpi_generic_address    *reg,
	u32                     offset)
{
	u32                     value = 0;
	ACPI_PHYSICAL_ADDRESS   mem_address;
	ACPI_IO_ADDRESS         io_address;
	acpi_pci_id             pci_id;
	u16                     pci_register;


	FUNCTION_ENTRY ();


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within
	 */
	if ((!reg) ||
		(!ACPI_VALID_ADDRESS (reg->address))) {
		return 0;
	}


	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (ACPI_PHYSICAL_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_read_memory (mem_address, &value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (ACPI_IO_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_read_port (io_address, &value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (ACPI_GET_ADDRESS (reg->address));
		pci_id.function = ACPI_PCI_FUNCTION (ACPI_GET_ADDRESS (reg->address));
		pci_register   = (u16) (ACPI_PCI_REGISTER (ACPI_GET_ADDRESS (reg->address)) + offset);

		acpi_os_read_pci_configuration (&pci_id, pci_register, &value, width);
		break;
	}

	return value;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_low_level_write
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - To be written
 *              Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

void
acpi_hw_low_level_write (
	u32                     width,
	u32                     value,
	acpi_generic_address    *reg,
	u32                     offset)
{
	ACPI_PHYSICAL_ADDRESS   mem_address;
	ACPI_IO_ADDRESS         io_address;
	acpi_pci_id             pci_id;
	u16                     pci_register;


	FUNCTION_ENTRY ();


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within
	 */
	if ((!reg) ||
		(!ACPI_VALID_ADDRESS (reg->address))) {
		return;
	}


	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (ACPI_PHYSICAL_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_write_memory (mem_address, value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (ACPI_IO_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_write_port (io_address, value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (ACPI_GET_ADDRESS (reg->address));
		pci_id.function = ACPI_PCI_FUNCTION (ACPI_GET_ADDRESS (reg->address));
		pci_register   = (u16) (ACPI_PCI_REGISTER (ACPI_GET_ADDRESS (reg->address)) + offset);

		acpi_os_write_pci_configuration (&pci_id, pci_register, value, width);
		break;
	}
}
