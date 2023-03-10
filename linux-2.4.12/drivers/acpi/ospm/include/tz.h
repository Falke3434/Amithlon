/*****************************************************************************
 *
 * Module Name: tz.h
 *   $Revision: 21 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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

#ifndef __TZ_H__
#define __TZ_H__

/* TBD: Linux-specific */
#include <linux/module.h>
#include <linux/timer.h>

#include <bm.h>
#include <pr.h>


/*****************************************************************************
 *                             Types & Other Defines
 *****************************************************************************/

#define TZ_MAX_THRESHOLDS	12	/* _AC0 through _AC9 + _CRT + _PSV */
#define TZ_MAX_ACTIVE_THRESHOLDS 10	/* _AC0 through _AC9 */
#define TZ_MAX_COOLING_DEVICES	10	/* TBD: Make size dynamic */


/*
 * Notifications:
 * --------------
 */
#define TZ_NOTIFY_TEMPERATURE_CHANGE ((BM_NOTIFY) 0x80)
#define TZ_NOTIFY_THRESHOLD_CHANGE ((BM_NOTIFY) 0x81)
#define TZ_NOTIFY_DEVICE_LISTS_CHANGE ((BM_NOTIFY) 0x82)


/*
 * TZ_THRESHOLD_TYPE:
 * ------------------
 */
typedef u32			TZ_THRESHOLD_TYPE;

#define TZ_THRESHOLD_UNKNOWN	((TZ_THRESHOLD_TYPE) 0x00)
#define TZ_THRESHOLD_CRITICAL	((TZ_THRESHOLD_TYPE) 0x01)

#define TZ_THRESHOLD_PASSIVE	((TZ_THRESHOLD_TYPE) 0x02)
#define TZ_THRESHOLD_ACTIVE	((TZ_THRESHOLD_TYPE) 0x03)


/*
 * TZ_COOLING_STATE:
 * -----------------
 */
typedef u32			TZ_COOLING_STATE;

#define TZ_COOLING_UNKNOWN	((TZ_COOLING_STATE) 0x00)
#define TZ_COOLING_ENABLED	((TZ_COOLING_STATE) 0x01)
#define TZ_COOLING_DISABLED	((TZ_COOLING_STATE) 0x02)


/*
 * TZ_COOLING_MODE:
 * ----------------
 */
typedef u32			TZ_COOLING_MODE;

#define TZ_COOLING_MODE_ACTIVE	((TZ_COOLING_MODE) 0x00)
#define TZ_COOLING_MODE_PASSIVE	((TZ_COOLING_MODE) 0x01)


/*
 * Thermal State:
 * --------------
 * The encoding of TZ_STATE is illustrated below.
 * Note that a set bit (1) indicates the property is TRUE
 * (e.g. if bit 0 is set then the device has dynamic status).
 * No bits set indicates an OK cooling state.
 * +--+--+--+-----------+----------+
 * |31|30|29| Bits 27:4 | Bits 3:0 |
 * +--+--+--+-----------+----------+
 *  |  |  |       |          |
 *  |  |  |       |          +------ Active Index
 *  |  |  |       +----------------- <reserved>
 *  |  |  +------------------------- Active
 *  |  +---------------------------- Passive
 *  +------------------------------- Critical
 *
 * Active Index:    Value representing the level of active cooling
 *                  presently applied (e.g. 0=_AL0, 9=_AL9).  Only
 *                  valid when 'Active' is set.
 * Active:          If set, indicates that the system temperature
 *                  has crossed at least one active threshold (_ALx).
 * Passive:         If set, indicates that the system temperature
 *                  has crossed the passive threshold (_PSL).
 * Passive:         If set, indicates that the system temperature
 *                  has crossed the critical threshold (_CRT).
 */
typedef u32			TZ_STATE;

