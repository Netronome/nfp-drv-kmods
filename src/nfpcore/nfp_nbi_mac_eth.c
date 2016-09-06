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
 * nfp_nbi_mac_eth.c
 * nfp6000 MAC API functions
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *
 * Functions mostly related to the MAC Ethernet
 * (Hydra) core and the MAC-NBI channel interface.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "nfp.h"
#include "nfp_nbi.h"

#define NFP_MAC                                     0x3a0000
#define NFP_MAC_MACMUXCTRL                          0x0000000c
#define NFP_MAC_MACSERDESEN                         0x00000010
#define   NFP_MAC_MACSERDESEN_SERDESENABLE(_x)      (((_x) & 0xffffff) << 0)
#define NFP_MAC_ETH(_x)	\
	(NFP_MAC + 0x40000 + ((_x) & 0x1) * 0x20000)
#define NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1(_x) \
					(0x00004080 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1_BLOCKLOCKED \
					(1 << 0)
#define   NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1_RCVLINKSTATUS \
					(1 << 12)
#define NFP_MAC_ETH_MACETHCHPCSSEG_CTL1(_x)     \
					(0x00004000 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_LOOPBACK \
					(1 << 14)
#define   NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(_x) \
					(((_x) & 0xf) << 2)
#define   NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 \
					(1 << 13)
#define   NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 \
					(1 << 6)
#define NFP_MAC_ETH_MACETHCHPCSSEG_STATUS1(_x)  \
					(0x00004004 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHCHPCSSEG_STATUS1_RCVLINKSTATUS \
					(1 << 2)
#define NFP_MAC_ETH_MACETHGLOBAL_ETHACTCTLSEG           0x00003000
#define   NFP_MAC_ETH_MACETHGLOBAL_ETHACTCTLSEG_ETHACTIVATESEGMENT(_x) \
					(((_x) & 0xfff) << 0)
#define NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG(_x)          \
					(0x00000008 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHRXENA   \
					(1 << 1)
#define   NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHTXENA   \
					(1 << 0)
#define NFP_MAC_ETH_MACETHSEG_ETHMACADDR0(_x)           \
					(0x0000000c + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHSEG_ETHMACADDR0_ETHMACADDR0(_x) \
					(((_x) & 0xffffffff) << 0)
#define NFP_MAC_ETH_MACETHSEG_ETHMACADDR1(_x)           \
					(0x00000010 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_MACETHSEG_ETHMACADDR1_ETHMACADDR1(_x) \
					(((_x) & 0xffff) << 0)
#define NFP_MAC_ETH_ETHSGMIIIFMODE(_x)        \
					(0x00000350 + (0x400 * ((_x) & 0xf)))
#define   NFP_MAC_ETH_ETHSGMIIIFMODE_ETHSGMIIENA \
					(1 << 0)
#define   NFP_MAC_ETH_ETHSGMIIIFMODE_ETHSGMIIPCSENABLE \
					(1 << 5)
#define     NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_100MBPS (1)
#define     NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_10MBPS (0)
#define   NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_of(_x) \
					(((_x) >> 2) & 0x3)
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_MASK \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0xf))
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_10GE \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0))
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_10PASSTS \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0x1))
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_8023AV \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0x2))
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_40GE \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0x3))
#define NFP_NBI_MAC_ETHCHPCSCTL1_MODE_100GE \
	(NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_13 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_6 | \
	 NFP_MAC_ETH_MACETHCHPCSSEG_CTL1_SPEED_0(0x4))
#define NFP_NBI_MAC_MACMUXCTRL_ERROR 0x1
#define NFP_NBI_MAC_ETHACTCTLSEG_DISABLED 0x2
#define NFP_NBI_MAC_ETHCHPCSStatus1_RCVLINKSTATUS_DOWN 0x4
#define NFP_NBI_MAC_ETHCHPCSBASERSTATUS1_RCVLINKSTATUS_DOWN 0x8
#define NFP_NBI_MAC_ETHCHPCSBASERSTATUS1_BLOCKLOCKED_FALSE 0x10
#define NFP_NBI_MAC_ETHCMDCONFIG_ETHTXENA_FALSE 0x20
#define NFP_NBI_MAC_ETHCMDCONFIG_ETHRXENA_FALSE 0x40
#define NFP_MAC_ILK_LKRXALIGNSTATUS_FALSE 0x80
#define NFP_MAC_ILK_LKRXSTATUSMESSAGE_FALSE 0x100
#define NFP_NBI_MAC_MACEQINH 0x00000278
#define NFP_NBI_MAC_MACEQINHDONE 0x0000027c
#define NFP_NBI_MAC_HYD0BLOCKRESET 0x00000004
#define NFP_NBI_MAC_HYD1BLOCKRESET 0x00000008
#define NFP_NBI_MAC_MACHYDBLKRESET_MAC_HYD_RX_SERDES_RST(_x)	\
		(((_x) & 0xfff) << 20)
