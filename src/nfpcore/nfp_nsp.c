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
 * nfp_nsp.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include "nfp.h"
#include "nfp_cpp.h"

#define NSP_RESOURCE		"nfp.sp"

/* Offsets relative to the CSR base */
#define NSP_STATUS            0x00
#define   NSP_STATUS_MAGIC_of(x)      (((x) >> 48) & 0xffff)
#define   NSP_STATUS_MAGIC(x)         (((x) & 0xffffULL) << 48)
#define   NSP_STATUS_MAJOR_of(x)      (((x) >> 40) & 0xf)
#define   NSP_STATUS_MAJOR(x)         (((x) & 0xfULL) << 40)
#define   NSP_STATUS_MINOR_of(x)      (((x) >> 32) & 0xfff)
#define   NSP_STATUS_MINOR(x)         (((x) & 0xfffULL) << 32)
#define   NSP_STATUS_CODE_of(x)       (((x) >> 16) & 0xffff)
#define   NSP_STATUS_CODE(x)          (((x) & 0xffffULL) << 16)
#define   NSP_STATUS_RESULT_of(x)     (((x) >>  8) & 0xff)
#define   NSP_STATUS_RESULT(x)        (((x) & 0xffULL) << 8)
#define   NSP_STATUS_BUSY             BIT_ULL(0)

#define NSP_COMMAND           0x08
#define   NSP_COMMAND_OPTION(x)       ((u64)((x) & 0xffffffff) << 32)
#define   NSP_COMMAND_OPTION_of(x)    (((x) >> 32) & 0xffffffff)
#define   NSP_COMMAND_CODE(x)         (((x) & 0xffff) << 16)
#define   NSP_COMMAND_CODE_of(x)      (((x) >> 16) & 0xffff)
#define   NSP_COMMAND_START           BIT_ULL(0)

/* CPP address to retrieve the data from */
#define NSP_BUFFER            0x10
#define   NSP_BUFFER_CPP(x)           ((u64)(((x) >> 8) & 0xffffff) << 40)
#define   NSP_BUFFER_CPP_of(x)        ((((x) >> 40) & 0xffffff) << 8)
#define   NSP_BUFFER_ADDRESS(x)       (((x) & ((1ULL << 40) - 1)) << 0)
#define   NSP_BUFFER_ADDRESS_of(x)    (((x) >> 0) & ((1ULL << 40) - 1))

#define NSP_MAGIC             0xab10
#define NSP_MAJOR             0

#define NSP_CODE_MAJOR_of(code)	(((code) >> 12) & 0xf)
#define NSP_CODE_MINOR_of(code)	(((code) >>  0) & 0xfff)

struct nfp_nsp {
	struct nfp_resource *res;
};

static void nfp_nsp_des(void *ptr)
{
	struct nfp_nsp *priv = ptr;

	nfp_resource_release(priv->res);
}

static void *nfp_nsp_con(struct nfp_device *nfp)
{
	struct nfp_resource *res;
	struct nfp_nsp *priv;

	res = nfp_resource_acquire(nfp, NSP_RESOURCE);
	if (IS_ERR(res))
		return NULL;

	priv = nfp_device_private_alloc(nfp, sizeof(*priv), nfp_nsp_des);
	if (!priv) {
		nfp_resource_release(res);
		return priv;
	}

	priv->res = res;

	return priv;
}

/**
 * nfp_nsp_command() - Execute a command on the NFP Service Processor
 * @nfp:	NFP Device handle
 * @code:	NSP Command Code
 *
 * Return: 0 for success with no result
 *
 *         1..255 for NSP completion with a result code
 *
 *         -EAGAIN if the NSP is not yet present
 *
 *         -ENODEV if the NSP is not a supported model
 *
 *         -EBUSY if the NSP is stuck
 *
 *         -EINTR if interrupted while waiting for completion
 *
 *         -ETIMEDOUT if the NSP took longer than 30 seconds to complete
 */
