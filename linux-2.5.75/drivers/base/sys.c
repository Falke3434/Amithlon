/*
 * sys.c - pseudo-bus for system 'devices' (cpus, PICs, timers, etc)
 *
 * Copyright (c) 2002-3 Patrick Mochel
 *               2002-3 Open Source Development Lab
 *
 * This file is released under the GPLv2
 * 
 * This exports a 'system' bus type. 
 * By default, a 'sys' bus gets added to the root of the system. There will
 * always be core system devices. Devices can use sys_device_register() to
 * add themselves as children of the system bus.
 */

#undef DEBUG

#include <linux/sysdev.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>


extern struct subsystem devices_subsys;

#define to_sysdev(k) container_of(k,struct sys_device,kobj)
#define to_sysdev_attr(a) container_of(a,struct sysdev_attribute,attr)


static ssize_t 
sysdev_show(struct kobject * kobj, struct attribute * attr, char * buffer)
{
	struct sys_device * sysdev = to_sysdev(kobj);
	struct sysdev_attribute * sysdev_attr = to_sysdev_attr(attr);

	if (sysdev_attr->show)
		return sysdev_attr->show(sysdev,buffer);
	return 0;
}


static ssize_t
sysdev_store(struct kobject * kobj, struct attribute * attr, 
	     const char * buffer, size_t count)
{
	struct sys_device * sysdev = to_sysdev(kobj);
	struct sysdev_attribute * sysdev_attr = to_sysdev_attr(attr);

	if (sysdev_attr->store)
		return sysdev_attr->store(sysdev,buffer,count);
	return 0;
}

static struct sysfs_ops sysfs_ops = {
	.show	= sysdev_show,
	.store	= sysdev_store,
};

static struct kobj_type ktype_sysdev = {
	.sysfs_ops	= &sysfs_ops,
};


int sysdev_create_file(struct sys_device * s, struct sysdev_attribute * a)
{
	return sysfs_create_file(&s->kobj,&a->attr);
}


void sysdev_remove_file(struct sys_device * s, struct sysdev_attribute * a)
{
	sysfs_remove_file(&s->kobj,&a->attr);
}

EXPORT_SYMBOL(sysdev_create_file);
EXPORT_SYMBOL(sysdev_remove_file);

/* 
 * declare system_subsys 
 */
decl_subsys(system,&ktype_sysdev,NULL);

int sysdev_class_register(struct sysdev_class * cls)
{
	pr_debug("Registering sysdev class '%s'\n",cls->kset.kobj.name);
	INIT_LIST_HEAD(&cls->drivers);
	cls->kset.subsys = &system_subsys;
	kset_set_kset_s(cls,system_subsys);
	return kset_register(&cls->kset);
}

void sysdev_class_unregister(struct sysdev_class * cls)
{
	pr_debug("Unregistering sysdev class '%s'\n",cls->kset.kobj.name);
	kset_unregister(&cls->kset);
}

EXPORT_SYMBOL(sysdev_class_register);
EXPORT_SYMBOL(sysdev_class_unregister);


static LIST_HEAD(global_drivers);

/**
 *	sysdev_driver_register - Register auxillary driver
 * 	@cls:	Device class driver belongs to.
 *	@drv:	Driver.
 *
 *	If @cls is valid, then @drv is inserted into @cls->drivers to be 
 *	called on each operation on devices of that class. The refcount
 *	of @cls is incremented. 
 *	Otherwise, @drv is inserted into global_drivers, and called for 
 *	each device.
 */

int sysdev_driver_register(struct sysdev_class * cls, 
			   struct sysdev_driver * drv)
{
	down_write(&system_subsys.rwsem);
	if (cls && kset_get(&cls->kset)) {
		list_add_tail(&drv->entry,&cls->drivers);

		/* If devices of this class already exist, tell the driver */
		if (drv->add) {
			struct sys_device *dev;
			list_for_each_entry(dev, &cls->kset.list, kobj.entry)
				drv->add(dev);
		}
	} else
		list_add_tail(&drv->entry,&global_drivers);
	up_write(&system_subsys.rwsem);
	return 0;
}


