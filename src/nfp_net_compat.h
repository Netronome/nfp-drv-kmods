/*
 * Copyright (C) 2015-2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_net_compat.h
 * Common declarations for kernel backwards-compatibility
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#ifndef _NFP_NET_COMPAT_H_
#define _NFP_NET_COMPAT_H_

#include "nfpcore/kcompat.h"

#include <asm/barrier.h>
#include <linux/bitops.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
#include <linux/bpf.h>
#endif
#include <linux/ethtool.h>
#include <linux/compiler.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/msi.h>
#include <linux/netdev_features.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/udp.h>

#include <net/act_api.h>
#include <net/pkt_cls.h>
#if COMPAT__HAS_DEVLINK
#include <net/devlink.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#include <net/switchdev.h>
#endif
#include <net/tc_act/tc_mirred.h>

#include "nfp_net.h"

#define COMPAT__HAVE_VXLAN_OFFLOAD \
	(VER_NON_RHEL_GE(3, 12) || VER_RHEL_GE(7, 4))
#define COMPAT__HAVE_NDO_FEATURES_CHECK \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
#define COMPAT__HAVE_UDP_OFFLOAD \
	(VER_NON_RHEL_GE(4, 8) || VER_RHEL_GE(7, 4))
#define COMPAT__HAVE_XDP \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0))
#define COMPAT__HAVE_XDP_ADJUST_HEAD \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
#define COMPAT__HAVE_XDP_METADATA \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0))
/* We only want to support switchdev with ops and attrs */
#define COMPAT__HAVE_SWITCHDEV_ATTRS \
	(VER_NON_RHEL_GE(4, 5) || VER_RHEL_GE(7, 5))

#ifndef NETIF_F_HW_VLAN_CTAG_RX
#define NETIF_F_HW_VLAN_CTAG_RX NETIF_F_HW_VLAN_RX
#endif
#ifndef NETIF_F_HW_VLAN_CTAG_TX
#define NETIF_F_HW_VLAN_CTAG_TX NETIF_F_HW_VLAN_TX
#endif
#ifndef NETIF_F_HW_VLAN_CTAG_FILTER
#define NETIF_F_HW_VLAN_CTAG_FILTER	NETIF_F_HW_VLAN_FILTER
#endif
#ifndef NETIF_F_HW_TC
#define NETIF_F_HW_TC	NETDEV_FEATURE_COUNT
#endif
#ifndef ETH_RSS_HASH_NO_CHANGE
#define ETH_RSS_HASH_NO_CHANGE	0
#endif
#ifndef ETH_RSS_HASH_UNKNOWN
#define ETH_RSS_HASH_UNKNOWN	0
#endif
#ifndef ETH_RSS_HASH_TOP
#define ETH_RSS_HASH_TOP_BIT	0
#define ETH_RSS_HASH_TOP	(1 << ETH_RSS_HASH_TOP_BIT)
#endif
#ifndef ETH_RSS_HASH_XOR
#define ETH_RSS_HASH_FUNCS_COUNT	1
#define ETH_RSS_HASH_XOR	2
#endif
#ifndef ETH_RSS_HASH_CRC32
#define ETH_RSS_HASH_CRC32	4
#endif

#ifndef NETIF_F_CSUM_MASK
#define NETIF_F_CSUM_MASK	NETIF_F_ALL_CSUM
#endif

#ifndef SPEED_5000
#define SPEED_5000		5000
#endif
#ifndef SPEED_25000
#define SPEED_25000		25000
#endif
#ifndef SPEED_40000
#define SPEED_40000		40000
#endif
#ifndef SPEED_50000
#define SPEED_50000		50000
#endif
#ifndef SPEED_100000
#define SPEED_100000		100000
#endif

#ifndef XDP_PACKET_HEADROOM
#define XDP_PACKET_HEADROOM	256
#endif

#ifndef XDP_FLAGS_DRV_MODE
#define XDP_FLAGS_DRV_MODE	(1 << 2)
#endif
#ifndef XDP_FLAGS_HW_MODE
#define XDP_FLAGS_HW_MODE	(1 << 3)
#endif

#ifndef XDP_FLAGS_MODES
#define XDP_FLAGS_MODES		(XDP_FLAGS_DRV_MODE | XDP_FLAGS_HW_MODE)
#endif

