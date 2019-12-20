// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <net/pkt_cls.h>

#include "cmsg.h"
#include "main.h"

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_meta_tci(struct nfp_flower_meta_tci *ext,
			    struct nfp_flower_meta_tci *msk,
			    struct flow_rule *rule, u8 key_type)
{
	u16 tmp_tci;

	memset(ext, 0, sizeof(struct nfp_flower_meta_tci));
	memset(msk, 0, sizeof(struct nfp_flower_meta_tci));

	/* Populate the metadata frame. */
	ext->nfp_flow_key_layer = key_type;
	ext->mask_id = ~0;

	msk->nfp_flow_key_layer = key_type;
	msk->mask_id = ~0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		/* Populate the tci field. */
		tmp_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		tmp_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.key->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.key->vlan_id);
		ext->tci = cpu_to_be16(tmp_tci);

		tmp_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		tmp_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      match.mask->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      match.mask->vlan_id);
		msk->tci = cpu_to_be16(tmp_tci);
	}
}
#else
nfp_flower_compile_meta_tci(struct nfp_flower_meta_tci *frame,
			    compat__flow_cls_offload *flow, u8 key_type,
			    bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_vlan *flow_vlan;
	u16 tmp_tci;

	memset(frame, 0, sizeof(struct nfp_flower_meta_tci));
	/* Populate the metadata frame. */
	frame->nfp_flow_key_layer = key_type;
	frame->mask_id = ~0;

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_VLAN)) {
		flow_vlan = skb_flow_dissector_target(flow->dissector,
						      FLOW_DISSECTOR_KEY_VLAN,
						      target);
		tmp_tci = NFP_FLOWER_MASK_VLAN_PRESENT;
		/* Populate the tci field. */
		tmp_tci |= FIELD_PREP(NFP_FLOWER_MASK_VLAN_PRIO,
				      flow_vlan->vlan_priority) |
			   FIELD_PREP(NFP_FLOWER_MASK_VLAN_VID,
				      flow_vlan->vlan_id);
		frame->tci = cpu_to_be16(tmp_tci);
	}
}
#endif

static void
nfp_flower_compile_ext_meta(struct nfp_flower_ext_meta *frame, u32 key_ext)
{
	frame->nfp_flow_key_layer2 = cpu_to_be32(key_ext);
}

static int
nfp_flower_compile_port(struct nfp_flower_in_port *frame, u32 cmsg_port,
			bool mask_version, enum nfp_flower_tun_type tun_type,
			struct netlink_ext_ack *extack)
{
	if (mask_version) {
		frame->in_port = cpu_to_be32(~0);
		return 0;
	}

