/* drm_context.h -- IOCTLs for generic contexts -*- linux-c -*-
 * Created: Fri Nov 24 18:31:37 2000 by gareth@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 * ChangeLog:
 *  2001-11-16	Torsten Duwe <duwe@caldera.de>
 *		added context constructor/destructor hooks,
 *		needed by SiS driver's memory management.
 */

#include "drmP.h"

#if !__HAVE_CTX_BITMAP
#error "__HAVE_CTX_BITMAP must be defined"
#endif


/* ================================================================
 * Context bitmap support
 */

void DRM(ctxbitmap_free)( drm_device_t *dev, int ctx_handle )
{
	if ( ctx_handle < 0 ) goto failed;
	if ( !dev->ctx_bitmap ) goto failed;

	if ( ctx_handle < DRM_MAX_CTXBITMAP ) {
		down(&dev->struct_sem);
		clear_bit( ctx_handle, dev->ctx_bitmap );
		dev->context_sareas[ctx_handle] = NULL;
		up(&dev->struct_sem);
		return;
	}
failed:
       	DRM_ERROR( "Attempt to free invalid context handle: %d\n",
		   ctx_handle );
       	return;
}

int DRM(ctxbitmap_next)( drm_device_t *dev )
{
	int bit;

	if(!dev->ctx_bitmap) return -1;

	down(&dev->struct_sem);
	bit = find_first_zero_bit( dev->ctx_bitmap, DRM_MAX_CTXBITMAP );
	if ( bit < DRM_MAX_CTXBITMAP ) {
		set_bit( bit, dev->ctx_bitmap );
	   	DRM_DEBUG( "drm_ctxbitmap_next bit : %d\n", bit );
		if((bit+1) > dev->max_context) {
			dev->max_context = (bit+1);
			if(dev->context_sareas) {
				drm_map_t **ctx_sareas;

				ctx_sareas = DRM(realloc)(dev->context_sareas,
						(dev->max_context - 1) * 
						sizeof(*dev->context_sareas),
						dev->max_context * 
						sizeof(*dev->context_sareas),
						DRM_MEM_MAPS);
				if(!ctx_sareas) {
					clear_bit(bit, dev->ctx_bitmap);
					up(&dev->struct_sem);
					return -1;
				}
				dev->context_sareas = ctx_sareas;
				dev->context_sareas[bit] = NULL;
			} else {
				/* max_context == 1 at this point */
				dev->context_sareas = DRM(alloc)(
						dev->max_context * 
						sizeof(*dev->context_sareas),
						DRM_MEM_MAPS);
				if(!dev->context_sareas) {
					clear_bit(bit, dev->ctx_bitmap);
					up(&dev->struct_sem);
					return -1;
				}
				dev->context_sareas[bit] = NULL;
			}
		}
		up(&dev->struct_sem);
		return bit;
	}
	up(&dev->struct_sem);
	return -1;
}

int DRM(ctxbitmap_init)( drm_device_t *dev )
{
	int i;
   	int temp;

	down(&dev->struct_sem);
	dev->ctx_bitmap = (unsigned long *) DRM(alloc)( PAGE_SIZE,
							DRM_MEM_CTXBITMAP );
	if ( dev->ctx_bitmap == NULL ) {
		up(&dev->struct_sem);
		return -ENOMEM;
	}
	memset( (void *)dev->ctx_bitmap, 0, PAGE_SIZE );
	dev->context_sareas = NULL;
	dev->max_context = -1;
	up(&dev->struct_sem);

	for ( i = 0 ; i < DRM_RESERVED_CONTEXTS ; i++ ) {
		temp = DRM(ctxbitmap_next)( dev );
	   	DRM_DEBUG( "drm_ctxbitmap_init : %d\n", temp );
	}

	return 0;
}

void DRM(ctxbitmap_cleanup)( drm_device_t *dev )
{
	down(&dev->struct_sem);
	if( dev->context_sareas ) DRM(free)( dev->context_sareas,
					     sizeof(*dev->context_sareas) * 
					     dev->max_context,
					     DRM_MEM_MAPS );
	DRM(free)( (void *)dev->ctx_bitmap, PAGE_SIZE, DRM_MEM_CTXBITMAP );
	up(&dev->struct_sem);
}

/* ================================================================
 * Per Context SAREA Support
 */

int DRM(getsareactx)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_priv_map_t request;
	drm_map_t *map;

	if (copy_from_user(&request,
			   (drm_ctx_priv_map_t *)arg,
			   sizeof(request)))
		return -EFAULT;

	down(&dev->struct_sem);
	if (dev->max_context < 0 || request.ctx_id >= (unsigned) dev->max_context) {
		up(&dev->struct_sem);
		return -EINVAL;
	}

	map = dev->context_sareas[request.ctx_id];
	up(&dev->struct_sem);

	request.handle = map->handle;
	if (copy_to_user((drm_ctx_priv_map_t *)arg, &request, sizeof(request)))
		return -EFAULT;
	return 0;
}

