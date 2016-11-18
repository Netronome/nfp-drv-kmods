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
 * nfp_spi.c
 * Authors: David Brunecz <david.brunecz@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 */
#include <linux/kernel.h>
#include <linux/time.h>

#include "nfp.h"
#include "nfp_spi.h"

#include "nfp6000/nfp6000.h"

#include "kcompat.h"

struct nfp_spi {
	struct nfp_cpp *cpp;
	struct nfp_cpp_area *csr;
	int mode;
	int clkdiv;
	int bus;
	int width;
	int key;
};

#define NFP_ARM_SPI                                          (0x403c00)

#define NFP_SPI_TIMEOUT_MS         100

/* NFP6000 SPI CONTROLLER defines */
#define NFP_SPI_PORTMC(x)    (0x10 + (((x) & 3) << 2))
#define   NFP_SPI_PORTMC_DATADRIVEDISABLE                 BIT(31)
#define   NFP_SPI_PORTMC_CLOCKIDLE                        BIT(29)
#define   NFP_SPI_PORTMC_SELECT(_x)                       (((_x) & 0xf) << 24)
#define   NFP_SPI_PORTMC_DATAWIDTH(_x)                    (((_x) & 0x3) << 20)
#define   NFP_SPI_PORTMC_DATAINTRAIL                      BIT(19)
#define   NFP_SPI_PORTMC_DATAINLEAD                       BIT(18)
#define   NFP_SPI_PORTMC_DATAOUTTRAIL                     BIT(17)
#define   NFP_SPI_PORTMC_DATAOUTLEAD                      BIT(16)
#define   NFP_SPI_PORTMC_CLOCKDISABLE                     BIT(15)
#define   NFP_SPI_PORTMC_CLOCKEDGECOUNT(_x)               (((_x) & 0x7f) << 8)
#define NFP_SPI_PORTCFG(x)   (0x00 + (((x) & 3) << 2))
#define   NFP_SPI_PORTCFG_MODE                            BIT(31)
#define     NFP_SPI_PORTCFG_MODE_AUTOMATIC                (0 << 31)
#define     NFP_SPI_PORTCFG_MODE_MANUAL                   BIT(31)
#define NFP_SPI_PORTMDO(x)   (0x20 + (((x) & 3) << 2))
#define NFP_SPI_PORTMDI(x)   (0x30 + (((x) & 3) << 2))
#define NFP_SPI_SPIIOCONFIG                                  0x00000100
#define NFP_SPI_SPIIOIDLESTATUS                              0x00000104
#define NFP_SPI_WE                         0x0000010c
#define   NFP_SPI_WE_AVAILABLE  BIT(4)
#define   NFP_SPI_WE_WRITEENABLETARGET(_x) (((_x) & 0xf) << 0)
#define   NFP_SPI_PORTCFG_BUSY                            BIT(30)

#define VALID_CS(cs)            ((cs >= 0) && (cs <= 3))
#define CS_OFF                  NFP_SPI_PORTMC_SELECT(0xf)
#define CS_BITS(cs)                                       \
		((VALID_CS(cs)) ?                         \
		NFP_SPI_PORTMC_SELECT((0xf & ~(1 << cs))) \
		: CS_OFF)

#define SPIMODEBITS(s)                                                \
	((s->mode & BIT(1) ? NFP_SPI_PORTMC_CLOCKIDLE : 0) |        \
	 (s->mode & BIT(0)                                          \
	  ? (NFP_SPI_PORTMC_DATAINTRAIL | NFP_SPI_PORTMC_DATAOUTLEAD) \
	  : (NFP_SPI_PORTMC_DATAINLEAD | NFP_SPI_PORTMC_DATAOUTTRAIL)))

#define CPHA(mode)  (mode & 1)

#define NFP6_SPI_DEFAULT_CTRL(edges, spi)          \
	(SPIMODEBITS(spi)                          \
	 | NFP_SPI_PORTMC_DATAWIDTH(spi->width)    \
	 | (spi)->clkdiv                           \
	 | NFP_SPI_PORTMC_CLOCKEDGECOUNT(edges))

#define SET_EDGE_COUNT(ctrl, cnt) \
	do {                                                    \
		ctrl &= ~0x7f00;                                \
		ctrl |= NFP_SPI_PORTMC_CLOCKEDGECOUNT(cnt);     \
	} while (0)

