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
 * nfp_roce.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/list.h>

#include "nfp.h"
#include "nfp_roce.h"
#include "nfp_nffw.h"

#include "nfp6000/nfp6000.h"

struct nfp_roce {
	struct list_head list;
	struct nfp_roce_info info;
	struct nfp_cpp_area *command;
	struct nfp_cpp_area *doorbell;
	struct netro_ibdev *ibdev;
};

static LIST_HEAD(nfp_roce_list);

static DEFINE_MUTEX(roce_driver_mutex);
static struct nfp_roce_drv *roce_driver;

/**
 * nfp_register_roce_driver() - Register the RoCE driver with NFP core.
 * @drv:		RoCE driver callback function table.
 *
 * This routine is called by the netro RoCEv2 kernel driver to
 * notify the NFP NIC/core driver that the RoCE driver has been loaded. If
 * RoCE is not enabled or the ABI version is not supported, the NFP NIC/core
 * should return an error. Otherwise, the NFP NIC/core should invoke the
 * add_device() callback for each NIC instance.
 *
 * Return: 0, or -ERRNO
 */
int nfp_register_roce_driver(struct nfp_roce_drv *drv)
{
	struct nfp_roce *roce;

	if (!drv || drv->abi_version != NETRO_ROCE_ABI_VERSION)
		return -EINVAL;

	mutex_lock(&roce_driver_mutex);
	if (roce_driver) {
		mutex_unlock(&roce_driver_mutex);
		return -EBUSY;
	}
	roce_driver = drv;

	list_for_each_entry(roce, &nfp_roce_list, list) {
		BUG_ON(roce->ibdev);
		roce->ibdev = roce_driver->add_device(&roce->info);
		if (IS_ERR_OR_NULL(roce->ibdev)) {
			int err = roce->ibdev ? PTR_ERR(roce->ibdev) : -ENODEV;

			dev_warn(&roce->info.pdev->dev,
				 "RoCE: Can't register device: %d\n", err);
			roce->ibdev = NULL;
		}
	}

	mutex_unlock(&roce_driver_mutex);

	return 0;
}

/**
 * nfp_unregister_roce_driver() - Unregister the RoCE driver with NFP core.
 * @drv:	The callback function table passed in the associated
 *		nfp_register_roce_driver() call.
 *
 * This routine is called by the netro RoCEv2 driver to notify the NFP
 * NIC/core driver that the RoCE driver is unloading. The NFP NIC
 * driver invokes the remove_device routine for each netro RoCE device
 * that has been added.
 */
void nfp_unregister_roce_driver(struct nfp_roce_drv *drv)
{
	mutex_lock(&roce_driver_mutex);
	if (drv == roce_driver) {
		struct nfp_roce *roce;

		list_for_each_entry(roce, &nfp_roce_list, list) {
			if (!roce->ibdev)
				continue;
			roce_driver->remove_device(roce->ibdev);
			roce->ibdev = NULL;
		}
		roce_driver = NULL;
	}
	mutex_unlock(&roce_driver_mutex);
}

/**
 * nfp_roce_attach() - attach a network device set to the RoCE subsystem
 * @nfp:		NFP Device handle
 * @netdev:		Array of net_devices to attach to
 * @netdevs:		Number of entries in the netdev array
 * @msix_entry:		MSI-X vectors to use
 * @msix_entries:	Number of entries in the msix_entry array
 *
 * This routine attachs a RoCE interface to a pci/cpp/netdev triple,
 * and returns a RoCE handle.
 *
 * Return: IS_ERR() checkable pointer to a struct nfp_roce
 */
struct nfp_roce *nfp_roce_add(struct nfp_device *nfp,
			      struct net_device **netdev, int netdevs,
			      struct msix_entry *msix_entry, int msix_entries)
{
	const char *cmd_symbol = "_cmd_iface_reg";
	int err, i;
	struct nfp_cpp *cpp;
	struct device *dev;
	const struct nfp_rtsym *cmd;
	struct nfp_roce *roce;
	struct nfp_roce_info *info;

	/* First, let's validate that the NFP device is
	 * a PCI interface.
	 */
	if (!nfp) {
		err = -EINVAL;
		goto error_check;
	}

	cpp = nfp_device_cpp(nfp);
	dev = nfp_cpp_device(cpp);

	if (!dev || !dev->parent) {
		err = -ENODEV;
		goto error_check;
	}

	dev = dev->parent;

	if (!dev_is_pci(dev)) {
		err = -EINVAL;
		goto error_check;
	}