#define NFP_NBI_MAC_MACHYDBLKRESET_MAC_HYD_TX_SERDES_RST(_x)	\
		(((_x) & 0xfff) << 4)

static int __nfp_nbi_mac_eth_ifdown(struct nfp_nbi_dev *nbi, int core, int port)
{
	u64 r;
	u32 d = 0;
	u32 m = 0;
	int ret;
	int mode = 0;
	int timeout = 100;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	/* Disable port enqueue at packet boundary */
	m = 0x1 << (port + core * 12);
	ret = nfp_nbi_mac_regw(nbi, NFP_MAC, NFP_NBI_MAC_MACEQINH, m, m);
	if (ret < 0)
		return ret;
	d = 0;
	while ((d == 0) && (timeout-- > 0)) {
		ret = nfp_nbi_mac_regr(nbi,
				       NFP_MAC, NFP_NBI_MAC_MACEQINHDONE, &d);
		if (ret < 0) {
			nfp_nbi_mac_regw(nbi,
					 NFP_MAC, NFP_NBI_MAC_MACEQINH, m, 0);
			return ret;
		}
		d &= m;
		usleep_range(100, 200);
	}

	/* Disable transmit & receive paths */
	r = NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG(port);
	d = NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHRXENA |
		NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHTXENA;
	ret = nfp_nbi_mac_regw(nbi, NFP_MAC_ETH(core), r, d, 0);
	if (ret < 0)
		return ret;

	/* Reenable enqueue */
	ret = nfp_nbi_mac_regw(nbi, NFP_MAC, NFP_NBI_MAC_MACEQINH, m, 0);
	if (ret < 0)
		return ret;

	/* Disable the serdes lanes */
	mode = nfp_nbi_mac_eth_read_mode(nbi, core, port);
	if (mode < 0)
		return mode;
	switch (mode) {
	case NFP_NBI_MAC_ENET_100G:
		m = 0x3ff << (core * 12);
		break;
	case NFP_NBI_MAC_ENET_40G:
		m = 0xf << (port + core * 12);
		break;
	default:
		m = 0x1 << (port + core * 12);
		break;
	}
	d = NFP_NBI_MAC_MACHYDBLKRESET_MAC_HYD_RX_SERDES_RST(m) |
		NFP_NBI_MAC_MACHYDBLKRESET_MAC_HYD_TX_SERDES_RST(m);
	if (core)
		r = NFP_NBI_MAC_HYD1BLOCKRESET;
	else
		r = NFP_NBI_MAC_HYD0BLOCKRESET;

	ret = nfp_nbi_mac_regw(nbi, NFP_MAC, r, d, d);
	if (ret < 0)
		return ret;

	m = NFP_MAC_MACSERDESEN_SERDESENABLE(m);
	ret = nfp_nbi_mac_regw(nbi, NFP_MAC, NFP_MAC_MACSERDESEN, m, 0);
	if (ret < 0)
		return ret;

	return nfp_nbi_mac_regw(nbi, NFP_MAC, r, d, 0);
}

/**
 * nfp_nbi_mac_eth_ifdown() - Disable an Ethernet port.
 * @nbi:	NBI device handle
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 *
 * This function disables packet enqueue to the port, waits for
 * packets in progress to complete, disables Rx & Tx, and deactivates
 * the serdes lanes for the specified port.
 *
 * Return: 0, or -ERRNO
 */
