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

/* These need to match the linker */
#define _SYM_TGT_LMEM	   0
#define _SYM_TGT_UMEM	   0xFE /* Only NFP-32xx */
#define _SYM_TGT_EMU_CACHE  0x17

struct _rtsym {
	u8  type;
	u8  target;
	union {
		u8  val;
		/* 0xff if N/A */
		u8  nfp6000_island;
	} domain1;
	u8  addr_hi;
	u32 addr_lo;
	u16 name;
	union {
		u8  val;
		u8  nfp6000_menum;
	} domain2;
	u8  size_hi;
	u32 size_lo;
};

struct nfp_rtsym_cache {
	int num;
	char *strtab;
	struct nfp_rtsym symtab[];
};

static int nfp6000_meid(u8 island_id, u8 menum)
{
	return (island_id & 0x3F) == island_id && menum < 12 ?
		(island_id << 4) | (menum + 4) : -1;
}

static int __nfp_rtsymtab_probe(struct nfp_cpp *cpp)
{
	const u32 dram = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0);
	struct nfp_rtsym_cache *cache;
	u32 strtab_addr, symtab_addr;
	u32 strtab_size, symtab_size;
	const struct nfp_mip *mip;
	struct _rtsym *rtsymtab;
	int err, n, size;
	u32 *wptr;

	mip = nfp_mip(cpp);
	if (!mip)
		return -EIO;

	err = nfp_mip_strtab(mip, &strtab_addr, &strtab_size);
	if (err < 0)
		return err;

	err = nfp_mip_symtab(mip, &symtab_addr, &symtab_size);
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

	size = sizeof(*cache);
	size += symtab_size / sizeof(*rtsymtab) * sizeof(struct nfp_rtsym);
	size +=	strtab_size + 1;
	cache = kmalloc(size, GFP_KERNEL);
	if (!cache) {
		err = -ENOMEM;
		goto err_free_rtsym_raw;
	}

	cache->num = symtab_size / sizeof(*rtsymtab);
	cache->strtab = (void *)&cache->symtab[cache->num];

	err = nfp_cpp_read(cpp, dram | 24, symtab_addr, rtsymtab, symtab_size);
	if (err != symtab_size)
		goto err_free_cache;

	err = nfp_cpp_read(cpp, dram | 24, strtab_addr,
			   cache->strtab, strtab_size);
	if (err != strtab_size)
		goto err_free_cache;
	cache->strtab[strtab_size] = '\0';

	for (wptr = (u32 *)rtsymtab, n = 0; n < cache->num; n++)
		wptr[n] = le32_to_cpu(wptr[n]);

	for (n = 0; n < cache->num; n++) {
		cache->symtab[n].type = rtsymtab[n].type;
		cache->symtab[n].name = cache->strtab +
			(rtsymtab[n].name % strtab_size);
		cache->symtab[n].addr = (((u64)rtsymtab[n].addr_hi) << 32) +
			rtsymtab[n].addr_lo;
		cache->symtab[n].size = (((u64)rtsymtab[n].size_hi) << 32) +
			rtsymtab[n].size_lo;

		switch (rtsymtab[n].target) {
		case _SYM_TGT_LMEM:
			cache->symtab[n].target = NFP_RTSYM_TARGET_LMEM;
			break;
		case _SYM_TGT_EMU_CACHE:
			cache->symtab[n].target = NFP_RTSYM_TARGET_EMU_CACHE;
			break;
		case _SYM_TGT_UMEM:
			goto err_free_cache;
		default:
			cache->symtab[n].target = rtsymtab[n].target;
			break;
		}

		if (rtsymtab[n].domain2.nfp6000_menum != 0xff)
			cache->symtab[n].domain = nfp6000_meid(
				rtsymtab[n].domain1.nfp6000_island,
				rtsymtab[n].domain2.nfp6000_menum);
		else if (rtsymtab[n].domain1.nfp6000_island != 0xff)
			cache->symtab[n].domain =
				rtsymtab[n].domain1.nfp6000_island;
		else
			cache->symtab[n].domain = -1;
	}

	kfree(rtsymtab);
	nfp_rtsym_cache_set(cpp, cache);
	return 0;

