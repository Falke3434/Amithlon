/*
 * This file implements the perfmon-2 subsystem which is used
 * to program the IA-64 Performance Monitoring Unit (PMU).
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and 
 * David Mosberger, Hewlett Packard Co.
 * 
 * Version Perfmon-2.x is a rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co. 
 *
 * Copyright (C) 1999-2003  Hewlett Packard Co
 *               Stephane Eranian <eranian@hpl.hp.com>
 *               David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://www.hpl.hp.com/research/linux/perfmon
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/list.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/vfs.h>
#include <linux/pagemap.h>
#include <linux/mount.h>
#include <linux/version.h>

#include <asm/bitops.h>
#include <asm/errno.h>
#include <asm/page.h>
#include <asm/perfmon.h>
#include <asm/processor.h>
#include <asm/signal.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/delay.h>

#ifdef CONFIG_PERFMON
/*
 * perfmon context state
 */
#define PFM_CTX_UNLOADED	1	/* context is not loaded onto any task */
#define PFM_CTX_LOADED		2	/* context is loaded onto a task */
#define PFM_CTX_MASKED		3	/* context is loaded but monitoring is masked due to overflow */
#define PFM_CTX_ZOMBIE		4	/* owner of the context is closing it */
#define PFM_CTX_TERMINATED	5	/* the task the context was loaded onto is gone */

#define CTX_LOADED(c)        (c)->ctx_state = PFM_CTX_LOADED
#define CTX_UNLOADED(c)      (c)->ctx_state = PFM_CTX_UNLOADED
#define CTX_ZOMBIE(c)        (c)->ctx_state = PFM_CTX_ZOMBIE
#define CTX_DESTROYED(c)     (c)->ctx_state = PFM_CTX_DESTROYED
#define CTX_MASKED(c)        (c)->ctx_state = PFM_CTX_MASKED
#define CTX_TERMINATED(c)    (c)->ctx_state = PFM_CTX_TERMINATED

#define CTX_IS_UNLOADED(c)   ((c)->ctx_state == PFM_CTX_UNLOADED)
#define CTX_IS_LOADED(c)     ((c)->ctx_state == PFM_CTX_LOADED)
#define CTX_IS_ZOMBIE(c)     ((c)->ctx_state == PFM_CTX_ZOMBIE)
#define CTX_IS_MASKED(c)     ((c)->ctx_state == PFM_CTX_MASKED)
#define CTX_IS_TERMINATED(c) ((c)->ctx_state == PFM_CTX_TERMINATED)
#define CTX_IS_DEAD(c)	     ((c)->ctx_state == PFM_CTX_TERMINATED || (c)->ctx_state == PFM_CTX_ZOMBIE)

#define PFM_INVALID_ACTIVATION	(~0UL)


/*
 * depth of message queue
 */
#define PFM_MAX_MSGS		32
#define PFM_CTXQ_EMPTY(g)	((g)->ctx_msgq_head == (g)->ctx_msgq_tail)

/*
 * type of a PMU register (bitmask).
 * bitmask structure:
 * 	bit0   : register implemented
 * 	bit1   : end marker
 * 	bit2-3 : reserved
 * 	bit4   : pmc has pmc.pm
 * 	bit5   : pmc controls a counter (has pmc.oi), pmd is used as counter
 * 	bit6-7 : register type
 * 	bit8-31: reserved
 */
#define PFM_REG_NOTIMPL		0x0 /* not implemented at all */
#define PFM_REG_IMPL		0x1 /* register implemented */
#define PFM_REG_END		0x2 /* end marker */
#define PFM_REG_MONITOR		(0x1<<4|PFM_REG_IMPL) /* a PMC with a pmc.pm field only */
#define PFM_REG_COUNTING	(0x2<<4|PFM_REG_MONITOR|PFM_REG_IMPL) /* a monitor + pmc.oi+ PMD used as a counter */
#define PFM_REG_CONTROL		(0x4<<4|PFM_REG_IMPL) /* PMU control register */
#define	PFM_REG_CONFIG		(0x8<<4|PFM_REG_IMPL) /* configuration register */
#define PFM_REG_BUFFER	 	(0xc<<4|PFM_REG_IMPL) /* PMD used as buffer */

#define PMC_IS_LAST(i)	(pmu_conf.pmc_desc[i].type & PFM_REG_END)
#define PMD_IS_LAST(i)	(pmu_conf.pmd_desc[i].type & PFM_REG_END)

#define PFM_IS_DISABLED() (pmu_conf.enabled == 0)

#define PMC_OVFL_NOTIFY(ctx, i)	((ctx)->ctx_pmds[i].flags &  PFM_REGFL_OVFL_NOTIFY)

/* i assumed unsigned */
#define PMC_IS_IMPL(i)	  (i< PMU_MAX_PMCS && (pmu_conf.pmc_desc[i].type & PFM_REG_IMPL))
#define PMD_IS_IMPL(i)	  (i< PMU_MAX_PMDS && (pmu_conf.pmd_desc[i].type & PFM_REG_IMPL))

/* XXX: these assume that register i is implemented */
#define PMD_IS_COUNTING(i) ((pmu_conf.pmd_desc[i].type & PFM_REG_COUNTING) == PFM_REG_COUNTING)
#define PMC_IS_COUNTING(i) ((pmu_conf.pmc_desc[i].type & PFM_REG_COUNTING) == PFM_REG_COUNTING)
#define PMC_IS_MONITOR(i)  ((pmu_conf.pmc_desc[i].type & PFM_REG_MONITOR)  == PFM_REG_MONITOR)
#define PMC_IS_CONTROL(i)  ((pmu_conf.pmc_desc[i].type & PFM_REG_CONTROL)  == PFM_REG_CONTROL)

#define PMC_DFL_VAL(i)     pmu_conf.pmc_desc[i].default_value
#define PMC_RSVD_MASK(i)   pmu_conf.pmc_desc[i].reserved_mask
#define PMD_PMD_DEP(i)	   pmu_conf.pmd_desc[i].dep_pmd[0]
#define PMC_PMD_DEP(i)	   pmu_conf.pmc_desc[i].dep_pmd[0]

/* k assumed unsigned (up to 64 registers) */
#define IBR_IS_IMPL(k)	  (k< IA64_NUM_DBG_REGS)
#define DBR_IS_IMPL(k)	  (k< IA64_NUM_DBG_REGS)

#define CTX_OVFL_NOBLOCK(c)	((c)->ctx_fl_block == 0)
#define CTX_HAS_SMPL(c)		((c)->ctx_fl_is_sampling)
#define PFM_CTX_TASK(h)		(h)->ctx_task

/* XXX: does not support more than 64 PMDs */
#define CTX_USED_PMD(ctx, mask) (ctx)->ctx_used_pmds[0] |= (mask)
#define CTX_IS_USED_PMD(ctx, c) (((ctx)->ctx_used_pmds[0] & (1UL << (c))) != 0UL)

#define CTX_USED_MONITOR(ctx, mask) (ctx)->ctx_used_monitors[0] |= (mask)

#define CTX_USED_IBR(ctx,n) 	(ctx)->ctx_used_ibrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USED_DBR(ctx,n) 	(ctx)->ctx_used_dbrs[(n)>>6] |= 1UL<< ((n) % 64)
#define CTX_USES_DBREGS(ctx)	(((pfm_context_t *)(ctx))->ctx_fl_using_dbreg==1)
#define PFM_CODE_RR	0	/* requesting code range restriction */
#define PFM_DATA_RR	1	/* requestion data range restriction */

#define PFM_CPUINFO_CLEAR(v)	pfm_get_cpu_var(pfm_syst_info) &= ~(v)
#define PFM_CPUINFO_SET(v)	pfm_get_cpu_var(pfm_syst_info) |= (v)
#define PFM_CPUINFO_GET()	pfm_get_cpu_var(pfm_syst_info)

/*
 * context protection macros
 * in SMP:
 * 	- we need to protect against CPU concurrency (spin_lock)
 * 	- we need to protect against PMU overflow interrupts (local_irq_disable)
 * in UP:
 * 	- we need to protect against PMU overflow interrupts (local_irq_disable)
 *
 * spin_lock_irqsave()/spin_lock_irqrestore():
 * 	in SMP: local_irq_disable + spin_lock
 * 	in UP : local_irq_disable
 *
 * spin_lock()/spin_lock():
 * 	in UP : removed automatically
 * 	in SMP: protect against context accesses from other CPU. interrupts
 * 	        are not masked. This is useful for the PMU interrupt handler
 * 	        because we know we will not get PMU concurrency in that code.
 */
#define PROTECT_CTX(c, f) \
	do {  \
		DPRINT(("spinlock_irq_save ctx %p by [%d]\n", c, current->pid)); \
		spin_lock_irqsave(&(c)->ctx_lock, f); \
		DPRINT(("spinlocked ctx %p  by [%d]\n", c, current->pid)); \
	} while(0)

#define UNPROTECT_CTX(c, f) \
	do { \
		DPRINT(("spinlock_irq_restore ctx %p by [%d]\n", c, current->pid)); \
		spin_unlock_irqrestore(&(c)->ctx_lock, f); \
	} while(0)

#define PROTECT_CTX_NOPRINT(c, f) \
	do {  \
		spin_lock_irqsave(&(c)->ctx_lock, f); \
	} while(0)


#define UNPROTECT_CTX_NOPRINT(c, f) \
	do { \
		spin_unlock_irqrestore(&(c)->ctx_lock, f); \
	} while(0)


#define PROTECT_CTX_NOIRQ(c) \
	do {  \
		spin_lock(&(c)->ctx_lock); \
	} while(0)

#define UNPROTECT_CTX_NOIRQ(c) \
	do { \
		spin_unlock(&(c)->ctx_lock); \
	} while(0)


#ifdef CONFIG_SMP

#define GET_ACTIVATION()	pfm_get_cpu_var(pmu_activation_number)
#define INC_ACTIVATION()	pfm_get_cpu_var(pmu_activation_number)++
#define SET_ACTIVATION(c)	(c)->ctx_last_activation = GET_ACTIVATION()

#else /* !CONFIG_SMP */
#define SET_ACTIVATION(t) 	do {} while(0)
#define GET_ACTIVATION(t) 	do {} while(0)
#define INC_ACTIVATION(t) 	do {} while(0)
#endif /* CONFIG_SMP */

#define SET_PMU_OWNER(t, c)	do { pfm_get_cpu_var(pmu_owner) = (t); pfm_get_cpu_var(pmu_ctx) = (c); } while(0)
#define GET_PMU_OWNER()		pfm_get_cpu_var(pmu_owner)
#define GET_PMU_CTX()		pfm_get_cpu_var(pmu_ctx)

#define LOCK_PFS()	    	spin_lock(&pfm_sessions.pfs_lock)
#define UNLOCK_PFS()	    	spin_unlock(&pfm_sessions.pfs_lock)

#define PFM_REG_RETFLAG_SET(flags, val)	do { flags &= ~PFM_REG_RETFL_MASK; flags |= (val); } while(0)

#ifdef CONFIG_SMP
#define PFM_CPU_ONLINE_MAP	cpu_online_map
#define cpu_is_online(i)	(PFM_CPU_ONLINE_MAP & (1UL << i))
#else
#define PFM_CPU_ONLINE_MAP	 1UL
#define cpu_is_online(i)	(i==0)
#endif

/*
 * cmp0 must be the value of pmc0
 */
#define PMC0_HAS_OVFL(cmp0)  (cmp0 & ~0x1UL)

#define PFMFS_MAGIC 0xa0b4d889

/*
 * debugging
 */
#define DPRINT(a) \
	do { \
		if (unlikely(pfm_sysctl.debug >0)) { printk("%s.%d: CPU%d [%d] ", __FUNCTION__, __LINE__, smp_processor_id(), current->pid); printk a; } \
	} while (0)

#define DPRINT_ovfl(a) \
	do { \
		if (unlikely(pfm_sysctl.debug > 0 && pfm_sysctl.debug_ovfl >0)) { printk("%s.%d: CPU%d [%d] ", __FUNCTION__, __LINE__, smp_processor_id(), current->pid); printk a; } \
	} while (0)
/*
 * Architected PMC structure
 */
typedef struct {
	unsigned long pmc_plm:4;	/* privilege level mask */
	unsigned long pmc_ev:1;		/* external visibility */
	unsigned long pmc_oi:1;		/* overflow interrupt */
	unsigned long pmc_pm:1;		/* privileged monitor */
	unsigned long pmc_ig1:1;	/* reserved */
	unsigned long pmc_es:8;		/* event select */
	unsigned long pmc_ig2:48;	/* reserved */
} pfm_monitor_t;

/*
 * 64-bit software counter structure
 */
typedef struct {
	unsigned long	val;		/* virtual 64bit counter value */
	unsigned long	lval;		/* last reset value */
	unsigned long	long_reset;	/* reset value on sampling overflow */
	unsigned long	short_reset;    /* reset value on overflow */
	unsigned long	reset_pmds[4];  /* which other pmds to reset when this counter overflows */
	unsigned long	smpl_pmds[4];   /* which pmds are accessed when counter overflow */
	unsigned long	seed;		/* seed for random-number generator */
	unsigned long	mask;		/* mask for random-number generator */
	unsigned int 	flags;		/* notify/do not notify */
	unsigned int 	reserved;	/* for future use */
	unsigned long	eventid;	/* overflow event identifier */
} pfm_counter_t;

/*
 * context flags
 */
typedef struct {
	unsigned int block:1;		/* when 1, task will blocked on user notifications */
	unsigned int system:1;		/* do system wide monitoring */
	unsigned int using_dbreg:1;	/* using range restrictions (debug registers) */
	unsigned int is_sampling:1;	/* true if using a custom format */
	unsigned int excl_idle:1;	/* exclude idle task in system wide session */
	unsigned int unsecure:1;	/* exclude idle task in system wide session */
	unsigned int going_zombie:1;	/* context is zombie (MASKED+blocking) */
	unsigned int trap_reason:2;	/* reason for going into pfm_handle_work() */
	unsigned int no_msg:1;		/* no message sent on overflow */
	unsigned int reserved:22;
} pfm_context_flags_t;

#define PFM_TRAP_REASON_NONE		0x0	/* default value */
#define PFM_TRAP_REASON_BLOCK		0x1	/* we need to block on overflow */
#define PFM_TRAP_REASON_RESET		0x2	/* we need to reset PMDs */


/*
 * perfmon context: encapsulates all the state of a monitoring session
 */

typedef struct pfm_context {
	spinlock_t		ctx_lock;		/* context protection */

	pfm_context_flags_t	ctx_flags;		/* bitmask of flags  (block reason incl.) */
	unsigned int		ctx_state;		/* state: active/inactive (no bitfield) */

	struct task_struct 	*ctx_task;		/* task to which context is attached */

	unsigned long		ctx_ovfl_regs[4];	/* which registers overflowed (notification) */

	struct semaphore	ctx_restart_sem;   	/* use for blocking notification mode */

	unsigned long		ctx_used_pmds[4];	/* bitmask of PMD used            */
	unsigned long		ctx_all_pmds[4];	/* bitmask of all accessible PMDs */
	unsigned long		ctx_reload_pmds[4];	/* bitmask of force reload PMD on ctxsw in */

	unsigned long		ctx_all_pmcs[4];	/* bitmask of all accessible PMCs */
	unsigned long		ctx_reload_pmcs[4];	/* bitmask of force reload PMC on ctxsw in */
	unsigned long		ctx_used_monitors[4];	/* bitmask of monitor PMC being used */

	unsigned long		ctx_pmcs[IA64_NUM_PMC_REGS];	/*  saved copies of PMC values */

	unsigned int		ctx_used_ibrs[1];		/* bitmask of used IBR (speedup ctxsw in) */
	unsigned int		ctx_used_dbrs[1];		/* bitmask of used DBR (speedup ctxsw in) */
	unsigned long		ctx_dbrs[IA64_NUM_DBG_REGS];	/* DBR values (cache) when not loaded */
	unsigned long		ctx_ibrs[IA64_NUM_DBG_REGS];	/* IBR values (cache) when not loaded */

	pfm_counter_t		ctx_pmds[IA64_NUM_PMD_REGS]; /* software state for PMDS */

	u64			ctx_saved_psr_up;	/* only contains psr.up value */

	unsigned long		ctx_last_activation;	/* context last activation number for last_cpu */
	unsigned int		ctx_last_cpu;		/* CPU id of current or last CPU used (SMP only) */
	unsigned int		ctx_cpu;		/* cpu to which perfmon is applied (system wide) */

	int			ctx_fd;			/* file descriptor used my this context */

	pfm_buffer_fmt_t	*ctx_buf_fmt;		/* buffer format callbacks */
	void			*ctx_smpl_hdr;		/* points to sampling buffer header kernel vaddr */
	unsigned long		ctx_smpl_size;		/* size of sampling buffer */
	void			*ctx_smpl_vaddr;	/* user level virtual address of smpl buffer */

	wait_queue_head_t 	ctx_msgq_wait;
	pfm_msg_t		ctx_msgq[PFM_MAX_MSGS];
	int			ctx_msgq_head;
	int			ctx_msgq_tail;
	struct fasync_struct	*ctx_async_queue;

	wait_queue_head_t 	ctx_zombieq;		/* termination cleanup wait queue */
} pfm_context_t;

/*
 * magic number used to verify that structure is really
 * a perfmon context
 */
#define PFM_IS_FILE(f)		((f)->f_op == &pfm_file_ops)

#define PFM_GET_CTX(t)	 	((pfm_context_t *)(t)->thread.pfm_context)

#ifdef CONFIG_SMP
#define SET_LAST_CPU(ctx, v)	(ctx)->ctx_last_cpu = (v)
#define GET_LAST_CPU(ctx)	(ctx)->ctx_last_cpu
#else
#define SET_LAST_CPU(ctx, v)	do {} while(0)
#define GET_LAST_CPU(ctx)	do {} while(0)
#endif


#define ctx_fl_block		ctx_flags.block
#define ctx_fl_system		ctx_flags.system
#define ctx_fl_using_dbreg	ctx_flags.using_dbreg
#define ctx_fl_is_sampling	ctx_flags.is_sampling
#define ctx_fl_excl_idle	ctx_flags.excl_idle
#define ctx_fl_unsecure		ctx_flags.unsecure
#define ctx_fl_going_zombie	ctx_flags.going_zombie
#define ctx_fl_trap_reason	ctx_flags.trap_reason
#define ctx_fl_no_msg		ctx_flags.no_msg

#define PFM_SET_WORK_PENDING(t, v)	do { (t)->thread.pfm_needs_checking = v; } while(0);
#define PFM_GET_WORK_PENDING(t)		(t)->thread.pfm_needs_checking

/*
 * global information about all sessions
 * mostly used to synchronize between system wide and per-process
 */
typedef struct {
	spinlock_t		pfs_lock;		   /* lock the structure */

	unsigned int		pfs_task_sessions;	   /* number of per task sessions */
	unsigned int		pfs_sys_sessions;	   /* number of per system wide sessions */
	unsigned int		pfs_sys_use_dbregs;	   /* incremented when a system wide session uses debug regs */
	unsigned int		pfs_ptrace_use_dbregs;	   /* incremented when a process uses debug regs */
	struct task_struct	*pfs_sys_session[NR_CPUS]; /* point to task owning a system-wide session */
} pfm_session_t;

/*
 * information about a PMC or PMD.
 * dep_pmd[]: a bitmask of dependent PMD registers
 * dep_pmc[]: a bitmask of dependent PMC registers
 */
typedef struct {
	unsigned int		type;
	int			pm_pos;
	unsigned long		default_value;	/* power-on default value */
	unsigned long		reserved_mask;	/* bitmask of reserved bits */
	int			(*read_check)(struct task_struct *task, pfm_context_t *ctx, unsigned int cnum, unsigned long *val, struct pt_regs *regs);
	int			(*write_check)(struct task_struct *task, pfm_context_t *ctx, unsigned int cnum, unsigned long *val, struct pt_regs *regs);
	unsigned long		dep_pmd[4];
	unsigned long		dep_pmc[4];
} pfm_reg_desc_t;

/* assume cnum is a valid monitor */
#define PMC_PM(cnum, val)	(((val) >> (pmu_conf.pmc_desc[cnum].pm_pos)) & 0x1)
#define PMC_WR_FUNC(cnum)	(pmu_conf.pmc_desc[cnum].write_check)
#define PMD_WR_FUNC(cnum)	(pmu_conf.pmd_desc[cnum].write_check)
#define PMD_RD_FUNC(cnum)	(pmu_conf.pmd_desc[cnum].read_check)

/*
 * This structure is initialized at boot time and contains
 * a description of the PMU main characteristics.
 */
typedef struct {
	unsigned long  ovfl_val;	/* overflow value for counters */

	pfm_reg_desc_t *pmc_desc;	/* detailed PMC register dependencies descriptions */
	pfm_reg_desc_t *pmd_desc;	/* detailed PMD register dependencies descriptions */

	unsigned int   num_pmcs;	/* number of PMCS: computed at init time */
	unsigned int   num_pmds;	/* number of PMDS: computed at init time */
	unsigned long  impl_pmcs[4];	/* bitmask of implemented PMCS */
	unsigned long  impl_pmds[4];	/* bitmask of implemented PMDS */

	char	      *pmu_name;	/* PMU family name */
	unsigned int  enabled;		/* indicates if perfmon initialized properly */
	unsigned int  pmu_family;	/* cpuid family pattern used to identify pmu */

	unsigned int  num_ibrs;		/* number of IBRS: computed at init time */
	unsigned int  num_dbrs;		/* number of DBRS: computed at init time */
	unsigned int  num_counters;	/* PMC/PMD counting pairs : computed at init time */

	unsigned int  use_rr_dbregs:1;	/* set if debug registers used for range restriction */
} pmu_config_t;

/*
 * debug register related type definitions
 */
typedef struct {
	unsigned long ibr_mask:56;
	unsigned long ibr_plm:4;
	unsigned long ibr_ig:3;
	unsigned long ibr_x:1;
} ibr_mask_reg_t;

typedef struct {
	unsigned long dbr_mask:56;
	unsigned long dbr_plm:4;
	unsigned long dbr_ig:2;
	unsigned long dbr_w:1;
	unsigned long dbr_r:1;
} dbr_mask_reg_t;

typedef union {
	unsigned long  val;
	ibr_mask_reg_t ibr;
	dbr_mask_reg_t dbr;
} dbreg_t;


/*
 * perfmon command descriptions
 */
typedef struct {
	int		(*cmd_func)(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);
	char		*cmd_name;
	int		cmd_flags;
	unsigned int	cmd_narg;
	size_t		cmd_argsize;
	int		(*cmd_getsize)(void *arg, size_t *sz);
} pfm_cmd_desc_t;

#define PFM_CMD_FD		0x01	/* command requires a file descriptor */
#define PFM_CMD_ARG_READ	0x02	/* command must read argument(s) */
#define PFM_CMD_ARG_RW		0x04	/* command must read/write argument(s) */
#define PFM_CMD_STOP		0x08	/* command does not work on zombie context */


#define PFM_CMD_IDX(cmd)	(cmd)
#define PFM_CMD_IS_VALID(cmd)	((PFM_CMD_IDX(cmd) >= 0) && (PFM_CMD_IDX(cmd) < PFM_CMD_COUNT) \
				  && pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_func != NULL)

#define PFM_CMD_NAME(cmd)	pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_name
#define PFM_CMD_READ_ARG(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_ARG_READ)
#define PFM_CMD_RW_ARG(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_ARG_RW)
#define PFM_CMD_USE_FD(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_FD)
#define PFM_CMD_STOPPED(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_flags & PFM_CMD_STOP)

#define PFM_CMD_ARG_MANY	-1 /* cannot be zero */
#define PFM_CMD_NARG(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_narg)
#define PFM_CMD_ARG_SIZE(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_argsize)
#define PFM_CMD_GETSIZE(cmd)	(pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_getsize)

typedef struct {
	int	debug;		/* turn on/off debugging via syslog */
	int	debug_ovfl;	/* turn on/off debug printk in overflow handler */
	int	fastctxsw;	/* turn on/off fast (unsecure) ctxsw */
	int 	debug_pfm_read;
} pfm_sysctl_t;

typedef struct {
	unsigned long pfm_spurious_ovfl_intr_count;	/* keep track of spurious ovfl interrupts */
	unsigned long pfm_ovfl_intr_count; 		/* keep track of ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles;		/* cycles spent processing ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles_min;		/* min cycles spent processing ovfl interrupts */
	unsigned long pfm_ovfl_intr_cycles_max;		/* max cycles spent processing ovfl interrupts */
	unsigned long pfm_sysupdt_count;
	unsigned long pfm_sysupdt_cycles;
	unsigned long pfm_smpl_handler_calls;
	unsigned long pfm_smpl_handler_cycles;
	char pad[SMP_CACHE_BYTES] ____cacheline_aligned;
} pfm_stats_t;

/*
 * perfmon internal variables
 */
static pfm_stats_t		pfm_stats[NR_CPUS];
static pfm_session_t		pfm_sessions;	/* global sessions information */

static struct proc_dir_entry 	*perfmon_dir;
static pfm_uuid_t		pfm_null_uuid = {0,};

static spinlock_t		pfm_smpl_fmt_lock;
static pfm_buffer_fmt_t		*pfm_buffer_fmt_list;
#define LOCK_BUF_FMT_LIST()	    spin_lock(&pfm_smpl_fmt_lock)
#define UNLOCK_BUF_FMT_LIST()	    spin_unlock(&pfm_smpl_fmt_lock)

/* sysctl() controls */
static pfm_sysctl_t pfm_sysctl;
int pfm_debug_var;

