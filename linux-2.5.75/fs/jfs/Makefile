#
# Makefile for the Linux JFS filesystem routines.
#

obj-$(CONFIG_JFS_FS) += jfs.o

jfs-objs := super.o file.o inode.o namei.o jfs_mount.o jfs_umount.o \
	    jfs_xtree.o jfs_imap.o jfs_debug.o jfs_dmap.o \
	    jfs_unicode.o jfs_dtree.o jfs_inode.o \
	    jfs_extent.o symlink.o jfs_metapage.o \
	    jfs_logmgr.o jfs_txnmgr.o jfs_uniupr.o resize.o xattr.o

ifeq ($(CONFIG_JFS_POSIX_ACL),y)
jfs-objs += acl.o
endif

EXTRA_CFLAGS += -D_JFS_4K
