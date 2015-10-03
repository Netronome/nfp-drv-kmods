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
 * nfp_i2c.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include "nfp.h"
#include "nfp_cpp.h"
#include "nfp_i2c.h"
#include "nfp_gpio.h"

#define I2C_SPEED_DEFAULT       (100 * 1000) /* 100Khz */
#define I2C_TIMEOUT_DEFAULT     (100)        /* 100ms */

#define I2C_BUS_MAX 8		/* Up to 8 I2C busses */

struct i2c_driver {
	void (*set_scl)(void *priv, int bit);
	int (*get_scl)(void *priv);
	void (*set_sda)(void *priv, int bit);	/* -1 = tristate for input */
	int (*get_sda)(void *priv);
	unsigned delay;
	long timeout_ms;
	void *priv;
};

static inline void i2c_set_scl(struct i2c_driver *bus, int bit)
{
	bus->set_scl(bus->priv, bit);
}

static inline int i2c_get_scl(struct i2c_driver *bus)
{
	return bus->get_scl(bus->priv);
}

static inline void i2c_set_sda(struct i2c_driver *bus, int bit)
{
	bus->set_sda(bus->priv, bit);
}

static inline int i2c_get_sda(struct i2c_driver *bus)
{
	return bus->get_sda(bus->priv);
}

static inline void i2c_clock_delay(struct i2c_driver *bus)
{
	udelay(bus->delay);
}

static inline int i2c_start(struct i2c_driver *bus)
{
	int timeout = 100;

	i2c_set_scl(bus, 1);

	/* Check for clock stretched by the slave
	 */
	while ((timeout-- > 0) && (i2c_get_scl(bus) == 0))
		i2c_clock_delay(bus);

	if (timeout <= 0)
		return -EAGAIN;

	i2c_set_sda(bus, 1);
	i2c_clock_delay(bus);
	i2c_clock_delay(bus);
	i2c_clock_delay(bus);
	i2c_set_sda(bus, 0);
	i2c_clock_delay(bus);
	i2c_set_scl(bus, 0);

	return 0;
}

static inline void i2c_stop(struct i2c_driver *bus)
{
	i2c_set_scl(bus, 0);
	i2c_set_sda(bus, 0);
	i2c_clock_delay(bus);
	i2c_set_scl(bus, 1);
	i2c_clock_delay(bus);
	i2c_clock_delay(bus);
	i2c_set_sda(bus, 1);
	i2c_clock_delay(bus);
	i2c_set_sda(bus, -1);
	i2c_set_scl(bus, -1);
}

static inline void i2c_ack(struct i2c_driver *bus, int ack)
{
	i2c_set_scl(bus, 0);
	i2c_clock_delay(bus);
	i2c_set_sda(bus, ack);
	i2c_clock_delay(bus);
	i2c_set_scl(bus, 1);
	i2c_clock_delay(bus);
	i2c_clock_delay(bus);
	i2c_set_scl(bus, 0);
	i2c_clock_delay(bus);
}

static inline int i2c_writeb(struct i2c_driver *bus, u8 data)
{
	int i, nack;

	for (i = 0; i < 8; i++) {
		i2c_set_scl(bus, 0);
		i2c_clock_delay(bus);
		i2c_set_sda(bus, (data >> (7 - i)) & 1);
		i2c_clock_delay(bus);
		i2c_set_scl(bus, 1);
		i2c_clock_delay(bus);
		i2c_clock_delay(bus);
	}

	i2c_set_scl(bus, 0);
	i2c_set_sda(bus, -1);
	i2c_clock_delay(bus);
	i2c_clock_delay(bus);
	i2c_set_scl(bus, 1);
	i2c_clock_delay(bus);
	nack = i2c_get_sda(bus);
	i2c_set_scl(bus, 0);
	i2c_clock_delay(bus);
	i2c_set_sda(bus, -1);

	if (nack)
		return -ENODEV;

	return 0;
}

static inline u8 i2c_readb(struct i2c_driver *bus, int ack)
{
	u8 tmp;
	int i;

	tmp = 0;
	i2c_set_sda(bus, -1);
	for (i = 0; i < 8; i++) {
		i2c_set_scl(bus, 0);
		i2c_clock_delay(bus);
		i2c_clock_delay(bus);
		i2c_set_scl(bus, 1);
		i2c_clock_delay(bus);
		tmp |= i2c_get_sda(bus) << (7 - i);
		i2c_clock_delay(bus);
	}

	i2c_ack(bus, ack);

	return tmp;
}