int nfp_nbi_mac_eth_ifdown(struct nfp_nbi_dev *nbi, int core, int port)
{
	int rc, err;

	err = nfp_nbi_mac_acquire(nbi);
	if (err < 0)
		return err;

	rc = __nfp_nbi_mac_eth_ifdown(nbi, core, port);

	err = nfp_nbi_mac_release(nbi);
	if (err < 0)
		return err;

	return rc;
}

static int __nfp_nbi_mac_eth_ifup(struct nfp_nbi_dev *nbi, int core, int port)
{
	int ret;
	u64 r;
	u32 d = 0;
	u32 m = 0;
	int mode = 0;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	/* Ensure enqueue is enabled */
	m = 0x1 << (port + core * 12);
	nfp_nbi_mac_regw(nbi, NFP_MAC, NFP_NBI_MAC_MACEQINH, m, 0);

	/* Enable transmit & receive paths */
	r = NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG(port);
	d = NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHRXENA |
	    NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHTXENA;
	m = d;
	ret = nfp_nbi_mac_regw(nbi, NFP_MAC_ETH(core), r, m, d);
	if (ret < 0)
		return ret;

	/* Enable the serdes lanes */
	mode = nfp_nbi_mac_eth_read_mode(nbi, core, port);
	if (mode < 0)
		return mode;
	switch (mode) {
	case NFP_NBI_MAC_ENET_100G:
		m = 0x3ff << (core * 12);
		break;
	case NFP_NBI_MAC_ENET_40G:
		m = 0xf << (port + core * 12);
		break;
	default:
		m = 0x1 << (port + core * 12);
		break;
	}
	r = NFP_MAC_MACSERDESEN;
	m = NFP_MAC_MACSERDESEN_SERDESENABLE(m);
	d = m;

	return nfp_nbi_mac_regw(nbi, NFP_MAC, r, m, d);
}

/**
 * nfp_nbi_mac_eth_ifup() - Enable an Ethernet port.
 * @nbi:	NBI device handle
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 *
 * This function enables Rx & Tx, and initiates a PCS reset and
 * activates the specified port. It assumes that the port speed and
 * all other configuration parameters for the port have been
 * initialized elsewhere.
 *
 * Return: 0, or -ERRNO
 */
int nfp_nbi_mac_eth_ifup(struct nfp_nbi_dev *nbi, int core, int port)
{
	int rc, err;

	err = nfp_nbi_mac_acquire(nbi);
	if (err < 0)
		return err;

	rc = __nfp_nbi_mac_eth_ifup(nbi, core, port);

	err = nfp_nbi_mac_release(nbi);
	if (err < 0)
		return err;

	return rc;
}

/**
 * nfp_nbi_mac_eth_read_linkstate() - Check the link state of an Ethernet port
 * @nbi:	NBI device
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 * @linkstate:	State detail
 *
 * This function returns 1 if the specified port has link up and block
 * lock.  It returns zero if the link is down.  If linkstate parameter
 * is not NULL this function will use it to return more detail for the
 * link down state.
 *
 * Return: 0 - link down, 1 - link up, or -ERRNO.
 */
int nfp_nbi_mac_eth_read_linkstate(struct nfp_nbi_dev *nbi, int core, int port,
				   u32 *linkstate)
{
	u64 r;
	u32 d = 0;
	u32 m = 0;
	u32 status = 0;
	int ret;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	if (linkstate)
		*linkstate = 0;

	ret = nfp_nbi_mac_regr(nbi, NFP_MAC,
			       NFP_MAC_MACMUXCTRL, &d);
	if (ret < 0)
		return ret;

	m = 1 << ((core * 12) + port);
	if ((d & m) > 0)
		status |= NFP_NBI_MAC_MACMUXCTRL_ERROR;

	r = NFP_MAC_ETH_MACETHGLOBAL_ETHACTCTLSEG;
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if (!(d & (0x1 << port)))
		status |= NFP_NBI_MAC_ETHACTCTLSEG_DISABLED;

	r = NFP_MAC_ETH_MACETHCHPCSSEG_STATUS1(port);
	/* Double read to clear latch low on link down */
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if (!(d & NFP_MAC_ETH_MACETHCHPCSSEG_STATUS1_RCVLINKSTATUS))
		status |= NFP_NBI_MAC_ETHCHPCSStatus1_RCVLINKSTATUS_DOWN;

	r = NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1(port);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if (!(d & NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1_RCVLINKSTATUS))
		status |=
		    NFP_NBI_MAC_ETHCHPCSBASERSTATUS1_RCVLINKSTATUS_DOWN;

	if (!(d & NFP_MAC_ETH_MACETHCHPCSSEG_BASERSTATUS1_BLOCKLOCKED))
		status |=
		    NFP_NBI_MAC_ETHCHPCSBASERSTATUS1_BLOCKLOCKED_FALSE;

	r = NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG(port);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if (!(d & NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHTXENA))
		status |= NFP_NBI_MAC_ETHCMDCONFIG_ETHTXENA_FALSE;

	if (!(d & NFP_MAC_ETH_MACETHSEG_ETHCMDCONFIG_ETHRXENA))
		status |= NFP_NBI_MAC_ETHCMDCONFIG_ETHRXENA_FALSE;

	if (linkstate)
		*linkstate = status;

	return (status) ? 0 : 1;
}

