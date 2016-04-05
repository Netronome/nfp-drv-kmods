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
 * nfp_ca.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *
 * Parses the hwinfo table that the ARM firmware builds in the ARM scratch SRAM
 * after chip reset.
 *
 * Some of the fields:
 *   me.count = 40
 *   me.mask = 0x7f_ffff_ffff
 *
 *   me.count is the total number of MEs on the system.
 *   me.mask is the bitmask of MEs that are available for application usage.
 *
 *   (ie, in this example, ME 39 has been reserved by boardconfig.)
 *
 *   arm.mem = 512
 *   assembly.model = rsvp
 *   board.exec = linux.bin
 *   board.setup = boardconfig.bin
 *   chip.model = NFP3240
 *   config.timestamp = 2010-1-5 17:58:22 GMT
 *   ddr.mem = 2048
 *   flash.model = tip
 *   me.mem = 1536
 *   qdr1.enabled = 1
 *   qdr1.mem = 8
 *   qdr1.type = mem
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/log2.h>
#include <linux/crc32.h>
#include <linux/delay.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp3200/nfp3200.h"
#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_pl.h"
#include <asm/byteorder.h>

#define HWINFO_SIZE_MIN	0x100

/*
 * The Hardware Info Table defines the properties of the system.
 *
 * HWInfo v1 Table (fixed size)
 *
 * 0x0000: u32 version	        Hardware Info Table version (1.0)
 * 0x0004: u32 size	        Total size of the table, including
 *			        the CRC32 (IEEE 802.3)
 * 0x0008: u32 jumptab	        Offset of key/value table
 * 0x000c: u32 keys	        Total number of keys in the key/value table
 * NNNNNN:		        Key/value jump table and string data
 * (size - 4): u32 crc32	CRC32 (same as IEEE 802.3, POSIX cksum, etc)
 *				CRC32("",0) = ~0, CRC32("a",1) = 0x48C279FE
 *
 * HWInfo v2 Table (variable size)
 *
 * 0x0000: u32 version	        Hardware Info Table version (2.0)
 * 0x0004: u32 size	        Current size of the data area, excluding CRC32
 * 0x0008: u32 limit	        Maximum size of the table
 * 0x000c: u32 reserved	        Unused, set to zero
 * NNNNNN:			Key/value data
 * (size - 4): u32 crc32	CRC32 (same as IEEE 802.3, POSIX cksum, etc)
 *				CRC32("",0) = ~0, CRC32("a",1) = 0x48C279FE
 *
 * If the HWInfo table is in the process of being updated, the low bit
 * of version will be set.
 *
 * HWInfo v1 Key/Value Table
 * -------------------------
 *
 *  The key/value table is a set of offsets to ASCIIZ strings which have
 *  been strcmp(3) sorted (yes, please use bsearch(3) on the table).
 *
 *  All keys are guaranteed to be unique.
 *
 * N+0:	u32 key_1		Offset to the first key
 * N+4:	u32 val_1		Offset to the first value
 * N+8: u32 key_2		Offset to the second key
 * N+c: u32 val_2		Offset to the second value
 * ...
 *
 * HWInfo v1 Key/Value Table
 * -------------------------
 *
 * Packed UTF8Z strings, ie 'key1\000value1\000key2\000value2\000'
 *
 * Unsorted.
 */

/* Hardware Info Version 1.0 */
#define NFP_HWINFO_VERSION_1 \
	(('H' << 24) | ('I' << 16) | (1 << 8) | (0 << 1) | 0)
/* Hardware Info Version 2.0 */
#define NFP_HWINFO_VERSION_2 \
	(('H' << 24) | ('I' << 16) | (2 << 8) | (0 << 1) | 0)
#define NFP_HWINFO_VERSION_UPDATING(ver)   ((ver) & 1)

#define NFP_HWINFO_VERSION_IN(base) __le32_to_cpu(((u32 *)(base))[0])
#define NFP_HWINFO_VERSION_SET(base, val) \
	(((u32 *)(base))[0] = __cpu_to_le32(val))

/***************** HWInfo v1 ****************/

