/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2019 Netronome Systems, Inc. */

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

/* Redefine LINUX_VERSION_CODE for *-next kernels */
#ifdef BPF_F_TEST_RND_HI32
#undef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(5, 3, 0)
#endif

#if (defined(COMPAT__HAVE_METADATA_IP_TUNNEL) ||	\
     LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_csum.h>
#endif

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

#ifdef CONFIG_NET_POLL_CONTROLLER
#define COMPAT__NEED_NDO_POLL_CONTROLLER \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0))
#else
#define COMPAT__NEED_NDO_POLL_CONTROLLER	0
#endif

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

#ifndef ETH_MODULE_SFF_8636
#define ETH_MODULE_SFF_8636	0x3
#endif
#ifndef ETH_MODULE_SFF_8636_LEN
#define ETH_MODULE_SFF_8636_LEN	256
#endif
#ifndef ETH_MODULE_SFF_8436
#define ETH_MODULE_SFF_8436	0x4
#endif
#ifndef ETH_MODULE_SFF_8436_LEN
#define ETH_MODULE_SFF_8436_LEN	256
#endif

#ifndef IANA_VXLAN_UDP_PORT
#define IANA_VXLAN_UDP_PORT	4789
#endif
#ifndef GENEVE_UDP_PORT
#define GENEVE_UDP_PORT		6081
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

#ifndef NL_SET_ERR_MSG_MOD
#define NL_SET_ERR_MSG_MOD(ea, msg)	pr_warn(KBUILD_MODNAME ": " msg)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
typedef struct tc_block_offload compat__flow_block_offload;
typedef struct tc_cls_flower_offload compat__flow_cls_offload;
#else
typedef struct flow_block_offload compat__flow_block_offload;
typedef struct flow_cls_offload compat__flow_cls_offload;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
#define FLOW_CLS_REPLACE TC_CLSFLOWER_REPLACE
#define FLOW_CLS_DESTROY TC_CLSFLOWER_DESTROY
#define FLOW_CLS_STATS TC_CLSFLOWER_STATS
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

#if VER_NON_RHEL_LT(3, 11) || VER_RHEL_LT(7, 3)
static inline struct net_device *netdev_notifier_info_to_dev(void *ptr)
{
	return ptr;
}
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
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
	return skb->xmit_more;
#else
	return netdev_xmit_more();
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define SFP_SFF8472_COMPLIANCE		0x5e
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

#if VER_NON_RHEL_LT(4, 15) || VER_RHEL_LT(7, 7)
static inline void skb_metadata_set(const struct sk_buff *skb, u8 value)
{
}
#endif

#if VER_NON_RHEL_LT(4, 15) || VER_RHEL_LT(7, 6)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
#define DEFINE_SHOW_ATTRIBUTE(__name)					\
static int __name ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __name ## _show, inode->i_private);	\
}									\
									\
static const struct file_operations __name ## _fops = {			\
	.owner		= THIS_MODULE,					\
	.open		= __name ## _open,				\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
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
#if VER_NON_RHEL_LT(4, 16) || VER_RHEL_LT(7, 6)
static inline struct net_device *tcf_mirred_dev(const struct tc_action *action)
{
	int ifindex;

	ifindex = tcf_mirred_ifindex(action);
	return __dev_get_by_index(current->nsproxy->net_ns, ifindex);
}
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0) || \
    (!defined(CONFIG_NET_CLS) && LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0))
#define tcf_block_shared(b)	false
#endif

#if VER_NON_RHEL_LT(4, 16) || VER_RHEL_LT(7, 6)
static inline void xdp_rxq_info_unreg(struct xdp_rxq_info *xdp_rxq)
{
}

static inline int
xdp_rxq_info_reg(struct xdp_rxq_info *xdp_rxq, struct net_device *dev, u32 q)
{
	return 0;
}
#endif

#if COMPAT__HAS_DEVLINK && (VER_NON_RHEL_LT(4, 18) || VER_RHEL_LT(7, 7))
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
struct bpf_offload_dev {
	u32 empty;
};

static inline int
bpf_offload_dev_netdev_register(struct bpf_offload_dev *bpf_dev,
				struct net_device *dev)
{
	return 0;
}