#define SPI_DEFAULT_MODE        (BIT(1) | BIT(0))	/* SPI_MODE3 */
#define SPIXDAT23_OFFS          8
#define SPI_MAX_BITS_PER_CTRL_WRITE 32

/* SPI source clock is PCLK(1GHz), the clock divider bits are
 * the count of PCLKs per SPI half-cycle, 8bits of divider give
 * a range 1-256 per half cycle or 2-512 per cycle, giving a
 * clock range of 500MHz down to ~2MHz
 *
 * pclk_freq(1000MHz) / (2 * (1 + pclk_half_cycle_count_bits)) = spi_freq
 */
#define MHZ(x)                  ((x) * 1000 * 1000)
#define PCLK_HZ                MHZ(1000)
#define MIN_SPI_HZ              ((PCLK_HZ / 512))	/*   ~2MHz */
#define MAX_SPI_HZ              ((PCLK_HZ /   2))	/* ~500MHz */
#define DEF_SPI_HZ              MHZ(5)

#define BITS_TO_BYTES(x)    (((x) + 7) / 8)

static int nfp6000_spi_csr_readl(struct nfp_spi *spi, u32 csr, u32 *val)
{
	return nfp_cpp_area_readl(spi->csr, csr, val);
}

static int nfp6000_spi_csr_writel(struct nfp_spi *spi, u32 csr, u32 val)
{
	return nfp_cpp_area_writel(spi->csr, csr, val);
}

/******************************************************************************/

#define offset_of(s, e)         ((intptr_t)&((s *)NULL)->e)

/******************************************************************************/

static int nfp6000_spi_run_clock(struct nfp_spi *spi, u32 control)
{
	u32 tmp;
	int err;
	struct timespec ts, timeout = {
		.tv_sec = NFP_SPI_TIMEOUT_MS / 1000,
		.tv_nsec = (NFP_SPI_TIMEOUT_MS % 1000) * 1000000,
	};

	err = nfp6000_spi_csr_writel(spi, NFP_SPI_PORTMC(spi->bus), control);
	if (err < 0)
		return err;

	ts = CURRENT_TIME;
	timeout = timespec_add(ts, timeout);

	for (ts = CURRENT_TIME;
	     timespec_compare(&ts, &timeout) < 0; ts = CURRENT_TIME) {
		err =
		    nfp6000_spi_csr_readl(spi, NFP_SPI_PORTCFG(spi->bus),
					  &tmp);
		if (err < 0)
			return err;

		if (!(tmp & NFP_SPI_PORTCFG_BUSY))
			return 0;
	}

	return -ETIMEDOUT;
}

static int nfp_spi_set_pin_association(struct nfp_spi *spi, int port, int pin)
{
	unsigned int val;
	int err;

	err = nfp6000_spi_csr_readl(spi, NFP_SPI_SPIIOCONFIG, &val);
	if (err < 0)
		return err;
	val &= ~(0x3 << (2 * ((pin & 3) - 1)));
	val |= (port & 3) << (2 * ((pin & 3) - 1));

	return nfp6000_spi_csr_writel(spi, NFP_SPI_SPIIOCONFIG, val);
}

static int do_first_bit_cpha0_hack(struct nfp_spi *spi, u32 ctrl,
				   u32 mdo)
{
	u32 control = ctrl | NFP_SPI_PORTMC_CLOCKDISABLE;

	SET_EDGE_COUNT(control, 1);

	return nfp6000_spi_run_clock(spi, control);
}

static int nfp6000_spi_cs_control(struct nfp_spi *spi, int cs, u32 enable)
{
	u32 ctrl = NFP6_SPI_DEFAULT_CTRL(4, spi) |
	    NFP_SPI_PORTMC_CLOCKDISABLE;

	ctrl |= (enable) ? CS_BITS(cs) : CS_OFF;

	return nfp6000_spi_run_clock(spi, ctrl);
}

static int nfp6000_spi_set_manual_mode(struct nfp_spi *spi)
{
	u32 tmp;
	int err;

	err = nfp6000_spi_csr_readl(spi, NFP_SPI_PORTCFG(spi->bus), &tmp);
	if (err < 0)
		return err;
	tmp |= NFP_SPI_PORTCFG_MODE_MANUAL;
	return nfp6000_spi_csr_writel(spi, NFP_SPI_PORTCFG(spi->bus), tmp);
}

