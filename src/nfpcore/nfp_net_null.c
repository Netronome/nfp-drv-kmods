/*
 * Copyright (C) 2015 Netronome Systems, Inc.
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
 * nfp_net_null
 * Null network device, for use as an ethtool interface
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp_nbi.h"
#include "nfp_nbi_phymod.h"

#include "nfp_net_null.h"

#define TX_TIMEOUT	(2 * HZ)

static const char nfp_net_null_driver_name[] = NFP_NET_NULL_TYPE;
static const char nfp_net_null_driver_version[] = "0.0";

struct nfp_net_null {
	struct device *parent;
	struct nfp_device *nfp;
	int ports;
	struct net_device *port[48];
};

#define ETO_PRIV_FLAG_TX_LINK		BIT(0)
#define ETO_PRIV_FLAG_RX_LINK		BIT(1)
#define ETO_PRIV_FLAG_FAIL_TO_WIRE	BIT(2)

struct nfp_net_null_dev {
	struct ethtool_ops ethtool_ops;
	struct net_device_ops netdev_ops;
	int mac;
	int port;
	int no_stats;
	u32 priv_flags;
	struct nfp_phymod_eth *eth;
	struct nfp_nbi_dev *nbi;
	struct nfp6000_mac_dev_stats {
		u32 cpp_id;
		u64 cpp_addr;
		struct nfp_nbi_mac_portstats cache;
	} stats;
};

static netdev_tx_t nfp_net_null_ndo_start_xmit(struct sk_buff *skb,
					       struct net_device *dev)
{
	dev->stats.tx_errors++;
	dev->stats.tx_carrier_errors++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void nfp_net_null_eto_get_drvinfo(struct net_device *dev,
					 struct ethtool_drvinfo *di)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);

	/* Fill in the stats and flags info
	 */
	strlcpy(di->bus_info, dev_name(dev->dev.parent), sizeof(di->bus_info));

	di->n_priv_flags = 0;
	di->n_stats = sizeof(nm->stats.cache) / sizeof(u64);
	di->testinfo_len = 0;
	di->eedump_len = 0;
	di->regdump_len = 0;

	/* Override the rest */
	strlcpy(di->driver, nfp_net_null_driver_name, sizeof(di->driver));
	strlcpy(di->version, nfp_net_null_driver_version, sizeof(di->version));

	/* FIXME: Get firmware version from hwinfo */
	di->fw_version[0] = 0;
}

static const char *string_priv_flags[] = {
	"tx_link",	/* ETO_PRIV_FLAG_TX_LINK */
	"rx_link",	/* ETO_PRIV_FLAG_RX_LINK */
	"fail_to_wire",	/* ETO_PRIV_FLAG_FAIL_TO_WIRE */
};

static void nfp_net_null_eto_get_strings(struct net_device *dev,
					 u32 stringset, u8 *buf)
{
	int i, count;
	const char **set;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		set = string_priv_flags;
		count = ARRAY_SIZE(string_priv_flags);
		break;
	default:
		count = 0;
		break;
	}

	for (i = 0; i < count; i++, buf += ETH_GSTRING_LEN)
		strncpy(buf, set[i], ETH_GSTRING_LEN);
}

static int nfp_net_null_eto_get_sset_count(struct net_device *dev,
					   int stringset)
{
	int count;

	switch (stringset) {
	case ETH_SS_PRIV_FLAGS:
		count = ARRAY_SIZE(string_priv_flags);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return count;
}

static void update_priv_flags(struct nfp_net_null_dev *nm)
{
	int err;
	u32 txstatus, rxstatus;
	u32 flags = 0;
	int active;

	err = nfp_phymod_eth_read_disable(nm->eth, &txstatus, &rxstatus);
	if (err >= 0) {
		if (!txstatus)
			flags |= ETO_PRIV_FLAG_TX_LINK;
		if (!rxstatus)
			flags |= ETO_PRIV_FLAG_RX_LINK;
	}

	err = nfp_phymod_eth_get_fail_to_wire(nm->eth, NULL, &active);
	if (err >= 0) {
		if (active)
			flags |= ETO_PRIV_FLAG_FAIL_TO_WIRE;
	}

	nm->priv_flags = flags;
}

static u32 nfp_net_null_eto_get_priv_flags(struct net_device *dev)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);

	update_priv_flags(nm);

	return nm->priv_flags;
}

