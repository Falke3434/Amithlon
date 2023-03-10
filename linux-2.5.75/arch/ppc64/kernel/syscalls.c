/*
 * linux/arch/ppc/kernel/sys_ppc.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Derived from "arch/i386/kernel/sys_i386.c"
 * Adapted from the i386 version by Gary Thomas
 * Modified by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras (paulus@cs.anu.edu.au).
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/PPC
 * platform.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/sys.h>
#include <linux/ipc.h>
#include <linux/utsname.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/personality.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>
#include <asm/semaphore.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>

extern unsigned long wall_jiffies;

void
check_bugs(void)
{
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int 
sys_ipc (uint call, int first, int second, long third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	ret = -ENOSYS;
	switch (call) {
	case SEMOP:
		ret = sys_semop (first, (struct sembuf *)ptr, second);
		break;
	case SEMGET:
		ret = sys_semget (first, second, third);
		break;
	case SEMCTL: {
		union semun fourth;

		if (!ptr)
			break;
		if ((ret = get_user(fourth.__pad, (void **)ptr)))
			break;
		ret = sys_semctl (first, second, third, fourth);
		break;
	}
	case MSGSND:
		ret = sys_msgsnd (first, (struct msgbuf *) ptr, second, third);
		break;
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;

			if (!ptr)
				break;
			if ((ret = copy_from_user(&tmp,
						(struct ipc_kludge *) ptr,
						sizeof (tmp)) ? -EFAULT : 0))
				break;
			ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp,
					  third);
			break;
		}
		default:
			ret = sys_msgrcv (first, (struct msgbuf *) ptr,
					  second, fifth, third);
			break;
		}
		break;
	case MSGGET:
		ret = sys_msgget ((key_t) first, second);
		break;
	case MSGCTL:
		ret = sys_msgctl (first, second, (struct msqid_ds *) ptr);
		break;
	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = sys_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				break;
			ret = put_user (raddr, (ulong *) third);
			break;
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				break;
			ret = sys_shmat (first, (char *) ptr, second,
					 (ulong *) third);
			break;
		}
		break;
	case SHMDT: 
		ret = sys_shmdt ((char *)ptr);
		break;
	case SHMGET:
		ret = sys_shmget (first, second, third);
		break;
	case SHMCTL:
		ret = sys_shmctl (first, second, (struct shmid_ds *) ptr);
		break;
	}

	return ret;
}

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(int *fildes)
{
	int fd[2];
	int error;
	
	error = do_pipe(fd);
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	
	return error;
}

unsigned long sys_mmap(unsigned long addr, size_t len,
		       unsigned long prot, unsigned long flags,
		       unsigned long fd, off_t offset)
{
	struct file * file = NULL;
	unsigned long ret = -EBADF;

	if (!(flags & MAP_ANONYMOUS)) {
		if (!(file = fget(fd)))
			goto out;
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	down_write(&current->mm->mmap_sem);
	ret = do_mmap(file, addr, len, prot, flags, offset);
	up_write(&current->mm->mmap_sem);
	if (file)
		fput(file);

out:
	return ret;
}

static int __init set_fakeppc(char *str)
{
	if (*str)
		return 0;
	init_task.personality = PER_LINUX32;
	return 1;
}
__setup("fakeppc", set_fakeppc);

asmlinkage int sys_uname(struct old_utsname * name)
{
	int err = -EFAULT;
	
	down_read(&uts_sem);
	if (name && !copy_to_user(name, &system_utsname, sizeof (*name)))
		err = 0;
	up_read(&uts_sem);
	
	return err;
}

asmlinkage time_t sys64_time(time_t* tloc)
{
	time_t secs;
	time_t usecs;

	long tb_delta = tb_ticks_since(tb_last_stamp);
	tb_delta += (jiffies - wall_jiffies) * tb_ticks_per_jiffy;

	secs  = xtime.tv_sec;  
	usecs = (xtime.tv_nsec/1000) + tb_delta / tb_ticks_per_usec;
	while (usecs >= USEC_PER_SEC) {
		++secs;
		usecs -= USEC_PER_SEC;
	}

	if (tloc) {
		if (put_user(secs,tloc))
			secs = -EFAULT;
	}

	return secs;
}