static inline void i2c_reset(struct i2c_driver *bus)
{
	int i;

	i2c_set_scl(bus, 1);
	i2c_set_sda(bus, 1);
	i2c_set_sda(bus, -1);

	/* Clock out 9 ticks, to drain (hopefully) any I2C device */
	for (i = 0; i < 9; i++) {
		i2c_set_scl(bus, 0);
		i2c_clock_delay(bus);
		i2c_set_scl(bus, 1);
		i2c_clock_delay(bus);
	}

	i2c_stop(bus);
}

static int i2c_init(struct i2c_driver *bus, unsigned clock_rate)
{
	/* Convert from freq to usec */
	bus->delay = (1000000 / clock_rate);

	i2c_reset(bus);

	return 0;
}

static int ms_timeout(struct timeval *tv_epoc, long timeout_ms)
{
	struct timeval tv;
	unsigned long ms;

	if (timeout_ms < 0) {
		do_gettimeofday(tv_epoc);
		return 0;
	}

	do_gettimeofday(&tv);

	ms = (tv.tv_usec - tv_epoc->tv_usec) / 1000;
	ms += (tv.tv_sec - tv_epoc->tv_sec) * 1000;

	return (timeout_ms < ms);
}

static int i2c_cmd(struct i2c_driver *bus, u8 chip,
		   const u8 *w_buff, size_t w_len,
		   u8 *r_buff, size_t r_len)
{
	int i, err;
	struct timeval tv;

	ms_timeout(&tv, -1);

retry:
	err = i2c_start(bus);
	if (err < 0)
		return err;

	/* Send chip ID */
	err = i2c_writeb(bus, chip << 1);
	if (err < 0) {
		i2c_stop(bus);
		return err;
	}

	/* Send register address */
	while (w_len > 0) {
		w_len--;
		err = i2c_writeb(bus, *(w_buff++));
		if (err < 0) {
			i2c_stop(bus);
			return err;
		}
	}

	if (r_len == 0)
		goto done;

	/* Repeated Start */
	err = i2c_start(bus);
	if (err == -EAGAIN && !ms_timeout(&tv, bus->timeout_ms))
		goto retry;

	err = i2c_writeb(bus, (chip << 1) | 1);
	if (err < 0) {
		i2c_stop(bus);
		return err;
	}

	/* Get register data */
	for (i = 0; i < r_len; i++) {
		int ack = (i == (r_len - 1)) ? 1 : 0;

		r_buff[i] = i2c_readb(bus, ack);
	}

	if (i != r_len)
		err = -EIO;

done:
	i2c_stop(bus);

	return err;
}

static int i2c_write(struct i2c_driver *bus, u8 chip,
		     unsigned int addr, size_t alen,
		     const u8 *buff, size_t buff_len)
{
	int i, err;
	struct timeval tv;

	ms_timeout(&tv, -1);

	do {
		err = i2c_start(bus);
	} while (err == -EAGAIN && !ms_timeout(&tv, bus->timeout_ms));

	if (err) {
		i2c_stop(bus);
		return err;
	}

	/* Send chip ID */
	err = i2c_writeb(bus, chip << 1);
	if (err < 0) {
		i2c_stop(bus);
		return err;
	}

	/* Send register address */
	while (alen > 0) {
		alen--;
		err = i2c_writeb(bus, addr >> (alen * 8));
		if (err < 0) {
			i2c_stop(bus);
			return err;
		}
	}

	/* Get register data */
	for (i = 0; i < buff_len; i++) {
		err = i2c_writeb(bus, buff[i]);
		if (err < 0) {
			i2c_stop(bus);
			return err;
		}
	}
	i2c_stop(bus);

	return 0;
}

static int i2c_read(struct i2c_driver *bus, u8 chip,
		    unsigned int addr, size_t alen,
		    u8 *buff, size_t buff_len)
{
	int i, err;
	struct timeval tv;

	ms_timeout(&tv, -1);

retry:
	err = i2c_start(bus);
	if (err == -EAGAIN && !ms_timeout(&tv, bus->timeout_ms))
		goto retry;

	/* Send chip ID */
	err = i2c_writeb(bus, chip << 1);
	if (err < 0) {
		i2c_stop(bus);
		return err;
	}

	/* Send register address */
	while (alen > 0) {
		alen--;
		err = i2c_writeb(bus, addr >> (alen * 8));
		if (err < 0) {
			i2c_stop(bus);
			return err;
		}
	}

	/* Repeated Start */
	err = i2c_start(bus);
	if (err == -EAGAIN && !ms_timeout(&tv, bus->timeout_ms))
		goto retry;

	err = i2c_writeb(bus, (chip << 1) | 1);
	if (err < 0) {
		i2c_stop(bus);
		return err;
	}

	/* Get register data */
	for (i = 0; i < buff_len; i++) {
		int ack = (i == (buff_len - 1)) ? 1 : 0;

		buff[i] = i2c_readb(bus, ack);
	}
	i2c_stop(bus);

	return 0;
}

