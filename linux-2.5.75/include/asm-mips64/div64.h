/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_DIV64_H
#define _ASM_DIV64_H

/*
 * Don't use this one in new code
 */
#define do_div64_32(res, high, low, base) ({ \
	unsigned int __quot, __mod; \
	unsigned long __div; \
	unsigned int __low, __high, __base; \
	\
	__high = (high); \
	__low = (low); \
	__div = __high; \
	__div = __div << 32 | __low; \
	__base = (base); \
	\
	__mod = __div % __base; \
	__div = __div / __base; \
	\
	__quot = __div; \
	(res) = __quot; \
	__mod; })

#include <asm-generic.h>

#endif /* _ASM_DIV64_H */
