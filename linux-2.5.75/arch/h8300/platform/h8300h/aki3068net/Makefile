#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

all: $(BOARD).o
O_TARGET := $(BOARD).o
obj-y := timer.o

timer.o: timer.c

clean:
	rm -f *.[oa]