/* Hardware Info Table Version 1.x */
#define NFP_HWINFO_SIZE_IN(base)      __le32_to_cpu(((u32 *)(base))[1])
#define NFP_HWINFO_V1_TABLE_IN(base)  __le32_to_cpu(((u32 *)(base))[2])
#define NFP_HWINFO_V1_KEYS_IN(base)   __le32_to_cpu(((u32 *)(base))[3])
#define NFP_HWINFO_V2_LIMIT_IN(base)  __le32_to_cpu(((u32 *)(base))[2])
#define NFP_HWINFO_CRC32_IN(base) \
	__le32_to_cpu(((u32 *)NFP_HWINFO_DATA_END(base))[0])

#define NFP_HWINFO_SIZE_SET(base, val) \
		(((u32 *)(base))[1] = __cpu_to_le32(val))

#define NFP_HWINFO_V1_TABLE_SET(base, val) \
	(((u32 *)(base))[2] = __cpu_to_le32(val))
#define NFP_HWINFO_V1_KEYS_SET(base, val) \
	(((u32 *)(base))[3] = __cpu_to_le32(val))

#define NFP_HWINFO_V2_LIMIT_SET(base, val) \
	(((u32 *)(base))[2] = __cpu_to_le32(val))
#define NFP_HWINFO_V2_RESERVED_SET(base, val) \
	(((u32 *)(base))[3] = __cpu_to_le32(val))

#define NFP_HWINFO_CRC32_SET(base, val) \
	(((u32 *)NFP_HWINFO_DATA_END(base))[0] = __cpu_to_le32(val))

#define NFP_HWINFO_DATA_START(base) ((void *)&(((u32 *)base)[4]))
#define NFP_HWINFO_DATA_END(base) \
	((void *)(((char *)(base)) + NFP_HWINFO_SIZE_IN(base) - sizeof(u32)))

/* Key/Value Table Version 1.x */
#define NFP_HWINFO_V1_KEY_IN(base, key_id) \
	((const char *)((char *)(base) + \
		__le32_to_cpu(((u32 *)((base) + \
			      NFP_HWINFO_V1_TABLE_IN(base)))[(key_id) * 2 + \
							     0])))
#define NFP_HWINFO_V1_VAL_IN(base, key_id) \
	((const char *)((char *)(base) + \
		__le32_to_cpu(((u32 *)((base) + \
			      NFP_HWINFO_V1_TABLE_IN(base)))[(key_id) * 2 + \
							     1])))

#undef NFP_SUBSYS
#define NFP_SUBSYS "[HWINFO] "

static int hwinfo_wait = 20;	/* 20 seconds (NFP6000 boot is slow) */
module_param(hwinfo_wait, int, S_IRUGO);
MODULE_PARM_DESC(hwinfo_wait, "-1 for no timeout, or N seconds to wait for board.state match");

static int hwinfo_debug;
module_param(hwinfo_debug, int, S_IRUGO);
MODULE_PARM_DESC(hwinfo_debug, "Enable to log hwinfo contents on load");

#define NFP_HWINFO_DEBUG	hwinfo_debug

/* NOTE: This should be 15 (SKUSE_POWER)
 */
static int board_state = 15;	/* board.state to match against */
module_param(board_state, int, S_IRUGO);
MODULE_PARM_DESC(board_state, "board.state to wait for");

static void hwinfo_db_parse(struct nfp_device *nfp, void *hwinfo)
{
	const char *key;
	const char *val;

	for (key = NFP_HWINFO_DATA_START(hwinfo);
	     *key && key < (const char *)NFP_HWINFO_DATA_END(hwinfo);
	     key = val + strlen(val) + 1) {
		val = key + strlen(key) + 1;

		nfp_dbg(nfp, "%s=%s\n", key, val);
	}
}

static u32 hwinfo_crc(void *db)
{
	u32 len = NFP_HWINFO_SIZE_IN(db) - sizeof(u32);
	u32 crc;

	crc = crc32_be(0, db, len);

	/* Extend with the length of the string (but not the length word). */
	while (len != 0) {
		u8 byte = len & 0xff;

		crc = crc32_be(crc, (void *)&byte, 1);
		len >>= 8;
	}

	return ~crc;
}

