// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include "nfp_net_compat.h"

#include <linux/lockdep.h>
#include <linux/netdevice.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"
#include "nfp_port.h"

struct nfp_port *nfp_port_from_netdev(struct net_device *netdev)
{
	if (nfp_netdev_is_nfp_net(netdev)) {
		struct nfp_net *nn = netdev_priv(netdev);

		return nn->port;
	}

	if (nfp_netdev_is_nfp_repr(netdev)) {
		struct nfp_repr *repr = netdev_priv(netdev);

		return repr->port;
	}

	WARN(1, "Unknown netdev type for nfp_port\n");

	return NULL;
}

#if VER_NON_RHEL_GE(5, 1) || VER_RHEL_GE(8, 2)
int nfp_port_get_port_parent_id(struct net_device *netdev,
				struct netdev_phys_item_id *ppid)
{
	struct nfp_port *port;
	const u8 *serial;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	ppid->id_len = nfp_cpp_serial(port->app->cpp, &serial);
	memcpy(&ppid->id, serial, ppid->id_len);

	return 0;
}
#endif

#if VER_NON_RHEL_OR_SLEL_GE(4, 13) || VER_RHEL_GE(7, 5) || \
    SLEL_LOCALVER_GE(4, 12, 14, 95, 60)
#if VER_NON_RHEL_OR_SLEL_LT(4, 14)
int nfp_port_setup_tc(struct net_device *netdev, u32 handle, u32 chain_index,
		      __be16 proto, struct tc_to_netdev *tc)
#else
int nfp_port_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		      void *type_data)
#endif
{
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

#if VER_NON_RHEL_OR_SLEL_LT(4, 14) || SLEL_LOCALVER_LT(4, 12, 14, 95, 60)
	if (TC_H_MAJ(handle) != TC_H_MAJ(TC_H_INGRESS) || chain_index ||
	    (tc->type == TC_SETUP_CLSFLOWER && !eth_proto_is_802_3(proto)))
		return -EOPNOTSUPP;

	return nfp_app_setup_tc(port->app, netdev, tc->type, tc->cls_bpf);
#else
	return nfp_app_setup_tc(port->app, netdev, type, type_data);
#endif
}
#endif

int nfp_port_set_features(struct net_device *netdev, netdev_features_t features)
{
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return 0;

	if ((netdev->features & NETIF_F_HW_TC) > (features & NETIF_F_HW_TC) &&
	    port->tc_offload_cnt) {
		netdev_err(netdev, "Cannot disable HW TC offload while offloads active\n");
		return -EBUSY;
	}

	return 0;
}

#ifndef VERSION__DEVLINK_PORT_SPLIT
struct nfp_port *
nfp_port_from_id(struct nfp_pf *pf, enum nfp_port_type type, unsigned int id)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct nfp_port *port;

	devl_assert_locked(devlink);

	if (type != NFP_PORT_PHYS_PORT)
		return NULL;

	list_for_each_entry(port, &pf->ports, port_list)
		if (port->eth_id == id)
			return port;

	return NULL;
}
#endif

struct nfp_eth_table_port *__nfp_port_get_eth_port(struct nfp_port *port)
{
	if (!port)
		return NULL;
	if (port->type != NFP_PORT_PHYS_PORT)
		return NULL;

	return port->eth_port;
}

struct nfp_eth_table_port *nfp_port_get_eth_port(struct nfp_port *port)
{
	if (!__nfp_port_get_eth_port(port))
		return NULL;

	if (test_bit(NFP_PORT_CHANGED, &port->flags))
		if (nfp_net_refresh_eth_port(port))
			return NULL;

	return __nfp_port_get_eth_port(port);
}

int
nfp_port_get_phys_port_name(struct net_device *netdev, char *name, size_t len)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int n;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	switch (port->type) {
	case NFP_PORT_PHYS_PORT:
		eth_port = __nfp_port_get_eth_port(port);
		if (!eth_port)
			return -EOPNOTSUPP;

		if (!eth_port->is_split)
			n = snprintf(name, len, "p%d", eth_port->label_port);
		else
			n = snprintf(name, len, "p%ds%d", eth_port->label_port,
				     eth_port->label_subport);
		break;
	case NFP_PORT_PF_PORT:
		if (!port->pf_split)
			n = snprintf(name, len, "pf%d", port->pf_id);
		else
			n = snprintf(name, len, "pf%ds%d", port->pf_id,
				     port->pf_split_id);
		break;
	case NFP_PORT_VF_PORT:
		n = snprintf(name, len, "pf%dvf%d", port->pf_id, port->vf_id);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (n >= len)
		return -EINVAL;

	return 0;
}

/**
 * nfp_port_configure() - helper to set the interface configured bit
 * @netdev:	net_device instance
 * @configed:	Desired state
 *
 * Helper to set the ifup/ifdown state on the PHY only if there is a physical
 * interface associated with the netdev.
 *
 * Return:
 * 0 - configuration successful (or no change);
 * -ERRNO - configuration failed.
 */
int nfp_port_configure(struct net_device *netdev, bool configed)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int err;

	port = nfp_port_from_netdev(netdev);
	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return 0;
	if (port->eth_forced)
		return 0;

	err = nfp_eth_set_configured(port->app->cpp, eth_port->index, configed);
	return err < 0 && err != -EOPNOTSUPP ? err : 0;
}

#ifdef CONFIG_NFP_NET_PF
int nfp_port_init_phy_port(struct nfp_pf *pf, struct nfp_app *app,
			   struct nfp_port *port, unsigned int id)
{
	/* Check if vNIC has external port associated and cfg is OK */
	if (!pf->eth_tbl || id >= pf->eth_tbl->count) {
		nfp_err(app->cpp,
			"NSP port entries don't match vNICs (no entry %d)\n",
			id);
		return -EINVAL;
	}
	if (pf->eth_tbl->ports[id].override_changed) {
		nfp_warn(app->cpp,
			 "Config changed for port #%d, reboot required before port will be operational\n",
			 pf->eth_tbl->ports[id].index);
		port->type = NFP_PORT_INVALID;
		return 0;
	}

	port->eth_port = &pf->eth_tbl->ports[id];
	port->eth_id = pf->eth_tbl->ports[id].index;
#if VER_NON_RHEL_GE(3, 15) || VER_RHEL_GE(7, 1)
	port->netdev->dev_port = id;
#endif
	if (pf->mac_stats_mem)
		port->eth_stats =
			pf->mac_stats_mem + port->eth_id * NFP_MAC_STATS_SIZE;

	return 0;
}

struct nfp_port *
nfp_port_alloc(struct nfp_app *app, enum nfp_port_type type,
	       struct net_device *netdev)
{
	struct nfp_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->netdev = netdev;
	port->type = type;
	port->app = app;

	list_add_tail(&port->port_list, &app->pf->ports);

	return port;
}

void nfp_port_free(struct nfp_port *port)
{
	if (!port)
		return;
	list_del(&port->port_list);
	kfree(port);
}
#endif
