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

#define SFF_8647_EXT_ID                  129
#define SFF_8647_CONNECTOR               130
#define SFF_8647_DEVICE_TECH             147
#define SFF_8647_CABLE_LEN               150
#define SFF_8647_VENDOR                  152
#define SFF_8647_VENDOR_OUI              168
#define SFF_8647_VENDOR_PN               171
#define SFF_8647_VENDOR_SN               189
#define SFF_8647_DATECODE                205
#define SFF_8647_CC                      223
#define  SFF_8647_CC_START               128
#define  SFF_8647_CC_END                 222

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

static int sff_8647_read_vendor(struct nfp_phymod *phy, char *name, u32 sz)
{
	return read_sff_string(phy, name, sz, 16, SFF_8647_VENDOR);
}

static int sff_8647_read_product(struct nfp_phymod *phy, char *prod, u32 sz)
{
	return read_sff_string(phy, prod, sz, 16, SFF_8647_VENDOR_PN);
}

static int sff_8647_read_serial(struct nfp_phymod *phy, char *serial, u32 sz)
{
	return read_sff_string(phy, serial, sz, 16, SFF_8647_VENDOR_SN);
}

static int sff_8647_read_vendor_oui(struct nfp_phymod *phy, u32 *oui)
{
	return read_vendor_oui(phy, oui, SFF_8647_VENDOR_OUI);
}

static int sff_8647_read_connector(struct nfp_phymod *phy, int *connector)
{
	u8 tmp;

	if (nfp_phymod_read8(phy, SFF_8647_CONNECTOR, &tmp) < 0)
		return -1;
	*connector = tmp;
	return 0;
}

static int sff_8647_verify_checkcodes(struct nfp_phymod *phy, int *ccs)
{
	int err, cnt;
	u8 cc;

	if (!ccs)
		return -1;

	if (nfp_phymod_read8(phy, SFF_8647_CC, &cc) < 0)
		return -1;
	cnt = 1 + SFF_8647_CC_END - SFF_8647_CC_START;
	err = verify_sff_checkcode(phy, SFF_8647_CC_START, cnt, cc);
	if (err < 0)
		return -1;
	*ccs = (err & 1) << 0;

	return 0;
}

static int sff_8647_read_length(struct nfp_phymod *phy, int *length)
{
	u8 hi, lo;

	if (nfp_phymod_read8(phy, SFF_8647_CABLE_LEN + 0, &hi) < 0)
		return -1;
	if (nfp_phymod_read8(phy, SFF_8647_CABLE_LEN + 1, &lo) < 0)
		return -1;
	*length = ((hi << 8) | lo) / 2;
	return 0;
}

static int sff_8647_get_active_or_passive(struct nfp_phymod *phy, int *anp)
{
	int con;

	if (sff_8647_read_connector(phy, &con))
		return -1;
	*anp = (con == 0x31 || con == 0x32) ? 1 : 0;
	return 0;
}

static int sff_8647_read8(struct nfp_phymod *phy, u32 reg, u8 *val)
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

static int sff_8647_write8(struct nfp_phymod *phy, u32 reg, u8 val)
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

	.read_connector = sff_8647_read_connector,
	.read_vendor    = sff_8647_read_vendor,
	.read_vend_oui  = sff_8647_read_vendor_oui,
	.read_product   = sff_8647_read_product,
	.read_serial    = sff_8647_read_serial,
	.read_length    = sff_8647_read_length,
	.get_active_or_passive = sff_8647_get_active_or_passive,
	.verify_checkcodes = sff_8647_verify_checkcodes,

	.read8 = sff_8647_read8,
	.write8 = sff_8647_write8,
};

#endif /* NFP_NBI_PHYMOD_C */
