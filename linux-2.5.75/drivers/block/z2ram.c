/*
** z2ram - Amiga pseudo-driver to access 16bit-RAM in ZorroII space
**         as a block device, to be used as a RAM disk or swap space
** 
** Copyright (C) 1994 by Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
**
** ++Geert: support for zorro_unused_z2ram, better range checking
** ++roman: translate accesses via an array
** ++Milan: support for ChipRAM usage
** ++yambo: converted to 2.0 kernel
** ++yambo: modularized and support added for 3 minor devices including:
**          MAJOR  MINOR  DESCRIPTION
**          -----  -----  ----------------------------------------------
**          37     0       Use Zorro II and Chip ram
**          37     1       Use only Zorro II ram
**          37     2       Use only Chip ram
**          37     4-7     Use memory list entry 1-4 (first is 0)
** ++jskov: support for 1-4th memory list entry.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define DEVICE_NAME "Z2RAM"

#include <linux/major.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/blk.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/bitops.h>
#include <asm/amigahw.h>
#include <asm/pgtable.h>

#include <linux/zorro.h>


extern int m68k_realnum_memory;
extern struct mem_info m68k_memory[NUM_MEMINFO];

#define TRUE                  (1)
#define FALSE                 (0)

#define Z2MINOR_COMBINED      (0)
#define Z2MINOR_Z2ONLY        (1)
#define Z2MINOR_CHIPONLY      (2)
#define Z2MINOR_MEMLIST1      (4)
#define Z2MINOR_MEMLIST2      (5)
#define Z2MINOR_MEMLIST3      (6)
#define Z2MINOR_MEMLIST4      (7)
#define Z2MINOR_COUNT         (8) /* Move this down when adding a new minor */

#define Z2RAM_CHUNK1024       ( Z2RAM_CHUNKSIZE >> 10 )

static u_long *z2ram_map    = NULL;
static u_long z2ram_size    = 0;
static int z2_count         = 0;
static int chip_count       = 0;
static int list_count       = 0;
static int current_device   = -1;

static spinlock_t z2ram_lock = SPIN_LOCK_UNLOCKED;

static struct block_device_operations z2_fops;
static struct gendisk *z2ram_gendisk;

static void do_z2_request(request_queue_t *q)
{
	struct request *req;
	while ((req = elv_next_request(q)) != NULL) {
		unsigned long start = req->sector << 9;
		unsigned long len  = req->current_nr_sectors << 9;

		if (start + len > z2ram_size) {
			printk( KERN_ERR DEVICE_NAME ": bad access: block=%lu, count=%u\n",
				req->sector, req->current_nr_sectors);
			end_request(req, 0);
			continue;
		}
		while (len) {
			unsigned long addr = start & Z2RAM_CHUNKMASK;
			unsigned long size = Z2RAM_CHUNKSIZE - addr;
			if (len < size)
				size = len;
			addr += z2ram_map[ start >> Z2RAM_CHUNKSHIFT ];
			if (rq_data_dir(req) == READ)
				memcpy(req->buffer, (char *)addr, size);
			else
				memcpy((char *)addr, req->buffer, size);
			start += size;
			len -= size;
		}
		end_request(req, 1);
	}
}

static void
get_z2ram( void )
{
    int i;

    for ( i = 0; i < Z2RAM_SIZE / Z2RAM_CHUNKSIZE; i++ )
    {
	if ( test_bit( i, zorro_unused_z2ram ) )
	{
	    z2_count++;
	    z2ram_map[ z2ram_size++ ] = 
		ZTWO_VADDR( Z2RAM_START ) + ( i << Z2RAM_CHUNKSHIFT );
	    clear_bit( i, zorro_unused_z2ram );
	}
    }

    return;
}

static void
get_chipram( void )
{

    while ( amiga_chip_avail() > ( Z2RAM_CHUNKSIZE * 4 ) )
    {
	chip_count++;
	z2ram_map[ z2ram_size ] =
	    (u_long)amiga_chip_alloc( Z2RAM_CHUNKSIZE, "z2ram" );

	if ( z2ram_map[ z2ram_size ] == 0 )
	{
	    break;
	}

	z2ram_size++;
    }
	
    return;
}