static int nfp_net_null_eto_set_priv_flags(struct net_device *dev, u32 flags)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);
	int err, update;
	u32 txstate, rxstate;

	update_priv_flags(nm);

	/* TX and RX PHY link control */
	txstate = (nm->priv_flags & ETO_PRIV_FLAG_TX_LINK) ? 0 : 1;
	rxstate = (nm->priv_flags & ETO_PRIV_FLAG_RX_LINK) ? 0 : 1;

	update = 0;
	if ((flags ^ nm->priv_flags) & ETO_PRIV_FLAG_TX_LINK) {
		txstate = (flags & ETO_PRIV_FLAG_TX_LINK) ? 0 : 1;
		update = 1;
	}

	if ((flags ^ nm->priv_flags) & ETO_PRIV_FLAG_RX_LINK) {
		txstate = (flags & ETO_PRIV_FLAG_RX_LINK) ? 0 : 1;
		update = 1;
	}

	if (update) {
		err = nfp_phymod_eth_write_disable(nm->eth, txstate, rxstate);
		if (err < 0)
			return err;
	}

	if ((flags ^ nm->priv_flags) & ETO_PRIV_FLAG_FAIL_TO_WIRE) {
		int active = (flags & ETO_PRIV_FLAG_FAIL_TO_WIRE) ? 1 : 0;

		err = nfp_phymod_eth_set_fail_to_wire(nm->eth, active);
		if (err < 0)
			return err;
	}

	return 0;
}

static int phy_parse_qsfp(struct nfp_phymod *phy, struct ethtool_cmd *cmd)
{
	u32 supported = 0, advertise = 0, port = PORT_OTHER;
	u8 tmp = 0;
	int i, err;

	err = nfp_phymod_read8(phy, 131, &tmp);
	if (err < 0)
		return err;

	if (tmp == 0) {
		cmd->supported = SUPPORTED_Backplane;
		cmd->advertising = ADVERTISED_Backplane;
		cmd->port = PORT_OTHER;
		return 0;
	}

	for (i = 0; i < 8; i++) {
		if (!(tmp & (1 << i)))
			continue;

		switch (i) {
		case 0:
			supported |= SUPPORTED_Backplane |
				     SUPPORTED_40000baseKR4_Full;
			advertise |= ADVERTISED_Backplane |
				     ADVERTISED_40000baseKR4_Full;
			port = PORT_OTHER;
			break;
		case 1:
			supported |= SUPPORTED_40000baseLR4_Full;
			advertise |= ADVERTISED_40000baseLR4_Full;
			port = PORT_FIBRE;
			break;
		case 2:
			supported |= SUPPORTED_40000baseSR4_Full;
			advertise |= ADVERTISED_40000baseSR4_Full;
			port = PORT_FIBRE;
			break;
		case 3:
			supported |= SUPPORTED_40000baseCR4_Full;
			advertise |= ADVERTISED_40000baseCR4_Full;
			port = PORT_FIBRE;
			break;
		case 4:
		case 5:
		case 6:
			supported |= SUPPORTED_10000baseR_FEC;
			advertise |= ADVERTISED_10000baseR_FEC;
			port = PORT_FIBRE;
			break;
		case 7:
			supported |= SUPPORTED_Backplane;
			advertise |= ADVERTISED_Backplane;
			port = PORT_OTHER;
			break;
		}
	}

	cmd->supported = supported;
	cmd->advertising = advertise;
	cmd->port = port;

	return 0;
}

