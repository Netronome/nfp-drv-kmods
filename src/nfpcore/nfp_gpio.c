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
 * nfp_gpio.c
 */

#include <linux/kernel.h>

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_gpio.h"

#include "nfp3200/nfp3200.h"
#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_gpio.h"

#include "nfp6000/nfp6000.h"

#define NFP_ARM_GPIO                                         (0x403000)

struct gpio {
	int pins;
	int (*read)(struct nfp_cpp *cpp, int csr_offset, u32 *val);
	int (*write)(struct nfp_cpp *cpp, int csr_offset, u32 val);
};

static int nfp3200_csr_readl(struct nfp_cpp *cpp, int csr_offset, u32 *val)
{
	return nfp_xpb_readl(cpp, NFP_XPB_GPIO + csr_offset, val);
}

static int nfp3200_csr_writel(struct nfp_cpp *cpp, int csr_offset, u32 val)
{
	return nfp_xpb_writel(cpp, NFP_XPB_GPIO + csr_offset, val);
}

#define NFP_ARM_ID  NFP_CPP_ID(NFP_CPP_TARGET_ARM, NFP_CPP_ACTION_RW, 0)

static int nfp6000_csr_readl(struct nfp_cpp *cpp, int csr_offset, u32 *val)
{
	return nfp_cpp_readl(cpp, NFP_ARM_ID, NFP_ARM_GPIO + csr_offset, val);
}

static int nfp6000_csr_writel(struct nfp_cpp *cpp, int csr_offset, u32 val)
{
	return nfp_cpp_writel(cpp, NFP_ARM_ID, NFP_ARM_GPIO + csr_offset, val);
}

static void *gpio_new(struct nfp_device *nfp)
{
	u32 model;
	struct gpio *gpio;

	gpio = nfp_device_private_alloc(nfp, sizeof(*gpio), NULL);
	if (!gpio)
		return NULL;

	model = nfp_cpp_model(nfp_device_cpp(nfp));

	if (NFP_CPP_MODEL_IS_3200(model)) {
		gpio->pins = 12;
		gpio->read = nfp3200_csr_readl;
		gpio->write = nfp3200_csr_writel;
	} else if (NFP_CPP_MODEL_IS_6000(model)) {
		gpio->pins = 32;
		gpio->read = nfp6000_csr_readl;
		gpio->write = nfp6000_csr_writel;
	} else {
		gpio->pins = 0;
		gpio->read = NULL;
		gpio->write = NULL;
	}

	return gpio;
}

/**
 * nfp_gpio_pins() - Return the number of pins supported by this NFP
 * @dev:	NFP Device handle
 *
 * Return: Number of GPIO pins, or -ERRNO
 */
int nfp_gpio_pins(struct nfp_device *dev)
{
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 model;
	int max_pin;

	if (!cpp)
		return -ENODEV;

	model = nfp_cpp_model(cpp);
	if (NFP_CPP_MODEL_IS_3200(model))
		max_pin = 12;
	else if (NFP_CPP_MODEL_IS_6000(model))
		max_pin = 32;
	else
		max_pin = 0;

	return max_pin;
}

/**
 * nfp_gpio_direction() - GPIO Pin Setup
 * @dev:	NFP Device handle
 * @gpio_pin:	GPIO Pin (0 .. 11)
 * @is_output:	0 = input, 1 = output
 *
 * Return: 0, or -ERRNO
 */
int nfp_gpio_direction(struct nfp_device *dev, int gpio_pin, int is_output)
{
	struct gpio *gpio = nfp_device_private(dev, gpio_new);
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 mask;
	int err;

	if (gpio_pin < 0 || gpio_pin >= gpio->pins)
		return -EINVAL;

	mask = (1 << gpio_pin);

	if (is_output)
		err = gpio->write(cpp, NFP_GPIO_PDSR, mask);
	else
		err = gpio->write(cpp, NFP_GPIO_PDCR, mask);

	return err < 0 ? err : 0;
}

/**
 * nfp_gpio_get_direction() - GPIO Get Pin Direction
 * @dev:	NFP Device handle
 * @gpio_pin:	GPIO Pin (0 .. X)
 * @is_output:	0 = input, 1 = output
 *
 * Return: 0, or -ERRNO
 */
int nfp_gpio_get_direction(struct nfp_device *dev, int gpio_pin, int *is_output)
{
	struct gpio *gpio = nfp_device_private(dev, gpio_new);
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 val;
	int err;

	if (gpio_pin < 0 || gpio_pin >= gpio->pins)
		return -EINVAL;

	err = gpio->read(cpp, NFP_GPIO_PDPR, &val);

	if (err >= 0)
		*is_output = (val >> gpio_pin) & 1;

	return err;
}

/**
 * nfp_gpio_get() - GPIO Pin Input
 * @dev:	NFP Device handle
 * @gpio_pin:	GPIO Pin (0 .. 11)
 *
 * Return: 0, 1 = value of pin, or -ERRNO
 */
int nfp_gpio_get(struct nfp_device *dev, int gpio_pin)
{
	struct gpio *gpio = nfp_device_private(dev, gpio_new);
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 mask, value;
	int err;

	if (gpio_pin < 0 || gpio_pin >= gpio->pins)
		return -EINVAL;

	err = gpio->read(cpp, NFP_GPIO_PLR, &value);
	if (err < 0)
		return err;

	mask = (1 << gpio_pin);

	return (value & mask) ? 1 : 0;
}

/**
 * nfp_gpio_set() - GPIO Pin Output
 * @dev:	NFP Device handle
 * @gpio_pin:	GPIO Pin (0 .. 11)
 * @value:	0, 1
 *
 * Return: 0, or -ERRNO
 */
int nfp_gpio_set(struct nfp_device *dev, int gpio_pin, int value)
{
	struct gpio *gpio = nfp_device_private(dev, gpio_new);
	struct nfp_cpp *cpp = nfp_device_cpp(dev);
	u32 mask;
	int err;

	if (gpio_pin < 0 || gpio_pin >= gpio->pins)
		return -EINVAL;

	mask = (1 << gpio_pin);

	if (value == 0)
		err = gpio->write(cpp, NFP_GPIO_POCR, mask);
	else
		err = gpio->write(cpp, NFP_GPIO_POSR, mask);

	return err < 0 ? err : 0;
}
