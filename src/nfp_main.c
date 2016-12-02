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
#include "nfp_net.h"

#ifdef CONFIG_NFP_NET_PF
static bool nfp_pf_netdev = true;
module_param(nfp_pf_netdev, bool, 0444);
MODULE_PARM_DESC(nfp_pf_netdev, "Create netdevs on PF (requires appropriate firmware) (default = true)");
#else
static const bool nfp_pf_netdev;
#endif

static int nfp_fallback = -1;
#ifdef CONFIG_NFP_NET_PF
module_param(nfp_fallback, bint, 0444);
MODULE_PARM_DESC(nfp_fallback, "(netdev mode) Stay bound to device with user space access only (no netdevs) if no suitable FW is present (default = nfp_dev_cpp)");
#endif

int nfp_dev_cpp = -1;
module_param(nfp_dev_cpp, bint, 0444);
MODULE_PARM_DESC(nfp_dev_cpp,
		 "NFP CPP /dev interface (default = !nfp_pf_netdev)");

bool nfp_net_vnic;
module_param(nfp_net_vnic, bool, 0444);
MODULE_PARM_DESC(nfp_net_vnic, "vNIC net devices (default = false)");

static int nfp_mon_event = -1;
module_param(nfp_mon_event, bint, 0444);
MODULE_PARM_DESC(nfp_mon_event, "(non-netdev mode) Event monitor support (default = !nfp_pf_netdev)");

static bool nfp_reset_on_exit;
module_param(nfp_reset_on_exit, bool, 0444);
MODULE_PARM_DESC(nfp_reset_on_exit,
		 "Soft reset the NFP on exit (default = false)");

static const char nfp_driver_name[] = "nfp";
const char nfp_driver_version[] = "0.1";

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
#if COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);
#endif

static void nfp_register_vnic(struct nfp_pf *pf)
{
	int pcie_unit;

	pcie_unit = NFP_CPP_INTERFACE_UNIT_of(nfp_cpp_interface(pf->cpp));

	if (!nfp_net_vnic)
		return;

	pf->nfp_net_vnic =
		nfp_platform_device_register_unit(pf->cpp, NFP_NET_VNIC_TYPE,
						  pcie_unit,
						  NFP_NET_VNIC_UNITS);
}

static int nfp_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *pci_id)
{
	struct nfp_device *nfp_dev;
	struct nfp_pf *pf;
	int err, irq;

	err = pci_enable_device(pdev);
	if (err < 0)
		return err;

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(NFP_NET_MAX_DMA_BITS));
	if (err)
		goto err_pci_disable;

	err = pci_request_regions(pdev, nfp_driver_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to reserve pci resources.\n");
		goto err_pci_disable;
	}

	pf = kzalloc(sizeof(*pf), GFP_KERNEL);
	if (!pf) {
		err = -ENOMEM;
		goto err_rel_regions;
	}
	INIT_LIST_HEAD(&pf->ports);
	pci_set_drvdata(pdev, pf);
	pf->pdev = pdev;

	if (nfp_mon_event) {
		/* Completely optional: we will be fine with Legacy IRQs */
		err = pci_enable_msix(pdev, &pf->msix, 1);
		if (!err)
			irq = pf->msix.vector;
		else
			irq = pdev->irq;
	} else {
		irq = -1;
	}

	pf->cpp = nfp_cpp_from_nfp6000_pcie(pdev, irq);
	if (IS_ERR_OR_NULL(pf->cpp)) {
		err = PTR_ERR(pf->cpp);
		if (err >= 0)
			err = -ENOMEM;
		goto err_disable_msix;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	err = nfp_sriov_attr_add(&pdev->dev);
	if (err < 0)
		goto err_cpp_free;
#endif

	nfp_dev = nfp_device_from_cpp(pf->cpp);
	if (!nfp_dev) {
		err = -ENODEV;
		goto err_sriov_remove;
	}

	err = nfp_fw_load(pdev, nfp_dev, nfp_pf_netdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to load FW\n");
		goto err_dev_close;
	}

	pf->fw_loaded = !!err;

	if (nfp_dev_cpp) {
		pf->nfp_dev_cpp =
			nfp_platform_device_register(pf->cpp, NFP_DEV_CPP_TYPE);
		if (!pf->nfp_dev_cpp)
			dev_err(&pdev->dev, "Failed to enable user space access. Ignoring.\n");
	}

	if (nfp_pf_netdev) {
		err = nfp_net_pci_probe(pf, nfp_dev, nfp_reset);
		if (nfp_fallback && err == 1) {
			dev_info(&pdev->dev, "Netronome NFP Fallback driver\n");
		} else if (err) {
			err = err < 0 ? err : -EINVAL;
			goto err_dev_cpp_unreg;
		}
	} else {
		nfp_register_vnic(pf);
	}

	nfp_device_close(nfp_dev);

	return 0;

err_dev_cpp_unreg:
	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);
err_dev_close:
	nfp_device_close(nfp_dev);
err_sriov_remove:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
err_cpp_free:
#endif
	nfp_cpp_free(pf->cpp);
