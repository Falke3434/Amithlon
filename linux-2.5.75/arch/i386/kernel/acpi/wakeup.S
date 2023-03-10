.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page.h>

#
# wakeup_code runs in real mode, and at unknown address (determined at run-time).
# Therefore it must only use relative jumps/calls. 
#
# Do we need to deal with A20? It is okay: ACPI specs says A20 must be enabled
#
# If physical address of wakeup_code is 0x12345, BIOS should call us with
# cs = 0x1234, eip = 0x05
# 

ALIGN
	.align	4096
ENTRY(wakeup_start)
wakeup_code:
	wakeup_code_start = .
	.code16

 	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'L', %fs:(0x10)

	cli
	cld

	# setup data segment
	movw	%cs, %ax
	movw	%ax, %ds					# Make ds:0 point to wakeup_start
	movw	%ax, %ss
	mov	$(wakeup_stack - wakeup_code), %sp		# Private stack is needed for ASUS board
	movw	$0x0e00 + 'S', %fs:(0x12)

	pushl	$0						# Kill any dangerous flags
	popfl

	movl	real_magic - wakeup_code, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	testl	$1, video_flags - wakeup_code
	jz	1f
	lcall   $0xc000,$3
	movw	%cs, %ax
	movw	%ax, %ds					# Bios might have played with that
	movw	%ax, %ss
1:

	testl	$2, video_flags - wakeup_code
	jz	1f
	mov	video_mode - wakeup_code, %ax
	call	mode_set
1:

	# set up page table
	movl	$swapper_pg_dir-__PAGE_OFFSET, %eax
	movl	%eax, %cr3

	# make sure %cr4 is set correctly (features, etc)
	movl	real_save_cr4 - wakeup_code, %eax
	movl	%eax, %cr4
	movw	$0xb800, %ax
	movw	%ax,%fs
	movw	$0x0e00 + 'i', %fs:(0x12)
	
	# need a gdt
	lgdt	real_save_gdt - wakeup_code

	movl	real_save_cr0 - wakeup_code, %eax
	movl	%eax, %cr0
	jmp 1f
1:
	movw	$0x0e00 + 'n', %fs:(0x14)

	movl	real_magic - wakeup_code, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_real_magic

	ljmpl	$__KERNEL_CS,$wakeup_pmode_return

real_save_gdt:	.word 0
		.long 0
real_save_cr0:	.long 0
real_save_cr3:	.long 0
real_save_cr4:	.long 0
real_magic:	.long 0
video_mode:	.long 0
video_flags:	.long 0

bogus_real_magic:
	movw	$0x0e00 + 'B', %fs:(0x12)
	jmp bogus_real_magic

/* This code uses an extended set of video mode numbers. These include:
 * Aliases for standard modes
 *	NORMAL_VGA (-1)
 *	EXTENDED_VGA (-2)
 *	ASK_VGA (-3)
 * Video modes numbered by menu position -- NOT RECOMMENDED because of lack
 * of compatibility when extending the table. These are between 0x00 and 0xff.
 */
#define VIDEO_FIRST_MENU 0x0000

/* Standard BIOS video modes (BIOS number + 0x0100) */
#define VIDEO_FIRST_BIOS 0x0100

/* VESA BIOS video modes (VESA number + 0x0200) */
#define VIDEO_FIRST_VESA 0x0200

/* Video7 special modes (BIOS number + 0x0900) */
#define VIDEO_FIRST_V7 0x0900

# Setting of user mode (AX=mode ID) => CF=success
mode_set:
	movw	%ax, %bx
#if 0
	cmpb	$0xff, %ah
	jz	setalias

	testb	$VIDEO_RECALC>>8, %ah
	jnz	_setrec

	cmpb	$VIDEO_FIRST_RESOLUTION>>8, %ah
	jnc	setres
	
	cmpb	$VIDEO_FIRST_SPECIAL>>8, %ah
	jz	setspc

	cmpb	$VIDEO_FIRST_V7>>8, %ah
	jz	setv7
#endif
	
	cmpb	$VIDEO_FIRST_VESA>>8, %ah
	jnc	check_vesa
#if 0	
	orb	%ah, %ah
	jz	setmenu
#endif
	
	decb	%ah
#	jz	setbios				  Add bios modes later

setbad:	clc
	ret

check_vesa:
	subb	$VIDEO_FIRST_VESA>>8, %bh
	orw	$0x4000, %bx			# Use linear frame buffer
	movw	$0x4f02, %ax			# VESA BIOS mode set call
	int	$0x10
	cmpw	$0x004f, %ax			# AL=4f if implemented
	jnz	_setbad				# AH=0 if OK

	stc
	ret

_setbad: jmp setbad

	.code32
	ALIGN

.org	0x800
wakeup_stack_begin:	# Stack grows down

.org	0xff0		# Just below end of page
wakeup_stack:
ENTRY(wakeup_end)
	
.org	0x1000