static int hwinfo_db_validate(struct nfp_device *nfp, void *db, u32 len)
{
	u32 crc;

	if (NFP_HWINFO_VERSION_IN(db) != NFP_HWINFO_VERSION_1 &&
	    NFP_HWINFO_VERSION_IN(db) != NFP_HWINFO_VERSION_2) {
		nfp_err(nfp, "Unknown hwinfo version 0x%x, expected 0x%x or 0x%x\n",
			NFP_HWINFO_VERSION_IN(db),
			NFP_HWINFO_VERSION_1, NFP_HWINFO_VERSION_2);
		return -EINVAL;
	}

	if (NFP_HWINFO_SIZE_IN(db) > len) {
		nfp_err(nfp, "Unsupported hwinfo size %u > %u\n",
			NFP_HWINFO_SIZE_IN(db), len);
		return -EINVAL;
	}

	crc = hwinfo_crc(db);
	if (crc != NFP_HWINFO_CRC32_IN(db)) {
		nfp_err(nfp, "Corrupt hwinfo table (CRC mismatch), calculated 0x%x, expected 0x%x\n",
			crc, NFP_HWINFO_CRC32_IN(db));
		return -EINVAL;
	}

	return 0;
}

static int hwinfo_fetch_nowait(struct nfp_device *nfp,
			       void **hwdb, size_t *hwdb_size)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	struct nfp_cpp_area *area;
	struct nfp_resource *res;
	size_t   cpp_size;
	u8 header[16];
	u64 cpp_addr;
	void *tmpdb;
	u32 cpp_id;
	int r = 0;
	u32 ver;

	res = nfp_resource_acquire(nfp, NFP_RESOURCE_NFP_HWINFO);
	if (!IS_ERR(res)) {
		cpp_id = nfp_resource_cpp_id(res);
		cpp_addr = nfp_resource_address(res);
		cpp_size = nfp_resource_size(res);

		nfp_resource_release(res);

		if (cpp_size < HWINFO_SIZE_MIN)
			return -ENOENT;
	} else {
		u32 model = nfp_cpp_model(cpp);

		/* Try getting the HWInfo table from the 'classic' location
		 */
		if (NFP_CPP_MODEL_IS_3200(model)) {
			cpp_id = NFP_CPP_ID(NFP_CPP_TARGET_ARM_SCRATCH,
					    NFP_CPP_ACTION_RW, 0);
			cpp_addr = 0;
			cpp_size = 2 * 1024;
		} else if (NFP_CPP_MODEL_IS_6000(model)) {
			cpp_id = NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_MU,
						   NFP_CPP_ACTION_RW, 0, 1);
			cpp_addr = 0x30000;
			cpp_size = 0x0e000;
		} else {
			return -ENODEV;
		}
	}

	/* Fetch the hardware table from the ARM's SRAM (scratch).  It
	 * occupies 0x0000 - 0x1fff.
	 */
	area = nfp_cpp_area_alloc_with_name(cpp, cpp_id, "nfp.hwinfo",
					    cpp_addr, cpp_size);
	if (!area)
		return -EIO;

	r = nfp_cpp_area_acquire(area);
	if (r < 0)
		goto exit_area_free;

	r = nfp_cpp_area_read(area, 0, header, sizeof(header));
	if (r < 0) {
		nfp_err(nfp, "Can't read version: %d\n", r);
		goto exit_area_release;
	}

	ver = NFP_HWINFO_VERSION_IN(header);

	if (NFP_HWINFO_VERSION_UPDATING(ver)) {
		r = -EBUSY;
		goto exit_area_release;
	}

	if (ver != NFP_HWINFO_VERSION_2 && ver != NFP_HWINFO_VERSION_1) {
		nfp_err(nfp, "Unknown HWInfo version: 0x%08x\n", ver);
		r = -EINVAL;
		goto exit_area_release;
	}

	tmpdb = kmalloc(cpp_size, GFP_KERNEL);
	if (!tmpdb) {
		r = -ENOMEM;
		goto exit_area_release;
	}

	memset(tmpdb, 0xff, cpp_size);

	r = nfp_cpp_area_read(area, 0, tmpdb, cpp_size);
	if (r >= 0 && r != cpp_size) {
		kfree(tmpdb);
		r = (r < 0) ? r : -EIO;
		goto exit_area_release;
	}

	*hwdb = tmpdb;
	*hwdb_size = cpp_size;

