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
 * nfp_nffw.c
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#include <linux/kernel.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp_nffw.h"

/* Init-CSR owner IDs for firmware map to firmware IDs which start at 4.
 * Lower IDs are reserved for target and loader IDs.
 */
#define NFFW_FWID_EXT   3 /* For active MEs that we didn't load. */
#define NFFW_FWID_BASE  4

#define NFFW_FWID_ALL   255

/**
 * NFFW_INFO_VERSION history:
 * 0: This was never actually used (before versioning), but it refers to
 *    the previous struct which had FWINFO_CNT = MEINFO_CNT = 120 that later
 *    changed to 200.
 * 1: First versioned struct, with
 *     FWINFO_CNT = 120
 *     MEINFO_CNT = 120
 * 2:  FWINFO_CNT = 200
 *     MEINFO_CNT = 200
 */
#define NFFW_INFO_VERSION_CURRENT 2

/* Enough for all current chip families */
#define NFFW_MEINFO_CNT_V1 120
#define NFFW_FWINFO_CNT_V1 120
#define NFFW_MEINFO_CNT_V2 200
#define NFFW_FWINFO_CNT_V2 200

/* Work in 32-bit words to make cross-platform endianness easier to handle */

/** nfp.nffw meinfo **/
struct nffw_meinfo {
	u32 ctxmask__fwid__meid;
};

struct nffw_fwinfo {
	u32 loaded__mu_da__mip_off_hi;
	u32 mip_cppid; /* 0 means no MIP */
	u32 mip_offset_lo;
};

struct nfp_nffw_info_v1 {
	struct nffw_meinfo meinfo[NFFW_MEINFO_CNT_V1];
	struct nffw_fwinfo fwinfo[NFFW_FWINFO_CNT_V1];
};

struct nfp_nffw_info_v2 {
	struct nffw_meinfo meinfo[NFFW_MEINFO_CNT_V2];
	struct nffw_fwinfo fwinfo[NFFW_FWINFO_CNT_V2];
};

/** Resource: nfp.nffw main **/
struct nfp_nffw_info_data {
	u32 flags[2];
	union {
		struct nfp_nffw_info_v1 v1;
		struct nfp_nffw_info_v2 v2;
	} info;
};

struct nfp_nffw_info {
	struct nfp_cpp *cpp;
	struct nfp_resource *res;

	struct nfp_nffw_info_data fwinf;
};

/* NFFW_FWID_BASE is a firmware ID is the index
 * into the table plus this base */

/* flg_info_version = flags[0]<27:16>
 * This is a small version counter intended only to detect if the current
 * implementation can read the current struct. Struct changes should be very
 * rare and as such a 12-bit counter should cover large spans of time. By the
 * time it wraps around, we don't expect to have 4096 versions of this struct
 * to be in use at the same time.
 */
static u32 nffw_res_info_version_get(struct nfp_nffw_info_data *res)
{
	return (res->flags[0] >> 16) & 0xfff;
}

/* loaded = loaded__mu_da__mip_off_hi<31:31> */
static u32 nffw_fwinfo_loaded_get(struct nffw_fwinfo *fi)
{
	return (fi->loaded__mu_da__mip_off_hi >> 31) & 1;
}

/* mip_cppid = mip_cppid */
static u32 nffw_fwinfo_mip_cppid_get(struct nffw_fwinfo *fi)
{
	return fi->mip_cppid;
}

/* loaded = loaded__mu_da__mip_off_hi<8:8> */
static u32 nffw_fwinfo_mip_mu_da_get(struct nffw_fwinfo *fi)
{
	return (fi->loaded__mu_da__mip_off_hi >> 8) & 1;
}

/* mip_offset = (loaded__mu_da__mip_off_hi<7:0> << 8) | mip_offset_lo */
static u64 nffw_fwinfo_mip_offset_get(struct nffw_fwinfo *fi)
{
	return (((u64)fi->loaded__mu_da__mip_off_hi & 0xFF) << 32) |
		fi->mip_offset_lo;
}

/* flg_init = flags[0]<0> */
static u32 nffw_res_flg_init_get(struct nfp_nffw_info_data *fwinf)
{
	return (fwinf->flags[0] >> 0) & 0x1;
}

static unsigned int nffw_res_fwinfos(struct nfp_nffw_info_data *fwinf,
				     struct nffw_fwinfo **arr)
{
	struct nffw_fwinfo *fwinfo = NULL;
	unsigned int cnt = 0;

	/* For the this code, version 0 is most likely to be
	 * version 1 in this case. Since the kernel driver
	 * does not take responsibility for initialising the
	 * nfp.nffw resource, any previous code (CA firmware or
	 * userspace) that left the version 0 and did set
	 * the init flag is going to be version 1.
	 */
	switch (nffw_res_info_version_get(fwinf)) {
	case 0:
	case 1:
		fwinfo = &fwinf->info.v1.fwinfo[0];
		cnt = NFFW_FWINFO_CNT_V1;
		break;
	case 2:
		fwinfo = &fwinf->info.v2.fwinfo[0];
		cnt = NFFW_FWINFO_CNT_V2;
		break;
	default:
		break;
	}

	*arr = fwinfo;
	return cnt;
}

/**
 * nfp_nffw_info_open() - Acquire the lock on the NFFW table
 * @cpp:	NFP CPP handle
 *
 * Return: 0, or -ERRNO
 */