	if (tun_type) {
		frame->in_port = cpu_to_be32(NFP_FL_PORT_TYPE_TUN | tun_type);
	} else {
		if (!cmsg_port) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported offload: invalid ingress interface for match offload");
			return -EOPNOTSUPP;
		}
		frame->in_port = cpu_to_be32(cmsg_port);
	}

	return 0;
}

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *ext,
		       struct nfp_flower_mac_mpls *msk, struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_mac_mpls));
	memset(msk, 0, sizeof(struct nfp_flower_mac_mpls));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		/* Populate mac frame. */
		ether_addr_copy(ext->mac_dst, &match.key->dst[0]);
		ether_addr_copy(ext->mac_src, &match.key->src[0]);
		ether_addr_copy(msk->mac_dst, &match.mask->dst[0]);
		ether_addr_copy(msk->mac_src, &match.mask->src[0]);
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_match_mpls match;
		u32 t_mpls;

		flow_rule_match_mpls(rule, &match);
		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, match.key->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, match.key->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, match.key->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;
		ext->mpls_lse = cpu_to_be32(t_mpls);
		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, match.mask->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, match.mask->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, match.mask->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;
		msk->mpls_lse = cpu_to_be32(t_mpls);
	} else if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		/* Check for mpls ether type and set NFP_FLOWER_MASK_MPLS_Q
		 * bit, which indicates an mpls ether type but without any
		 * mpls fields.
		 */
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		if (match.key->n_proto == cpu_to_be16(ETH_P_MPLS_UC) ||
		    match.key->n_proto == cpu_to_be16(ETH_P_MPLS_MC)) {
			ext->mpls_lse = cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
			msk->mpls_lse = cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
		}
	}
}
#else
nfp_flower_compile_mac(struct nfp_flower_mac_mpls *frame,
		       compat__flow_cls_offload *flow,
		       bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_eth_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_mac_mpls));

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_ETH_ADDRS,
						 target);
		/* Populate mac frame. */
		ether_addr_copy(frame->mac_dst, &addr->dst[0]);
		ether_addr_copy(frame->mac_src, &addr->src[0]);
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_MPLS)) {
		struct flow_dissector_key_mpls *mpls;
		u32 t_mpls;

		mpls = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_MPLS,
						 target);

		t_mpls = FIELD_PREP(NFP_FLOWER_MASK_MPLS_LB, mpls->mpls_label) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_TC, mpls->mpls_tc) |
			 FIELD_PREP(NFP_FLOWER_MASK_MPLS_BOS, mpls->mpls_bos) |
			 NFP_FLOWER_MASK_MPLS_Q;

		frame->mpls_lse = cpu_to_be32(t_mpls);
	} else if (dissector_uses_key(flow->dissector,
				      FLOW_DISSECTOR_KEY_BASIC)) {
		/* Check for mpls ether type and set NFP_FLOWER_MASK_MPLS_Q
		 * bit, which indicates an mpls ether type but without any
		 * mpls fields.
		 */
		struct flow_dissector_key_basic *key_basic;

		key_basic = skb_flow_dissector_target(flow->dissector,
						      FLOW_DISSECTOR_KEY_BASIC,
						      flow->key);
		if (key_basic->n_proto == cpu_to_be16(ETH_P_MPLS_UC) ||
		    key_basic->n_proto == cpu_to_be16(ETH_P_MPLS_MC))
			frame->mpls_lse = cpu_to_be32(NFP_FLOWER_MASK_MPLS_Q);
	}
}
#endif

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_tport(struct nfp_flower_tp_ports *ext,
			 struct nfp_flower_tp_ports *msk,
			 struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_tp_ports));
	memset(msk, 0, sizeof(struct nfp_flower_tp_ports));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		ext->port_src = match.key->src;
		ext->port_dst = match.key->dst;
		msk->port_src = match.mask->src;
		msk->port_dst = match.mask->dst;
	}
}
#else
nfp_flower_compile_tport(struct nfp_flower_tp_ports *frame,
			 struct tc_cls_flower_offload *flow,
			 bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ports *tp;

	memset(frame, 0, sizeof(struct nfp_flower_tp_ports));

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_PORTS)) {
		tp = skb_flow_dissector_target(flow->dissector,
					       FLOW_DISSECTOR_KEY_PORTS,
					       target);
		frame->port_src = tp->src;
		frame->port_dst = tp->dst;
	}
}
#endif

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_ip_ext(struct nfp_flower_ip_ext *ext,
			  struct nfp_flower_ip_ext *msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);
		ext->proto = match.key->ip_proto;
		msk->proto = match.mask->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(rule, &match);
		ext->tos = match.key->tos;
		ext->ttl = match.key->ttl;
		msk->tos = match.mask->tos;
		msk->ttl = match.mask->ttl;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_TCP)) {
		u16 tcp_flags, tcp_flags_mask;
		struct flow_match_tcp match;

		flow_rule_match_tcp(rule, &match);
		tcp_flags = be16_to_cpu(match.key->flags);
		tcp_flags_mask = be16_to_cpu(match.mask->flags);

		if (tcp_flags & TCPHDR_FIN)
			ext->flags |= NFP_FL_TCP_FLAG_FIN;
		if (tcp_flags_mask & TCPHDR_FIN)
			msk->flags |= NFP_FL_TCP_FLAG_FIN;

		if (tcp_flags & TCPHDR_SYN)
			ext->flags |= NFP_FL_TCP_FLAG_SYN;
		if (tcp_flags_mask & TCPHDR_SYN)
			msk->flags |= NFP_FL_TCP_FLAG_SYN;

		if (tcp_flags & TCPHDR_RST)
			ext->flags |= NFP_FL_TCP_FLAG_RST;
		if (tcp_flags_mask & TCPHDR_RST)
			msk->flags |= NFP_FL_TCP_FLAG_RST;

		if (tcp_flags & TCPHDR_PSH)
			ext->flags |= NFP_FL_TCP_FLAG_PSH;
		if (tcp_flags_mask & TCPHDR_PSH)
			msk->flags |= NFP_FL_TCP_FLAG_PSH;

		if (tcp_flags & TCPHDR_URG)
			ext->flags |= NFP_FL_TCP_FLAG_URG;
		if (tcp_flags_mask & TCPHDR_URG)
			msk->flags |= NFP_FL_TCP_FLAG_URG;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);
		if (match.key->flags & FLOW_DIS_IS_FRAGMENT)
			ext->flags |= NFP_FL_IP_FRAGMENTED;
		if (match.mask->flags & FLOW_DIS_IS_FRAGMENT)
			msk->flags |= NFP_FL_IP_FRAGMENTED;
		if (match.key->flags & FLOW_DIS_FIRST_FRAG)
			ext->flags |= NFP_FL_IP_FRAG_FIRST;
		if (match.mask->flags & FLOW_DIS_FIRST_FRAG)
			msk->flags |= NFP_FL_IP_FRAG_FIRST;
	}
}
#else
nfp_flower_compile_ip_ext(struct nfp_flower_ip_ext *frame,
			  struct tc_cls_flower_offload *flow,
			  bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_dissector_key_basic *basic;

		basic = skb_flow_dissector_target(flow->dissector,
						  FLOW_DISSECTOR_KEY_BASIC,
						  target);
		frame->proto = basic->ip_proto;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_dissector_key_ip *flow_ip;

		flow_ip = skb_flow_dissector_target(flow->dissector,
						    FLOW_DISSECTOR_KEY_IP,
						    target);
		frame->tos = flow_ip->tos;
		frame->ttl = flow_ip->ttl;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_TCP)) {
		struct flow_dissector_key_tcp *tcp;
		u32 tcp_flags;

		tcp = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_TCP, target);
		tcp_flags = be16_to_cpu(tcp->flags);

		if (tcp_flags & TCPHDR_FIN)
			frame->flags |= NFP_FL_TCP_FLAG_FIN;
		if (tcp_flags & TCPHDR_SYN)
			frame->flags |= NFP_FL_TCP_FLAG_SYN;
		if (tcp_flags & TCPHDR_RST)
			frame->flags |= NFP_FL_TCP_FLAG_RST;
		if (tcp_flags & TCPHDR_PSH)
			frame->flags |= NFP_FL_TCP_FLAG_PSH;
		if (tcp_flags & TCPHDR_URG)
			frame->flags |= NFP_FL_TCP_FLAG_URG;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_dissector_key_control *key;

		key = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_CONTROL,
						target);
		if (key->flags & FLOW_DIS_IS_FRAGMENT)
			frame->flags |= NFP_FL_IP_FRAGMENTED;
		if (key->flags & FLOW_DIS_FIRST_FRAG)
			frame->flags |= NFP_FL_IP_FRAG_FIRST;
	}
}
#endif

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *ext,
			struct nfp_flower_ipv4 *msk, struct flow_rule *rule)
{
	struct flow_match_ipv4_addrs match;