/**
 *	sysdev_driver_unregister - Remove an auxillary driver.
 *	@cls:	Class driver belongs to.
 *	@drv:	Driver.
 */
void sysdev_driver_unregister(struct sysdev_class * cls,
			      struct sysdev_driver * drv)
{
	down_write(&system_subsys.rwsem);
	list_del_init(&drv->entry);
	if (cls) {
		if (drv->remove) {
			struct sys_device *dev;
			list_for_each_entry(dev, &cls->kset.list, kobj.entry)
				drv->remove(dev);
		}
		kset_put(&cls->kset);
	}
	up_write(&system_subsys.rwsem);
}


/**
 *	sys_device_register - add a system device to the tree
 *	@sysdev:	device in question
 *
 */
int sys_device_register(struct sys_device * sysdev)
{
	int error;
	struct sysdev_class * cls = sysdev->cls;

	if (!cls)
		return -EINVAL;

	/* Make sure the kset is set */
	sysdev->kobj.kset = &cls->kset;

	/* But make sure we point to the right type for sysfs translation */
	sysdev->kobj.ktype = &ktype_sysdev;

	/* set the kobject name */
	snprintf(sysdev->kobj.name,KOBJ_NAME_LEN,"%s%d",
		 cls->kset.kobj.name,sysdev->id);

	pr_debug("Registering sys device '%s'\n",sysdev->kobj.name);

	/* Register the object */
	error = kobject_register(&sysdev->kobj);

	if (!error) {
		struct sysdev_driver * drv;

		down_write(&system_subsys.rwsem);
		/* Generic notification is implicit, because it's that 
		 * code that should have called us. 
		 */

		/* Notify global drivers */
		list_for_each_entry(drv,&global_drivers,entry) {
			if (drv->add)
				drv->add(sysdev);
		}

		/* Notify class auxillary drivers */
		list_for_each_entry(drv,&cls->drivers,entry) {
			if (drv->add)
				drv->add(sysdev);
		}
		up_write(&system_subsys.rwsem);
	}
	return error;
}

void sys_device_unregister(struct sys_device * sysdev)
{
	struct sysdev_driver * drv;

	down_write(&system_subsys.rwsem);
	list_for_each_entry(drv,&global_drivers,entry) {
		if (drv->remove)
			drv->remove(sysdev);
	}

	list_for_each_entry(drv,&sysdev->cls->drivers,entry) {
		if (drv->remove)
			drv->remove(sysdev);
	}
	up_write(&system_subsys.rwsem);

	kobject_unregister(&sysdev->kobj);
}



/**
 *	sysdev_shutdown - Shut down all system devices.
 *
 *	Loop over each class of system devices, and the devices in each
 *	of those classes. For each device, we call the shutdown method for
 *	each driver registered for the device - the globals, the auxillaries,
 *	and the class driver. 
 *
 *	Note: The list is iterated in reverse order, so that we shut down
 *	child devices before we shut down thier parents. The list ordering
 *	is guaranteed by virtue of the fact that child devices are registered
 *	after their parents. 
 */

void sysdev_shutdown(void)
{
	struct sysdev_class * cls;

	pr_debug("Shutting Down System Devices\n");

	down_write(&system_subsys.rwsem);
	list_for_each_entry_reverse(cls,&system_subsys.kset.list,
				    kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Shutting down type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* Call global drivers first. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->shutdown)
					drv->shutdown(sysdev);
			}

			/* Now call the generic one */
			if (cls->shutdown)
				cls->shutdown(sysdev);
		}
	}
	up_write(&system_subsys.rwsem);
}


/**
 *	sysdev_save - Save system device state
 *	@state:	Power state we're entering.
 *
 *	This is called when the system is going to sleep, but before interrupts 
 *	have been disabled. This allows system device drivers to allocate and 
 *	save device state, including sleeping during the process..
 */

