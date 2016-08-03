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

/**
 * No module present
 */
#define NFP_PHYMOD_TYPE_NONE 0x00

/**
 * SFP  module
 */
#define NFP_PHYMOD_TYPE_SFP  1

/**
 * SFP+  module
 */
#define NFP_PHYMOD_TYPE_SFPP 10

/**
 * SFP28  module
 */
#define NFP_PHYMOD_TYPE_SFP28 25

/**
 * QSFP  module
 */
#define NFP_PHYMOD_TYPE_QSFP 40

/**
 * CXP  module
 */
#define NFP_PHYMOD_TYPE_CXP  100

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_LOS 0x00000001

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_FAULT 0x00000002

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_OPTPWR 0x00000004

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_OPTBIAS 0x00000008

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_HILOVOLT 0x00000010

/**
 * PHY module summary status
 */
#define NFP_PHYMOD_SUMSTAT_HILOTEMP 0x00000020

struct nfp_phymod;
struct nfp_phymod_eth;

struct nfp_phymod *nfp_phymod_next(struct nfp_device *nfp, void **ptr);

int nfp_phymod_get_index(struct nfp_phymod *phymod, int *index);
int nfp_phymod_get_label(struct nfp_phymod *phymod, const char **label);
int nfp_phymod_get_nbi(struct nfp_phymod *phymod, int *nbi);
int nfp_phymod_get_port(struct nfp_phymod *phymod, int *base, int *lanes);
int nfp_phymod_get_type(struct nfp_phymod *phymod, int *type);

int nfp_phymod_indicate_link(struct nfp_phymod *phymod, int is_on);
int nfp_phymod_indicate_activity(struct nfp_phymod *phymod, int is_on);

int nfp_phymod_read_status(struct nfp_phymod *phymod,
			   u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_status_los(struct nfp_phymod *phymod,
			       u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_status_fault(struct nfp_phymod *phymod,
				 u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_status_optpower(struct nfp_phymod *phymod,
				    u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_status_optbias(struct nfp_phymod *phymod,
				   u32 *rxtstaus, u32 *txstatus);
int nfp_phymod_read_status_voltage(struct nfp_phymod *phymod,
				   u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_status_temp(struct nfp_phymod *phymod,
				u32 *txstatus, u32 *rxstatus);
int nfp_phymod_read_lanedisable(struct nfp_phymod *phymod,
				u32 *txstatus, u32 *rxstatus);
int nfp_phymod_write_lanedisable(struct nfp_phymod *phymod,
				 u32 txstate, u32 rxstate);

int nfp_phymod_read8(struct nfp_phymod *phymod, u32 addr, u8 *data);
int nfp_phymod_write8(struct nfp_phymod *phymod, u32 addr, u8 data);

int nfp_phymod_verify_sff_checkcode(struct nfp_phymod *phymod, int *cc_status);
int nfp_phymod_read_vendor(struct nfp_phymod *phymod, char *name, u32 size);
int nfp_phymod_read_oui(struct nfp_phymod *phymod, u32 *oui);
int nfp_phymod_read_product(struct nfp_phymod *phymod, char *product, u32 size);
int nfp_phymod_read_serial(struct nfp_phymod *phymod, char *serial, u32 size);
int nfp_phymod_read_type(struct nfp_phymod *phymod, int *type);
int nfp_phymod_read_connector(struct nfp_phymod *phymod, int *connector);
int nfp_phymod_read_length(struct nfp_phymod *phymod, int *length);
int nfp_phymod_get_active_or_passive(struct nfp_phymod *phymod, int *anp);
int nfp_phymod_read_extended_compliance_code(struct nfp_phymod *phymod,
					     u32 *val);

struct nfp_phymod_eth *nfp_phymod_eth_next(struct nfp_device *dev,
					   struct nfp_phymod *phy, void **ptr);

int nfp_phymod_eth_get_index(struct nfp_phymod_eth *eth, int *index);
int nfp_phymod_eth_get_phymod(struct nfp_phymod_eth *eth,
			      struct nfp_phymod **phy, int *lane);
int nfp_phymod_eth_get_mac(struct nfp_phymod_eth *eth, const u8 **mac);
int nfp_phymod_eth_get_label(struct nfp_phymod_eth *eth, const char **label);
int nfp_phymod_eth_get_nbi(struct nfp_phymod_eth *eth, int *nbi);
int nfp_phymod_eth_get_port(struct nfp_phymod_eth *eth, int *base, int *lanes);
int nfp_phymod_eth_get_speed(struct nfp_phymod_eth *eth, int *speed);
int nfp_phymod_eth_get_fail_to_wire(struct nfp_phymod_eth *eth,
				    const char **eth_label, int *active);
int nfp_phymod_eth_set_fail_to_wire(struct nfp_phymod_eth *eth, int force);
int nfp_phymod_eth_read_disable(struct nfp_phymod_eth *eth,
				u32 *txstatus, u32 *rxstatus);
int nfp_phymod_eth_write_disable(struct nfp_phymod_eth *eth,
				 u32 txstate, u32 rxstate);

#endif
