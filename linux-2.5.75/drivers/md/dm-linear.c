/*
 * Copyright (C) 2001 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>

/*
 * Linear: maps a linear range of a device.
 */
struct linear_c {
	struct dm_dev *dev;
	sector_t start;
};

/*
 * Construct a linear mapping: <dev_path> <offset>
 */
static int linear_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct linear_c *lc;

	if (argc != 2) {
		ti->error = "dm-linear: Not enough arguments";
		return -EINVAL;
	}

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (lc == NULL) {
		ti->error = "dm-linear: Cannot allocate linear context";
		return -ENOMEM;
	}

	if (sscanf(argv[1], SECTOR_FORMAT, &lc->start) != 1) {
		ti->error = "dm-linear: Invalid device sector";
		goto bad;
	}

	if (dm_get_device(ti, argv[0], lc->start, ti->len,
			  dm_table_get_mode(ti->table), &lc->dev)) {
		ti->error = "dm-linear: Device lookup failed";
		goto bad;
	}

	ti->private = lc;
	return 0;

      bad:
	kfree(lc);
	return -EINVAL;
}

static void linear_dtr(struct dm_target *ti)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	dm_put_device(ti, lc->dev);
	kfree(lc);
}

static int linear_map(struct dm_target *ti, struct bio *bio)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	bio->bi_bdev = lc->dev->bdev;
	bio->bi_sector = lc->start + (bio->bi_sector - ti->begin);

	return 1;
}

static int linear_status(struct dm_target *ti, status_type_t type,
			 char *result, unsigned int maxlen)
{
	struct linear_c *lc = (struct linear_c *) ti->private;
	char b[BDEVNAME_SIZE];

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s " SECTOR_FORMAT,
			 bdevname(lc->dev->bdev, b), lc->start);
		break;
	}
	return 0;
}

static struct target_type linear_target = {
	.name   = "linear",
	.module = THIS_MODULE,
	.ctr    = linear_ctr,
	.dtr    = linear_dtr,
	.map    = linear_map,
	.status = linear_status,
};

int __init dm_linear_init(void)
{
	int r = dm_register_target(&linear_target);

	if (r < 0)
		DMERR("linear: register failed %d", r);

	return r;
}

void dm_linear_exit(void)
{
	int r = dm_unregister_target(&linear_target);

	if (r < 0)
		DMERR("linear: unregister failed %d", r);
}
