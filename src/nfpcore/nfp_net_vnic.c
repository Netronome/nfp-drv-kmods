// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/*
 * nfp_net_vnic.c
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *
 * Implements a software network interface for communicating over the
 * PCIe interface to the NFP's ARM core.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/io.h>
#include <linux/in6.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp_net_vnic.h"

#define IPHDR_ALIGN_OFFSET	((16 - (sizeof(struct ethhdr))) & 0xf)
#define PKTBUF_OFFSET		(2) /* ((4 - (sizeof(struct ethhdr))) & 0xf) */

/*
 * The NFP net splits the BAR memory into two equal parts, one in each
 * direction.  The host Rx part corresponds to the ARM's Tx part and
 * vice versa.
 *
 * We do not perform any fancy packet buffering.  Each direction can
 * only contain one packet, and the driver will not put any new
 * packets into the buffer until the previous packet has been consumed
 * by the other side.  The packet buffer can be polled by checking its
 * 'length' field.  When a packet has been consumed the 'length' field
 * is set to zero.
 *
 * The 'enabled' field is written with a non-zero value to indicate
 * that the Rx queue is active.  This is used by the other side of the
 * connection to perform carrier detection.
 *
 * The 'type' field is currently not used and must be set to zero.
 */

struct nfp_net_vnic_queue {
	struct {
		u32 length;
		u32 type;
		u32 enabled;
		u32 __rsvd;
	} ctrl;
	char pktdata[0];
};

struct nfp_net_vnic {
	struct platform_device *pdev;
	struct net_device *netdev;
	struct nfp_cpp *cpp;
	struct nfp_cpp_area *area;
	struct net_device_stats stats;
	struct timer_list timer;
	unsigned long timer_int;

	unsigned int id;
	unsigned int mtu;
	unsigned hostside:1;
	unsigned up:1;

	u8 remote_mac[ETH_ALEN];    /* Remote MAC address */
	struct in6_addr remote_ip6; /* Remote IPv6 Address */

	void __iomem *pktbufs;
	struct nfp_net_vnic_queue __iomem *rx;
	struct nfp_net_vnic_queue __iomem *tx;
};

static unsigned int nfp_net_vnic_pollinterval = 1;
module_param(nfp_net_vnic_pollinterval, uint, 0444);
MODULE_PARM_DESC(nfp_net_vnic_pollinterval, "Polling interval for Rx/Tx queues (in ms)");

static unsigned int nfp_net_vnic_debug;
module_param(nfp_net_vnic_debug, uint, 0444);
MODULE_PARM_DESC(nfp_net_vnic_debug, "Enable debug printk messages");

