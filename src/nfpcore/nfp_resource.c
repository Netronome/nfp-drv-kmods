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
 * nfp_resource.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#define NFP6000_LONGNAMES 1

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "crc32.h"

#define NFP_XPB_OVERLAY(island)  (((island) & 0x3f) << 24)
#define NFP_XPB_ISLAND(island)   (NFP_XPB_OVERLAY(island) + 0x60000)

struct nfp_resource {
	char name[NFP_RESOURCE_ENTRY_NAME_SZ + 1];
	u32 cpp_id;
	u64 addr;
	u64 size;
	struct nfp_cpp_mutex *mutex;
};

static int nfp_cpp_resource_acquire(struct nfp_cpp *cpp, const char *name,
				    u32 *r_cpp, u64 *r_addr, u64 *r_size,
				    struct nfp_cpp_mutex **resource_mutex)
{
	struct nfp_resource_entry_region region;
	struct nfp_resource_entry tmp;
	struct nfp_cpp_mutex *mutex;
	int err, i;
	u32 key;
	u32 cpp_id;

	for (i = 0; i < sizeof(region.name); i++) {
		if (*name != 0)
			region.name[i] = *(name++);
		else
			region.name[i] = 0;
	}

	cpp_id = NFP_CPP_ID(NFP_RESOURCE_TBL_TARGET, 3, 0);  /* Atomic read */

	key = NFP_RESOURCE_TABLE_KEY;
	mutex = nfp_cpp_mutex_alloc(cpp, NFP_RESOURCE_TBL_TARGET,
				    NFP_RESOURCE_TBL_BASE, key);
	if (!mutex)
		return -ENOMEM;

	/* Wait for the lock.. */
	err = nfp_cpp_mutex_lock(mutex);
	if (err < 0) {
		nfp_cpp_mutex_free(mutex);
		return err;
	}

	/* Search for a matching entry */
	if (memcmp(region.name,
		   NFP_RESOURCE_TABLE_NAME "\0\0\0\0\0\0\0\0", 8) != 0)
		key = crc32_posix(&region.name[0], sizeof(region.name));
	for (i = 0; i < NFP_RESOURCE_TBL_ENTRIES; i++) {
		u64 addr = NFP_RESOURCE_TBL_BASE +
			sizeof(struct nfp_resource_entry) * i;

		err = nfp_cpp_read(cpp, cpp_id, addr, &tmp, sizeof(tmp));
		if (err < 0) {
			/* Unlikely to work if the read failed,
			 * but we should at least try... */
			nfp_cpp_mutex_unlock(mutex);
			nfp_cpp_mutex_free(mutex);
			return err;
		}

		if (tmp.mutex.key == key) {
			/* Found key! */
			if (resource_mutex)
				*resource_mutex = nfp_cpp_mutex_alloc(cpp,
							NFP_RESOURCE_TBL_TARGET, addr, key);

			if (r_cpp)
				*r_cpp = NFP_CPP_ID(tmp.region.cpp_target,
						tmp.region.cpp_action,
						tmp.region.cpp_token);

			if (r_addr)
				*r_addr = (u64)tmp.region.page_offset << 8;

			if (r_size)
				*r_size = (u64)tmp.region.page_size << 8;

			nfp_cpp_mutex_unlock(mutex);
			nfp_cpp_mutex_free(mutex);

			return 0;
		}
	}

	nfp_cpp_mutex_unlock(mutex);
	nfp_cpp_mutex_free(mutex);

	return -ENOENT;
}

/**
 * nfp_resource_acquire() - Acquire a resource handle
 * @nfp:		NFP Device handle
 * @name:		Name of the resource
 *
 * NOTE: This function implictly locks the acquired resource
 *
 * Return: NFP Resource handle, or ERR_PTR()
 */
struct nfp_resource *nfp_resource_acquire(struct nfp_device *nfp,
					  const char *name)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	struct nfp_cpp_mutex *mutex;
	struct nfp_resource *res;
	u64 addr, size;
	u32 cpp_id;
	int err;

	err = nfp_cpp_resource_acquire(cpp, name, &cpp_id, &addr,
				       &size, &mutex);
	if (err < 0)
		return ERR_PTR(err);

	err = nfp_cpp_mutex_lock(mutex);
	if (err < 0) {
		nfp_cpp_mutex_free(mutex);
		return ERR_PTR(err);
	}

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res) {
		nfp_cpp_mutex_free(mutex);
		return ERR_PTR(-ENOMEM);
	}

	strncpy(res->name, name, NFP_RESOURCE_ENTRY_NAME_SZ);
	res->cpp_id = cpp_id;
	res->addr = addr;
	res->size = size;
	res->mutex = mutex;

	return res;
}

/**
 * nfp_resource_release() - Release a NFP Resource handle
 * @res:	NFP Resource handle
 *
 * NOTE: This function implictly unlocks the resource handle
 */
void nfp_resource_release(struct nfp_resource *res)
{
	nfp_cpp_mutex_unlock(res->mutex);
	nfp_cpp_mutex_free(res->mutex);
	kfree(res);
}

/**
 * nfp_resource_cpp_id() - Return the cpp_id of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: NFP CPP ID
 */
u32 nfp_resource_cpp_id(struct nfp_resource *res)
{
	return res->cpp_id;
}

/**
 * nfp_resource_name() - Return the name of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: const char pointer to the name of the resource
 */
const char *nfp_resource_name(struct nfp_resource *res)
{
	return res->name;
}

/**
 * nfp_resource_address() - Return the address of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: Address of the resource
 */
u64 nfp_resource_address(struct nfp_resource *res)
{
	return res->addr;
}

/**
 * nfp_resource_size() - Return the size in bytes of a resource handle
 * @res:	NFP Resource handle
 *
 * Return: Size of the resource in bytes
 */
u64 nfp_resource_size(struct nfp_resource *res)
{
	return res->size;
}

/**
 * nfp_resource_lock() - Lock the resource
 * @res:	NFP Resource handle
 *
 * Note: nfp_resource_acquire() already acquires the lock, this should only
 * be used if more granular control of locking of the resource is needed.
 *
 * Return: same as nfp_cpp_mutex_lock().
 */
int nfp_resource_lock(struct nfp_resource *res)
{
	return nfp_cpp_mutex_lock(res->mutex);
}

/**
 * nfp_resource_unlock() - Lock the resource
 * @res:	NFP Resource handle
 *
 * Note: nfp_resource_release() already releases the lock, this should only
 * be used if more granular control of locking of the resource is needed.
 *
 * Return: same as nfp_cpp_mutex_unlock().
 */
int nfp_resource_unlock(struct nfp_resource *res)
{
	return nfp_cpp_mutex_unlock(res->mutex);
}
