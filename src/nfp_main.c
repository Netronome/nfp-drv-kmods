/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
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

#include "nfpcore/kcompat.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
#include <net/devlink.h>
#endif

#include "nfp_modinfo.h"

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_nsp.h"

#include "nfpcore/nfp6000_pcie.h"

#include "nfpcore/nfp_net_vnic.h"

#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"

#ifdef CONFIG_NFP_NET_PF
static bool nfp_pf_netdev = true;
#ifdef CONFIG_NFP_USER_SPACE_CPP
module_param(nfp_pf_netdev, bool, 0444);
MODULE_PARM_DESC(nfp_pf_netdev, "Create netdevs on PF (requires appropriate firmware) (default = true)");
#endif
#else
static const bool nfp_pf_netdev;
const struct devlink_ops nfp_devlink_ops;
#endif /* CONFIG_NFP_NET_PF */

static int nfp_fallback = -1;
#if defined(CONFIG_NFP_NET_PF) && defined(CONFIG_NFP_USER_SPACE_CPP)
module_param(nfp_fallback, bint, 0444);
MODULE_PARM_DESC(nfp_fallback, "(netdev mode) Stay bound to device with user space access only (no netdevs) if no suitable FW is present (default = nfp_dev_cpp)");
#endif

#ifdef CONFIG_NFP_USER_SPACE_CPP
int nfp_dev_cpp = -1;
module_param(nfp_dev_cpp, bint, 0444);
MODULE_PARM_DESC(nfp_dev_cpp,
		 "NFP CPP /dev interface (default = !nfp_pf_netdev)");
#else
int nfp_dev_cpp;
#endif

bool nfp_net_vnic;
module_param(nfp_net_vnic, bool, 0444);
MODULE_PARM_DESC(nfp_net_vnic, "vNIC net devices (default = false)");

static int nfp_mon_event = -1;
module_param(nfp_mon_event, bint, 0444);
MODULE_PARM_DESC(nfp_mon_event, "(non-netdev mode) Event monitor support (default = !nfp_pf_netdev)");

static bool fw_load_required;
module_param(fw_load_required, bool, 0444);
MODULE_PARM_DESC(fw_load_required,
		 "Stop if requesting FW failed (default = false)");

static const char nfp_driver_name[] = "nfp";
const char nfp_driver_version[] = NFP_SRC_VERSION;

static const struct pci_device_id nfp_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6010,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
#if COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);
#endif

static bool nfp_board_ready(struct nfp_pf *pf)
{
	const char *cp;
	long state;
	int err;

	cp = nfp_hwinfo_lookup(pf->hwinfo, "board.state");
	if (!cp)
		return false;

	err = kstrtol(cp, 0, &state);
	if (err < 0)
		return false;

	return state == 15;
}

static int nfp_pf_board_state_wait(struct nfp_pf *pf)
{
	const unsigned long wait_until = jiffies + 10 * HZ;

	while (!nfp_board_ready(pf)) {
		if (time_is_before_eq_jiffies(wait_until)) {
			nfp_err(pf->cpp, "NFP board initialization timeout\n");
			return -EINVAL;
		}

		nfp_info(pf->cpp, "waiting for board initialization\n");
		if (msleep_interruptible(500))
			return -ERESTARTSYS;

		/* Refresh cached information */
		kfree(pf->hwinfo);
		pf->hwinfo = nfp_hwinfo_read(pf->cpp);
	}

	return 0;
}

static int nfp_pcie_sriov_read_nfd_limit(struct nfp_pf *pf)
{
	int err;

	pf->limit_vfs = nfp_rtsym_read_le(pf->rtbl, "nfd_vf_cfg_max_vfs", &err);
	if (!err)
		return pci_sriov_set_totalvfs(pf->pdev, pf->limit_vfs);

	pf->limit_vfs = ~0;
	pci_sriov_set_totalvfs(pf->pdev, 0); /* 0 is unset */
	/* Allow any setting for backwards compatibility if symbol not found */
	if (err == -ENOENT)
		return 0;

	nfp_warn(pf->cpp, "Warning: VF limit read failed: %d\n", err);
	return err;
}