static inline void
bpf_offload_dev_netdev_unregister(struct bpf_offload_dev *bpf_dev,
				  struct net_device *dev)
{
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
static inline struct bpf_offload_dev *bpf_offload_dev_create(void)
#else
static inline struct bpf_offload_dev *
bpf_offload_dev_create(struct bpf_prog_offload_ops *dev_ops)
#endif
{
	return NULL;
}

static inline void bpf_offload_dev_destroy(struct bpf_offload_dev *bpf_dev)
{
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static inline bool
compat_bpf_offload_dev_match(struct bpf_prog *prog, struct net_device *dev)
{
	struct bpf_prog_offload *offload = prog->aux->offload;

	if (!offload)
		return false;
	if (offload->netdev != dev)
		return false;
	return true;
}
#define bpf_offload_dev_match(prog, dev) compat_bpf_offload_dev_match(prog, dev)
#endif

#define FLOW_DISSECTOR_KEY_ENC_IP	22
#define FLOW_DISSECTOR_KEY_ENC_OPTS	23

#define FLOW_DIS_TUN_OPTS_MAX 255
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0) */

#if VER_NON_RHEL_LT(4, 19) || VER_RHEL_LT(7, 7)
struct flow_dissector_key_enc_opts {
	u8 data[FLOW_DIS_TUN_OPTS_MAX];
	u8 len;
	__be16 dst_opt_type;
};

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
	if (bpf->prog_attached && info->flags & XDP_FLAGS_HW_MODE)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
static inline struct sk_buff *__skb_peek(const struct sk_buff_head *list)
{
	return list->next;
}
#endif

#if VER_NON_RHEL_LT(4, 20) || VER_RHEL_LT(7, 7) || VER_RHEL_EQ(8, 0)
static inline bool netif_is_vxlan(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
		!strcmp(dev->rtnl_link_ops->kind, "vxlan");
}

static inline bool
__netdev_tx_sent_queue(struct netdev_queue *nd_q, u32 len, bool xmit_more)
{
	netdev_tx_sent_queue(nd_q, len);

	return !xmit_more || netif_xmit_stopped(nd_q);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#undef CONFIG_NFP_APP_ABM_NIC

static inline bool netif_is_geneve(const struct net_device *dev)
{
       return dev->rtnl_link_ops &&
              !strcmp(dev->rtnl_link_ops->kind, "geneve");
}

static inline bool netif_is_gretap(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
	       !strcmp(dev->rtnl_link_ops->kind, "gretap");
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define dma_zalloc_coherent	dma_alloc_coherent
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
#define BPF_JMP32	0x06

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
static inline struct nfp_net *compat__bpf_prog_get_nn(struct bpf_prog *prog)
{
	return netdev_priv(prog->aux->offload->netdev);
}
#endif

#ifdef COMPAT__HAVE_METADATA_IP_TUNNEL
enum flow_action_id {
	FLOW_ACTION_ACCEPT,
	FLOW_ACTION_DROP,
	FLOW_ACTION_TRAP,
	FLOW_ACTION_GOTO,
	FLOW_ACTION_REDIRECT,
	FLOW_ACTION_MIRRED,
	FLOW_ACTION_VLAN_PUSH,
	FLOW_ACTION_VLAN_POP,
	FLOW_ACTION_VLAN_MANGLE,
	FLOW_ACTION_TUNNEL_ENCAP,
	FLOW_ACTION_TUNNEL_DECAP,
	FLOW_ACTION_MANGLE,
	FLOW_ACTION_ADD,
	FLOW_ACTION_CSUM,
	FLOW_ACTION_MARK,
	FLOW_ACTION_WAKE,
	FLOW_ACTION_QUEUE,
	FLOW_ACTION_UNKNOWN = 10000,
};

static inline int compat__tca_to_flow_act_id(const struct tc_action *act)
{
	if (is_tcf_gact_shot(act))
		return FLOW_ACTION_DROP;
	if (is_tcf_mirred_egress_redirect(act))
		return FLOW_ACTION_REDIRECT;
	if (is_tcf_mirred_egress_mirror(act))
		return FLOW_ACTION_MIRRED;
	if (is_tcf_vlan(act) && tcf_vlan_action(act) == TCA_VLAN_ACT_POP)
		return FLOW_ACTION_VLAN_POP;
	if (is_tcf_vlan(act) && tcf_vlan_action(act) == TCA_VLAN_ACT_PUSH)
		return FLOW_ACTION_VLAN_PUSH;
	if (is_tcf_tunnel_set(act))
		return FLOW_ACTION_TUNNEL_ENCAP;
	if (is_tcf_tunnel_release(act))
		return FLOW_ACTION_TUNNEL_DECAP;
	if (is_tcf_pedit(act))
		return FLOW_ACTION_MANGLE;
	if (is_tcf_csum(act))
		return FLOW_ACTION_CSUM;
	return FLOW_ACTION_UNKNOWN;
}

static inline void *compat__tca_tun_info(const struct tc_action *act)
{
	return tcf_tunnel_info(act);
}

static inline u32 compat__tca_csum_update_flags(const struct tc_action *act)
{
	return tcf_csum_update_flags(act);
}

static inline __be16 compat__tca_vlan_push_proto(const struct tc_action *act)
{
	return tcf_vlan_push_proto(act);
}

static inline u8 compat__tca_vlan_push_prio(const struct tc_action *act)
{
	return tcf_vlan_push_prio(act);
}

static inline u16 compat__tca_vlan_push_vid(const struct tc_action *act)
{
	return tcf_vlan_push_vid(act);
}

static inline struct net_device *
compat__tca_mirred_dev(const struct tc_action *act)
{
	return tcf_mirred_dev(act);
}

static inline int compat__tca_pedit_nkeys(const struct tc_action *act)
{
	return tcf_pedit_nkeys(act);
}

static inline u32 compat__tca_pedit_val(const struct tc_action *act, int idx)
{
	return tcf_pedit_val(act, idx);
}

static inline u32 compat__tca_pedit_mask(const struct tc_action *act, int idx)
{
	return tcf_pedit_mask(act, idx);
}

static inline u32 compat__tca_pedit_cmd(const struct tc_action *act, int idx)
{
	return tcf_pedit_cmd(act, idx);
}

static inline int compat__tca_pedit_htype(const struct tc_action *act, int idx)
{
	return tcf_pedit_htype(act, idx);
}

static inline u32 compat__tca_pedit_offset(const struct tc_action *act, int idx)
{
	return tcf_pedit_offset(act, idx);
}

static inline bool
netdev_port_same_parent_id(struct net_device *a, struct net_device *b)
{
	return switchdev_port_same_parent_id(a, b);
}
#endif /* COMPAT__HAVE_METADATA_IP_TUNNEL */
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0) */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static inline int
compat__tca_to_flow_act_id(const struct flow_action_entry *act)
{
	return act->id;
}

static inline void *compat__tca_tun_info(const struct flow_action_entry *act)
{
	return (void *)act->tunnel;
}

static inline u32
compat__tca_csum_update_flags(const struct flow_action_entry *act)
{
	return act->csum_flags;
}

static inline __be16
compat__tca_vlan_push_proto(const struct flow_action_entry *act)
{
	return act->vlan.proto;
}

static inline u8
compat__tca_vlan_push_prio(const struct flow_action_entry *act)
{
	return act->vlan.prio;
}

static inline u16
compat__tca_vlan_push_vid(const struct flow_action_entry *act)
{
	return act->vlan.vid;
}

static inline struct net_device *
compat__tca_mirred_dev(const struct flow_action_entry *act)
{
	return act->dev;
}

static inline int compat__tca_pedit_nkeys(const struct flow_action_entry *act)
{
	return 1;
}

static inline u32
compat__tca_pedit_val(const struct flow_action_entry *act, int idx)
{
	return act->mangle.val;
}

static inline u32
compat__tca_pedit_mask(const struct flow_action_entry *act, int idx)
{
	return act->mangle.mask;
}

static inline u32
compat__tca_pedit_cmd(const struct flow_action_entry *act, int idx)
{
	return TCA_PEDIT_KEY_EX_CMD_SET;
}

static inline int
compat__tca_pedit_htype(const struct flow_action_entry *act, int idx)
{
	return act->mangle.htype;
}

static inline u32
compat__tca_pedit_offset(const struct flow_action_entry *act, int idx)
{
	return act->mangle.offset;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
int compat__nfp_net_flash_device(struct net_device *netdev,
				 struct ethtool_flash *flash);
#else
#define compat__nfp_net_flash_device	NULL
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static inline struct flow_rule *
compat__flow_cls_offload_flow_rule(compat__flow_cls_offload *flow)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
	return tc_cls_flower_offload_flow_rule(flow);
#else
	return flow_cls_offload_flow_rule(flow);
#endif
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
int compat__flow_block_cb_setup_simple(struct tc_block_offload *f,
				       struct list_head *driver_list,
				       tc_setup_cb_t *nfp_cb, void *cb_ident,
				       void *cb_priv, bool ingress_only);
#else
static inline int
compat__flow_block_cb_setup_simple(struct flow_block_offload *f,
				   struct list_head *driver_list,
				   flow_setup_cb_t *nfp_cb, void *cb_ident,
				   void *cb_priv, bool ingress_only)
{
	return flow_block_cb_setup_simple(f, driver_list, nfp_cb, cb_ident,
					  cb_priv, ingress_only);
}
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 2, 0)
static inline void nfp_flower_qos_init(struct nfp_app *app)
{
}

static inline void nfp_flower_qos_cleanup(struct nfp_app *app)
{
}

static inline int
nfp_flower_setup_qos_offload(struct nfp_app *app, struct net_device *netdev,
			     void *flow)
{
	return -EOPNOTSUPP;
}

static inline void
nfp_flower_stats_rlim_reply(struct nfp_app *app, struct sk_buff *skb)
{
}
#endif
#endif /* _NFP_NET_COMPAT_H_ */