exit_area_release:
	nfp_cpp_area_release(area);

exit_area_free:
	nfp_cpp_area_free(area);

	return r;
}

static int hwinfo_fetch(struct nfp_device *nfp, void **hwdb, size_t *hwdb_size)
{
	int timeout;
	int r = -ENODEV;

	for (timeout = (hwinfo_wait * 10); timeout >= 0; timeout--) {
		r = hwinfo_fetch_nowait(nfp, hwdb, hwdb_size);
		if (r >= 0)
			break;

		msleep(100);	/* Sleep for 1/10 second. */
	}

	if (r < 0) {
		nfp_err(nfp, "NFP access error detected\n");
	} else if (hwinfo_wait >= 0 && timeout < 0) {
		nfp_err(nfp, "Timed out after %d seconds, waiting for HWInfo\n",
			hwinfo_wait);
		r = -EIO;
	}

	return r;
}

struct hwinfo_priv {
	void *db;
};

static void hwinfo_des(void *ptr)
{
	struct hwinfo_priv *priv = ptr;

	kfree(priv->db);
}

static void *hwinfo_con(struct nfp_device *nfp)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	struct hwinfo_priv *priv = NULL;
	size_t hwdb_size = 0;
	void *hwdb = NULL;
	int r = 0;
	u32 model;

	model = nfp_cpp_model(cpp);

	if (NFP_CPP_MODEL_IS_3200(model) &&
	    NFP_CPP_INTERFACE_TYPE_of(nfp_cpp_interface(cpp)) ==
	       NFP_CPP_INTERFACE_TYPE_PCI) {
		u32 straps;
		u32 pl_re;
		u32 arm_re;

		r = nfp_xpb_readl(cpp, NFP_XPB_PL | NFP_PL_STRAPS, &straps);
		if (r < 0) {
			nfp_err(nfp, "nfp_xpb_readl failed().\n");
			r = -ENODEV;
			goto err;
		}
		r = nfp_xpb_readl(cpp, NFP_XPB_PL | NFP_PL_RE, &pl_re);
		if (r < 0) {
			nfp_err(nfp, "nfp_xpb_readl failed().\n");
			r = -ENODEV;
			goto err;
		}
		arm_re = NFP_PL_RE_ARM_GASKET_RESET |
			 NFP_PL_RE_ARM_GASKET_ENABLE;
		if (((straps & NFP_PL_STRAPS_CFG_PROM_BOOT) == 0) &&
		    ((pl_re & arm_re) != arm_re)) {
			nfp_err(nfp, "ARM gasket is disabled.\n");
			r = -ENODEV;
			goto err;
		}
	}

	r = hwinfo_fetch(nfp, &hwdb, &hwdb_size);
	if (r < 0)
		goto err;

	r = hwinfo_db_validate(nfp, hwdb, hwdb_size);
	if (r < 0)
		goto err;

	if (NFP_HWINFO_DEBUG)
		hwinfo_db_parse(nfp, hwdb);

	priv = nfp_device_private_alloc(nfp, sizeof(*priv),
					hwinfo_des);
	if (priv)
		priv->db = hwdb;
	else
		r = -ENOMEM;

err:
	if (r < 0 && hwdb)
		kfree(hwdb);

	return priv;
}

/**
 * nfp_hwinfo_lookup() - Find a value in the HWInfo table by name
 * @nfp:	NFP Device handle
 * @lookup:	HWInfo name to search for
 *
 * Return: Value of the HWInfo name, or NULL
 */
const char *nfp_hwinfo_lookup(struct nfp_device *nfp, const char *lookup)
{
	struct hwinfo_priv *priv = nfp_device_private(nfp, hwinfo_con);
	const char *val = NULL;
	const char *key;

	if (!priv || !lookup)
		return NULL;

	for (key = NFP_HWINFO_DATA_START(priv->db);
		*key &&
		key < (const char *)NFP_HWINFO_DATA_END(priv->db);
		key = val + strlen(val) + 1, val = NULL) {
		val = key + strlen(key) + 1;

		if (strcmp(key, lookup) == 0)
			break;
	}

	return val;
}