static int nfp_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	int err;

	if (num_vfs > pf->limit_vfs) {
		nfp_info(pf->cpp, "Firmware limits number of VFs to %u\n",
			 pf->limit_vfs);
		return -EINVAL;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable PCI SR-IOV: %d\n", err);
		return err;
	}

	mutex_lock(&pf->lock);

	err = nfp_pf_netdev ? nfp_app_sriov_enable(pf->app, num_vfs) : 0;
	if (err) {
		dev_warn(&pdev->dev,
			 "App specific PCI SR-IOV configuration failed: %d\n",
			 err);
		goto err_sriov_disable;
	}

	pf->num_vfs = num_vfs;

	dev_dbg(&pdev->dev, "Created %d VFs.\n", pf->num_vfs);

	mutex_unlock(&pf->lock);
	return num_vfs;

err_sriov_disable:
	mutex_unlock(&pf->lock);
	pci_disable_sriov(pdev);
	return err;
#endif
	return 0;
}

static int nfp_pcie_sriov_disable(struct pci_dev *pdev)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);

	mutex_lock(&pf->lock);

	/* If the VFs are assigned we cannot shut down SR-IOV without
	 * causing issues, so just leave the hardware available but
	 * disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Disabling while VFs assigned - VFs will not be deallocated\n");
		mutex_unlock(&pf->lock);
		return -EPERM;
	}

	nfp_app_sriov_disable(pf->app);

	pf->num_vfs = 0;

	mutex_unlock(&pf->lock);

	pci_disable_sriov(pdev);
	dev_dbg(&pdev->dev, "Removed VFs.\n");
#endif
	return 0;
}

static int nfp_pcie_sriov_configure(struct pci_dev *pdev, int num_vfs)
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

static const struct firmware *
nfp_net_fw_request(struct pci_dev *pdev, struct nfp_pf *pf, const char *name)
{
	const struct firmware *fw = NULL;
	int err;

	err = request_firmware_direct(&fw, name, &pdev->dev);
	nfp_info(pf->cpp, "  %s: %s\n",
		 name, err ? "not found" : "found, loading...");
	if (err)
		return NULL;

	return fw;
}

/**
 * nfp_net_fw_find() - Find the correct firmware image for netdev mode
 * @pdev:	PCI Device structure
 * @pf:		NFP PF Device structure
 *
 * Return: firmware if found and requested successfully.
 */
static const struct firmware *
nfp_net_fw_find(struct pci_dev *pdev, struct nfp_pf *pf)
{
	struct nfp_eth_table_port *port;
	const struct firmware *fw;
	const char *fw_model;
	char fw_name[256];
	const u8 *serial;
	u16 interface;
	int spc, i, j;

	nfp_info(pf->cpp, "Looking for firmware file in order of priority:\n");

	/* First try to find a firmware image specific for this device */
	interface = nfp_cpp_interface(pf->cpp);
	nfp_cpp_serial(pf->cpp, &serial);
	sprintf(fw_name, "netronome/serial-%pMF-%02hhx-%02hhx.nffw",
		serial, interface >> 8, interface & 0xff);
	fw = nfp_net_fw_request(pdev, pf, fw_name);
	if (fw)
		return fw;

	/* Then try the PCI name */
	sprintf(fw_name, "netronome/pci-%s.nffw", pci_name(pdev));
	fw = nfp_net_fw_request(pdev, pf, fw_name);
	if (fw)
		return fw;

	/* Finally try the card type and media */
	if (!pf->eth_tbl) {
		dev_err(&pdev->dev, "Error: can't identify media config\n");
		return NULL;
	}

	fw_model = nfp_hwinfo_lookup(pf->hwinfo, "assembly.partno");
	if (!fw_model) {
		dev_err(&pdev->dev, "Error: can't read part number\n");
		return NULL;
	}

	spc = ARRAY_SIZE(fw_name);
	spc -= snprintf(fw_name, spc, "netronome/nic_%s", fw_model);

	for (i = 0; spc > 0 && i < pf->eth_tbl->count; i += j) {
		port = &pf->eth_tbl->ports[i];
		j = 1;
		while (i + j < pf->eth_tbl->count &&
		       port->speed == port[j].speed)
			j++;

		spc -= snprintf(&fw_name[ARRAY_SIZE(fw_name) - spc], spc,
				"_%dx%d", j, port->speed / 1000);
	}

	if (spc <= 0)
		return NULL;

	spc -= snprintf(&fw_name[ARRAY_SIZE(fw_name) - spc], spc, ".nffw");
	if (spc <= 0)
		return NULL;

	return nfp_net_fw_request(pdev, pf, fw_name);
}

