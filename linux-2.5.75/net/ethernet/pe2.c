#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>

static int pEII_request(struct datalink_proto *dl, 
		struct sk_buff *skb, unsigned char *dest_node)
{
	struct net_device	*dev = skb->dev;

	skb->protocol = htons (ETH_P_IPX);
	if(dev->hard_header)
		dev->hard_header(skb, dev, ETH_P_IPX, dest_node, NULL, skb->len);
	return dev_queue_xmit(skb);
}

struct datalink_proto *
make_EII_client(void)
{
	struct datalink_proto	*proto;

	proto = (struct datalink_proto *) kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto != NULL) {
		proto->header_length = 0;
		proto->request = pEII_request;
	}

	return proto;
}

void destroy_EII_client(struct datalink_proto *dl)
{
	if (dl)
		kfree(dl);
}