struct nfp_nffw_info *nfp_nffw_info_open(struct nfp_cpp *cpp)
{
	struct nfp_nffw_info_data *fwinf;
	struct nfp_nffw_info *state;
	struct nfp_resource *res;
	int err;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return ERR_PTR(-ENOMEM);

	res = nfp_resource_acquire(cpp, NFP_RESOURCE_NFP_NFFW);
	if (!IS_ERR(res)) {
		u32 cpp_id = nfp_resource_cpp_id(res);
		u64 addr = nfp_resource_address(res);
		u32 info_ver;

		fwinf = &state->fwinf;

		if (sizeof(*fwinf) > nfp_resource_size(res)) {
			nfp_resource_release(res);
			kfree(state);
			return ERR_PTR(-ERANGE);
		}

		err = nfp_cpp_read(cpp, cpp_id, addr, fwinf, sizeof(*fwinf));
		if (err < 0) {
			nfp_resource_release(res);
			kfree(state);
			return ERR_PTR(err);
		}

#ifndef __LITTLE_ENDIAN
		/* Endian swap */
		{
			u32 *v;
			unsigned int i;

			for (i = 0, v = (u32 *)fwinf;
			     i < sizeof(fwinf);
			     i += sizeof(*v), v++) {
				*v = le32_to_cpu(*v);
			}
		}
#endif

		if (!nffw_res_flg_init_get(fwinf)) {
			nfp_resource_release(res);
			kfree(state);
			return ERR_PTR(-EINVAL);
		}

		info_ver = nffw_res_info_version_get(fwinf);
		if (info_ver > NFFW_INFO_VERSION_CURRENT) {
			nfp_resource_release(res);
			kfree(state);
			return ERR_PTR(-EIO);
		}
	} else {
		kfree(state);
		return ERR_PTR(-ENODEV);
	}

	state->res = res;
	state->cpp = cpp;
	return state;
}

/**
 * nfp_nffw_info_release() - Release the lock on the NFFW table
 * @state:	NFP FW info state
 *
 * Return: 0, or -ERRNO
 */
void nfp_nffw_info_close(struct nfp_nffw_info *state)
{
	struct nfp_nffw_info_data *fwinf = &state->fwinf;
	struct nfp_cpp *cpp = state->cpp;
	struct nfp_resource *res;
	int err;

	res = state->res;
	if (res) {
		u32 cpp_id = nfp_resource_cpp_id(res);
		u64 addr = nfp_resource_address(res);

#ifndef __LITTLE_ENDIAN
		/* Endian swap the buffer we are writing out in-place */
		{
			u32 *v;
			unsigned int i;

			for (i = 0, v = (u32 *)fwinf;
			     i < sizeof(*fwinf);
			     i += sizeof(*v), v++) {
				*v = cpu_to_le32(*v);
			}
		}
#endif

		err = nfp_cpp_write(cpp, cpp_id, addr, fwinf, sizeof(*fwinf));
		nfp_resource_release(res);
		/* Clear the device's nffw_info data to invalidate it */
		memset(fwinf, 0, sizeof(*fwinf));
		if (err < 0)
			nfp_cpp_err(cpp, "NFFW info write back failed\n");
	}

	kfree(state);
}

/**
 * nfp_nffw_info_fw_mip() - Retrieve the location of the MIP of a firmware
 * @state:	NFP FW info state
 * @fwid:	NFFW firmware ID
 * @cpp_id:	Pointer to the CPP ID of the MIP
 * @off:	Pointer to the CPP Address of the MIP
 *
 * Return: 0, or -ERRNO
 */
int nfp_nffw_info_fw_mip(struct nfp_nffw_info *state, u8 fwid,
			 u32 *cpp_id, u64 *off)
{
	unsigned int fwidx = fwid - NFFW_FWID_BASE;
	struct nffw_fwinfo *fwinfo;
	unsigned int cnt;

	cnt = nffw_res_fwinfos(&state->fwinf, &fwinfo);

	if (!cnt)
		return -ENODEV;

	if (fwid < NFFW_FWID_BASE || fwidx >= cnt)
		return -EINVAL;

	fwinfo = &fwinfo[fwidx];
	if (!nffw_fwinfo_loaded_get(fwinfo))
		return -ENOENT;

	if (cpp_id)
		*cpp_id = nffw_fwinfo_mip_cppid_get(fwinfo);
	if (off)
		*off = nffw_fwinfo_mip_offset_get(fwinfo);

	if (nffw_fwinfo_mip_mu_da_get(fwinfo))
		*off |= 1ULL << 63;

	return 0;
}

/**
 * nfp_nffw_info_fwid_first() - Return the first firmware ID in the NFFW
 * @state:	NFP FW info state
 *
 * Return: First NFFW firmware ID
 */
u8 nfp_nffw_info_fwid_first(struct nfp_nffw_info *state)
{
	struct nffw_fwinfo *fwinfo;
	unsigned int idx;
	unsigned int cnt;

	cnt = nffw_res_fwinfos(&state->fwinf, &fwinfo);

	if (!cnt)
		return 0;

	for (idx = 0; idx < cnt; idx++) {
		if (nffw_fwinfo_loaded_get(&fwinfo[idx]))
			return idx + NFFW_FWID_BASE;
	}

	return 0;
}