/**
 * nfp_net_fw_load() - Load the firmware image
 * @pdev:       PCI Device structure
 * @pf:		NFP PF Device structure
 * @nsp:	NFP SP handle
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
static int
nfp_fw_load(struct pci_dev *pdev, struct nfp_pf *pf, struct nfp_nsp *nsp)
{
	const struct firmware *fw;
	u16 interface;
	int err;

	interface = nfp_cpp_interface(pf->cpp);
	if (NFP_CPP_INTERFACE_UNIT_of(interface) != 0) {
		/* Only Unit 0 should reset or load firmware */
		dev_info(&pdev->dev, "Firmware will be loaded by partner\n");
		return 0;
	}

	fw = nfp_net_fw_find(pdev, pf);
	if (!fw)
		return fw_load_required ? -ENOENT : 0;

	dev_info(&pdev->dev, "Soft-reset, loading FW image\n");
	err = nfp_nsp_device_soft_reset(nsp);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to soft reset the NFP: %d\n",
			err);
		goto exit_release_fw;
	}

	err = nfp_nsp_load_fw(nsp, fw);

	if (err < 0) {
		dev_err(&pdev->dev, "FW loading failed: %d\n", err);
		goto exit_release_fw;
	}

	dev_info(&pdev->dev, "Finished loading FW image\n");

exit_release_fw:
	release_firmware(fw);

	return err < 0 ? err : 1;
}

static void
nfp_nsp_init_ports(struct pci_dev *pdev, struct nfp_pf *pf,
		   struct nfp_nsp *nsp)
{
	bool needs_reinit = false;
	int i;

	pf->eth_tbl = __nfp_eth_read_ports(pf->cpp, nsp);
	if (!pf->eth_tbl)
		return;

	if (!nfp_nsp_has_mac_reinit(nsp))
		return;

	for (i = 0; i < pf->eth_tbl->count; i++)
		needs_reinit |= pf->eth_tbl->ports[i].override_changed;
	if (!needs_reinit)
		return;

	kfree(pf->eth_tbl);
	if (nfp_nsp_mac_reinit(nsp))
		dev_warn(&pdev->dev, "MAC reinit failed\n");

	pf->eth_tbl = __nfp_eth_read_ports(pf->cpp, nsp);
}

static int nfp_nsp_init(struct pci_dev *pdev, struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	int err;

	if (!nfp_pf_netdev)
		return 0;

	err = nfp_resource_wait(pf->cpp, NFP_RESOURCE_NSP, 30);
	if (err)
		return err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		dev_err(&pdev->dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	err = nfp_nsp_wait(nsp);
	if (err < 0)
		goto exit_close_nsp;

	nfp_nsp_init_ports(pdev, pf, nsp);

	pf->nspi = __nfp_nsp_identify(nsp);
	if (pf->nspi)
		dev_info(&pdev->dev, "BSP: %s\n", pf->nspi->version);

	err = nfp_fw_load(pdev, pf, nsp);
	if (err < 0) {
		kfree(pf->nspi);
		kfree(pf->eth_tbl);
		dev_err(&pdev->dev, "Failed to load FW\n");
		goto exit_close_nsp;
	}

	pf->fw_loaded = !!err;
	err = 0;

exit_close_nsp:
	nfp_nsp_close(nsp);

	return err;
}

static void nfp_fw_unload(struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		nfp_err(pf->cpp, "Reset failed, can't open NSP\n");
		return;
	}

	err = nfp_nsp_device_soft_reset(nsp);
	if (err < 0)
		dev_warn(&pf->pdev->dev, "Couldn't unload firmware: %d\n", err);
	else
		dev_info(&pf->pdev->dev, "Firmware safely unloaded\n");

	nfp_nsp_close(nsp);
}