	memset(ext, 0, sizeof(struct nfp_flower_ipv4));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		flow_rule_match_ipv4_addrs(rule, &match);
		ext->ipv4_src = match.key->src;
		ext->ipv4_dst = match.key->dst;
		msk->ipv4_src = match.mask->src;
		msk->ipv4_dst = match.mask->dst;
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
}
#else
nfp_flower_compile_ipv4(struct nfp_flower_ipv4 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_ipv4));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV4_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV4_ADDRS,
						 target);
		frame->ipv4_src = addr->src;
		frame->ipv4_dst = addr->dst;
	}

	nfp_flower_compile_ip_ext(&frame->ip_ext, flow, mask_version);
}
#endif

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *ext,
			struct nfp_flower_ipv6 *msk, struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_ipv6));
	memset(msk, 0, sizeof(struct nfp_flower_ipv6));

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		ext->ipv6_src = match.key->src;
		ext->ipv6_dst = match.key->dst;
		msk->ipv6_src = match.mask->src;
		msk->ipv6_dst = match.mask->dst;
	}

	nfp_flower_compile_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
}
#else
nfp_flower_compile_ipv6(struct nfp_flower_ipv6 *frame,
			struct tc_cls_flower_offload *flow,
			bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv6_addrs *addr;

	memset(frame, 0, sizeof(struct nfp_flower_ipv6));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_IPV6_ADDRS)) {
		addr = skb_flow_dissector_target(flow->dissector,
						 FLOW_DISSECTOR_KEY_IPV6_ADDRS,
						 target);
		frame->ipv6_src = addr->src;
		frame->ipv6_dst = addr->dst;
	}

	nfp_flower_compile_ip_ext(&frame->ip_ext, flow, mask_version);
}
#endif

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_geneve_opt(void *ext, void *msk, struct flow_rule *rule)
{
	struct flow_match_enc_opts match;

	flow_rule_match_enc_opts(rule, &match);
	memcpy(ext, match.key->data, match.key->len);
	memcpy(msk, match.mask->data, match.mask->len);

	return 0;
}
#else
nfp_flower_compile_geneve_opt(void *key_buf, struct tc_cls_flower_offload *flow,
			      bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_enc_opts *opts;

	opts = skb_flow_dissector_target(flow->dissector,
					 FLOW_DISSECTOR_KEY_ENC_OPTS,
					 target);
	memcpy(key_buf, opts->data, opts->len);

	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static void
nfp_flower_compile_tun_ipv4_addrs(struct nfp_flower_tun_ipv4 *ext,
				  struct nfp_flower_tun_ipv4 *msk,
				  struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_enc_ipv4_addrs(rule, &match);
		ext->src = match.key->src;
		ext->dst = match.key->dst;
		msk->src = match.mask->src;
		msk->dst = match.mask->dst;
	}
}

