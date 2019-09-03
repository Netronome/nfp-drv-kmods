/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include "nfp_net_compat.h"

#include <linux/ethtool.h>
#if COMPAT__HAVE_SWITCHDEV_ATTRS
#include <net/switchdev.h>
#endif

#include "nfpcore/nfp_cpp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_port.h"

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

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0) &&	\
    LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
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
