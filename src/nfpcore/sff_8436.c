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
 * sff_8436.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 * QSFP support
 */

/* Included from nfp_nbi_phymod.c - not compiled separately! */
#ifdef NFP_NBI_PHYMOD_C

/* SFF-8436 operations */
struct sff_8436 {
	int selected;
	int page;
	struct {
		struct pin irq, present;
	} in;
	struct {
		struct pin modsel, reset, lp_mode;
	} out;
	struct sff_bus bus;
};

static int sff_8436_open(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff;
	int err;

	sff = kzalloc(sizeof(*sff), GFP_KERNEL);
	if (!sff)
		return -ENOMEM;

	sff->selected = 0;
	sff->page = -1;

	err = _phymod_get_attr_pin(phy, "pin.irq", &sff->in.irq);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.present", &sff->in.present);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.modsel", &sff->out.modsel);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.reset", &sff->out.reset);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_pin(phy, "pin.lp_mode", &sff->out.lp_mode);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.irq, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.present, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->out.modsel, 1);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->out.reset, 1);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->out.lp_mode, 1);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_bus(phy, "SFF-8436", &sff->bus);
	if (err < 0)
		goto exit;

	phy->sff.priv = sff;

	return 0;

exit:
	kfree(sff);
	return err;
}

static void sff_8436_close(struct nfp_phymod *phy)
{
	struct sff_8436 *sff = phy->sff.priv;

	phy->sff.op->select(phy, 0);
	if (sff->bus.op && sff->bus.op->close)
		sff->bus.op->close(&sff->bus);

	kfree(sff);
}

static int sff_8436_poll_irq(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff = phy->sff.priv;
	int err;

	err = pin_get(priv->nfp, &sff->in.irq);
	if (err < 0)
		return err;

	return err ? 0 : 1;
}

static int sff_8436_poll_present(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff = phy->sff.priv;
	int err;

	err = pin_get(priv->nfp, &sff->in.present);
	if (err < 0)
		return err;

	return err ? 0 : 1;
}

static int sff_8436_select(struct nfp_phymod *phy, int is_selected)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff = phy->sff.priv;
	int err;

	if (sff->bus.op && sff->bus.op->select) {
		err = sff->bus.op->select(&sff->bus, is_selected);
		if (err < 0)
			return err;
	}

	err = pin_set(priv->nfp, &sff->out.modsel, is_selected ? 0 : 1);
	if (err < 0)
		return err;

	sff->selected = is_selected;

	return 0;
}

static int sff_8436_reset(struct nfp_phymod *phy, int in_reset)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff = phy->sff.priv;

	return pin_set(priv->nfp, &sff->out.reset, in_reset ? 1 : 0);
}

static int sff_8436_power(struct nfp_phymod *phy, int is_full_power)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8436 *sff = phy->sff.priv;

	return pin_set(priv->nfp, &sff->out.lp_mode, is_full_power ? 0 : 1);
}

static int sff_8436_read8(struct nfp_phymod *phy, u32 reg, u8 *val)
{
	struct sff_8436 *sff = phy->sff.priv;
	int page = (reg >> 8);

	if (!sff->selected ||
	    !sff->bus.op || !sff->bus.op->read8 || !sff->bus.op->write8)
		return -EINVAL;

	reg &= 0xff;

	if (page != sff->page) {
		sff->bus.op->write8(&sff->bus, reg, page);
		sff->page = page;
	}

	return sff->bus.op->read8(&sff->bus, reg, val);
}

static int sff_8436_write8(struct nfp_phymod *phy, u32 reg, u8 val)
{
	struct sff_8436 *sff = phy->sff.priv;
	int page = (reg >> 8);

	if (!sff->selected || !sff->bus.op || !sff->bus.op->write8)
		return -EINVAL;

	reg &= 0xff;

	if (page != sff->page) {
		sff->bus.op->write8(&sff->bus, reg, page);
		sff->page = page;
	}

	return sff->bus.op->write8(&sff->bus, reg, val);
}