err_disable_msix:
	if (pdev->msix_enabled)
		pci_disable_msix(pdev);
	pci_set_drvdata(pdev, NULL);
	kfree(pf);
err_rel_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);

	return err;
}

static void nfp_pci_remove(struct pci_dev *pdev)
{
	struct nfp_pf *pf = pci_get_drvdata(pdev);

	if (!list_empty(&pf->ports))
		nfp_net_pci_remove(pf);

	if (pf->nfp_net_vnic)
		nfp_platform_device_unregister(pf->nfp_net_vnic);

	nfp_pcie_sriov_disable(pdev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
#endif

	if (nfp_reset_on_exit || (nfp_pf_netdev && pf->fw_loaded))
		nfp_fw_unload(pf);

	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);

	pci_set_drvdata(pdev, NULL);
	nfp_cpp_free(pf->cpp);

	if (nfp_mon_event)
		pci_disable_msix(pdev);

	kfree(pf);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nfp_pci_driver = {
	.name        = nfp_driver_name,
	.id_table    = nfp_pci_device_ids,
	.probe       = nfp_pci_probe,
	.remove      = nfp_pci_remove,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.sriov_configure = nfp_pcie_sriov_configure,
#endif
};

#if !COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
static const struct pci_device_id compat_nfp_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6010,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
#ifdef CONFIG_NFP_NET_VF
	{ PCI_VENDOR_ID_NETRONOME, 0x6003,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
#endif
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, compat_nfp_device_ids);

static int compat_nfp_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_id)
{
#ifdef CONFIG_NFP_NET_VF
	if (pdev->device == 0x6003)
		return nfp_netvf_pci_driver.probe(pdev, pci_id);
#endif
	return nfp_pci_driver.probe(pdev, pci_id);
}

static void compat_nfp_remove(struct pci_dev *pdev)
{
#ifdef CONFIG_NFP_NET_VF
	if (pdev->device == 0x6003) {
		nfp_netvf_pci_driver.remove(pdev);
		return;
	}
#endif
	nfp_pci_driver.remove(pdev);
}

static struct pci_driver compat_nfp_driver = {
	.name        = nfp_driver_name,
	.id_table    = compat_nfp_device_ids,
	.probe       = compat_nfp_probe,
	.remove      = compat_nfp_remove,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.sriov_configure = nfp_pcie_sriov_configure,
#endif
};
#endif /* !COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES */

static int nfp_net_vf_register(void)
{
#if defined(CONFIG_NFP_NET_VF) && COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
	return pci_register_driver(&nfp_netvf_pci_driver);
#endif
	return 0;
}

static void nfp_net_vf_unregister(void)
{
#if defined(CONFIG_NFP_NET_VF) && COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
	pci_unregister_driver(&nfp_netvf_pci_driver);
#endif
}

static bool __init nfp_resolve_params(void)
{
	if (nfp_pf_netdev && nfp_mon_event == 1) {
		pr_err("nfp_mon_event cannot be used in netdev mode\n");
		return false;
	}
	if (!nfp_pf_netdev && nfp_fallback == 1)
		pr_warn("nfp_fallback is ignored in netdev mode\n");
	if (nfp_fallback == 1 && nfp_dev_cpp == 0) {
		pr_err("nfp_fallback cannot be used without nfp_dev_cpp\n");
		return false;
	}

	if (nfp_dev_cpp == -1)
		nfp_dev_cpp = !nfp_pf_netdev;
	if (nfp_mon_event == -1)
		nfp_mon_event = !nfp_pf_netdev;
	if (nfp_fallback == -1)
		nfp_fallback = nfp_pf_netdev && nfp_dev_cpp;

	return true;
}

static int __init nfp_main_init(void)
{
	int err;

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2015 Netronome Systems\n",
		nfp_driver_name);
	pr_info(NFP_BUILD_DESCRIPTION(nfp));

	if (!nfp_resolve_params())
		return -EINVAL;

	err = nfp_cppcore_init();
	if (err < 0)
		goto fail_cppcore_init;

	err = nfp_dev_cpp_init();
	if (err < 0)
		goto fail_dev_cpp_init;

	err = nfp_net_vnic_init();
	if (err < 0)
		goto fail_net_vnic_init;

	nfp_net_debugfs_create();

#if COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
	err = pci_register_driver(&nfp_pci_driver);
#else
	err = pci_register_driver(&compat_nfp_driver);
#endif
	if (err < 0)
		goto err_destroy_debugfs;

	err = nfp_net_vf_register();
	if (err)
		goto err_unreg_pf;

	return err;

err_unreg_pf:
#if COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
	pci_unregister_driver(&nfp_pci_driver);
#else
	pci_unregister_driver(&compat_nfp_driver);
#endif
err_destroy_debugfs:
	nfp_net_debugfs_destroy();
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
	nfp_net_vf_unregister();
#if COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
	pci_unregister_driver(&nfp_pci_driver);
#else
	pci_unregister_driver(&compat_nfp_driver);
#endif
	nfp_net_debugfs_destroy();
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
