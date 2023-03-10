
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/romfs_fs.h>
#include <linux/initrd.h>

#include "do_mounts.h"

unsigned int real_root_dev;	/* do_proc_dointvec cannot handle kdev_t */
static int __initdata old_fd, root_fd;
static int __initdata mount_initrd = 1;

static int __init no_initrd(char *str)
{
	mount_initrd = 0;
	return 1;
}

__setup("noinitrd", no_initrd);

static int __init do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };
	extern char * envp_init[];

	close(old_fd);close(root_fd);
	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/console",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp_init);
}

static void __init handle_initrd(void)
{
	int error;
	int i, pid;

	real_root_dev = ROOT_DEV;
	create_dev("/dev/root.old", Root_RAM0, NULL);
	/* mount initrd on rootfs' /root */
	mount_block_root("/dev/root.old", root_mountflags & ~MS_RDONLY);
	sys_mkdir("/old", 0700);
	root_fd = open("/", 0, 0);
	old_fd = open("/old", 0, 0);
	/* move initrd over / and chdir/chroot in initrd root */
	sys_chdir("/root");
	sys_mount(".", "/", NULL, MS_MOVE, NULL);
	sys_chroot(".");
	mount_devfs_fs ();

	pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
	if (pid > 0) {
		while (pid != waitpid(-1, &i, 0))
			yield();
	}

	/* move initrd to rootfs' /old */
	sys_fchdir(old_fd);
	sys_mount("/", ".", NULL, MS_MOVE, NULL);
	/* switch root and cwd back to / of rootfs */
	sys_fchdir(root_fd);
	sys_chroot(".");
	close(old_fd);
	close(root_fd);
	umount_devfs("/old/dev");

	if (real_root_dev == Root_RAM0) {
		sys_chdir("/old");
		return;
	}

	ROOT_DEV = real_root_dev;
	mount_root();

	printk(KERN_NOTICE "Trying to move old root to /initrd ... ");
	error = sys_mount("/old", "/root/initrd", NULL, MS_MOVE, NULL);
	if (!error)
		printk("okay\n");
	else {
		int fd = open("/dev/root.old", O_RDWR, 0);
		printk("failed\n");
		printk(KERN_NOTICE "Unmounting old root\n");
		sys_umount("/old", MNT_DETACH);
		printk(KERN_NOTICE "Trying to free ramdisk memory ... ");
		if (fd < 0) {
			error = fd;
		} else {
			error = sys_ioctl(fd, BLKFLSBUF, 0);
			close(fd);
		}
		printk(!error ? "okay\n" : "failed\n");
	}
}

int __init initrd_load(void)
{
	if (!mount_initrd)
		return 0;

	create_dev("/dev/ram", MKDEV(RAMDISK_MAJOR, 0), NULL);
	create_dev("/dev/initrd", MKDEV(RAMDISK_MAJOR, INITRD_MINOR), NULL);
	/* Load the initrd data into /dev/ram0. Execute it as initrd unless
	 * /dev/ram0 is supposed to be our actual root device, in
	 * that case the ram disk is just set up here, and gets
	 * mounted in the normal path. */
	if (rd_load_image("/dev/initrd") && ROOT_DEV != Root_RAM0) {
		handle_initrd();
		return 1;
	}
	return 0;
}
