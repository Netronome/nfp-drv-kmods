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
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>

#include "nfp_modinfo.h"

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"

#include "nfpcore/nfp3200_plat.h"
#include "nfpcore/nfp3200_pcie.h"
#include "nfpcore/nfp6000_pcie.h"

#include "nfpcore/nfp_mon_err.h"
#include "nfpcore/nfp_dev_cpp.h"
#include "nfpcore/nfp_net_null.h"
#include "nfpcore/nfp_net_vnic.h"

#include "nfp_main.h"

bool nfp_mon_err;
module_param(nfp_mon_err, bool, 0444);
MODULE_PARM_DESC(nfp_mon_err, "ECC Monitor (default = disbled)");
bool nfp_dev_cpp = 1;
module_param(nfp_dev_cpp, bool, 0444);
MODULE_PARM_DESC(nfp_dev_cpp, "NFP CPP /dev interface (default = enabled)");
bool nfp_net_null;
module_param(nfp_net_null, bool, 0444);
MODULE_PARM_DESC(nfp_net_null, "Null net devices (default = disabled)");
bool nfp_net_vnic;
module_param(nfp_net_vnic, bool, 0444);
MODULE_PARM_DESC(nfp_net_vnic, "vNIC net devices (default = disabled)");
static bool nfp_mon_event = 1;
module_param(nfp_mon_event, bool, 0444);
MODULE_PARM_DESC(nfp_mon_event, "Event monitor support (default = enabled)");
static bool nfp_reset;
module_param(nfp_reset, bool, 0444);
MODULE_PARM_DESC(nfp_reset,
		 "Soft reset the NFP on init (default = disable)");
static char *nfp3200_firmware;
module_param(nfp3200_firmware, charp, 0444);
MODULE_PARM_DESC(nfp3200_firmware,
		 "NFP3200 firmware to load from /lib/firmware/");
static char *nfp6000_firmware;
module_param(nfp6000_firmware, charp, 0444);
MODULE_PARM_DESC(nfp6000_firmware,
		 "NFP6000 firmware to load from /lib/firmware/");

struct nfp_pci {
	struct nfp_cpp *cpp;
	struct msix_entry msix;
	int fw_loaded;

	struct platform_device *nfp_mon_err;
	struct platform_device *nfp_dev_cpp;
	struct platform_device *nfp_net_null;
	struct platform_device *nfp_net_vnic;

#ifdef CONFIG_PCI_IOV
	/* SR-IOV handling */
	unsigned int num_vfs;
#endif
};

static const char nfp_driver_name[] = "nfp";

static const struct pci_device_id nfp_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP3200,
	  PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP3200,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP3200,
	  PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP3240,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);

/*
 * SR-IOV support
 */

static int nfp6000_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pci *nfp = pci_get_drvdata(pdev);
	int err = 0;

	if (num_vfs > 64) {
		err = -EPERM;
		goto err_out;
	}

	nfp->num_vfs = num_vfs;

	/* Device specific VF config goes here */

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable PCI sriov: %d\n", err);
		goto err_out;
	}

	dev_dbg(&pdev->dev, "Created %d VFs.\n", nfp->num_vfs);

	return num_vfs;

err_out:
	return err;
#endif
	return 0;
}