static void
nfp_flower_compile_tun_ipv6_addrs(struct nfp_flower_tun_ipv6 *ext,
				  struct nfp_flower_tun_ipv6 *msk,
				  struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_enc_ipv6_addrs(rule, &match);
		ext->src = match.key->src;
		ext->dst = match.key->dst;
		msk->src = match.mask->src;
		msk->dst = match.mask->dst;
	}
}

static void
nfp_flower_compile_tun_ip_ext(struct nfp_flower_tun_ip_ext *ext,
			      struct nfp_flower_tun_ip_ext *msk,
			      struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match;

		flow_rule_match_enc_ip(rule, &match);
		ext->tos = match.key->tos;
		ext->ttl = match.key->ttl;
		msk->tos = match.mask->tos;
		msk->ttl = match.mask->ttl;
	}
}

static void
nfp_flower_compile_tun_udp_key(__be32 *key, __be32 *key_msk,
			       struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;
		u32 vni;

		flow_rule_match_enc_keyid(rule, &match);
		vni = be32_to_cpu(match.key->keyid) << NFP_FL_TUN_VNI_OFFSET;
		*key = cpu_to_be32(vni);
		vni = be32_to_cpu(match.mask->keyid) << NFP_FL_TUN_VNI_OFFSET;
		*key_msk = cpu_to_be32(vni);
	}
}

