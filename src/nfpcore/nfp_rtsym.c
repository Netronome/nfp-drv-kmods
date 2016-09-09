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
 * nfp_rtsym.c
 * Interface for accessing run-time symbol table
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */
#define NFP6000_LONGNAMES

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
#include <asm-generic/io-64-nonatomic-hi-lo.h>
#else
#include <linux/io-64-nonatomic-hi-lo.h>
#endif

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_nffw.h"

#include "nfp3200/nfp3200.h"

/* These need to match the linker */
#define _SYM_TGT_LMEM	   0
#define _SYM_TGT_UMEM	   0xFE /* Only NFP-32xx */
#define _SYM_TGT_EMU_CACHE  0x17

struct _rtsym {
	u8  type;
	u8  target;
	union {
		u8  val;
		/* N/A, island or linear menum, depends on 'target' */
		u8  nfp3200_domain;
		/* 0xff if N/A */
		u8  nfp6000_island;
	} domain1;
	u8  addr_hi;
	u32 addr_lo;
	u16 name;
	union {
		u8  val;
		u8  nfp3200___rsvd;
		/* 0xff if N/A */
		u8  nfp6000_menum;
	} domain2;
	u8  size_hi;
	u32 size_lo;
};

struct nfp_rtsym_priv {
	const struct nfp_mip *mip;
	int numrtsyms;
	struct nfp_rtsym *rtsymtab;
	char *rtstrtab;
};

static void nfp_rtsym_priv_des(void *data)
{
	struct nfp_rtsym_priv *priv = data;

	kfree(priv->rtsymtab);
	kfree(priv->rtstrtab);
}

static void *nfp_rtsym_priv_con(struct nfp_device *dev)
{
	struct nfp_rtsym_priv *priv;

	priv = nfp_device_private_alloc(dev, sizeof(*priv), nfp_rtsym_priv_des);

	return priv;
}

static int nfp3200_melin2meid(u8 melinnum)
{
	u8 cluster_num = melinnum >> 3;
	u8 menum = melinnum & 0x7;

	return cluster_num < 5 ? (cluster_num << 4) | menum | 0x8 : -1;
}

static int nfp6000_meid(u8 island_id, u8 menum)
{
	return (island_id & 0x3F) == island_id && menum < 12 ?
		(island_id << 4) | (menum + 4) : -1;
}

static int __nfp_rtsymtab_probe(struct nfp_device *dev,
				struct nfp_rtsym_priv *priv)
{
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	struct _rtsym *rtsymtab;
	u32 *wptr;
	int err, n;
	u32 model = nfp_cpp_model(cpp);
	const u32 dram = NFP_CPP_ID(NFP_CPP_TARGET_MU,
					 NFP_CPP_ACTION_RW, 0);
	u32 strtab_addr, symtab_addr;
	u32 strtab_size, symtab_size;

	if (!priv->mip) {
		priv->mip = nfp_mip(dev);
		if (!priv->mip)
			return -ENODEV;
	}

	err = nfp_mip_strtab(priv->mip, &strtab_addr, &strtab_size);
	if (err < 0)
		return err;

	err = nfp_mip_symtab(priv->mip, &symtab_addr, &symtab_size);
	if (err < 0)
		return err;

	if (symtab_size == 0 || strtab_size == 0 ||
	    (symtab_size % sizeof(*rtsymtab)) != 0)
		return -ENXIO;

	/* Align to 64 bits */
	symtab_size = (symtab_size + 7) & ~7;
	strtab_size = (strtab_size + 7) & ~7;

	rtsymtab = kmalloc(symtab_size, GFP_KERNEL);
	if (!rtsymtab)
		return -ENOMEM;

	priv->numrtsyms = symtab_size / sizeof(*rtsymtab);
	priv->rtsymtab = kmalloc_array(priv->numrtsyms,
				       sizeof(struct nfp_rtsym),
				       GFP_KERNEL);
	if (!priv->rtsymtab) {
		err = -ENOMEM;
		goto err_symtab;
	}

	priv->rtstrtab = kmalloc(strtab_size + 1, GFP_KERNEL);
	if (!priv->rtstrtab) {
		err = -ENOMEM;
		goto err_strtab;
	}

