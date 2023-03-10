#ifndef _M68K_IRQ_H_
#define _M68K_IRQ_H_

#include <linux/config.h>
#include <asm/ptrace.h>

#ifdef CONFIG_COLDFIRE
/*
 * On the ColdFire we keep track of all vectors. That way drivers
 * can register whatever vector number they wish, and we can deal
 * with it.
 */
#define	SYS_IRQS	256
#define	NR_IRQS		SYS_IRQS

#else

/*
 * # of m68k interrupts
 */
#define SYS_IRQS 8
#define NR_IRQS (24+SYS_IRQS)

#endif /* CONFIG_COLDFIRE */

/*
 * Interrupt source definitions
 * General interrupt sources are the level 1-7.
 * Adding an interrupt service routine for one of these sources
 * results in the addition of that routine to a chain of routines.
 * Each one is called in succession.  Each individual interrupt
 * service routine should determine if the device associated with
 * that routine requires service.
 */

#define IRQ1		(1)	/* level 1 interrupt */
#define IRQ2		(2)	/* level 2 interrupt */
#define IRQ3		(3)	/* level 3 interrupt */
#define IRQ4		(4)	/* level 4 interrupt */
#define IRQ5		(5)	/* level 5 interrupt */
#define IRQ6		(6)	/* level 6 interrupt */
#define IRQ7		(7)	/* level 7 interrupt (non-maskable) */

/*
 * Machine specific interrupt sources.
 *
 * Adding an interrupt service routine for a source with this bit
 * set indicates a special machine specific interrupt source.
 * The machine specific files define these sources.
 *
 * The IRQ_MACHSPEC bit is now gone - the only thing it did was to
 * introduce unnecessary overhead.
 *
 * All interrupt handling is actually machine specific so it is better
 * to use function pointers, as used by the Sparc port, and select the
 * interrupt handling functions when initializing the kernel. This way
 * we save some unnecessary overhead at run-time. 
 *                                                      01/11/97 - Jes
 */

extern void (*mach_enable_irq)(unsigned int);
extern void (*mach_disable_irq)(unsigned int);

extern int sys_request_irq(unsigned int, 
	void (*)(int, void *, struct pt_regs *), 
	unsigned long, const char *, void *);
extern void sys_free_irq(unsigned int, void *);

/*
 * various flags for request_irq() - the Amiga now uses the standard
 * mechanism like all other architectures - SA_INTERRUPT and SA_SHIRQ
 * are your friends.
 */
#define IRQ_FLG_LOCK	(0x0001)	/* handler is not replaceable	*/
#define IRQ_FLG_REPLACE	(0x0002)	/* replace existing handler	*/
#define IRQ_FLG_FAST	(0x0004)
#define IRQ_FLG_SLOW	(0x0008)
#define IRQ_FLG_STD	(0x8000)	/* internally used		*/

#ifdef CONFIG_M68360

#define CPM_INTERRUPT    IRQ4

/* see MC68360 User's Manual, p. 7-377  */
#define CPM_VECTOR_BASE  0x04           /* 3 MSbits of CPM vector */

#endif /* CONFIG_M68360 */

/*
 * This structure is used to chain together the ISRs for a particular
 * interrupt source (if it supports chaining).
 */
typedef struct irq_node {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
	struct irq_node *next;
} irq_node_t;

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
	const char	*devname;
} irq_handler_t;

/* count of spurious interrupts */
extern volatile unsigned int num_spurious;

/*
 * This function returns a new irq_node_t
 */
extern irq_node_t *new_irq_node(void);

/*
 * Some drivers want these entry points
 */
#define enable_irq(x)	(mach_enable_irq  ? (*mach_enable_irq)(x)  : 0)
#define disable_irq(x)	(mach_disable_irq ? (*mach_disable_irq)(x) : 0)

#define enable_irq_nosync(x)	enable_irq(x)
#define disable_irq_nosync(x)	disable_irq(x)

#endif /* _M68K_IRQ_H_ */
