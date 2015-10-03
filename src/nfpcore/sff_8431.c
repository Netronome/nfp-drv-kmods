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
 * sff_8431.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *
 * QSFP support
 */

/* Included from nfp_nbi_phymod.c - not compiled separately! */
#ifdef NFP_NBI_PHYMOD_C

/* SFF-8431 operations - built off of the SFF-8431 operations */
struct sff_8431 {
	struct sff_bus bus[2];
	int page;
	int selected;
	int tx_disable;
	struct {
		struct pin present;
		struct pin rx_los;
		struct pin tx_fault;
	} in;
	struct {
		struct pin tx_disable;
	} out;
};

static int sff_8431_open(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8431 *sff;
	int err;

	sff = kzalloc(sizeof(*sff), GFP_KERNEL);
	if (!sff)
		return -ENOMEM;

	sff->selected = 0;

	err = _phymod_get_attr_pin(phy, "pin.present", &sff->in.present);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.tx_fault", &sff->in.tx_fault);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.rx_los", &sff->in.rx_los);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.tx_disable", &sff->out.tx_disable);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.present, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.tx_fault, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.rx_los, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->out.tx_disable, 1);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_bus(phy, "SFF-8431", &sff->bus[0]);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_bus(phy, "SFF-8472", &sff->bus[1]);
	/* Optional, so err is not checked */
	if (err < 0) {
		err = 0;
		sff->bus[1].op = NULL;
	}

	phy->sff.priv = sff;

	return 0;

exit:
	kfree(sff);
	return err;
}

static void sff_8431_close(struct nfp_phymod *phy)
{
	struct sff_8431 *sff = phy->sff.priv;
	int i;

	phy->sff.op->select(phy, 0);
	for (i = 0; i < ARRAY_SIZE(sff->bus); i++) {
		struct sff_bus *bus = &sff->bus[i];

		if (bus->op && bus->op->close)
			bus->op->close(bus);
	}

	kfree(sff);
}

static int sff_8431_poll_present(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8431 *sff = phy->sff.priv;
	int err;

	err = pin_get(priv->nfp, &sff->in.present);
	if (err < 0)
		return err;

	return err ? 0 : 1;
}

static int sff_8431_select(struct nfp_phymod *phy, int is_selected)
{
	struct sff_8431 *sff = phy->sff.priv;
	struct sff_bus *bus = &sff->bus[sff->page];
	int err;

	if (bus->op && bus->op->select) {
		err = bus->op->select(bus, is_selected);
		if (err < 0)
			return err;
	}

	sff->selected = is_selected;

	return 0;
}

static int sff_8431_read8(struct nfp_phymod *phy, u32 reg, u8 *val)
{
	struct sff_8431 *sff = phy->sff.priv;
	int page = (reg >> 8);
	struct sff_bus *bus;

	if (page > ARRAY_SIZE(sff->bus))
		return -EINVAL;

	bus = &sff->bus[page];

	if (sff->selected && page != sff->page) {
		sff_8431_select(phy, 0);
		sff->page = page;
		sff_8431_select(phy, 1);
	}

	if (!sff->selected || !bus->op || !bus->op->read8)
		return -EINVAL;

	reg &= 0xff;

	return bus->op->read8(bus, reg, val);
}

static int sff_8431_write8(struct nfp_phymod *phy, u32 reg, uint8_t val)
{
	struct sff_8431 *sff = phy->sff.priv;
	int page = (reg >> 8);
	struct sff_bus *bus;

	if (page > ARRAY_SIZE(sff->bus))
		return -EINVAL;

	bus = &sff->bus[page];

	if (sff->selected && page != sff->page) {
		sff_8431_select(phy, 0);
		sff->page = page;
		sff_8431_select(phy, 1);
	}

	if (!sff->selected || !bus->op || !bus->op->write8)
		return -EINVAL;

	reg &= 0xff;

	return bus->op->write8(bus, reg, val);
}

static int sff_8431_status_los(struct nfp_phymod *phy,
			       u32 *tx_status, u32 *rx_status)
{
	struct sff_8431 *sff = phy->sff.priv;
	int err;

	err = pin_get(phy->priv->nfp, &sff->in.rx_los);
	if (err < 0)
		return err;

	if (tx_status)
		*tx_status = 0;

	if (rx_status)
		*rx_status = err;

	return 0;
}

static int sff_8431_status_fault(struct nfp_phymod *phy,
				 u32 *tx_status, u32 *rx_status)
{
	struct sff_8431 *sff = phy->sff.priv;
	int err;

	err = pin_get(phy->priv->nfp, &sff->in.tx_fault);
	if (err < 0)
		return err;

	if (tx_status)
		*tx_status = err;

	if (rx_status)
		*rx_status = 0;

	return 0;
}

static int sff_8431_get_lane_dis(struct nfp_phymod *phy,
				 u32 *tx_status, u32 *rx_status)
{
	struct sff_8431 *sff = phy->sff.priv;
	u32 rxs = 0, txs = 0;

	txs = sff->tx_disable;

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8431_set_lane_dis(struct nfp_phymod *phy,
				 u32 tx_status, u32 rx_status)
{
	struct sff_8431 *sff = phy->sff.priv;
	int err;

	err = pin_set(phy->priv->nfp, &sff->out.tx_disable, tx_status & 1);
	if (err < 0)
		return err;

	sff->tx_disable = tx_status & 1;
	return 0;
}

static const struct sff_ops sff_8431_ops = {
	.type = 8431,
	.open = sff_8431_open,
	.close = sff_8431_close,
	.select = sff_8431_select,

	.poll_present = sff_8431_poll_present,
	.status_los = sff_8431_status_los,
	.status_fault = sff_8431_status_fault,

	.read8 = sff_8431_read8,
	.write8 = sff_8431_write8,

	.set_lane_dis = sff_8431_set_lane_dis,
	.get_lane_dis = sff_8431_get_lane_dis,
};

#endif /* NFP_NBI_PHYMOD_C */