static ctl_table pfm_ctl_table[]={
	{1, "debug", &pfm_sysctl.debug, sizeof(int), 0666, NULL, &proc_dointvec, NULL,},
	{2, "debug_ovfl", &pfm_sysctl.debug_ovfl, sizeof(int), 0666, NULL, &proc_dointvec, NULL,},
	{3, "fastctxsw", &pfm_sysctl.fastctxsw, sizeof(int), 0600, NULL, &proc_dointvec, NULL,},
	{ 0, },
};
static ctl_table pfm_sysctl_dir[] = {
	{1, "perfmon", NULL, 0, 0755, pfm_ctl_table, },
 	{0,},
};
static ctl_table pfm_sysctl_root[] = {
	{1, "kernel", NULL, 0, 0755, pfm_sysctl_dir, },
 	{0,},
};
static struct ctl_table_header *pfm_sysctl_header;

static void pfm_vm_close(struct vm_area_struct * area);

static struct vm_operations_struct pfm_vm_ops={
	close: pfm_vm_close
};


#define pfm_wait_task_inactive(t)	wait_task_inactive(t)
#define pfm_get_cpu_var(v)		__get_cpu_var(v)
#define pfm_get_cpu_data(a,b)		per_cpu(a, b)
typedef	irqreturn_t	pfm_irq_handler_t;
#define PFM_IRQ_HANDLER_RET(v)	do {  \
		put_cpu_no_resched(); \
		return IRQ_HANDLED;   \
	} while(0);

static inline void
pfm_put_task(struct task_struct *task)
{
	if (task != current) put_task_struct(task);
}

static inline void
pfm_set_task_notify(struct task_struct *task)
{
	struct thread_info *info;

	info = (struct thread_info *) ((char *) task + IA64_TASK_SIZE);
	set_bit(TIF_NOTIFY_RESUME, &info->flags);
}

static inline void
pfm_clear_task_notify(void)
{
	clear_thread_flag(TIF_NOTIFY_RESUME);
}

static inline void
pfm_reserve_page(unsigned long a)
{
	SetPageReserved(vmalloc_to_page((void *)a));
}
static inline void
pfm_unreserve_page(unsigned long a)
{
	ClearPageReserved(vmalloc_to_page((void*)a));
}

static inline int
pfm_remap_page_range(struct vm_area_struct *vma, unsigned long from, unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	return remap_page_range(vma, from, phys_addr, size, prot);
}

static inline unsigned long
pfm_protect_ctx_ctxsw(pfm_context_t *x)
{
	spin_lock(&(x)->ctx_lock);
	return 0UL;
}

static inline unsigned long
pfm_unprotect_ctx_ctxsw(pfm_context_t *x, unsigned long f)
{
	spin_unlock(&(x)->ctx_lock);
}

static inline unsigned int
pfm_do_munmap(struct mm_struct *mm, unsigned long addr, size_t len, int acct)
{
	return do_munmap(mm, addr, len);
}

static inline unsigned long 
pfm_get_unmapped_area(struct file *file, unsigned long addr, unsigned long len, unsigned long pgoff, unsigned long flags, unsigned long exec)
{
	return get_unmapped_area(file, addr, len, pgoff, flags);
}


static struct super_block *
pfmfs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "pfm:", NULL, PFMFS_MAGIC);
}

static struct file_system_type pfm_fs_type = {
	.name     = "pfmfs",
	.get_sb   = pfmfs_get_sb,
	.kill_sb  = kill_anon_super,
};

DEFINE_PER_CPU(unsigned long, pfm_syst_info);
DEFINE_PER_CPU(struct task_struct *, pmu_owner);
DEFINE_PER_CPU(pfm_context_t  *, pmu_ctx);
DEFINE_PER_CPU(unsigned long, pmu_activation_number);


/* forward declaration */
static struct file_operations pfm_file_ops;

/*
 * forward declarations
 */
#ifndef CONFIG_SMP
static void pfm_lazy_save_regs (struct task_struct *ta);
#endif

#if   defined(CONFIG_ITANIUM)
#include "perfmon_itanium.h"
#elif defined(CONFIG_MCKINLEY)
#include "perfmon_mckinley.h"
#else
#include "perfmon_generic.h"
#endif

static int pfm_end_notify_user(pfm_context_t *ctx);

static inline void
pfm_clear_psr_pp(void)
{
	__asm__ __volatile__ ("rsm psr.pp;; srlz.i;;"::: "memory");
}

static inline void
pfm_set_psr_pp(void)
{
	__asm__ __volatile__ ("ssm psr.pp;; srlz.i;;"::: "memory");
}

static inline void
pfm_clear_psr_up(void)
{
	__asm__ __volatile__ ("rsm psr.up;; srlz.i;;"::: "memory");
}

static inline void
pfm_set_psr_up(void)
{
	__asm__ __volatile__ ("ssm psr.up;; srlz.i;;"::: "memory");
}

static inline unsigned long
pfm_get_psr(void)
{
	unsigned long tmp;
	__asm__ __volatile__ ("mov %0=psr;;": "=r"(tmp) :: "memory");
	return tmp;
}

static inline void
pfm_set_psr_l(unsigned long val)
{
	__asm__ __volatile__ ("mov psr.l=%0;; srlz.i;;"::"r"(val): "memory");
}

static inline void
pfm_freeze_pmu(void)
{
	ia64_set_pmc(0,1UL);
	ia64_srlz_d();
}

static inline void
pfm_unfreeze_pmu(void)
{
	ia64_set_pmc(0,0UL);
	ia64_srlz_d();
}

/*
 * PMD[i] must be a counter. no check is made
 */
static inline unsigned long
pfm_read_soft_counter(pfm_context_t *ctx, int i)
{
	return ctx->ctx_pmds[i].val + (ia64_get_pmd(i) & pmu_conf.ovfl_val);
}

/*
 * PMD[i] must be a counter. no check is made
 */
static inline void
pfm_write_soft_counter(pfm_context_t *ctx, int i, unsigned long val)
{
	ctx->ctx_pmds[i].val = val  & ~pmu_conf.ovfl_val;
	/*
	 * writing to unimplemented part is ignore, so we do not need to
	 * mask off top part
	 */
	ia64_set_pmd(i, val & pmu_conf.ovfl_val);
}

static pfm_msg_t *
pfm_get_new_msg(pfm_context_t *ctx)
{
	int idx, next;

	next = (ctx->ctx_msgq_tail+1) % PFM_MAX_MSGS;

	DPRINT(("ctx_fd=%p head=%d tail=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail));
	if (next == ctx->ctx_msgq_head) return NULL;

 	idx = 	ctx->ctx_msgq_tail;
	ctx->ctx_msgq_tail = next;

	DPRINT(("ctx=%p head=%d tail=%d msg=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail, idx));

	return ctx->ctx_msgq+idx;
}

static pfm_msg_t *
pfm_get_next_msg(pfm_context_t *ctx)
{
	pfm_msg_t *msg;

	DPRINT(("ctx=%p head=%d tail=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail));

	if (PFM_CTXQ_EMPTY(ctx)) return NULL;

	/*
	 * get oldest message
	 */
	msg = ctx->ctx_msgq+ctx->ctx_msgq_head;

	/*
	 * and move forward
	 */
	ctx->ctx_msgq_head = (ctx->ctx_msgq_head+1) % PFM_MAX_MSGS;

	DPRINT(("ctx=%p head=%d tail=%d type=%d\n", ctx, ctx->ctx_msgq_head, ctx->ctx_msgq_tail, msg->pfm_gen_msg.msg_type));

	return msg;
}

static void
pfm_reset_msgq(pfm_context_t *ctx)
{
	ctx->ctx_msgq_head = ctx->ctx_msgq_tail = 0;
	DPRINT(("ctx=%p msgq reset\n", ctx));
}


/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long
pfm_kvirt_to_pa(unsigned long adr)
{
	__u64 pa = ia64_tpa(adr);
	return pa;
}

static void *
pfm_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long addr;

	size = PAGE_ALIGN(size);
	mem  = vmalloc(size);
	if (mem) {
		//printk("perfmon: CPU%d pfm_rvmalloc(%ld)=%p\n", smp_processor_id(), size, mem);
		memset(mem, 0, size);
		addr = (unsigned long)mem;
		while (size > 0) {
			pfm_reserve_page(addr);
			addr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void
pfm_rvfree(void *mem, unsigned long size)
{
	unsigned long addr;

	if (mem) {
		DPRINT(("freeing physical buffer @%p size=%lu\n", mem, size));
		addr = (unsigned long) mem;
		while ((long) size > 0) {
			pfm_unreserve_page(addr);
			addr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
	return;
}

static pfm_context_t *
pfm_context_alloc(void)
{
	pfm_context_t *ctx;

	/* allocate context descriptor */
	ctx = kmalloc(sizeof(pfm_context_t), GFP_KERNEL);
	if (ctx) {
		memset(ctx, 0, sizeof(pfm_context_t));
		DPRINT(("alloc ctx @%p\n", ctx));
	}
	return ctx;
}

static void
pfm_context_free(pfm_context_t *ctx)
{
	if (ctx) {
		DPRINT(("free ctx @%p\n", ctx));
		kfree(ctx);
	}
}

static void
pfm_mask_monitoring(struct task_struct *task)
{
	pfm_context_t *ctx = PFM_GET_CTX(task);
	struct thread_struct *th = &task->thread;
	unsigned long mask, val;
	int i;

	DPRINT(("[%d] masking monitoring for [%d]\n", current->pid, task->pid));

	/*
	 * monitoring can only be masked as a result of a valid
	 * counter overflow. In UP, it means that the PMU still
	 * has an owner. Note that the owner can be different
	 * from the current task. However the PMU state belongs
	 * to the owner.
	 * In SMP, a valid overflow only happens when task is
	 * current. Therefore if we come here, we know that
	 * the PMU state belongs to the current task, therefore
	 * we can access the live registers.
	 *
	 * So in both cases, the live register contains the owner's
	 * state. We can ONLY touch the PMU registers and NOT the PSR.
	 *
	 * As a consequence to this call, the thread->pmds[] array
	 * contains stale information which must be ignored
	 * when context is reloaded AND monitoring is active (see
	 * pfm_restart).
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i = 0; mask; i++, mask>>=1) {
		/* skip non used pmds */
		if ((mask & 0x1) == 0) continue;
		val = ia64_get_pmd(i);

		if (PMD_IS_COUNTING(i)) {
			/*
		 	 * we rebuild the full 64 bit value of the counter
		 	 */
			ctx->ctx_pmds[i].val += (val & pmu_conf.ovfl_val);
		} else {
			ctx->ctx_pmds[i].val = val;
		}
		DPRINT(("pmd[%d]=0x%lx hw_pmd=0x%lx\n",
			i,
			ctx->ctx_pmds[i].val,
			val & pmu_conf.ovfl_val));
	}
	/*
	 * mask monitoring by setting the privilege level to 0
	 * we cannot use psr.pp/psr.up for this, it is controlled by
	 * the user
	 *
	 * if task is current, modify actual registers, otherwise modify
	 * thread save state, i.e., what will be restored in pfm_load_regs()
	 */
	mask = ctx->ctx_used_monitors[0] >> PMU_FIRST_COUNTER;
	for(i= PMU_FIRST_COUNTER; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0UL) continue;
		ia64_set_pmc(i, th->pmcs[i] & ~0xfUL);
		th->pmcs[i] &= ~0xfUL;
	}
	/*
	 * make all of this visible
	 */
	ia64_srlz_d();
}

/*
 * must always be done with task == current
 *
 * context must be in MASKED state when calling
 */
static void
pfm_restore_monitoring(struct task_struct *task)
{
	pfm_context_t *ctx = PFM_GET_CTX(task);
	struct thread_struct *th = &task->thread;
	unsigned long mask;
	unsigned long psr, val;
	int i;

	if (task != current) {
		printk(KERN_ERR "perfmon.%d: invalid task[%d] current[%d]\n", __LINE__, task->pid, current->pid);
		return;
	}
	if (CTX_IS_MASKED(ctx) == 0) {
		printk(KERN_ERR "perfmon.%d: task[%d] current[%d] invalid state=%d\n", __LINE__,
			task->pid, current->pid, ctx->ctx_state);
		return;
	}
	psr = pfm_get_psr();
	/*
	 * monitoring is masked via the PMC.
	 * As we restore their value, we do not want each counter to
	 * restart right away. We stop monitoring using the PSR,
	 * restore the PMC (and PMD) and then re-establish the psr
	 * as it was. Note that there can be no pending overflow at
	 * this point, because monitoring was MASKED.
	 *
	 * system-wide session are pinned and self-monitoring
	 */
	if (ctx->ctx_fl_system && (PFM_CPUINFO_GET() & PFM_CPUINFO_DCR_PP)) {
		/* disable dcr pp */
		ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);
		pfm_clear_psr_pp();
	} else {
		pfm_clear_psr_up();
	}
	/*
	 * first, we restore the PMD
	 */
	mask = ctx->ctx_used_pmds[0];
	for (i = 0; mask; i++, mask>>=1) {
		/* skip non used pmds */
		if ((mask & 0x1) == 0) continue;

		if (PMD_IS_COUNTING(i)) {
			/*
			 * we split the 64bit value according to
			 * counter width
			 */
			val = ctx->ctx_pmds[i].val & pmu_conf.ovfl_val;
			ctx->ctx_pmds[i].val &= ~pmu_conf.ovfl_val;
		} else {
			val = ctx->ctx_pmds[i].val;
		}
		ia64_set_pmd(i, val);

		DPRINT(("pmd[%d]=0x%lx hw_pmd=0x%lx\n",
			i,
			ctx->ctx_pmds[i].val,
			val));
	}
	/*
	 * restore the PMCs
	 */
	mask = ctx->ctx_used_monitors[0] >> PMU_FIRST_COUNTER;
	for(i= PMU_FIRST_COUNTER; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0UL) continue;
		th->pmcs[i] = ctx->ctx_pmcs[i];
		ia64_set_pmc(i, th->pmcs[i]);
		DPRINT(("[%d] pmc[%d]=0x%lx\n", task->pid, i, th->pmcs[i]));
	}
	ia64_srlz_d();

	/*
	 * now restore PSR
	 */
	if (ctx->ctx_fl_system && (PFM_CPUINFO_GET() & PFM_CPUINFO_DCR_PP)) {
		/* enable dcr pp */
		ia64_set_dcr(ia64_get_dcr() | IA64_DCR_PP);
		ia64_srlz_i();
	}
	pfm_set_psr_l(psr);
}

static inline void
pfm_save_pmds(unsigned long *pmds, unsigned long mask)
{
	int i;

	ia64_srlz_d();

	for (i=0; mask; i++, mask>>=1) {
		if (mask & 0x1) pmds[i] = ia64_get_pmd(i);
	}
}

/*
 * reload from thread state (used for ctxw only)
 */
static inline void
pfm_restore_pmds(unsigned long *pmds, unsigned long mask)
{
	int i;
	unsigned long val, ovfl_val = pmu_conf.ovfl_val;

	DPRINT(("mask=0x%lx\n", mask));
	for (i=0; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0) continue;
		val = PMD_IS_COUNTING(i) ? pmds[i] & ovfl_val : pmds[i];
		ia64_set_pmd(i, val);
		DPRINT(("pmd[%d]=0x%lx\n", i, val));
	}
	ia64_srlz_d();
}

/*
 * propagate PMD from context to thread-state
 */
static inline void
pfm_copy_pmds(struct task_struct *task, pfm_context_t *ctx)
{
	struct thread_struct *thread = &task->thread;
	unsigned long ovfl_val = pmu_conf.ovfl_val;
	unsigned long mask = ctx->ctx_all_pmds[0];
	unsigned long val;
	int i;

	DPRINT(("mask=0x%lx\n", mask));

	for (i=0; mask; i++, mask>>=1) {

		val = ctx->ctx_pmds[i].val;

		/*
		 * We break up the 64 bit value into 2 pieces
		 * the lower bits go to the machine state in the
		 * thread (will be reloaded on ctxsw in).
		 * The upper part stays in the soft-counter.
		 */
		if (PMD_IS_COUNTING(i)) {
			ctx->ctx_pmds[i].val = val & ~ovfl_val;
			 val &= ovfl_val;
		}
		thread->pmds[i] = val;

		DPRINT(("pmd[%d]=0x%lx soft_val=0x%lx\n",
			i,
			thread->pmds[i],
			ctx->ctx_pmds[i].val));
	}
}

/*
 * propagate PMC from context to thread-state
 */
static inline void
pfm_copy_pmcs(struct task_struct *task, pfm_context_t *ctx)
{
	struct thread_struct *thread = &task->thread;
	unsigned long mask = ctx->ctx_all_pmcs[0];
	int i;

	DPRINT(("mask=0x%lx\n", mask));

	for (i=0; mask; i++, mask>>=1) {
		/* masking 0 with ovfl_val yields 0 */
		thread->pmcs[i] = ctx->ctx_pmcs[i];
		DPRINT(("pmc[%d]=0x%lx\n", i, thread->pmcs[i]));
	}
}



static inline void
pfm_restore_pmcs(unsigned long *pmcs, unsigned long mask)
{
	int i;

	DPRINT(("mask=0x%lx\n", mask));
	for (i=0; mask; i++, mask>>=1) {
		if ((mask & 0x1) == 0) continue;
		ia64_set_pmc(i, pmcs[i]);
		DPRINT(("pmc[%d]=0x%lx\n", i, pmcs[i]));
	}
	ia64_srlz_d();
}

static inline void
pfm_restore_ibrs(unsigned long *ibrs, unsigned int nibrs)
{
	int i;

	for (i=0; i < nibrs; i++) {
		ia64_set_ibr(i, ibrs[i]);
	}
	ia64_srlz_i();
}

static inline void
pfm_restore_dbrs(unsigned long *dbrs, unsigned int ndbrs)
{
	int i;

	for (i=0; i < ndbrs; i++) {
		ia64_set_dbr(i, dbrs[i]);
	}
	ia64_srlz_d();
}

static inline int
pfm_uuid_cmp(pfm_uuid_t a, pfm_uuid_t b)
{
	return memcmp(a, b, sizeof(pfm_uuid_t));
}

static inline int
pfm_buf_fmt_exit(pfm_buffer_fmt_t *fmt, struct task_struct *task, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_exit) ret = (*fmt->fmt_exit)(task, buf, regs);
	return ret;
}

static inline int
pfm_buf_fmt_getsize(pfm_buffer_fmt_t *fmt, struct task_struct *task, unsigned int flags, int cpu, void *arg, unsigned long *size)
{
	int ret = 0;
	if (fmt->fmt_getsize) ret = (*fmt->fmt_getsize)(task, flags, cpu, arg, size);
	return ret;
}


static inline int
pfm_buf_fmt_validate(pfm_buffer_fmt_t *fmt, struct task_struct *task, unsigned int flags,
		     int cpu, void *arg)
{
	int ret = 0;
	if (fmt->fmt_validate) ret = (*fmt->fmt_validate)(task, flags, cpu, arg);
	return ret;
}

static inline int
pfm_buf_fmt_init(pfm_buffer_fmt_t *fmt, struct task_struct *task, void *buf, unsigned int flags,
		     int cpu, void *arg)
{
	int ret = 0;
	if (fmt->fmt_init) ret = (*fmt->fmt_init)(task, buf, flags, cpu, arg);
	return ret;
}

static inline int
pfm_buf_fmt_restart(pfm_buffer_fmt_t *fmt, struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_restart) ret = (*fmt->fmt_restart)(task, ctrl, buf, regs);
	return ret;
}

static inline int
pfm_buf_fmt_restart_active(pfm_buffer_fmt_t *fmt, struct task_struct *task, pfm_ovfl_ctrl_t *ctrl, void *buf, struct pt_regs *regs)
{
	int ret = 0;
	if (fmt->fmt_restart_active) ret = (*fmt->fmt_restart_active)(task, ctrl, buf, regs);
	return ret;
}



int
pfm_register_buffer_fmt(pfm_buffer_fmt_t *fmt)
{
	pfm_buffer_fmt_t *p;
	int ret = 0;

	/* some sanity checks */
	if (fmt == NULL || fmt->fmt_name == NULL) return -EINVAL;

	/* we need at least a handler */
	if (fmt->fmt_handler == NULL) return -EINVAL;

	/*
	 * XXX: need check validity of fmt_arg_size
	 */

	LOCK_BUF_FMT_LIST();
	p = pfm_buffer_fmt_list;


	while (p) {
		if (pfm_uuid_cmp(fmt->fmt_uuid, p->fmt_uuid) == 0) break;
		p = p->fmt_next;
	}

	if (p) {
		printk(KERN_ERR "perfmon: duplicate sampling format: %s\n", fmt->fmt_name);
		ret = -EBUSY;
	} else {
		fmt->fmt_prev = NULL;
		fmt->fmt_next = pfm_buffer_fmt_list;
		pfm_buffer_fmt_list = fmt;
		printk(KERN_ERR "perfmon: added sampling format %s\n", fmt->fmt_name);
	}
	UNLOCK_BUF_FMT_LIST();

	return ret;
}

int
pfm_unregister_buffer_fmt(pfm_uuid_t uuid)
{
	pfm_buffer_fmt_t *p;
	int ret = 0;

	LOCK_BUF_FMT_LIST();
	p = pfm_buffer_fmt_list;
	while (p) {
		if (memcmp(uuid, p->fmt_uuid, sizeof(pfm_uuid_t)) == 0) break;
		p = p->fmt_next;
	}
	if (p) {
		if (p->fmt_prev)
			p->fmt_prev->fmt_next = p->fmt_next;
		else
			pfm_buffer_fmt_list = p->fmt_next;

		if (p->fmt_next)
			p->fmt_next->fmt_prev = p->fmt_prev;

		printk(KERN_ERR "perfmon: removed sampling format: %s\n",  p->fmt_name);
		p->fmt_next = p->fmt_prev = NULL;
	} else {
		printk(KERN_ERR "perfmon: cannot unregister format, not found\n");
		ret = -EINVAL;
	}
	UNLOCK_BUF_FMT_LIST();

	return ret;

}

/*
 * find a buffer format based on its uuid
 */
static pfm_buffer_fmt_t *
pfm_find_buffer_fmt(pfm_uuid_t uuid, int nolock)
{
	pfm_buffer_fmt_t *p;

	LOCK_BUF_FMT_LIST();
	for (p = pfm_buffer_fmt_list; p ; p = p->fmt_next) {
		if (pfm_uuid_cmp(uuid, p->fmt_uuid) == 0) break;
	}

	UNLOCK_BUF_FMT_LIST();

	return p;
}

static int
pfm_reserve_session(struct task_struct *task, int is_syswide, unsigned int cpu)
{
	/*
	 * validy checks on cpu_mask have been done upstream
	 */
	LOCK_PFS();

	DPRINT(("in sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	if (is_syswide) {
		/*
		 * cannot mix system wide and per-task sessions
		 */
		if (pfm_sessions.pfs_task_sessions > 0UL) {
			DPRINT(("system wide not possible, %u conflicting task_sessions\n",
			  	pfm_sessions.pfs_task_sessions));
			goto abort;
		}

		if (pfm_sessions.pfs_sys_session[cpu]) goto error_conflict;

		DPRINT(("reserving system wide session on CPU%u currently on CPU%u\n", cpu, smp_processor_id()));

		pfm_sessions.pfs_sys_session[cpu] = task;

		pfm_sessions.pfs_sys_sessions++ ;

	} else {
		if (pfm_sessions.pfs_sys_sessions) goto abort;
		pfm_sessions.pfs_task_sessions++;
	}

	DPRINT(("out sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	UNLOCK_PFS();

	return 0;

error_conflict:
	DPRINT(("system wide not possible, conflicting session [%d] on CPU%d\n",
  		pfm_sessions.pfs_sys_session[cpu]->pid,
		smp_processor_id()));
abort:
	UNLOCK_PFS();

	return -EBUSY;

}

static int
pfm_unreserve_session(pfm_context_t *ctx, int is_syswide, unsigned int cpu)
{

	/*
	 * validy checks on cpu_mask have been done upstream
	 */
	LOCK_PFS();

	DPRINT(("in sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));


	if (is_syswide) {
		pfm_sessions.pfs_sys_session[cpu] = NULL;
		/*
		 * would not work with perfmon+more than one bit in cpu_mask
		 */
		if (ctx && ctx->ctx_fl_using_dbreg) {
			if (pfm_sessions.pfs_sys_use_dbregs == 0) {
				printk(KERN_ERR "perfmon: invalid release for ctx %p sys_use_dbregs=0\n", ctx);
			} else {
				pfm_sessions.pfs_sys_use_dbregs--;
			}
		}
		pfm_sessions.pfs_sys_sessions--;
	} else {
		pfm_sessions.pfs_task_sessions--;
	}
	DPRINT(("out sys_sessions=%u task_sessions=%u dbregs=%u syswide=%d cpu=%u\n",
		pfm_sessions.pfs_sys_sessions,
		pfm_sessions.pfs_task_sessions,
		pfm_sessions.pfs_sys_use_dbregs,
		is_syswide,
		cpu));

	UNLOCK_PFS();

	return 0;
}

/*
 * removes virtual mapping of the sampling buffer.
 * IMPORTANT: cannot be called with interrupts disable, e.g. inside
 * a PROTECT_CTX() section.
 */
static int
pfm_remove_smpl_mapping(struct task_struct *task, void *vaddr, unsigned long size)
{
	int r;

	/* sanity checks */
	if (task->mm == NULL || size == 0UL || vaddr == NULL) {
		printk(KERN_ERR "perfmon: pfm_remove_smpl_mapping [%d] invalid context mm=%p\n", task->pid, task->mm);
		return -EINVAL;
	}

	DPRINT(("smpl_vaddr=%p size=%lu\n", vaddr, size));

	/*
	 * does the actual unmapping
	 */
	down_write(&task->mm->mmap_sem);

	DPRINT(("down_write done smpl_vaddr=%p size=%lu\n", vaddr, size));

	r = pfm_do_munmap(task->mm, (unsigned long)vaddr, size, 0);

	up_write(&task->mm->mmap_sem);
	if (r !=0) {
		printk(KERN_ERR "perfmon: [%d] unable to unmap sampling buffer @%p size=%lu\n", task->pid, vaddr, size);
	}

	DPRINT(("do_unmap(%p, %lu)=%d\n", vaddr, size, r));

	return 0;
}

/*
 * free actual physical storage used by sampling buffer
 */
#if 0
static int
pfm_free_smpl_buffer(pfm_context_t *ctx)
{
	pfm_buffer_fmt_t *fmt;

	if (ctx->ctx_smpl_hdr == NULL) goto invalid_free;

	/*
	 * we won't use the buffer format anymore
	 */
	fmt = ctx->ctx_buf_fmt;

	DPRINT(("sampling buffer @%p size %lu vaddr=%p\n",
		ctx->ctx_smpl_hdr,
		ctx->ctx_smpl_size,
		ctx->ctx_smpl_vaddr));

	pfm_buf_fmt_exit(fmt, current, NULL, NULL);

	/*
	 * free the buffer
	 */
	pfm_rvfree(ctx->ctx_smpl_hdr, ctx->ctx_smpl_size);

	ctx->ctx_smpl_hdr  = NULL;
	ctx->ctx_smpl_size = 0UL;

	return 0;

invalid_free:
	printk(KERN_ERR "perfmon: pfm_free_smpl_buffer [%d] no buffer\n", current->pid);
	return -EINVAL;
}
#endif

static inline void
pfm_exit_smpl_buffer(pfm_buffer_fmt_t *fmt)
{
	if (fmt == NULL) return;

	pfm_buf_fmt_exit(fmt, current, NULL, NULL);

}

/*
 * pfmfs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pfm: will go nicely and kill the special-casing in procfs.
 */
static struct vfsmount *pfmfs_mnt;

static int __init
init_pfm_fs(void)
{
	int err = register_filesystem(&pfm_fs_type);
	if (!err) {
		pfmfs_mnt = kern_mount(&pfm_fs_type);
		err = PTR_ERR(pfmfs_mnt);
		if (IS_ERR(pfmfs_mnt))
			unregister_filesystem(&pfm_fs_type);
		else
			err = 0;
	}
	return err;
}

static void __exit
exit_pfm_fs(void)
{
	unregister_filesystem(&pfm_fs_type);
	mntput(pfmfs_mnt);
}

static loff_t
pfm_lseek(struct file *file, loff_t offset, int whence)
{
	DPRINT(("pfm_lseek called\n"));
	return -ESPIPE;
}

static ssize_t
pfm_do_read(struct file *filp, char *buf, size_t size, loff_t *ppos)
{
	pfm_context_t *ctx;
	pfm_msg_t *msg;
	ssize_t ret;
	unsigned long flags;
  	DECLARE_WAITQUEUE(wait, current);
	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_poll: bad magic [%d]\n", current->pid);
		return -EINVAL;
	}

	ctx = (pfm_context_t *)filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_read: NULL ctx [%d]\n", current->pid);
		return -EINVAL;
	}

	/*
	 * check even when there is no message
	 */
	if (size < sizeof(pfm_msg_t)) {
		DPRINT(("message is too small ctx=%p (>=%ld)\n", ctx, sizeof(pfm_msg_t)));
		return -EINVAL;
	}
	/*
	 * seeks are not allowed on message queues
	 */
	if (ppos != &filp->f_pos) return -ESPIPE;

	PROTECT_CTX(ctx, flags);

  	/*
	 * put ourselves on the wait queue
	 */
  	add_wait_queue(&ctx->ctx_msgq_wait, &wait);


  	for(;;) {
		/*
		 * check wait queue
		 */

  		set_current_state(TASK_INTERRUPTIBLE);

		DPRINT(("head=%d tail=%d\n", ctx->ctx_msgq_head, ctx->ctx_msgq_tail));

		ret = 0;
		if(PFM_CTXQ_EMPTY(ctx) == 0) break;

		UNPROTECT_CTX(ctx, flags);

		/*
		 * check non-blocking read
		 */
      		ret = -EAGAIN;
		if(filp->f_flags & O_NONBLOCK) break;

		/*
		 * check pending signals
		 */
		if(signal_pending(current)) {
			ret = -EINTR;
			break;
		}
      		/*
		 * no message, so wait
		 */
      		schedule();

		PROTECT_CTX(ctx, flags);
	}
	DPRINT(("[%d] back to running ret=%ld\n", current->pid, ret));
  	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ctx->ctx_msgq_wait, &wait);

	if (ret < 0) goto abort;

	ret = -EINVAL;
	msg = pfm_get_next_msg(ctx);
	if (msg == NULL) {
		printk(KERN_ERR "perfmon: pfm_read no msg for ctx=%p [%d]\n", ctx, current->pid);
		goto abort_locked;
	}

	DPRINT(("[%d] fd=%d type=%d\n", current->pid, msg->pfm_gen_msg.msg_ctx_fd, msg->pfm_gen_msg.msg_type));

	ret = -EFAULT;
  	if(copy_to_user(buf, msg, sizeof(pfm_msg_t)) == 0) ret = sizeof(pfm_msg_t);

abort_locked:
	UNPROTECT_CTX(ctx, flags);
abort:
	return ret;
}

static ssize_t
pfm_read(struct file *filp, char *buf, size_t size, loff_t *ppos)
{
	int oldvar, ret;

	oldvar = pfm_debug_var;
	pfm_debug_var = pfm_sysctl.debug_pfm_read;
	ret = pfm_do_read(filp, buf, size, ppos);
	pfm_debug_var = oldvar;
	return ret;
}

static ssize_t
pfm_write(struct file *file, const char *ubuf,
			  size_t size, loff_t *ppos)
{
	DPRINT(("pfm_write called\n"));
	return -EINVAL;
}

static unsigned int
pfm_poll(struct file *filp, poll_table * wait)
{
	pfm_context_t *ctx;
	unsigned long flags;
	unsigned int mask = 0;

	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_poll: bad magic [%d]\n", current->pid);
		return 0;
	}

	ctx = (pfm_context_t *)filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_poll: NULL ctx [%d]\n", current->pid);
		return 0;
	}


	DPRINT(("pfm_poll ctx_fd=%d before poll_wait\n", ctx->ctx_fd));

	poll_wait(filp, &ctx->ctx_msgq_wait, wait);

	PROTECT_CTX(ctx, flags);

	if (PFM_CTXQ_EMPTY(ctx) == 0)
		mask =  POLLIN | POLLRDNORM;

	UNPROTECT_CTX(ctx, flags);

	DPRINT(("pfm_poll ctx_fd=%d mask=0x%x\n", ctx->ctx_fd, mask));

	return mask;
}

static int
pfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	DPRINT(("pfm_ioctl called\n"));
	return -EINVAL;
}

/*
 * context is locked when coming here
 */
static inline int
pfm_do_fasync(int fd, struct file *filp, pfm_context_t *ctx, int on)
{
	int ret;

	ret = fasync_helper (fd, filp, on, &ctx->ctx_async_queue);

	DPRINT(("pfm_fasync called by [%d] on ctx_fd=%d on=%d async_queue=%p ret=%d\n",
		current->pid,
		fd,
		on,
		ctx->ctx_async_queue, ret));

	return ret;
}

static int
pfm_fasync(int fd, struct file *filp, int on)
{
	pfm_context_t *ctx;
	unsigned long flags;
	int ret;

	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_fasync bad magic [%d]\n", current->pid);
		return -EBADF;
	}

	ctx = (pfm_context_t *)filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_fasync NULL ctx [%d]\n", current->pid);
		return -EBADF;
	}


	PROTECT_CTX(ctx, flags);

	ret = pfm_do_fasync(fd, filp, ctx, on);

	DPRINT(("pfm_fasync called by [%d] on ctx_fd=%d on=%d async_queue=%p ret=%d\n",
		current->pid,
		fd,
		on,
		ctx->ctx_async_queue, ret));

	UNPROTECT_CTX(ctx, flags);

	return ret;
}

