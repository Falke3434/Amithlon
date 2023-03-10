/*
 *  linux/include/asm-arm/hardware/amba.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASMARM_AMBA_H
#define ASMARM_AMBA_H

struct amba_device {
	struct device		dev;
	struct resource		res;
	unsigned int		irq;
	unsigned int		periphid;
};

struct amba_id {
	unsigned int		id;
	unsigned int		mask;
	void			*data;
};

struct amba_driver {
	struct device_driver	drv;
	int			(*probe)(struct amba_device *, void *);
	int			(*remove)(struct amba_device *);
	void			(*shutdown)(struct amba_device *);
	int			(*suspend)(struct amba_device *, u32, u32);
	int			(*resume)(struct amba_device *, u32);
	struct amba_id		*id_table;
};

#define amba_get_drvdata(d)	dev_get_drvdata(&d->dev)
#define amba_set_drvdata(d,p)	dev_set_drvdata(&d->dev, p)

int amba_driver_register(struct amba_driver *);
void amba_driver_unregister(struct amba_driver *);
int amba_device_register(struct amba_device *, struct resource *);
void amba_device_unregister(struct amba_device *);

#endif