int nfp_nsp_command(struct nfp_device *nfp, u16 code, u32 option,
		    u32 buff_cpp, u64 buff_addr)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	struct nfp_nsp *nsp;
	u32 nsp_cpp;
	u64 nsp_base;
	u64 nsp_status;
	u64 nsp_command;
	u64 nsp_buffer;
	int err, ok;
	u64 tmp;
	int timeout = 30 * 10;	/* 30 seconds total */

	nsp = nfp_device_private(nfp, nfp_nsp_con);
	if (!nsp)
		return -EAGAIN;

	nsp_cpp = nfp_resource_cpp_id(nsp->res);
	nsp_base = nfp_resource_address(nsp->res);
	nsp_status = nsp_base + NSP_STATUS;
	nsp_command = nsp_base + NSP_COMMAND;
	nsp_buffer = nsp_base + NSP_BUFFER;

	err = nfp_cpp_readq(cpp, nsp_cpp, nsp_status, &tmp);
	if (err < 0)
		return err;

	if (NSP_MAGIC != NSP_STATUS_MAGIC_of(tmp)) {
		nfp_err(nfp, "NSP: Cannot detect NFP Service Processor\n");
		return -ENODEV;
	}

	ok = NSP_STATUS_MAJOR_of(tmp) == NSP_CODE_MAJOR_of(code) &&
	     NSP_STATUS_MINOR_of(tmp) >= NSP_CODE_MINOR_of(code);
	if (!ok) {
		nfp_err(nfp, "NSP: Code 0x%04x not supported (ABI %d.%d)\n",
			code,
			(int)NSP_STATUS_MAJOR_of(tmp),
			(int)NSP_STATUS_MINOR_of(tmp));
		return -EINVAL;
	}

	if (tmp & NSP_STATUS_BUSY) {
		nfp_err(nfp, "NSP: Service processor busy!\n");
		return -EBUSY;
	}

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_buffer,
			     NSP_BUFFER_CPP(buff_cpp) |
			     NSP_BUFFER_ADDRESS(buff_addr));
	if (err < 0)
		return err;

	err = nfp_cpp_writeq(cpp, nsp_cpp, nsp_command,
			     NSP_COMMAND_OPTION(option) |
			     NSP_COMMAND_CODE(code) | NSP_COMMAND_START);
	if (err < 0)
		return err;

	/* Wait for NSP_COMMAND_START to go to 0 */
	for (; timeout > 0; timeout--) {
		err = nfp_cpp_readq(cpp, nsp_cpp, nsp_command, &tmp);
		if (err < 0)
			return err;

		if (!(tmp & NSP_COMMAND_START))
			break;

		if (msleep_interruptible(100) > 0) {
			nfp_warn(nfp, "NSP: Interrupt waiting for code 0x%04x to start\n",
				 code);
			return -EINTR;
		}
	}

	if (timeout < 0) {
		nfp_warn(nfp, "NSP: Timeout waiting for code 0x%04x to start\n",
			 code);
		return -ETIMEDOUT;
	}

	/* Wait for NSP_STATUS_BUSY to go to 0 */
	for (; timeout > 0; timeout--) {
		err = nfp_cpp_readq(cpp, nsp_cpp, nsp_status, &tmp);
		if (err < 0)
			return err;

		if (!(tmp & NSP_STATUS_BUSY))
			break;

		if (msleep_interruptible(100) > 0) {
			nfp_warn(nfp, "NSP: Interrupt waiting for code 0x%04x to complete\n",
				 code);
			return -EINTR;
		}
	}

	if (timeout < 0) {
		nfp_warn(nfp, "NSP: Timeout waiting for code 0x%04x to complete\n",
			 code);
		return -ETIMEDOUT;
	}

	err = NSP_STATUS_RESULT_of(tmp);
	if (err > 0)
		return -err;

	err = nfp_cpp_readq(cpp, nsp_cpp, nsp_command, &tmp);
	if (err < 0)
		return err;

	return NSP_COMMAND_OPTION_of(tmp);
}