#ifdef CONFIG_SMP
/*
 * this function is exclusively called from pfm_close().
 * The context is not protected at that time, nor are interrupts
 * on the remote CPU. That's necessary to avoid deadlocks.
 */
static void
pfm_syswide_force_stop(void *info)
{
	pfm_context_t   *ctx = (pfm_context_t *)info;
	struct pt_regs *regs = ia64_task_regs(current);
	struct task_struct *owner;

	if (ctx->ctx_cpu != smp_processor_id()) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop for CPU%d  but on CPU%d\n",
			ctx->ctx_cpu,
			smp_processor_id());
		return;
	}
	owner = GET_PMU_OWNER();
	if (owner != ctx->ctx_task) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop CPU%d unexpected owner [%d] instead of [%d]\n",
			smp_processor_id(),
			owner->pid, ctx->ctx_task->pid);
		return;
	}
	if (GET_PMU_CTX() != ctx) {
		printk(KERN_ERR "perfmon: pfm_syswide_force_stop CPU%d unexpected ctx %p instead of %p\n",
			smp_processor_id(),
			GET_PMU_CTX(), ctx);
		return;
	}

	DPRINT(("[%d] on CPU%d forcing system wide stop for [%d]\n", current->pid, smp_processor_id(), ctx->ctx_task->pid));
	/*
	 * Update local PMU
	 */
	ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);
	ia64_srlz_i();
	/*
	 * update local cpuinfo
	 */
	PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP);
	PFM_CPUINFO_CLEAR(PFM_CPUINFO_SYST_WIDE);
	PFM_CPUINFO_CLEAR(PFM_CPUINFO_EXCL_IDLE);

	pfm_clear_psr_pp();

	/*
	 * also stop monitoring in the local interrupted task
	 */
	ia64_psr(regs)->pp = 0;

	SET_PMU_OWNER(NULL, NULL);
}

static void
pfm_syswide_cleanup_other_cpu(pfm_context_t *ctx)
{
	int ret;

	DPRINT(("[%d] calling CPU%d for cleanup\n", current->pid, ctx->ctx_cpu));
	ret = smp_call_function_single(ctx->ctx_cpu, pfm_syswide_force_stop, ctx, 0, 1);
	DPRINT(("[%d] called CPU%d for cleanup ret=%d\n", current->pid, ctx->ctx_cpu, ret));
}
#endif /* CONFIG_SMP */

/*
 * called either on explicit close() or from exit_files().
 *
 * IMPORTANT: we get called ONLY when the refcnt on the file gets to zero (fput()),i.e,
 * last task to access the file. Nobody else can access the file at this point.
 *
 * When called from exit_files(), the VMA has been freed because exit_mm()
 * is executed before exit_files().
 *
 * When called from exit_files(), the current task is not yet ZOMBIE but we will
 * flush the PMU state to the context. This means * that when we see the context
 * state as TERMINATED we are guranteed to have the latest PMU state available,
 * even if the task itself is in the middle of being ctxsw out.
 */
static int pfm_context_unload(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs);
static int
pfm_close(struct inode *inode, struct file *filp)
{
	pfm_context_t *ctx;
	struct task_struct *task;
	struct pt_regs *regs;
  	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	unsigned long smpl_buf_size = 0UL;
	void *smpl_buf_vaddr = NULL;
	void *smpl_buf_addr = NULL;
	int free_possible = 1;

	{ u64 psr = pfm_get_psr();
	  BUG_ON((psr & IA64_PSR_I) == 0UL);
	}

	DPRINT(("pfm_close called private=%p\n", filp->private_data));

	if (!inode) {
		printk(KERN_ERR "pfm_close: NULL inode\n");
		return 0;
	}

	if (PFM_IS_FILE(filp) == 0) {
		printk(KERN_ERR "perfmon: pfm_close: bad magic [%d]\n", current->pid);
		return -EBADF;
	}

	ctx = (pfm_context_t *)filp->private_data;
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: pfm_close: NULL ctx [%d]\n", current->pid);
		return -EBADF;
	}

	PROTECT_CTX(ctx, flags);

	/*
	 * remove our file from the async queue, if we use it
	 */
	if (filp->f_flags & FASYNC) {
		DPRINT(("[%d] before async_queue=%p\n", current->pid, ctx->ctx_async_queue));
		pfm_do_fasync (-1, filp, ctx, 0);
		DPRINT(("[%d] after async_queue=%p\n", current->pid, ctx->ctx_async_queue));
	}

	task = PFM_CTX_TASK(ctx);

	DPRINT(("[%d] ctx_state=%d\n", current->pid, ctx->ctx_state));

	if (CTX_IS_UNLOADED(ctx) || CTX_IS_TERMINATED(ctx)) {
		goto doit;
	}

	regs = ia64_task_regs(task);

	/*
	 * context still loaded/masked and self monitoring,
	 * we stop/unload and we destroy right here
	 *
	 * We always go here for system-wide sessions
	 */
	if (task == current) {
#ifdef CONFIG_SMP
		/*
		 * the task IS the owner but it migrated to another CPU: that's bad
		 * but we must handle this cleanly. Unfortunately, the kernel does
		 * not provide a mechanism to block migration (while the context is loaded).
		 *
		 * We need to release the resource on the ORIGINAL cpu.
		 */
		if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {

			DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));

			UNPROTECT_CTX(ctx, flags);

			pfm_syswide_cleanup_other_cpu(ctx);

			PROTECT_CTX(ctx, flags);

			/*
			 * short circuit pfm_context_unload();
			 */
			task->thread.pfm_context = NULL;
			ctx->ctx_task            = NULL;

			CTX_UNLOADED(ctx);

			pfm_unreserve_session(ctx, 1 , ctx->ctx_cpu);
		} else
#endif /* CONFIG_SMP */
		{

			DPRINT(("forcing unload on [%d]\n", current->pid));
			/*
		 	* stop and unload, returning with state UNLOADED
		 	* and session unreserved.
		 	*/
			pfm_context_unload(ctx, NULL, 0, regs);

			CTX_TERMINATED(ctx);

			DPRINT(("[%d] ctx_state=%d\n", current->pid, ctx->ctx_state));
		}
		goto doit;
	}

	/*
	 * The task is currently blocked or will block after an overflow.
	 * we must force it to wakeup to get out of the
	 * MASKED state and transition to the unloaded state by itself
	 */
	if (CTX_IS_MASKED(ctx) && CTX_OVFL_NOBLOCK(ctx) == 0) {

		/*
		 * set a "partial" zombie state to be checked
		 * upon return from down() in pfm_handle_work().
		 *
		 * We cannot use the ZOMBIE state, because it is checked
		 * by pfm_load_regs() which is called upon wakeup from down().
		 * In such cas, it would free the context and then we would
		 * return to pfm_handle_work() which would access the
		 * stale context. Instead, we set a flag invisible to pfm_load_regs()
		 * but visible to pfm_handle_work().
		 *
		 * For some window of time, we have a zombie context with
		 * ctx_state = MASKED  and not ZOMBIE
		 */
		ctx->ctx_fl_going_zombie = 1;

		/*
		 * force task to wake up from MASKED state
		 */
		up(&ctx->ctx_restart_sem);

		DPRINT(("waking up ctx_state=%d for [%d]\n", ctx->ctx_state, current->pid));

		/*
		 * put ourself to sleep waiting for the other
		 * task to report completion
		 *
		 * the context is protected by mutex, therefore there
		 * is no risk of being notified of completion before
		 * begin actually on the waitq.
		 */
  		set_current_state(TASK_INTERRUPTIBLE);
  		add_wait_queue(&ctx->ctx_zombieq, &wait);

		UNPROTECT_CTX(ctx, flags);

		/*
		 * XXX: check for signals :
		 * 	- ok of explicit close
		 * 	- not ok when coming from exit_files()
		 */
      		schedule();

		DPRINT(("woken up ctx_state=%d for [%d]\n", ctx->ctx_state, current->pid));

		PROTECT_CTX(ctx, flags);

		remove_wait_queue(&ctx->ctx_zombieq, &wait);
  		set_current_state(TASK_RUNNING);

		/*
		 * context is terminated at this point
		 */
		DPRINT(("after zombie wakeup ctx_state=%d for [%d]\n", ctx->ctx_state, current->pid));
	}
	else {
#ifdef CONFIG_SMP
		/*
	 	 * switch context to zombie state
	 	 */
		CTX_ZOMBIE(ctx);

		DPRINT(("zombie ctx for [%d]\n", task->pid));
		/*
		 * cannot free the context on the spot. deferred until
		 * the task notices the ZOMBIE state
		 */
		free_possible = 0;
#else
		pfm_context_unload(ctx, NULL, 0, regs);
#endif
	}

doit:	/* cannot assume task is defined from now on */
	/*
	 * the context is still attached to a task (possibly current)
	 * we cannot destroy it right now
	 */
	/*
	 * remove virtual mapping, if any. will be NULL when
	 * called from exit_files().
	 */
	if (ctx->ctx_smpl_vaddr) {
		smpl_buf_vaddr = ctx->ctx_smpl_vaddr;
		smpl_buf_size  = ctx->ctx_smpl_size;
		ctx->ctx_smpl_vaddr = NULL;
	}

	/*
	 * we must fre the sampling buffer right here because
	 * we cannot rely on it being cleaned up later by the
	 * monitored task. It is not possible to free vmalloc'ed
	 * memory in pfm_load_regs(). Instead, we remove the buffer
	 * now. should there be subsequent PMU overflow originally
	 * meant for sampling, the will be converted to spurious
	 * and that's fine because the monitoring tools is gone anyway.
	 */
	if (ctx->ctx_smpl_hdr) {
		smpl_buf_addr = ctx->ctx_smpl_hdr;
		smpl_buf_size = ctx->ctx_smpl_size;
		/* no more sampling */
		ctx->ctx_smpl_hdr = NULL;
	}


	DPRINT(("[%d] ctx_state=%d free_possible=%d vaddr=%p addr=%p size=%lu\n",
		current->pid,
		ctx->ctx_state,
		free_possible,
		smpl_buf_vaddr,
		smpl_buf_addr,
		smpl_buf_size));

	if (smpl_buf_addr) pfm_exit_smpl_buffer(ctx->ctx_buf_fmt);

	/*
	 * UNLOADED and TERMINATED mean that the session has already been
	 * unreserved.
	 */
	if (CTX_IS_ZOMBIE(ctx)) {
		pfm_unreserve_session(ctx, ctx->ctx_fl_system , ctx->ctx_cpu);
	}

	/*
	 * disconnect file descriptor from context must be done
	 * before we unlock.
	 */
	filp->private_data = NULL;

	/*
	 * if we free on the spot, the context is now completely unreacheable
	 * from the callers side. The monitored task side is also cut, so we
	 * can freely cut.
	 *
	 * If we have a deferred free, only the caller side is disconnected.
	 */
	UNPROTECT_CTX(ctx, flags);

	/*
	 * if there was a mapping, then we systematically remove it
	 * at this point. Cannot be done inside critical section
	 * because some VM function reenables interrupts.
	 *
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (smpl_buf_vaddr) pfm_remove_smpl_mapping(current, smpl_buf_vaddr, smpl_buf_size);
	if (smpl_buf_addr)  pfm_rvfree(smpl_buf_addr, smpl_buf_size);

	/*
	 * return the memory used by the context
	 */
	if (free_possible) pfm_context_free(ctx);

	return 0;
}

static int
pfm_no_open(struct inode *irrelevant, struct file *dontcare)
{
	DPRINT(("pfm_no_open called\n"));
	return -ENXIO;
}

static struct file_operations pfm_file_ops = {
	.llseek   = pfm_lseek,
	.read     = pfm_read,
	.write    = pfm_write,
	.poll     = pfm_poll,
	.ioctl    = pfm_ioctl,
	.open     = pfm_no_open,	/* special open code to disallow open via /proc */
	.fasync   = pfm_fasync,
	.release  = pfm_close
};

static int
pfmfs_delete_dentry(struct dentry *dentry)
{
	return 1;
}
static struct dentry_operations pfmfs_dentry_operations = {
	d_delete:	pfmfs_delete_dentry,
};


static int
pfm_alloc_fd(struct file **cfile)
{
	int fd, ret = 0;
	struct file *file = NULL;
	struct inode * inode;
	char name[32];
	struct qstr this;

	fd = get_unused_fd();
	if (fd < 0) return -ENFILE;

	ret = -ENFILE;

	file = get_empty_filp();
	if (!file) goto out;

	/*
	 * allocate a new inode
	 */
	inode = new_inode(pfmfs_mnt->mnt_sb);
	if (!inode) goto out;

	DPRINT(("new inode ino=%ld @%p\n", inode->i_ino, inode));

	inode->i_sb   = pfmfs_mnt->mnt_sb;
	inode->i_mode = S_IFCHR|S_IRUGO;
	inode->i_sock = 0;
	inode->i_uid  = current->fsuid;
	inode->i_gid  = current->fsgid;

	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.len  = strlen(name);
	this.hash = inode->i_ino;

	ret = -ENOMEM;

	/*
	 * allocate a new dcache entry
	 */
	file->f_dentry = d_alloc(pfmfs_mnt->mnt_sb->s_root, &this);
	if (!file->f_dentry) goto out;

	file->f_dentry->d_op = &pfmfs_dentry_operations;

	d_add(file->f_dentry, inode);
	file->f_vfsmnt = mntget(pfmfs_mnt);

	file->f_op    = &pfm_file_ops;
	file->f_mode  = FMODE_READ;
	file->f_flags = O_RDONLY;
	file->f_pos   = 0;

	/*
	 * may have to delay until context is attached?
	 */
	fd_install(fd, file);

	/*
	 * the file structure we will use
	 */
	*cfile = file;

	return fd;
out:
	if (file) put_filp(file);
	put_unused_fd(fd);
	return ret;
}

static void
pfm_free_fd(int fd, struct file *file)
{
	if (file) put_filp(file);
	put_unused_fd(fd);
}

/*
 * This function gets called from mm/mmap.c:exit_mmap() only when there is a sampling buffer
 * attached to the context AND the current task has a mapping for it, i.e., it is the original
 * creator of the context.
 *
 * This function is used to remember the fact that the vma describing the sampling buffer
 * has now been removed. It can only be called when no other tasks share the same mm context.
 *
 */
static void
pfm_vm_close(struct vm_area_struct *vma)
{
	pfm_context_t *ctx = (pfm_context_t *)vma->vm_private_data;
	unsigned long flags;

	PROTECT_CTX(ctx, flags);
	ctx->ctx_smpl_vaddr = NULL;
	UNPROTECT_CTX(ctx, flags);
	DPRINT(("[%d] clearing vaddr for ctx %p\n", current->pid, ctx));
}

static int
pfm_remap_buffer(struct vm_area_struct *vma, unsigned long buf, unsigned long addr, unsigned long size)
{
	unsigned long page;

	DPRINT(("CPU%d buf=0x%lx addr=0x%lx size=%ld\n", smp_processor_id(), buf, addr, size));

	while (size > 0) {
		page = pfm_kvirt_to_pa(buf);

		if (pfm_remap_page_range(vma, addr, page, PAGE_SIZE, PAGE_READONLY)) return -ENOMEM;

		addr  += PAGE_SIZE;
		buf   += PAGE_SIZE;
		size  -= PAGE_SIZE;
	}
	return 0;
}

/*
 * allocate a sampling buffer and remaps it into the user address space of the task
 */
