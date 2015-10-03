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
 * nfp_xpb.h
 */

#ifndef NFP3200_NFP_XPB_H
#define NFP3200_NFP_XPB_H

#define NFP_XPB_SIZE		0x02000000

#define NFP_XPB_DEST(cluster, device)	\
	((((cluster) & 0x1f) << 20) | (((device) & 0x3f) << 14))
#define NFP_XPB_DEST_SIZE	(1 << 14)

#define NFP_XPB_DEST_CLUSTER_of(xpb_dest)	(((xpb_dest) >> 20) & 0x1f)
#define NFP_XPB_DEST_DEVICE_of(xpb_dest)	(((xpb_dest) >> 14) & 0x3f)
#define NFP_XPB_DEST_ADDR_of(xpb_addr)		((xpb_addr) & 0x3fff)

#define NFP_ME_CLUSTER_START(me) \
	NFP_XPB_DEST(me, 1)	/* Cluster Config */
#define NFP_ME_LSCRATCH_CSR_START(me) \
	NFP_XPB_DEST(me, 2)	/* Local scratch Config */
#define NFP_ME_LSCRATCH_ECC_START(me) \
	NFP_XPB_DEST(me, 3)	/* Local scratch ECC Monitor */

/* Crypto CSRs */
#define NFP_CRYPTO_CIF_START \
	NFP_XPB_DEST(19, 1)	/* CIF CSRs */
/* ... */

/* ARM CSRs */
/* ... */
#define NFP_ARM_INTR_START \
	NFP_XPB_DEST(20, 5)	/* Interrupt manager */
#define NFP_ARM_CFG_START \
	NFP_XPB_DEST(20, 6)	/* Local CSRs */

#endif /* NFP3200_NFP_XPB_H */
