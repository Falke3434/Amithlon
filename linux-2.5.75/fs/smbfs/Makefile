#
# Makefile for the linux smb-filesystem routines.
#

obj-$(CONFIG_SMB_FS) += smbfs.o

smbfs-objs := proc.o dir.o cache.o sock.o inode.o file.o ioctl.o getopt.o \
		symlink.o smbiod.o request.o

# If you want debugging output, you may add these flags to the EXTRA_CFLAGS
# SMBFS_PARANOIA should normally be enabled.

EXTRA_CFLAGS += -DSMBFS_PARANOIA
#EXTRA_CFLAGS += -DSMBFS_DEBUG
#EXTRA_CFLAGS += -DSMBFS_DEBUG_VERBOSE
#EXTRA_CFLAGS += -DDEBUG_SMB_MALLOC
#EXTRA_CFLAGS += -DDEBUG_SMB_TIMESTAMP
#EXTRA_CFLAGS += -Werror

#
# Maintainer rules
#

# getopt.c not included. It is intentionally separate
SRC = proc.c dir.c cache.c sock.c inode.c file.c ioctl.c smbiod.c request.c \
	symlink.c

proto:
	-rm -f proto.h
	@echo >  proto2.h "/*"
	@echo >> proto2.h " *  Autogenerated with cproto on: " `date`
	@echo >> proto2.h " */"
	@echo >> proto2.h ""
	@echo >> proto2.h "struct smb_request;"
	@echo >> proto2.h ""
	cproto -E "gcc -E" -e -v -I $(TOPDIR)/include -DMAKING_PROTO -D__KERNEL__ $(SRC) >> proto2.h
	mv proto2.h proto.h
