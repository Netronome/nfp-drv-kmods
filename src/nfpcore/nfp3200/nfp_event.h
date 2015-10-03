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
 * nfp_event.h
 * Event source and type definitions
 */

#ifndef NFP3200_NFP_EVENT_H
#define NFP3200_NFP_EVENT_H

/* Event bus providers */
#define NFP_EVENT_SOURCE_LOCAL_SCRATCH(cluster)	(cluster)
#define NFP_EVENT_SOURCE_MSF0			8
#define NFP_EVENT_SOURCE_PCIE			9
#define NFP_EVENT_SOURCE_MSF1			10
#define NFP_EVENT_SOURCE_CRYPTO			11
#define NFP_EVENT_SOURCE_ARM			12
#define NFP_EVENT_SOURCE_DDR			14
#define NFP_EVENT_SOURCE_SHAC			15

/* Event bus types */
#define NFP_EVENT_TYPE_FIFO_NOT_EMPTY		0
#define NFP_EVENT_TYPE_FIFO_NOT_FULL		1
#define NFP_EVENT_TYPE_DMA			2
#define NFP_EVENT_TYPE_PROCESS			3
#define NFP_EVENT_TYPE_STATUS			4
#define NFP_EVENT_TYPE_FIFO_UNDERFLOW		8
#define NFP_EVENT_TYPE_FIFO_OVERFLOW		9
#define NFP_EVENT_TYPE_ECC_SINGLE_CORRECTION	10
#define NFP_EVENT_TYPE_ECC_MULTI_ERROR		11
#define NFP_EVENT_TYPE_ECC_SINGLE_ERROR		12

#endif /* NFP3200_NFP_EVENT_H */