	if (!netdev || netdevs < 1) {
		nfp_info(nfp, "RoCE: No net devices found\n");
		err = -EINVAL;
		goto error_check;
	}

	if (netdevs > ARRAY_SIZE(info->netdev)) {
		nfp_info(nfp, "RoCE: Only %d net devices supported\n",
			 (int)ARRAY_SIZE(info->netdev));
		netdevs = ARRAY_SIZE(info->netdev);
	}

	cmd = nfp_rtsym_lookup(nfp, cmd_symbol);
	if (!cmd) {
		nfp_err(nfp, "RoCE: rtsym '%s' does not exist\n",
			cmd_symbol);
		err = -ENOENT;
		goto error_check;
	}

	roce = kzalloc(sizeof(*roce) +
		       sizeof(struct msix_entry) * msix_entries,
		       GFP_KERNEL);
	if (!roce) {
		err = -ENOMEM;
		goto error_check;
	}

	info = &roce->info;
	info->model = nfp_cpp_model(cpp);
	info->pdev = to_pci_dev(dev);

	for (i = 0; i < netdevs; i++)
		info->netdev[i] = netdev[i];

	roce->command = nfp_cpp_area_alloc_acquire(cpp,
				NFP_CPP_ISLAND_ID(cmd->target,
						  NFP_CPP_ACTION_RW, 0,
						  cmd->domain),
				cmd->addr, cmd->size);
	if (IS_ERR_OR_NULL(roce->command)) {
		err = roce->command ? PTR_ERR(roce->command) : -ENOMEM;
		goto error_command;
	}

	info->db_length = 0x1000 * 64;
	roce->doorbell = nfp_cpp_area_alloc_acquire(cpp,
				NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_CLS,
						  NFP_CPP_ACTION_RW, 0,
						  4 /* PCI0 Island */),
				0x80000000,
				info->db_length);
	if (IS_ERR_OR_NULL(roce->doorbell)) {
		err = roce->doorbell ? PTR_ERR(roce->doorbell) : -ENOMEM;
		goto error_doorbell;
	}

	info->cmdif = nfp_cpp_area_iomem(roce->command);

	info->db_base = nfp_cpp_area_phys(roce->doorbell);

	ether_addr_copy(info->def_mac, info->netdev[0]->dev_addr);

	info->num_vectors = msix_entries;
	for (i = 0; i < msix_entries; i++)
		info->msix[i] = msix_entry[i];

	mutex_lock(&roce_driver_mutex);
	list_add(&roce->list, &nfp_roce_list);

	/* If we already have a RoCE driver registered, try to
	 * use it. If it fails, that's fine, we can try another
	 * one at the next reload.
	 */
	if (roce_driver) {
		roce->ibdev = roce_driver->add_device(info);
		if (IS_ERR_OR_NULL(roce->ibdev)) {
			err = roce->ibdev ? PTR_ERR(roce->ibdev) : -ENODEV;
			nfp_warn(nfp, "RoCE: Can't create interface: %d\n",
				 err);
			roce->ibdev = NULL;
		}
	}

	mutex_unlock(&roce_driver_mutex);
	return roce;

error_doorbell:
	nfp_cpp_area_release_free(roce->command);
error_command:
	kfree(roce);
error_check:

	return ERR_PTR(err);
}

/**
 * nfp_roce_remove() - remove a RoCE device
 * @roce:		RoCE device handle
 *
 * This routine attachs a RoCE interface to a pci/cpp/netdev triple,
 * and returns a RoCE handle.
 */
void nfp_roce_remove(struct nfp_roce *roce)
{
	if (IS_ERR_OR_NULL(roce))
		return;

	mutex_lock(&roce_driver_mutex);
	list_del(&roce->list);

	if (roce->ibdev) {
		roce_driver->remove_device(roce->ibdev);
		roce->ibdev = NULL;
	}

	nfp_cpp_area_release_free(roce->doorbell);
	nfp_cpp_area_release_free(roce->command);
	kfree(roce);
	mutex_unlock(&roce_driver_mutex);
}

/**
 * nfp_roce_state() - Alter the state of the RoCE device
 * @roce:		RoCE device handle
 * @port:		Netdev port to update
 * @state:		State update for the port
 *
 * This should be coordinated with the netdev state
 */
void nfp_roce_port_set_state(struct nfp_roce *roce, int port,
			     enum nfp_roce_devstate_e state)
{
	mutex_lock(&roce_driver_mutex);
	if (roce_driver && roce->ibdev)
		roce_driver->event_notifier(roce->ibdev, port, state);
	mutex_unlock(&roce_driver_mutex);
}