#ifndef SWITCHDEV_SET_OPS
#if COMPAT__HAVE_SWITCHDEV_ATTRS && defined(CONFIG_NET_SWITCHDEV)
#define SWITCHDEV_SET_OPS(netdev, ops) ((netdev)->switchdev_ops = (ops))
#else
#define SWITCHDEV_SET_OPS(netdev, ops) do {} while (0)
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
#define NAPI_POLL_WEIGHT	64
#define NETIF_F_GSO_GRE		0
#define NETIF_F_GSO_UDP_TUNNEL	0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
typedef u32 netdev_features_t;

static inline u32 ethtool_rxfh_indir_default(u32 index, u32 n_rx_rings)
{
	return index % n_rx_rings;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
static inline void eth_hw_addr_random(struct net_device *dev)
{
	dev->addr_assign_type |= NET_ADDR_RANDOM;
	random_ether_addr(dev->dev_addr);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
static inline int netif_get_num_default_rss_queues(void)
{
	return min_t(int, 8, num_online_cpus());
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
static inline int
netif_set_xps_queue(struct net_device *d, const struct cpumask *m, u16 i)
{
	return 0;
}

static inline int eth_prepare_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!(dev->priv_flags & IFF_LIVE_ADDR_CHANGE) && netif_running(dev))
		return -EBUSY;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	return 0;
}

static inline void eth_commit_mac_addr_change(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)
static inline struct sk_buff *
ns___vlan_hwaccel_put_tag(struct sk_buff *skb, __be16 vlan_proto,
			  u16 vlan_tci)
{
	return __vlan_hwaccel_put_tag(skb, vlan_tci);
}

#define __vlan_hwaccel_put_tag ns___vlan_hwaccel_put_tag
#endif

#if VER_NON_RHEL_LT(3, 11) || VER_RHEL_LT(7, 0)
enum {
	IFLA_VF_LINK_STATE_AUTO,	/* link state of the uplink */
	IFLA_VF_LINK_STATE_ENABLE,	/* link always up */
	IFLA_VF_LINK_STATE_DISABLE,	/* link always down */
	__IFLA_VF_LINK_STATE_MAX,
};
#endif

#if VER_NON_RHEL_LT(3, 13) || VER_RHEL_LT(7, 1)
static inline void u64_stats_init(struct u64_stats_sync *syncp)
{
#if BITS_PER_LONG == 32 && defined(CONFIG_SMP)
	seqcount_init(&syncp->seq);
#endif
}
#endif

#if VER_NON_RHEL_LT(3, 14)
enum compat_pkt_hash_types {
	compat_PKT_HASH_TYPE_NONE,     /* Undefined type */
	compat_PKT_HASH_TYPE_L2,       /* Input: src_MAC, dest_MAC */
	compat_PKT_HASH_TYPE_L3,       /* Input: src_IP, dst_IP */
	compat_PKT_HASH_TYPE_L4,       /* Input: src_IP, dst_IP,
						 src_port, dst_port */
};

#define PKT_HASH_TYPE_NONE	compat_PKT_HASH_TYPE_NONE
#define PKT_HASH_TYPE_L2	compat_PKT_HASH_TYPE_L2
#define PKT_HASH_TYPE_L3	compat_PKT_HASH_TYPE_L3
#define PKT_HASH_TYPE_L4	compat_PKT_HASH_TYPE_L4

static inline void compat_skb_set_hash(struct sk_buff *skb, __u32 hash,
				       enum compat_pkt_hash_types type)
{
	skb->l4_rxhash = (type == PKT_HASH_TYPE_L4);
	skb->rxhash = hash;
}

#define skb_set_hash(s, h, t)	compat_skb_set_hash(s, h, t)
#endif

#if VER_NON_RHEL_LT(3, 14) || VER_RHEL_LT(7, 2)
static inline void dev_consume_skb_any(struct sk_buff *skb)
{
	dev_kfree_skb_any(skb);
}
#endif

static inline int skb_xmit_more(struct sk_buff *skb)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
	return false;
#else
	return skb->xmit_more;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
static inline void pci_msi_unmask_irq(struct irq_data *data)
{
	unmask_msi_irq(data);
}
#endif

static inline void
compat_incr_checksum_unnecessary(struct sk_buff *skb, bool encap)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->encapsulation = encap;
#else
	__skb_incr_checksum_unnecessary(skb);
#endif
}

#if VER_NON_RHEL_LT(3, 19) || VER_RHEL_LT(7, 2)
static inline void netdev_rss_key_fill(void *buffer, size_t len)
{
	get_random_bytes(buffer, len);
}