static int
pfm_smpl_buffer_alloc(struct task_struct *task, pfm_context_t *ctx, unsigned long rsize, void **user_vaddr)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma = NULL;
	unsigned long size;
	void *smpl_buf;


	/*
	 * the fixed header + requested size and align to page boundary
	 */
	size = PAGE_ALIGN(rsize);

	DPRINT(("sampling buffer rsize=%lu size=%lu bytes\n", rsize, size));

	/*
	 * check requested size to avoid Denial-of-service attacks
	 * XXX: may have to refine this test
	 * Check against address space limit.
	 *
	 * if ((mm->total_vm << PAGE_SHIFT) + len> task->rlim[RLIMIT_AS].rlim_cur)
	 * 	return -ENOMEM;
	 */
	if (size > task->rlim[RLIMIT_MEMLOCK].rlim_cur) return -EAGAIN;

	/*
	 * We do the easy to undo allocations first.
 	 *
	 * pfm_rvmalloc(), clears the buffer, so there is no leak
	 */
	smpl_buf = pfm_rvmalloc(size);
	if (smpl_buf == NULL) {
		DPRINT(("Can't allocate sampling buffer\n"));
		return -ENOMEM;
	}

	DPRINT(("[%d]  smpl_buf @%p\n", current->pid, smpl_buf));

	/* allocate vma */
	vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (!vma) {
		DPRINT(("Cannot allocate vma\n"));
		goto error_kmem;
	}
	/*
	 * partially initialize the vma for the sampling buffer
	 *
	 * The VM_DONTCOPY flag is very important as it ensures that the mapping
	 * will never be inherited for any child process (via fork()) which is always
	 * what we want.
	 */
	vma->vm_mm	     = mm;
	vma->vm_flags	     = VM_READ| VM_MAYREAD |VM_RESERVED|VM_DONTCOPY;
	vma->vm_page_prot    = PAGE_READONLY; /* XXX may need to change */
	vma->vm_ops	     = &pfm_vm_ops;
	vma->vm_pgoff	     = 0;
	vma->vm_file	     = NULL;
	vma->vm_private_data = ctx;	/* information needed by the pfm_vm_close() function */

	/*
	 * Now we have everything we need and we can initialize
	 * and connect all the data structures
	 */

	ctx->ctx_smpl_hdr   = smpl_buf;
	ctx->ctx_smpl_size  = size; /* aligned size */

	/*
	 * Let's do the difficult operations next.
	 *
	 * now we atomically find some area in the address space and
	 * remap the buffer in it.
	 */
	down_write(&task->mm->mmap_sem);

	/* find some free area in address space, must have mmap sem held */
	vma->vm_start = pfm_get_unmapped_area(NULL, 0, size, 0, MAP_PRIVATE|MAP_ANONYMOUS, 0);
	if (vma->vm_start == 0UL) {
		DPRINT(("Cannot find unmapped area for size %ld\n", size));
		up_write(&task->mm->mmap_sem);
		goto error;
	}
	vma->vm_end = vma->vm_start + size;

	DPRINT(("aligned size=%ld, hdr=%p mapped @0x%lx\n", size, ctx->ctx_smpl_hdr, vma->vm_start));

	/* can only be applied to current task, need to have the mm semaphore held when called */
	if (pfm_remap_buffer(vma, (unsigned long)smpl_buf, vma->vm_start, size)) {
		DPRINT(("Can't remap buffer\n"));
		up_write(&task->mm->mmap_sem);
		goto error;
	}

	/*
	 * now insert the vma in the vm list for the process, must be
	 * done with mmap lock held
	 */
	insert_vm_struct(mm, vma);

	mm->total_vm  += size >> PAGE_SHIFT;

	up_write(&task->mm->mmap_sem);

	/*
	 * keep track of user level virtual address
	 */
	ctx->ctx_smpl_vaddr = (void *)vma->vm_start;
	*(unsigned long *)user_vaddr = vma->vm_start;

	return 0;

error:
	kmem_cache_free(vm_area_cachep, vma);
error_kmem:
	pfm_rvfree(smpl_buf, size);

	return -ENOMEM;
}

/*
 * XXX: do something better here
 */
static int
pfm_bad_permissions(struct task_struct *task)
{
	/* stolen from bad_signal() */
	return (current->session != task->session)
	    && (current->euid ^ task->suid) && (current->euid ^ task->uid)
	    && (current->uid ^ task->suid) && (current->uid ^ task->uid);
}

static int
pfarg_is_sane(struct task_struct *task, pfarg_context_t *pfx)
{
	int ctx_flags;

	/* valid signal */

	ctx_flags = pfx->ctx_flags;

	if (ctx_flags & PFM_FL_SYSTEM_WIDE) {

		/*
		 * cannot block in this mode
		 */
		if (ctx_flags & PFM_FL_NOTIFY_BLOCK) {
			DPRINT(("cannot use blocking mode when in system wide monitoring\n"));
			return -EINVAL;
		}
	} else {
	}
	/* probably more to add here */

	return 0;
}

static int
pfm_setup_buffer_fmt(struct task_struct *task, pfm_context_t *ctx, unsigned int ctx_flags,
		     unsigned int cpu, pfarg_context_t *arg)
{
	pfm_buffer_fmt_t *fmt = NULL;
	unsigned long size = 0UL;
	void *uaddr = NULL;
	void *fmt_arg = NULL;
	int ret = 0;
#define PFM_CTXARG_BUF_ARG(a)	(pfm_buffer_fmt_t *)(a+1)

	/* invoke and lock buffer format, if found */
	fmt = pfm_find_buffer_fmt(arg->ctx_smpl_buf_id, 0);
	if (fmt == NULL) {
		DPRINT(("[%d] cannot find buffer format\n", task->pid));
		return -EINVAL;
	}

	/*
	 * buffer argument MUST be contiguous to pfarg_context_t
	 */
	if (fmt->fmt_arg_size) fmt_arg = PFM_CTXARG_BUF_ARG(arg);

	ret = pfm_buf_fmt_validate(fmt, task, ctx_flags, cpu, fmt_arg);

	DPRINT(("[%d] after validate(0x%x,%d,%p)=%d\n", task->pid, ctx_flags, cpu, fmt_arg, ret));

	if (ret) goto error;

	/* link buffer format and context */
	ctx->ctx_buf_fmt = fmt;

	/*
	 * check if buffer format wants to use perfmon buffer allocation/mapping service
	 */
	ret = pfm_buf_fmt_getsize(fmt, task, ctx_flags, cpu, fmt_arg, &size);
	if (ret) goto error;

	if (size) {
		/*
		 * buffer is always remapped into the caller's address space
		 */
		ret = pfm_smpl_buffer_alloc(current, ctx, size, &uaddr);
		if (ret) goto error;

		/* keep track of user address of buffer */
		arg->ctx_smpl_vaddr = uaddr;
	}
	ret = pfm_buf_fmt_init(fmt, task, ctx->ctx_smpl_hdr, ctx_flags, cpu, fmt_arg);

error:
	return ret;
}

static void
pfm_reset_pmu_state(pfm_context_t *ctx)
{
	int i;

	/*
	 * install reset values for PMC.
	 */
	for (i=1; PMC_IS_LAST(i) == 0; i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		ctx->ctx_pmcs[i] = PMC_DFL_VAL(i);
		DPRINT(("pmc[%d]=0x%lx\n", i, ctx->ctx_pmcs[i]));
	}
	/*
	 * PMD registers are set to 0UL when the context in memset()
	 */

	/*
	 * On context switched restore, we must restore ALL pmc and ALL pmd even
	 * when they are not actively used by the task. In UP, the incoming process
	 * may otherwise pick up left over PMC, PMD state from the previous process.
	 * As opposed to PMD, stale PMC can cause harm to the incoming
	 * process because they may change what is being measured.
	 * Therefore, we must systematically reinstall the entire
	 * PMC state. In SMP, the same thing is possible on the
	 * same CPU but also on between 2 CPUs.
	 *
	 * The problem with PMD is information leaking especially
	 * to user level when psr.sp=0
	 *
	 * There is unfortunately no easy way to avoid this problem
	 * on either UP or SMP. This definitively slows down the
	 * pfm_load_regs() function.
	 */

	 /*
	  * bitmask of all PMCs accessible to this context
	  *
	  * PMC0 is treated differently.
	  */
	ctx->ctx_all_pmcs[0] = pmu_conf.impl_pmcs[0] & ~0x1;

	/*
	 * bitmask of all PMDs that are accesible to this context
	 */
	ctx->ctx_all_pmds[0] = pmu_conf.impl_pmds[0];

	DPRINT(("<%d> all_pmcs=0x%lx all_pmds=0x%lx\n", ctx->ctx_fd, ctx->ctx_all_pmcs[0],ctx->ctx_all_pmds[0]));

	/*
	 * useful in case of re-enable after disable
	 */
	ctx->ctx_used_ibrs[0] = 0UL;
	ctx->ctx_used_dbrs[0] = 0UL;
}

static int
pfm_ctx_getsize(void *arg, size_t *sz)
{
	pfarg_context_t *req = (pfarg_context_t *)arg;
	pfm_buffer_fmt_t *fmt;

	*sz = 0;

	if (!pfm_uuid_cmp(req->ctx_smpl_buf_id, pfm_null_uuid)) return 0;

	/* no buffer locking here, will be called again */
	fmt = pfm_find_buffer_fmt(req->ctx_smpl_buf_id, 1);
	if (fmt == NULL) {
		DPRINT(("cannot find buffer format\n"));
		return -EINVAL;
	}
	/* get just enough to copy in user parameters */
	*sz = fmt->fmt_arg_size;
	DPRINT(("arg_size=%lu\n", *sz));

	return 0;
}



/*
 * cannot attach if :
 * 	- kernel task
 * 	- task not owned by caller
 * 	- task incompatible with context mode
 */
static int
pfm_task_incompatible(pfm_context_t *ctx, struct task_struct *task)
{
	/*
	 * no kernel task or task not owner by caller
	 */
	if (task->mm == NULL) {
		DPRINT(("[%d] task [%d] has not memory context (kernel thread)\n", current->pid, task->pid));
		return -EPERM;
	}
	if (pfm_bad_permissions(task)) {
		DPRINT(("[%d] no permission to attach to  [%d]\n", current->pid, task->pid));
		return -EPERM;
	}
	/*
	 * cannot block in self-monitoring mode
	 */
	if (CTX_OVFL_NOBLOCK(ctx) == 0 && task == current) {
		DPRINT(("cannot load a blocking context on self for [%d]\n", task->pid));
		return -EINVAL;
	}

	if (task->state == TASK_ZOMBIE) {
		DPRINT(("[%d] cannot attach to  zombie task [%d]\n", current->pid, task->pid));
		return -EBUSY;
	}

	/*
	 * always ok for self
	 */
	if (task == current) return 0;

	if (task->state != TASK_STOPPED) {
		DPRINT(("[%d] cannot attach to non-stopped task [%d] state=%ld\n", current->pid, task->pid, task->state));
		return -EBUSY;
	}
	/*
	 * make sure the task is off any CPU
	 */
	pfm_wait_task_inactive(task);

	/* more to come... */

	return 0;
}

static int
pfm_get_task(pfm_context_t *ctx, pid_t pid, struct task_struct **task)
{
	struct task_struct *p = current;
	int ret;

	/* XXX: need to add more checks here */
	if (pid < 2) return -EPERM;

	if (pid != current->pid) {

		read_lock(&tasklist_lock);

		p = find_task_by_pid(pid);

		/* make sure task cannot go away while we operate on it */
		if (p) get_task_struct(p);

		read_unlock(&tasklist_lock);

		if (p == NULL) return -ESRCH;
	}

	ret = pfm_task_incompatible(ctx, p);
	if (ret == 0) {
		*task = p;
	} else if (p != current) {
		pfm_put_task(p);
	}
	return ret;
}



static int
pfm_context_create(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_context_t *req = (pfarg_context_t *)arg;
	struct file *filp;
	int ctx_flags;
	int ret;

	/* let's check the arguments first */
	ret = pfarg_is_sane(current, req);
	if (ret < 0) return ret;

	ctx_flags = req->ctx_flags;

	ret = -ENOMEM;

	ctx = pfm_context_alloc();
	if (!ctx) goto error;

	req->ctx_fd = ctx->ctx_fd = pfm_alloc_fd(&filp);
	if (req->ctx_fd < 0) goto error_file;

	/*
	 * attach context to file
	 */
	filp->private_data = ctx;

	/*
	 * does the user want to sample?
	 */
	if (pfm_uuid_cmp(req->ctx_smpl_buf_id, pfm_null_uuid)) {
		ret = pfm_setup_buffer_fmt(current, ctx, ctx_flags, 0, req);
		if (ret) goto buffer_error;
	}

	/*
	 * init context protection lock
	 */
	spin_lock_init(&ctx->ctx_lock);

	/*
	 * context is unloaded
	 */
	CTX_UNLOADED(ctx);

	/*
	 * initialization of context's flags
	 */
	ctx->ctx_fl_block       = (ctx_flags & PFM_FL_NOTIFY_BLOCK) ? 1 : 0;
	ctx->ctx_fl_system      = (ctx_flags & PFM_FL_SYSTEM_WIDE) ? 1: 0;
	ctx->ctx_fl_unsecure	= (ctx_flags & PFM_FL_UNSECURE) ? 1: 0;
	ctx->ctx_fl_is_sampling = ctx->ctx_buf_fmt ? 1 : 0; /* assume record() is defined */
	ctx->ctx_fl_no_msg      = (ctx_flags & PFM_FL_OVFL_NO_MSG) ? 1: 0;
	/*
	 * will move to set properties
	 * ctx->ctx_fl_excl_idle   = (ctx_flags & PFM_FL_EXCL_IDLE) ? 1: 0;
	 */

	/*
	 * init restart semaphore to locked
	 */
	sema_init(&ctx->ctx_restart_sem, 0);

	/*
	 * activation is used in SMP only
	 */
	ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
	SET_LAST_CPU(ctx, -1);

	/*
	 * initialize notification message queue
	 */
	ctx->ctx_msgq_head = ctx->ctx_msgq_tail = 0;
	init_waitqueue_head(&ctx->ctx_msgq_wait);
	init_waitqueue_head(&ctx->ctx_zombieq);

	DPRINT(("ctx=%p flags=0x%x system=%d notify_block=%d excl_idle=%d unsecure=%d no_msg=%d ctx_fd=%d \n",
		ctx,
		ctx_flags,
		ctx->ctx_fl_system,
		ctx->ctx_fl_block,
		ctx->ctx_fl_excl_idle,
		ctx->ctx_fl_unsecure,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd));

	/*
	 * initialize soft PMU state
	 */
	pfm_reset_pmu_state(ctx);

	return 0;

buffer_error:
	pfm_free_fd(ctx->ctx_fd, filp);

	if (ctx->ctx_buf_fmt) {
		pfm_buf_fmt_exit(ctx->ctx_buf_fmt, current, NULL, regs);
	}
error_file:
	pfm_context_free(ctx);

error:
	return ret;
}

static inline unsigned long
pfm_new_counter_value (pfm_counter_t *reg, int is_long_reset)
{
	unsigned long val = is_long_reset ? reg->long_reset : reg->short_reset;
	unsigned long new_seed, old_seed = reg->seed, mask = reg->mask;
	extern unsigned long carta_random32 (unsigned long seed);

	if (reg->flags & PFM_REGFL_RANDOM) {
		new_seed = carta_random32(old_seed);
		val -= (old_seed & mask);	/* counter values are negative numbers! */
		if ((mask >> 32) != 0)
			/* construct a full 64-bit random value: */
			new_seed |= carta_random32(old_seed >> 32) << 32;
		reg->seed = new_seed;
	}
	reg->lval = val;
	return val;
}

static void
pfm_reset_regs_masked(pfm_context_t *ctx, unsigned long *ovfl_regs, int flag)
{
	unsigned long mask = ovfl_regs[0];
	unsigned long reset_others = 0UL;
	unsigned long val;
	int i, is_long_reset = (flag == PFM_PMD_LONG_RESET);

	DPRINT_ovfl(("ovfl_regs=0x%lx flag=%d\n", ovfl_regs[0], flag));

	if (flag == PFM_PMD_NO_RESET) return;

	/*
	 * now restore reset value on sampling overflowed counters
	 */
	mask >>= PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER; mask; i++, mask >>= 1) {
		if (mask & 0x1) {
			ctx->ctx_pmds[i].val = val = pfm_new_counter_value(ctx->ctx_pmds+ i, is_long_reset);
			reset_others |= ctx->ctx_pmds[i].reset_pmds[0];

			DPRINT_ovfl((" %s reset ctx_pmds[%d]=%lx\n",
				  is_long_reset ? "long" : "short", i, val));
		}
	}

	/*
	 * Now take care of resetting the other registers
	 */
	for(i = 0; reset_others; i++, reset_others >>= 1) {

		if ((reset_others & 0x1) == 0) continue;

		ctx->ctx_pmds[i].val = val = pfm_new_counter_value(ctx->ctx_pmds + i, is_long_reset);

		DPRINT_ovfl(("%s reset_others pmd[%d]=%lx\n",
			  is_long_reset ? "long" : "short", i, val));
	}
}

static void
pfm_reset_regs(pfm_context_t *ctx, unsigned long *ovfl_regs, int flag)
{
	unsigned long mask = ovfl_regs[0];
	unsigned long reset_others = 0UL;
	unsigned long val;
	int i, is_long_reset = (flag == PFM_PMD_LONG_RESET);

	DPRINT_ovfl(("ovfl_regs=0x%lx flag=%d\n", ovfl_regs[0], flag));

	if (flag == PFM_PMD_NO_RESET) return;

	if (CTX_IS_MASKED(ctx)) {
		pfm_reset_regs_masked(ctx, ovfl_regs, flag);
		return;
	}

	/*
	 * now restore reset value on sampling overflowed counters
	 */
	mask >>= PMU_FIRST_COUNTER;
	for(i = PMU_FIRST_COUNTER; mask; i++, mask >>= 1) {
		if (mask & 0x1) {
			val = pfm_new_counter_value(ctx->ctx_pmds+ i, is_long_reset);
			reset_others |= ctx->ctx_pmds[i].reset_pmds[0];

			DPRINT_ovfl((" %s reset ctx_pmds[%d]=%lx\n",
				  is_long_reset ? "long" : "short", i, val));

			pfm_write_soft_counter(ctx, i, val);
		}
	}

	/*
	 * Now take care of resetting the other registers
	 */
	for(i = 0; reset_others; i++, reset_others >>= 1) {

		if ((reset_others & 0x1) == 0) continue;

		val = pfm_new_counter_value(ctx->ctx_pmds + i, is_long_reset);

		if (PMD_IS_COUNTING(i)) {
			pfm_write_soft_counter(ctx, i, val);
		} else {
			ia64_set_pmd(i, val);
		}
		DPRINT_ovfl(("%s reset_others pmd[%d]=%lx\n",
			  is_long_reset ? "long" : "short", i, val));
	}
	ia64_srlz_d();
}

static int
pfm_write_pmcs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = NULL;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned long value;
	unsigned long smpl_pmds, reset_pmds;
	unsigned int cnum, reg_flags, flags;
	int i, can_access_pmu = 0, is_loaded;
	int is_monitor, is_counting;
	int ret = -EINVAL;
#define PFM_CHECK_PMC_PM(x, y, z) ((x)->ctx_fl_system ^ PMC_PM(y, z))

	if (CTX_IS_DEAD(ctx)) return -EINVAL;

	is_loaded = CTX_IS_LOADED(ctx);

	if (is_loaded) {
		thread = &ctx->ctx_task->thread;
		can_access_pmu = GET_PMU_OWNER() == ctx->ctx_task? 1 : 0;
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
			DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
			return -EBUSY;
		}
	}

	for (i = 0; i < count; i++, req++) {

		cnum       = req->reg_num;
		reg_flags  = req->reg_flags;
		value      = req->reg_value;
		smpl_pmds  = req->reg_smpl_pmds[0];
		reset_pmds = req->reg_reset_pmds[0];
		flags      = 0;

		is_counting = PMC_IS_COUNTING(cnum);
		is_monitor  = PMC_IS_MONITOR(cnum);

		/*
		 * we reject all non implemented PMC as well
		 * as attempts to modify PMC[0-3] which are used
		 * as status registers by the PMU
		 */
		if (PMC_IS_IMPL(cnum) == 0 || PMC_IS_CONTROL(cnum)) {
			DPRINT(("pmc%u is unimplemented or invalid\n", cnum));
			goto error;
		}
		/*
		 * If the PMC is a monitor, then if the value is not the default:
		 * 	- system-wide session: PMCx.pm=1 (privileged monitor)
		 * 	- per-task           : PMCx.pm=0 (user monitor)
		 */
		if ((is_monitor || is_counting) && value != PMC_DFL_VAL(i) && PFM_CHECK_PMC_PM(ctx, cnum, value)) {
			DPRINT(("pmc%u pmc_pm=%ld fl_system=%d\n",
				cnum,
				PMC_PM(cnum, value),
				ctx->ctx_fl_system));
			goto error;
		}


		if (is_counting) {
			pfm_monitor_t *p = (pfm_monitor_t *)&value;
			/*
		 	 * enforce generation of overflow interrupt. Necessary on all
		 	 * CPUs.
		 	 */
			p->pmc_oi = 1;

			if (reg_flags & PFM_REGFL_OVFL_NOTIFY) {
				flags |= PFM_REGFL_OVFL_NOTIFY;
			}

			if (reg_flags & PFM_REGFL_RANDOM) flags |= PFM_REGFL_RANDOM;

			/* verify validity of smpl_pmds */
			if ((smpl_pmds & pmu_conf.impl_pmds[0]) != smpl_pmds) {
				DPRINT(("invalid smpl_pmds 0x%lx for pmc%u\n", smpl_pmds, cnum));
				goto error;
			}

			/* verify validity of reset_pmds */
			if ((reset_pmds & pmu_conf.impl_pmds[0]) != reset_pmds) {
				DPRINT(("invalid reset_pmds 0x%lx for pmc%u\n", reset_pmds, cnum));
				goto error;
			}
		} else {
			if (reg_flags & (PFM_REGFL_OVFL_NOTIFY|PFM_REGFL_RANDOM)) {
				DPRINT(("cannot set ovfl_notify or random on pmc%u\n", cnum));
				goto error;
			}
			/* eventid on non-counting monitors are ignored */
		}

		/*
		 * execute write checker, if any
		 */
		if (PMC_WR_FUNC(cnum)) {
			ret = PMC_WR_FUNC(cnum)(ctx->ctx_task, ctx, cnum, &value, regs);
			if (ret) goto error;
			ret = -EINVAL;
		}

		/*
		 * no error on this register
		 */
		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		/*
		 * Now we commit the changes to the software state
		 */

		/*
		 * update overflow information
		 */
		if (is_counting) {
			/*
		 	 * full flag update each time a register is programmed
		 	 */
			ctx->ctx_pmds[cnum].flags = flags;

			ctx->ctx_pmds[cnum].reset_pmds[0] = reset_pmds;
			ctx->ctx_pmds[cnum].smpl_pmds[0]  = smpl_pmds;
			ctx->ctx_pmds[cnum].eventid       = req->reg_smpl_eventid;

			/*
			 * Mark all PMDS to be accessed as used.
			 *
			 * We do not keep track of PMC because we have to
			 * systematically restore ALL of them.
			 *
			 * We do not update the used_monitors mask, because
			 * if we have not programmed them, then will be in
			 * a quiescent state, therefore we will not need to
			 * mask/restore then when context is MASKED.
			 */
			CTX_USED_PMD(ctx, reset_pmds);
			CTX_USED_PMD(ctx, smpl_pmds);
			/*
		 	 * make sure we do not try to reset on
		 	 * restart because we have established new values
		 	 */
			if (CTX_IS_MASKED(ctx)) ctx->ctx_ovfl_regs[0] &= ~1UL << cnum;
		}
		/*
		 * Needed in case the user does not initialize the equivalent
		 * PMD. Clearing is done indirectly via pfm_reset_pmu_state() so there is no
		 * possible leak here.
		 */
		CTX_USED_PMD(ctx, pmu_conf.pmc_desc[cnum].dep_pmd[0]);

		/*
		 * keep track of the monitor PMC that we are using.
		 * we save the value of the pmc in ctx_pmcs[] and if
		 * the monitoring is not stopped for the context we also
		 * place it in the saved state area so that it will be
		 * picked up later by the context switch code.
		 *
		 * The value in ctx_pmcs[] can only be changed in pfm_write_pmcs().
		 *
		 * The value in t->pmc[] may be modified on overflow, i.e.,  when
		 * monitoring needs to be stopped.
		 */
		if (is_monitor) CTX_USED_MONITOR(ctx, 1UL << cnum);

		/*
		 * update context state
		 */
		ctx->ctx_pmcs[cnum] = value;

		if (is_loaded) {
			/*
			 * write thread state
			 */
			if (ctx->ctx_fl_system == 0) thread->pmcs[cnum] = value;

			/*
			 * write hardware register if we can
			 */
			if (can_access_pmu) {
				ia64_set_pmc(cnum, value);
			}
#ifdef CONFIG_SMP
			else {
				/*
				 * per-task SMP only here
				 *
			 	 * we are guaranteed that the task is not running on the other CPU,
			 	 * we indicate that this PMD will need to be reloaded if the task
			 	 * is rescheduled on the CPU it ran last on.
			 	 */
				ctx->ctx_reload_pmcs[0] |= 1UL << cnum;
			}
#endif
		}

		DPRINT(("pmc[%u]=0x%lx loaded=%d access_pmu=%d all_pmcs=0x%lx used_pmds=0x%lx eventid=%ld smpl_pmds=0x%lx reset_pmds=0x%lx reloads_pmcs=0x%lx used_monitors=0x%lx ovfl_regs=0x%lx\n",
			  cnum,
			  value,
			  is_loaded,
			  can_access_pmu,
			  ctx->ctx_all_pmcs[0],
			  ctx->ctx_used_pmds[0],
			  ctx->ctx_pmds[cnum].eventid,
			  smpl_pmds,
			  reset_pmds,
			  ctx->ctx_reload_pmcs[0],
			  ctx->ctx_used_monitors[0],
			  ctx->ctx_ovfl_regs[0]));
	}

	/*
	 * make sure the changes are visible
	 */
	if (can_access_pmu) ia64_srlz_d();

	return 0;
error:
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);

	req->reg_flags = PFM_REG_RETFL_EINVAL;

	DPRINT(("pmc[%u]=0x%lx error %d\n", cnum, value, ret));

	return ret;
}

