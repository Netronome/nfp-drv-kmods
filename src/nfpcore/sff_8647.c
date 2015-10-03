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
 * sff_8647.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 * CXP support
 */

/* Included from nfp_nbi_phymod.c - not compiled separately! */
#ifdef NFP_NBI_PHYMOD_C

/* SFF-8647 operations */
struct sff_8647 {
	int selected;
	int page;
	struct {
		struct pin irq, present;
	} in;
	struct {
		struct pin reset;
	} out;
	struct sff_bus bus;
};

static int sff_8647_open(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8647 *sff;
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

	err = _phymod_get_attr_pin(phy, "pin.reset", &sff->out.reset);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.irq, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->in.present, 0);
	if (err < 0)
		goto exit;

	err = pin_direction(priv->nfp, &sff->out.reset, 1);
	if (err < 0)
		goto exit;

	err = _phymod_get_attr_bus(phy, "SFF-8647", &sff->bus);
	if (err < 0)
		goto exit;

	phy->sff.priv = sff;

	return 0;

exit:
	kfree(sff);
	return err;
}

static void sff_8647_close(struct nfp_phymod *phy)
{
	struct sff_8647 *sff = phy->sff.priv;

	phy->sff.op->select(phy, 0);
	if (sff->bus.op && sff->bus.op->close)
		sff->bus.op->close(&sff->bus);

	kfree(sff);
}

static int sff_8647_poll_irq(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8647 *sff = phy->sff.priv;
	int err;

	err = pin_get(priv->nfp, &sff->in.irq);
	if (err < 0)
		return err;

	return err ? 0 : 1;
}

static int sff_8647_poll_present(struct nfp_phymod *phy)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8647 *sff = phy->sff.priv;
	int err;

	err = pin_get(priv->nfp, &sff->in.present);
	if (err < 0)
		return err;

	return err ? 0 : 1;
}

static int sff_8647_select(struct nfp_phymod *phy, int is_selected)
{
	struct sff_8647 *sff = phy->sff.priv;
	int err;

	if (sff->bus.op && sff->bus.op->select) {
		err = sff->bus.op->select(&sff->bus, is_selected);
		if (err < 0)
			return err;
	}

	sff->selected = is_selected;

	return 0;
}

static int sff_8647_reset(struct nfp_phymod *phy, int in_reset)
{
	struct nfp_phymod_priv *priv = phy->priv;
	struct sff_8647 *sff = phy->sff.priv;

	return pin_set(priv->nfp, &sff->out.reset, in_reset ? 1 : 0);
}

static int sff_8647_read8(struct nfp_phymod *phy, uint32_t reg, uint8_t *val)
{
	struct sff_8647 *sff = phy->sff.priv;
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

static int sff_8647_write8(struct nfp_phymod *phy, uint32_t reg, uint8_t val)
{
	struct sff_8647 *sff = phy->sff.priv;
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

static const struct sff_ops sff_8647_ops = {
	.type = 8647,
	.open = sff_8647_open,
	.close = sff_8647_close,
	.select = sff_8647_select,
	.poll_irq = sff_8647_poll_irq,
	.poll_present = sff_8647_poll_present,
	.reset = sff_8647_reset,

	.read8 = sff_8647_read8,
	.write8 = sff_8647_write8,
};

#endif /* NFP_NBI_PHYMOD_C */