int DRM(setsareactx)(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_priv_map_t request;
	drm_map_t *map = NULL;
	drm_map_list_t *r_list = NULL;
	struct list_head *list;

	if (copy_from_user(&request,
			   (drm_ctx_priv_map_t *)arg,
			   sizeof(request)))
		return -EFAULT;

	down(&dev->struct_sem);
	list_for_each(list, &dev->maplist->head) {
		r_list = list_entry(list, drm_map_list_t, head);
		if(r_list->map &&
		   r_list->map->handle == request.handle)
			goto found;
	}
bad:
	up(&dev->struct_sem);
	return -EINVAL;

found:
	map = r_list->map;
	if (!map) goto bad;
	if (dev->max_context < 0)
		goto bad;
	if (request.ctx_id >= (unsigned) dev->max_context)
		goto bad;
	dev->context_sareas[request.ctx_id] = map;
	up(&dev->struct_sem);
	return 0;
}

/* ================================================================
 * The actual DRM context handling routines
 */

int DRM(context_switch)( drm_device_t *dev, int old, int new )
{
        if ( test_and_set_bit( 0, &dev->context_flag ) ) {
                DRM_ERROR( "Reentering -- FIXME\n" );
                return -EBUSY;
        }


        DRM_DEBUG( "Context switch from %d to %d\n", old, new );

        if ( new == dev->last_context ) {
                clear_bit( 0, &dev->context_flag );
                return 0;
        }

        return 0;
}

int DRM(context_switch_complete)( drm_device_t *dev, int new )
{
        dev->last_context = new;  /* PRE/POST: This is the _only_ writer. */
        dev->last_switch  = jiffies;

        if ( !_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ) {
                DRM_ERROR( "Lock isn't held after context switch\n" );
        }

				/* If a context switch is ever initiated
                                   when the kernel holds the lock, release
                                   that lock here. */
        clear_bit( 0, &dev->context_flag );
        wake_up( &dev->context_wait );

        return 0;
}

int DRM(resctx)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_ctx_res_t res;
	drm_ctx_t ctx;
	int i;

	if ( copy_from_user( &res, (drm_ctx_res_t *)arg, sizeof(res) ) )
		return -EFAULT;

	if ( res.count >= DRM_RESERVED_CONTEXTS ) {
		memset( &ctx, 0, sizeof(ctx) );
		for ( i = 0 ; i < DRM_RESERVED_CONTEXTS ; i++ ) {
			ctx.handle = i;
			if ( copy_to_user( &res.contexts[i],
					   &i, sizeof(i) ) )
				return -EFAULT;
		}
	}
	res.count = DRM_RESERVED_CONTEXTS;

	if ( copy_to_user( (drm_ctx_res_t *)arg, &res, sizeof(res) ) )
		return -EFAULT;
	return 0;
}

int DRM(addctx)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ctx_t ctx;

	if ( copy_from_user( &ctx, (drm_ctx_t *)arg, sizeof(ctx) ) )
		return -EFAULT;

	ctx.handle = DRM(ctxbitmap_next)( dev );
	if ( ctx.handle == DRM_KERNEL_CONTEXT ) {
				/* Skip kernel's context and get a new one. */
		ctx.handle = DRM(ctxbitmap_next)( dev );
	}
	DRM_DEBUG( "%d\n", ctx.handle );
	if ( ctx.handle == -1 ) {
		DRM_DEBUG( "Not enough free contexts.\n" );
				/* Should this return -EBUSY instead? */
		return -ENOMEM;
	}
#ifdef DRIVER_CTX_CTOR
	if ( ctx.handle != DRM_KERNEL_CONTEXT )
		DRIVER_CTX_CTOR(ctx.handle); /* XXX: also pass dev ? */
#endif

	if ( copy_to_user( (drm_ctx_t *)arg, &ctx, sizeof(ctx) ) )
		return -EFAULT;
	return 0;
}

int DRM(modctx)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	/* This does nothing */
	return 0;
}

int DRM(getctx)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_ctx_t ctx;

	if ( copy_from_user( &ctx, (drm_ctx_t*)arg, sizeof(ctx) ) )
		return -EFAULT;

	/* This is 0, because we don't handle any context flags */
	ctx.flags = 0;

	if ( copy_to_user( (drm_ctx_t*)arg, &ctx, sizeof(ctx) ) )
		return -EFAULT;
	return 0;
}

int DRM(switchctx)( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ctx_t ctx;

	if ( copy_from_user( &ctx, (drm_ctx_t *)arg, sizeof(ctx) ) )
		return -EFAULT;

	DRM_DEBUG( "%d\n", ctx.handle );
	return DRM(context_switch)( dev, dev->last_context, ctx.handle );
}

int DRM(newctx)( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ctx_t ctx;

	if ( copy_from_user( &ctx, (drm_ctx_t *)arg, sizeof(ctx) ) )
		return -EFAULT;

	DRM_DEBUG( "%d\n", ctx.handle );
	DRM(context_switch_complete)( dev, ctx.handle );

	return 0;
}

int DRM(rmctx)( struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_ctx_t ctx;

	if ( copy_from_user( &ctx, (drm_ctx_t *)arg, sizeof(ctx) ) )
		return -EFAULT;

	DRM_DEBUG( "%d\n", ctx.handle );
	if ( ctx.handle == DRM_KERNEL_CONTEXT + 1 ) {
		priv->remove_auth_on_close = 1;
	}
	if ( ctx.handle != DRM_KERNEL_CONTEXT ) {
#ifdef DRIVER_CTX_DTOR
		DRIVER_CTX_DTOR(ctx.handle); /* XXX: also pass dev ? */
#endif
		DRM(ctxbitmap_free)( dev, ctx.handle );
	}

	return 0;
}

