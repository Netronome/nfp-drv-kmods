/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include "nfp_net_compat.h"

#include <linux/ethtool.h>
#include <linux/lockdep.h>
#if COMPAT__HAVE_SWITCHDEV_ATTRS
#include <net/switchdev.h>
#endif

#if VER_NON_RHEL_GE(5, 0) || VER_RHEL_GE(8, 0)
#include "flower/main.h"
#endif

#include "nfpcore/nfp_cpp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_port.h"
#include "crypto/crypto.h"
#include <net/xfrm.h>

#ifndef CONFIG_NFP_NET_PF
int nfp_net_pci_probe(struct nfp_pf *pf)
{
	return -ENODEV;
}

void nfp_net_pci_remove(struct nfp_pf *pf)
{
}

int nfp_net_refresh_eth_port(struct nfp_port *port)
{
	return -ENODEV;
}

void nfp_net_refresh_port_table(struct nfp_port *port)
{
}
#endif

#ifndef COMPAT__HAVE_METADATA_IP_TUNNEL
void nfp_repr_inc_rx_stats(struct net_device *netdev, unsigned int len)
{
}

void nfp_repr_transfer_features(struct net_device *netdev,
				struct net_device *lower)
{
}

int nfp_reprs_resync_phys_ports(struct nfp_app *app)
{
	return 0;
}
#endif

#if VER_NON_RHEL_LT(5, 1) || VER_RHEL_LT(8, 5)
int compat__nfp_net_flash_device(struct net_device *netdev,
				 struct ethtool_flash *flash)
{
	struct nfp_app *app;
	int ret;

	if (flash->region != ETHTOOL_FLASH_ALL_REGIONS)
		return -EOPNOTSUPP;

	app = nfp_app_from_netdev(netdev);
	if (!app)
		return -EOPNOTSUPP;

	dev_hold(netdev);
	rtnl_unlock();
	ret = nfp_flash_update_common(app->pf, flash->data, NULL);
	rtnl_lock();
	dev_put(netdev);

	return ret;
}
#endif

#if (VER_NON_RHEL_GE(4, 18) && VER_NON_RHEL_LT(5, 3)) || \
	(VER_RHEL_LT(8, 2) && VER_RHEL_GE(8, 0))
