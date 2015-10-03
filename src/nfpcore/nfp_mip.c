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
 * nfp_mip.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#define NFP6000_LONGNAMES

#include <linux/kernel.h>

#include "nfp3200/nfp3200.h"

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_nffw.h"
#include "nfp_target.h"

#define NFP_MIP_SIGNATURE	0x0050494d  /* "MIP\0" */
#define NFP_MIP_MAX_OFFSET	(256 * 1024)

#define UINT32_MAX	(0xffffffff)

#define NFP_MIP_VERSION         1
#define NFP_MIP_QC_VERSION      1
#define NFP_MIP_VPCI_VERSION    1

enum nfp_mip_entry_type {
	NFP_MIP_TYPE_NONE = 0,
	NFP_MIP_TYPE_QC = 1,
	NFP_MIP_TYPE_VPCI = 2,
};

struct nfp_mip {
	u32 signature;
	u32 mip_version;
	u32 mip_size;
	u32 first_entry;

	u32 version;
	u32 buildnum;
	u32 buildtime;
	u32 loadtime;

	u32 symtab_addr;
	u32 symtab_size;
	u32 strtab_addr;
	u32 strtab_size;

	char name[16];
	char toolchain[32];
};

struct nfp_mip_entry {
	u32 type;
	u32 version;
	u32 offset_next;
};

struct nfp_mip_qc {
	u32 type;
	u32 version;
	u32 offset_next;
	u32 type_config;
	u32 type_config_size;
	u32 host_config;
	u32 host_config_size;
	u32 config_signal;
	u32 nfp_queue_size;
	u32 queue_base;
	u32 sequence_base;
	u32 sequence_type;
	u32 status_base;
	u32 status_version;
	u32 error_base;
};

struct nfp_mip_vpci {
	u32 type;
	u32 version;
	u32 offset_next;
	u32 vpci_epconfig;
	u32 vpci_epconfig_size;
};

static void __mip_update_byteorder(struct nfp_mip *mip)
{
	struct nfp_mip_entry *ent;
	int offset;

	/* Convert main MIP structure. */
	mip->signature = le32_to_cpu(mip->signature);
	mip->mip_version = le32_to_cpu(mip->mip_version);
	mip->mip_size = le32_to_cpu(mip->mip_size);
	mip->first_entry = le32_to_cpu(mip->first_entry);
	mip->version = le32_to_cpu(mip->version);
	mip->buildnum = le32_to_cpu(mip->buildnum);
	mip->buildtime = le32_to_cpu(mip->buildtime);
	mip->loadtime = le32_to_cpu(mip->loadtime);
	mip->symtab_addr = le32_to_cpu(mip->symtab_addr);
	mip->symtab_size = le32_to_cpu(mip->symtab_size);
	mip->strtab_addr = le32_to_cpu(mip->strtab_addr);
	mip->strtab_size = le32_to_cpu(mip->strtab_size);

	/* Convert known MIP entries. */
	for (offset = mip->first_entry;
		 (offset + sizeof(*ent)) < mip->mip_size;
		 offset += ent->offset_next) {
		ent = (struct nfp_mip_entry *)(((char *)mip) + offset);
		ent->type = le32_to_cpu(ent->type);
		ent->version = le32_to_cpu(ent->version);
		ent->offset_next = le32_to_cpu(ent->offset_next);

		if ((offset + ent->offset_next) > mip->mip_size)
			break;

		switch (ent->type) {
		case NFP_MIP_TYPE_NONE:
			return;

		case NFP_MIP_TYPE_QC:
		{
			struct nfp_mip_qc *qc = (struct nfp_mip_qc *)ent;

			if (qc->version != NFP_MIP_QC_VERSION)
				break;
			qc->type_config = le32_to_cpu(qc->type_config);
			qc->type_config_size =
				le32_to_cpu(qc->type_config_size);
			qc->host_config = le32_to_cpu(qc->host_config);
			qc->host_config_size =
				le32_to_cpu(qc->host_config_size);
			qc->config_signal = le32_to_cpu(qc->config_signal);
			qc->nfp_queue_size = le32_to_cpu(qc->nfp_queue_size);
			qc->queue_base = le32_to_cpu(qc->queue_base);
			qc->sequence_base = le32_to_cpu(qc->sequence_base);
			qc->sequence_type = le32_to_cpu(qc->sequence_type);
			qc->status_base = le32_to_cpu(qc->status_base);
			qc->status_version = le32_to_cpu(qc->status_version);
			qc->error_base = le32_to_cpu(qc->error_base);
			break;
		}

		case NFP_MIP_TYPE_VPCI:
		{
			struct nfp_mip_vpci *vpci = (struct nfp_mip_vpci *)ent;

			if (vpci->version != NFP_MIP_VPCI_VERSION)
				break;
			vpci->vpci_epconfig =
				le32_to_cpu(vpci->vpci_epconfig);
			vpci->vpci_epconfig_size =
				le32_to_cpu(vpci->vpci_epconfig_size);
			break;
		}

		default:
			ent->type = cpu_to_le32(ent->type);
			ent->version = cpu_to_le32(ent->version);
			break;
		}
	}
}

