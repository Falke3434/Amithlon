#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#define LINE_SIZE 80

#include <asm/mtrr.h>
#include "mtrr.h"

/* RED-PEN: this is accessed without any locking */
extern unsigned int *usage_table;

static int mtrr_seq_show(struct seq_file *seq, void *offset);

#define FILE_FCOUNT(f) (((struct seq_file *)((f)->private_data))->private)

static int
mtrr_file_add(unsigned long base, unsigned long size,
	      unsigned int type, char increment, struct file *file, int page)
{
	int reg, max;
	unsigned int *fcount = FILE_FCOUNT(file); 

	max = num_var_ranges;
	if (fcount == NULL) {
		fcount = kmalloc(max * sizeof *fcount, GFP_KERNEL);
		if (!fcount)
			return -ENOMEM;
		memset(fcount, 0, max * sizeof *fcount);
		FILE_FCOUNT(file) = fcount;
	}
	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
			return -EINVAL;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_add_page(base, size, type, 1);
	if (reg >= 0)
		++fcount[reg];
	return reg;
}

static int
mtrr_file_del(unsigned long base, unsigned long size,
	      struct file *file, int page)
{
	int reg;
	unsigned int *fcount = FILE_FCOUNT(file);

	if (!page) {
		if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
			return -EINVAL;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
	}
	reg = mtrr_del_page(-1, base, size);
	if (reg < 0)
		return reg;
	if (fcount == NULL)
		return reg;
	if (fcount[reg] < 1)
		return -EINVAL;
	--fcount[reg];
	return reg;
}

/* RED-PEN: seq_file can seek now. this is ignored. */
static ssize_t
mtrr_write(struct file *file, const char __user *buf, size_t len, loff_t * ppos)
/*  Format of control line:
    "base=%Lx size=%Lx type=%s"     OR:
    "disable=%d"
*/
{
	int i, err;
	unsigned long reg;
	unsigned long long base, size;
	char *ptr;
	char line[LINE_SIZE];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;
	memset(line, 0, LINE_SIZE);
	if (len > LINE_SIZE)
		len = LINE_SIZE;
	if (copy_from_user(line, buf, len - 1))
		return -EFAULT;
	ptr = line + strlen(line) - 1;
	if (*ptr == '\n')
		*ptr = '\0';
	if (!strncmp(line, "disable=", 8)) {
		reg = simple_strtoul(line + 8, &ptr, 0);
		err = mtrr_del_page(reg, 0, 0);
		if (err < 0)
			return err;
		return len;
	}
	if (strncmp(line, "base=", 5))
		return -EINVAL;
	base = simple_strtoull(line + 5, &ptr, 0);
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "size=", 5))
		return -EINVAL;
	size = simple_strtoull(ptr + 5, &ptr, 0);
	if ((base & 0xfff) || (size & 0xfff))
		return -EINVAL;
	for (; isspace(*ptr); ++ptr) ;
	if (strncmp(ptr, "type=", 5))
		return -EINVAL;
	ptr += 5;
	for (; isspace(*ptr); ++ptr) ;
	for (i = 0; i < MTRR_NUM_TYPES; ++i) {
		if (strcmp(ptr, mtrr_strings[i]))
			continue;
		base >>= PAGE_SHIFT;
		size >>= PAGE_SHIFT;
		err =
		    mtrr_add_page((unsigned long) base, (unsigned long) size, i,
				  1);
		if (err < 0)
			return err;
		return len;
	}
	return -EINVAL;
}

