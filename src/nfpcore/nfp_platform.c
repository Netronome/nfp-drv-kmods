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
 * nfp_platform.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "nfp.h"
#include "nfp_cpp.h"

/**
 * nfp_platform_device_register_unit() - Multi-unit NFP CPP bus devices
 * @cpp:	NFP CPP handle
 * @type:	Platform driver name to match to
 * @unit:	Unit number of this device
 * @units:	Maximum units per NFP CPP bus
 *
 * NOTE: Use nfp_platform_device_unregister() to release the
 * struct platform_device.
 *
 * Return: struct platform_device *, or NULL
 */
struct platform_device *nfp_platform_device_register_unit(struct nfp_cpp *cpp,
							  const char *type,
							  int unit, int units)
{
	struct device *dev = nfp_cpp_device(cpp);
	const struct nfp_platform_data pdata = {
		.nfp = nfp_cpp_device_id(cpp),
		.unit = unit,
	};
	struct platform_device *pdev;
	int err;
	int id;

	id = nfp_cpp_device_id(cpp) * units + unit;

	pdev = platform_device_alloc(type, id);
	if (!pdev) {
		dev_err(dev, "Can't create '%s.%d' platform device\n",
			type, id);
		return NULL;
	}

	pdev->dev.parent = dev;
	platform_device_add_data(pdev, &pdata, sizeof(pdata));

	err = platform_device_add(pdev);
	if (err < 0) {
		dev_err(dev, "Can't register '%s.%d' platform device\n",
			type, id);
		platform_device_put(pdev);
		return NULL;
	}

	return pdev;
}

/**
 * nfp_platform_device_register() - NFP CPP bus device registration
 * @cpp:	NFP CPP handle
 * @type:	Platform driver name to match to
 *
 * NOTE: Use nfp_platform_device_unregister() to release the
 * struct platform_device.
 *
 * Return: struct platform_device *, or NULL
 */
struct platform_device *nfp_platform_device_register(struct nfp_cpp *cpp,
						     const char *type)
{
	return nfp_platform_device_register_unit(cpp, type, 0, 1);
}

/**
 * nfp_platform_device_unregister() - Unregister a NFP CPP bus device
 * @pdev:	Platform device
 *
 */
void nfp_platform_device_unregister(struct platform_device *pdev)
{
	platform_device_unregister(pdev);
}