struct nfp_mip_priv {
	struct nfp_mip *mip;
};

static void __nfp_mip_des(void *data)
{
	struct nfp_mip_priv *priv = data;

	kfree(priv->mip);
}

static void *__nfp_mip_con(struct nfp_device *dev)
{
	return nfp_device_private_alloc(dev,
			sizeof(struct nfp_mip_priv),
			__nfp_mip_des);
}

/**
 * nfp_mip() - Get MIP for NFP device.
 * @dev:	NFP device
 *
 * Copy MIP structure from NFP device and return it.  The returned
 * structure is handled internally by the library and should not be
 * explicitly freed by the caller.  It will be implicitly freed when
 * closing the NFP device.  Further, any subsequent call to
 * nfp_mip_probe() returning non-zero renders references to any
 * previously returned MIP structure invalid.
 *
 * If the MIP is found, the main fields of the MIP structure are
 * automatically converted to the endianness of the host CPU, as are
 * any MIP entries known to the library.  If a MIP entry is not known
 * to the library, only the 'offset_next' field of the entry structure
 * is endian converted.  The remainder of the structure is left as-is.
 * Such entries must be searched for by explicitly converting the type
 * and version to/from little-endian.
 *
 * Return: MIP structure, or NULL
 */
const struct nfp_mip *nfp_mip(struct nfp_device *dev)
{
	struct nfp_mip_priv *priv = nfp_device_private(dev, __nfp_mip_con);
	int err;

	if (priv->mip)
		return priv->mip;

	err = nfp_mip_probe(dev);
	if (err < 0)
		return NULL;

	return priv->mip;
}

#define   NFP_IMB_TGTADDRESSMODECFG_MODE_of(_x)      (((_x) >> 13) & 0x7)
#define   NFP_IMB_TGTADDRESSMODECFG_ADDRMODE                 BIT(12)
#define     NFP_IMB_TGTADDRESSMODECFG_ADDRMODE_32_BIT        0
#define     NFP_IMB_TGTADDRESSMODECFG_ADDRMODE_40_BIT        BIT(12)

static int nfp_mip_nfp6000_mu_locality_lsb(struct nfp_device *dev)
{
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 xpbaddr, imbcppat;
	int err;

	if (!cpp)
		return -ENODEV;

	/* Hardcoded XPB IMB Base, island 0 */
	xpbaddr = 0x000a0000 + (NFP_CPP_TARGET_MU * 4);
	err = nfp_xpb_readl(cpp, xpbaddr, &imbcppat);
	if (err < 0)
		return err;

	return _nfp6000_cppat_mu_locality_lsb(
		NFP_IMB_TGTADDRESSMODECFG_MODE_of(imbcppat),
		(imbcppat & NFP_IMB_TGTADDRESSMODECFG_ADDRMODE) ==
		NFP_IMB_TGTADDRESSMODECFG_ADDRMODE_40_BIT);
}

