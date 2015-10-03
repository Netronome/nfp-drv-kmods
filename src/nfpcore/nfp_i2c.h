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
 * nfp_i2c.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef NFP_I2C_H
#define NFP_I2C_H

struct nfp_i2c;

struct nfp_i2c *nfp_i2c_alloc(struct nfp_device *dev,
			      int gpio_scl, int gpio_sda);
void nfp_i2c_free(struct nfp_i2c *bus);

int nfp_i2c_set_speed(struct nfp_i2c *bus, unsigned int speed_hz);
int nfp_i2c_set_timeout(struct nfp_i2c *bus, long timeout_ms);

int nfp_i2c_cmd(struct nfp_i2c *bus, int i2c_dev,
		const void *w_buff, size_t w_len,
		void *r_buff, size_t r_len);
int nfp_i2c_read(struct nfp_i2c *bus, int i2c_dev,
		 u32 addr, size_t a_len,
		 void *r_buff, size_t r_len);
int nfp_i2c_write(struct nfp_i2c *bus, int i2c_dev,
		  u32 address, size_t a_len,
		  const void *w_buff, size_t w_len);

#endif /* NFP_I2C_H */