#define SPI0_CLKIDLE_OFFS   0
#define SPI1_CLKIDLE_OFFS   4
#define SPI2_CLKIDLE_OFFS   8
#define SPI3_CLKIDLE_OFFS   10
static int nfp6000_spi_set_clk_pol(struct nfp_spi *spi)
{
	int err;
	unsigned int val;
	unsigned int polbit_offset[] = { SPI0_CLKIDLE_OFFS, SPI1_CLKIDLE_OFFS,
		SPI2_CLKIDLE_OFFS, SPI3_CLKIDLE_OFFS
	};
	err = nfp6000_spi_csr_readl(spi, NFP_SPI_SPIIOIDLESTATUS, &val);
	if (err < 0)
		return err;
	val &= ~(1 << (polbit_offset[spi->bus & 3]));
	val |= ((spi->mode & 1) << (polbit_offset[spi->bus & 3]));

	return nfp6000_spi_csr_writel(spi, NFP_SPI_SPIIOIDLESTATUS, val);
}

/**
 * nfp6000_spi_transact() - Perform an arbitrary SPI transaction
 * @spi:                      SPI Bus
 * @cs:                       SPI Chip select (0..3)
 * @cs_action:                Combination of the CS_SELECT and CS_DESELECT flags
 * @tx:                       TX buffer
 * @tx_bit_cnt:               TX buffer size in bits
 * @rx:                       RX buffer
 * @rx_bit_cnt:               RX buffer size in bits
 * @mdio_data_drive_disable:  MDIO compatibility flag
 *
 * Return: 0 or -ERRNO
 */
int nfp6000_spi_transact(struct nfp_spi *spi, int cs, int cs_action,
			 const void *tx, u32 tx_bit_cnt,
			 void *rx, u32 rx_bit_cnt,
			 int mdio_data_drive_disable)
{
	int err = 0;
	int first_tx_bit = 1;
	u32 i, tmp, ctrl, clk_bit_cnt;
	u8 *_tx, *_rx;
	u32 txbits, rxbits;

	ctrl = SPIMODEBITS(spi);
	ctrl |=
	    NFP_SPI_PORTMC_DATAWIDTH(spi->width) | spi->clkdiv | CS_BITS(cs);

	if (mdio_data_drive_disable && !tx) {
		/* used only for MDIO compatibility/implementation
		 * via this routine
		 */
		ctrl |= NFP_SPI_PORTMC_DATADRIVEDISABLE;
	}

	if (VALID_CS(cs) && (cs_action & CS_SELECT)) {
		if (cs > 0)
			nfp_spi_set_pin_association(spi, spi->bus, cs);
		err = nfp6000_spi_cs_control(spi, cs, 1);
		if (err < 0)
			return err;
	}

	_tx = (u8 *)tx;
	_rx = (u8 *)rx;
	while ((tx_bit_cnt > 0) || (rx_bit_cnt > 0)) {
		txbits =
		    min_t(u32, SPI_MAX_BITS_PER_CTRL_WRITE, tx_bit_cnt);
		rxbits =
		    min_t(u32, SPI_MAX_BITS_PER_CTRL_WRITE, rx_bit_cnt);
		clk_bit_cnt = max_t(u32, rxbits, txbits);
		if (clk_bit_cnt < SPI_MAX_BITS_PER_CTRL_WRITE)
			clk_bit_cnt = (clk_bit_cnt + 7) & ~7;

		SET_EDGE_COUNT(ctrl, 2 * clk_bit_cnt);

		if (txbits) {
			if (txbits % 8)
				_tx[txbits / 8] |=
				    ((1 << (8 - (txbits % 8))) - 1);
			for (i = 0, tmp = 0; i < BITS_TO_BYTES(txbits);
			     i++, _tx++)
				tmp |= (_tx[0] << (24 - (i * 8)));
			for (; i < BITS_TO_BYTES(SPI_MAX_BITS_PER_CTRL_WRITE);
			     i++, _tx++)
				tmp |= (0xff << (24 - (i * 8)));
		} else {
			tmp = 0xffffffff;
		}
		err =
		    nfp6000_spi_csr_writel(spi, NFP_SPI_PORTMDO(spi->bus),
					   tmp);
		if (err < 0)
			return err;

		if (first_tx_bit && CPHA(spi->mode) == 0) {
			do_first_bit_cpha0_hack(spi, ctrl, tmp);
			first_tx_bit = 0;
		}

		err = nfp6000_spi_run_clock(spi, ctrl);
		if (err < 0)
			return err;

		if (rxbits) {
			err =
			    nfp6000_spi_csr_readl(spi,
						  NFP_SPI_PORTMDI(spi->bus),
						  &tmp);
			if (err < 0)
				return err;
			if (clk_bit_cnt < SPI_MAX_BITS_PER_CTRL_WRITE)
				tmp =
				    tmp << (SPI_MAX_BITS_PER_CTRL_WRITE -
					    clk_bit_cnt);

			for (i = 0; i < BITS_TO_BYTES(rxbits); i++, _rx++)
				_rx[0] = (tmp >> (24 - (i * 8))) & 0xff;
		}
		tx_bit_cnt -= txbits;
		rx_bit_cnt -= rxbits;
	}

	if (VALID_CS(cs) && (cs_action & CS_DESELECT))
		err = nfp6000_spi_cs_control(spi, cs, 0);

	return err;
}

