/* 
 * Sony Programmable I/O Control Device driver for VAIO
 *
 * Copyright (C) 2001 Stelian Pop <stelian.pop@fr.alcove.com>, Alc?ve
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _SONYPI_H_ 
#define _SONYPI_H_

#include <linux/types.h>

/* events the user application reading /dev/sonypi can use */

#define SONYPI_EVENT_JOGDIAL_DOWN		 1
#define SONYPI_EVENT_JOGDIAL_UP			 2
#define SONYPI_EVENT_JOGDIAL_DOWN_PRESSED	 3
#define SONYPI_EVENT_JOGDIAL_UP_PRESSED		 4
#define SONYPI_EVENT_JOGDIAL_PRESSED		 5
#define SONYPI_EVENT_JOGDIAL_RELEASED		 6
#define SONYPI_EVENT_CAPTURE_PRESSED		 7
#define SONYPI_EVENT_CAPTURE_RELEASED		 8
#define SONYPI_EVENT_CAPTURE_PARTIALPRESSED	 9
#define SONYPI_EVENT_CAPTURE_PARTIALRELEASED	10
#define SONYPI_EVENT_FNKEY_ESC			11
#define SONYPI_EVENT_FNKEY_F1			12
#define SONYPI_EVENT_FNKEY_F2			13
#define SONYPI_EVENT_FNKEY_F3			14
#define SONYPI_EVENT_FNKEY_F4			15
#define SONYPI_EVENT_FNKEY_F5			16
#define SONYPI_EVENT_FNKEY_F6			17
#define SONYPI_EVENT_FNKEY_F7			18
#define SONYPI_EVENT_FNKEY_F8			19
#define SONYPI_EVENT_FNKEY_F9			20
#define SONYPI_EVENT_FNKEY_F10			21
#define SONYPI_EVENT_FNKEY_F11			22
#define SONYPI_EVENT_FNKEY_F12			23
#define SONYPI_EVENT_FNKEY_1			24
#define SONYPI_EVENT_FNKEY_2			25
#define SONYPI_EVENT_FNKEY_D			26
#define SONYPI_EVENT_FNKEY_E			27
#define SONYPI_EVENT_FNKEY_F			28
#define SONYPI_EVENT_FNKEY_S			29
#define SONYPI_EVENT_FNKEY_B			30
#define SONYPI_EVENT_BLUETOOTH_PRESSED		31
#define SONYPI_EVENT_PKEY_P1                    32
#define SONYPI_EVENT_PKEY_P2                    33
#define SONYPI_EVENT_PKEY_P3                    34


/* brightness etc. ioctls */
#define SONYPI_IOCGBRT	_IOR('v', 0, __u8)
#define SONYPI_IOCSBRT	_IOW('v', 0, __u8)

#ifdef __KERNEL__

/* used only for communication between v4l and sonypi */

#define SONYPI_COMMAND_GETCAMERA		 1
#define SONYPI_COMMAND_SETCAMERA		 2
#define SONYPI_COMMAND_GETCAMERABRIGHTNESS	 3
#define SONYPI_COMMAND_SETCAMERABRIGHTNESS	 4
#define SONYPI_COMMAND_GETCAMERACONTRAST	 5
#define SONYPI_COMMAND_SETCAMERACONTRAST	 6
#define SONYPI_COMMAND_GETCAMERAHUE		 7
#define SONYPI_COMMAND_SETCAMERAHUE		 8
#define SONYPI_COMMAND_GETCAMERACOLOR		 9
#define SONYPI_COMMAND_SETCAMERACOLOR		10
#define SONYPI_COMMAND_GETCAMERASHARPNESS	11
#define SONYPI_COMMAND_SETCAMERASHARPNESS	12
#define SONYPI_COMMAND_GETCAMERAPICTURE		13
#define SONYPI_COMMAND_SETCAMERAPICTURE		14
#define SONYPI_COMMAND_GETCAMERAAGC		15
#define SONYPI_COMMAND_SETCAMERAAGC		16
#define SONYPI_COMMAND_GETCAMERADIRECTION	17
#define SONYPI_COMMAND_GETCAMERAROMVERSION	18
#define SONYPI_COMMAND_GETCAMERAREVISION	19

u8 sonypi_camera_command(int command, u8 value);

#endif /* __KERNEL__ */

#endif /* _SONYPI_H_ */
