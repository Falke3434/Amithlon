/* main.c: AFS client file system
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <rxrpc/rxrpc.h>
#include <rxrpc/transport.h>
#include <rxrpc/call.h>
#include <rxrpc/peer.h>
#include "cell.h"
#include "server.h"
#include "fsclient.h"
#include "cmservice.h"
#include "kafstimod.h"
#include "kafsasyncd.h"
#include "internal.h"

struct rxrpc_transport *afs_transport;

static int afs_init(void);
static void afs_exit(void);
static int afs_adding_peer(struct rxrpc_peer *peer);
static void afs_discarding_peer(struct rxrpc_peer *peer);

module_init(afs_init);
module_exit(afs_exit);

MODULE_DESCRIPTION("AFS Client File System");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL");

static struct rxrpc_peer_ops afs_peer_ops = {
	.adding		= afs_adding_peer,
	.discarding	= afs_discarding_peer,
};

struct list_head afs_cb_hash_tbl[AFS_CB_HASH_COUNT];
spinlock_t afs_cb_hash_lock = SPIN_LOCK_UNLOCKED;

/*****************************************************************************/
/*
 * initialise the AFS client FS module
 */
static int afs_init(void)
{
	int loop, ret;

	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 registering.\n");

	/* initialise the callback hash table */
	spin_lock_init(&afs_cb_hash_lock);
	for (loop=AFS_CB_HASH_COUNT-1; loop>=0; loop--)
		INIT_LIST_HEAD(&afs_cb_hash_tbl[loop]);

	/* register the /proc stuff */
	ret = afs_proc_init();
	if (ret<0)
		return ret;

	/* initialise the cell DB */
	ret = afs_cell_init();
	if (ret<0)
		goto error;

	/* start the timeout daemon */
	ret = afs_kafstimod_start();
	if (ret<0)
		goto error;

	/* start the async operation daemon */
	ret = afs_kafsasyncd_start();
	if (ret<0)
		goto error_kafstimod;

	/* create the RxRPC transport */
	ret = rxrpc_create_transport(7001,&afs_transport);
	if (ret<0)
		goto error_kafsasyncd;

	afs_transport->peer_ops = &afs_peer_ops;

	/* register the filesystems */
	ret = afs_fs_init();
	if (ret<0)
		goto error_transport;

	return ret;

 error_transport:
	rxrpc_put_transport(afs_transport);
 error_kafsasyncd:
	afs_kafsasyncd_stop();
 error_kafstimod:
	afs_kafstimod_stop();
 error:
	afs_cell_purge();
	afs_proc_cleanup();
	printk(KERN_ERR "kAFS: failed to register: %d\n",ret);
	return ret;
} /* end afs_init() */

/*****************************************************************************/
/*
 * clean up on module removal
 */
static void __exit afs_exit(void)
{
	printk(KERN_INFO "kAFS: Red Hat AFS client v0.1 unregistering.\n");

	afs_fs_exit();
	rxrpc_put_transport(afs_transport);
	afs_kafstimod_stop();
	afs_kafsasyncd_stop();
	afs_cell_purge();
	afs_proc_cleanup();

} /* end afs_exit() */

/*****************************************************************************/
/*
 * notification that new peer record is being added
 * - called from krxsecd
 * - return an error to induce an abort
 * - mustn't sleep (caller holds an rwlock)
 */
static int afs_adding_peer(struct rxrpc_peer *peer)
{
	afs_server_t *server;
	int ret;

	_debug("kAFS: Adding new peer %08x\n",ntohl(peer->addr.s_addr));

	/* determine which server the peer resides in (if any) */
	ret = afs_server_find_by_peer(peer,&server);
	if (ret<0)
		return ret; /* none that we recognise, so abort */

	_debug("Server %p{u=%d}\n",server,atomic_read(&server->usage));

	_debug("Cell %p{u=%d}\n",server->cell,atomic_read(&server->cell->usage));

	/* cross-point the structs under a global lock */
	spin_lock(&afs_server_peer_lock);
	peer->user = server;
	server->peer = peer;
	spin_unlock(&afs_server_peer_lock);

	afs_put_server(server);

	return 0;
} /* end afs_adding_peer() */

/*****************************************************************************/
/*
 * notification that a peer record is being discarded
 * - called from krxiod or krxsecd
 */
static void afs_discarding_peer(struct rxrpc_peer *peer)
{
	afs_server_t *server;

	_enter("%p",peer);

	_debug("Discarding peer %08x (rtt=%lu.%lumS)\n",
	       ntohl(peer->addr.s_addr),
	       (long)(peer->rtt/1000),
	       (long)(peer->rtt%1000));

	/* uncross-point the structs under a global lock */
	spin_lock(&afs_server_peer_lock);
	server = peer->user;
	if (server) {
		peer->user = NULL;
		server->peer = NULL;

		//_debug("Server %p{u=%d}\n",server,atomic_read(&server->usage));
		//_debug("Cell %p{u=%d}\n",server->cell,atomic_read(&server->cell->usage));
	}
	spin_unlock(&afs_server_peer_lock);

	_leave("");

} /* end afs_discarding_peer() */