static int nfp6000_pcie_sriov_disable(struct pci_dev *pdev)
{
	struct nfp_pci *nfp = pci_get_drvdata(pdev);

#ifdef CONFIG_PCI_IOV
	/* Device specific VF config goes here */
	nfp->num_vfs = 0;

	/*
	 * If our VFs are assigned we cannot shut down SR-IOV without
	 * causing issues, so just leave the hardware available but
	 * disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Disabling while VFs assigned - VFs will not be deallocated\n");
		return -EPERM;
	}
	pci_disable_sriov(pdev);
#endif

	dev_dbg(&pdev->dev, "Removed VFs.\n");

	return 0;
}

static int nfp6000_pcie_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs == 0)
		return nfp6000_pcie_sriov_disable(pdev);
	else
		return nfp6000_pcie_sriov_enable(pdev, num_vfs);
}

/**
 * nfp_pcie_fw_load - Load the firmware image
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
static int nfp_pcie_fw_load(struct pci_dev *pdev, struct nfp_cpp *cpp)
{
	const struct firmware *fw = NULL;
	const char *fw_name;
	u32 model = nfp_cpp_model(cpp);
	int err, timeout = 30; /* Seconds */
	struct nfp_device *nfp;
	int need_armsp;

	if (NFP_CPP_MODEL_IS_3200(model)) {
		fw_name = nfp3200_firmware;
		need_armsp = 0;
	} else if (NFP_CPP_MODEL_IS_6000(model)) {
		fw_name = nfp6000_firmware;
		need_armsp = 1;
	} else {
		return 0;
	}

	nfp = nfp_device_from_cpp(cpp);
	if (!nfp)
		return 0;

	if (nfp_reset) {
		dev_info(&pdev->dev, "NFP soft-reset requested\n");
		err = nfp_reset_soft(nfp);
		if (err < 0) {
			dev_warn(&pdev->dev,
				 "Could not soft-reset, err = %d\n", err);
			goto exit;
		}
		dev_info(&pdev->dev, "NFP soft-reset completed\n");
	}

	if (!fw_name) {
		err = 0;
		goto exit;
	}

	/* Make sure we have the ARM service processor */
	if (need_armsp) {
		dev_info(&pdev->dev,
			 "Waiting for NSP to respond (%d sec max).\n", timeout);
		for (; timeout > 0; timeout--) {
			err = nfp_nsp_command(nfp, SPCODE_NOOP, 0, 0, 0);
			if (err != -EAGAIN)
				break;
			if (msleep_interruptible(1000) > 0) {
				err = -ETIMEDOUT;
				break;
			}
		}
		if (err < 0)
			goto exit;
	}

	nfp_device_close(nfp);
	nfp = NULL;

	err = request_firmware(&fw, fw_name, &pdev->dev);
	if (err < 0) {
		fw = NULL;
		goto exit;
	}

	err = nfp_ca_replay(cpp, fw->data, fw->size);
	release_firmware(fw);
	if (err < 0)
		goto exit;

	dev_info(&pdev->dev, "Loaded FW image: %s\n", fw_name);
	return 1;

exit:
	if (nfp)
		nfp_device_close(nfp);

	if (err < 0 && fw_name) {
		dev_err(&pdev->dev, "Could not %s firmare \"%s\", err %d\n",
			fw ? "load" : "request", fw_name, err);
	}

	return err;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
#ifdef CONFIG_PCI_IOV
/* Kernel version 3.8 introduced a standard, sysfs based interface for
 * managing VFs.  Here we implement that interface for older kernels. */
static ssize_t show_sriov_totalvfs(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%u\n", pci_sriov_get_totalvfs(pdev));
}

static ssize_t show_sriov_numvfs(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nfp_pci *nfp = pci_get_drvdata(pdev);

	return sprintf(buf, "%u\n", nfp->num_vfs);
}

/*
 * num_vfs > 0; number of VFs to enable
 * num_vfs = 0; disable all VFs
 *
 * Note: SRIOV spec doesn't allow partial VF
 *       disable, so it's all or none.
 */
static ssize_t store_sriov_numvfs(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nfp_pci *nfp = pci_get_drvdata(pdev);
	int ret;
	unsigned long num_vfs;

	ret = kstrtoul(buf, 0, &num_vfs);
	if (ret < 0)
		return ret;

	if (num_vfs > pci_sriov_get_totalvfs(pdev))
		return -ERANGE;

	if (num_vfs == nfp->num_vfs)
		return count;           /* no change */

	if (num_vfs == 0) {
		/* disable VFs */
		ret = nfp6000_pcie_sriov_configure(pdev, 0);
		if (ret < 0)
			return ret;
		return count;
	}

	/* enable VFs */
	if (nfp->num_vfs) {
		dev_warn(&pdev->dev, "%d VFs already enabled. Disable before enabling %d VFs\n",
			 nfp->num_vfs, (int)num_vfs);
		return -EBUSY;
	}

	ret = nfp6000_pcie_sriov_configure(pdev, num_vfs);
	if (ret < 0)
		return ret;

	if (ret != num_vfs)
		dev_warn(&pdev->dev, "%d VFs requested; only %d enabled\n",
			 (int)num_vfs, ret);

	return count;
}

static DEVICE_ATTR(sriov_totalvfs, S_IRUGO, show_sriov_totalvfs, NULL);
static DEVICE_ATTR(sriov_numvfs, S_IRUGO | S_IWUSR | S_IWGRP,
		   show_sriov_numvfs, store_sriov_numvfs);

static int nfp_sriov_attr_add(struct device *dev)
{
	int err = 0;

	err = device_create_file(dev, &dev_attr_sriov_totalvfs);
	if (err)
		return err;

	return device_create_file(dev, &dev_attr_sriov_numvfs);
}

static void nfp_sriov_attr_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_sriov_totalvfs);
	device_remove_file(dev, &dev_attr_sriov_numvfs);
}
#endif /* CONFIG_PCI_IOV */
#endif /* Linux kernel version */