wakeup_pmode_return:
	movl	$__KERNEL_DS, %eax
	movl	%eax, %ds
	movw	$0x0e00 + 'u', %ds:(0xb8016)

	# restore other segment registers
	xorl	%eax, %eax
	movw	%ax, %fs
	movw	%ax, %gs

	# reload the gdt, as we need the full 32 bit address
	lgdt	saved_gdt
	lidt	saved_idt
	lldt	saved_ldt
	ljmp	$(__KERNEL_CS),$1f
1:
	movl	%cr3, %eax
	movl	%eax, %cr3
	wbinvd

	# and restore the stack ... but you need gdt for this to work
	movl	$__KERNEL_DS, %eax
	movw	%ax, %ss
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movl	saved_esp, %esp

	movw	$0x0e00 + 'W', %ds:(0xb8018)
	movl	$(1024*1024*3), %ecx
	movl	$0, %esi
	rep	lodsb
	movw	$0x0e00 + 'O', %ds:(0xb8018)

	movl	%cs:saved_magic, %eax
	cmpl	$0x12345678, %eax
	jne	bogus_magic

	# restore the other general registers
	movl	saved_ebx, %ebx
	movl	saved_edi, %edi
	movl	saved_esi, %esi
	movl	saved_ebp, %ebp

	# jump to place where we left off
	movl	saved_eip,%eax
	movw	$0x0e00 + 'x', %ds:(0xb8018)
	pushl	%eax
	popl	%eax
	movw	$0x0e00 + '!', %ds:(0xb801a)
	jmp	*%eax

bogus_magic:
	movw	$0x0e00 + 'B', %ds:(0xb8018)
	jmp bogus_magic

bogus_magic2:
	movw	$0x0e00 + '2', %ds:(0xb8018)
	jmp bogus_magic2
		
##
# acpi_copy_wakeup_routine
#
# Copy the above routine to low memory.
#
# Parameters:
# %eax:	place to copy wakeup routine to
#
# Returned address is location of code in low memory (past data and stack)
#
ENTRY(acpi_copy_wakeup_routine)

	sgdt	saved_gdt
	sidt	saved_idt
	sldt	saved_ldt
	str	saved_tss

	movl    %cr3, %edx
	movl    %edx, real_save_cr3 - wakeup_start (%eax)
	movl    %cr4, %edx
	movl    %edx, real_save_cr4 - wakeup_start (%eax)
	movl	%cr0, %edx
	movl	%edx, real_save_cr0 - wakeup_start (%eax)
	sgdt    real_save_gdt - wakeup_start (%eax)

	movl	saved_videomode, %edx
	movl	%edx, video_mode - wakeup_start (%eax)
	movl	acpi_video_flags, %edx
	movl	%edx, video_flags - wakeup_start (%eax)
	movl	$0x12345678, real_magic - wakeup_start (%eax)
	movl	$0x12345678, saved_magic
	ret

.data
ALIGN
ENTRY(saved_ebp)	.long	0
ENTRY(saved_esi)	.long	0
ENTRY(saved_edi)	.long	0
ENTRY(saved_ebx)	.long	0

ENTRY(saved_eip)	.long	0
ENTRY(saved_esp)	.long	0

ENTRY(saved_magic)	.long	0

ENTRY(do_suspend_lowlevel)
	cmpl $0,4(%esp)
	jne ret_point
	call save_processor_state

	movl %esp, saved_context_esp
	movl %eax, saved_context_eax
	movl %ebx, saved_context_ebx
	movl %ecx, saved_context_ecx
	movl %edx, saved_context_edx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl ; popl saved_context_eflags

	movl $ret_point,saved_eip
	movl %esp,saved_esp
	movl %ebp,saved_ebp
	movl %ebx,saved_ebx
	movl %edi,saved_edi
	movl %esi,saved_esi

	pushl $3
	call acpi_enter_sleep_state
	addl $4,%esp
	ret
	.p2align 4,,7
ret_point:
	movl $__KERNEL_DS,%eax
	movw %ax, %ds
	movl saved_context_esp, %esp
	movl saved_context_ebp, %ebp
	movl saved_context_eax, %eax
	movl saved_context_ebx, %ebx
	movl saved_context_ecx, %ecx
	movl saved_context_edx, %edx
	movl saved_context_esi, %esi
	movl saved_context_edi, %edi
	call restore_processor_state
	pushl saved_context_eflags ; popfl
	ret

ENTRY(do_suspend_lowlevel_s4bios)
	cmpl $0,4(%esp)
	jne ret_point
	call save_processor_state

	movl %esp, saved_context_esp
	movl %eax, saved_context_eax
	movl %ebx, saved_context_ebx
	movl %ecx, saved_context_ecx
	movl %edx, saved_context_edx
	movl %ebp, saved_context_ebp
	movl %esi, saved_context_esi
	movl %edi, saved_context_edi
	pushfl ; popl saved_context_eflags

	movl $ret_point,saved_eip
	movl %esp,saved_esp
	movl %ebp,saved_ebp
	movl %ebx,saved_ebx
	movl %edi,saved_edi
	movl %esi,saved_esi

	call acpi_enter_sleep_state_s4bios
	ret

ALIGN
# saved registers
saved_gdt:	.long	0,0
saved_idt:	.long	0,0
saved_ldt:	.long	0
saved_tss:	.long	0

