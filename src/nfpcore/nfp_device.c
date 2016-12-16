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
 * nfp_device.c
 * Author: Jakub Kicinski <jakub.kicinski@netronome.com>
 *         Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ctype.h>

#include "nfp.h"
#include "nfp6000/nfp6000.h"

/* Retrieve the Global NFP Lock, located at the first
 * High Locality address in the first MU/EMU.
 *
 * This overlaps and is identical to the lock for
 * the first NFP Resource Entry in the NFP Resource Page.
 */
static struct nfp_cpp_mutex *nfp_device_mutex(struct nfp_cpp *cpp)
{
	struct nfp_cpp_mutex *mutex;

	mutex = nfp_device_mutex_cache(cpp);
	if (mutex)
		return mutex;

	mutex = nfp_cpp_mutex_alloc(cpp, NFP_RESOURCE_TBL_TARGET,
				    NFP_RESOURCE_TBL_BASE, 0);
	nfp_device_mutex_cache_set(cpp, mutex);

	return mutex;
}

/**
 * nfp_device_trylock() - perform an advisory trylock on the NFP device
 * @cpp:	NFP CPP handle
 *
 * Return 0 on success, or -ERRNO on failure
 */
int nfp_device_trylock(struct nfp_cpp *cpp)
{
	struct nfp_cpp_mutex *m = nfp_device_mutex(cpp);

	if (!m)
		return -EINVAL;

	return nfp_cpp_mutex_trylock(m);
}

/**
 * nfp_device_lock() - perform an advisory lock on the NFP device
 * @cpp:	NFP CPP handle
 *
 * Return 0 on success, or -ERRNO on failure
 */
int nfp_device_lock(struct nfp_cpp *cpp)
{
	struct nfp_cpp_mutex *m = nfp_device_mutex(cpp);

	if (!m)
		return -EINVAL;

	return nfp_cpp_mutex_lock(m);
}

/**
 * nfp_device_unlock() - perform an advisory unlock on the NFP device
 * @cpp:	NFP CPP handle
 *
 * Return 0 on success, or -ERRNO on failure
 */
int nfp_device_unlock(struct nfp_cpp *cpp)
{
	struct nfp_cpp_mutex *m = nfp_device_mutex(cpp);

	if (!m)
		return -EINVAL;

	return nfp_cpp_mutex_unlock(m);
}
