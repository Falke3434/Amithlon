/*
 *  linux/include/asm-arm/glue.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *  Copyright (C) 2000-2002 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file provides the glue to stick the processor-specific bits
 *  into the kernel in an efficient manner.  The idea is to use branches
 *  when we're only targetting one class of TLB, or indirect calls
 *  when we're targetting multiple classes of TLBs.
 */
#ifdef __KERNEL__

#include <linux/config.h>

#ifdef __STDC__
#define ____glue(name,fn)	name##fn
#else
#define ____glue(name,fn)	name/**/fn
#endif
#define __glue(name,fn)		____glue(name,fn)



/*
 *	Data Abort Model
 *	================
 *
 *	We have the following to choose from:
 *	  arm6          - ARM6 style
 *	  arm7		- ARM7 style
 *	  v4_early	- ARMv4 without Thumb early abort handler
 *	  v4t_late	- ARMv4 with Thumb late abort handler
 *	  v4t_early	- ARMv4 with Thumb early abort handler
 *	  v5tej_early	- ARMv5 with Thumb and Java early abort handler
 *	  xscale	- ARMv5 with Thumb with Xscale extensions
 */
#undef CPU_ABORT_HANDLER
#undef MULTI_ABORT

#if defined(CONFIG_CPU_ARM610)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER cpu_arm6_data_abort
# endif
#endif

#if defined(CONFIG_CPU_ARM710)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER cpu_arm7_data_abort
# endif
#endif

#if defined(CONFIG_CPU_ARM720T)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER v4t_late_abort
# endif
#endif

#if defined(CONFIG_CPU_SA110) || defined(CONFIG_CPU_SA1100)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER v4_early_abort
# endif
#endif

#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM1020)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER v4t_early_abort
# endif
#endif

#if defined(CONFIG_CPU_ARM926T)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER v5tej_early_abort
# endif
#endif

#if defined(CONFIG_CPU_XSCALE)
# ifdef CPU_ABORT_HANDLER
#  define MULTI_ABORT 1
# else
#  define CPU_ABORT_HANDLER xscale_abort
# endif
#endif

#ifndef CPU_ABORT_HANDLER
#error Unknown data abort handler type
#endif

#endif
