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
 * The NFP CPP device wrapper
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ctype.h>

#include "nfp.h"

#include "nfp3200/nfp3200.h"
#include "nfp3200/nfp_xpb.h"

struct nfp_device {
	int cpp_free;
	struct nfp_cpp *cpp;

	spinlock_t private_lock;	/* Lock for private_list */
	struct list_head private_list;
};

struct nfp_device_private {
	 struct list_head entry;
	 void *(*constructor)(struct nfp_device *dev);
	 void (*destructor)(void *priv);
	 /* Data is allocated immediately after */
};

/**
 * nfp_device_cpp() - Get the CPP handle from a struct nfp_device
 * @nfp:	NFP device
 *
 * NOTE: Do not call nfp_cpp_free() on the returned handle,
 *       as it is owned by the NFP device.
 *
 * Return: NFP CPP handle
 */
struct nfp_cpp *nfp_device_cpp(struct nfp_device *nfp)
{
		return nfp->cpp;
}

/**
 * nfp_device_from_cpp() - Construct a NFP device from a CPP handle
 * @cpp:	CPP handle
 *
 * Return: NFP Device handle, or NULL
 */
struct nfp_device *nfp_device_from_cpp(struct nfp_cpp *cpp)
{
		int err = -ENODEV;
		struct nfp_device *nfp;

		nfp = kzalloc(sizeof(*nfp), GFP_KERNEL);
		if (!nfp) {
			err = -ENOMEM;
			goto err_nfp_alloc;
		}
		nfp->cpp = cpp;

		spin_lock_init(&nfp->private_lock);
		INIT_LIST_HEAD(&nfp->private_list);

		return nfp;

err_nfp_alloc:
		return NULL;
}

/**
 * nfp_device_close() - Close a NFP device
 * @nfp:	NFP Device handle
 */
void nfp_device_close(struct nfp_device *nfp)
{
		struct nfp_device_private *priv;

		while (!list_empty(&nfp->private_list)) {
			priv = list_first_entry(&nfp->private_list,
						struct nfp_device_private,
						entry);
			list_del(&priv->entry);
			if (priv->destructor)
				priv->destructor(&priv[1]);
			kfree(priv);
		}

		if (nfp->cpp_free)
			nfp_cpp_free(nfp->cpp);
		kfree(nfp);
}

/**
 * nfp_device_open() - Open a NFP device by ID
 * @id:		NFP device ID
 *
 * Return: NFP Device handle, or NULL
 */
struct nfp_device *nfp_device_open(unsigned int id)
{
		struct nfp_cpp *cpp;
		struct nfp_device *nfp;

		cpp = nfp_cpp_from_device_id(id);
		if (!cpp)
			return NULL;

		nfp = nfp_device_from_cpp(cpp);
		if (!nfp) {
			nfp_cpp_free(cpp);
			return NULL;
		}

		nfp->cpp_free = 1;
		return nfp;
}

/**
 * nfp_device_id() - Get the device ID from a NFP handle
 * @nfp:	NFP device
 *
 * Return: NFP Device ID
 */
int nfp_device_id(struct nfp_device *nfp)
{
		return nfp_cpp_device_id(nfp->cpp);
}

/**
 * nfp_device_private() - Allocate private memory for a NFP device
 * @dev:		NFP device
 * @constructor:	Constructor for the private area
 *
 * Returns a private memory area, identified by the constructor,
 * that will automatically be freed on nfp_device_close().
 *
 * Return: Allocated and constructed private memory, or NULL
 */
void *nfp_device_private(struct nfp_device *dev,
			 void *(*constructor)(struct nfp_device *dev))
{
	struct nfp_device_private *priv;

	spin_lock(&dev->private_lock);
	list_for_each_entry(priv, &dev->private_list, entry) {
		if (priv->constructor == constructor) {
			/* Return the data after the entry's metadata */
			spin_unlock(&dev->private_lock);
			return &priv[1];
		}
	}
	spin_unlock(&dev->private_lock);

	priv = constructor(dev);
	if (priv) {
		/* Set the constructor in the metadata */
		priv[-1].constructor = constructor;
	}

	return priv;
}

/**
 * nfp_device_private_alloc() - Constructor allocation method
 * @dev:		NFP device
 * @private_size:	Size to allocate
 * @destructor:		Destructor function to call on device close, or NULL
 *
 * Allocate your private area - must be called in the constructor
 * function passed to nfp_device_private().
 *
 * Return: Allocated memory, or NULL
 */
void *nfp_device_private_alloc(struct nfp_device *dev,
			       size_t private_size,
			       void (*destructor)(void *private_data))
{
	struct nfp_device_private *priv;

	priv = kzalloc(sizeof(*priv) + private_size, GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->destructor = destructor;
	spin_lock(&dev->private_lock);
	list_add(&priv->entry, &dev->private_list);
	spin_unlock(&dev->private_lock);
	return &priv[1];
}