static int nfp_net_null_eto_get_settings(struct net_device *dev,
					 struct ethtool_cmd *cmd)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);
	int err, speed, phy_type;
	struct nfp_phymod *phy;

	speed = SPEED_UNKNOWN;
	err = nfp_phymod_eth_get_speed(nm->eth, &speed);
	if (err < 0)
		return err;

	ethtool_cmd_speed_set(cmd, speed);

	err = nfp_phymod_eth_get_phymod(nm->eth, &phy, NULL);
	if (err < 0)
		return err;

	err = nfp_phymod_get_type(phy, &phy_type);
	if (err < 0)
		return err;

	/* FIXME: CXP/SFP Read the actual port config from the MAC */
	switch (phy_type) {
	case NFP_PHYMOD_TYPE_QSFP:
		err = phy_parse_qsfp(phy, cmd);
		if (err < 0)
			return err;
		break;
	default:
		cmd->supported = SUPPORTED_Backplane;
		cmd->advertising = ADVERTISED_Backplane;
		cmd->port = PORT_OTHER;
		break;
	}

	/* We also support PFC, but there is no ethtool bit for that yet */
	cmd->supported   |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	cmd->advertising |= ADVERTISED_Pause | SUPPORTED_Asym_Pause;

	cmd->duplex = DUPLEX_FULL;
	cmd->transceiver = XCVR_EXTERNAL;
	cmd->autoneg = AUTONEG_DISABLE;

	cmd->mdio_support = 0;
	cmd->phy_address = 0;

	cmd->maxtxpkt = 0;
	cmd->maxrxpkt = 0;
	cmd->eth_tp_mdix = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	cmd->eth_tp_mdix_ctrl = 0;
#endif

	cmd->lp_advertising = cmd->advertising;

	return 0;
}

static int nfp_net_null_update_stats(struct net_device *dev)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);
	struct nfp_nbi_mac_portstats *ps = &nm->stats.cache;
	int err;

	err = nfp_nbi_mac_stats_read_port(nm->nbi, nm->port, ps);
	if (err < 0) {
		/* Return an error, or report the stale cache?
		 * For now, let's just use the stale cache.
		 */
		if (!nm->no_stats)
			netdev_dbg(dev, "Can't access nbi%d.%d stats\n",
				   nm->mac, nm->port);
	}

	nm->no_stats = (err < 0) ? 1 : 0;

	return err;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
static struct rtnl_link_stats64 *
nfp_net_null_ndo_get_stats64(struct net_device *dev,
			     struct rtnl_link_stats64 *st)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);
	struct nfp_nbi_mac_portstats *ps = &nm->stats.cache;

	nfp_net_null_update_stats(dev);

	if (!st)
		return NULL;

	st->rx_packets = ps->RxPStatsPkts;
	/* FIXME: Shouldn't there be a txPStatsPkts? */
	st->tx_packets = ps->TxPStatsPkts1518toMAXoctets +
			 ps->TxPStatsPkts1024to1518octets +
			 ps->TxPStatsPkts512to1023octets +
			 ps->TxPStatsPkts256to511octets +
			 ps->TxPStatsPkts65to127octets +
			 ps->TxPStatsPkts64octets;
	st->rx_bytes = ps->RxPIfInOctets;
	st->tx_bytes = ps->TxPIfOutOctets;
	st->rx_errors = ps->RxPIfInErrors;
	st->tx_errors = ps->TxPIfOutErrors;
	st->rx_dropped = ps->RxPStatsDropEvents;
	/* FIXME: Are all errors dropped? */
	st->tx_dropped = ps->TxPIfOutErrors;
	st->multicast = ps->RxPIfInMultiCastPkts;
	/* CHECKME: Are jabbers collisions? */
	st->collisions = ps->RxPStatsJabbers;

	st->rx_length_errors = ps->RxInRangeLengthErrors;
	st->rx_over_errors = 0;
	st->rx_crc_errors = ps->RxFrameCheckSequenceErrors;
	st->rx_frame_errors = ps->RxAlignmentErrors;
	st->rx_fifo_errors = 0;
	st->rx_missed_errors = 0;

	st->tx_aborted_errors = 0;
	st->tx_carrier_errors = 0;
	st->tx_fifo_errors = 0;
	st->tx_heartbeat_errors = 0;
	st->tx_window_errors = 0;

	st->rx_compressed = 0;
	st->tx_compressed = 0;

	return st;
}
#endif /* >= KERNEL_VERSION(3, 1, 0) */

