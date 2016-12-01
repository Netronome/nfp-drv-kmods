/*
 * Copyright (C) 2016 Netronome Systems, Inc.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/pci.h>
#include <linux/firmware.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"

#include "nfp_main.h"

bool nfp_reset;
module_param(nfp_reset, bool, 0444);
MODULE_PARM_DESC(nfp_reset,
		 "Soft reset the NFP on init (default = false)");

static bool fw_load_required;
module_param(fw_load_required, bool, 0444);
MODULE_PARM_DESC(fw_load_required,
		 "Stop if requesting FW failed (default = false)");

static char *nfp6000_firmware;
module_param(nfp6000_firmware, charp, 0444);
MODULE_PARM_DESC(nfp6000_firmware, "(non-netdev mode) NFP6000 firmware to load from /lib/firmware/ (default = unset to not load FW)");

/* Default FW names */
#define NFP_NET_FW_DEFAULT	"nfp6000_net"
MODULE_FIRMWARE("netronome/" NFP_NET_FW_DEFAULT ".cat");

static int nfp_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	int err = 0;
	int max_vfs;

	switch (pdev->device) {
	case PCI_DEVICE_NFP4000:
	case PCI_DEVICE_NFP6000:
		max_vfs = 64;
		break;
	case PCI_DEVICE_NFP6010:
		max_vfs = 248;
		break;
	default:
		return -ENOTSUPP;
	}

	if (num_vfs > max_vfs) {
		err = -EPERM;
		goto err_out;
	}

	pf->num_vfs = num_vfs;

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable PCI sriov: %d\n", err);
		goto err_out;
	}

	dev_dbg(&pdev->dev, "Created %d VFs.\n", pf->num_vfs);

	return num_vfs;

err_out:
	return err;
#endif
	return 0;
}

int nfp_pcie_sriov_disable(struct pci_dev *pdev)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);

	/* If the VFs are assigned we cannot shut down SR-IOV without
	 * causing issues, so just leave the hardware available but
	 * disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Disabling while VFs assigned - VFs will not be deallocated\n");
		return -EPERM;
	}

	pf->num_vfs = 0;

	pci_disable_sriov(pdev);
	dev_dbg(&pdev->dev, "Removed VFs.\n");
#endif
	return 0;
}

int nfp_pcie_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs == 0)
		return nfp_pcie_sriov_disable(pdev);
	else
		return nfp_pcie_sriov_enable(pdev, num_vfs);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
#ifdef CONFIG_PCI_IOV
/* Kernel version 3.8 introduced a standard, sysfs based interface for
 * managing VFs.  Here we implement that interface for older kernels.
 */
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
	struct nfp_pf *pf = pci_get_drvdata(to_pci_dev(dev));

	return sprintf(buf, "%u\n", pf->num_vfs);
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
	struct nfp_pf *pf = pci_get_drvdata(to_pci_dev(dev));
	unsigned long num_vfs;
	int ret;

	ret = kstrtoul(buf, 0, &num_vfs);
	if (ret < 0)
		return ret;

	if (num_vfs > pci_sriov_get_totalvfs(pdev))
		return -ERANGE;

	if (num_vfs == pf->num_vfs)
		return count;           /* no change */

	if (num_vfs == 0) {
		/* disable VFs */
		ret = nfp_pcie_sriov_configure(pdev, 0);
		if (ret < 0)
			return ret;
		return count;
	}

	/* enable VFs */
	if (pf->num_vfs) {
		dev_warn(&pdev->dev, "%d VFs already enabled. Disable before enabling %d VFs\n",
			 pf->num_vfs, (int)num_vfs);
		return -EBUSY;
	}

	ret = nfp_pcie_sriov_configure(pdev, num_vfs);
	if (ret < 0)
		return ret;

	if (ret != num_vfs)
		dev_warn(&pdev->dev, "%d VFs requested; only %d enabled\n",
			 (int)num_vfs, ret);

	return count;
}

DEVICE_ATTR(sriov_totalvfs, S_IRUGO, show_sriov_totalvfs, NULL);
DEVICE_ATTR(sriov_numvfs, S_IRUGO | S_IWUSR | S_IWGRP,
	    show_sriov_numvfs, store_sriov_numvfs);

int nfp_sriov_attr_add(struct device *dev)
{
	int err = 0;

	err = device_create_file(dev, &dev_attr_sriov_totalvfs);
	if (err)
		return err;

	return device_create_file(dev, &dev_attr_sriov_numvfs);
}

void nfp_sriov_attr_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_sriov_totalvfs);
	device_remove_file(dev, &dev_attr_sriov_numvfs);
}
#endif /* CONFIG_PCI_IOV */
#endif /* Linux kernel version */