struct i2c_gpio_priv {
	unsigned int gpio_scl;
	unsigned int gpio_sda;
	unsigned int speed_hz;
	struct nfp_device *dev;
};

static void i2c_gpio_set_scl(void *priv, int bit)
{
	struct i2c_gpio_priv *i2c = priv;

	if (bit < 0) {		/* Tristate */
		nfp_gpio_direction(i2c->dev, i2c->gpio_scl, 0);
	} else {
		nfp_gpio_set(i2c->dev, i2c->gpio_scl, bit);
		nfp_gpio_direction(i2c->dev, i2c->gpio_scl, 1);
	}
}

static int i2c_gpio_get_scl(void *priv)
{
	struct i2c_gpio_priv *i2c = priv;
	int val;

	/* On the NFP, detection of SCL clock
	 * stretching by slave is not possible
	 * on Bus 0, since GPIO0 may have a
	 * pull-up/pull-down to force the ARM/PCIE
	 * boot selection. In this case, always
	 * return 1, so that the SCL clock
	 * stretching logic is not used.
	 */
	if (i2c->gpio_scl == 0)
		return 1;

	nfp_gpio_direction(i2c->dev, i2c->gpio_scl, 0);
	val = nfp_gpio_get(i2c->dev, i2c->gpio_scl);
	nfp_gpio_direction(i2c->dev, i2c->gpio_scl, 1);

	return val;
}

static void i2c_gpio_set_sda(void *priv, int bit)
{
	struct i2c_gpio_priv *i2c = priv;

	if (bit < 0) {		/* Tristate */
		nfp_gpio_direction(i2c->dev, i2c->gpio_sda, 0);
	} else {
		nfp_gpio_set(i2c->dev, i2c->gpio_sda, bit);
		nfp_gpio_direction(i2c->dev, i2c->gpio_sda, 1);
	}
}

static int i2c_gpio_get_sda(void *priv)
{
	struct i2c_gpio_priv *i2c = priv;

	return nfp_gpio_get(i2c->dev, i2c->gpio_sda);
}

static int i2c_gpio_init(struct i2c_driver *drv, struct i2c_gpio_priv *priv)
{
	int err;

	drv->set_scl = i2c_gpio_set_scl;
	drv->get_scl = i2c_gpio_get_scl;
	drv->set_sda = i2c_gpio_set_sda;
	drv->get_sda = i2c_gpio_get_sda;
	drv->priv = priv;

	i2c_gpio_set_sda(priv, -1);
	i2c_gpio_set_scl(priv, -1);

	err = i2c_init(drv, priv->speed_hz);
	if (err < 0)
		drv->priv = NULL;

	return err;
}

struct nfp_i2c {
	int initialized;
	struct i2c_driver bus;
	struct i2c_gpio_priv gpio;
};

/**
 * nfp_i2c_alloc() - NFP I2C Bus creation
 * @nfp:	NFP Device handle
 * @gpio_scl:	NFP GPIO pin for I2C SCL
 * @gpio_sda:	NFP GPIO pin for I2C SDA
 *
 * Return: NFP I2C handle, or NULL
 */
struct nfp_i2c *nfp_i2c_alloc(struct nfp_device *nfp,
			      int gpio_scl, int gpio_sda)
{
	struct nfp_i2c *i2c;
	u32 model;
	int pins;

	model = nfp_cpp_model(nfp_device_cpp(nfp));

	if (NFP_CPP_MODEL_IS_3200(model))
		pins = 12;
	else if (NFP_CPP_MODEL_IS_6000(model))
		pins = 32;
	else
		return NULL;

	if (gpio_scl < 0 || gpio_scl >= pins)
		return NULL;

	if (gpio_sda < 0 || gpio_sda >= pins)
		return NULL;

	if (gpio_scl == gpio_sda)
		return NULL;

	i2c = kzalloc(sizeof(*i2c), GFP_KERNEL);

	if (!i2c)
		return NULL;

	i2c->gpio.gpio_scl = gpio_scl;
	i2c->gpio.gpio_sda = gpio_sda;
	i2c->gpio.speed_hz = I2C_SPEED_DEFAULT;
	i2c->gpio.dev      = nfp;
	i2c->bus.timeout_ms = I2C_TIMEOUT_DEFAULT;

	i2c->initialized = 0;

	return i2c;
}

