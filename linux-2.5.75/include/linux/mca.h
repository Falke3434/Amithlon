/*
 * Header for Microchannel Architecture Bus
 * Written by Martin Kolinek, February 1996
 */

#ifndef _LINUX_MCA_H
#define _LINUX_MCA_H

/* FIXME: This shouldn't happen, but we need everything that previously
 * included mca.h to compile.  Take it out later when the MCA #includes
 * are sorted out */
#include <linux/device.h>

/* get the platform specific defines */
#include <asm/mca.h>

/* The detection of MCA bus is done in the real mode (using BIOS).
 * The information is exported to the protected code, where this
 * variable is set to one in case MCA bus was detected.
 */
#ifndef MCA_bus__is_a_macro
extern int  MCA_bus;
#endif

/* This sets up an information callback for /proc/mca/slot?.  The
 * function is called with the buffer, slot, and device pointer (or
 * some equally informative context information, or nothing, if you
 * prefer), and is expected to put useful information into the
 * buffer.  The adapter name, id, and POS registers get printed
 * before this is called though, so don't do it again.
 *
 * This should be called with a NULL procfn when a module
 * unregisters, thus preventing kernel crashes and other such
 * nastiness.
 */
typedef int (*MCA_ProcFn)(char* buf, int slot, void* dev);

/* Should only be called by the NMI interrupt handler, this will do some
 * fancy stuff to figure out what might have generated a NMI.
 */
extern void mca_handle_nmi(void);

enum MCA_AdapterStatus {
	MCA_ADAPTER_NORMAL = 0,
	MCA_ADAPTER_NONE = 1,
	MCA_ADAPTER_DISABLED = 2,
	MCA_ADAPTER_ERROR = 3
};

struct mca_device {
	u64			dma_mask;
	int			pos_id;
	int			slot;

	/* index into id_table, set by the bus match routine */
	int			index;

	/* is there a driver installed? 0 - No, 1 - Yes */
	int			driver_loaded;
	/* POS registers */
	unsigned char		pos[8];
	/* if a pseudo adapter of the motherboard, this is the motherboard
	 * register value to use for setup cycles */
	short			pos_register;
	
	enum MCA_AdapterStatus	status;
#ifdef CONFIG_MCA_PROC_FS
	/* name of the proc/mca file */
	char			procname[8];
	/* /proc info callback */
	MCA_ProcFn		procfn;
	/* device/context info for proc callback */
	void			*proc_dev;
#endif
	struct device		dev;
};
#define to_mca_device(mdev) container_of(mdev, struct mca_device, dev)

struct mca_bus_accessor_functions {
	unsigned char	(*mca_read_pos)(struct mca_device *, int reg);
	void		(*mca_write_pos)(struct mca_device *, int reg,
					 unsigned char byte);
	int		(*mca_transform_irq)(struct mca_device *, int irq);
	int		(*mca_transform_ioport)(struct mca_device *,
						  int region);
	void *		(*mca_transform_memory)(struct mca_device *,
						void *memory);
};

struct mca_bus {
	u64			default_dma_mask;
	int			number;
	struct mca_bus_accessor_functions f;
	struct device		dev;
};
#define to_mca_bus(mdev) container_of(mdev, struct mca_bus, dev)

struct mca_driver {
	const short		*id_table;
	void			*driver_data;
	struct device_driver	driver;
};
#define to_mca_driver(mdriver) container_of(mdriver, struct mca_driver, driver)

/* Ongoing supported API functions */
extern struct mca_device *mca_find_device_by_slot(int slot);
extern int mca_system_init(void);
extern struct mca_bus *mca_attach_bus(int);

extern unsigned char mca_device_read_stored_pos(struct mca_device *mca_dev,
						int reg);
extern unsigned char mca_device_read_pos(struct mca_device *mca_dev, int reg);
extern void mca_device_write_pos(struct mca_device *mca_dev, int reg,
				 unsigned char byte);
extern int mca_device_transform_irq(struct mca_device *mca_dev, int irq);
extern int mca_device_transform_ioport(struct mca_device *mca_dev, int port);
extern void *mca_device_transform_memory(struct mca_device *mca_dev,
					 void *mem);
extern int mca_device_claimed(struct mca_device *mca_dev);
extern void mca_device_set_claim(struct mca_device *mca_dev, int val);
extern enum MCA_AdapterStatus mca_device_status(struct mca_device *mca_dev);

extern struct bus_type mca_bus_type;

extern int mca_register_driver(struct mca_driver *drv);
extern void mca_unregister_driver(struct mca_driver *drv);

/* WARNING: only called by the boot time device setup */
extern int mca_register_device(int bus, struct mca_device *mca_dev);

#ifdef CONFIG_MCA_LEGACY
#include <linux/mca-legacy.h>
#endif

#ifdef CONFIG_MCA_PROC_FS
extern void mca_do_proc_init(void);
extern void mca_set_adapter_procfn(int slot, MCA_ProcFn, void* dev);
#else
static inline void mca_do_proc_init(void)
{
}

static inline void mca_set_adapter_procfn(int slot, MCA_ProcFn *fn, void* dev)
{
}
#endif

#endif /* _LINUX_MCA_H */