	if (NFP_CPP_MODEL_IS_3200(model)) {
		err = nfp_cpp_read(cpp, dram, symtab_addr,
				   rtsymtab, symtab_size);
		if (err < symtab_size)
			goto err_read_symtab;

		err = nfp_cpp_read(cpp, dram, strtab_addr,
				   priv->rtstrtab, strtab_size);
		if (err < strtab_size)
			goto err_read_strtab;
		priv->rtstrtab[strtab_size] = '\0';

		for (wptr = (u32 *)rtsymtab, n = 0;
		     n < priv->numrtsyms; n++)
			wptr[n] = le32_to_cpu(wptr[n]);

		for (n = 0; n < priv->numrtsyms; n++) {
			priv->rtsymtab[n].type = rtsymtab[n].type;
			priv->rtsymtab[n].name = priv->rtstrtab +
				(rtsymtab[n].name % strtab_size);
			priv->rtsymtab[n].addr =
				(((uint64_t)rtsymtab[n].addr_hi) << 32) +
				rtsymtab[n].addr_lo;
			priv->rtsymtab[n].size =
				(((uint64_t)rtsymtab[n].size_hi) << 32) +
				rtsymtab[n].size_lo;
			switch (rtsymtab[n].target) {
			case _SYM_TGT_LMEM:
				priv->rtsymtab[n].target =
					NFP_RTSYM_TARGET_LMEM;
				priv->rtsymtab[n].domain = nfp3200_melin2meid(
					rtsymtab[n].domain1.nfp3200_domain);
				break;
			case _SYM_TGT_UMEM:
				priv->rtsymtab[n].target =
					NFP_RTSYM_TARGET_USTORE;
				priv->rtsymtab[n].domain = nfp3200_melin2meid(
					rtsymtab[n].domain1.nfp3200_domain);
				break;
			default:
				priv->rtsymtab[n].target = rtsymtab[n].target;
				priv->rtsymtab[n].domain =
					rtsymtab[n].domain1.nfp3200_domain;
				break;
			}
		}
	} else if (NFP_CPP_MODEL_IS_6000(model)) {
		err = nfp_cpp_read(cpp, dram | 24, symtab_addr,
				   rtsymtab, symtab_size);
		if (err != symtab_size)
			goto err_read_symtab;

		err = nfp_cpp_read(cpp, dram | 24, strtab_addr,
				   priv->rtstrtab, strtab_size);
		if (err != strtab_size)
			goto err_read_strtab;
		priv->rtstrtab[strtab_size] = '\0';

		for (wptr = (u32 *)rtsymtab, n = 0;
		     n < priv->numrtsyms; n++)
			wptr[n] = le32_to_cpu(wptr[n]);

		for (n = 0; n < priv->numrtsyms; n++) {
			priv->rtsymtab[n].type = rtsymtab[n].type;
			priv->rtsymtab[n].name = priv->rtstrtab +
				(rtsymtab[n].name % strtab_size);
			priv->rtsymtab[n].addr = (((uint64_t)
						rtsymtab[n].addr_hi) << 32) +
				rtsymtab[n].addr_lo;
			priv->rtsymtab[n].size = (((uint64_t)
						rtsymtab[n].size_hi) << 32) +
				rtsymtab[n].size_lo;

			switch (rtsymtab[n].target) {
			case _SYM_TGT_LMEM:
				priv->rtsymtab[n].target =
					NFP_RTSYM_TARGET_LMEM;
				break;
			case _SYM_TGT_EMU_CACHE:
				priv->rtsymtab[n].target =
					NFP_RTSYM_TARGET_EMU_CACHE;
				break;
			case _SYM_TGT_UMEM:
				goto err_read_symtab;
			default:
				priv->rtsymtab[n].target = rtsymtab[n].target;
				break;
			}

			if (rtsymtab[n].domain2.nfp6000_menum != 0xff)
				priv->rtsymtab[n].domain = nfp6000_meid(
					rtsymtab[n].domain1.nfp6000_island,
					rtsymtab[n].domain2.nfp6000_menum);
			else if (rtsymtab[n].domain1.nfp6000_island != 0xff)
				priv->rtsymtab[n].domain =
					rtsymtab[n].domain1.nfp6000_island;
			else
				priv->rtsymtab[n].domain = -1;
		}
	} else {
		err = -EINVAL;
		goto err_read_symtab;
	}

	kfree(rtsymtab);
	return 0;

err_read_strtab:
err_read_symtab:
	kfree(priv->rtstrtab);
	priv->rtstrtab = NULL;
err_strtab:
	kfree(priv->rtsymtab);
	priv->rtsymtab = NULL;
	priv->numrtsyms = 0;
err_symtab:
	kfree(rtsymtab);
	return err;
}

