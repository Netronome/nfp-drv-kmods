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
 * nfp3200.h
 */

#ifndef NFP3200_NFP3200_H
#define NFP3200_NFP3200_H

/* CPP Push-Pull IDs */
#define NFP_CPP_PPID_INVALID		0
#define NFP_CPP_PPID_MSF0		1		/* SPI4.2 */
#define NFP_CPP_PPID_ME(x)		((((x) >> 3) * 16 + 8) + ((x) & 7))
#define NFP_CPP_PPID_PCIE		17		/* PCIe */
#define NFP_CPP_PPID_MSF1		33		/* XAUI */
#define NFP_CPP_PPID_CRYPTO		49		/* Crypto */
#define NFP_CPP_PPID_ARM		65		/* ARM Core */

/* CPP Target IDs */
#define NFP_CPP_TARGET_INVALID		0
#define NFP_CPP_TARGET_MSF0		1		/* SPI4.2 */
#define NFP_CPP_TARGET_QDR		2
#define NFP_CPP_TARGET_MSF1		3
#define NFP_CPP_TARGET_HASH		4
#define NFP_CPP_TARGET_MU		7
#define NFP_CPP_TARGET_DDR		NFP_CPP_TARGET_MU
#define NFP_CPP_TARGET_GLOBAL_SCRATCH	8
#define NFP_CPP_TARGET_PCIE		9
#define NFP_CPP_TARGET_ARM		10
#define NFP_CPP_TARGET_ARM_SCRATCH	NFP_CPP_TARGET_ARM
#define NFP_CPP_TARGET_CRYPTO		12
#define NFP_CPP_TARGET_CRYPTO_SCRATCH	NFP_CPP_TARGET_CRYPTO
#define NFP_CPP_TARGET_CAP		13
#define NFP_CPP_TARGET_CLUSTER		14
#define NFP_CPP_TARGET_CT		NFP_CPP_TARGET_CLUSTER
#define NFP_CPP_TARGET_LOCAL_SCRATCH	15
#define NFP_CPP_TARGET_CLS	        NFP_CPP_TARGET_LOCAL_SCRATCH

/*
 * NOTE: Global Scratch, Local Scratch, DDR, etc. actions are
 * specified in the NFP3200 databook
 */

/* CPP Masters */

#define NFP_CPP_MASTER_ME(x)		(((((x) >> 3) * 16) + 8) | ((x) & 7))
#define NFP_CPP_MASTER_MSF0		1
#define NFP_CPP_MASTER_PCIE		17
#define NFP_CPP_MASTER_MSF1		33
#define NFP_CPP_MASTER_CRYPTO		49
#define NFP_CPP_MASTER_ARM		65

#endif /* NFP3200_NFP3200_H */