static void nfp_register_vnic(struct nfp_pf *pf)
{
	int pcie_unit;

	pcie_unit = nfp_cppcore_pcie_unit(pf->cpp);

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
	struct devlink *devlink;
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

	devlink = devlink_alloc(&nfp_devlink_ops, sizeof(*pf));
	if (!devlink) {
		err = -ENOMEM;
		goto err_rel_regions;
	}
	pf = devlink_priv(devlink);
	INIT_LIST_HEAD(&pf->vnics);
	INIT_LIST_HEAD(&pf->ports);
	mutex_init(&pf->lock);
	pci_set_drvdata(pdev, pf);
	pf->pdev = pdev;

	pf->wq = alloc_workqueue("nfp-%s", 0, 2, pci_name(pdev));
	if (!pf->wq) {
		err = -ENOMEM;
		goto err_pci_priv_unset;
	}

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

	err = nfp_resource_table_init(pf->cpp);
	if (err)
		goto err_cpp_free;

	pf->hwinfo = nfp_hwinfo_read(pf->cpp);

	dev_info(&pdev->dev, "Assembly: %s%s%s-%s CPLD: %s\n",
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.vendor"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.partno"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.serial"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.revision"),
		 nfp_hwinfo_lookup(pf->hwinfo, "cpld.version"));

	err = nfp_pf_board_state_wait(pf);
	if (err && nfp_pf_netdev)
		goto err_hwinfo_free;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	err = nfp_sriov_attr_add(&pdev->dev);
	if (err < 0)
		goto err_hwinfo_free;
#endif
	err = nfp_nsp_init(pdev, pf);
	if (err)
		goto err_sriov_remove;

	pf->mip = nfp_mip_open(pf->cpp);
	pf->rtbl = __nfp_rtsym_table_read(pf->cpp, pf->mip);

	pf->dump_flag = NFP_DUMP_NSP_DIAG;
	pf->dumpspec = nfp_net_dump_load_dumpspec(pf->cpp, pf->rtbl);

	err = nfp_pcie_sriov_read_nfd_limit(pf);
	if (err)
		goto err_fw_unload;

	pf->num_vfs = pci_num_vf(pdev);
	if (pf->num_vfs > pf->limit_vfs) {
		dev_err(&pdev->dev,
			"Error: %d VFs already enabled, but loaded FW can only support %d\n",
			pf->num_vfs, pf->limit_vfs);
		err = -EINVAL;
		goto err_fw_unload;
	}

	if (nfp_dev_cpp) {
		pf->nfp_dev_cpp =
			nfp_platform_device_register(pf->cpp, NFP_DEV_CPP_TYPE);
		if (!pf->nfp_dev_cpp)
			dev_err(&pdev->dev, "Failed to enable user space access. Ignoring.\n");
	}

	if (nfp_pf_netdev) {
		err = nfp_net_pci_probe(pf);
		if (nfp_fallback && err == 1) {
			dev_info(&pdev->dev, "Netronome NFP Fallback driver\n");
		} else if (err) {
			err = err < 0 ? err : -EINVAL;
			goto err_dev_cpp_unreg;
		}
	} else {
		nfp_register_vnic(pf);
	}

	err = nfp_hwmon_register(pf);
	if (err) {
		dev_err(&pdev->dev, "Failed to register hwmon info\n");
		goto err_net_remove;
	}

	return 0;

