# $Id: Makefile,v 1.7.2.1 2002/03/14 01:26:21 kanoj Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

L_TARGET = promlib.a
obj-y   := bootstr.o devops.o init.o memory.o misc.o \
	   tree.o console.o printf.o p1275.o map.o

.S.s:
	$(CPP) $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -c $< -o $*.o

include $(TOPDIR)/Rules.make

%.o: %.c
	$(CC) $(subst -pg,,$(CFLAGS)) -c $<