#define nfp_net_vnic_err(sn, fmt, args...) \
	netdev_err((sn)->netdev, fmt, ## args)
#define nfp_net_vnic_warn(sn, fmt, args...) \
	netdev_warn((sn)->netdev, fmt, ## args)
#define nfp_net_vnic_info(sn, fmt, args...) \
	netdev_info((sn)->netdev, fmt, ## args)
#define nfp_net_vnic_dbg(sn, fmt, args...) do { \
		if (nfp_net_vnic_debug) \
			netdev_dbg((sn)->netdev, fmt, ## args); \
	} while (0)

static void nnq_pkt_read(void *dst, struct nfp_net_vnic_queue __iomem *squeue,
			 size_t size)
{
	u32 __iomem *s = (u32 __iomem *)&squeue->pktdata[0];
	u32 *d, n;

	/* Make sure that packet data is aligned at word boundaries */
#if PKTBUF_OFFSET != 0
	u32 tmp = __raw_readl(s++);
	u8 *tb = (u8 *)&tmp;
#  if PKTBUF_OFFSET == 1
	((u8 *)dst)[0] = tb[1];
	((u8 *)dst)[1] = tb[2];
	((u8 *)dst)[2] = tb[3];
#  elif PKTBUF_OFFSET == 2
	((u8 *)dst)[0] = tb[2];
	((u8 *)dst)[1] = tb[3];
#  elif PKTBUF_OFFSET == 3
	((u8 *)dst)[0] = tb[3];
#  endif
	dst += (4 - PKTBUF_OFFSET);
	size -= (4 - PKTBUF_OFFSET);
#endif

	d = dst;
	n = (size + sizeof(u32) - 1) / sizeof(u32);
	while (n--)
		*d++ = __raw_readl(s++);
}

static void nnq_pkt_write(struct nfp_net_vnic_queue __iomem *dqueue,
			  void *src, size_t size)
{
	u32 __iomem *d = (u32 __iomem *)&dqueue->pktdata[0];
	u32 *s, n;

	/* Make sure that packet data is aligned at word boundaries */
#if PKTBUF_OFFSET != 0
	u32 tmp = 0;
	u8 *tb = (u8 *)&tmp;
#  if PKTBUF_OFFSET == 1
	tb[1] = ((u8 *)src)[0];
	tb[2] = ((u8 *)src)[1];
	tb[3] = ((u8 *)src)[2];
#  elif PKTBUF_OFFSET == 2
	tb[2] = ((u8 *)src)[0];
	tb[3] = ((u8 *)src)[1];
#  elif PKTBUF_OFFSET == 3
	tb[3] = ((u8 *)src)[0];
#  endif
	__raw_writel(tmp, d++);
	src += (4 - PKTBUF_OFFSET);
	size -= (4 - PKTBUF_OFFSET);
#endif

	s = src;
	n = (size + sizeof(u32) - 1) / sizeof(u32);
	while (n--)
		__raw_writel(*s++, d++);
}

static int nnq_enabled(struct nfp_net_vnic_queue __iomem *queue)
{
	return readl(&queue->ctrl.enabled);
}

static int nnq_pending_pkt(struct nfp_net_vnic_queue __iomem *queue)
{
	return readl(&queue->ctrl.length);
}

static void nnq_receive_pkt(struct nfp_net_vnic_queue __iomem *queue)
{
	writel(0, &queue->ctrl.length);
}

static int nfp_net_vnic_carrier_detect(struct nfp_net_vnic *vnic)
{
	if (nnq_enabled(vnic->tx)) {
		if (!vnic->up) {
			netif_carrier_on(vnic->netdev);
			vnic->up = 1;
		}
	} else {
		if (vnic->up) {
			netif_carrier_off(vnic->netdev);
			vnic->up = 0;
		}
	}

	return vnic->up;
}

static int nfp_net_vnic_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct nfp_net_vnic *vnic = netdev_priv(netdev);

	if (!vnic->up) {
		/* We are disconnected. */
		dev_kfree_skb_any(skb);
		vnic->stats.tx_errors++;
		vnic->stats.tx_carrier_errors++;
		nfp_net_vnic_dbg(vnic, "Tx while down (%d)\n", skb->len);
		return NETDEV_TX_OK;
	}
	if (skb->len > vnic->mtu) {
		/* Packet is too big. */
		dev_kfree_skb_any(skb);
		vnic->stats.tx_errors++;
		vnic->stats.tx_dropped++;
		nfp_net_vnic_err(vnic, "Tx Too big (%d)\n", skb->len);
		return NETDEV_TX_OK;
	}
	if (nnq_pending_pkt(vnic->tx)) {
		/* Tx queue still has a pending packet. */
		netif_stop_queue(netdev);
		nfp_net_vnic_dbg(vnic, "Tx Busy [%d]\n", skb->len);
		return NETDEV_TX_BUSY;
	}

	/* Deliver the packet. */
	nnq_pkt_write(vnic->tx, skb->data, skb->len);
	writel(0, &vnic->tx->ctrl.type);
	writel(skb->len, &vnic->tx->ctrl.length);

	/* Flush the writes */
	wmb();

	dev_kfree_skb_any(skb);
	netif_trans_update(netdev);
	vnic->stats.tx_packets++;
	vnic->stats.tx_bytes += skb->len;

	nfp_net_vnic_dbg(vnic, "Tx Complete [%d]\n", skb->len);
	return NETDEV_TX_OK;
}

static void nfp_net_vnic_tx_poll(struct nfp_net_vnic *vnic)
{
	if (!nnq_pending_pkt(vnic->tx) &&
	    netif_queue_stopped(vnic->netdev)) {
		/* Transmit completed; restart our transmit queue. */
		nfp_net_vnic_dbg(vnic, "Tx Resume\n");
		netif_wake_queue(vnic->netdev);
	}
}

static int nfp_net_vnic_rx(struct nfp_net_vnic *vnic)
{
	struct sk_buff *skb;
	u32 pkt_size, pkt_type;
	int err;

	pkt_size = readl(&vnic->rx->ctrl.length);
	if (pkt_size == 0)
		return 0;

	pkt_type = readl(&vnic->rx->ctrl.type);
	if (pkt_size > vnic->mtu || pkt_type != 0) {
		nfp_net_vnic_dbg(vnic,
				 "Rx packet invalid (len = %d, type = %d)\n",
				 pkt_size, pkt_type);
		nnq_receive_pkt(vnic->rx);
		return -EINVAL;
	}

	skb = netdev_alloc_skb(vnic->netdev, pkt_size + IPHDR_ALIGN_OFFSET);
	if (!skb) {
		nnq_receive_pkt(vnic->rx);
		vnic->stats.rx_dropped++;
		nfp_net_vnic_dbg(vnic, "Rx Failure [%d]\n", pkt_size);
		return -ENOMEM;
	}
	skb_reserve(skb, IPHDR_ALIGN_OFFSET);

	/* Deliver the packet. */
	nnq_pkt_read(skb_put(skb, pkt_size), vnic->rx, pkt_size);
	skb->dev = vnic->netdev;
	skb->protocol = eth_type_trans(skb, vnic->netdev);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	err = netif_receive_skb(skb);

	if (!err) {
		vnic->stats.rx_packets++;
		vnic->stats.rx_bytes += pkt_size;
		nfp_net_vnic_dbg(vnic, "Rx Received [%d]\n", pkt_size);
	} else {
		vnic->stats.rx_dropped++;
		nfp_net_vnic_dbg(vnic, "Rx Dropped  [%d]\n", pkt_size);
	}

	nnq_receive_pkt(vnic->rx);
	return err;
}

static void nfp_net_vnic_rx_poll(struct nfp_net_vnic *vnic)
{
	if (nnq_pending_pkt(vnic->rx))
		nfp_net_vnic_rx(vnic);
}

/*
 * nfp_net_vnic_schedule - Schedule NFP net timer to trigger again
 * @vnic:	vnic pointer
 */
static void nfp_net_vnic_schedule(struct nfp_net_vnic *vnic)
{
	mod_timer(&vnic->timer, jiffies + vnic->timer_int);
}

/*
 * nfp_net_vnic_timer - Timer triggered to poll vnic queues
 * @data:	vnic pointer
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
static void nfp_net_vnic_timer(unsigned long t)
#else
static void nfp_net_vnic_timer(struct timer_list *t)
#endif
{
	struct nfp_net_vnic *vnic = from_timer(vnic, t, timer);

	BUG_ON(!vnic);

	nfp_net_vnic_carrier_detect(vnic);

	if (vnic->up) {
		nfp_net_vnic_rx_poll(vnic);
		nfp_net_vnic_tx_poll(vnic);
	}

	nfp_net_vnic_schedule(vnic);
}

static struct net_device_stats *nfp_net_vnic_stats(struct net_device *netdev)
{
	struct nfp_net_vnic *vnic = netdev_priv(netdev);

	return &vnic->stats;
}

static int nfp_net_vnic_netdev_open(struct net_device *netdev)
{
	struct nfp_net_vnic *vnic = netdev_priv(netdev);

	/* Setup a timer for polling queues at regular intervals. */
	timer_setup(&vnic->timer, nfp_net_vnic_timer, 0);
	vnic->timer_int = nfp_net_vnic_pollinterval * HZ / 1000;
	if (!vnic->timer_int)
		vnic->timer_int = 1;
	nfp_net_vnic_schedule(vnic);

	netif_start_queue(netdev);

	/* Mark our side of the queue as enabled. */
	writel(1, &vnic->rx->ctrl.enabled);

	nfp_net_vnic_dbg(vnic, "%s opened\n", netdev->name);
	return 0;
}

static int nfp_net_vnic_netdev_close(struct net_device *netdev)
{
	struct nfp_net_vnic *vnic = netdev_priv(netdev);

	netif_stop_queue(netdev);
	del_timer_sync(&vnic->timer);

	/* Mark our side of the queue as disabled. */
	writel(0, &vnic->rx->ctrl.enabled);

	nfp_net_vnic_dbg(vnic, "%s closed\n", netdev->name);
	return 0;
}

/*
 * Device initialization/cleanup.
 */
static struct net_device_ops nfp_net_vnic_netdev_ops = {
	.ndo_open = nfp_net_vnic_netdev_open,
	.ndo_stop = nfp_net_vnic_netdev_close,
	.ndo_start_xmit = nfp_net_vnic_tx,
	.ndo_get_stats = nfp_net_vnic_stats,
};

/*
 * sysfs interface
 */
static ssize_t
show_remote_mac(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfp_net_vnic *vnic = dev_get_drvdata(dev);

	return sprintf(buf, "%pM\n", vnic->remote_mac);
}

static ssize_t
show_remote_ip6(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nfp_net_vnic *vnic = dev_get_drvdata(dev);

	/* 2.6.31+ groks %pI6c as a format string. 2.6.29 introduce %pI but
	 * we ignore this and roll our own for all kernels lower than
	 * 2.6.31 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31))
	return sprintf(buf, "%pI6c\n", &vnic->remote_ip6);
#else
	return sprintf(buf,
		       "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
		       vnic->remote_ip6.s6_addr[0],
		       vnic->remote_ip6.s6_addr[1],

		       vnic->remote_ip6.s6_addr[2],
		       vnic->remote_ip6.s6_addr[3],

		       vnic->remote_ip6.s6_addr[4],
		       vnic->remote_ip6.s6_addr[5],

		       vnic->remote_ip6.s6_addr[6],
		       vnic->remote_ip6.s6_addr[7],

		       vnic->remote_ip6.s6_addr[8],
		       vnic->remote_ip6.s6_addr[9],

		       vnic->remote_ip6.s6_addr[10],
		       vnic->remote_ip6.s6_addr[11],

		       vnic->remote_ip6.s6_addr[12],
		       vnic->remote_ip6.s6_addr[13],

		       vnic->remote_ip6.s6_addr[14],
		       vnic->remote_ip6.s6_addr[15]);
#endif
}

static DEVICE_ATTR(remote_mac, S_IRUGO, show_remote_mac, NULL);
static DEVICE_ATTR(remote_ip6, S_IRUGO, show_remote_ip6, NULL);

static int nfp_net_vnic_attr_add(struct nfp_net_vnic *vnic)
{
	int err = 0;

	err = device_create_file(&vnic->pdev->dev, &dev_attr_remote_mac);
	if (err)
		return err;

	err = device_create_file(&vnic->pdev->dev, &dev_attr_remote_ip6);
	if (err)
		device_remove_file(&vnic->pdev->dev, &dev_attr_remote_mac);

	return err;
}

static void nfp_net_vnic_attr_remove(struct nfp_net_vnic *vnic)
{
	device_remove_file(&vnic->pdev->dev, &dev_attr_remote_mac);
	device_remove_file(&vnic->pdev->dev, &dev_attr_remote_ip6);
}

/*
 * nfp_net_vnic_assign_addr - Assign a MAC address (and work out remote address)
 * @netdev:	netdev pointer
 *
 * No unique MAC addresses are allocated for this network link. Instead a
 * unique locally assigned MAC address is derived from the MAC address of
 * the management interface.  Further, we alter a single bit in the MAC
 * address to distinguish between the NFP and the PCI host side of this
 * driver. Finally, we derive the self assigned IPv6 address for the remote
 * end based on IPv6s stateless address autoconfiguration. This IPv6
 * address is also accessible via sysfs to facilitate connection setup.
 *
 * If for whatever reason the MAC address for the management interface is
 * not available a fall back MAC address is used. When using the fall back
 * MAC address, the ARM side cannot reliably determine the MAC address
 * chosen on the host if multiple cards are present. Thus, it will not
 * advertise a valid remote MAC and IPv6 address. The host can still
 * determine the MAC/IPv6 address of the ARM. Users can still connect
 * to the ARM from the host via the network link.
 */
#define DEFAULT_MAC "\x02\x15\x4D\x42\x00\x00"
static void nfp_net_vnic_assign_addr(struct net_device *netdev,
				     int vnic_unit, const char *mac_str)
{
	struct nfp_net_vnic *vnic = netdev_priv(netdev);
	u8 mac_addr[ETH_ALEN];
	int default_mac = 0;

	/* Try getting the MAC address from the Management interface  */
	if (!mac_str) {
		ether_addr_copy(mac_addr, DEFAULT_MAC);
		default_mac = 1;
		goto mac_out;
	}

	if (sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		nfp_net_vnic_warn(vnic,
				  "Error converting MAC string (%s). Using default\n",
				  mac_str);
		ether_addr_copy(mac_addr, DEFAULT_MAC);
		default_mac = 1;
		goto mac_out;
	}

	if (is_zero_ether_addr(mac_addr)) {
		nfp_net_vnic_warn(vnic,
				  "Management MAC is Zero. Using default\n");
		ether_addr_copy(mac_addr, DEFAULT_MAC);
		default_mac = 1;
		goto mac_out;
	}

mac_out:
	/* Set the "Locally Administered Address" bit */
	mac_addr[0] |= 0x2;
	mac_addr[5] += vnic_unit;

	ether_addr_copy(vnic->remote_mac, mac_addr);

	/* Use the third least significant bit to create a host/nfp side
	 * unique MAC address. store the remote address in our state for
	 * export via sysfs.
	 * If the fall-back DEFAULT_MAC was used, generate a unique MAC
	 * address for each interface on the host side by adding the NFP id
	 * to the MAC. On the ARM side we cannot determine the NFP id and thus
	 * cannot work out the remote MAC address. */
	if (vnic->hostside) {
		mac_addr[0] |= 0x4;
		if (default_mac)
			mac_addr[ETH_ALEN - 1] = vnic->id;
		vnic->remote_mac[0] &= ~(0x4);
	} else {
		mac_addr[0] &= ~(0x4);
		if (default_mac)
			memset(vnic->remote_mac, 0, ETH_ALEN);
		else
			vnic->remote_mac[0] |= 0x4;
	}

	if (is_zero_ether_addr(mac_addr)) {
		vnic->remote_ip6.s6_addr32[0] = htonl(0xfe800000);
		vnic->remote_ip6.s6_addr32[1] = 0;
	} else {
		/* work out IPv6 address for the remote end
		 * based on net/ipv6/addrconf.c  */
		vnic->remote_ip6.s6_addr32[0] = htonl(0xfe800000);
		vnic->remote_ip6.s6_addr32[1] = 0;

		/* The EUI48 is split in 2 with 0xFF and 0xFE in the middle */
		memcpy(vnic->remote_ip6.s6_addr + 8, vnic->remote_mac, 3);
		memcpy(vnic->remote_ip6.s6_addr + 8 + 5,
		       vnic->remote_mac + 3, 3);
		vnic->remote_ip6.s6_addr[8 + 3] = 0xFF;
		vnic->remote_ip6.s6_addr[8 + 4] = 0xFE;
		vnic->remote_ip6.s6_addr[8 + 0] ^= 0x2;
	}

	/* Set the devices MAC address */
	ether_addr_copy(netdev->dev_addr, mac_addr);
}

static void nfp_net_vnic_netdev_setup(struct net_device *netdev)
{
	ether_setup(netdev);

	netdev->netdev_ops = &nfp_net_vnic_netdev_ops;
	netdev->flags &= ~IFF_MULTICAST;
	netdev->tx_queue_len = 100;
}

static int nfp_net_vnic_probe(struct platform_device *pdev)
{
	struct net_device *netdev;
	struct nfp_net_vnic *vnic;
	int err;
	u16 interface;
	const char *res_name;
	struct nfp_resource *res;
	u64 cpp_addr;
	u32 cpp_id;
	unsigned long barsz;
	struct nfp_cpp_area *area;
	int vnic_unit;
	struct nfp_cpp *cpp;
	struct nfp_platform_data *pdata;
	char *netm_mac = "ethm.mac";
	struct nfp_hwinfo *hwinfo;
	const char *mac_hwinfo;
	char mac_str[32] = {};

	pdata = nfp_platform_device_data(pdev);
	BUG_ON(!pdata);

	cpp = nfp_cpp_from_device_id(pdata->nfp);
	if (!cpp)
		return -ENODEV;

	vnic_unit = pdata->unit;

	/* HACK:
	 *
	 * We perform this hwinfo lookup here, since it will
	 * cause the host driver to poll until all the platform
	 * initialization has been completed by the NFP's ARM
	 * firmware.
	 */
	hwinfo = nfp_hwinfo_read(cpp);
	mac_hwinfo = nfp_hwinfo_lookup(hwinfo, netm_mac);
	if (mac_hwinfo)
		memcpy(mac_str, mac_hwinfo, sizeof(mac_str) - 1);
	kfree(hwinfo);

	switch (vnic_unit) {
	case 0:
		res_name = NFP_RESOURCE_VNIC_PCI_0;
		break;
	case 1:
		res_name = NFP_RESOURCE_VNIC_PCI_1;
		break;
	case 2:
		res_name = NFP_RESOURCE_VNIC_PCI_2;
		break;
	case 3:
		res_name = NFP_RESOURCE_VNIC_PCI_3;
		break;
	default:
		res_name = NULL;
		break;
	}

	if (!res_name) {
		nfp_cpp_free(cpp);
		return -ENODEV;
	}

	res = nfp_resource_acquire(cpp, res_name);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "No '%s' resource present\n",
			res_name);
		err = -ENOENT;
		goto err_resource_acquire;
	}

	cpp_addr = nfp_resource_address(res);
	cpp_id   = nfp_resource_cpp_id(res);
	barsz    = nfp_resource_size(res);
	nfp_resource_release(res);

	area = nfp_cpp_area_alloc_acquire(cpp, "vnic", cpp_id, cpp_addr, barsz);
	if (!area) {
		dev_err(&pdev->dev, "Can't acquire %lu byte area at %d:%d:%d:0x%llx\n",
			barsz, NFP_CPP_ID_TARGET_of(cpp_id),
			NFP_CPP_ID_ACTION_of(cpp_id),
			NFP_CPP_ID_TOKEN_of(cpp_id),
			(unsigned long long)barsz);
		err = -EINVAL;
		goto err_area_acquire;
	}

	netdev = alloc_netdev(sizeof(*vnic), "nvn%d",
			      NET_NAME_UNKNOWN,
			      nfp_net_vnic_netdev_setup);
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_netdev;
	}

	interface = nfp_cpp_interface(cpp);

	/* Setup vnic structure */
	vnic = netdev_priv(netdev);
	memset(vnic, 0, sizeof(*vnic));
	vnic->pdev = pdev;
	vnic->cpp = cpp;
	vnic->area = area;
	vnic->netdev = netdev;
	vnic->id = pdev->id;
	vnic->hostside = NFP_CPP_INTERFACE_TYPE_of(interface)
				!= NFP_CPP_INTERFACE_TYPE_ARM;

	/* Work out our MAC address */
	if (!*mac_str)
		nfp_net_vnic_warn(vnic,
				  "Could not determine MAC address from '%s'. Using default\n",
				  netm_mac);
	nfp_net_vnic_assign_addr(netdev, vnic_unit, mac_str);

	SET_NETDEV_DEV(netdev, &pdev->dev);
	platform_set_drvdata(pdev, vnic);

	/* Control registers and packet data are in BAR0 (64bit) */
	vnic->pktbufs = devm_ioremap_nocache(
		&pdev->dev, nfp_cpp_area_phys(area), barsz);
	if (!vnic->pktbufs) {
		nfp_net_vnic_err(vnic, "Failed to map packet buffers\n");
		err = -EIO;
		goto err_pktbufs;
	}

	/* Setup rx/tx queues according to which side we're on. */
	vnic->mtu = (barsz / 2) - sizeof(*vnic->rx) - sizeof(u32);
	if (vnic->hostside) {
		vnic->rx = vnic->pktbufs;
		vnic->tx = vnic->pktbufs + (barsz / 2);
	} else {
		vnic->rx = vnic->pktbufs + (barsz / 2);
		vnic->tx = vnic->pktbufs;
	}

	/* Prepare the netdev */
	netdev->mtu = vnic->mtu - 16; /* ethhdr + padding */

	err = register_netdev(netdev);
	if (err)
		goto err_register_netdev;

	err = nfp_net_vnic_attr_add(vnic);
	if (err)
		goto err_add_attr;

	nfp_net_vnic_info(vnic, "NFP vNIC MAC Address: %pM\n",
			  netdev->dev_addr);

	return 0;