#define SFF_8436_ID		0
#define SFF_8436_STATUS		2
#define SFF_8436_LOS		3
#define SFF_8436_FAULT_TX	4
#define SFF_8436_TEMP		6
#define SFF_8436_VCC		7
#define SFF_8436_POWER_RX_12	9
#define SFF_8436_POWER_RX_34	10
#define SFF_8436_BIAS_TX_12	11
#define SFF_8436_BIAS_TX_34	12
#define SFF_8436_DISABLE_TX	86

static int sff_8436_status_los(struct nfp_phymod *phy,
			       u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_LOS, &tmp);
	if (err < 0)
		return err;

	/* Check RX LOS status bits */
	if (tmp & 0x0f)
		rxs |= tmp & 0xf;
	/* Check TX LOS status bits */
	if (tmp & 0xf0)
		txs |= (tmp >> 4) & 0xf;

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_status_fault(struct nfp_phymod *phy,
				 u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_FAULT_TX, &tmp);
	if (err < 0)
		return err;

	if (tmp & 0xf)
		txs |= tmp & 0xf;

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_status_power(struct nfp_phymod *phy,
				 u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_POWER_RX_12, &tmp);
	if (err < 0)
		return err;

	if (tmp)
		rxs |= tmp;

	err = nfp_phymod_read8(phy, SFF_8436_POWER_RX_34, &tmp);
	if (err < 0)
		return err;

	if (tmp)
		rxs |= (u32)tmp << 8;

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_status_bias(struct nfp_phymod *phy,
				u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_BIAS_TX_12, &tmp);
	if (err < 0)
		return err;

	if (tmp)
		txs |= tmp;

	err = nfp_phymod_read8(phy, SFF_8436_BIAS_TX_34, &tmp);
	if (err < 0)
		return err;

	if (tmp)
		txs |= (u32)tmp << 8;

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_status_volt(struct nfp_phymod *phy,
				u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_VCC, &tmp);
	if (err < 0)
		return err;

	if (tmp & 0xf0) {
		txs |= (tmp >> 4) & 0xf;
		rxs |= (tmp >> 4) & 0xf;
	}

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_status_temp(struct nfp_phymod *phy,
				u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_TEMP, &tmp);
	if (err < 0)
		return err;

	if (tmp & 0xf0) {
		txs |= (tmp >> 4) & 0xf;
		rxs |= (tmp >> 4) & 0xf;
	}

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_get_lane_dis(struct nfp_phymod *phy,
				 u32 *tx_status, u32 *rx_status)
{
	u8 tmp;
	int err;
	u32 rxs = 0, txs = 0;

	err = nfp_phymod_read8(phy, SFF_8436_DISABLE_TX, &tmp);
	if (err < 0)
		return err;

	txs = (tmp & 0x0f);

	if (tx_status)
		*tx_status = txs;

	if (rx_status)
		*rx_status = rxs;

	return 0;
}

static int sff_8436_set_lane_dis(struct nfp_phymod *phy,
				 u32 tx_status, u32 rx_status)
{
	return nfp_phymod_write8(phy, SFF_8436_DISABLE_TX, tx_status & 0xf);
}

static const struct sff_ops sff_8436_ops = {
	.type = 8436,
	.open = sff_8436_open,
	.close = sff_8436_close,
	.poll_present = sff_8436_poll_present,
	.poll_irq = sff_8436_poll_irq,
	.select = sff_8436_select,
	.reset = sff_8436_reset,
	.power = sff_8436_power,

	.read8 = sff_8436_read8,
	.write8 = sff_8436_write8,

	.status_los = sff_8436_status_los,
	.status_fault = sff_8436_status_fault,
	.status_power = sff_8436_status_power,
	.status_bias = sff_8436_status_bias,
	.status_volt = sff_8436_status_volt,
	.status_temp = sff_8436_status_temp,

	.get_lane_dis = sff_8436_get_lane_dis,
	.set_lane_dis = sff_8436_set_lane_dis,
};

#endif /* NFP_NBI_PHYMOD_C */
