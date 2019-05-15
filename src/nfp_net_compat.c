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