static inline void napi_schedule_irqoff(struct napi_struct *n)
{
	napi_schedule(n);
}

static inline void napi_complete_done(struct napi_struct *n, int work_done)
{
	napi_complete(n);
}

static inline struct page *dev_alloc_page(void)
{
	return alloc_page(GFP_ATOMIC | __GFP_COLD | __GFP_NOWARN);
}
#endif

#if VER_NON_RHEL_LT(3, 19) || VER_RHEL_LT(7, 2)
static inline int skb_put_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;

	if (unlikely(size < len)) {
		len -= size;
		if (skb_pad(skb, len))
			return -ENOMEM;
		__skb_put(skb, len);
	}
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
#define napi_alloc_frag(x) netdev_alloc_frag(x)
#endif

#if VER_NON_RHEL_LT(3, 19) || VER_RHEL_LT(7, 3)
struct netdev_phys_item_id {
	unsigned char id[32];
	unsigned char id_len;
};
#endif

#if VER_NON_RHEL_LT(4, 0) || VER_RHEL_LT(7, 2)
#define skb_vlan_tag_present(skb)	vlan_tx_tag_present(skb)
#define skb_vlan_tag_get(skb)		vlan_tx_tag_get(skb)
#endif

#if VER_NON_RHEL_LT(4, 1)
static inline netdev_features_t vlan_features_check(const struct sk_buff *skb,
						    netdev_features_t features)
{
	return features;
}
#endif

