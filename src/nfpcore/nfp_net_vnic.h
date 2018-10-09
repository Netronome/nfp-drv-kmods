/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/*
 * nfp_net_vnic.h
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef NFP_NET_VNIC_H
#define NFP_NET_VNIC_H

#define NFP_NET_VNIC_TYPE	"nfp-net-vnic"
#define NFP_NET_VNIC_UNITS	4

int nfp_net_vnic_init(void);
void nfp_net_vnic_exit(void);

#endif /* NFP_NET_VNIC_H */