static void
nfp_flower_compile_tun_gre_key(__be32 *key, __be32 *key_msk, __be16 *flags,
			       __be16 *flags_msk, struct flow_rule *rule)
{
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(rule, &match);
		*key = match.key->keyid;
		*key_msk = match.mask->keyid;

		*flags = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
		*flags_msk = cpu_to_be16(NFP_FL_GRE_FLAG_KEY);
	}
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static void
nfp_flower_compile_ipv4_gre_tun(struct nfp_flower_ipv4_gre_tun *ext,
				struct nfp_flower_ipv4_gre_tun *msk,
				struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_ipv4_gre_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4_gre_tun));

	/* NVGRE is the only supported GRE tunnel type */
	ext->ethertype = cpu_to_be16(ETH_P_TEB);
	msk->ethertype = cpu_to_be16(~0);

	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_gre_key(&ext->tun_key, &msk->tun_key,
				       &ext->tun_flags, &msk->tun_flags, rule);
}
#endif

static void
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *ext,
				struct nfp_flower_ipv4_udp_tun *msk,
				struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_ipv4_udp_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv4_udp_tun));

	nfp_flower_compile_tun_ipv4_addrs(&ext->ipv4, &msk->ipv4, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_udp_key(&ext->tun_id, &msk->tun_id, rule);
}
#else
nfp_flower_compile_ipv4_udp_tun(struct nfp_flower_ipv4_udp_tun *frame,
				struct tc_cls_flower_offload *flow,
				bool mask_version)
{
	struct fl_flow_key *target = mask_version ? flow->mask : flow->key;
	struct flow_dissector_key_ipv4_addrs *tun_ips;
	struct flow_dissector_key_keyid *vni;
	struct flow_dissector_key_ip *ip;

	memset(frame, 0, sizeof(struct nfp_flower_ipv4_udp_tun));

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		u32 temp_vni;

		vni = skb_flow_dissector_target(flow->dissector,
						FLOW_DISSECTOR_KEY_ENC_KEYID,
						target);
		temp_vni = be32_to_cpu(vni->keyid) << NFP_FL_TUN_VNI_OFFSET;
		frame->tun_id = cpu_to_be32(temp_vni);
	}

	if (dissector_uses_key(flow->dissector,
			       FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)) {
		tun_ips =
		   skb_flow_dissector_target(flow->dissector,
					     FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS,
					     target);
		frame->ipv4.src = tun_ips->src;
		frame->ipv4.dst = tun_ips->dst;
	}

	if (dissector_uses_key(flow->dissector, FLOW_DISSECTOR_KEY_ENC_IP)) {
		ip = skb_flow_dissector_target(flow->dissector,
					       FLOW_DISSECTOR_KEY_ENC_IP,
					       target);
		frame->ip_ext.tos = ip->tos;
		frame->ip_ext.ttl = ip->ttl;
	}
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static void
nfp_flower_compile_ipv6_udp_tun(struct nfp_flower_ipv6_udp_tun *ext,
				struct nfp_flower_ipv6_udp_tun *msk,
				struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_ipv6_udp_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv6_udp_tun));

	nfp_flower_compile_tun_ipv6_addrs(&ext->ipv6, &msk->ipv6, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_udp_key(&ext->tun_id, &msk->tun_id, rule);
}

static void
nfp_flower_compile_ipv6_gre_tun(struct nfp_flower_ipv6_gre_tun *ext,
				struct nfp_flower_ipv6_gre_tun *msk,
				struct flow_rule *rule)
{
	memset(ext, 0, sizeof(struct nfp_flower_ipv6_gre_tun));
	memset(msk, 0, sizeof(struct nfp_flower_ipv6_gre_tun));

	/* NVGRE is the only supported GRE tunnel type */
	ext->ethertype = cpu_to_be16(ETH_P_TEB);
	msk->ethertype = cpu_to_be16(~0);

	nfp_flower_compile_tun_ipv6_addrs(&ext->ipv6, &msk->ipv6, rule);
	nfp_flower_compile_tun_ip_ext(&ext->ip_ext, &msk->ip_ext, rule);
	nfp_flower_compile_tun_gre_key(&ext->tun_key, &msk->tun_key,
				       &ext->tun_flags, &msk->tun_flags, rule);
}
#endif

