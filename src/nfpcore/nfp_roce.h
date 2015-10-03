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
 * nfp_roce.h - Describes NFP NIC/core interface exported to netro
 * RoCEv2 HCA driver.
 *
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 *
 * This file should ultimately be located with the NFP Ethernet/BSP driver.
 *
 * The netro RoCE HCA kernel driver is dependent on the NFP NIC/core
 * drivers. When the netro driver is loaded it registers a RoCE HCA with
 * the InfiniBand stack for each NFP NIC instance for which RoCE is enabled.
 *
 * The NFP NIC/core must be configured to enable RoCE support to use
 * the RoCE HCA capability. The RoCE HCA driver utilizes an NFP register
 * driver interface to register with the NFP NIC/core and to receive
 * callbacks to add the RoCE device for each enabled NIC instance.
 */

#ifndef NFPCORE_NFP_ROCE_H
#define NFPCORE_NFP_ROCE_H

#include <linux/pci.h>
#include <linux/if_ether.h>

#include "nfp.h"
/*
 * Information passed back to RoCE HCA driver by the NFP/core driver for each
 * NIC instance in the add_device() callback.
 */
#define NETRO_ROCE_ABI_VERSION		0x0100  /* Major v1, Minor v0 */
#define NETRO_MAX_NAME_LEN		32
#define NETRO_MAX_MSIX_VECTORS		32
#define NETRO_MAX_ROCE_PORTS		64

enum nfp_roce_devstate_e {
	NFP_DEV_UP		= 0,
	NFP_DEV_DOWN		= 1,
	NFP_DEV_SHUTDOWN	= 2
};

/**
 * struct nfp_roce_info - NFP RoCE subdriver interface
 * @model:		Model number from nfp_cpp_model()
 * @pdev:		PCI Device parent of CPP interface
 * @netdev:		Network devices to attach RoCE ports to
 * @cmdif:		Command interface iomem
 * @db_base:		DMAable page area
 * @db_length:		Size of DMAable page area
 * @def_mac:		MAC for the RoCE interface
 * @num_vectors:	Number of MSI-X vectors for RoCE's use
 * @msix:		MSI-X vectors (resized to num_vectors)
 */
struct nfp_roce_info {
	u32 model;

	/* We need the following, don't see a way to get through NFP open */
	struct pci_dev	*pdev;
	struct net_device *netdev[NETRO_MAX_ROCE_PORTS];

	/*
	 * PCI Resources allocated by the NFP core and
	 * acquired/released by the RoCE driver:
	 * 1) Driver/ME command interface
	 * 2) DB area (first page is for EQs, the remainder for SQ/CQ)
	 */
	void __iomem *cmdif;
	phys_addr_t  db_base;
	u32 db_length;		/* The length of the physical doorbell area */

	/*
	 * We use the default MAC specified to create the HCA GUID that will
	 * be used to identify the HCA. The default MAC should be unique
	 * to the NFP NIC device.
	 */
	u8 def_mac[ETH_ALEN];

	/*
	 * Pool of interrupt vectors that RoCE driver can use for
	 * setting up EQ interrupts.
	 */
	u32	num_vectors;
	struct msix_entry	msix[0];
};

struct netro_ibdev;

/**
 * struct nfp_roce_drv - NFP RoCE driver interface
 * @abi_version:	Must be NETRO_ROCE_ABI_VERSION
 * @add_device:		Callback to create a new RoCE device
 * @remove_device:	Callback to remove an existing RoCE device
 * @event_notifier:	Callback to update an existing RoCE device's state
 *
 * NFP RoCE register driver input parameters. Passed to the NFP core
 * in the nfp_register_roce_driver() and nfp_unregister_roce_driver()
 * functions.
 *
 * The netro add_device() call back will return the netro RoCE device.
 * This is opaque to the NIC, but should be passed in remove_device()
 * or state_change() callbacks.
 *
 * The netro event_notifier() call back is a state change handler
 * used to pass NFP device state changes from NFP driver to RoCE driver.
 */
struct nfp_roce_drv {
	u32	abi_version;
	struct netro_ibdev *(*add_device)(struct nfp_roce_info *roce_info);
	void	(*remove_device)(struct netro_ibdev *);
	void	(*event_notifier)(struct netro_ibdev *, int port, u32 state);
};

int nfp_register_roce_driver(struct nfp_roce_drv *drv);
void nfp_unregister_roce_driver(struct nfp_roce_drv *drv);

struct nfp_roce;

struct nfp_roce *nfp_roce_add(struct nfp_device *nfp,
			      struct net_device **netdev, int netdevs,
			      struct msix_entry *entry, int entries);
void nfp_roce_remove(struct nfp_roce *roce);
void nfp_roce_port_set_state(struct nfp_roce *roce, int port,
			     enum nfp_roce_devstate_e state);

#endif /* NFPCORE_NFP_ROCE_H */
