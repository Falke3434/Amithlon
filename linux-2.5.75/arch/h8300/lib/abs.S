;;; memcpy.S

#include <asm/linkage.h>

#if defined(__H8300H__) 
	.h8300h
#endif
#if defined(__H8300S__) 
	.h8300s
#endif
	.text
.global SYMBOL_NAME(abs)

;;; int abs(int n)
SYMBOL_NAME_LABEL(abs)
	mov.l	er0,er0
	bpl	1f
	neg.l	er0
1:
	rts
	