int nfp_flower_compile_flow_match(struct nfp_app *app,
				  compat__flow_cls_offload *flow,
				  struct nfp_fl_key_ls *key_ls,
				  struct net_device *netdev,
				  struct nfp_fl_payload *nfp_flow,
				  enum nfp_flower_tun_type tun_type,
				  struct netlink_ext_ack *extack)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	struct flow_rule *rule = compat__flow_cls_offload_flow_rule(flow);
#endif
	u32 port_id;
	int err;
	u8 *ext;
	u8 *msk;

	port_id = nfp_flower_get_port_id_from_netdev(app, netdev);

	memset(nfp_flow->unmasked_data, 0, key_ls->key_size);
	memset(nfp_flow->mask_data, 0, key_ls->key_size);

	ext = nfp_flow->unmasked_data;
	msk = nfp_flow->mask_data;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)ext,
				    (struct nfp_flower_meta_tci *)msk,
				    rule, key_ls->key_layer);
#else
	/* Populate Exact Metadata. */
	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)ext,
				    flow, key_ls->key_layer, false);
	/* Populate Mask Metadata. */
	nfp_flower_compile_meta_tci((struct nfp_flower_meta_tci *)msk,
				    flow, key_ls->key_layer, true);
#endif
	ext += sizeof(struct nfp_flower_meta_tci);
	msk += sizeof(struct nfp_flower_meta_tci);

	/* Populate Extended Metadata if Required. */
	if (NFP_FLOWER_LAYER_EXT_META & key_ls->key_layer) {
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)ext,
					    key_ls->key_layer_two);
		nfp_flower_compile_ext_meta((struct nfp_flower_ext_meta *)msk,
					    key_ls->key_layer_two);
		ext += sizeof(struct nfp_flower_ext_meta);
		msk += sizeof(struct nfp_flower_ext_meta);
	}

	/* Populate Exact Port data. */
	err = nfp_flower_compile_port((struct nfp_flower_in_port *)ext,
				      port_id, false, tun_type, extack);
	if (err)
		return err;

	/* Populate Mask Port Data. */
	err = nfp_flower_compile_port((struct nfp_flower_in_port *)msk,
				      port_id, true, tun_type, extack);
	if (err)
		return err;

	ext += sizeof(struct nfp_flower_in_port);
	msk += sizeof(struct nfp_flower_in_port);

	if (NFP_FLOWER_LAYER_MAC & key_ls->key_layer) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)ext,
				       (struct nfp_flower_mac_mpls *)msk,
				       rule);
#else
		/* Populate Exact MAC Data. */
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)ext,
				       flow, false);
		/* Populate Mask MAC Data. */
		nfp_flower_compile_mac((struct nfp_flower_mac_mpls *)msk,
				       flow, true);
#endif
		ext += sizeof(struct nfp_flower_mac_mpls);
		msk += sizeof(struct nfp_flower_mac_mpls);
	}

	if (NFP_FLOWER_LAYER_TP & key_ls->key_layer) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)ext,
					 (struct nfp_flower_tp_ports *)msk,
					 rule);
#else
		/* Populate Exact TP Data. */
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)ext,
					 flow, false);
		/* Populate Mask TP Data. */
		nfp_flower_compile_tport((struct nfp_flower_tp_ports *)msk,
					 flow, true);
#endif
		ext += sizeof(struct nfp_flower_tp_ports);
		msk += sizeof(struct nfp_flower_tp_ports);
	}

	if (NFP_FLOWER_LAYER_IPV4 & key_ls->key_layer) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)ext,
					(struct nfp_flower_ipv4 *)msk,
					rule);
#else
		/* Populate Exact IPv4 Data. */
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)ext,
					flow, false);
		/* Populate Mask IPv4 Data. */
		nfp_flower_compile_ipv4((struct nfp_flower_ipv4 *)msk,
					flow, true);
