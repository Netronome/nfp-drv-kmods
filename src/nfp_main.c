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
 * nfp_main.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Alejandro Lucero <alejandro.lucero@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>

#include "nfp_modinfo.h"

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"

#include "nfpcore/nfp6000_pcie.h"

#include "nfpcore/nfp_dev_cpp.h"
#include "nfpcore/nfp_net_vnic.h"

#include "nfp_main.h"

bool nfp_dev_cpp = true;
module_param(nfp_dev_cpp, bool, 0444);
MODULE_PARM_DESC(nfp_dev_cpp, "NFP CPP /dev interface (default = true)");

bool nfp_net_vnic;
module_param(nfp_net_vnic, bool, 0444);
MODULE_PARM_DESC(nfp_net_vnic, "vNIC net devices (default = false)");

static bool nfp_mon_event = true;
module_param(nfp_mon_event, bool, 0444);
MODULE_PARM_DESC(nfp_mon_event, "Event monitor support (default = true)");

static bool nfp_reset_on_exit;
module_param(nfp_reset_on_exit, bool, 0444);
MODULE_PARM_DESC(nfp_reset_on_exit,
		 "Soft reset the NFP on exit (default = false)");

static const char nfp_driver_name[] = "nfp";

static const struct pci_device_id nfp_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6010,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);

static void register_pf(struct nfp_pf *np)
{
	int pcie_unit;

	pcie_unit = NFP_CPP_INTERFACE_UNIT_of(nfp_cpp_interface(np->cpp));

	if (nfp_dev_cpp)
		np->nfp_dev_cpp = nfp_platform_device_register(np->cpp,
				NFP_DEV_CPP_TYPE);

	if (nfp_net_vnic)
		np->nfp_net_vnic = nfp_platform_device_register_unit(np->cpp,
							   NFP_NET_VNIC_TYPE,
							   pcie_unit,
							   NFP_NET_VNIC_UNITS);
}

static int nfp_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *pci_id)
{
	struct nfp_device *nfp_dev;
	struct nfp_pf *np;
	int err, irq;

	err = pci_enable_device(pdev);
	if (err < 0)
		return err;

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(40));
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to set PCI device mask.\n");
		goto err_dma_mask;
	}
	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(40));
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to set device consistent mask.\n");
		goto err_dma_mask;
	}

	err = pci_request_regions(pdev, nfp_driver_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to reserve pci resources.\n");
		goto err_request_regions;
	}

	np = kzalloc(sizeof(*np), GFP_KERNEL);
	if (!np) {
		err = -ENOMEM;
		goto err_kzalloc;
	}
	INIT_LIST_HEAD(&np->ports);

	if (nfp_mon_event) {
		/* Completely optional: we will be fine with Legacy IRQs */
		err = pci_enable_msix(pdev, &np->msix, 1);
		if (pdev->msix_enabled)
			irq = np->msix.vector;
		else
			irq = pdev->irq;
	} else {
		irq = -1;
	}

	np->cpp = nfp_cpp_from_nfp6000_pcie(pdev, irq);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	if (!IS_ERR_OR_NULL(np->cpp)) {
		err = nfp_sriov_attr_add(&pdev->dev);
		if (err < 0)
			goto err_nfp_cpp;
	}
#endif
	if (IS_ERR_OR_NULL(np->cpp)) {
		err = PTR_ERR(np->cpp);
		if (err >= 0)
			err = -ENOMEM;
		goto err_nfp_cpp;
	}

	nfp_dev = nfp_device_from_cpp(np->cpp);
	if (!nfp_dev) {
		err = -ENODEV;
		goto err_nfp_dev;
	}

	err = nfp_fw_load(pdev, nfp_dev, false);
	nfp_device_close(nfp_dev);
	if (err < 0)
		goto err_fw_load;

	np->fw_loaded = !!err;

	register_pf(np);

	pci_set_drvdata(pdev, np);

	return 0;

err_fw_load:
err_nfp_dev:
	nfp_cpp_free(np->cpp);

err_nfp_cpp:
	pci_disable_msix(pdev);

	kfree(np);
err_kzalloc:
	pci_release_regions(pdev);
err_request_regions:
err_dma_mask:
	pci_disable_device(pdev);
	return err;
}

static void nfp_pci_remove(struct pci_dev *pdev)
{
	struct nfp_pf *np = pci_get_drvdata(pdev);

	nfp_platform_device_unregister(np->nfp_net_vnic);

	nfp_pcie_sriov_disable(pdev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
#endif

	if (nfp_reset_on_exit)
		nfp_fw_unload(np);

	nfp_platform_device_unregister(np->nfp_dev_cpp);

	pci_set_drvdata(pdev, NULL);
	nfp_cpp_free(np->cpp);

	pci_disable_msix(pdev);

	kfree(np);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nfp_pcie_driver = {
	.name        = (char *)nfp_driver_name,
	.id_table    = nfp_pci_device_ids,
	.probe       = nfp_pci_probe,
	.remove      = nfp_pci_remove,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.sriov_configure = nfp_pcie_sriov_configure,
#endif
};

static int __init nfp_main_init(void)
{
	int err;

	mutex_lock(&module_mutex);
	if (find_module("nfp_net")) {
		pr_err("%s: Cannot be loaded while nfp_net is loaded\n",
		       nfp_driver_name);
		mutex_unlock(&module_mutex);
		return -EBUSY;
	}
	mutex_unlock(&module_mutex);

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2015 Netronome Systems\n",
		nfp_driver_name);
	pr_info(NFP_BUILD_DESCRIPTION(nfp));

	err = nfp_cppcore_init();
	if (err < 0)
		goto fail_cppcore_init;

	err = nfp_dev_cpp_init();
	if (err < 0)
		goto fail_dev_cpp_init;

	err = nfp_net_vnic_init();
	if (err < 0)
		goto fail_net_vnic_init;

	err = pci_register_driver(&nfp_pcie_driver);
	if (err < 0)
		goto fail_pci_init;

	return err;

fail_pci_init:
	nfp_net_vnic_exit();
fail_net_vnic_init:
	nfp_dev_cpp_exit();
fail_dev_cpp_init:
	nfp_cppcore_exit();
fail_cppcore_init:
	return err;
}

static void __exit nfp_main_exit(void)
{
	pci_unregister_driver(&nfp_pcie_driver);
	nfp_net_vnic_exit();
	nfp_dev_cpp_exit();
	nfp_cppcore_exit();
}

module_init(nfp_main_init);
module_exit(nfp_main_exit);

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Netronome Flow Processor (NFP) driver.");
MODULE_INFO_NFP();
