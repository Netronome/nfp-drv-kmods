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
 * nfp_cpplib.c
 * Library of functions to access the NFP's CPP bus
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp6000/nfp_xpb.h"

/* NFP6000 PL */
#define NFP_PL_DEVICE_ID                         0x00000004
#define   NFP_PL_DEVICE_ID_PART_NUM_of(_x)       (((_x) >> 16) & 0xffff)
#define   NFP_PL_DEVICE_ID_SKU_of(_x)            (((_x) >> 8) & 0xff)
#define   NFP_PL_DEVICE_ID_MAJOR_REV_of(_x)   (((_x) >> 4) & 0xf)
#define   NFP_PL_DEVICE_ID_MINOR_REV_of(_x)   (((_x) >> 0) & 0xf)

/**
 * nfp_cpp_readl() - Read a u32 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readl(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u32 *value)
{
	int err;
	u32 tmp;

	err = nfp_cpp_read(cpp, cpp_id, address, &tmp, sizeof(tmp));
	*value = le32_to_cpu(tmp);

	return err;
}

/**
 * nfp_cpp_writel() - Write a u32 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writel(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u32 value)
{
	value = cpu_to_le32(value);
	return nfp_cpp_write(cpp, cpp_id, address, &value, sizeof(value));
}

/**
 * nfp_cpp_readq() - Read a u64 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readq(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u64 *value)
{
	int err;
	u64 tmp;

	err = nfp_cpp_read(cpp, cpp_id, address, &tmp, sizeof(tmp));
	*value = le64_to_cpu(tmp);

	return err;
}

/**
 * nfp_cpp_writeq() - Write a u64 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writeq(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u64 value)
{
	value = cpu_to_le64(value);
	return  nfp_cpp_write(cpp, cpp_id, address, &value, sizeof(value));
}

/* NFP6000 specific */
#define NFP6000_ARM_GCSR_SOFTMODEL0	0x00400144

/* NOTE: This code should not use nfp_xpb_* functions,
 * as those are model-specific
 */
int __nfp_cpp_model_autodetect(struct nfp_cpp *cpp, u32 *model)
{
	const u32 arm_id = NFP_CPP_ID(NFP_CPP_TARGET_ARM, 0, 0);
	u32 tmp;
	int err;

	err = nfp_cpp_readl(cpp, arm_id, NFP6000_ARM_GCSR_SOFTMODEL0, model);
	if (err < 0)
		return err;

	/* The PL's PluDeviceID revision code is authoratative */
	*model &= ~0xff;
	err = nfp_xpb_readl(cpp, NFP_XPB_DEVICE(1, 1, 16) + NFP_PL_DEVICE_ID,
			    &tmp);
	if (err < 0)
		return err;

	*model |= ((NFP_PL_DEVICE_ID_MAJOR_REV_of(tmp) - 1) << 4) |
		NFP_PL_DEVICE_ID_MINOR_REV_of(tmp);

	return 0;
}

static u8 __nfp_bytemask_of(int width, u64 addr)
{
	u8 byte_mask;

	if (width == 8)
		byte_mask = 0xff;
	else if (width == 4)
		byte_mask = 0x0f << (addr & 4);
	else if (width == 2)
		byte_mask = 0x03 << (addr & 6);
	else if (width == 1)
		byte_mask = 0x01 << (addr & 7);
	else
		byte_mask = 0;

	return byte_mask;
}

int __nfp_cpp_explicit_read(struct nfp_cpp *cpp, u32 cpp_id,
			    u64 addr, void *buff, size_t len, int width_read)
{
	struct nfp_cpp_explicit *expl;
	int cnt, incr = 16 * width_read;
	char *tmp = buff;
	int err;
	u8 byte_mask;

	expl = nfp_cpp_explicit_acquire(cpp);

	if (!expl)
		return -EBUSY;

	if (incr > 128)
		incr = 128;

	if (len & (width_read - 1))
		return -EINVAL;

	/* Translate a NFP_CPP_ACTION_RW to action 0
	 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 0,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	if (incr > len)
		incr = len;

	byte_mask = __nfp_bytemask_of(width_read, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id,
				    (incr / width_read) - 1, byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1,
				    0, NFP_SIGNAL_PUSH,
				    0, NFP_SIGNAL_NONE);

	for (cnt = 0; cnt < len; cnt += incr, addr += incr, tmp += incr) {
		if ((cnt + incr) > len) {
			incr = (len - cnt);
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    (incr / width_read) - 1,
						    0xff);
		}
		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
		err = nfp_cpp_explicit_get(expl, tmp, incr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
	}

	nfp_cpp_explicit_release(expl);

	return len;
}

int __nfp_cpp_explicit_write(struct nfp_cpp *cpp, u32 cpp_id, u64 addr,
			     const void *buff, size_t len, int width_write)
{
	struct nfp_cpp_explicit *expl;
	int cnt, incr = 16 * width_write;
	const char *tmp = buff;
	int err;
	u8 byte_mask;

	expl = nfp_cpp_explicit_acquire(cpp);

	if (!expl)
		return -EBUSY;

	if (incr > 128)
		incr = 128;

	if (len & (width_write - 1))
		return -EINVAL;

	/* Translate a NFP_CPP_ACTION_RW to action 1
	 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 1,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	if (incr > len)
		incr = len;

	byte_mask = __nfp_bytemask_of(width_write, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id, (incr / width_write) - 1,
				    byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1,
				    0, NFP_SIGNAL_PULL,
				    0, NFP_SIGNAL_NONE);

	for (cnt = 0; cnt < len; cnt += incr, addr += incr, tmp += incr) {
		if ((cnt + incr) > len) {
			incr = (len - cnt);
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    (incr / width_write) - 1,
						    0xff);
		}
		err = nfp_cpp_explicit_put(expl, tmp, incr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
	}

	nfp_cpp_explicit_release(expl);

	return len;
}