/**
 * nfp_spi_read() - Perform a trivial SPI read
 * @spi:     SPI Bus
 * @cs:      SPI Chip select (0..3)
 * @cmd_len: Number of bytes in the command
 * @cmd:     SPI command
 * @res_len: Number of bytes of response
 * @res:     SPI response
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_read(struct nfp_spi *spi, int cs,
		 unsigned int cmd_len, const void *cmd,
		 unsigned int res_len, void *res)
{
	int err;

	err = nfp6000_spi_transact(spi, cs, CS_SELECT,
				   cmd, cmd_len * 8, NULL, 0, 0);
	if (err < 0)
		return err;

	return nfp6000_spi_transact(spi, cs, CS_DESELECT,
				    NULL, 0, res, res_len * 8, 0);
}

/**
 * nfp_spi_write() - Perform a trivial SPI write
 * @spi:     SPI Bus
 * @cs:      SPI Chip select (0..3)
 * @cmd_len: Number of bytes in the command
 * @cmd:     SPI command
 * @dat_len: Number of bytes of write data
 * @dat:     SPI write data
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_write(struct nfp_spi *spi, int cs,
		  unsigned int cmd_len, const void *cmd,
		  unsigned int dat_len, const void *dat)
{
	int err;

	err = nfp6000_spi_transact(spi, cs, CS_SELECT,
				   cmd, cmd_len * 8, NULL, 0, 0);
	if (err < 0)
		return err;

	return nfp6000_spi_transact(spi, cs, CS_DESELECT,
				    dat, dat_len * 8, NULL, 0, 0);
}

static int spi_interface_key(u16 interface)
{
	switch (NFP_CPP_INTERFACE_TYPE_of(interface)) {
	case NFP_CPP_INTERFACE_TYPE_ARM:
		return 1;
	case NFP_CPP_INTERFACE_TYPE_PCI:
		return NFP_CPP_INTERFACE_UNIT_of(interface) + 2;
	default:
		return -EINVAL;
	}
}

/**
 * nfp_spi_acquire() - Acquire a handle to one of the NFP SPI busses
 * @nfp:     NFP Device
 * @bus:     SPI Bus (0..3)
 * @width:   SPI Bus Width (0 (default), 1 bit, 2 bit, or 4 bit)
 *
 * Return: NFP SPI handle or ERR_PTR()
 */
struct nfp_spi *nfp_spi_acquire(struct nfp_device *nfp, int bus, int width)
{
	struct nfp_spi *spi;
	struct nfp_cpp *cpp;
	int err, key;
	u32 val;
	int timeout = 5 * 1000;	/* 5s */

	if (width != 0 && width != 1 && width != 2 && width != 4)
		return ERR_PTR(-EINVAL);

	cpp = nfp_device_cpp(nfp);
	key = spi_interface_key(nfp_cpp_interface(cpp));
	if (key < 0)
		return ERR_PTR(key);

	spi = kzalloc(sizeof(*spi), GFP_KERNEL);
	if (!spi)
		return ERR_PTR(-ENOMEM);

	spi->cpp = cpp;
	spi->key = key;
	spi->mode = SPI_DEFAULT_MODE;
	spi->bus = bus;
	spi->width = (width == 0 || width == 1) ? 1 : (width == 2) ? 2 : 3;