static void nfp_net_null_eto_get_ethtool_stats(struct net_device *dev,
					       struct ethtool_stats *stats,
					       u64 *data)
{
	struct nfp_net_null_dev *nm = netdev_priv(dev);

	nfp_net_null_update_stats(dev);

	memcpy(data, &nm->stats.cache, sizeof(nm->stats.cache));
}

static u32 nfp_net_null_eto_get_link(struct net_device *dev)
{
	int err;
	struct nfp_net_null_dev *nm = netdev_priv(dev);
	u32 state = 0;
	int port = nm->port;

	err = nfp_nbi_mac_eth_read_linkstate(nm->nbi,
					     port / 12, port % 12, &state);
	if (err < 1)
		netif_carrier_off(dev);
	else
		netif_carrier_on(dev);

	return netif_carrier_ok(dev) ? 1 : 0;
}

/* Allocate one netdev
 */
static int nfp_net_null_create(struct nfp_net_null *np,
			       int np_port, struct nfp_phymod_eth *eth)
{
	struct nfp_net_null_dev *nd;
	struct net_device *dev;
	int err;
	int mac, port, speed;

	err = nfp_phymod_eth_get_speed(eth, &speed);
	if (err < 0)
		return err;

	err = nfp_phymod_eth_get_nbi(eth, &mac);
	if (err < 0)
		return err;

	err = nfp_phymod_eth_get_port(eth, &port, NULL);
	if (err < 0)
		return err;

	dev = alloc_etherdev(sizeof(*nd));
	if (!dev)
		return -ENOMEM;

	nd = netdev_priv(dev);

	nd->eth = eth;
	nd->nbi = nfp_nbi_open(np->nfp, mac);
	if (!nd->nbi) {
		free_netdev(dev);
		return -ENOMEM;
	}

	nd->mac = mac;
	nd->port = port;

	/* Use the nfp6000_mac_netdev_ops as a baseline,
	 * and override as needed
	 */
	nd->netdev_ops.ndo_start_xmit = nfp_net_null_ndo_start_xmit;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
	nd->netdev_ops.ndo_get_stats64 = nfp_net_null_ndo_get_stats64,
#endif

	dev->netdev_ops = &nd->netdev_ops;

	nd->ethtool_ops.get_drvinfo = nfp_net_null_eto_get_drvinfo;
	nd->ethtool_ops.get_settings = nfp_net_null_eto_get_settings;
	nd->ethtool_ops.get_ethtool_stats = nfp_net_null_eto_get_ethtool_stats;
	nd->ethtool_ops.get_link = nfp_net_null_eto_get_link;
	nd->ethtool_ops.get_strings = nfp_net_null_eto_get_strings;
	nd->ethtool_ops.get_sset_count = nfp_net_null_eto_get_sset_count;
	nd->ethtool_ops.get_priv_flags = nfp_net_null_eto_get_priv_flags;
	nd->ethtool_ops.set_priv_flags = nfp_net_null_eto_set_priv_flags;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0))
	nd->ethtool_ops.get_ts_info = ethtool_op_get_ts_info;
