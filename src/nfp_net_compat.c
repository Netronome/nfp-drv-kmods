/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2019 Netronome Systems, Inc. */

#include "nfp_net_compat.h"

#include <linux/ethtool.h>

#include "nfp_app.h"
#include "nfp_main.h"

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