#endif
		ext += sizeof(struct nfp_flower_ipv4);
		msk += sizeof(struct nfp_flower_ipv4);
	}

	if (NFP_FLOWER_LAYER_IPV6 & key_ls->key_layer) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)ext,
					(struct nfp_flower_ipv6 *)msk,
					rule);
#else
		/* Populate Exact IPv4 Data. */
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)ext,
					flow, false);
		/* Populate Mask IPv4 Data. */
		nfp_flower_compile_ipv6((struct nfp_flower_ipv6 *)msk,
					flow, true);
#endif
		ext += sizeof(struct nfp_flower_ipv6);
		msk += sizeof(struct nfp_flower_ipv6);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GRE) {
		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_gre_tun *gre_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			nfp_flower_compile_ipv6_gre_tun((void *)ext,
							(void *)msk, rule);
			gre_match = (struct nfp_flower_ipv6_gre_tun *)ext;
			dst = &gre_match->ipv6.dst;
			ext += sizeof(struct nfp_flower_ipv6_gre_tun);
			msk += sizeof(struct nfp_flower_ipv6_gre_tun);

			entry = nfp_tunnel_add_ipv6_off(app, dst);
			if (!entry)
				return -EOPNOTSUPP;

			nfp_flow->nfp_tun_ipv6 = entry;
		} else {
			__be32 dst;

			nfp_flower_compile_ipv4_gre_tun((void *)ext,
							(void *)msk, rule);
			dst = ((struct nfp_flower_ipv4_gre_tun *)ext)->ipv4.dst;
			ext += sizeof(struct nfp_flower_ipv4_gre_tun);
			msk += sizeof(struct nfp_flower_ipv4_gre_tun);

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			nfp_flow->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(app, dst);
		}
	}
#endif

	if (key_ls->key_layer & NFP_FLOWER_LAYER_VXLAN ||
	    key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_TUN_IPV6) {
			struct nfp_flower_ipv6_udp_tun *udp_match;
			struct nfp_ipv6_addr_entry *entry;
			struct in6_addr *dst;

			nfp_flower_compile_ipv6_udp_tun((void *)ext,
							(void *)msk, rule);
			udp_match = (struct nfp_flower_ipv6_udp_tun *)ext;
			dst = &udp_match->ipv6.dst;
			ext += sizeof(struct nfp_flower_ipv6_udp_tun);
			msk += sizeof(struct nfp_flower_ipv6_udp_tun);

			entry = nfp_tunnel_add_ipv6_off(app, dst);
			if (!entry)
				return -EOPNOTSUPP;

			nfp_flow->nfp_tun_ipv6 = entry;
		} else {
#endif
			__be32 dst;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
			nfp_flower_compile_ipv4_udp_tun((void *)ext,
							(void *)msk, rule);
#else
			/* Populate Exact VXLAN Data. */
			nfp_flower_compile_ipv4_udp_tun((void *)ext, flow,
							false);
			/* Populate Mask VXLAN Data. */
			nfp_flower_compile_ipv4_udp_tun((void *)msk, flow,
							true);
#endif
			dst = ((struct nfp_flower_ipv4_udp_tun *)ext)->ipv4.dst;
			ext += sizeof(struct nfp_flower_ipv4_udp_tun);
			msk += sizeof(struct nfp_flower_ipv4_udp_tun);

			/* Store the tunnel destination in the rule data.
			 * This must be present and be an exact match.
			 */
			nfp_flow->nfp_tun_ipv4_addr = dst;
			nfp_tunnel_add_ipv4_off(app, dst);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
		}
#endif

		if (key_ls->key_layer_two & NFP_FLOWER_LAYER2_GENEVE_OP) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
			err = nfp_flower_compile_geneve_opt(ext, msk, rule);
#else
			err = nfp_flower_compile_geneve_opt(ext, flow, false);
			if (err)
				return err;

			err = nfp_flower_compile_geneve_opt(msk, flow, true);
#endif
			if (err)
				return err;
		}
	}

	return 0;
}