/**
 * nfp_rtsym_count() - Get the number of RTSYM descriptors
 * @dev:		NFP Device handle
 *
 * Return: Number of RTSYM descriptors, or -ERRNO
 */
int nfp_rtsym_count(struct nfp_device *dev)
{
	struct nfp_rtsym_priv *priv = nfp_device_private(dev,
							 nfp_rtsym_priv_con);
	int err;

	if (!priv->rtsymtab) {
		err = __nfp_rtsymtab_probe(dev, priv);
		if (err < 0)
			return err;
	}

	return priv->numrtsyms;
}

/**
 * nfp_rtsym_get() - Get the Nth RTSYM descriptor
 * @dev:		NFP Device handle
 * @idx:		Index (0-based) of the RTSYM descriptor
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *nfp_rtsym_get(struct nfp_device *dev, int idx)
{
	struct nfp_rtsym_priv *priv = nfp_device_private(dev,
							 nfp_rtsym_priv_con);
	int err;

	if (!priv->rtsymtab) {
		err = __nfp_rtsymtab_probe(dev, priv);
		if (err < 0)
			return NULL;
	}

	if (idx >= priv->numrtsyms)
		return NULL;

	return &priv->rtsymtab[idx];
}

/**
 * nfp_rtsym_lookup() - Return the RTSYM descriptor for a symbol name
 * @dev:		NFP Device handle
 * @name:		Symbol name
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *nfp_rtsym_lookup(struct nfp_device *dev,
					 const char *name)
{
	struct nfp_rtsym_priv *priv = nfp_device_private(dev,
							 nfp_rtsym_priv_con);
	int err, n;

	if (!priv->rtsymtab) {
		err = __nfp_rtsymtab_probe(dev, priv);
		if (err < 0)
			return NULL;
	}

	for (n = 0; n < priv->numrtsyms; n++) {
		if (strcmp(name, priv->rtsymtab[n].name) == 0)
			return &priv->rtsymtab[n];
	}

	return NULL;
}

/**
 * nfp_rtsym_read_le() - Read a simple unsigned scalar value from symbol
 * @nfp:	NFP Device handle
 * @name:	Symbol name
 * @error:	Poniter to error code (optional)
 *
 * Lookup a symbol, map, read it and return it's value. Value of the symbol
 * will be interpreted as a simple little-endian unsigned value. Symbol can
 * be 1, 2, 4 or 8 bytes in size.
 *
 * Return: value read, on error sets the error and returns ~0ULL.
 */
u64 nfp_rtsym_read_le(struct nfp_device *nfp, const char *name, int *error)
{
	const struct nfp_rtsym *sym;
	struct nfp_cpp_area *area;
	void __iomem *ptr;
	int err;
	u64 val;
	u32 id;

	sym = nfp_rtsym_lookup(nfp, name);
	if (!sym) {
		err = -ENOENT;
		goto err;
	}

	id = NFP_CPP_ISLAND_ID(sym->target, NFP_CPP_ACTION_RW, 0, sym->domain);
	area = nfp_cpp_area_alloc_acquire(nfp_device_cpp(nfp), id, sym->addr,
					  sym->size);
	if (IS_ERR_OR_NULL(area)) {
		err = area ? PTR_ERR(area) : -ENOMEM;
		goto err;
	}

	ptr = nfp_cpp_area_iomem(area);
	if (IS_ERR_OR_NULL(ptr)) {
		err = ptr ? PTR_ERR(ptr) : -ENOMEM;
		goto err_release_free;
	}

	switch (sym->size) {
	case 1:
		val = readb(ptr);
		break;
	case 2:
		val = readw(ptr);
		break;
	case 4:
		val = readl(ptr);
		break;
	case 8:
		val = readq(ptr);
		break;
	default:
		nfp_err(nfp, "rtsym '%s' non-scalar size: %lld\n",
			name, sym->size);
		err = -EINVAL;
		goto err_release_free;
	}

	nfp_cpp_area_release_free(area);

	return val;

err_release_free:
	nfp_cpp_area_release_free(area);
err:
	if (error)
		*error = err;
	return ~0ULL;
}

/**
 * nfp_rtsym_reload() - Force a reload of the RTSYM table
 * @dev:	NFP Device handle
 */
void nfp_rtsym_reload(struct nfp_device *dev)
{
	struct nfp_rtsym_priv *priv = nfp_device_private(dev,
							 nfp_rtsym_priv_con);
	kfree(priv->rtsymtab);
	kfree(priv->rtstrtab);
	priv->numrtsyms = 0;
}
