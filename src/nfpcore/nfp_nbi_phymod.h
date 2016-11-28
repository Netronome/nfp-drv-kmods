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
 * nfp_nbi_phymod.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef __NFP_PHYMOD_H__
#define __NFP_PHYMOD_H__

#include <linux/kernel.h>

struct nfp_phymod;
struct nfp_phymod_eth;

struct nfp_phymod_eth *nfp_phymod_eth_next(struct nfp_cpp *dev,
					   struct nfp_phymod *phy, void **ptr);

int nfp_phymod_eth_get_index(struct nfp_phymod_eth *eth, int *index);
int nfp_phymod_eth_get_mac(struct nfp_phymod_eth *eth, const u8 **mac);
int nfp_phymod_eth_get_label(struct nfp_phymod_eth *eth, const char **label);
int nfp_phymod_eth_get_nbi(struct nfp_phymod_eth *eth, int *nbi);
int nfp_phymod_eth_get_port(struct nfp_phymod_eth *eth, int *base, int *lanes);
int nfp_phymod_eth_get_speed(struct nfp_phymod_eth *eth, int *speed);
int nfp_phymod_eth_read_disable(struct nfp_phymod_eth *eth,
				u32 *txstatus, u32 *rxstatus);
int nfp_phymod_eth_write_disable(struct nfp_phymod_eth *eth,
				 u32 txstate, u32 rxstate);

#endif