static int
pfm_write_pmds(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = NULL;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned long value, hw_value;
	unsigned int cnum;
	int i, can_access_pmu = 0;
	int is_counting, is_loaded;
	int ret = -EINVAL;

	if (CTX_IS_DEAD(ctx)) return -EINVAL;

	is_loaded = CTX_IS_LOADED(ctx);

	/*
	 * on both UP and SMP, we can only write to the PMC when the task is
	 * the owner of the local PMU.
	 */
	if (is_loaded) {
		thread = &ctx->ctx_task->thread;
		can_access_pmu = GET_PMU_OWNER() == ctx->ctx_task ? 1 : 0;
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
			DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
			return -EBUSY;
		}
	}

	for (i = 0; i < count; i++, req++) {

		cnum  = req->reg_num;
		value = req->reg_value;

		if (!PMD_IS_IMPL(cnum)) {
			DPRINT(("pmd[%u] is unimplemented or invalid\n", cnum));
			goto abort_mission;
		}
		is_counting = PMD_IS_COUNTING(cnum);

		/*
		 * execute write checker, if any
		 */
		if (PMD_WR_FUNC(cnum)) {
			unsigned long v = value;

			ret = PMD_WR_FUNC(cnum)(ctx->ctx_task, ctx, cnum, &v, regs);
			if (ret) goto abort_mission;

			value = v;
			ret   = -EINVAL;
		}

		/*
		 * no error on this register
		 */
		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		/*
		 * now commit changes to software state
		 */
		hw_value = value;

		/*
		 * update virtualized (64bits) counter
		 */
		if (is_counting) {
			/*
			 * write context state
			 */
			ctx->ctx_pmds[cnum].lval = value;

			/*
			 * when context is load we use the split value
			 */
			if (is_loaded) {
				hw_value = value &  pmu_conf.ovfl_val;
				value    = value & ~pmu_conf.ovfl_val;
			}

			/*
			 * update sampling periods
			 */
			ctx->ctx_pmds[cnum].long_reset  = req->reg_long_reset;
			ctx->ctx_pmds[cnum].short_reset = req->reg_short_reset;

			/*
			 * update randomization parameters
			 */
			ctx->ctx_pmds[cnum].seed = req->reg_random_seed;
			ctx->ctx_pmds[cnum].mask = req->reg_random_mask;
		}

		/*
		 * update context value
		 */
		ctx->ctx_pmds[cnum].val  = value;

		/*
		 * Keep track of what we use
		 *
		 * We do not keep track of PMC because we have to
		 * systematically restore ALL of them.
		 */
		CTX_USED_PMD(ctx, PMD_PMD_DEP(cnum));

		/*
		 * mark this PMD register used as well
		 */
		CTX_USED_PMD(ctx, RDEP(cnum));

		/*
		 * make sure we do not try to reset on
		 * restart because we have established new values
		 */
		if (is_counting && CTX_IS_MASKED(ctx)) {
			ctx->ctx_ovfl_regs[0] &= ~1UL << cnum;
		}

		if (is_loaded) {
			/*
		 	 * write thread state
		 	 */
			if (ctx->ctx_fl_system == 0) thread->pmds[cnum] = hw_value;

			/*
			 * write hardware register if we can
			 */
			if (can_access_pmu) {
				ia64_set_pmd(cnum, hw_value);
			} else {
#ifdef CONFIG_SMP
				/*
			 	 * we are guaranteed that the task is not running on the other CPU,
			 	 * we indicate that this PMD will need to be reloaded if the task
			 	 * is rescheduled on the CPU it ran last on.
			 	 */
				ctx->ctx_reload_pmds[0] |= 1UL << cnum;
#endif
			}
		}

		DPRINT(("pmd[%u]=0x%lx loaded=%d access_pmu=%d, hw_value=0x%lx ctx_pmd=0x%lx  short_reset=0x%lx "
			  "long_reset=0x%lx notify=%c used_pmds=0x%lx reset_pmds=0x%lx reload_pmds=0x%lx all_pmds=0x%lx ovfl_regs=0x%lx\n",
			cnum,
			value,
			is_loaded,
			can_access_pmu,
			hw_value,
			ctx->ctx_pmds[cnum].val,
			ctx->ctx_pmds[cnum].short_reset,
			ctx->ctx_pmds[cnum].long_reset,
			PMC_OVFL_NOTIFY(ctx, cnum) ? 'Y':'N',
			ctx->ctx_used_pmds[0],
			ctx->ctx_pmds[cnum].reset_pmds[0],
			ctx->ctx_reload_pmds[0],
			ctx->ctx_all_pmds[0],
			ctx->ctx_ovfl_regs[0]));
	}

	/*
	 * make changes visible
	 */
	if (can_access_pmu) ia64_srlz_d();

	return 0;

abort_mission:
	/*
	 * for now, we have only one possibility for error
	 */
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);

	/*
	 * we change the return value to EFAULT in case we cannot write register return code.
	 * The caller first must correct this error, then a resubmission of the request will
	 * eventually yield the EINVAL.
	 */
	req->reg_flags = PFM_REG_RETFL_EINVAL;

	DPRINT(("pmd[%u]=0x%lx ret %d\n", cnum, value, ret));

	return ret;
}

/*
 * By the way of PROTECT_CONTEXT(), interrupts are masked while we are in this function.
 * Therefore we know, we do not have to worry about the PMU overflow interrupt. If an
 * interrupt is delivered during the call, it will be kept pending until we leave, making
 * it appears as if it had been generated at the UNPROTECT_CONTEXT(). At least we are
 * guaranteed to return consistent data to the user, it may simply be old. It is not
 * trivial to treat the overflow while inside the call because you may end up in
 * some module sampling buffer code causing deadlocks.
 */
static int
pfm_read_pmds(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = NULL;
	unsigned long val = 0UL, lval ;
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned int cnum, reg_flags = 0;
	int i, is_loaded, can_access_pmu = 0;
	int ret = -EINVAL;

	if (CTX_IS_ZOMBIE(ctx)) return -EINVAL;

	/*
	 * access is possible when loaded only for
	 * self-monitoring tasks or in UP mode
	 */
	is_loaded = CTX_IS_LOADED(ctx);

	if (is_loaded) {
		thread = &ctx->ctx_task->thread;
		/*
		 * this can be true when not self-monitoring only in UP
		 */
		can_access_pmu = GET_PMU_OWNER() == ctx->ctx_task? 1 : 0;

		if (can_access_pmu) ia64_srlz_d();
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
			DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
			return -EBUSY;
		}
	}
	DPRINT(("enter loaded=%d access_pmu=%d ctx_state=%d\n",
		is_loaded,
		can_access_pmu,
		ctx->ctx_state));

	/*
	 * on both UP and SMP, we can only read the PMD from the hardware register when
	 * the task is the owner of the local PMU.
	 */

	for (i = 0; i < count; i++, req++) {

		lval        = 0UL;
		cnum        = req->reg_num;
		reg_flags   = req->reg_flags;

		if (!PMD_IS_IMPL(cnum)) goto error;
		/*
		 * we can only read the register that we use. That includes
		 * the one we explicitely initialize AND the one we want included
		 * in the sampling buffer (smpl_regs).
		 *
		 * Having this restriction allows optimization in the ctxsw routine
		 * without compromising security (leaks)
		 */
		if (!CTX_IS_USED_PMD(ctx, cnum)) goto error;

		/*
		 * If the task is not the current one, then we check if the
		 * PMU state is still in the local live register due to lazy ctxsw.
		 * If true, then we read directly from the registers.
		 */
		if (can_access_pmu){
			val = ia64_get_pmd(cnum);
		} else {
			/*
			 * context has been saved
			 * if context is zombie, then task does not exist anymore.
			 * In this case, we use the full value saved in the context (pfm_flush_regs()).
			 */
			val = CTX_IS_LOADED(ctx) ? thread->pmds[cnum] : 0UL;
		}

		if (PMD_IS_COUNTING(cnum)) {
			/*
			 * XXX: need to check for overflow when loaded
			 */
			val &= pmu_conf.ovfl_val;
			val += ctx->ctx_pmds[cnum].val;

			lval = ctx->ctx_pmds[cnum].lval;
		}

		/*
		 * execute read checker, if any
		 */
		if (PMD_RD_FUNC(cnum)) {
			unsigned long v = val;
			ret = PMD_RD_FUNC(cnum)(ctx->ctx_task, ctx, cnum, &v, regs);
			if (ret) goto error;
			val = v;
			ret = -EINVAL;
		}

		PFM_REG_RETFLAG_SET(reg_flags, 0);

		DPRINT(("pmd[%u]=0x%lx loaded=%d access_pmu=%d ctx_state=%d\n",
			cnum,
			val,
			is_loaded,
			can_access_pmu,
			ctx->ctx_state));

		/*
		 * update register return value, abort all if problem during copy.
		 * we only modify the reg_flags field. no check mode is fine because
		 * access has been verified upfront in sys_perfmonctl().
		 */
		req->reg_value            = val;
		req->reg_flags            = reg_flags;
		req->reg_last_reset_val   = lval;
	}

	return 0;

error:
	PFM_REG_RETFLAG_SET(reg_flags, PFM_REG_RETFL_EINVAL);

	req->reg_flags = PFM_REG_RETFL_EINVAL;

	DPRINT(("error pmd[%u]=0x%lx\n", cnum, val));

	return ret;
}

long
pfm_mod_write_pmcs(struct task_struct *task, pfarg_reg_t *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (task == NULL || req == NULL) return -EINVAL;

 	ctx = task->thread.pfm_context;

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current) return -EBUSY;

	return pfm_write_pmcs(ctx, req, nreq, regs);
}

long
pfm_mod_read_pmds(struct task_struct *task, pfarg_reg_t *req, unsigned int nreq, struct pt_regs *regs)
{
	pfm_context_t *ctx;

	if (task == NULL || req == NULL) return -EINVAL;

 	//ctx = task->thread.pfm_context;
 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	return pfm_read_pmds(ctx, req, nreq, regs);
}

long
pfm_mod_fast_read_pmds(struct task_struct *task, unsigned long mask[4], unsigned long *addr, struct pt_regs *regs)
{
	pfm_context_t *ctx;
	unsigned long m, val;
	unsigned int j;

	if (task == NULL || addr == NULL) return -EINVAL;

 	//ctx = task->thread.pfm_context;
 	ctx = GET_PMU_CTX();

	if (ctx == NULL) return -EINVAL;

	/*
	 * for now limit to current task, which is enough when calling
	 * from overflow handler
	 */
	if (task != current && ctx->ctx_fl_system == 0) return -EBUSY;

	m = mask[0];
	for (j=0; m; m >>=1, j++) {

		if ((m & 0x1) == 0) continue;

		if (!(PMD_IS_IMPL(j)  && CTX_IS_USED_PMD(ctx, j)) ) return -EINVAL;

		if (PMD_IS_COUNTING(j)) {
			val = pfm_read_soft_counter(ctx, j);
		} else {
			val = ia64_get_pmd(j);
		}

		*addr++ = val;

		/* XXX: should call read checker routine? */
		DPRINT(("single_read_pmd[%u]=0x%lx\n", j, val));
	}
	return 0;
}

/*
 * Only call this function when a process it trying to
 * write the debug registers (reading is always allowed)
 */
int
pfm_use_debug_registers(struct task_struct *task)
{
	pfm_context_t *ctx = task->thread.pfm_context;
	int ret = 0;

	if (pmu_conf.use_rr_dbregs == 0) return 0;

	DPRINT(("called for [%d]\n", task->pid));

	/*
	 * do it only once
	 */
	if (task->thread.flags & IA64_THREAD_DBG_VALID) return 0;

	/*
	 * Even on SMP, we do not need to use an atomic here because
	 * the only way in is via ptrace() and this is possible only when the
	 * process is stopped. Even in the case where the ctxsw out is not totally
	 * completed by the time we come here, there is no way the 'stopped' process
	 * could be in the middle of fiddling with the pfm_write_ibr_dbr() routine.
	 * So this is always safe.
	 */
	if (ctx && ctx->ctx_fl_using_dbreg == 1) return -1;

	LOCK_PFS();

	/*
	 * We cannot allow setting breakpoints when system wide monitoring
	 * sessions are using the debug registers.
	 */
	if (pfm_sessions.pfs_sys_use_dbregs> 0)
		ret = -1;
	else
		pfm_sessions.pfs_ptrace_use_dbregs++;

	DPRINT(("ptrace_use_dbregs=%u  sys_use_dbregs=%u by [%d] ret = %d\n",
		  pfm_sessions.pfs_ptrace_use_dbregs,
		  pfm_sessions.pfs_sys_use_dbregs,
		  task->pid, ret));

	UNLOCK_PFS();

	return ret;
}

/*
 * This function is called for every task that exits with the
 * IA64_THREAD_DBG_VALID set. This indicates a task which was
 * able to use the debug registers for debugging purposes via
 * ptrace(). Therefore we know it was not using them for
 * perfmormance monitoring, so we only decrement the number
 * of "ptraced" debug register users to keep the count up to date
 */
int
pfm_release_debug_registers(struct task_struct *task)
{
	int ret;

	if (pmu_conf.use_rr_dbregs == 0) return 0;

	LOCK_PFS();
	if (pfm_sessions.pfs_ptrace_use_dbregs == 0) {
		printk(KERN_ERR "perfmon: invalid release for [%d] ptrace_use_dbregs=0\n", task->pid);
		ret = -1;
	}  else {
		pfm_sessions.pfs_ptrace_use_dbregs--;
		ret = 0;
	}
	UNLOCK_PFS();

	return ret;
}

static int
pfm_restart(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	pfm_buffer_fmt_t *fmt;
	pfm_ovfl_ctrl_t rst_ctrl;
	int is_loaded;
	int ret = 0;

	fmt       = ctx->ctx_buf_fmt;
	is_loaded = CTX_IS_LOADED(ctx);

	if (is_loaded && CTX_HAS_SMPL(ctx) && fmt->fmt_restart_active) goto proceed;

	/*
	 * restarting a terminated context is a nop
	 */
	if (unlikely(CTX_IS_TERMINATED(ctx))) {
		DPRINT(("context is terminated, nothing to do\n"));
		return 0;
	}


	/*
	 * LOADED, UNLOADED, ZOMBIE
	 */
	if (CTX_IS_MASKED(ctx) == 0) return -EBUSY;

proceed:
	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
		return -EBUSY;
	}

	task = PFM_CTX_TASK(ctx);

	/* sanity check */
	if (unlikely(task == NULL)) {
		printk(KERN_ERR "perfmon: [%d] pfm_restart no task\n", current->pid);
		return -EINVAL;
	}

	/*
	 * this test is always true in system wide mode
	 */
	if (task == current) {

		fmt = ctx->ctx_buf_fmt;

		DPRINT(("restarting self %d ovfl=0x%lx\n",
			task->pid,
			ctx->ctx_ovfl_regs[0]));

		if (CTX_HAS_SMPL(ctx)) {

			prefetch(ctx->ctx_smpl_hdr);

			rst_ctrl.stop_monitoring = 0;
			rst_ctrl.reset_pmds      = PFM_PMD_NO_RESET;

			if (is_loaded)
				ret = pfm_buf_fmt_restart_active(fmt, task, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
			else
				ret = pfm_buf_fmt_restart(fmt, task, &rst_ctrl, ctx->ctx_smpl_hdr, regs);


		} else {
			rst_ctrl.stop_monitoring = 0;
			rst_ctrl.reset_pmds      = PFM_PMD_LONG_RESET;
		}

		if (ret == 0) {
			if (rst_ctrl.reset_pmds)
				pfm_reset_regs(ctx, ctx->ctx_ovfl_regs, rst_ctrl.reset_pmds);

			if (rst_ctrl.stop_monitoring == 0) {
				DPRINT(("resuming monitoring for [%d]\n", task->pid));

				if (CTX_IS_MASKED(ctx)) pfm_restore_monitoring(task);
			} else {
				DPRINT(("keeping monitoring stopped for [%d]\n", task->pid));

				// cannot use pfm_stop_monitoring(task, regs);
			}
		}
		/*
		 * clear overflowed PMD mask to remove any stale information
		 */
		ctx->ctx_ovfl_regs[0] = 0UL;

		/*
		 * back to LOADED state
		 */
		CTX_LOADED(ctx);

		return 0;
	}
	/* restart another task */

	/*
	 * if blocking, then post the semaphore.
	 * if non-blocking, then we ensure that the task will go into
	 * pfm_handle_work() before returning to user mode.
	 * We cannot explicitely reset another task, it MUST always
	 * be done by the task itself. This works for system wide because
	 * the tool that is controlling the session is doing "self-monitoring".
	 *
	 * XXX: what if the task never goes back to user?
	 *
	 */
	if (CTX_OVFL_NOBLOCK(ctx) == 0) {
		DPRINT(("unblocking [%d] \n", task->pid));
		up(&ctx->ctx_restart_sem);
	} else {
		DPRINT(("[%d] armed exit trap\n", task->pid));

		ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_RESET;


		PFM_SET_WORK_PENDING(task, 1);

		pfm_set_task_notify(task);

		/*
		 * XXX: send reschedule if task runs on another CPU
		 */
	}
	return 0;
}

static int
pfm_debug(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	unsigned int m = *(unsigned int *)arg;

	pfm_sysctl.debug = m == 0 ? 0 : 1;

	pfm_debug_var = pfm_sysctl.debug;

	printk(KERN_ERR "perfmon debugging %s (timing reset)\n", pfm_sysctl.debug ? "on" : "off");


	if (m==0) {
		memset(pfm_stats, 0, sizeof(pfm_stats));
		for(m=0; m < NR_CPUS; m++) pfm_stats[m].pfm_ovfl_intr_cycles_min = ~0UL;
	}

	return 0;
}


static int
pfm_write_ibr_dbr(int mode, pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct thread_struct *thread = NULL;
	pfarg_dbreg_t *req = (pfarg_dbreg_t *)arg;
	dbreg_t dbreg;
	unsigned int rnum;
	int first_time;
	int ret = 0;
	int i, can_access_pmu = 0, is_loaded;

	if (pmu_conf.use_rr_dbregs == 0) return -EINVAL;

	if (CTX_IS_DEAD(ctx)) return -EINVAL;

	is_loaded = CTX_IS_LOADED(ctx);
	/*
	 * on both UP and SMP, we can only write to the PMC when the task is
	 * the owner of the local PMU.
	 */
	if (is_loaded) {
		thread = &ctx->ctx_task->thread;
		can_access_pmu = GET_PMU_OWNER() == ctx->ctx_task ? 1 : 0;
		/*
		 * In system wide and when the context is loaded, access can only happen
		 * when the caller is running on the CPU being monitored by the session.
		 * It does not have to be the owner (ctx_task) of the context per se.
		 */
		if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
			DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
			return -EBUSY;
		}
	}

	/*
	 * we do not need to check for ipsr.db because we do clear ibr.x, dbr.r, and dbr.w
	 * ensuring that no real breakpoint can be installed via this call.
	 *
	 * IMPORTANT: regs can be NULL in this function
	 */

	first_time = ctx->ctx_fl_using_dbreg == 0;

	/*
	 * don't bother if we are loaded and task is being debugged
	 */
	if (is_loaded && (thread->flags & IA64_THREAD_DBG_VALID) != 0) {
		DPRINT(("debug registers already in use for [%d]\n", ctx->ctx_task->pid));
		return -EBUSY;
	}

	/*
	 * check for debug registers in system wide mode
	 *
	 * We make the reservation even when context is not loaded
	 * to make sure we get our slot. Note that the PFM_LOAD_CONTEXT
	 * may still fail if the task has DBG_VALID set.
	 */
	LOCK_PFS();

	if (first_time && ctx->ctx_fl_system) {
		if (pfm_sessions.pfs_ptrace_use_dbregs)
			ret = -EBUSY;
		else
			pfm_sessions.pfs_sys_use_dbregs++;
	}

	UNLOCK_PFS();

	if (ret != 0) return ret;

	/*
	 * mark ourself as user of the debug registers for
	 * perfmon purposes.
	 */
	ctx->ctx_fl_using_dbreg = 1;

	/*
 	 * clear hardware registers to make sure we don't
 	 * pick up stale state.
	 *
	 * for a system wide session, we do not use
	 * thread.dbr, thread.ibr because this process
	 * never leaves the current CPU and the state
	 * is shared by all processes running on it
 	 */
	if (first_time && can_access_pmu) {
		DPRINT(("[%d] clearing ibrs, dbrs\n", ctx->ctx_task->pid));
		for (i=0; i < pmu_conf.num_ibrs; i++) {
			ia64_set_ibr(i, 0UL);
			ia64_srlz_i();
		}
		ia64_srlz_i();
		for (i=0; i < pmu_conf.num_dbrs; i++) {
			ia64_set_dbr(i, 0UL);
			ia64_srlz_d();
		}
		ia64_srlz_d();
	}

	/*
	 * Now install the values into the registers
	 */
	for (i = 0; i < count; i++, req++) {

		rnum      = req->dbreg_num;
		dbreg.val = req->dbreg_value;

		ret = -EINVAL;

		if ((mode == PFM_CODE_RR && !IBR_IS_IMPL(rnum)) || ((mode == PFM_DATA_RR) && !DBR_IS_IMPL(rnum))) {
			DPRINT(("invalid register %u val=0x%lx mode=%d i=%d count=%d\n",
				  rnum, dbreg.val, mode, i, count));

			goto abort_mission;
		}

		/*
		 * make sure we do not install enabled breakpoint
		 */
		if (rnum & 0x1) {
			if (mode == PFM_CODE_RR)
				dbreg.ibr.ibr_x = 0;
			else
				dbreg.dbr.dbr_r = dbreg.dbr.dbr_w = 0;
		}

		PFM_REG_RETFLAG_SET(req->dbreg_flags, 0);

		/*
		 * Debug registers, just like PMC, can only be modified
		 * by a kernel call. Moreover, perfmon() access to those
		 * registers are centralized in this routine. The hardware
		 * does not modify the value of these registers, therefore,
		 * if we save them as they are written, we can avoid having
		 * to save them on context switch out. This is made possible
		 * by the fact that when perfmon uses debug registers, ptrace()
		 * won't be able to modify them concurrently.
		 */
		if (mode == PFM_CODE_RR) {
			CTX_USED_IBR(ctx, rnum);

			if (can_access_pmu) ia64_set_ibr(rnum, dbreg.val);

			ctx->ctx_ibrs[rnum] = dbreg.val;

			DPRINT(("write ibr%u=0x%lx used_ibrs=0x%x is_loaded=%d access_pmu=%d\n",
				rnum, dbreg.val, ctx->ctx_used_ibrs[0], is_loaded, can_access_pmu));
		} else {
			CTX_USED_DBR(ctx, rnum);

			if (can_access_pmu) ia64_set_dbr(rnum, dbreg.val);

			ctx->ctx_dbrs[rnum] = dbreg.val;

			DPRINT(("write dbr%u=0x%lx used_dbrs=0x%x is_loaded=%d access_pmu=%d\n",
				rnum, dbreg.val, ctx->ctx_used_dbrs[0], is_loaded, can_access_pmu));
		}
	}

	return 0;

abort_mission:
	/*
	 * in case it was our first attempt, we undo the global modifications
	 */
	if (first_time) {
		LOCK_PFS();
		if (ctx->ctx_fl_system) {
			pfm_sessions.pfs_sys_use_dbregs--;
		}
		UNLOCK_PFS();
		ctx->ctx_fl_using_dbreg = 0;
	}
	/*
	 * install error return flag
	 */
	PFM_REG_RETFLAG_SET(req->dbreg_flags, PFM_REG_RETFL_EINVAL);

	return ret;
}

static int
pfm_write_ibrs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	return pfm_write_ibr_dbr(PFM_CODE_RR, ctx, arg, count, regs);
}

static int
pfm_write_dbrs(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	return pfm_write_ibr_dbr(PFM_DATA_RR, ctx, arg, count, regs);
}

static int
pfm_get_features(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_features_t *req = (pfarg_features_t *)arg;

	req->ft_version = PFM_VERSION;
	return 0;
}

static int
pfm_stop(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct pt_regs *tregs;


	if (CTX_IS_LOADED(ctx) == 0 && CTX_IS_MASKED(ctx) == 0) return -EINVAL;

	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
		return -EBUSY;
	}

	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (ctx->ctx_fl_system) {
		/*
		 * Update local PMU first
		 *
		 * disable dcr pp
		 */
		ia64_set_dcr(ia64_get_dcr() & ~IA64_DCR_PP);
		ia64_srlz_i();

		/*
		 * update local cpuinfo
		 */
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP);

		/*
		 * stop monitoring, does srlz.i
		 */
		pfm_clear_psr_pp();

		/*
		 * stop monitoring in the caller
		 */
		ia64_psr(regs)->pp = 0;

		return 0;
	}
	/*
	 * per-task mode
	 */

	if (ctx->ctx_task == current) {
		/* stop monitoring  at kernel level */
		pfm_clear_psr_up();

		/*
	 	 * stop monitoring at the user level
	 	 */
		ia64_psr(regs)->up = 0;
	} else {
		tregs = ia64_task_regs(ctx->ctx_task);

		/*
	 	 * stop monitoring at the user level
	 	 */
		ia64_psr(tregs)->up = 0;

		/*
		 * monitoring disabled in kernel at next reschedule
		 */
		ctx->ctx_saved_psr_up = 0;
		printk("pfm_stop: current [%d] task=[%d]\n", current->pid, ctx->ctx_task->pid);
	}
	return 0;
}


static int
pfm_start(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct pt_regs *tregs;

	if (CTX_IS_LOADED(ctx) == 0) return -EINVAL;

	/*
 	 * In system wide and when the context is loaded, access can only happen
 	 * when the caller is running on the CPU being monitored by the session.
 	 * It does not have to be the owner (ctx_task) of the context per se.
 	 */
	if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
		return -EBUSY;
	}

	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (ctx->ctx_fl_system) {

		/*
		 * set user level psr.pp for the caller
		 */
		ia64_psr(regs)->pp = 1;

		/*
		 * now update the local PMU and cpuinfo
		 */
		PFM_CPUINFO_SET(PFM_CPUINFO_DCR_PP);

		/*
		 * start monitoring at kernel level
		 */
		pfm_set_psr_pp();

		/* enable dcr pp */
		ia64_set_dcr(ia64_get_dcr()|IA64_DCR_PP);
		ia64_srlz_i();

		return 0;
	}

	/*
	 * per-process mode
	 */

	if (ctx->ctx_task == current) {

		/* start monitoring at kernel level */
		pfm_set_psr_up();

		/*
		 * activate monitoring at user level
		 */
		ia64_psr(regs)->up = 1;

	} else {
		tregs = ia64_task_regs(ctx->ctx_task);

		/*
		 * start monitoring at the kernel level the next
		 * time the task is scheduled
		 */
		ctx->ctx_saved_psr_up = IA64_PSR_UP;

		/*
		 * activate monitoring at user level
		 */
		ia64_psr(tregs)->up = 1;
	}

	return 0;
}