	spi->csr = nfp_cpp_area_alloc_acquire(spi->cpp,
					      NFP_CPP_ID(NFP_CPP_TARGET_ARM,
							 NFP_CPP_ACTION_RW, 0),
					      NFP_ARM_SPI, 0x400);

	if (!spi->csr) {
		kfree(spi);
		return ERR_PTR(-ENOMEM);
	}

	/* Is it locked? */
	for (; timeout > 0; timeout -= 100) {
		nfp6000_spi_csr_writel(spi, NFP_SPI_WE,
				       spi->key);
		nfp6000_spi_csr_readl(spi, NFP_SPI_WE, &val);
		if (val == spi->key)
			break;
		if (msleep_interruptible(100) != 100) {
			nfp_cpp_area_release_free(spi->csr);
			kfree(spi);
			return ERR_PTR(-EINTR);
		}
	}

	/* Unable to claim the SPI device lock? */
	if (timeout <= 0) {
		nfp_cpp_area_release_free(spi->csr);
		kfree(spi);
		return ERR_PTR(-EBUSY);
	}

	/* DAT1(SPI MISO) is disabled(configured as SPI port 0 DAT2/3)
	 * by default for SPI ports 2 and 3
	 */
	if (bus > 1) {
		err = nfp6000_spi_csr_readl(spi, NFP_SPI_SPIIOCONFIG, &val);
		if (err < 0) {
			kfree(spi);
			return ERR_PTR(err);
		}
		val &= ~(3 << SPIXDAT23_OFFS);
		val |= ((bus & 3) << SPIXDAT23_OFFS);
		err = nfp6000_spi_csr_writel(spi, NFP_SPI_SPIIOCONFIG, val);
		if (err < 0) {
			nfp_cpp_area_release_free(spi->csr);
			kfree(spi);
			return ERR_PTR(err);
		}
	}

	nfp_spi_speed_set(spi, DEF_SPI_HZ);
	err = nfp6000_spi_set_manual_mode(spi);
	if (err < 0) {
		nfp_cpp_area_release_free(spi->csr);
		kfree(spi);
		return ERR_PTR(err);
	}

	nfp6000_spi_set_clk_pol(spi);
	return spi;
}

/**
 * nfp_spi_release() - Release the handle to a NFP SPI bus
 * @spi:     NFP SPI bus
 */
void nfp_spi_release(struct nfp_spi *spi)
{
	nfp6000_spi_csr_writel(spi, NFP_SPI_WE,
			       NFP_SPI_WE_AVAILABLE);
	nfp_cpp_area_release_free(spi->csr);
	kfree(spi);
}

/**
 * nfp_spi_speed_set() - Set the clock rate of the NFP SPI bus
 * @spi:     NFP SPI bus
 * @hz:      SPI clock rate (-1 = default speed)
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_speed_set(struct nfp_spi *spi, int hz)
{
	if (hz < 0)
		hz = DEF_SPI_HZ;

	if (hz < MIN_SPI_HZ || hz > MAX_SPI_HZ)
		return -EINVAL;

	/* clkdiv = PCLK_HZ / 2 / hz - 1 */
	spi->clkdiv = PCLK_HZ / 2 / hz - 1;

	return 0;
}

/**
 * nfp_spi_speed_get() - Get the clock rate of the NFP SPI bus
 * @spi:     NFP SPI bus
 * @hz:      SPI clock rate pointer
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_speed_get(struct nfp_spi *spi, int *hz)
{
	if (hz)
		*hz = PCLK_HZ / 2 / (spi->clkdiv + 1);

	return 0;
}

/**
 * nfp_spi_mode_set() - Set the SPI mode
 * @spi:     NFP SPI bus
 * @mode:    SPI CPHA/CPOL mode (-1, 0, 1, 2, or 3)
 *
 * Use mode of '-1' for the default for this bus.
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_mode_set(struct nfp_spi *spi, int mode)
{
	if (mode < -1 || mode > 3)
		return -EINVAL;

	spi->mode = (mode == -1) ? SPI_DEFAULT_MODE : mode;
	nfp6000_spi_set_clk_pol(spi);

	return 0;
}

/**
 * nfp_spi_mode_get() - Get the SPI mode
 * @spi:     NFP SPI bus
 * @mode:    SPI CPHA/CPOL mode pointer
 *
 * Return: 0 or -ERRNO
 */
int nfp_spi_mode_get(struct nfp_spi *spi, int *mode)
{
	if (mode)
		*mode = spi->mode;

	return 0;
}