int sysdev_save(u32 state)
{
	struct sysdev_class * cls;

	pr_debug("Saving System Device State\n");

	down_write(&system_subsys.rwsem);

	list_for_each_entry_reverse(cls,&system_subsys.kset.list,
				    kset.kobj.entry) {
		struct sys_device * sysdev;
		pr_debug("Saving state for type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;

			pr_debug(" %s\n",sysdev->kobj.name);

			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->save)
					drv->save(sysdev,state);
			}

			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->save)
					drv->save(sysdev,state);
			}

			if (cls->save)
				cls->save(sysdev,state);
		}
	}
	up_write(&system_subsys.rwsem);
	return 0;
}


/**
 *	sysdev_suspend - Suspend all system devices.
 *	@state:		Power state to enter.
 *
 *	We perform an almost identical operation as sys_device_shutdown()
 *	above, though calling ->suspend() instead.
 *
 *	Note: Interrupts are disabled when called, so we can't sleep when
 *	trying to get the subsystem's rwsem. If that happens, print a nasty
 *	warning and return an error.
 */

int sysdev_suspend(u32 state)
{
	struct sysdev_class * cls;

	pr_debug("Suspending System Devices\n");

	if (!down_write_trylock(&system_subsys.rwsem)) {
		printk("%s: Cannot acquire semaphore; Failing\n",__FUNCTION__);
		return -EFAULT;
	}

	list_for_each_entry_reverse(cls,&system_subsys.kset.list,
				    kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Suspending type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* Call global drivers first. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->suspend)
					drv->suspend(sysdev,state);
			}

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->suspend)
					drv->suspend(sysdev,state);
			}

			/* Now call the generic one */
			if (cls->suspend)
				cls->suspend(sysdev,state);
		}
	}
	up_write(&system_subsys.rwsem);

	return 0;
}


/**
 *	sysdev_resume - Bring system devices back to life.
 *
 *	Similar to sys_device_suspend(), but we iterate the list forwards
 *	to guarantee that parent devices are resumed before their children.
 *
 *	Note: Interrupts are disabled when called.
 */

int sysdev_resume(void)
{
	struct sysdev_class * cls;

	pr_debug("Resuming System Devices\n");

	if(!down_write_trylock(&system_subsys.rwsem))
		return -EFAULT;

	list_for_each_entry(cls,&system_subsys.kset.list,kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Resuming type '%s':\n",cls->kset.kobj.name);

		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			/* First, call the class-specific one */
			if (cls->resume)
				cls->resume(sysdev);

			/* Call auxillary drivers next. */
			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->resume)
					drv->resume(sysdev);
			}

			/* Call global drivers. */
			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->resume)
					drv->resume(sysdev);
			}

		}
	}
	up_write(&system_subsys.rwsem);
	return 0;
}


/**
 *	sysdev_restore - Restore system device state
 *
 *	This is called during a suspend/resume cycle last, after interrupts 
 *	have been re-enabled. This is intended for auxillary drivers, etc, 
 *	that may sleep when restoring state.
 */

int sysdev_restore(void)
{
	struct sysdev_class * cls;

	down_write(&system_subsys.rwsem);
	pr_debug("Restoring System Device State\n");

	list_for_each_entry(cls,&system_subsys.kset.list,kset.kobj.entry) {
		struct sys_device * sysdev;

		pr_debug("Restoring state for type '%s':\n",cls->kset.kobj.name);
		list_for_each_entry(sysdev,&cls->kset.list,kobj.entry) {
			struct sysdev_driver * drv;
			pr_debug(" %s\n",sysdev->kobj.name);

			if (cls->restore)
				cls->restore(sysdev);

			list_for_each_entry(drv,&cls->drivers,entry) {
				if (drv->restore)
					drv->restore(sysdev);
			}

			list_for_each_entry(drv,&global_drivers,entry) {
				if (drv->restore)
					drv->restore(sysdev);
			}
		}
	}

	up_write(&system_subsys.rwsem);
	return 0;
}


int __init sys_bus_init(void)
{
	system_subsys.kset.kobj.parent = &devices_subsys.kset.kobj;
	return subsystem_register(&system_subsys);
}

EXPORT_SYMBOL(sys_device_register);
EXPORT_SYMBOL(sys_device_unregister);
