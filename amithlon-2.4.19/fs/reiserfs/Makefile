#
# Makefile for the linux reiser-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := reiserfs.o
O_OBJS   := bitmap.o do_balan.o namei.o inode.o file.o dir.o fix_node.o super.o prints.o objectid.o \
lbalance.o ibalance.o stree.o hashes.o buffer2.o tail_conversion.o journal.o resize.o tail_conversion.o version.o item_ops.o

M_OBJS   := $(O_TARGET)

# gcc -O2 (the kernel default)  is overaggressive on ppc when many inline
# functions are used.  This causes the compiler to advance the stack
# pointer out of the available stack space, corrupting kernel space,
# and causing a panic. Since this behavior only affects ppc, this ifeq
# will work around it. If any other architecture displays this behavior,
# add it here.
ifeq ($(shell uname -m),ppc) 
EXTRA_CFLAGS := -O1
endif

include $(TOPDIR)/Rules.make

TAGS:
	etags *.c

