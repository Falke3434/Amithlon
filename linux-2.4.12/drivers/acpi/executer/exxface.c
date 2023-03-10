
/******************************************************************************
 *
 * Module Name: exxface - External interpreter interfaces
 *              $Revision: 29 $
 *
 *****************************************************************************/

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
#include "acinterp.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exxface")

#if 0

/*
 * DEFINE_AML_GLOBALS is tested in amlcode.h
 * to determine whether certain global names should be "defined" or only
 * "declared" in the current compilation.  This enhances maintainability
 * by enabling a single header file to embody all knowledge of the names
 * in question.
 *
 * Exactly one module of any executable should #define DEFINE_GLOBALS
 * before #including the header files which use this convention.  The
 * names in question will be defined and initialized in that module,
 * and declared as extern in all other modules which #include those
 * header files.
 */

#define DEFINE_AML_GLOBALS
#include "amlcode.h"
#include "acparser.h"
#include "acnamesp.h"


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_execute_method
 *
 * PARAMETERS:  Pcode               - Pointer to the pcode stream
 *              Pcode_length        - Length of pcode that comprises the method
 *              **Params            - List of parameters to pass to method,
 *                                    terminated by NULL. Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a control method
 *
 ******************************************************************************/

acpi_status
acpi_ex_execute_method (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc)
{
	acpi_status             status;


	FUNCTION_TRACE ("Ex_execute_method");


	/*
	 * The point here is to lock the interpreter and call the low
	 * level execute.
	 */
	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_psx_execute (method_node, params, return_obj_desc);

	acpi_ex_exit_interpreter ();

	return_ACPI_STATUS (status);
}


#endif