#define TZ_STATE_OK		((TZ_STATE) 0x00000000)
#define TZ_STATE_ACTIVE		((TZ_STATE) 0x20000000)
#define TZ_STATE_PASSIVE	((TZ_STATE) 0x40000000)
#define TZ_STATE_CRITICAL	((TZ_STATE) 0x80000000)


/*
 * TZ_THRESHOLD:
 * -------------
 * Information on an individual threshold.
 */
typedef struct {
	TZ_THRESHOLD_TYPE	type;
	u32			index;
	u32			temperature;
	TZ_COOLING_STATE	cooling_state;
	BM_HANDLE_LIST		cooling_devices;
} TZ_THRESHOLD;


/*
 * TZ_THRESHOLD_LIST:
 * ------------------
 * Container for the thresholds of a given thermal zone.
 * Note that thresholds are always ordered by increasing
 * temperature value to simplify use by thermal policy.
 */
typedef struct {
	u32			count;
	TZ_THRESHOLD		thresholds[TZ_MAX_THRESHOLDS];
} TZ_THRESHOLD_LIST;


/*
 * TZ_CRITICAL_POLICY:
 * -------------------
 */
typedef struct {
	TZ_THRESHOLD		*threshold;
} TZ_CRITICAL_POLICY;


/*
 * TZ_PASSIVE_POLICY:
 * ------------------
 */
typedef struct {
	u32			tc1;
	u32			tc2;
	u32			tsp;
	TZ_THRESHOLD		*threshold;
} TZ_PASSIVE_POLICY;


/*
 * TZ_ACTIVE_POLICY:
 * -----------------
 */
typedef struct {
	u32			threshold_count;
	TZ_THRESHOLD		*threshold[TZ_MAX_ACTIVE_THRESHOLDS];
} TZ_ACTIVE_POLICY;


/*
 * TZ_POLICY:
 * ---------
 */
typedef struct {
	u32			temperature;
	TZ_STATE		state;
	TZ_COOLING_MODE		cooling_mode;
	u32			polling_freq;
	TZ_THRESHOLD_LIST	threshold_list;
	TZ_CRITICAL_POLICY	critical;
	TZ_PASSIVE_POLICY	passive;
	TZ_ACTIVE_POLICY	active;
	/* TBD: Linux-specific */
	struct timer_list	timer;
} TZ_POLICY;


/*
 * TZ_CONTEXT:
 * -----------
 */
typedef struct {
	BM_HANDLE		device_handle;
	acpi_handle		acpi_handle;
	char			uid[9];
	TZ_POLICY		policy;
} TZ_CONTEXT;


/*****************************************************************************
 *                             Function Prototypes
 *****************************************************************************/

/* thermal_zone.c */

acpi_status
tz_initialize (void);

acpi_status
tz_terminate (void);

acpi_status
tz_notify (
	BM_NOTIFY               notify_type,
	BM_HANDLE               device_handle,
	BM_DRIVER_CONTEXT	*context);

acpi_status
tz_request (
	BM_REQUEST              *request,
	BM_DRIVER_CONTEXT	context);

acpi_status
tz_get_temperature (
	TZ_CONTEXT              *thermal_zone,
	u32                     *temperature);

acpi_status
tz_get_thresholds (
	TZ_CONTEXT              *thermal_zone,
	TZ_THRESHOLD_LIST       *threshold_list);

void
tz_print (
	TZ_CONTEXT              *thermal_zone);

/* tzpolicy.c */

acpi_status
tz_policy_add_device (
	TZ_CONTEXT              *thermal_zone);

acpi_status
tz_policy_remove_device (
	TZ_CONTEXT              *thermal_zone);

void
tz_policy_check (
	void                    *context);

/* Thermal Zone Driver OSL */

acpi_status
tz_osl_add_device (
	TZ_CONTEXT		*thermal_zone);

acpi_status
tz_osl_remove_device (
	TZ_CONTEXT		*thermal_zone);

acpi_status
tz_osl_generate_event (
	u32			event,
	TZ_CONTEXT		*thermal_zone);


#endif  /* __TZ_H__ */