static int
pfm_get_pmc_reset(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	pfarg_reg_t *req = (pfarg_reg_t *)arg;
	unsigned int cnum;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < count; i++, req++) {

		cnum = req->reg_num;

		if (!PMC_IS_IMPL(cnum)) goto abort_mission;

		req->reg_value = PMC_DFL_VAL(cnum);

		PFM_REG_RETFLAG_SET(req->reg_flags, 0);

		DPRINT(("pmc_reset_val pmc[%u]=0x%lx\n", cnum, req->reg_value));
	}
	return 0;

abort_mission:
	PFM_REG_RETFLAG_SET(req->reg_flags, PFM_REG_RETFL_EINVAL);
	return ret;
}

static int
pfm_context_load(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task;
	struct thread_struct *thread;
	struct pfm_context_t *old;
#ifndef CONFIG_SMP
	struct task_struct *owner_task = NULL;
#endif
	pfarg_load_t *req = (pfarg_load_t *)arg;
	unsigned long *pmcs_source, *pmds_source;
	int the_cpu;
	int ret = 0;

	/*
	 * can only load from unloaded or terminated state
	 */
	if (CTX_IS_UNLOADED(ctx) == 0 && CTX_IS_TERMINATED(ctx) == 0) {
		DPRINT(("[%d] cannot load to [%d], invalid ctx_state=%d\n",
			current->pid,
			req->load_pid,
			ctx->ctx_state));
		return -EINVAL;
	}

	DPRINT(("load_pid [%d]\n", req->load_pid));

	if (CTX_OVFL_NOBLOCK(ctx) == 0 && req->load_pid == current->pid) {
		DPRINT(("cannot use blocking mode on self for [%d]\n", current->pid));
		return -EINVAL;
	}

	ret = pfm_get_task(ctx, req->load_pid, &task);
	if (ret) {
		DPRINT(("load_pid [%d] get_task=%d\n", req->load_pid, ret));
		return ret;
	}

	ret = -EINVAL;

	/*
	 * system wide is self monitoring only
	 */
	if (ctx->ctx_fl_system && task != current) {
		DPRINT(("system wide is self monitoring only current=%d load_pid=%d\n",
			current->pid,
			req->load_pid));
		goto error;
	}

	thread = &task->thread;

	ret = -EBUSY;

	/*
	 * cannot load a context which is using range restrictions,
	 * into a task that is being debugged.
	 */
	if (ctx->ctx_fl_using_dbreg && (thread->flags & IA64_THREAD_DBG_VALID)) {
		DPRINT(("load_pid [%d] task is debugged, cannot load range restrictions\n", req->load_pid));
		goto error;
	}

	/*
	 * SMP system-wide monitoring implies self-monitoring.
	 *
	 * The programming model expects the task to
	 * be pinned on a CPU throughout the session.
	 * Here we take note of the current CPU at the
	 * time the context is loaded. No call from
	 * another CPU will be allowed.
	 *
	 * The pinning via shed_setaffinity()
	 * must be done by the calling task prior
	 * to this call.
	 *
	 * systemwide: keep track of CPU this session is supposed to run on
	 */
	the_cpu = ctx->ctx_cpu = smp_processor_id();

	/*
	 * now reserve the session
	 */
	ret = pfm_reserve_session(current, ctx->ctx_fl_system, the_cpu);
	if (ret) goto error;

	ret = -EBUSY;
	/*
	 * task is necessarily stopped at this point.
	 *
	 * If the previous context was zombie, then it got removed in
	 * pfm_save_regs(). Therefore we should not see it here.
	 * If we see a context, then this is an active context
	 *
	 * XXX: needs to be atomic
	 */
	DPRINT(("[%d] before cmpxchg() old_ctx=%p new_ctx=%p\n",
		current->pid, 
		thread->pfm_context, ctx));

	old = ia64_cmpxchg("acq", &thread->pfm_context, NULL, ctx, sizeof(pfm_context_t *));
	if (old != NULL) {
		DPRINT(("load_pid [%d] already has a context\n", req->load_pid));
		goto error_unres;
	}

	pfm_reset_msgq(ctx);

	CTX_LOADED(ctx);

	/*
	 * link context to task
	 */
	ctx->ctx_task = task;

	if (ctx->ctx_fl_system) {

		/*
		 * we load as stopped
		 */
		PFM_CPUINFO_SET(PFM_CPUINFO_SYST_WIDE);
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_DCR_PP);

		if (ctx->ctx_fl_excl_idle) PFM_CPUINFO_SET(PFM_CPUINFO_EXCL_IDLE);
	} else {
		thread->flags |= IA64_THREAD_PM_VALID;
	}

	/*
	 * propagate into thread-state
	 */
	pfm_copy_pmds(task, ctx);
	pfm_copy_pmcs(task, ctx);

	pmcs_source = thread->pmcs;
	pmds_source = thread->pmds;

	/*
	 * always the case for system-wide
	 */
	if (task == current) {

		if (ctx->ctx_fl_system == 0) {

			/* allow user level control */
			ia64_psr(regs)->sp = 0;
			DPRINT(("clearing psr.sp for [%d]\n", task->pid));

			SET_LAST_CPU(ctx, smp_processor_id());
			INC_ACTIVATION();
			SET_ACTIVATION(ctx);
#ifndef CONFIG_SMP
			/*
			 * push the other task out, if any
			 */
			owner_task = GET_PMU_OWNER();
			if (owner_task) pfm_lazy_save_regs(owner_task);
#endif
		}
		/*
		 * load all PMD from ctx to PMU (as opposed to thread state)
		 * restore all PMC from ctx to PMU
		 */
		pfm_restore_pmds(pmds_source, ctx->ctx_all_pmds[0]);
		pfm_restore_pmcs(pmcs_source, ctx->ctx_all_pmcs[0]);

		ctx->ctx_reload_pmcs[0] = 0UL;
		ctx->ctx_reload_pmds[0] = 0UL;

		/*
		 * guaranteed safe by earlier check against DBG_VALID
		 */
		if (ctx->ctx_fl_using_dbreg) {
			pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf.num_ibrs);
			pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf.num_dbrs);
		}
		/*
		 * set new ownership
		 */
		SET_PMU_OWNER(task, ctx);

		DPRINT(("context loaded on PMU for [%d]\n", task->pid));
	} else {
		/*
		 * when not current, task MUST be stopped, so this is safe
		 */
		regs = ia64_task_regs(task);

		/* force a full reload */
		ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
		SET_LAST_CPU(ctx, -1);

		/* initial saved psr (stopped) */
		ctx->ctx_saved_psr_up = 0UL;
		ia64_psr(regs)->up = ia64_psr(regs)->pp = 0;

		if (ctx->ctx_fl_unsecure) {
			ia64_psr(regs)->sp = 0;
			DPRINT(("context unsecured for [%d]\n", task->pid));
		}
	}

	ret = 0;

error_unres:
	if (ret) pfm_unreserve_session(ctx, ctx->ctx_fl_system, the_cpu);
error:
	/*
	 * release task, there is now a link with the context
	 */
	if (ctx->ctx_fl_system == 0 && task != current) pfm_put_task(task);

	return ret;
}

/*
 * in this function, we do not need to increase the use count
 * for the task via get_task_struct(), because we hold the
 * context lock. If the task were to disappear while having
 * a context attached, it would go through pfm_exit_thread()
 * which also grabs the context lock  and would therefore be blocked
 * until we are here.
 */
static void pfm_flush_pmds(struct task_struct *, pfm_context_t *ctx);

static int
pfm_context_unload(pfm_context_t *ctx, void *arg, int count, struct pt_regs *regs)
{
	struct task_struct *task = ctx->ctx_task;
	struct pt_regs *tregs;

	DPRINT(("ctx_state=%d task [%d]\n", ctx->ctx_state, task ? task->pid : -1));

	/*
	 * unload only when necessary
	 */
	if (CTX_IS_TERMINATED(ctx) || CTX_IS_UNLOADED(ctx)) {
		DPRINT(("[%d] ctx_state=%d, nothing to do\n", current->pid, ctx->ctx_state));
		return 0;
	}

	/*
	 * In system wide and when the context is loaded, access can only happen
	 * when the caller is running on the CPU being monitored by the session.
	 * It does not have to be the owner (ctx_task) of the context per se.
	 */
	if (ctx->ctx_fl_system && ctx->ctx_cpu != smp_processor_id()) {
		DPRINT(("[%d] should be running on CPU%d\n", current->pid, ctx->ctx_cpu));
		return -EBUSY;
	}

	/*
	 * clear psr and dcr bits
	 */
	pfm_stop(ctx, NULL, 0, regs);

	CTX_UNLOADED(ctx);

	/*
	 * in system mode, we need to update the PMU directly
	 * and the user level state of the caller, which may not
	 * necessarily be the creator of the context.
	 */
	if (ctx->ctx_fl_system) {

		/*
		 * Update cpuinfo
		 *
		 * local PMU is taken care of in pfm_stop()
		 */
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_SYST_WIDE);
		PFM_CPUINFO_CLEAR(PFM_CPUINFO_EXCL_IDLE);

		/*
		 * save PMDs in context
		 * release ownership
		 */
		pfm_flush_pmds(current, ctx);

		/*
		 * at this point we are done with the PMU
		 * so we can unreserve the resource.
		 */
		pfm_unreserve_session(ctx, 1 , ctx->ctx_cpu);

		/*
		 * disconnect context from task
		 */
		task->thread.pfm_context = NULL;
		/*
		 * disconnect task from context
		 */
		ctx->ctx_task = NULL;

		/*
		 * There is nothing more to cleanup here.
		 */
		return 0;
	}

	/*
	 * per-task mode
	 */
	tregs = task == current ? regs : ia64_task_regs(task);

	if (task == current || ctx->ctx_fl_unsecure) {
		/*
		 * cancel user level control
		 */
		ia64_psr(regs)->sp = 1;
		DPRINT(("setting psr.sp for [%d]\n", task->pid));

	}
	/*
	 * save PMDs to context
	 * release ownership
	 */
	pfm_flush_pmds(task, ctx);

	/*
	 * at this point we are done with the PMU
	 * so we can unreserve the resource.
	 */
	pfm_unreserve_session(ctx, 0 , ctx->ctx_cpu);

	/*
	 * reset activation counter and psr
	 */
	ctx->ctx_last_activation = PFM_INVALID_ACTIVATION;
	SET_LAST_CPU(ctx, -1);

	/*
	 * PMU state will not be restored
	 */
	task->thread.flags &= ~IA64_THREAD_PM_VALID;

	/*
	 * break links between context and task
	 */
	task->thread.pfm_context  = NULL;
	ctx->ctx_task             = NULL;

	PFM_SET_WORK_PENDING(task, 0);
	ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_NONE;

	DPRINT(("disconnected [%d] from context\n", task->pid));

	return 0;
}

static void
pfm_force_cleanup(pfm_context_t *ctx, struct pt_regs *regs)
{
	struct task_struct *task = ctx->ctx_task;

	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;

	if (GET_PMU_OWNER() == task) {
		DPRINT(("cleared ownership for [%d]\n", ctx->ctx_task->pid));
		SET_PMU_OWNER(NULL, NULL);
	}

	/*
	 * disconnect the task from the context and vice-versa
	 */
	PFM_SET_WORK_PENDING(task, 0);

	task->thread.pfm_context  = NULL;
	task->thread.flags       &= ~IA64_THREAD_PM_VALID;

	DPRINT(("context <%d> force cleanup for [%d] by [%d]\n", ctx->ctx_fd, task->pid, current->pid));
}


/*
 * called only from exit_thread(): task == current
 */
