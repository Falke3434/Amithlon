/*
 * Copyright (C) 1995-2003 Russell King
 *               2001-2002 Keith Owens
 *     
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>

/*
 * Make sure that the compiler and target are compatible.
 */
#if defined(__APCS_26__)
#error Sorry, your compiler targets APCS-26 but this kernel requires APCS-32
#endif
/*
 * GCC 2.95.1, 2.95.2: ignores register clobber list in asm().
 * GCC 3.0, 3.1: general bad code generation.
 * GCC 3.2.0: incorrect function argument offset calculation.
 * GCC 3.2.x: miscompiles NEW_AUX_ENT in fs/binfmt_elf.c
 *            (http://gcc.gnu.org/cgi-bin/gnatsweb.pl?cmd=view&pr=8896)
 */
#if __GNUC__ < 2 || \
   (__GNUC__ == 2 && __GNUC_MINOR__ < 95) || \
   (__GNUC__ == 2 && __GNUC_MINOR__ == 95 && __GNUC_PATCHLEVEL__ != 0 && \
					     __GNUC_PATCHLEVEL__ < 3) || \
   (__GNUC__ == 3 && __GNUC_MINOR__ < 2) || \
   (__GNUC__ == 3 && __GNUC_MINOR__ == 2 && __GNUC_PATCHLEVEL__ < 1)
#error Your compiler is too buggy; it is known to miscompile kernels.
#error    Known good compilers: 2.95.3, 2.95.4, 2.96, 3.2.2+PR8896
#endif

/* Use marker if you need to separate the values later */

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
  DEFINE(TSK_USED_MATH,		offsetof(struct task_struct, used_math));
  DEFINE(TSK_ACTIVE_MM,		offsetof(struct task_struct, active_mm));
  BLANK();
  DEFINE(VMA_VM_MM,		offsetof(struct vm_area_struct, vm_mm));
  DEFINE(VMA_VM_FLAGS,		offsetof(struct vm_area_struct, vm_flags));
  BLANK();
  DEFINE(VM_EXEC,	       	VM_EXEC);
  BLANK();
  DEFINE(HPTE_TYPE_SMALL,      	PTE_TYPE_SMALL);
  DEFINE(HPTE_AP_READ,		PTE_AP_READ);
  DEFINE(HPTE_AP_WRITE,		PTE_AP_WRITE);
  BLANK();
  DEFINE(LPTE_PRESENT,		L_PTE_PRESENT);
  DEFINE(LPTE_YOUNG,		L_PTE_YOUNG);
  DEFINE(LPTE_BUFFERABLE,      	L_PTE_BUFFERABLE);
  DEFINE(LPTE_CACHEABLE,       	L_PTE_CACHEABLE);
  DEFINE(LPTE_USER,		L_PTE_USER);
  DEFINE(LPTE_WRITE,		L_PTE_WRITE);
  DEFINE(LPTE_EXEC,		L_PTE_EXEC);
  DEFINE(LPTE_DIRTY,		L_PTE_DIRTY);
  BLANK();
  DEFINE(PAGE_SZ,	       	PAGE_SIZE);
  BLANK();
  DEFINE(SYS_ERROR0,		0x9f0000);
  return 0; 
}