/**
 * nfp_net_fw_find() - Find the correct firmware image for netdev mode
 * @pdev:	PCI Device structure
 * @nfp:	NFP Device handle
 * @fwp:	Pointer to firmware pointer
 *
 * Return: -ERRNO on error, 0 on FW found or OK to continue without it
 */
static int nfp_net_fw_find(struct pci_dev *pdev, struct nfp_device *nfp,
			   const struct firmware **fwp)
{
	const struct firmware *fw = NULL;
	const char *fw_model;
	char fw_name[128];
	int err = 0;

	*fwp = NULL;

	fw_model = nfp_hwinfo_lookup(nfp, "assembly.partno");

	if (fw_model) {
		snprintf(fw_name,
			 sizeof(fw_name), "netronome/%s.cat", fw_model);
		fw_name[sizeof(fw_name) - 1] = 0;
		err = request_firmware(&fw, fw_name, &pdev->dev);
	}
	if (!fw_model || err < 0) {
		snprintf(fw_name, sizeof(fw_name),
			 "netronome/%s.cat", NFP_NET_FW_DEFAULT);
		fw_name[sizeof(fw_name) - 1] = 0;
		err = request_firmware(&fw, fw_name, &pdev->dev);
	}

	if (err < 0)
		return fw_load_required ? err : 0;

	dev_info(&pdev->dev, "Loading FW image: %s\n", fw_name);
	*fwp = fw;

	return 0;
}

static int nfp_fw_find(struct pci_dev *pdev, struct nfp_device *nfp,
		       const struct firmware **fwp)
{
	const struct firmware *fw = NULL;
	int err;

	*fwp = NULL;

	if (!nfp6000_firmware)
		return 0;

	err = request_firmware(&fw, nfp6000_firmware, &pdev->dev);
	if (err < 0)
		return fw_load_required ? err : 0;

	*fwp = fw;

	return 0;
}

/**
 * nfp_net_fw_load() - Load the firmware image
 * @pdev:       PCI Device structure
 * @nfp:        NFP Device structure
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
int nfp_fw_load(struct pci_dev *pdev, struct nfp_device *nfp, bool nfp_netdev)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	const struct firmware *fw;
	int timeout = 30;
	u16 interface;
	int err;

	interface = nfp_cpp_interface(cpp);
	if (NFP_CPP_INTERFACE_UNIT_of(interface) != 0) {
		/* Only Unit 0 should reset or load firmware */
		dev_info(&pdev->dev, "Firmware will be loaded by partner\n");
		return 0;
	}

	if (!nfp_netdev)
		err = nfp_fw_find(pdev, nfp, &fw);
	else
		err = nfp_net_fw_find(pdev, nfp, &fw);
	if (err)
		return err;

	if (!fw && !nfp_reset)
		return 0;

	if (fw) {
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
		if (err < 0) {
			dev_err(&pdev->dev, "NSP failed to respond\n");
			goto err_release_fw;
		}
	}

	dev_info(&pdev->dev, "NFP soft-reset (implied:%d forced:%d)\n",
		 !!fw, nfp_reset);
	err = nfp_reset_soft(nfp);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to soft reset the NFP: %d\n",
			err);
		goto err_release_fw;
	}

	if (fw) {
		/* Lock the NFP, prevent others from touching it while we
		 * load the firmware.
		 */
		err = nfp_device_lock(nfp);
		if (err < 0) {
			dev_err(&pdev->dev, "Can't lock NFP device: %d\n", err);
			goto err_release_fw;
		}

		err = nfp_ca_replay(cpp, fw->data, fw->size);
		nfp_device_unlock(nfp);

		if (err < 0) {
			dev_err(&pdev->dev, "FW loading failed: %d\n",
				err);
			goto err_release_fw;
		}

		dev_info(&pdev->dev, "Finished loading FW image\n");
	}

err_release_fw:
	release_firmware(fw);

	return err ? err : !!fw;
}

void nfp_fw_unload(struct nfp_pf *pf)
{
	struct nfp_device *nfp_dev;
	int err;

	nfp_dev = nfp_device_from_cpp(pf->cpp);
	if (!nfp_dev) {
		dev_warn(&pf->pdev->dev,
			 "Firmware was not unloaded (can't get nfp_dev)\n");
		return;
	}

	err = nfp_reset_soft(nfp_dev);
	if (err < 0)
		dev_warn(&pf->pdev->dev, "Couldn't unload firmware: %d\n", err);
	else
		dev_info(&pf->pdev->dev, "Firmware safely unloaded\n");

	nfp_device_close(nfp_dev);
}
