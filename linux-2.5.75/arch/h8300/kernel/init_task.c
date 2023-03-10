/*
 *  linux/arch/h8300/kernel/init_task.c
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/init_task.h>
#include <linux/fs.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

/*
 * Initial task structure.
 *
 * All other task structs will be allocated on slabs in fork.c
 */
__asm__(".align 4");
struct task_struct init_task = INIT_TASK(init_task);


/*
 * Initial thread structure.
 *
 * We need to make sure that this is 8192-byte aligned due to the
 * way process stacks are handled. This is done by having a special
 * "init_task" linker map entry..
 */
union thread_union init_thread_union
	__attribute__((__section__(".data.init_task"))) =
		{ INIT_THREAD_INFO(init_task) };