#endif

	dev->ethtool_ops = &nd->ethtool_ops;

	SET_NETDEV_DEV(dev, np->parent);
	dev->watchdog_timeo = TX_TIMEOUT;

	err = register_netdev(dev);
	if (err < 0) {
		nfp_nbi_close(nd->nbi);
		free_netdev(dev);
		return err;
	}

	netdev_info(dev, "nbi%d.%d %d%c\n", mac, port,
		    (speed < 1000) ? speed : (speed / 1000),
		    (speed < 1000) ? 'M' : 'G');

	np->port[np_port] = dev;

	return 0;
}

static void nfp_net_null_destroy(struct nfp_net_null *np, int port)
{
	struct net_device *dev = np->port[port];
	struct nfp_net_null_dev *nm = netdev_priv(dev);

	if (!dev)
		return;

	unregister_netdev(dev);
	nfp_nbi_close(nm->nbi);
	free_netdev(dev);
}

/*
 * nfp_net_null_probe() - callback for CPP devices being added
 * @pdev:	Platform device
 *
 * Return: 0, or -ERRNO
 */
static int nfp_net_null_probe(struct platform_device *pdev)
{
	struct nfp_device *nfp;
	u32 model;
	struct nfp_net_null *np;
	void *tmp = NULL;
	struct nfp_phymod_eth *eth;
	struct nfp_cpp *cpp;
	struct nfp_platform_data *pdata;

	pdata = nfp_platform_device_data(pdev);
	BUG_ON(!pdata);

	nfp = nfp_device_open(pdata->nfp);
	if (!nfp)
		return -ENODEV;

	cpp = nfp_device_cpp(nfp);
	BUG_ON(!cpp);

	model = nfp_cpp_model(cpp);
	if (!NFP_CPP_MODEL_IS_6000(model)) {
		/* TODO: Add NFP3200 support */
		nfp_device_close(nfp);
		return -ENODEV;
	}

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np) {
		nfp_device_close(nfp);
		return -ENOMEM;
	}

	np->nfp = nfp;
	np->ports = 0;
	np->parent = &pdev->dev;

	for (eth = nfp_phymod_eth_next(nfp, NULL, &tmp);
	     eth && np->ports < ARRAY_SIZE(np->port);
	     eth = nfp_phymod_eth_next(nfp, NULL, &tmp)) {
		int err;

		err = nfp_net_null_create(np, np->ports, eth);
		if (err < 0)
			continue;

		np->ports++;
	}

	platform_set_drvdata(pdev, np);

	return 0;
}

/*
 * nfp_net_null_remove() - callback for removing CPP devices
 * @pdev:	Platform device
 *
 * Return: 0, or -ERRNO
 */
static int nfp_net_null_remove(struct platform_device *pdev)
{
	struct nfp_net_null *np = platform_get_drvdata(pdev);
	int port;

	for (port = 0; port < np->ports; port++)
		nfp_net_null_destroy(np, port);

	nfp_device_close(np->nfp);

	platform_set_drvdata(pdev, NULL);
	kfree(np);

	return 0;
}

static struct platform_driver nfp_net_null_driver = {
	.probe = nfp_net_null_probe,
	.remove = nfp_net_null_remove,
	.driver = {
		.name = NFP_NET_NULL_TYPE,
	},
};

/**
 * nfp_net_null_init() - Register the null NFP network driver
 *
 * Only for debugging PHY registration.
 *
 * Return: 0, or -ERRNO
 */
int __init nfp_net_null_init(void)
{
	int err;

	err = platform_driver_register(&nfp_net_null_driver);
	if (err)
		return err;

	pr_info("%s: NFP Null Network Driver, Copyright (C) 2014-2015 Netronome Systems\n",
		NFP_NET_NULL_TYPE);

	return 0;
}

/**
 * nfp_net_null_exit() - Unregister the null NFP network driver
 */
void nfp_net_null_exit(void)
{
	platform_driver_unregister(&nfp_net_null_driver);
}