err_net_remove:
	if (pf->nfp_net_vnic)
		nfp_platform_device_unregister(pf->nfp_net_vnic);
	if (nfp_pf_netdev)
		nfp_net_pci_remove(pf);
err_dev_cpp_unreg:
	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);
	pci_sriov_set_totalvfs(pf->pdev, 0);
err_fw_unload:
	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (pf->fw_loaded)
		nfp_fw_unload(pf);
	kfree(pf->eth_tbl);
	kfree(pf->nspi);
	vfree(pf->dumpspec);
err_sriov_remove:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
#endif
err_hwinfo_free:
	kfree(pf->hwinfo);
err_cpp_free:
	nfp_cpp_free(pf->cpp);
err_disable_msix:
	if (pdev->msix_enabled)
		pci_disable_msix(pdev);
	destroy_workqueue(pf->wq);
err_pci_priv_unset:
	pci_set_drvdata(pdev, NULL);
	mutex_destroy(&pf->lock);
	devlink_free(devlink);
err_rel_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);

	return err;
}

static void nfp_pci_remove(struct pci_dev *pdev)
{
	struct nfp_pf *pf = pci_get_drvdata(pdev);

	nfp_hwmon_unregister(pf);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
#endif
	nfp_pcie_sriov_disable(pdev);
	pci_sriov_set_totalvfs(pf->pdev, 0);

	if (nfp_pf_netdev && pf->app)
		nfp_net_pci_remove(pf);

	if (pf->nfp_net_vnic)
		nfp_platform_device_unregister(pf->nfp_net_vnic);

	vfree(pf->dumpspec);
	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (pf->fw_loaded)
		nfp_fw_unload(pf);

	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);

	destroy_workqueue(pf->wq);
	pci_set_drvdata(pdev, NULL);
	kfree(pf->hwinfo);
	nfp_cpp_free(pf->cpp);

	if (nfp_mon_event)
		pci_disable_msix(pdev);

	kfree(pf->eth_tbl);
	kfree(pf->nspi);
	mutex_destroy(&pf->lock);
	devlink_free(priv_to_devlink(pf));
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nfp_pci_driver = {
	.name			= nfp_driver_name,
	.id_table		= nfp_pci_device_ids,
	.probe			= nfp_pci_probe,
	.remove			= nfp_pci_remove,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.sriov_configure	= nfp_pcie_sriov_configure,
#endif
};

#if !COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES
static const struct pci_device_id compat_nfp_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP6000,
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
		pr_warn("nfp_fallback is ignored in non-netdev mode\n");
	if (nfp_fallback == 1 && nfp_dev_cpp == 0) {
		pr_err("nfp_fallback cannot be used without nfp_dev_cpp\n");
		return false;
	}

#ifdef CONFIG_NFP_USER_SPACE_CPP
	if (nfp_dev_cpp == -1)
		nfp_dev_cpp = !nfp_pf_netdev;
#endif
	if (nfp_mon_event == -1)
		nfp_mon_event = !nfp_pf_netdev;
	if (nfp_fallback == -1)
		nfp_fallback = nfp_pf_netdev && nfp_dev_cpp;

	return true;
}

static int __init nfp_main_init(void)
{
	int err;

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2017 Netronome Systems\n",
		nfp_driver_name);
	pr_info(NFP_BUILD_DESCRIPTION(nfp));

	if (!nfp_resolve_params())
		return -EINVAL;

	err = nfp_cppcore_init();
	if (err < 0)
		return err;

	err = nfp_dev_cpp_init();
	if (err < 0)
		goto err_exit_cppcore;

	err = nfp_net_vnic_init();
	if (err < 0)
		goto err_exit_dev_cpp;

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
err_exit_dev_cpp:
	nfp_dev_cpp_exit();
err_exit_cppcore:
	nfp_cppcore_exit();
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

MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_4x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0096-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_2x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_4x10_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_8x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x25.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_1x10_1x25.nffw");

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Netronome Flow Processor (NFP) driver.");
MODULE_VERSION("4.18.0");
MODULE_INFO_NFP();
