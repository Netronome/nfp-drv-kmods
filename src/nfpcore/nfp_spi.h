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
 * nfp_spi.h
 */

#ifndef NFP_SPI_H
#define NFP_SPI_H

#include <linux/bitops.h>

#define CS_SELECT       BIT(0)
#define CS_DESELECT     BIT(1)

struct nfp_spi;

struct nfp_spi *nfp_spi_acquire(struct nfp_device *nfp, int bus, int width);
void nfp_spi_release(struct nfp_spi *spi);
int nfp_spi_speed_set(struct nfp_spi *spi, int hz);
int nfp_spi_speed_get(struct nfp_spi *spi, int *hz);
int nfp_spi_mode_set(struct nfp_spi *spi, int mode);
int nfp_spi_mode_get(struct nfp_spi *spi, int *mode);

int nfp6000_spi_transact(struct nfp_spi *spi, int cs, int cs_action,
			 const void *tx, uint32_t tx_bit_cnt,
			 void *rx, uint32_t rx_bit_cnt,
			 int mdio_data_drive_disable);

int nfp_spi_read(struct nfp_spi *spi, int cs,
		 unsigned int cmd_len, const void *cmd,
		 unsigned int res_len, void *res);

int nfp_spi_write(struct nfp_spi *spi, int cs,
		  unsigned int cmd_len, const void *cmd,
		  unsigned int dat_len, const void *dat);

#endif /* NFP_SPI_H */