err_free_cache:
	kfree(cache);
err_free_rtsym_raw:
	kfree(rtsymtab);
	return err;
}

static struct nfp_rtsym_cache *nfp_rtsym(struct nfp_cpp *cpp)
{
	struct nfp_rtsym_cache *cache;
	int err;

	cache = nfp_rtsym_cache(cpp);
	if (cache)
		return cache;

	err = __nfp_rtsymtab_probe(cpp);
	if (err < 0)
		return ERR_PTR(err);

	return nfp_rtsym_cache(cpp);
}

/**
 * nfp_rtsym_count() - Get the number of RTSYM descriptors
 * @cpp:		NFP CPP handle
 *
 * Return: Number of RTSYM descriptors, or -ERRNO
 */
int nfp_rtsym_count(struct nfp_cpp *cpp)
{
	struct nfp_rtsym_cache *cache;

	cache = nfp_rtsym(cpp);
	if (IS_ERR(cache))
		return PTR_ERR(cache);

	return cache->num;
}

/**
 * nfp_rtsym_get() - Get the Nth RTSYM descriptor
 * @cpp:		NFP CPP handle
 * @idx:		Index (0-based) of the RTSYM descriptor
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *nfp_rtsym_get(struct nfp_cpp *cpp, int idx)
{
	struct nfp_rtsym_cache *cache;

	cache = nfp_rtsym(cpp);
	if (IS_ERR(cache))
		return NULL;

	if (idx >= cache->num)
		return NULL;

	return &cache->symtab[idx];
}

/**
 * nfp_rtsym_lookup() - Return the RTSYM descriptor for a symbol name
 * @cpp:		NFP CPP handle
 * @name:		Symbol name
 *
 * Return: const pointer to a struct nfp_rtsym descriptor, or NULL
 */
const struct nfp_rtsym *nfp_rtsym_lookup(struct nfp_cpp *cpp, const char *name)
{
	struct nfp_rtsym_cache *cache;
	int n;

	cache = nfp_rtsym(cpp);
	if (IS_ERR(cache))
		return NULL;

	for (n = 0; n < cache->num; n++) {
		if (strcmp(name, cache->symtab[n].name) == 0)
			return &cache->symtab[n];
	}

	return NULL;
}

/**
 * nfp_rtsym_read_le() - Read a simple unsigned scalar value from symbol
 * @cpp:	NFP CPP handle
 * @name:	Symbol name
 * @error:	Poniter to error code (optional)
 *
 * Lookup a symbol, map, read it and return it's value. Value of the symbol
 * will be interpreted as a simple little-endian unsigned value. Symbol can
 * be 1, 2, 4 or 8 bytes in size.
 *
 * Return: value read, on error sets the error and returns ~0ULL.
 */
u64 nfp_rtsym_read_le(struct nfp_cpp *cpp, const char *name, int *error)
{
	const struct nfp_rtsym *sym;
	struct nfp_cpp_area *area;
	void __iomem *ptr;
	int err;
	u64 val;
	u32 id;

	sym = nfp_rtsym_lookup(cpp, name);
	if (!sym) {
		err = -ENOENT;
		goto err;
	}

	id = NFP_CPP_ISLAND_ID(sym->target, NFP_CPP_ACTION_RW, 0, sym->domain);
	area = nfp_cpp_area_alloc_acquire(cpp, id, sym->addr, sym->size);
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
		nfp_cpp_err(cpp, "rtsym '%s' non-scalar size: %lld\n",
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
 * @cpp:	NFP CPP handle
 */
void nfp_rtsym_reload(struct nfp_cpp *cpp)
{
	kfree(nfp_rtsym_cache(cpp));
	nfp_rtsym_cache_set(cpp, NULL);
}
