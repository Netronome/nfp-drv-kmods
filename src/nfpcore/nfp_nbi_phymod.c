/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
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
 * nfp_phymod.c
 * Authors: Jason Mcmullan <jason.mcmullan@netronome.com>
 *          David Brunecz <david.brunecz@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "nfp.h"
#include "nfp_nbi_phymod.h"

#define NSP_ETH_MAX_COUNT		48
#define NSP_ETH_TX_STATE_ENABLED	BIT_ULL(1)
#define NSP_ETH_PORT_LANES_of(x)	(((x) >>  0) & 0xf)
#define NSP_ETH_STATE_RATE_of(x)	(((x) >>  8) & 0xf)
#define NSP_ETH_PORT_PHYLABEL_of(x)	(((x) >> 54) & 0x3f)
#define NSP_ETH_PORT_LABEL_of(x)	(((x) >> 48) & 0x3f)

enum eth_rate {
	RATE_INVALID = 0,
	RATE_10M,
	RATE_100M,
	RATE_1G,
	RATE_10G,
	RATE_25G,
};

struct eth_table_entry {
	__le64 port;
	__le64 state;
	u8 mac[6];
	u8 resv[2];
	__le64 control;
};

struct eth_priv {
	struct nfp_device *nfp;
	int eth_idx;
	char label[8];
	u8 mac[6];

	struct eth_table_entry eths[NSP_ETH_MAX_COUNT];
};

static void eth_private_free(void *_priv)
{
}

static void *eth_private(struct nfp_device *nfp)
{
	struct eth_priv *priv;

	priv = nfp_device_private_alloc(nfp, sizeof(struct eth_priv),
					eth_private_free);
	if (!priv)
		return NULL;

	priv->nfp = nfp;

	return priv;
}

/**
 * nfp_phymod_eth_next() - PHY Module Ethernet port enumeration
 * @nfp:	NFP Device handle
 * @phy:	PHY module
 * @ptr:	Abstract pointer, must be NULL to get the first port
 *
 * This function allows enumeration of the Ethernet ports
 * attached to a PHY module
 *
 * Return: struct nfp_phymod_eth pointer, or NULL
 */
struct nfp_phymod_eth *nfp_phymod_eth_next(struct nfp_device *nfp,
					   struct nfp_phymod *phy, void **ptr)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	struct eth_priv *priv;
	int err;
	u64 port;

	priv = nfp_device_private(nfp, eth_private);
	if (!priv || !priv->eths)
		return NULL;

	if (!*ptr) {
		struct nfp_nsp *nsp;

		nsp = nfp_nsp_open(cpp);
		if (IS_ERR(nsp))
			return NULL;

		err = nfp_nsp_command_buf(nsp, SPCODE_ETH_RESCAN,
					  sizeof(priv->eths), NULL, 0,
					  priv->eths, sizeof(priv->eths));
		nfp_nsp_close(nsp);
		if (err)
			return NULL;
		priv->eth_idx = 0;
		*ptr = priv;
	} else {
		priv->eth_idx++;
	}

	for (; priv->eth_idx >= 0 && priv->eth_idx < NSP_ETH_MAX_COUNT;
	     priv->eth_idx++) {
		port = le64_to_cpu(priv->eths[priv->eth_idx].port);
		if (!NSP_ETH_PORT_LANES_of(port))
			continue;
		return (struct nfp_phymod_eth *)priv;
	}

	*ptr = NULL;
	return NULL;
}