static void register_pf(struct nfp_pci *np)
{
	int pcie_unit;
	u32 model = nfp_cpp_model(np->cpp);

	pcie_unit = NFP_CPP_INTERFACE_UNIT_of(nfp_cpp_interface(np->cpp));

	if (nfp_mon_err && NFP_CPP_MODEL_IS_3200(model))
		np->nfp_mon_err = nfp_platform_device_register(np->cpp,
				NFP_MON_ERR_TYPE);

	if (nfp_dev_cpp)
		np->nfp_dev_cpp = nfp_platform_device_register(np->cpp,
				NFP_DEV_CPP_TYPE);

	if (nfp_net_vnic)
		np->nfp_net_vnic = nfp_platform_device_register_unit(np->cpp,
							   NFP_NET_VNIC_TYPE,
							   pcie_unit,
							   NFP_NET_VNIC_UNITS);

	if (nfp_net_null)
		np->nfp_net_null = nfp_platform_device_register(np->cpp,
							   NFP_NET_NULL_TYPE);
}

static int nfp_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *pci_id)
{
	struct nfp_pci *np;
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

	switch (pdev->device) {
	case PCI_DEVICE_NFP3200:
		np->cpp = nfp_cpp_from_nfp3200_pcie(pdev, irq);
		break;
	case PCI_DEVICE_NFP4000:
	case PCI_DEVICE_NFP6000:
		np->cpp = nfp_cpp_from_nfp6000_pcie(pdev, irq);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
		if (!IS_ERR_OR_NULL(np->cpp)) {
			err = nfp_sriov_attr_add(&pdev->dev);
			if (err < 0)
				goto err_nfp_cpp;
		}
#endif
		break;
	default:
		err = -ENODEV;
		goto err_nfp_cpp;
	}

	if (IS_ERR_OR_NULL(np->cpp)) {
		err = PTR_ERR(np->cpp);
		if (err >= 0)
			err = -ENOMEM;
		goto err_nfp_cpp;
	}

	err = nfp_pcie_fw_load(pdev, np->cpp);
	if (err < 0)
		goto err_fw_load;

	np->fw_loaded = !!err;

	register_pf(np);

	pci_set_drvdata(pdev, np);

	return 0;

err_fw_load:
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
	struct nfp_pci *np = pci_get_drvdata(pdev);

	nfp_platform_device_unregister(np->nfp_net_null);
	nfp_platform_device_unregister(np->nfp_net_vnic);
	nfp_platform_device_unregister(np->nfp_mon_err);

	nfp6000_pcie_sriov_disable(pdev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
#ifdef CONFIG_PCI_IOV
	if (pdev->device == PCI_DEVICE_NFP6000 ||
	    pdev->device == PCI_DEVICE_NFP4000)
		nfp_sriov_attr_remove(&pdev->dev);
#endif
#endif

	if (np->fw_loaded) {
		int rc;
		struct nfp_device *nfp_dev;

		nfp_dev = nfp_device_from_cpp(np->cpp);

		if (nfp_dev) {
			rc = nfp_reset_soft(nfp_dev);
			if (rc < 0)
				dev_warn(&pdev->dev, "Could not unload firmware, err = %d\n",
					 rc);
			else
				dev_info(&pdev->dev, "Firmware safely unloaded\n");
			nfp_device_close(nfp_dev);
		}
	}

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
	.sriov_configure = nfp6000_pcie_sriov_configure,
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

	err = nfp_mon_err_init();
	if (err < 0)
		goto fail_mon_err_init;

	err = nfp_dev_cpp_init();
	if (err < 0)
		goto fail_dev_cpp_init;

	err = nfp_net_null_init();
	if (err < 0)
		goto fail_net_null_init;

	err = nfp_net_vnic_init();
	if (err < 0)
		goto fail_net_vnic_init;

	err = nfp3200_plat_init();
	if (err < 0)
		goto fail_plat_init;

	err = pci_register_driver(&nfp_pcie_driver);
	if (err < 0)
		goto fail_pci_init;

	return err;

fail_pci_init:
	nfp3200_plat_exit();
fail_plat_init:
	nfp_net_vnic_exit();
fail_net_vnic_init:
	nfp_net_null_exit();
fail_net_null_init:
	nfp_dev_cpp_exit();
fail_dev_cpp_init:
	nfp_mon_err_exit();
fail_mon_err_init:
	nfp_cppcore_exit();
fail_cppcore_init:
	return err;
}

static void __exit nfp_main_exit(void)
{
	pci_unregister_driver(&nfp_pcie_driver);
	nfp3200_plat_exit();
	nfp_net_vnic_exit();
	nfp_net_null_exit();
	nfp_mon_err_exit();
	nfp_dev_cpp_exit();
	nfp_cppcore_exit();
}

module_init(nfp_main_init);
module_exit(nfp_main_exit);

MODULE_AUTHOR("Netronome Systems <support@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Netronome Flow Processor (NFP) driver.");
MODULE_INFO_NFP();