static int __nfp_mip_location(struct nfp_device *dev,
			      u32 *cppid, u64 *addr,
			      unsigned long *size, unsigned long *load_time)
{
	int retval;
	u32 mip_cppid = 0;
	u64 mip_off = 0;
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	struct nfp_mip mip;
	u32 model = nfp_cpp_model(nfp_device_cpp(dev));

	/* First see if we can get it from the nfp.nffw resource */
	if (nfp_nffw_info_acquire(dev) == 0) {
		int mu_lsb = -1;

		if (NFP_CPP_MODEL_IS_6000(model))
			mu_lsb = nfp_mip_nfp6000_mu_locality_lsb(dev);
		else
			mu_lsb = 38; /* Assume 40-bit addressing */

		if ((nfp_nffw_info_fw_mip(dev, nfp_nffw_info_fwid_first(dev),
					  &mip_cppid, &mip_off) == 0) &&
			(mip_cppid != 0) &&
			(NFP_CPP_ID_TARGET_of(mip_cppid) ==
						NFP_CPP_TARGET_MU)) {
			if ((mip_off >> 63) & 1) {
				mip_off &= ~((u64)1) << 63;
				mip_off &= ~((u64)0x3) << mu_lsb;
				/* Direct Access */
				mip_off |= ((u64)2) << mu_lsb;
			}
		}
		nfp_nffw_info_release(dev);
	}

	/* Verify that the discovered area actually has a MIP signature */
	if (mip_cppid) {
		retval = nfp_cpp_read(cpp, mip_cppid,
				      mip_off,
				      &mip, sizeof(mip));
		if (retval < sizeof(mip) ||
		    le32_to_cpu(mip.signature) != NFP_MIP_SIGNATURE)
			mip_cppid = 0;
	}

	if (mip_cppid == 0) {
		for (mip_off = 0;
		     mip_off < NFP_MIP_MAX_OFFSET;
		     mip_off += 4096) {
			u32 cpp_id = NFP_CPP_ID(NFP_CPP_TARGET_MU,
						     NFP_CPP_ACTION_RW, 0);
			if (NFP_CPP_MODEL_IS_6000(model))
				cpp_id |= 24;
			retval = nfp_cpp_read(cpp, cpp_id,
					      mip_off,
					      &mip, sizeof(mip));
			if (retval < sizeof(mip))
				goto err_probe;
			if (le32_to_cpu(mip.signature) == NFP_MIP_SIGNATURE) {
				mip_cppid = cpp_id;
				break;
			}
		}
	}

	if (mip_cppid == 0)
		goto err_probe;

	/* This limitation is not required any more, only recommended
	 if ((le32_to_cpu(mip_version) != NFP_MIP_VERSION) ||
		(mip_off + le32_to_cpu(mip_size) >= NFP_MIP_MAX_OFFSET))
		goto err_probe;
	*/
	*cppid = mip_cppid;
	*addr = mip_off;
	*size = (le32_to_cpu(mip.mip_size) + 7) & ~7;
	*load_time = le32_to_cpu(mip.loadtime);
	return 0;

err_probe:
	return -ENODEV;
}

/**
 * nfp_mip_probe() - Check if MIP has been updated.
 * @dev:           NFP device
 *
 * Check if currently cached MIP has been updated on the NFP device,
 * and read potential new contents.  If a call to nfp_mip_probe()
 * returns non-zero, the old MIP structure returned by a previous call
 * to nfp_mip() is no longer guaranteed to be present and any
 * references to the old structure is invalid.
 *
 * Return: 1 if MIP has been updated, 0 if no update has occurred, or -ERRNO
 */
int nfp_mip_probe(struct nfp_device *dev)
{
	struct nfp_mip_priv *priv = nfp_device_private(dev, __nfp_mip_con);
	unsigned long size, time;
	u32 cpp_id;
	u64 addr;
	struct nfp_mip *mip;
	int retval;

	retval = __nfp_mip_location(dev, &cpp_id, &addr, &size, &time);
	if (retval != 0)
		return -ENODEV;

	if (priv->mip && priv->mip->loadtime == time)
		return 0; /* No change */

	/*
	 * Once we have confirmed a MIP update we discard old MIP and read
	 * new contents from DRAM.  We also discard the current symtab.
	 */

	if (priv->mip) {
		/* Invalidate rtsym first, it may want to
		 * still look at the mip
		 */
		nfp_rtsym_reload(dev);
		kfree(priv->mip);
		priv->mip = NULL;
	}

	mip = kmalloc(size, GFP_KERNEL);
	if (!mip)
		return -ENOMEM;

	retval = nfp_cpp_read(nfp_device_cpp(dev), cpp_id, addr, mip, size);
	if (retval != size) {
		kfree(mip);

		return (retval < 0) ? retval : -EIO;
	}

	if ((le32_to_cpu(mip->signature) != NFP_MIP_SIGNATURE) ||
	    (le32_to_cpu(mip->mip_version) != NFP_MIP_VERSION)) {
		kfree(mip);
		return -EIO;
	}

	__mip_update_byteorder(mip);

	priv->mip = mip;

	return 1;
}

/**
 * nfp_mip_symtab() - Get the address and size of the MIP symbol table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol table
 * @size:	Location for size of MIP symbol table
 *
 * Return: 0, or -ERRNO
 */
int nfp_mip_symtab(const struct nfp_mip *mip, u32 *addr, u32 *size)
{
	if (!mip)
		return -EINVAL;

	if (addr)
		*addr = mip->symtab_addr;
	if (size)
		*size = mip->symtab_size;

	return 0;
}

/**
 * nfp_mip_strtab() - Get the address and size of the MIP symbol name table
 * @mip:	MIP handle
 * @addr:	Location for NFP DDR address of MIP symbol name table
 * @size:	Location for size of MIP symbol name table
 *
 * Return: 0, or -ERRNO
 */
int nfp_mip_strtab(const struct nfp_mip *mip, u32 *addr, u32 *size)
{
	if (!mip)
		return -EINVAL;

	if (addr)
		*addr = mip->strtab_addr;
	if (size)
		*size = mip->strtab_size;

	return 0;
}