err_add_attr:
	unregister_netdev(vnic->netdev);
err_register_netdev:
	devm_iounmap(&pdev->dev, vnic->pktbufs);
err_pktbufs:
	free_netdev(netdev);
	platform_set_drvdata(pdev, NULL);
err_alloc_netdev:
	nfp_cpp_area_release_free(area);
err_area_acquire:
err_resource_acquire:
	nfp_cpp_free(cpp);
	return err;
}

static int nfp_net_vnic_remove(struct platform_device *pdev)
{
	struct nfp_net_vnic *vnic = platform_get_drvdata(pdev);

	nfp_net_vnic_attr_remove(vnic);
	unregister_netdev(vnic->netdev);
	devm_iounmap(&pdev->dev, vnic->pktbufs);
	free_netdev(vnic->netdev);
	platform_set_drvdata(pdev, NULL);
	nfp_cpp_area_release_free(vnic->area);
	nfp_cpp_free(vnic->cpp);

	return 0;
}

/*
 * Driver initialization/cleanup.
 */
static struct platform_driver nfp_net_vnic_driver = {
	.probe       = nfp_net_vnic_probe,
	.remove      = nfp_net_vnic_remove,
	.driver = {
		.name        = NFP_NET_VNIC_TYPE,
	},
};

/**
 * nfp_net_vnic_init() - Register the ARM vNIC NFP network driver
 *
 * For communication with the NFP ARM Linux, if running
 *
 * Return: 0, or -ERRNO
 */
int nfp_net_vnic_init(void)
{
	pr_info("%s: NFP vNIC driver, Copyright (C) 2010-2015 Netronome Systems\n",
		NFP_NET_VNIC_TYPE);

	return platform_driver_register(&nfp_net_vnic_driver);
}

/**
 * nfp_net_vnic_exit() - Unregister the ARM vNIC NFP network driver
 */
void nfp_net_vnic_exit(void)
{
	platform_driver_unregister(&nfp_net_vnic_driver);
}