int compat__flow_block_cb_setup_simple(struct tc_block_offload *f,
				       struct list_head *driver_list,
				       tc_setup_cb_t *nfp_cb, void *cb_ident,
				       void *cb_priv, bool ingress_only)
{
	if (ingress_only &&
	    f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	if (!ingress_only &&
	    f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		return -EOPNOTSUPP;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
	if (ingress_only && tcf_block_shared(f->block))
		return -EOPNOTSUPP;
#endif

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block, nfp_cb, cb_ident,
					     cb_priv, f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, nfp_cb, cb_ident);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}
#endif

#if !COMPAT__HAS_DEVLINK
int nfp_devlink_port_register(struct nfp_app *app, struct nfp_port *port)
{
	return 0;
}

void nfp_devlink_port_unregister(struct nfp_port *port)
{
}

void nfp_devlink_port_type_eth_set(struct nfp_port *port)
{
}

void nfp_devlink_port_type_clear(struct nfp_port *port)
{
}
#endif

#if VER_NON_RHEL_LT(5, 1) || VER_RHEL_LT(8, 2)
static int
nfp_port_attr_get(struct net_device *netdev, struct switchdev_attr *attr)
{
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID: {
		const u8 *serial;
		/* N.B: attr->u.ppid.id is binary data */
		attr->u.ppid.id_len = nfp_cpp_serial(port->app->cpp, &serial);
		memcpy(&attr->u.ppid.id, serial, attr->u.ppid.id_len);
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

const struct switchdev_ops nfp_port_switchdev_ops = {
	.switchdev_port_attr_get	= nfp_port_attr_get,
};
#endif

#if !COMPAT__HAS_DEVLINK_SB
int nfp_shared_buf_register(struct nfp_pf *pf)
{
	return 0;
}

void nfp_shared_buf_unregister(struct nfp_pf *pf)
{
}
#endif

#if VER_NON_RHEL_OR_SLEL_LT(5, 4) || VER_RHEL_LT(8, 3) || \
    VER_SLEL_LT(5, 3, 18)
int nfp_devlink_params_register(struct nfp_pf *pf)
{
	return 0;
}

void nfp_devlink_params_unregister(struct nfp_pf *pf)
{
}
#endif

#ifdef CONFIG_NFP_APP_FLOWER
#if VER_NON_RHEL_GE(5, 0) || VER_RHEL_GE(8, 1)
#if VER_NON_RHEL_LT(5, 8) || VER_NON_BCL_LT(8, 4) || \
    (VER_BCL_LT(8, 3) || COMPAT_BC82_KERN_11)
int compat__nfp_flower_indr_setup_tc_cb(struct net_device *netdev,
					void *cb_priv, enum tc_setup_type type,
					void *type_data)
{
	return nfp_flower_indr_setup_tc_cb(netdev, NULL, cb_priv, type,
					   type_data, NULL, NULL);
}
#elif VER_NON_RHEL_LT(5, 9) || VER_BCL_LT(8, 4)
int compat__nfp_flower_indr_setup_tc_cb(struct net_device *netdev,
					void *cb_priv, enum tc_setup_type type,
					void *type_data, void *data,
					void (*cleanup)(struct flow_block_cb *block_cb))
{
	return nfp_flower_indr_setup_tc_cb(netdev, NULL, cb_priv, type,
					   type_data, data, cleanup);
}
#endif
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 5)
void flow_rule_match_cvlan(const struct flow_rule *rule,
			   struct flow_match_vlan *out)
{
	const struct flow_match *match = &rule->match;
	struct flow_dissector *d = match->dissector;

	out->key = skb_flow_dissector_target(d, FLOW_DISSECTOR_KEY_CVLAN, match->key);
	out->mask = skb_flow_dissector_target(d, FLOW_DISSECTOR_KEY_CVLAN, match->mask);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
void devl_assert_locked(struct devlink *devlink)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 8, 0) || defined(CONFIG_LOCKDEP)
	struct nfp_pf *pf = devlink_priv(devlink);

	lockdep_assert_held(&pf->lock);
#endif
}

#ifdef CONFIG_LOCKDEP
/* For use in conjunction with LOCKDEP only e.g. rcu_dereference_protected() */
bool devl_lock_is_held(struct devlink *devlink)
{
	struct nfp_pf *pf = devlink_priv(devlink);

	return lockdep_is_held(&pf->lock);
}
#endif
#endif

#if VER_NON_RHEL_LT(5, 19) || \
		(RHEL_RELEASE_GE(9, 0, 0, 0) && RHEL_RELEASE_LT(9, 191, 0, 0)) || \
		RHEL_RELEASE_LT(8, 448, 0, 0)
void netif_inherit_tso_max(struct net_device *to, const struct net_device *from)
{
	netif_set_gso_max_size(to, from->gso_max_size);
	netif_set_gso_max_segs(to, from->gso_max_segs);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
__printf(2, 3) void ethtool_sprintf(u8 **data, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(*data, ETH_GSTRING_LEN, fmt, args);
	va_end(args);

	*data += ETH_GSTRING_LEN;
}
#endif
#endif

#ifdef COMPAT_HAVE_DIM
/* kernel commit: 61bf0009a765 ("dim: pass dim_sample to net_dim() by reference")
 * converted the 'net_dim' function to take in end_sample by reference. Add the
 * applicable compat function code. Use this compat__ function instead of doing
 * this everywhere in the rest of the source code.
 */
void compat__net_dim(struct dim *dim, const struct dim_sample *end_sample)
{
#ifdef COMPAT_DIM_PASS_BY_REF
	net_dim(dim, end_sample);
#else
	net_dim(dim, *end_sample);
#endif
}
#endif

#ifdef CONFIG_NFP_NET_IPSEC
#if VER_NON_RHEL_LT(6, 3) || RHEL_RELEASE_LT(9, 316, 0, 0)
#undef NL_SET_ERR_MSG_MOD
#define NL_SET_ERR_MSG_MOD(e, m)        nn_err(nn, "%s\n", m)
int compat__nfp_net_xfrm_add_state(struct xfrm_state *x)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	struct net_device *dev = x->xso.real_dev;
#else
	struct net_device *dev = x->xso.dev;
#endif

	return nfp_net_xfrm_add_state(dev, x, NULL);
}
#elif VER_NON_RHEL_LT(6, 16) || RHEL_RELEASE_GE(9, 316, 0, 0)
int compat__nfp_net_xfrm_add_state(struct xfrm_state *x,
				  struct netlink_ext_ack *extack)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	struct net_device *dev = x->xso.real_dev;
#else
	struct net_device *dev = x->xso.dev;
#endif

	return nfp_net_xfrm_add_state(dev, x, extack);
}
#else
int compat__nfp_net_xfrm_add_state(struct net_device *dev,
				  struct xfrm_state *x,
				  struct netlink_ext_ack *extack)
{
	return nfp_net_xfrm_add_state(dev, x, extack);
}
#endif // CONFIG_NFP_NET_IPSEC

#endif