#if !COMPAT__HAVE_NDO_FEATURES_CHECK
static inline bool compat_is_vxlan(struct sk_buff *skb, u8 l4_hdr)
{
	if (
/* Note: VXLAN was not setting TEB as inner_protocol before 3.18 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	    skb->inner_protocol != htons(ETH_P_TEB) ||
	    skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
#endif
	    l4_hdr != IPPROTO_UDP ||
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
	    skb_inner_mac_header(skb) - skb_transport_header(skb) !=
	    sizeof(struct udphdr) + 8 ||
#endif
	    0)
		return false;
	return true;
}

static inline bool compat_is_gretap(struct sk_buff *skb, u8 l4_hdr)
{
	if (
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	    skb->inner_protocol != htons(ETH_P_TEB) ||
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
	    skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
#endif
	    l4_hdr != IPPROTO_GRE)
		return false;
	return true;
}
#endif

#if VER_NON_RHEL_LT(4, 2) || VER_RHEL_LT(7, 3)
static inline void skb_free_frag(void *addr)
{
	put_page(virt_to_head_page(addr));
}
#endif

#if VER_NON_RHEL_LT(4, 2) || VER_RHEL_LT(7, 5)
enum switchdev_attr_id {
	SWITCHDEV_ATTR_ID_UNDEFINED,
	SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
};

struct switchdev_attr {
	enum switchdev_attr_id id;
	union {
		struct netdev_phys_item_id ppid;
	} u;
};

struct switchdev_ops {
	int (*switchdev_port_attr_get)(struct net_device *dev,
				       struct switchdev_attr *attr);
};
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
#define SWITCHDEV_ATTR_ID_PORT_PARENT_ID	SWITCHDEV_ATTR_PORT_PARENT_ID
#endif

static inline int
compat_ndo_features_check(struct nfp_net *nn, struct sk_buff *skb)
{
#if !COMPAT__HAVE_NDO_FEATURES_CHECK
	u8 l4_hdr;

	if (!skb->encapsulation)
		return 0;

	/* TSO limitation checks */
	if (skb_is_gso(skb)) {
		u32 hdrlen;

		hdrlen = skb_inner_transport_header(skb) - skb->data +
			inner_tcp_hdrlen(skb);

		if (unlikely(hdrlen > NFP_NET_LSO_MAX_HDR_SZ)) {
			nn_dp_warn(&nn->dp, "L4 offset too large for TSO!\n");
			return 1;
		}
	}

	/* Checksum encap validation */
	if (!(nn->dp.ctrl & NFP_NET_CFG_CTRL_TXCSUM) ||
	    skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	switch (vlan_get_protocol(skb)) {
	case htons(ETH_P_IP):
		l4_hdr = ip_hdr(skb)->protocol;
		break;
	case htons(ETH_P_IPV6):
		l4_hdr = ipv6_hdr(skb)->nexthdr;
		break;
	default:
		nn_dp_warn(&nn->dp, "non-IP packet for checksumming!\n");
		return 1;
	}

	if (!compat_is_vxlan(skb, l4_hdr) && !compat_is_gretap(skb, l4_hdr)) {
		nn_dp_warn(&nn->dp, "checksum on unsupported tunnel type!\n");
		return 1;
	}

#endif /* !COMPAT__HAVE_NDO_FEATURES_CHECK */
	return 0;
}

#if COMPAT__HAVE_VXLAN_OFFLOAD && !COMPAT__HAVE_UDP_OFFLOAD
#include <net/vxlan.h>

enum udp_parsable_tunnel_type {
	UDP_TUNNEL_TYPE_VXLAN,
};

struct udp_tunnel_info {
	unsigned short type;
	__be16 port;
};

static inline void udp_tunnel_get_rx_info(struct net_device *netdev)
{
	vxlan_get_rx_port(netdev);
}
#endif

#if VER_NON_RHEL_LT(4, 5) || VER_RHEL_LT(7, 3)
static inline int skb_inner_transport_offset(const struct sk_buff *skb)
{
	return skb_inner_transport_header(skb) - skb->data;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
struct compat__ethtool_link_ksettings {
	struct ethtool_cmd base;
};
/* Cheat backports */
#define ethtool_link_ksettings compat__ethtool_link_ksettings

static inline u32
compat__ethtool_cmd_speed_get(const struct ethtool_link_ksettings *cmd)
{
	return ethtool_cmd_speed(&cmd->base);
}

static inline void
compat__ethtool_cmd_speed_set(struct ethtool_link_ksettings *cmd, u32 speed)
{
	ethtool_cmd_speed_set(&cmd->base, speed);
}

#undef ethtool_link_ksettings_add_link_mode
#define ethtool_link_ksettings_add_link_mode(cmd, memb, type)	\
		(cmd)->base.memb |= SUPPORTED_ ## type
#else
static inline u32
compat__ethtool_cmd_speed_get(const struct ethtool_link_ksettings *cmd)
{
	return cmd->base.speed;
}

static inline void
compat__ethtool_cmd_speed_set(struct ethtool_link_ksettings *cmd, u32 speed)
{
	cmd->base.speed = speed;
}
#endif

#ifndef IFF_RXFH_CONFIGURED
static inline bool netif_is_rxfh_configured(const struct net_device *netdev)
{
	return false;
}
#endif

#if VER_NON_RHEL_LT(4, 6) || VER_RHEL_LT(7, 3)
static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_count);
}
#endif

#if VER_VANILLA_LT(4, 6) || VER_UBUNTU_LT(4, 4, 21) || VER_RHEL_LT(7, 3)
static inline void napi_consume_skb(struct sk_buff *skb, int budget)
{
	dev_consume_skb_any(skb);
}
#endif

#if VER_NON_RHEL_LT(4, 8) || VER_RHEL_LT(7, 5)
static inline void trace_devlink_hwmsg(void *devlink,
				       bool incoming, unsigned long type,
				       const u8 *buf, size_t len)
{
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
static inline int nfp_net_xdp_offload(struct nfp_net *nn, struct bpf_prog *prog)
{
	return -EINVAL;
}
#endif

#if VER_NON_RHEL_LT(4, 10) || VER_RHEL_LT(7, 5)
#define is_tcf_mirred_egress_redirect is_tcf_mirred_redirect

static inline int
compat__napi_complete_done(struct napi_struct *n, int work_done)
{
	napi_complete_done(n, work_done);
	return true;
}
#define napi_complete_done compat__napi_complete_done
#endif

#if VER_NON_RHEL_GE(4, 11) || VER_RHEL_GE(7, 5)
typedef void compat__stat64_ret_t;
#else
typedef struct rtnl_link_stats64 *compat__stat64_ret_t;

static inline void
trace_xdp_exception(const struct net_device *netdev,
		    const struct bpf_prog *prog, u32 act)
{
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
struct netdev_xdp;

#define NL_SET_ERR_MSG_MOD(ea, msg)	pr_warn(KBUILD_MODNAME ": " msg)

static inline struct netlink_ext_ack *compat__xdp_extact(struct netdev_xdp *xdp)
{
	return NULL;
}
#else
#define compat__xdp_extact(xdp)		(xdp)->extack
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#define compat__xdp_flags(xdp)		(xdp)->flags
#else
static inline u32 compat__xdp_flags(struct netdev_xdp *xdp)
{
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) &&	\
    LINUX_VERSION_CODE  < KERNEL_VERSION(4, 13, 0)
static inline void
tcf_exts_stats_update(const struct tcf_exts *exts,
		      u64 bytes, u64 packets, u64 lastuse)
{
#ifdef CONFIG_NET_CLS_ACT
	int i;

	preempt_disable();

	for (i = 0; i < exts->nr_actions; i++) {
		struct tc_action *a = exts->actions[i];

		tcf_action_stats_update(a, bytes, packets, lastuse);
	}

	preempt_enable();
#endif
}
#endif

#if VER_NON_RHEL_LT(4, 14) || VER_RHEL_LT(7, 5)
struct tc_to_netdev;

enum tc_setup_type {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	__COMPAT_TC_SETUP_CLSFLOWER = TC_SETUP_CLSFLOWER,
#define TC_SETUP_CLSFLOWER __COMPAT_TC_SETUP_CLSFLOWER
#endif
	__COMPAT_tc_setup_type_NONE,
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
static inline void skb_metadata_set(const struct sk_buff *skb, u8 value)
{
}

typedef int tc_setup_cb_t(enum tc_setup_type type, void *type_data,
			  void *cb_priv);

static inline int
tc_setup_cb_egdev_register(const struct net_device *dev, tc_setup_cb_t *cb,
			   void *cb_priv)
{
	return 0;
}

static inline int
tc_setup_cb_egdev_unregister(const struct net_device *dev, tc_setup_cb_t *cb,
			     void *cb_priv)
{
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0) &&	\
    LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
static inline bool
tc_cls_can_offload_and_chain0(const struct net_device *dev,
			      struct tc_cls_common_offload *common)
{
	return !common->chain_index;
}
#endif

#ifdef COMPAT__HAVE_METADATA_IP_TUNNEL
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
static inline struct net_device *tcf_mirred_dev(const struct tc_action *action)
{
	int ifindex;

	ifindex = tcf_mirred_ifindex(action);
	return __dev_get_by_index(current->nsproxy->net_ns, ifindex);
}
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) || !defined(CONFIG_NET_CLS)
#define tcf_block_shared(b)	false
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
static inline void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq)
{
}

static inline int
xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq, struct net_device *dev, u32 q)
{
	return 0;
}
#endif

#if COMPAT__HAS_DEVLINK && LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
enum devlink_port_flavour {
	DEVLINK_PORT_FLAVOUR_PHYSICAL,
	DEVLINK_PORT_FLAVOUR_CPU,
	DEVLINK_PORT_FLAVOUR_DSA,
};

static inline void
devlink_port_attrs_set(struct devlink_port *devlink_port,
		       enum devlink_port_flavour flavour,
		       u32 port_number, bool split, u32 split_subport_number)
{
	if (split)
		devlink_port_split_set(devlink_port, port_number);
}
#endif

#if !LINUX_RELEASE_4_19
#define tcf_block_cb_register(block, cb, ident, priv, ea)	\
	tcf_block_cb_register(block, cb, ident, priv)

#if COMPAT__HAVE_XDP
static inline int
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
xdp_attachment_query(struct xdp_attachment_info *info, struct netdev_xdp *bpf)
#else
xdp_attachment_query(struct xdp_attachment_info *info, struct netdev_bpf *bpf)
#endif
{
	bpf->prog_attached = !!info->prog;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	if (info->flags & XDP_FLAGS_HW_MODE)
		bpf->prog_attached = XDP_ATTACHED_HW;
	bpf->prog_id = info->prog ? info->prog->aux->id : 0;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0)
	bpf->prog_flags = info->prog ? info->flags : 0;
#endif
	return 0;
}

static inline bool xdp_attachment_flags_ok(struct xdp_attachment_info *info,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
					   struct netdev_xdp *bpf)
#else
					   struct netdev_bpf *bpf)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	if (info->prog && (bpf->flags ^ info->flags) & XDP_FLAGS_MODES) {
		NL_SET_ERR_MSG(bpf->extack,
			       "program loaded with different flags");
		return false;
	}
#endif
	return true;
}

static inline void
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
xdp_attachment_setup(struct xdp_attachment_info *info, struct netdev_xdp *bpf)
#else
xdp_attachment_setup(struct xdp_attachment_info *info, struct netdev_bpf *bpf)
#endif
{
	if (info->prog)
		bpf_prog_put(info->prog);
	info->prog = bpf->prog;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	info->flags = bpf->flags;
#endif
}
#endif /* COMPAT__HAVE_XDP */
#endif
#endif /* _NFP_NET_COMPAT_H_ */