/**
 * nfp_i2c_free() - Release a NFP I2C bus, and free its memory
 * @i2c:	NFP I2C handle
 *
 * As a side effect, the GPIO pins used for SCL and SDA will
 * be set to the 'input' direction when this call returns.
 */
void nfp_i2c_free(struct nfp_i2c *i2c)
{
	kfree(i2c);
}

/**
 * nfp_i2c_set_speed() - NFP I2C clock rate
 * @i2c:	NFP I2C handle
 * @speed_hz:	Speed in HZ. Use 0 for the default (100Khz)
 *
 * Return: 0, or -ERRNO
 */
int nfp_i2c_set_speed(struct nfp_i2c *i2c, unsigned int speed_hz)
{
	if (speed_hz == 0)
		speed_hz = I2C_SPEED_DEFAULT;

	i2c->gpio.speed_hz = speed_hz;

	return 0;
}

/**
 * nfp_i2c_set_timeout() - NFP I2C Timeout setup
 * @i2c:	NFP I2C handle
 * @timeout_ms:	Timeout in milliseconds, -1 for forever
 *
 * Return: 0, or -ERRNO
 */
int nfp_i2c_set_timeout(struct nfp_i2c *i2c, long timeout_ms)
{
	if (timeout_ms <= 0)
		timeout_ms = I2C_TIMEOUT_DEFAULT;

	i2c->bus.timeout_ms = timeout_ms;

	return 0;
}

/**
 * nfp_i2c_cmd() - NFP I2C Command
 * @i2c:	I2C Bus
 * @i2c_dev:	I2C Device ( 7-bit address )
 * @w_buff:	Data to write to device
 * @w_len:	Length in bytes to write (must be >= 1)
 * @r_buff:	Data to read from device (can be NULL if r_len == 0)
 * @r_len:	Length in bytes to read (must be >= 0)
 *
 * Return: 0, or -ERRNO
 */
int nfp_i2c_cmd(struct nfp_i2c *i2c, int i2c_dev,
		const void *w_buff, size_t w_len, void *r_buff, size_t r_len)
{
	if (!i2c->initialized) {
		int err = i2c_gpio_init(&i2c->bus, &i2c->gpio);

		if (err < 0)
			return err;
		i2c->initialized = 1;
	}

	return i2c_cmd(&i2c->bus, i2c_dev, w_buff, w_len, r_buff, r_len);
}

/**
 * nfp_i2c_read() - NFP I2C Read
 * @i2c:	I2C Bus
 * @i2c_dev:	I2C Device ( 7-bit address )
 * @addr:	Device address
 * @a_len:	Length (in bytes) of the device address
 * @r_buff:	Data to read from device (can be NULL if r_len == 0)
 * @r_len:	Length in bytes to read (must be >= 0)
 *
 * Return: 0, or -ERRNO
 */
int nfp_i2c_read(struct nfp_i2c *i2c, int i2c_dev, u32 addr,
		 size_t a_len, void *r_buff, size_t r_len)
{
	if (!i2c->initialized) {
		int err = i2c_gpio_init(&i2c->bus, &i2c->gpio);

		if (err < 0)
			return err;
		i2c->initialized = 1;
	}

	return i2c_read(&i2c->bus, i2c_dev, addr, a_len, r_buff, r_len);
}

/**
 * nfp_i2c_read() - NFP I2C Write
 * @i2c:	I2C Bus
 * @i2c_dev:	I2C Device ( 7-bit address )
 * @addr:	Device address
 * @a_len:	Length (in bytes) of the device address
 * @w_buff:	Data to write to device
 * @w_len:	Length in bytes to write (must be >= 1)
 *
 * Return: 0, or -ERRNO
 */
int nfp_i2c_write(struct nfp_i2c *i2c, int i2c_dev, u32 addr,
		  size_t a_len, const void *w_buff, size_t w_len)
{
	if (!i2c->initialized) {
		int err = i2c_gpio_init(&i2c->bus, &i2c->gpio);

		if (err < 0)
			return err;
		i2c->initialized = 1;
	}

	return i2c_write(&i2c->bus, i2c_dev, addr, a_len, w_buff, w_len);
}
