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
 * nfp_nffw.h
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#ifndef NFP_NFFW_H
#define NFP_NFFW_H

/* Implemented in nfp_nffw.c */

int nfp_nffw_info_acquire(struct nfp_device *dev);
int nfp_nffw_info_release(struct nfp_device *dev);
int nfp_nffw_info_fw_mip(struct nfp_device *dev, u8 fwid,
			 u32 *cpp_id, u64 *off);
uint8_t nfp_nffw_info_fwid_first(struct nfp_device *dev);

/* Implemented in nfp_mip.c */

struct nfp_mip;

const struct nfp_mip *nfp_mip(struct nfp_device *dev);
int nfp_mip_probe(struct nfp_device *dev);

int nfp_mip_symtab(const struct nfp_mip *mip, u32 *addr, u32 *size);
int nfp_mip_strtab(const struct nfp_mip *mip, u32 *addr, u32 *size);

/* Implemented in nfp_rtsym.c */

#define NFP_RTSYM_TYPE_NONE		(0)
#define NFP_RTSYM_TYPE_OBJECT		(1)
#define NFP_RTSYM_TYPE_FUNCTION		(2)
#define NFP_RTSYM_TYPE_ABS		(3)

#define NFP_RTSYM_TARGET_NONE		(0)
#define NFP_RTSYM_TARGET_LMEM		(-1)
#define NFP_RTSYM_TARGET_USTORE		(-2)
#define NFP_RTSYM_TARGET_EMU_CACHE	(-7)

/**
 * struct nfp_rtsym - RTSYM descriptor
 * @name:		Symbol name
 * @addr:		Address in the domain/target's address space
 * @size:		Size (in bytes) of the symbol
 * @type:		NFP_RTSYM_TYPE_* of the symbol
 * @target:		CPP Target identifier, or NFP_RTSYM_TARGET_*
 * @domain:		CPP Target Domain (island)
 */
struct nfp_rtsym {
	const char *name;
	u64 addr;
	u64 size;
	int type;
	int target;
	int domain;
};

void nfp_rtsym_reload(struct nfp_device *nfp);
int nfp_rtsym_count(struct nfp_device *dev);
const struct nfp_rtsym *nfp_rtsym_get(struct nfp_device *nfp, int idx);
const struct nfp_rtsym *nfp_rtsym_lookup(struct nfp_device *nfp,
					 const char *name);
u64 nfp_rtsym_read_le(struct nfp_device *nfp, const char *name, int *error);

#endif /* NFP_NFFW_H */