static int
z2_open( struct inode *inode, struct file *filp )
{
    int device;
    int max_z2_map = ( Z2RAM_SIZE / Z2RAM_CHUNKSIZE ) *
	sizeof( z2ram_map[0] );
    int max_chip_map = ( amiga_chip_size / Z2RAM_CHUNKSIZE ) *
	sizeof( z2ram_map[0] );
    int rc = -ENOMEM;

    device = minor( inode->i_rdev );

    if ( current_device != -1 && current_device != device )
    {
	rc = -EBUSY;
	goto err_out;
    }

    if ( current_device == -1 )
    {
	z2_count   = 0;
	chip_count = 0;
	list_count = 0;
	z2ram_size = 0;

	/* Use a specific list entry. */
	if (device >= Z2MINOR_MEMLIST1 && device <= Z2MINOR_MEMLIST4) {
		int index = device - Z2MINOR_MEMLIST1 + 1;
		unsigned long size, paddr, vaddr;

		if (index >= m68k_realnum_memory) {
			printk( KERN_ERR DEVICE_NAME
				": no such entry in z2ram_map\n" );
		        goto err_out;
		}

		paddr = m68k_memory[index].addr;
		size = m68k_memory[index].size & ~(Z2RAM_CHUNKSIZE-1);

#ifdef __powerpc__
		/* FIXME: ioremap doesn't build correct memory tables. */
		{
			vfree(vmalloc (size));
		}

		vaddr = (unsigned long) __ioremap (paddr, size, 
						   _PAGE_WRITETHRU);

#else
		vaddr = (unsigned long)z_remap_nocache_nonser(paddr, size);
#endif
		z2ram_map = 
			kmalloc((size/Z2RAM_CHUNKSIZE)*sizeof(z2ram_map[0]),
				GFP_KERNEL);
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    goto err_out;
		}

		while (size) {
			z2ram_map[ z2ram_size++ ] = vaddr;
			size -= Z2RAM_CHUNKSIZE;
			vaddr += Z2RAM_CHUNKSIZE;
			list_count++;
		}

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME
			": using %iK List Entry %d Memory\n",
			list_count * Z2RAM_CHUNK1024, index );
	} else

	switch ( device )
	{
	    case Z2MINOR_COMBINED:

		z2ram_map = kmalloc( max_z2_map + max_chip_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    goto err_out;
		}

		get_z2ram();
		get_chipram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK Zorro II RAM and %iK Chip RAM (Total %dK)\n",
			z2_count * Z2RAM_CHUNK1024,
			chip_count * Z2RAM_CHUNK1024,
			( z2_count + chip_count ) * Z2RAM_CHUNK1024 );

	    break;

    	    case Z2MINOR_Z2ONLY:
		z2ram_map = kmalloc( max_z2_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    goto err_out;
		}

		get_z2ram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK of Zorro II RAM\n",
			z2_count * Z2RAM_CHUNK1024 );

	    break;

	    case Z2MINOR_CHIPONLY:
		z2ram_map = kmalloc( max_chip_map, GFP_KERNEL );
		if ( z2ram_map == NULL )
		{
		    printk( KERN_ERR DEVICE_NAME
			": cannot get mem for z2ram_map\n" );
		    goto err_out;
		}

		get_chipram();

		if ( z2ram_size != 0 )
		    printk( KERN_INFO DEVICE_NAME 
			": using %iK Chip RAM\n",
			chip_count * Z2RAM_CHUNK1024 );
		    
	    break;

	    default:
		rc = -ENODEV;
		goto err_out;
	
	    break;
	}

	if ( z2ram_size == 0 )
	{
	    printk( KERN_NOTICE DEVICE_NAME
		": no unused ZII/Chip RAM found\n" );
	    goto err_out_kfree;
	}

	current_device = device;
	z2ram_size <<= Z2RAM_CHUNKSHIFT;
	set_capacity(z2ram_gendisk, z2ram_size >> 9);
    }

    return 0;

err_out_kfree:
    kfree( z2ram_map );
err_out:
    return rc;
}

static int
z2_release( struct inode *inode, struct file *filp )
{
    if ( current_device == -1 )
	return 0;     

    /*
     * FIXME: unmap memory
     */

    return 0;
}

static struct block_device_operations z2_fops =
{
	.owner		= THIS_MODULE,
	.open		= z2_open,
	.release	= z2_release,
};

static struct kobject *z2_find(dev_t dev, int *part, void *data)
{
	*part = 0;
	return get_disk(z2ram_gendisk);
}

static struct request_queue z2_queue;

int __init 
z2_init(void)
{

    if (!MACH_IS_AMIGA)
	return -ENXIO;

    if (register_blkdev(Z2RAM_MAJOR, DEVICE_NAME))
	return -EBUSY;

    z2ram_gendisk = alloc_disk(1);
    if (!z2ram_gendisk) {
	unregister_blkdev(Z2RAM_MAJOR, DEVICE_NAME);
	return -ENOMEM;
    }
    z2ram_gendisk->major = Z2RAM_MAJOR;
    z2ram_gendisk->first_minor = 0;
    z2ram_gendisk->fops = &z2_fops;
    sprintf(z2ram_gendisk->disk_name, "z2ram");

    blk_init_queue(&z2_queue, do_z2_request, &z2ram_lock);
    z2ram_gendisk->queue = &z2_queue;
    add_disk(z2ram_gendisk);
    blk_register_region(MKDEV(Z2RAM_MAJOR, 0), Z2MINOR_COUNT, THIS_MODULE,
				z2_find, NULL, NULL);

    return 0;
}

#if defined(MODULE)

MODULE_LICENSE("GPL");

int
init_module( void )
{
    int error;
    
    error = z2_init();
    if ( error == 0 )
    {
	printk( KERN_INFO DEVICE_NAME ": loaded as module\n" );
    }
    
    return error;
}

void
cleanup_module( void )
{
    int i, j;
    blk_unregister_region(MKDEV(Z2RAM_MAJOR, 0), 256);
    if ( unregister_blkdev( Z2RAM_MAJOR, DEVICE_NAME ) != 0 )
	printk( KERN_ERR DEVICE_NAME ": unregister of device failed\n");

    del_gendisk(z2ram_gendisk);
    put_disk(z2ram_gendisk);
    blk_cleanup_queue(&z2_queue);

    if ( current_device != -1 )
    {
	i = 0;

	for ( j = 0 ; j < z2_count; j++ )
	{
	    set_bit( i++, zorro_unused_z2ram ); 
	}

	for ( j = 0 ; j < chip_count; j++ )
	{
	    if ( z2ram_map[ i ] )
	    {
		amiga_chip_free( (void *) z2ram_map[ i++ ] );
	    }
	}

	if ( z2ram_map != NULL )
	{
	    kfree( z2ram_map );
	}
    }

    return;
} 
#endif