/**
 * nfp_phymod_eth_get_index() - Get the index for a phymod's eth interface
 * @eth:		PHY module ethernet interface
 * @index:		Pointer to a int for the index (unique for all eths)
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_index(struct nfp_phymod_eth *eth, int *index)
{
	struct eth_priv *priv = (struct eth_priv *)eth;

	*index = priv->eth_idx;
	return 0;
}

/**
 * nfp_phymod_eth_get_mac() - Get the MAC address of an ethernet port
 * @eth:		PHY module ethernet interface
 * @mac:		Pointer to a const u8 * for the 6-byte MAC
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_mac(struct nfp_phymod_eth *eth, const u8 **mac)
{
	struct eth_priv *priv;
	int i;
	u8 *m;

	priv = (struct eth_priv *)eth;
	m = priv->eths[priv->eth_idx].mac;
	for (i = 0; i < 6; i++)
		priv->mac[5 - i] = m[i];
	*mac = priv->mac;

	return 0;
}

/**
 * nfp_phymod_eth_get_label() - Get the string (UTF8) label
 * @eth:		PHY module ethernet interface
 * @label:		Pointer to a const char * for the label
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_label(struct nfp_phymod_eth *eth, const char **label)
{
	struct eth_priv *priv;
	int idx, p_lbl, e_lbl;

	priv = (struct eth_priv *)eth;
	idx = priv->eth_idx;

	p_lbl = NSP_ETH_PORT_PHYLABEL_of(le64_to_cpu(priv->eths[idx].port));
	e_lbl = NSP_ETH_PORT_LABEL_of(le64_to_cpu(priv->eths[idx].port));

	snprintf(priv->label, sizeof(priv->label), "%d.%d", p_lbl, e_lbl);
	*label = priv->label;
	return 0;
}

/**
 * nfp_phymod_eth_get_nbi() - Get the NBI ID for a phymod's Ethernet interface
 * @eth:		PHY module ethernet interface
 * @nbi:		Pointer to a int for the NBI
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_nbi(struct nfp_phymod_eth *eth, int *nbi)
{
	struct eth_priv *priv = (struct eth_priv *)eth;

	*nbi = priv->eth_idx / 24;
	return 0;
}

/**
 * nfp_phymod_eth_get_port() - Get the base port and/or lanes
 * @eth:		PHY module ethernet interface
 * @base:		Pointer to a int for base port (0..23)
 * @lanes:		Pointer to a int for number of phy lanes
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_port(struct nfp_phymod_eth *eth, int *base, int *lanes)
{
	struct eth_priv *priv;
	int idx;

	priv = (struct eth_priv *)eth;
	idx = priv->eth_idx;

	*base = idx % 24;
	*lanes = NSP_ETH_PORT_LANES_of(le64_to_cpu(priv->eths[idx].port));
	return 0;
}

/**
 * nfp_phymod_eth_get_speed() - Get the speed of the Ethernet port
 * @eth:		PHY module ethernet interface
 * @speed:	Pointer to an int for speed (in megabits/sec)
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_get_speed(struct nfp_phymod_eth *eth, int *speed)
{
	struct eth_priv *priv;
	int idx, lanes, r;

	priv = (struct eth_priv *)eth;
	idx = priv->eth_idx;

	lanes = NSP_ETH_PORT_LANES_of(le64_to_cpu(priv->eths[idx].port));

	switch (NSP_ETH_STATE_RATE_of(le64_to_cpu(priv->eths[idx].state))) {
	case RATE_10M:
		r = 10;
		break;
	case RATE_100M:
		r = 100;
		break;
	case RATE_1G:
		r = 1000;
		break;
	case RATE_10G:
		r = 10000;
		break;
	case RATE_25G:
		r = 25000;
		break;
	default:
		return -1;
	}
	*speed = r * lanes;
	return 0;
}

/**
 * nfp_phymod_eth_read_disable() - Read PHY Disable state for an eth port
 * @eth:	PHY module ethernet interface
 * @tx_disable:	Disable status for the ethernet port
 * @rx_disable:	Disable status for the ethernet port, not implemented
 *
 * For both rx_disable and tx_disable, 0 = active, 1 = disabled
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_read_disable(struct nfp_phymod_eth *eth,
				u32 *tx_disable, u32 *rx_disable)
{
	struct eth_priv *priv = (struct eth_priv *)eth;
	int idx;
	u32 val;

	priv = (struct eth_priv *)eth;
	idx = priv->eth_idx;

	if (NSP_ETH_TX_STATE_ENABLED & le64_to_cpu(priv->eths[idx].state))
		val = 0;
	else
		val = ~0;

	if (tx_disable)
		*tx_disable = val;
	if (rx_disable)
		*rx_disable = 0;
	return 0;
}

/**
 * nfp_phymod_eth_write_disable() - Write PHY Disable state for an eth port
 * @eth:	PHY module ethernet interface
 * @tx_disable:	Disable states for the ethernet port
 * @rx_disable:	Disable states for the ethernet port, not implemented
 *
 * For both rx_disable and tx_disable, 0 = active, 1 = disabled
 *
 * Return: 0, or -ERRNO
 */
int nfp_phymod_eth_write_disable(struct nfp_phymod_eth *eth,
				 u32 tx_disable, u32 rx_disable)
{
	struct eth_priv *priv;
	struct nfp_nsp *nsp;
	int idx, err;
	u64 control;

	priv = (struct eth_priv *)eth;
	idx = priv->eth_idx;
	control = le64_to_cpu(priv->eths[idx].control);

	if (!tx_disable == !!(NSP_ETH_TX_STATE_ENABLED & control))
		return 0;

	nsp = nfp_nsp_open(nfp_device_cpp(priv->nfp));
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	if (tx_disable)
		control &= ~NSP_ETH_TX_STATE_ENABLED;
	else
		control |= NSP_ETH_TX_STATE_ENABLED;

	priv->eths[idx].control = cpu_to_le64(control);

	err = nfp_nsp_command_buf(nsp, SPCODE_ETH_CONTROL,
				  sizeof(priv->eths),
				  priv->eths, sizeof(priv->eths),
				  priv->eths, sizeof(priv->eths));
	if (err)
		return -1;
	return 0;
}