/**
 * nfp_nbi_mac_eth_read_mode() - Return the mode for an Ethernet port.
 * @nbi:	NBI device
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 *
 * This function returns the mode for the specified port.
 *
 * Returned valued will be one of:
 *    NFP_NBI_MAC_ENET_OFF:	Disabled
 *    NFP_NBI_MAC_ILK:		Interlaken mode
 *    NFP_NBI_MAC_ENET_10M:	10Mbps Ethernet
 *    NFP_NBI_MAC_ENET_100M:	100Mbps Ethernet
 *    NFP_NBI_MAC_ENET_1G:	1Gbps Ethernet
 *    NFP_NBI_MAC_ENET_10G:	10Gbps Ethernet
 *    NFP_NBI_MAC_ENET_40G:	40Gbps Ethernet
 *    NFP_NBI_MAC_ENET_100G:	100Gbps Ethernet
 *
 * Return: Port mode, or -ERRNO
 */
int nfp_nbi_mac_eth_read_mode(struct nfp_nbi_dev *nbi, int core, int port)
{
	int ret;
	u64 r;
	u32 d = 0;
	u32 m = 0;
	u32 mux = 0;
	int mode;
	int s;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	/* Check the Serdes lane assignments */
	ret =
	    nfp_nbi_mac_regr(nbi, NFP_MAC, NFP_MAC_MACMUXCTRL,
			     &mux);
	if (ret < 0)
		return ret;

	m = 1 << ((core * 12) + port);
	if ((mux & m) > 0)
		return NFP_NBI_MAC_ILK;

	/* check port 0 for 100G 0x2050 */
	r = NFP_MAC_ETH_MACETHCHPCSSEG_CTL1(0);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if ((d & NFP_NBI_MAC_ETHCHPCSCTL1_MODE_MASK) ==
	    NFP_NBI_MAC_ETHCHPCSCTL1_MODE_100GE) {
		/* port 0-9 = 100G - ports 10, 11 can be 10G */
		if (port < 10)
			return NFP_NBI_MAC_ENET_100G;
	}

	/* check ports 0,4,8 for 40G */
	s = port % 4;
	r = NFP_MAC_ETH_MACETHCHPCSSEG_CTL1(port - s);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	if ((d & NFP_NBI_MAC_ETHCHPCSCTL1_MODE_MASK) ==
	    NFP_NBI_MAC_ETHCHPCSCTL1_MODE_40GE)
		return NFP_NBI_MAC_ENET_40G;

	/* All that remains is 10G or less */
	r = NFP_MAC_ETH_MACETHCHPCSSEG_CTL1(port);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	switch (d & NFP_NBI_MAC_ETHCHPCSCTL1_MODE_MASK) {
	case NFP_NBI_MAC_ETHCHPCSCTL1_MODE_10GE:
		/* check if < 10G AE */
		r = NFP_MAC_ETH_ETHSGMIIIFMODE(port);
		ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
		if (ret < 0)
			return ret;

		if (d & NFP_MAC_ETH_ETHSGMIIIFMODE_ETHSGMIIPCSENABLE) {
			if (d & NFP_MAC_ETH_ETHSGMIIIFMODE_ETHSGMIIENA) {
				int s;

				s = NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_of(d);
				/* SGMII */
				switch (s) {
				case NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_10MBPS:
					mode = NFP_NBI_MAC_ENET_10M;
					break;
				case NFP_MAC_ETH_ETHSGMIIIFMODE_SPEED_100MBPS:
					mode = NFP_NBI_MAC_ENET_100M;
					break;
				case 0x2:
					/* AE case */
					mode = NFP_NBI_MAC_ENET_1G;
					break;
				default:
					mode = -EINVAL;
					break;
				}
			} else {
				/* 100Base-X */
				mode = NFP_NBI_MAC_ENET_1G;
			}
			return mode;
		} else {
			return NFP_NBI_MAC_ENET_10G;
		}

		break;
	case NFP_NBI_MAC_ETHCHPCSCTL1_MODE_8023AV:
		break;
	case NFP_NBI_MAC_ETHCHPCSCTL1_MODE_10PASSTS:
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int nfp_nbi_mac_eth_write_mac_addr_(struct nfp_nbi_dev *nbi, int core,
					   int port, u64 hwaddr)
{
	int ret;
	u64 r;
	u32 d = 0;
	u32 m = 0;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	if ((hwaddr >> 48) > 0)
		return -EINVAL;

	r = NFP_MAC_ETH_MACETHSEG_ETHMACADDR0(port);
	m = NFP_MAC_ETH_MACETHSEG_ETHMACADDR0_ETHMACADDR0(0xffffffffffffULL);
	d = NFP_MAC_ETH_MACETHSEG_ETHMACADDR0_ETHMACADDR0(hwaddr);

	ret = nfp_nbi_mac_regw(nbi, NFP_MAC_ETH(core), r, m, d);
	if (ret < 0)
		return ret;

	r = NFP_MAC_ETH_MACETHSEG_ETHMACADDR1(port);
	m = NFP_MAC_ETH_MACETHSEG_ETHMACADDR1_ETHMACADDR1(0xffff);
	d = NFP_MAC_ETH_MACETHSEG_ETHMACADDR1_ETHMACADDR1(hwaddr >> 32);

	return nfp_nbi_mac_regw(nbi, NFP_MAC_ETH(core), r, m, d);
}

/**
 * nfp_nbi_mac_eth_write_mac_addr() - Write the MAC address for a port
 * @nbi:	NBI device
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 * @hwaddr:	MAC address (48-bits)
 *
 * Return: 0, or -ERRNO
 */
int nfp_nbi_mac_eth_write_mac_addr(struct nfp_nbi_dev *nbi, int core,
				   int port, u64 hwaddr)
{
	int rc, err;

	err = nfp_nbi_mac_acquire(nbi);
	if (err < 0)
		return err;

	rc = nfp_nbi_mac_eth_write_mac_addr_(nbi, core, port, hwaddr);

	err = nfp_nbi_mac_release(nbi);
	if (err < 0)
		return err;

	return rc;
}

/**
 * nfp_nbi_mac_eth_read_mac_addr() - Read the MAC address for a port
 * @nbi:	NBI device
 * @core:	MAC ethernet core: [0-1]
 * @port:	MAC ethernet port: [0-11]
 * @hwaddr:	MAC address (48-bits)
 *
 * Return: 0, or -ERRNO
 */
int nfp_nbi_mac_eth_read_mac_addr(struct nfp_nbi_dev *nbi, int core,
				  int port, u64 *hwaddr)
{
	int ret;
	u64 r;
	u32 d = 0;
	u32 m = 0;

	if (!nbi)
		return -ENODEV;

	if ((core < 0) || (core > 1))
		return -EINVAL;

	if ((port < 0) || (port > 11))
		return -EINVAL;

	if (!hwaddr)
		return -EINVAL;

	r = NFP_MAC_ETH_MACETHSEG_ETHMACADDR0(port);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &d);
	if (ret < 0)
		return ret;

	r = NFP_MAC_ETH_MACETHSEG_ETHMACADDR1(port);
	ret = nfp_nbi_mac_regr(nbi, NFP_MAC_ETH(core), r, &m);
	if (ret < 0)
		return ret;

	*hwaddr = m;
	*hwaddr = (*hwaddr << 32) | d;
	return 0;
}