void
pfm_exit_thread(struct task_struct *task)
{
	pfm_context_t *ctx;
	unsigned long flags;
	struct pt_regs *regs = ia64_task_regs(task);
	int ret;
	int free_ok = 0;

	ctx = PFM_GET_CTX(task);

	PROTECT_CTX(ctx, flags);

	DPRINT(("state=%d task [%d]\n", ctx->ctx_state, task->pid));

	/*
	 * come here only if attached
	 */
	if (unlikely(CTX_IS_UNLOADED(ctx))) {
		printk(KERN_ERR "perfmon: pfm_exit_thread [%d] ctx unloaded\n", task->pid);
		goto skip_all;
	}

	if (CTX_IS_LOADED(ctx) || CTX_IS_MASKED(ctx)) {

		ret = pfm_context_unload(ctx, NULL, 0, regs);
		if (ret) {
			printk(KERN_ERR "perfmon: pfm_exit_thread [%d] state=%d unload failed %d\n", task->pid, ctx->ctx_state, ret);
		}
		CTX_TERMINATED(ctx);
		DPRINT(("ctx terminated by [%d]\n", task->pid));

		pfm_end_notify_user(ctx);

	} else if (CTX_IS_ZOMBIE(ctx)) {
		pfm_clear_psr_up();

		BUG_ON(ctx->ctx_smpl_hdr);

		pfm_force_cleanup(ctx, regs);

		free_ok = 1;
	}
	{ u64 psr = pfm_get_psr();
	  BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	}
skip_all:
	UNPROTECT_CTX(ctx, flags);

	/*
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (free_ok) pfm_context_free(ctx);
}

/*
 * functions MUST be listed in the increasing order of their index (see permfon.h)
 */
#define PFM_CMD(name, flags, arg_count, arg_type, getsz) { name, #name, flags, arg_count, sizeof(arg_type), getsz }
#define PFM_CMD_S(name, flags) { name, #name, flags, 0, 0, NULL }
#define PFM_CMD_PCLRWS	(PFM_CMD_FD|PFM_CMD_ARG_RW|PFM_CMD_STOP)
#define PFM_CMD_PCLRW	(PFM_CMD_FD|PFM_CMD_ARG_RW)
#define PFM_CMD_NONE	{ NULL, "no-cmd", 0, 0, 0, NULL}

static pfm_cmd_desc_t pfm_cmd_tab[]={
/* 0  */PFM_CMD_NONE,
/* 1  */PFM_CMD(pfm_write_pmcs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 2  */PFM_CMD(pfm_write_pmds, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 3  */PFM_CMD(pfm_read_pmds, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 4  */PFM_CMD_S(pfm_stop, PFM_CMD_PCLRWS),
/* 5  */PFM_CMD_S(pfm_start, PFM_CMD_PCLRWS),
/* 6  */PFM_CMD_NONE,
/* 7  */PFM_CMD_NONE,
/* 8  */PFM_CMD(pfm_context_create, PFM_CMD_ARG_RW, 1, pfarg_context_t, pfm_ctx_getsize),
/* 9  */PFM_CMD_NONE,
/* 10 */PFM_CMD_S(pfm_restart, PFM_CMD_PCLRW),
/* 11 */PFM_CMD_NONE,
/* 12 */PFM_CMD(pfm_get_features, PFM_CMD_ARG_RW, 1, pfarg_features_t, NULL),
/* 13 */PFM_CMD(pfm_debug, 0, 1, unsigned int, NULL),
/* 14 */PFM_CMD_NONE,
/* 15 */PFM_CMD(pfm_get_pmc_reset, PFM_CMD_ARG_RW, PFM_CMD_ARG_MANY, pfarg_reg_t, NULL),
/* 16 */PFM_CMD(pfm_context_load, PFM_CMD_PCLRWS, 1, pfarg_load_t, NULL),
/* 17 */PFM_CMD_S(pfm_context_unload, PFM_CMD_PCLRWS),
/* 18 */PFM_CMD_NONE,
/* 19 */PFM_CMD_NONE,
/* 20 */PFM_CMD_NONE,
/* 21 */PFM_CMD_NONE,
/* 22 */PFM_CMD_NONE,
/* 23 */PFM_CMD_NONE,
/* 24 */PFM_CMD_NONE,
/* 25 */PFM_CMD_NONE,
/* 26 */PFM_CMD_NONE,
/* 27 */PFM_CMD_NONE,
/* 28 */PFM_CMD_NONE,
/* 29 */PFM_CMD_NONE,
/* 30 */PFM_CMD_NONE,
/* 31 */PFM_CMD_NONE,
/* 32 */PFM_CMD(pfm_write_ibrs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_dbreg_t, NULL),
/* 33 */PFM_CMD(pfm_write_dbrs, PFM_CMD_PCLRWS, PFM_CMD_ARG_MANY, pfarg_dbreg_t, NULL)
};
#define PFM_CMD_COUNT	(sizeof(pfm_cmd_tab)/sizeof(pfm_cmd_desc_t))

static int
pfm_check_task_state(pfm_context_t *ctx, int cmd, unsigned long flags)
{
	struct task_struct *task;

	task = PFM_CTX_TASK(ctx);
	if (task == NULL) {
		DPRINT(("context %d no task, state=%d\n", ctx->ctx_fd, ctx->ctx_state));
		return 0;
	}

	DPRINT(("context %d state=%d [%d] task_state=%ld must_stop=%d\n",
				ctx->ctx_fd,
				ctx->ctx_state,
				task->pid,
				task->state, PFM_CMD_STOPPED(cmd)));

	/*
	 * self-monitoring always ok.
	 *
	 * for system-wide the caller can either be the creator of the
	 * context (to one to which the context is attached to) OR
	 * a task running on the same CPU as the session.
	 */
	if (task == current || ctx->ctx_fl_system) return 0;

	/*
	 * context is UNLOADED, MASKED, TERMINATED we are safe to go
	 */
	if (CTX_IS_LOADED(ctx) == 0) return 0;

	if (CTX_IS_ZOMBIE(ctx)) return -EINVAL;

	/*
	 * context is loaded, we must make sure the task is stopped
	 * We could lift this restriction for UP but it would mean that
	 * the user has no guarantee the task would not run between
	 * two successive calls to perfmonctl(). That's probably OK.
	 * If this user wants to ensure the task does not run, then
	 * the task must be stopped.
	 */
	if (PFM_CMD_STOPPED(cmd) && task->state != TASK_STOPPED) {
		DPRINT(("[%d] task not in stopped state\n", task->pid));
		return -EBUSY;
	}

	UNPROTECT_CTX(ctx, flags);

	pfm_wait_task_inactive(task);

	PROTECT_CTX(ctx, flags);
	return 0;
}

/*
 * system-call entry point (must return long)
 */
asmlinkage long
sys_perfmonctl (int fd, int cmd, void *arg, int count, long arg5, long arg6, long arg7,
		long arg8, long stack)
{
	struct pt_regs *regs = (struct pt_regs *)&stack;
	struct file *file = NULL;
	pfm_context_t *ctx = NULL;
	unsigned long flags = 0UL;
	void *args_k = NULL;
	long ret; /* will expand int return types */
	size_t base_sz, sz, xtra_sz = 0;
	int narg, completed_args = 0, call_made = 0;
#define PFM_MAX_ARGSIZE	4096

	/*
	 * reject any call if perfmon was disabled at initialization time
	 */
	if (PFM_IS_DISABLED()) return -ENOSYS;

	if (unlikely(PFM_CMD_IS_VALID(cmd) == 0)) {
		DPRINT(("[%d] invalid cmd=%d\n", current->pid, cmd));
		return -EINVAL;
	}

	DPRINT(("cmd=%s idx=%d valid=%d narg=0x%x argsz=%lu count=%d\n",
		PFM_CMD_NAME(cmd),
		PFM_CMD_IDX(cmd),
		PFM_CMD_IS_VALID(cmd),
		PFM_CMD_NARG(cmd),
		PFM_CMD_ARG_SIZE(cmd), count));

	/*
	 * check if number of arguments matches what the command expects
	 */
	narg = PFM_CMD_NARG(cmd);
	if ((narg == PFM_CMD_ARG_MANY && count <= 0) || (narg > 0 && narg != count))
		return -EINVAL;

	/* get single argument size */
	base_sz = PFM_CMD_ARG_SIZE(cmd);

restart_args:
	sz = xtra_sz + base_sz*count;
	/*
	 * limit abuse to min page size
	 */
	if (unlikely(sz > PFM_MAX_ARGSIZE)) {
		printk(KERN_ERR "perfmon: [%d] argument too big %lu\n", current->pid, sz);
		return -E2BIG;
	}

	/*
	 * allocate default-sized argument buffer
	 */
	if (count && args_k == NULL) {
		args_k = kmalloc(PFM_MAX_ARGSIZE, GFP_KERNEL);
		if (args_k == NULL) return -ENOMEM;
	}

	ret = -EFAULT;

	/*
	 * copy arguments
	 *
	 * assume sz = 0 for command without parameters
	 */
	if (sz && copy_from_user(args_k, arg, sz)) {
		DPRINT(("[%d] cannot copy_from_user %lu bytes @%p\n", current->pid, sz, arg));
		goto error_args;
	}

	/*
	 * check if command supports extra parameters
	 */
	if (completed_args == 0 && PFM_CMD_GETSIZE(cmd)) {
		/*
		 * get extra parameters size (based on main argument)
		 */
		ret = PFM_CMD_GETSIZE(cmd)(args_k, &xtra_sz);
		if (ret) goto error_args;

		completed_args = 1;

		DPRINT(("[%d] restart_args sz=%lu xtra_sz=%lu\n", current->pid, sz, xtra_sz));

		/* retry if necessary */
		if (xtra_sz) goto restart_args;
	}

	if (PFM_CMD_USE_FD(cmd))  {

		ret = -EBADF;

		file = fget(fd);
		if (file == NULL) {
			DPRINT(("[%d] invalid fd %d\n", current->pid, fd));
			goto error_args;
		}
		if (PFM_IS_FILE(file) == 0) {
			DPRINT(("[%d] fd %d not related to perfmon\n", current->pid, fd));
			goto error_args;
		}


		ctx = (pfm_context_t *)file->private_data;
		if (ctx == NULL) {
			DPRINT(("[%d] no context for fd %d\n", current->pid, fd));
			goto error_args;
		}

		PROTECT_CTX(ctx, flags);

		/*
		 * check task is stopped
		 */
		ret = pfm_check_task_state(ctx, cmd, flags);
		if (ret) goto abort_locked;
	}

	ret = (*pfm_cmd_tab[PFM_CMD_IDX(cmd)].cmd_func)(ctx, args_k, count, regs);

	call_made = 1;

abort_locked:
	if (ctx) {
		DPRINT(("[%d] context unlocked\n", current->pid));
		UNPROTECT_CTX(ctx, flags);
		fput(file);
	}

	/* copy argument back to user, if needed */
	if (call_made && PFM_CMD_RW_ARG(cmd) && copy_to_user(arg, args_k, base_sz*count)) ret = -EFAULT;

error_args:
	if (args_k) kfree(args_k);

	return ret;
}

static void
pfm_resume_after_ovfl(pfm_context_t *ctx, unsigned long ovfl_regs, struct pt_regs *regs)
{
	pfm_buffer_fmt_t *fmt = ctx->ctx_buf_fmt;
	pfm_ovfl_ctrl_t rst_ctrl;
	int ret = 0;

	/*
	 * Unlock sampling buffer and reset index atomically
	 * XXX: not really needed when blocking
	 */
	if (CTX_HAS_SMPL(ctx)) {

		rst_ctrl.stop_monitoring = 1;
		rst_ctrl.reset_pmds      = PFM_PMD_NO_RESET;

		/* XXX: check return value */
		if (fmt->fmt_restart)
			ret = (*fmt->fmt_restart)(current, &rst_ctrl, ctx->ctx_smpl_hdr, regs);
	} else {
		rst_ctrl.stop_monitoring = 0;
		rst_ctrl.reset_pmds      = PFM_PMD_LONG_RESET;
	}

	if (ret == 0) {
		if (rst_ctrl.reset_pmds != PFM_PMD_NO_RESET)
			pfm_reset_regs(ctx, &ovfl_regs, rst_ctrl.reset_pmds);

		if (rst_ctrl.stop_monitoring == 0) {
			DPRINT(("resuming monitoring\n"));
			if (CTX_IS_MASKED(ctx)) pfm_restore_monitoring(current);
		} else {
			DPRINT(("stopping monitoring\n"));
			//pfm_stop_monitoring(current, regs);
		}
		CTX_LOADED(ctx);
	}
}


/*
 * context MUST BE LOCKED when calling
 * can only be called for current
 */
static void
pfm_context_force_terminate(pfm_context_t *ctx, struct pt_regs *regs)
{
	if (ctx->ctx_fl_system) {
		printk(KERN_ERR "perfmon: pfm_context_force_terminate [%d] is system-wide\n", current->pid);
		return;
	}
	/*
	 * we stop the whole thing, we do no need to flush
	 * we know we WERE masked
	 */
	pfm_clear_psr_up();
	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;

	/*
	 * disconnect the task from the context and vice-versa
	 */
	current->thread.pfm_context  = NULL;
	current->thread.flags       &= ~IA64_THREAD_PM_VALID;
	ctx->ctx_task = NULL;

	/*
	 * switch to terminated state
	 */
	CTX_TERMINATED(ctx);

	DPRINT(("context <%d> terminated for [%d]\n", ctx->ctx_fd, current->pid));

	/*
	 * and wakeup controlling task, indicating we are now disconnected
	 */
	wake_up_interruptible(&ctx->ctx_zombieq);

	/*
	 * given that context is still locked, the controlling
	 * task will only get access when we return from
	 * pfm_handle_work().
	 */
}

static int pfm_ovfl_notify_user(pfm_context_t *ctx, unsigned long ovfl_pmds);

void
pfm_handle_work(void)
{
	pfm_context_t *ctx;
	struct pt_regs *regs;
	unsigned long flags;
	unsigned long ovfl_regs;
	unsigned int reason;
	int ret;

	ctx = PFM_GET_CTX(current);
	if (ctx == NULL) {
		printk(KERN_ERR "perfmon: [%d] has no PFM context\n", current->pid);
		return;
	}

	PROTECT_CTX(ctx, flags);

	PFM_SET_WORK_PENDING(current, 0);

	pfm_clear_task_notify();

	regs = ia64_task_regs(current);

	/*
	 * extract reason for being here and clear
	 */
	reason = ctx->ctx_fl_trap_reason;
	ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_NONE;

	DPRINT(("[%d] reason=%d\n", current->pid, reason));

	/*
	 * must be done before we check non-blocking mode
	 */
	if (ctx->ctx_fl_going_zombie || CTX_IS_ZOMBIE(ctx)) goto do_zombie;

	ovfl_regs = ctx->ctx_ovfl_regs[0];

	//if (CTX_OVFL_NOBLOCK(ctx)) goto skip_blocking;
	if (reason == PFM_TRAP_REASON_RESET) goto skip_blocking;

	UNPROTECT_CTX(ctx, flags);

	DPRINT(("before block sleeping\n"));

	/*
	 * may go through without blocking on SMP systems
	 * if restart has been received already by the time we call down()
	 */
	ret = down_interruptible(&ctx->ctx_restart_sem);

	DPRINT(("after block sleeping ret=%d\n", ret));

	PROTECT_CTX(ctx, flags);

	if (ctx->ctx_fl_going_zombie) {
do_zombie:
		DPRINT(("context is zombie, bailing out\n"));
		pfm_context_force_terminate(ctx, regs);
		goto nothing_to_do;
	}
	/*
	 * in case of interruption of down() we don't restart anything
	 */
	if (ret < 0) goto nothing_to_do;

skip_blocking:
	pfm_resume_after_ovfl(ctx, ovfl_regs, regs);
	ctx->ctx_ovfl_regs[0] = 0UL;

nothing_to_do:

	UNPROTECT_CTX(ctx, flags);
}

static int
pfm_notify_user(pfm_context_t *ctx, pfm_msg_t *msg)
{
	if (CTX_IS_ZOMBIE(ctx)) {
		DPRINT(("ignoring overflow notification, owner is zombie\n"));
		return 0;
	}

	DPRINT(("[%d] waking up somebody\n", current->pid));

	if (msg) wake_up_interruptible(&ctx->ctx_msgq_wait);

	/*
	 * safe, we are not in intr handler, nor in ctxsw when
	 * we come here
	 */
	kill_fasync (&ctx->ctx_async_queue, SIGIO, POLL_IN);

	return 0;
}

static int
pfm_ovfl_notify_user(pfm_context_t *ctx, unsigned long ovfl_pmds)
{
	pfm_msg_t *msg = NULL;

	if (ctx->ctx_fl_no_msg == 0) {
		msg = pfm_get_new_msg(ctx);
		if (msg == NULL) {
			printk(KERN_ERR "perfmon: pfm_ovfl_notify_user no more notification msgs\n");
			return -1;
		}

		msg->pfm_ovfl_msg.msg_type         = PFM_MSG_OVFL;
		msg->pfm_ovfl_msg.msg_ctx_fd       = ctx->ctx_fd;
		msg->pfm_ovfl_msg.msg_tstamp       = ia64_get_itc(); /* relevant on UP only */
		msg->pfm_ovfl_msg.msg_active_set   = 0;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[0] = ovfl_pmds;
		msg->pfm_ovfl_msg.msg_ovfl_pmds[1] = msg->pfm_ovfl_msg.msg_ovfl_pmds[2] = msg->pfm_ovfl_msg.msg_ovfl_pmds[3] = 0UL;

	}

	DPRINT(("ovfl msg: msg=%p no_msg=%d fd=%d pid=%d ovfl_pmds=0x%lx\n",
		msg,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd,
		current->pid,
		ovfl_pmds));

	return pfm_notify_user(ctx, msg);
}

static int
pfm_end_notify_user(pfm_context_t *ctx)
{
	pfm_msg_t *msg;

	msg = pfm_get_new_msg(ctx);
	if (msg == NULL) {
		printk(KERN_ERR "perfmon: pfm_end_notify_user no more notification msgs\n");
		return -1;
	}

	msg->pfm_end_msg.msg_type    = PFM_MSG_END;
	msg->pfm_end_msg.msg_ctx_fd  = ctx->ctx_fd;
	msg->pfm_ovfl_msg.msg_tstamp = ia64_get_itc(); /* relevant on UP only */

	DPRINT(("end msg: msg=%p no_msg=%d ctx_fd=%d pid=%d\n",
		msg,
		ctx->ctx_fl_no_msg,
		ctx->ctx_fd, current->pid));

	return pfm_notify_user(ctx, msg);
}

/*
 * main overflow processing routine.
 * it can be called from the interrupt path or explicitely during the context switch code
 */
static void
pfm_overflow_handler(struct task_struct *task, pfm_context_t *ctx, u64 pmc0, struct pt_regs *regs)
{
	pfm_ovfl_arg_t ovfl_arg;
	unsigned long mask;
	unsigned long old_val;
	unsigned long ovfl_notify = 0UL, ovfl_pmds = 0UL, smpl_pmds = 0UL;
	pfm_ovfl_ctrl_t	ovfl_ctrl;
	unsigned int i, j, has_smpl, first_pmd = ~0U;
	int must_notify = 0;

	if (unlikely(CTX_IS_ZOMBIE(ctx))) goto stop_monitoring;

	/*
	 * sanity test. Should never happen
	 */
	if (unlikely((pmc0 & 0x1) == 0)) goto sanity_check;

	mask = pmc0 >> PMU_FIRST_COUNTER;

	DPRINT_ovfl(("pmc0=0x%lx pid=%d iip=0x%lx, %s"
		     "used_pmds=0x%lx reload_pmcs=0x%lx\n",
			pmc0,
			task ? task->pid: -1,
			(regs ? regs->cr_iip : 0),
			CTX_OVFL_NOBLOCK(ctx) ? "nonblocking" : "blocking",
			ctx->ctx_used_pmds[0],
			ctx->ctx_reload_pmcs[0]));

	has_smpl = CTX_HAS_SMPL(ctx);

	/*
	 * first we update the virtual counters
	 * assume there was a prior ia64_srlz_d() issued
	 */
	for (i = PMU_FIRST_COUNTER; mask ; i++, mask >>= 1) {

		/* skip pmd which did not overflow */
		if ((mask & 0x1) == 0) continue;

		DPRINT_ovfl(("pmd[%d] overflowed hw_pmd=0x%lx ctx_pmd=0x%lx\n",
					i, ia64_get_pmd(i), ctx->ctx_pmds[i].val));

		/*
		 * Note that the pmd is not necessarily 0 at this point as qualified events
		 * may have happened before the PMU was frozen. The residual count is not
		 * taken into consideration here but will be with any read of the pmd via
		 * pfm_read_pmds().
		 */
		old_val               = ctx->ctx_pmds[i].val;
		ctx->ctx_pmds[i].val += 1 + pmu_conf.ovfl_val;

		/*
		 * check for overflow condition
		 */
		if (likely(old_val > ctx->ctx_pmds[i].val)) {

			ovfl_pmds |= 1UL << i;

			/*
			 * keep track of pmds of interest for samples
			 */
			if (has_smpl) {
				if (first_pmd == ~0U) first_pmd = i;
				smpl_pmds |= ctx->ctx_pmds[i].smpl_pmds[0];
			}

			if (PMC_OVFL_NOTIFY(ctx, i)) ovfl_notify |= 1UL << i;
		}

		DPRINT_ovfl(("ctx_pmd[%d].val=0x%lx old_val=0x%lx pmd=0x%lx ovfl_pmds=0x%lx ovfl_notify=0x%lx first_pmd=%u smpl_pmds=0x%lx\n",
					i, ctx->ctx_pmds[i].val, old_val,
					ia64_get_pmd(i) & pmu_conf.ovfl_val, ovfl_pmds, ovfl_notify, first_pmd, smpl_pmds));
	}

	ovfl_ctrl.notify_user     = ovfl_notify ? 1 : 0;
	ovfl_ctrl.reset_pmds      = ovfl_pmds && ovfl_notify == 0UL ? 1 : 0;
	ovfl_ctrl.block           = ovfl_notify ? 1 : 0;
	ovfl_ctrl.stop_monitoring = ovfl_notify ? 1 : 0;

	/*
	 * when a overflow is detected, check for sampling buffer, if present, invoke
	 * record() callback.
	 */
	if (ovfl_pmds && has_smpl) {
		unsigned long start_cycles;
		int this_cpu = smp_processor_id();

		ovfl_arg.ovfl_pmds[0]   = ovfl_pmds;
		ovfl_arg.ovfl_notify[0] = ovfl_notify;
		ovfl_arg.ovfl_ctrl      = ovfl_ctrl;
		ovfl_arg.smpl_pmds[0]   = smpl_pmds;

		prefetch(ctx->ctx_smpl_hdr);

		ovfl_arg.pmd_value      = ctx->ctx_pmds[first_pmd].val;
		ovfl_arg.pmd_last_reset = ctx->ctx_pmds[first_pmd].lval;
		ovfl_arg.pmd_eventid    = ctx->ctx_pmds[first_pmd].eventid;

		/*
		 * copy values of pmds of interest. Sampling format may copy them
		 * into sampling buffer.
		 */
		if (smpl_pmds) {
			for(i=0, j=0; smpl_pmds; i++, smpl_pmds >>=1) {
				if ((smpl_pmds & 0x1) == 0) continue;
				ovfl_arg.smpl_pmds_values[j++] = PMD_IS_COUNTING(i) ?  pfm_read_soft_counter(ctx, i) : ia64_get_pmd(i);
			}
		}

		pfm_stats[this_cpu].pfm_smpl_handler_calls++;
		start_cycles = ia64_get_itc();

		/*
		 * call custom buffer format record (handler) routine
		 */
		(*ctx->ctx_buf_fmt->fmt_handler)(task, ctx->ctx_smpl_hdr, &ovfl_arg, regs);

		pfm_stats[this_cpu].pfm_smpl_handler_cycles += ia64_get_itc() - start_cycles;

		ovfl_pmds   = ovfl_arg.ovfl_pmds[0];
		ovfl_notify = ovfl_arg.ovfl_notify[0];
		ovfl_ctrl   = ovfl_arg.ovfl_ctrl;
	}

	if (ovfl_pmds && ovfl_ctrl.reset_pmds) {
		pfm_reset_regs(ctx, &ovfl_pmds, ovfl_ctrl.reset_pmds);
	}

	if (ovfl_notify && ovfl_ctrl.notify_user) {
		/*
		 * keep track of what to reset when unblocking
		 */
		ctx->ctx_ovfl_regs[0] = ovfl_pmds;

		if (CTX_OVFL_NOBLOCK(ctx) == 0 && ovfl_ctrl.block) {

			ctx->ctx_fl_trap_reason = PFM_TRAP_REASON_BLOCK;

			/*
			 * set the perfmon specific checking pending work
			 */
			PFM_SET_WORK_PENDING(task, 1);

			/*
			 * when coming from ctxsw, current still points to the
			 * previous task, therefore we must work with task and not current.
			 */
			pfm_set_task_notify(task);
		}
		/*
		 * defer until state is changed (shorten spin window). the context is locked
		 * anyway, so the signal receiver would come spin for nothing.
		 */
		must_notify = 1;
	}

	DPRINT_ovfl(("current [%d] owner [%d] pending=%ld reason=%u ovfl_pmds=0x%lx ovfl_notify=0x%lx stopped=%d\n",
			current->pid,
			GET_PMU_OWNER() ? GET_PMU_OWNER()->pid : -1,
			PFM_GET_WORK_PENDING(task),
			ctx->ctx_fl_trap_reason,
			ovfl_pmds,
			ovfl_notify,
			ovfl_ctrl.stop_monitoring ? 1 : 0));
	/*
	 * in case monitoring must be stopped, we toggle the psr bits
	 */
	if (ovfl_ctrl.stop_monitoring) {
		pfm_mask_monitoring(task);
		CTX_MASKED(ctx);
	}
	/*
	 * send notification now
	 */
	if (must_notify) pfm_ovfl_notify_user(ctx, ovfl_notify);

	return;


sanity_check:
	printk(KERN_ERR "perfmon: CPU%d overflow handler [%d] pmc0=0x%lx\n",
			smp_processor_id(),
			task ? task->pid : -1,
			pmc0);
	return;

stop_monitoring:
	/*
	 * in SMP, zombie context is never restored but reclaimed in pfm_load_regs().
	 * Moreover, zombies are also reclaimed in pfm_save_regs(). Therefore we can
	 * come here as zombie only if the task is the current task. In which case, we
	 * can access the PMU  hardware directly.
	 *
	 * Note that zombies do have PM_VALID set. So here we do the minimal.
	 *
	 * In case the context was zombified it could not be reclaimed at the time
	 * the monitoring program exited. At this point, the PMU reservation has been
	 * returned, the sampiing buffer has been freed. We must convert this call
	 * into a spurious interrupt. However, we must also avoid infinite overflows
	 * by stopping monitoring for this task. We can only come here for a per-task
	 * context. All we need to do is to stop monitoring using the psr bits which
	 * are always task private. By re-enabling secure montioring, we ensure that
	 * the monitored task will not be able to re-activate monitoring.
	 * The task will eventually be context switched out, at which point the context
	 * will be reclaimed (that includes releasing ownership of the PMU).
	 *
	 * So there might be a window of time where the number of per-task session is zero
	 * yet one PMU might have a owner and get at most one overflow interrupt for a zombie
	 * context. This is safe because if a per-task session comes in, it will push this one
	 * out and by the virtue on pfm_save_regs(), this one will disappear. If a system wide
	 * session is force on that CPU, given that we use task pinning, pfm_save_regs() will
	 * also push our zombie context out.
	 *
	 * Overall pretty hairy stuff....
	 */
	DPRINT(("ctx is zombie for [%d], converted to spurious\n", task ? task->pid: -1));
	pfm_clear_psr_up();
	ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;
	return;
}

static int
pfm_do_interrupt_handler(int irq, void *arg, struct pt_regs *regs)
{
	struct task_struct *task;
	pfm_context_t *ctx;
	unsigned long flags;
	u64 pmc0;
	int this_cpu = smp_processor_id();
	int retval = 0;

	pfm_stats[this_cpu].pfm_ovfl_intr_count++;

	/*
	 * srlz.d done before arriving here
	 */
	pmc0 = ia64_get_pmc(0);

	task = GET_PMU_OWNER();
	ctx  = GET_PMU_CTX();

	/*
	 * if we have some pending bits set
	 * assumes : if any PMC0.bit[63-1] is set, then PMC0.fr = 1
	 */
	if (PMC0_HAS_OVFL(pmc0) && task) {
		/*
		 * we assume that pmc0.fr is always set here
		 */

		/* sanity check */
		if (!ctx) goto report_spurious;

		if (ctx->ctx_fl_system == 0 && (task->thread.flags & IA64_THREAD_PM_VALID) == 0) {
			printk("perfmon: current [%d] owner = [%d] PMVALID=0 state=%d\n", current->pid, task->pid, ctx->ctx_state);
			goto report_spurious;
		}

		PROTECT_CTX_NOPRINT(ctx, flags);

		pfm_overflow_handler(task, ctx, pmc0, regs);

		UNPROTECT_CTX_NOPRINT(ctx, flags);

	} else {
		pfm_stats[this_cpu].pfm_spurious_ovfl_intr_count++;
		retval = -1;
	}
	/*
	 * keep it unfrozen at all times
	 */
	pfm_unfreeze_pmu();

	return retval;

report_spurious:
	printk(KERN_INFO "perfmon: spurious overflow interrupt on CPU%d: process %d has no PFM context\n",
		this_cpu, task->pid);
	pfm_unfreeze_pmu();
	return -1;
}

static pfm_irq_handler_t
pfm_interrupt_handler(int irq, void *arg, struct pt_regs *regs)
{
	unsigned long m;
	unsigned long min, max;
	int this_cpu;
	int ret;

	this_cpu = smp_processor_id();
	min      = pfm_stats[this_cpu].pfm_ovfl_intr_cycles_min;
	max      = pfm_stats[this_cpu].pfm_ovfl_intr_cycles_max;

	m = ia64_get_itc();

	ret = pfm_do_interrupt_handler(irq, arg, regs);

	m = ia64_get_itc() - m;

	/*
	 * don't measure spurious interrupts
	 */
	if (ret == 0) {
		if (m < min) pfm_stats[this_cpu].pfm_ovfl_intr_cycles_min = m;
		if (m > max) pfm_stats[this_cpu].pfm_ovfl_intr_cycles_max = m;
		pfm_stats[this_cpu].pfm_ovfl_intr_cycles += m;
	}
	PFM_IRQ_HANDLER_RET();
}


/* for debug only */
static int
pfm_proc_info(char *page)
{
	char *p = page;
	pfm_buffer_fmt_t *b;
	unsigned long psr;
	int i;

		p += sprintf(p, "model                     : %s\n", pmu_conf.pmu_name);
		p += sprintf(p, "fastctxsw                 : %s\n", pfm_sysctl.fastctxsw > 0 ? "Yes": "No");
		p += sprintf(p, "ovfl_mask                 : 0x%lx\n", pmu_conf.ovfl_val);

	for(i=0; i < NR_CPUS; i++) {
		if (cpu_is_online(i) == 0) continue;
		p += sprintf(p, "CPU%-2d overflow intrs      : %lu\n", i, pfm_stats[i].pfm_ovfl_intr_count);
		p += sprintf(p, "CPU%-2d overflow cycles     : %lu\n", i, pfm_stats[i].pfm_ovfl_intr_cycles);
		p += sprintf(p, "CPU%-2d overflow min        : %lu\n", i, pfm_stats[i].pfm_ovfl_intr_cycles_min);
		p += sprintf(p, "CPU%-2d overflow max        : %lu\n", i, pfm_stats[i].pfm_ovfl_intr_cycles_max);
		p += sprintf(p, "CPU%-2d smpl handler calls  : %lu\n", i, pfm_stats[i].pfm_smpl_handler_calls);
		p += sprintf(p, "CPU%-2d smpl handler cycles : %lu\n", i, pfm_stats[i].pfm_smpl_handler_cycles);
		p += sprintf(p, "CPU%-2d spurious intrs      : %lu\n", i, pfm_stats[i].pfm_spurious_ovfl_intr_count);
		p += sprintf(p, "CPU%-2d sysupdt count       : %lu\n", i, pfm_stats[i].pfm_sysupdt_count);
		p += sprintf(p, "CPU%-2d sysupdt cycles      : %lu\n", i, pfm_stats[i].pfm_sysupdt_cycles);
		p += sprintf(p, "CPU%-2d syst_wide           : %d\n" , i, pfm_get_cpu_data(pfm_syst_info, i) & PFM_CPUINFO_SYST_WIDE ? 1 : 0);
		p += sprintf(p, "CPU%-2d dcr_pp              : %d\n" , i, pfm_get_cpu_data(pfm_syst_info, i) & PFM_CPUINFO_DCR_PP ? 1 : 0);
		p += sprintf(p, "CPU%-2d exclude idle        : %d\n" , i, pfm_get_cpu_data(pfm_syst_info, i) & PFM_CPUINFO_EXCL_IDLE ? 1 : 0);
		p += sprintf(p, "CPU%-2d owner               : %d\n" , i, pfm_get_cpu_data(pmu_owner, i) ? pfm_get_cpu_data(pmu_owner, i)->pid: -1);
		p += sprintf(p, "CPU%-2d context             : %p\n" , i, pfm_get_cpu_data(pmu_ctx, i));
		p += sprintf(p, "CPU%-2d activations         : %lu\n", i, pfm_get_cpu_data(pmu_activation_number,i));
	}

	if (hweight64(PFM_CPU_ONLINE_MAP) == 1)
	{
		psr = pfm_get_psr();
		ia64_srlz_d();
		p += sprintf(p, "CPU%-2d psr                 : 0x%lx\n", smp_processor_id(), psr);
		p += sprintf(p, "CPU%-2d pmc0                : 0x%lx\n", smp_processor_id(), ia64_get_pmc(0));
		for(i=4; i < 8; i++) {
   			p += sprintf(p, "CPU%-2d pmc%u                : 0x%lx\n", smp_processor_id(), i, ia64_get_pmc(i));
   			p += sprintf(p, "CPU%-2d pmd%u                : 0x%lx\n", smp_processor_id(), i, ia64_get_pmd(i));
  		}
	}

	LOCK_PFS();
		p += sprintf(p, "proc_sessions             : %u\n"
			"sys_sessions              : %u\n"
			"sys_use_dbregs            : %u\n"
			"ptrace_use_dbregs         : %u\n",
			pfm_sessions.pfs_task_sessions,
			pfm_sessions.pfs_sys_sessions,
			pfm_sessions.pfs_sys_use_dbregs,
			pfm_sessions.pfs_ptrace_use_dbregs);
	UNLOCK_PFS();

	LOCK_BUF_FMT_LIST();

	for (b = pfm_buffer_fmt_list; b ; b = b->fmt_next) {
		p += sprintf(p, "format                    : %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x %s\n",
				b->fmt_uuid[0],
				b->fmt_uuid[1],
				b->fmt_uuid[2],
				b->fmt_uuid[3],
				b->fmt_uuid[4],
				b->fmt_uuid[5],
				b->fmt_uuid[6],
				b->fmt_uuid[7],
				b->fmt_uuid[8],
				b->fmt_uuid[9],
				b->fmt_uuid[10],
				b->fmt_uuid[11],
				b->fmt_uuid[12],
				b->fmt_uuid[13],
				b->fmt_uuid[14],
				b->fmt_uuid[15],
				b->fmt_name);
	}
	UNLOCK_BUF_FMT_LIST();

	return p - page;
}

/* /proc interface, for debug only */
static int
perfmon_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = pfm_proc_info(page);

	if (len <= off+count) *eof = 1;

	*start = page + off;
	len   -= off;

	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

/*
 * we come here as soon as local_cpu_data->pfm_syst_wide is set. this happens
 * during pfm_enable() hence before pfm_start(). We cannot assume monitoring
 * is active or inactive based on mode. We must rely on the value in
 * local_cpu_data->pfm_syst_info
 */
void
pfm_do_syst_wide_update_task(struct task_struct *task, unsigned long info, int is_ctxswin)
{
	struct pt_regs *regs;
	unsigned long dcr;
	unsigned long dcr_pp;

	dcr_pp = info & PFM_CPUINFO_DCR_PP ? 1 : 0;

	/*
	 * pid 0 is guaranteed to be the idle task. There is one such task with pid 0
	 * on every CPU, so we can rely on the pid to identify the idle task.
	 */
	if ((info & PFM_CPUINFO_EXCL_IDLE) == 0 || task->pid) {
		regs = ia64_task_regs(task);
		ia64_psr(regs)->pp = is_ctxswin ? dcr_pp : 0;
		return;
	}
	/*
	 * if monitoring has started
	 */
	if (dcr_pp) {
		dcr = ia64_get_dcr();
		/*
		 * context switching in?
		 */
		if (is_ctxswin) {
			/* mask monitoring for the idle task */
			ia64_set_dcr(dcr & ~IA64_DCR_PP);
			pfm_clear_psr_pp();
			ia64_srlz_i();
			return;
		}
		/*
		 * context switching out
		 * restore monitoring for next task
		 *
		 * Due to inlining this odd if-then-else construction generates
		 * better code.
		 */
		ia64_set_dcr(dcr |IA64_DCR_PP);
		pfm_set_psr_pp();
		ia64_srlz_i();
	}
}

void
pfm_syst_wide_update_task(struct task_struct *task, unsigned long info, int is_ctxswin)
{
	unsigned long start, end;
	pfm_stats[smp_processor_id()].pfm_sysupdt_count++;
	start = ia64_get_itc();
	pfm_do_syst_wide_update_task(task, info, is_ctxswin);
	end = ia64_get_itc();
	pfm_stats[smp_processor_id()].pfm_sysupdt_cycles += end-start;
}

#ifdef CONFIG_SMP
void
pfm_save_regs(struct task_struct *task)
{
	pfm_context_t *ctx;
	struct thread_struct *t;
	unsigned long flags;
	u64 psr;


	ctx = PFM_GET_CTX(task);
	if (ctx == NULL) goto save_error;
	t = &task->thread;

	/*
 	 * we always come here with interrupts ALREADY disabled by
 	 * the scheduler. So we simply need to protect against concurrent
	 * access, not CPU concurrency.
	 */
	flags = pfm_protect_ctx_ctxsw(ctx);

	if (CTX_IS_ZOMBIE(ctx)) {
		struct pt_regs *regs = ia64_task_regs(task);

		pfm_clear_psr_up();

		DPRINT(("ctx zombie, forcing cleanup for [%d]\n", task->pid));

		pfm_force_cleanup(ctx, regs);

		BUG_ON(ctx->ctx_smpl_hdr);

		pfm_unprotect_ctx_ctxsw(ctx, flags);

		pfm_context_free(ctx);
		return;
	}

	/*
	 * sanity check
	 */
	if (ctx->ctx_last_activation != GET_ACTIVATION()) {
		printk("ctx_activation=%lu activation=%lu state=%d: no save\n",
				ctx->ctx_last_activation,
				GET_ACTIVATION(), ctx->ctx_state);

		pfm_unprotect_ctx_ctxsw(ctx, flags);

		return;
	}

	/*
	 * save current PSR: needed because we modify it
	 */
	ia64_srlz_d();
	psr = pfm_get_psr();

	BUG_ON(psr & (IA64_PSR_I));

	/*
	 * stop monitoring:
	 * This is the last instruction which may generate an overflow
	 *
	 * We do not need to set psr.sp because, it is irrelevant in kernel.
	 * It will be restored from ipsr when going back to user level
	 */
	pfm_clear_psr_up();

	/*
	 * keep a copy of psr.up (for reload)
	 */
	ctx->ctx_saved_psr_up = psr & IA64_PSR_UP;

	{ u64 foo = pfm_get_psr();
	  BUG_ON(foo & ((IA64_PSR_UP|IA64_PSR_PP)));
	}

	/*
	 * release ownership of this PMU.
	 * PM interrupts are masked, so nothing
	 * can happen.
	 */
	SET_PMU_OWNER(NULL, NULL);

	/*
	 * we systematically save the PMD as we have no
	 * guarantee we will be schedule at that same
	 * CPU again.
	 */
	pfm_save_pmds(t->pmds, ctx->ctx_used_pmds[0]);

	/*
	 * save pmc0 ia64_srlz_d() done in pfm_save_pmds()
	 * we will need it on the restore path to check
	 * for pending overflow.
	 */
	t->pmcs[0] = ia64_get_pmc(0);

	/*
	 * unfreeze PMU if had pending overflows
	 */
	if (t->pmcs[0] & ~1UL) pfm_unfreeze_pmu();

	/*
	 * finally, unmask interrupts and allow context
	 * access.
	 * Any pended overflow interrupt may be delivered
	 * here and will be treated as spurious because we
	 * have have no PMU owner anymore.
	 */
	pfm_unprotect_ctx_ctxsw(ctx, flags);

	return;

save_error:
	printk(KERN_ERR "perfmon: pfm_save_regs CPU%d [%d] NULL context PM_VALID=%ld\n",
		smp_processor_id(), task->pid,
		task->thread.flags & IA64_THREAD_PM_VALID);
}

#else /* !CONFIG_SMP */

/*
 * in 2.5, interrupts are masked when we come here
 */
void
pfm_save_regs(struct task_struct *task)
{
	pfm_context_t *ctx;
	u64 psr;

	ctx = PFM_GET_CTX(task);
	if (ctx == NULL) goto save_error;

	/*
	 * save current PSR: needed because we modify it
	 */
	psr = pfm_get_psr();

	/*
	 * stop monitoring:
	 * This is the last instruction which may generate an overflow
	 *
	 * We do not need to set psr.sp because, it is irrelevant in kernel.
	 * It will be restored from ipsr when going back to user level
	 */
	pfm_clear_psr_up();

	/*
	 * keep a copy of psr.up (for reload)
	 */
	ctx->ctx_saved_psr_up = psr & IA64_PSR_UP;

#if 1
	{ u64 foo = pfm_get_psr();
	  BUG_ON(foo & (IA64_PSR_I));
	  BUG_ON(foo & ((IA64_PSR_UP|IA64_PSR_PP)));
	}
#endif
	return;
save_error:
	printk(KERN_ERR "perfmon: pfm_save_regs CPU%d [%d] NULL context PM_VALID=%ld\n",
		smp_processor_id(), task->pid,
		task->thread.flags & IA64_THREAD_PM_VALID);
}

static void
pfm_lazy_save_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	struct thread_struct *t;
	unsigned long flags;

#if 1
	{ u64 foo  = pfm_get_psr();
	  BUG_ON(foo & IA64_PSR_UP);
	}
#endif

	ctx = PFM_GET_CTX(task);
	t   = &task->thread;

	DPRINT(("on [%d] used_pmds=0x%lx\n", task->pid, ctx->ctx_used_pmds[0]));

	/*
	 * we need to mask PMU overflow here to
	 * make sure that we maintain pmc0 until
	 * we save it. overflow interrupts are
	 * treated as spurious if there is no
	 * owner.
	 *
	 * XXX: I don't think this is necessary
	 */
	PROTECT_CTX(ctx,flags);

	/*
	 * release ownership of this PMU.
	 * must be done before we save the registers.
	 *
	 * after this call any PMU interrupt is treated
	 * as spurious.
	 */
	SET_PMU_OWNER(NULL, NULL);

	/*
	 * save all the pmds we use
	 */
	pfm_save_pmds(t->pmds, ctx->ctx_used_pmds[0]);

	/*
	 * save pmc0 ia64_srlz_d() done in pfm_save_pmds()
	 * it is needed to check for pended overflow
	 * on the restore path
	 */
	t->pmcs[0] = ia64_get_pmc(0);

	/*
	 * unfreeze PMU if had pending overflows
	 */
	if (t->pmcs[0] & ~1UL) pfm_unfreeze_pmu();

	/*
	 * now get can unmask PMU interrupts, they will
	 * be treated as purely spurious and we will not
	 * lose any information
	 */
	UNPROTECT_CTX(ctx,flags);
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_SMP
void
pfm_load_regs (struct task_struct *task)
{
	pfm_context_t *ctx;
	struct thread_struct *t;
	unsigned long pmc_mask = 0UL, pmd_mask = 0UL;
	unsigned long flags;
	u64 psr, psr_up;

	ctx = PFM_GET_CTX(task);
	if (unlikely(ctx == NULL)) {
		printk(KERN_ERR "perfmon: pfm_load_regs() null context\n");
		return;
	}

	BUG_ON(GET_PMU_OWNER());

	t     = &task->thread;
	psr   = pfm_get_psr();

#if 1
	BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	BUG_ON(psr & IA64_PSR_I);
#endif

	/*
	 * possible on unload
	 */
	if (unlikely((t->flags & IA64_THREAD_PM_VALID) == 0)) {
		printk("[%d] PM_VALID=0, nothing to do\n", task->pid);
		return;
	}

	/*
 	 * we always come here with interrupts ALREADY disabled by
 	 * the scheduler. So we simply need to protect against concurrent
	 * access, not CPU concurrency.
	 */
	flags = pfm_protect_ctx_ctxsw(ctx);

	if (unlikely(CTX_IS_ZOMBIE(ctx))) {
		struct pt_regs *regs = ia64_task_regs(task);

		BUG_ON(ctx->ctx_smpl_hdr);

		DPRINT(("ctx zombie, forcing cleanup for [%d]\n", task->pid));

		pfm_force_cleanup(ctx, regs);

		pfm_unprotect_ctx_ctxsw(ctx, flags);

		/*
		 * this one (kmalloc'ed) is fine with interrupts disabled
		 */
		pfm_context_free(ctx);

		return;
	}

	/*
	 * we restore ALL the debug registers to avoid picking up
	 * stale state.
	 */
	if (ctx->ctx_fl_using_dbreg) {
		pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf.num_ibrs);
		pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf.num_dbrs);
	}
	/*
	 * retrieve saved psr.up
	 */
	psr_up = ctx->ctx_saved_psr_up;

	/*
	 * if we were the last user of the PMU on that CPU,
	 * then nothing to do except restore psr
	 */
	if (GET_LAST_CPU(ctx) == smp_processor_id() && ctx->ctx_last_activation == GET_ACTIVATION()) {

		/*
		 * retrieve partial reload masks (due to user modifications)
		 */
		pmc_mask = ctx->ctx_reload_pmcs[0];
		pmd_mask = ctx->ctx_reload_pmds[0];

		if (pmc_mask || pmd_mask) DPRINT(("partial reload [%d] pmd_mask=0x%lx pmc_mask=0x%lx\n", task->pid, pmd_mask, pmc_mask));
	} else {
		/*
	 	 * To avoid leaking information to the user level when psr.sp=0,
	 	 * we must reload ALL implemented pmds (even the ones we don't use).
	 	 * In the kernel we only allow PFM_READ_PMDS on registers which
	 	 * we initialized or requested (sampling) so there is no risk there.
	 	 */
		pmd_mask = pfm_sysctl.fastctxsw ?  ctx->ctx_used_pmds[0] : ctx->ctx_all_pmds[0];

		/*
	 	 * ALL accessible PMCs are systematically reloaded, unused registers
	 	 * get their default (from pfm_reset_pmu_state()) values to avoid picking
	 	 * up stale configuration.
	 	 *
	 	 * PMC0 is never in the mask. It is always restored separately.
	 	 */
		pmc_mask = ctx->ctx_all_pmcs[0];

		DPRINT(("full reload for [%d] activation=%lu last_activation=%lu last_cpu=%d pmd_mask=0x%lx pmc_mask=0x%lx\n",
			task->pid,
			GET_ACTIVATION(), ctx->ctx_last_activation,
			GET_LAST_CPU(ctx), pmd_mask, pmc_mask));

	}
	/*
	 * when context is MASKED, we will restore PMC with plm=0
	 * and PMD with stale information, but that's ok, nothing
	 * will be captured.
	 *
	 * XXX: optimize here
	 */
	if (pmd_mask) pfm_restore_pmds(t->pmds, pmd_mask);
	if (pmc_mask) pfm_restore_pmcs(t->pmcs, pmc_mask);

	/*
	 * check for pending overflow at the time the state
	 * was saved.
	 */
	if (unlikely(PMC0_HAS_OVFL(t->pmcs[0]))) {
		struct pt_regs *regs = ia64_task_regs(task);
		pfm_overflow_handler(task, ctx, t->pmcs[0], regs);
	}

	/*
	 * we clear PMC0, to ensure that any in flight interrupt
	 * will not be attributed to the new context we are installing
	 * because the actual overflow has been processed above already.
	 * No real effect until we unmask interrupts at the end of the
	 * function.
	 */
	pfm_unfreeze_pmu();

	/*
	 * we just did a reload, so we reset the partial reload fields
	 */
	ctx->ctx_reload_pmcs[0] = 0UL;
	ctx->ctx_reload_pmds[0] = 0UL;

	SET_LAST_CPU(ctx, smp_processor_id());

	/*
	 * dump activation value for this PMU
	 */
	INC_ACTIVATION();
	/*
	 * record current activation for this context
	 */
	SET_ACTIVATION(ctx);

	/*
	 * establish new ownership. Interrupts
	 * are still masked at this point.
	 */
	SET_PMU_OWNER(task, ctx);

	/*
	 * restore the psr.up bit 
	 */
	if (likely(psr_up)) pfm_set_psr_up();

	/*
	 * allow concurrent access to context
	 */
	pfm_unprotect_ctx_ctxsw(ctx, flags);
}
#else /*  !CONFIG_SMP */
/*
 * reload PMU state for UP kernels
 * in 2.5 we come here with interrupts disabled
 */
void
pfm_load_regs (struct task_struct *task)
{
	struct thread_struct *t;
	pfm_context_t *ctx;
	struct task_struct *owner;
	unsigned long pmd_mask, pmc_mask;
	u64 psr, psr_up;

	owner = GET_PMU_OWNER();
	ctx   = PFM_GET_CTX(task);
	t     = &task->thread;
	psr   = pfm_get_psr();

#if 1
	BUG_ON(psr & (IA64_PSR_UP|IA64_PSR_PP));
	BUG_ON(psr & IA64_PSR_I);
#endif

	/*
	 * we restore ALL the debug registers to avoid picking up
	 * stale state.
	 *
	 * This must be done even when the task is still the owner
	 * as the registers may have been modified via ptrace()
	 * (not perfmon) by the previous task.
	 */
	if (ctx->ctx_fl_using_dbreg) {
		pfm_restore_ibrs(ctx->ctx_ibrs, pmu_conf.num_ibrs);
		pfm_restore_dbrs(ctx->ctx_dbrs, pmu_conf.num_dbrs);
	}

	/*
	 * retrieved saved psr.up
	 */
	psr_up = ctx->ctx_saved_psr_up;

	/*
	 * short path, our state is still there, just
	 * need to restore psr and we go
	 *
	 * we do not touch either PMC nor PMD. the psr is not touched
	 * by the overflow_handler. So we are safe w.r.t. to interrupt
	 * concurrency even without interrupt masking.
	 */
	if (likely(owner == task)) {
		if (likely(psr_up)) pfm_set_psr_up();
		return;
	}

	DPRINT(("reload for [%d] owner=%d\n", task->pid, owner ? owner->pid : -1));

	/*
	 * someone else is still using the PMU, first push it out and
	 * then we'll be able to install our stuff !
	 *
	 * Upon return, there will be no owner for the current PMU
	 */
	if (owner) pfm_lazy_save_regs(owner);

	/*
	 * To avoid leaking information to the user level when psr.sp=0,
	 * we must reload ALL implemented pmds (even the ones we don't use).
	 * In the kernel we only allow PFM_READ_PMDS on registers which
	 * we initialized or requested (sampling) so there is no risk there.
	 */
	pmd_mask = pfm_sysctl.fastctxsw ?  ctx->ctx_used_pmds[0] : ctx->ctx_all_pmds[0];

	/*
	 * ALL accessible PMCs are systematically reloaded, unused registers
	 * get their default (from pfm_reset_pmu_state()) values to avoid picking
	 * up stale configuration.
	 *
	 * PMC0 is never in the mask. It is always restored separately
	 */
	pmc_mask = ctx->ctx_all_pmcs[0];

	pfm_restore_pmds(t->pmds, pmd_mask);
	pfm_restore_pmcs(t->pmcs, pmc_mask);

	/*
	 * Check for pending overflow when state was last saved.
	 * invoked handler is overflow status bits set.
	 *
	 * Any PMU overflow in flight at this point, will still
	 * be treated as spurious because we have no declared
	 * owner. Note that the first level interrupt handler
	 * DOES NOT TOUCH any PMC except PMC0 for which we have
	 * a copy already.
	 */
	if (unlikely(PMC0_HAS_OVFL(t->pmcs[0]))) {
		struct pt_regs *regs = ia64_task_regs(task);
		pfm_overflow_handler(task, ctx, t->pmcs[0], regs);
	}

	/*
	 * we clear PMC0, to ensure that any in flight interrupt
	 * will not be attributed to the new context we are installing
	 * because the actual overflow has been processed above already.
	 *
	 * This is an atomic operation.
	 */
	pfm_unfreeze_pmu();

	/*
	 * establish new ownership. If there was an in-flight
	 * overflow interrupt, it will be treated as spurious
	 * before and after the call, because no overflow
	 * status bit can possibly be set. No new overflow
	 * can be generated because, at this point, psr.up
	 * is still cleared.
	 */
	SET_PMU_OWNER(task, ctx);

	/*
	 * restore the psr. This is the point at which
	 * new overflow interrupts can be generated again.
	 */
	if (likely(psr_up)) pfm_set_psr_up();
}
#endif /* CONFIG_SMP */

/*
 * this function assumes monitoring is stopped
 */
static void
pfm_flush_pmds(struct task_struct *task, pfm_context_t *ctx)
{
	u64 pmc0;
	unsigned long mask2, val, pmd_val;
	int i, can_access_pmu = 0;
	int is_self;

	/*
	 * is the caller the task being monitored (or which initiated the
	 * session for system wide measurements)
	 */
	is_self = ctx->ctx_task == task ? 1 : 0;

#ifdef CONFIG_SMP
	if (task == current) {
#else
	/*
	 * in UP, the state can still be in the registers
	 */
	if (task == current || GET_PMU_OWNER() == task) {
#endif
		can_access_pmu = 1;
		/*
		 * Mark the PMU as not owned
		 * This will cause the interrupt handler to do nothing in case an overflow
		 * interrupt was in-flight
		 * This also guarantees that pmc0 will contain the final state
		 * It virtually gives us full control on overflow processing from that point
		 * on.
		 */
		SET_PMU_OWNER(NULL, NULL);

		/*
		 * read current overflow status:
		 *
		 * we are guaranteed to read the final stable state
		 */
		ia64_srlz_d();
		pmc0 = ia64_get_pmc(0); /* slow */

		/*
		 * reset freeze bit, overflow status information destroyed
		 */
		pfm_unfreeze_pmu();
	} else {
		pmc0 = task->thread.pmcs[0];
		/*
		 * clear whatever overflow status bits there were
		 */
		task->thread.pmcs[0] &= ~0x1;
	}

	/*
	 * we save all the used pmds
	 * we take care of overflows for counting PMDs
	 *
	 * XXX: sampling situation is not taken into account here
	 */
	mask2 = ctx->ctx_used_pmds[0];
	for (i = 0; mask2; i++, mask2>>=1) {

		/* skip non used pmds */
		if ((mask2 & 0x1) == 0) continue;

		/*
		 * can access PMU always true in system wide mode
		 */
		val = pmd_val = can_access_pmu ? ia64_get_pmd(i) : task->thread.pmds[i];

		if (PMD_IS_COUNTING(i)) {
			DPRINT(("[%d] pmd[%d] ctx_pmd=0x%lx hw_pmd=0x%lx\n",
				task->pid,
				i,
				ctx->ctx_pmds[i].val,
				val & pmu_conf.ovfl_val));

			/*
			 * we rebuild the full 64 bit value of the counter
			 */
			val = ctx->ctx_pmds[i].val + (val & pmu_conf.ovfl_val);

			/*
			 * now everything is in ctx_pmds[] and we need
			 * to clear the saved context from save_regs() such that
			 * pfm_read_pmds() gets the correct value
			 */
			pmd_val = 0UL;

			/*
			 * take care of overflow inline
			 */
			if (pmc0 & (1UL << i)) {
				val += 1 + pmu_conf.ovfl_val;
				DPRINT(("[%d] pmd[%d] overflowed\n", task->pid, i));
			}
		}

		DPRINT(("[%d] is_self=%d ctx_pmd[%d]=0x%lx  pmd_val=0x%lx\n", task->pid, is_self, i, val, pmd_val));

		if (is_self) task->thread.pmds[i] = pmd_val;
		ctx->ctx_pmds[i].val = val;
	}
}

static struct irqaction perfmon_irqaction = {
	.handler = pfm_interrupt_handler,
	.flags   = SA_INTERRUPT,
	.name    = "perfmon"
};

/*
 * perfmon initialization routine, called from the initcall() table
 */
static int init_pfm_fs(void);

int __init
pfm_init(void)
{
	unsigned int n, n_counters, i;

	printk("perfmon: version %u.%u IRQ %u\n",
		PFM_VERSION_MAJ,
		PFM_VERSION_MIN,
		IA64_PERFMON_VECTOR);

	/*
	 * PMU type sanity check
	 * XXX: maybe better to implement autodetection (but then we have a larger kernel)
	 */
	if (local_cpu_data->family != pmu_conf.pmu_family) {
		printk(KERN_INFO "perfmon: disabled, kernel only supports %s PMU family\n", pmu_conf.pmu_name);
		return -ENODEV;
	}

	/*
	 * compute the number of implemented PMD/PMC from the
	 * description tables
	 */
	n = 0;
	for (i=0; PMC_IS_LAST(i) == 0;  i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		pmu_conf.impl_pmcs[i>>6] |= 1UL << (i&63);
		n++;
	}
	pmu_conf.num_pmcs = n;

	n = 0; n_counters = 0;
	for (i=0; PMD_IS_LAST(i) == 0;  i++) {
		if (PMD_IS_IMPL(i) == 0) continue;
		pmu_conf.impl_pmds[i>>6] |= 1UL << (i&63);
		n++;
		if (PMD_IS_COUNTING(i)) n_counters++;
	}
	pmu_conf.num_pmds      = n;
	pmu_conf.num_counters  = n_counters;

	/*
	 * sanity checks on the number of debug registers
	 */
	if (pmu_conf.use_rr_dbregs) {
		if (pmu_conf.num_ibrs > IA64_NUM_DBG_REGS) {
			printk(KERN_INFO "perfmon: unsupported number of code debug registers (%u)\n", pmu_conf.num_ibrs);
			return -1;
		}
		if (pmu_conf.num_dbrs > IA64_NUM_DBG_REGS) {
			printk(KERN_INFO "perfmon: unsupported number of data debug registers (%u)\n", pmu_conf.num_ibrs);
			return -1;
		}
	}

	printk("perfmon: %s PMU detected, %u PMCs, %u PMDs, %u counters (%lu bits)\n",
	       pmu_conf.pmu_name,
	       pmu_conf.num_pmcs,
	       pmu_conf.num_pmds,
	       pmu_conf.num_counters,
	       ffz(pmu_conf.ovfl_val));

	/* sanity check */
	if (pmu_conf.num_pmds >= IA64_NUM_PMD_REGS || pmu_conf.num_pmcs >= IA64_NUM_PMC_REGS) {
		printk(KERN_ERR "perfmon: not enough pmc/pmd, perfmon disabled\n");
		return -1;
	}

	/*
	 * create /proc/perfmon (mostly for debugging purposes)
	 */
	perfmon_dir = create_proc_read_entry ("perfmon", 0, 0, perfmon_read_entry, NULL);
	if (perfmon_dir == NULL) {
		printk(KERN_ERR "perfmon: cannot create /proc entry, perfmon disabled\n");
		return -1;
	}

	/*
	 * create /proc/sys/kernel/perfmon (for debugging purposes)
	 */
	pfm_sysctl_header = register_sysctl_table(pfm_sysctl_root, 0);

	/*
	 * initialize all our spinlocks
	 */
	spin_lock_init(&pfm_sessions.pfs_lock);
	spin_lock_init(&pfm_smpl_fmt_lock);

	init_pfm_fs();

	for(i=0; i < NR_CPUS; i++) pfm_stats[i].pfm_ovfl_intr_cycles_min = ~0UL;

	/* we are all set */
	pmu_conf.enabled = 1;

	return 0;
}

__initcall(pfm_init);

void
pfm_init_percpu (void)
{
	int i;

	/*
	 * make sure no measurement is active
	 * (may inherit programmed PMCs from EFI).
	 */
	pfm_clear_psr_pp();
	pfm_clear_psr_up();


	if (smp_processor_id() == 0)
		register_percpu_irq(IA64_PERFMON_VECTOR, &perfmon_irqaction);

	ia64_set_pmv(IA64_PERFMON_VECTOR);
	ia64_srlz_d();

	/*
	 * we first initialize the PMU to a stable state.
	 * the values may have been changed from their power-up
	 * values by software executed before the kernel took over.
	 *
	 * At this point, pmu_conf has not yet been initialized
	 *
	 * On McKinley, this code is ineffective until PMC4 is initialized
	 * but that's all right because we take care of pmc0 later.
	 *
	 * XXX: potential problems with pmc1.
	 */
	for (i=1; PMC_IS_LAST(i) == 0;  i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		ia64_set_pmc(i, PMC_DFL_VAL(i));
	}

	for (i=0; PMD_IS_LAST(i) == 0; i++) {
		if (PMD_IS_IMPL(i) == 0) continue;
		ia64_set_pmd(i, 0UL);
	}

	/*
	 * we run with the PMU not frozen at all times
	 */
	pfm_unfreeze_pmu();
}

/*
 * used for debug purposes only
 */
void
dump_pmu_state(void)
{
	struct task_struct *task;
	struct thread_struct *t;
	pfm_context_t *ctx;
	unsigned long psr;
	int i;

	printk("current [%d] %s\n", current->pid, current->comm);

	task = GET_PMU_OWNER();
	ctx  = GET_PMU_CTX();

	printk("owner [%d] ctx=%p\n", task ? task->pid : -1, ctx);

	psr = pfm_get_psr();

	printk("psr.pp=%ld psr.up=%ld\n", (psr >> IA64_PSR_PP_BIT) &0x1UL, (psr >> IA64_PSR_PP_BIT)&0x1UL);

	t = &current->thread;

	for (i=1; PMC_IS_LAST(i) == 0; i++) {
		if (PMC_IS_IMPL(i) == 0) continue;
		printk("pmc[%d]=0x%lx tpmc=0x%lx\n", i, ia64_get_pmc(i), t->pmcs[i]);
	}

	for (i=1; PMD_IS_LAST(i) == 0; i++) {
		if (PMD_IS_IMPL(i) == 0) continue;
		printk("pmd[%d]=0x%lx tpmd=0x%lx\n", i, ia64_get_pmd(i), t->pmds[i]);
	}
	if (ctx) {
		printk("ctx_state=%d vaddr=%p addr=%p fd=%d ctx_task=[%d] saved_psr_up=0x%lx\n",
				ctx->ctx_state,
				ctx->ctx_smpl_vaddr,
				ctx->ctx_smpl_hdr,
				ctx->ctx_msgq_head,
				ctx->ctx_msgq_tail,
				ctx->ctx_saved_psr_up);
	}
}

/*
 * called from process.c:copy_thread(). task is new child.
 */
void
pfm_inherit(struct task_struct *task, struct pt_regs *regs)
{
	struct thread_struct *thread;

	DPRINT(("perfmon: pfm_inherit clearing state for [%d] current [%d]\n", task->pid, current->pid));

	thread = &task->thread;

	/*
	 * cut links inherited from parent (current)
	 */
	thread->pfm_context = NULL;

	PFM_SET_WORK_PENDING(task, 0);

	/*
	 * restore default psr settings
	 */
	ia64_psr(regs)->pp = ia64_psr(regs)->up = 0;
	ia64_psr(regs)->sp = 1;
}
#else  /* !CONFIG_PERFMON */
asmlinkage long
sys_perfmonctl (int fd, int cmd, void *arg, int count, long arg5, long arg6, long arg7,
		long arg8, long stack)
{
	return -ENOSYS;
}
#endif /* CONFIG_PERFMON */