static int
mtrr_ioctl(struct inode *inode, struct file *file,
	   unsigned int cmd, unsigned long __arg)
{
	int err;
	mtrr_type type;
	struct mtrr_sentry sentry;
	struct mtrr_gentry gentry;
	void __user *arg = (void __user *) __arg;

	switch (cmd) {
	default:
		return -ENOIOCTLCMD;
	case MTRRIOC_ADD_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_SET_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_add(sentry.base, sentry.size, sentry.type, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_DEL_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_file_del(sentry.base, sentry.size, file, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_KILL_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_del(-1, sentry.base, sentry.size);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_GET_ENTRY:
		if (copy_from_user(&gentry, arg, sizeof gentry))
			return -EFAULT;
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &gentry.size, &type);

		/* Hide entries that go above 4GB */
		if (gentry.base + gentry.size > 0x100000
		    || gentry.size == 0x100000)
			gentry.base = gentry.size = gentry.type = 0;
		else {
			gentry.base <<= PAGE_SHIFT;
			gentry.size <<= PAGE_SHIFT;
			gentry.type = type;
		}

		if (copy_to_user(arg, &gentry, sizeof gentry))
			return -EFAULT;
		break;
	case MTRRIOC_ADD_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err =
		    mtrr_file_add(sentry.base, sentry.size, sentry.type, 1,
				  file, 1);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_SET_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_add_page(sentry.base, sentry.size, sentry.type, 0);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_DEL_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_file_del(sentry.base, sentry.size, file, 1);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_KILL_PAGE_ENTRY:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (copy_from_user(&sentry, arg, sizeof sentry))
			return -EFAULT;
		err = mtrr_del_page(-1, sentry.base, sentry.size);
		if (err < 0)
			return err;
		break;
	case MTRRIOC_GET_PAGE_ENTRY:
		if (copy_from_user(&gentry, arg, sizeof gentry))
			return -EFAULT;
		if (gentry.regnum >= num_var_ranges)
			return -EINVAL;
		mtrr_if->get(gentry.regnum, &gentry.base, &gentry.size, &type);
		gentry.type = type;

		if (copy_to_user(arg, &gentry, sizeof gentry))
			return -EFAULT;
		break;
	}
	return 0;
}

static int
mtrr_close(struct inode *ino, struct file *file)
{
	int i, max;
	unsigned int *fcount = FILE_FCOUNT(file);

	if (fcount != NULL) {
		max = num_var_ranges;
		for (i = 0; i < max; ++i) {
			while (fcount[i] > 0) {
				mtrr_del(i, 0, 0);
				--fcount[i];
			}
		}
		kfree(fcount);
		FILE_FCOUNT(file) = NULL;
	}
	return single_release(ino, file);
}

static int mtrr_open(struct inode *inode, struct file *file)
{
	if (!mtrr_if) 
		return -EIO;
	if (!mtrr_if->get) 
		return -ENXIO; 
	return single_open(file, mtrr_seq_show, NULL);
}

static struct file_operations mtrr_fops = {
	.owner   = THIS_MODULE,
	.open	 = mtrr_open, 
	.read    = seq_read,
	.llseek  = seq_lseek,
	.write   = mtrr_write,
	.ioctl   = mtrr_ioctl,
	.release = mtrr_close,
};

#  ifdef CONFIG_PROC_FS

static struct proc_dir_entry *proc_root_mtrr;

#  endif			/*  CONFIG_PROC_FS  */

char * attrib_to_str(int x)
{
	return (x <= 6) ? mtrr_strings[x] : "?";
}

static int mtrr_seq_show(struct seq_file *seq, void *offset)
{
	char factor;
	int i, max, len;
	mtrr_type type;
	unsigned long base;
	unsigned int size;

	len = 0;
	max = num_var_ranges;
	for (i = 0; i < max; i++) {
		mtrr_if->get(i, &base, &size, &type);
		if (size == 0)
			usage_table[i] = 0;
		else {
			if (size < (0x100000 >> PAGE_SHIFT)) {
				/* less than 1MB */
				factor = 'K';
				size <<= PAGE_SHIFT - 10;
			} else {
				factor = 'M';
				size >>= 20 - PAGE_SHIFT;
			}
			/* RED-PEN: base can be > 32bit */ 
			len += seq_printf(seq, 
				   "reg%02i: base=0x%05lx000 (%4liMB), size=%4i%cB: %s, count=%d\n",
			     i, base, base >> (20 - PAGE_SHIFT), size, factor,
			     attrib_to_str(type), usage_table[i]);
		}
	}
	return 0;
}

static int __init mtrr_if_init(void)
{
#ifdef CONFIG_PROC_FS
	proc_root_mtrr =
	    create_proc_entry("mtrr", S_IWUSR | S_IRUGO, &proc_root);
	if (proc_root_mtrr) {
		proc_root_mtrr->owner = THIS_MODULE;
		proc_root_mtrr->proc_fops = &mtrr_fops;
	}
#endif
	return 0;
}

arch_initcall(mtrr_if_init);
