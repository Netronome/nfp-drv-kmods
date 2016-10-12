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
 * nfp_net_main.c
 * Netronome network device driver: Main entry point
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Alejandro Lucero <alejandro.lucero@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/msi.h>
#include <linux/random.h>
#include <linux/firmware.h>

#include <linux/ktime.h>
#include <linux/hrtimer.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp6000_pcie.h"
#include "nfpcore/nfp_dev_cpp.h"

#include "nfp_net_compat.h"
#include "nfp_net_ctrl.h"
#include "nfp_net.h"

#include "nfp_modinfo.h"

#define NFP_PF_CSR_SLICE_SIZE	(32 * 1024)

/**
 * struct nfp_net_pf - NFP PF-specific device structure
 * @pdev:		Backpointer to PCI device
 * @cpp:		Pointer to the CPP handle
 * @nfp_dev_cpp:	Pointer to the NFP Device handle
 * @ctrl_area:		Pointer to the CPP area for the control BAR
 * @tx_area:		Pointer to the CPP area for the TX queues
 * @rx_area:		Pointer to the CPP area for the FL/RX queues
 * @irq_entries:	Array of MSI-X entries for all ports
 * @num_vfs:		Number of SR-IOV VFs enabled
 * @fw_loaded:		Is the firmware loaded?
 * @nfp_fallback:	Is the driver used in fallback mode?
 * @ddir:		Per-device debugfs directory
 * @num_ports:		Number of adapter ports
 * @ports:		Linked list of port structures (struct nfp_net)
 */
struct nfp_net_pf {
	struct pci_dev *pdev;

	struct nfp_cpp *cpp;
	struct platform_device *nfp_dev_cpp;

	struct nfp_cpp_area *ctrl_area;
	struct nfp_cpp_area *tx_area;
	struct nfp_cpp_area *rx_area;

	struct msix_entry *irq_entries;

	unsigned int num_vfs;

	bool fw_loaded;
	bool nfp_fallback;

	struct dentry *ddir;

	unsigned int num_ports;
	struct list_head ports;
};

static bool nfp_dev_cpp = true;
module_param(nfp_dev_cpp, bool, 0444);
MODULE_PARM_DESC(nfp_dev_cpp,
		 "Enable NFP CPP user-space access (default = true)");

/* Default FW names */
#define NFP_NET_FW_DEFAULT	"nfp6000_net"
MODULE_FIRMWARE("netronome/" NFP_NET_FW_DEFAULT ".cat");

static bool fw_stop_on_fail;
module_param(fw_stop_on_fail, bool, 0444);
MODULE_PARM_DESC(fw_stop_on_fail, "Stop if FW load fails (default = false)");

static bool nfp_fallback = true;
module_param(nfp_fallback, bool, 0444);
MODULE_PARM_DESC(nfp_fallback,
		 "Fallback to nfp.ko behaviour if no suitable FW is present (default = true)");

static bool nfp_reset;
module_param(nfp_reset, bool, 0444);
MODULE_PARM_DESC(nfp_reset,
		 "Force soft reset the NFP during init (default = false)");

const char nfp_net_driver_name[] = "nfp_net";
const char nfp_net_driver_version[] = "0.1";

static const struct pci_device_id nfp_net_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_net_pci_device_ids);

/* Firmware loading functions */

/**
 * nfp_net_fw_find() - Find the correct firmware image
 * @pdev:       PCI Device structure
 * @nfp:        NFP Device structure
 * @fwp:	Pointer to firmware pointer
 *
 * Return: -ERRNO on error, or 0 if firmware found
 */
static int nfp_net_fw_find(struct pci_dev *pdev, struct nfp_device *nfp,
			   const struct firmware **fwp)
{
	const struct firmware *fw = NULL;
	const char *fw_model;
	char fw_name[128];
	int err;

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

	if (err >= 0) {
		dev_info(&pdev->dev, "Loading FW image: %s\n", fw_name);
		*fwp = fw;
	} else {
		*fwp = NULL;
	}

	return err;
}

/**
 * nfp_net_fw_load() - Load the firmware image
 * @pdev:       PCI Device structure
 * @nfp:        NFP Device structure
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
static int nfp_net_fw_load(struct pci_dev *pdev, struct nfp_device *nfp)
{
	struct nfp_cpp *cpp = nfp_device_cpp(nfp);
	const struct firmware *fw = NULL;
	u16 interface;
	int timeout = 30; /* Seconds */
	int err;

	interface = nfp_cpp_interface(cpp);
	if (NFP_CPP_INTERFACE_UNIT_of(interface) != 0) {
		/* Only Unit 0 should reset or load firmware */
		dev_info(&pdev->dev, "Firmware will be loaded by partner\n");
		return 0;
	}

	err = nfp_net_fw_find(pdev, nfp, &fw);
	if (err < 0) {
		dev_err(&pdev->dev, "No firmware located for NFP%s: %d\n",
			nfp_reset ? ", reset only mode" : "", err);
		/* Even though we did not find firmware, this is not
		 * fatal. We will simply use the fallback driver.
		 */
		err = 0;
	}

	if (fw) {
		/* Make sure we have the ARM service processor */
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
			dev_err(&pdev->dev, "Failed to find NFP Service Processor: %d\n",
				err);
			goto exit_release_fw;
		}
	}

	/* Always soft-reset the NFP before loading firmware */
	if (fw || nfp_reset) {
		dev_info(&pdev->dev, "NFP soft-reset...\n");
		err = nfp_reset_soft(nfp);
		if (err < 0) {
			dev_err(&pdev->dev, "Failed to soft reset the NFP: %d\n",
				err);
			goto exit_release_fw;
		}
	}

	if (fw) {
		/* Lock the NFP, prevent others from touching it while we
		 * load the firmware.
		 */
		err = nfp_device_lock(nfp);
		if (err < 0) {
			fw_stop_on_fail = true;
			dev_err(&pdev->dev, "Can't lock NFP device: %d\n", err);
			goto exit_release_fw;
		}

		err = nfp_ca_replay(cpp, fw->data, fw->size);
		nfp_device_unlock(nfp);

		if (err < 0) {
			dev_err(&pdev->dev, "FW loading failed: %d\n",
				err);
			goto exit_release_fw;
		}

		release_firmware(fw);

		dev_info(&pdev->dev, "Finished loading FW image\n");
	}

	return fw ? 1 : 0;

exit_release_fw:
	release_firmware(fw);

	return fw_stop_on_fail ? err : 0;
}

static void nfp_net_fw_unload(struct nfp_net_pf *pf)
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

/*
 * SR-IOV support
 */
static int nfp_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_net_pf *pf = pci_get_drvdata(pdev);
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

static int nfp_pcie_sriov_disable(struct pci_dev *pdev)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_net_pf *pf = pci_get_drvdata(pdev);

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
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nfp_net_pf *pf = pci_get_drvdata(pdev);

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
	struct pci_dev *pdev = to_pci_dev(dev);
	struct nfp_net_pf *pf = pci_get_drvdata(pdev);
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

/*
 * Helper functions
 */

static int nfp_is_ready(struct nfp_device *nfp)
{
	const char *cp;
	long state;
	int err;

	cp = nfp_hwinfo_lookup(nfp, "board.state");
	if (!cp)
		return 0;

	err = kstrtol(cp, 0, &state);
	if (err < 0)
		return 0;

	return state == 15;
}

/**
 * nfp_net_map_area() - Help function to map an area
 * @cpp:    NFP CPP handler
 * @name:   Name for the area
 * @target: CPP target
 * @addr:   CPP address
 * @size:   Size of the area
 * @area:   Area handle (returned).
 *
 * This function is primarily to simplify the code in the main probe
 * function. To undo the effect of this functions call
 * @nfp_cpp_area_release_free(*area);
 *
 * Return: Pointer to memory mapped area or ERR_PTR
 */
static u8 __iomem *nfp_net_map_area(struct nfp_cpp *cpp,
				    const char *name, int isl, int target,
				    unsigned long long addr, unsigned long size,
				    struct nfp_cpp_area **area)
{
	u8 __iomem *res;
	int err;

	*area = nfp_cpp_area_alloc_with_name(
		cpp, NFP_CPP_ISLAND_ID(target, NFP_CPP_ACTION_RW, 0, isl),
		name, addr, size);
	if (!*area) {
		err = -EIO;
		goto err_area;
	}

	err = nfp_cpp_area_acquire(*area);
	if (err < 0)
		goto err_acquire;

	res = nfp_cpp_area_iomem(*area);
	if (!res) {
		err = -EIO;
		goto err_map;
	}

	return res;

err_map:
	nfp_cpp_area_release(*area);
err_acquire:
	nfp_cpp_area_free(*area);
err_area:
	return (u8 __iomem *)ERR_PTR(err);
}

/**
 * nfp_net_get_mac_addr() - Get the MAC address.
 * @nn:       NFP Network structure
 * @nfp_dev:  NFP Device structure
 * @id:	      NFP port id
 *
 * First try to look up the MAC address in the HWINFO table. If that
 * fails generate a random address.
 */
static void
nfp_net_get_mac_addr(struct nfp_net *nn, struct nfp_device *nfp_dev,
		     unsigned int id)
{
	u8 mac_addr[ETH_ALEN];
	const char *mac_str;
	char name[32];

	snprintf(name, sizeof(name), "eth%d.mac", id);

	mac_str = nfp_hwinfo_lookup(nfp_dev, name);
	if (!mac_str) {
		dev_warn(&nn->pdev->dev,
			 "Can't lookup MAC address. Generate\n");
		eth_hw_addr_random(nn->netdev);
		return;
	}

	if (sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		dev_warn(&nn->pdev->dev,
			 "Can't parse MAC address (%s). Generate.\n", mac_str);
		eth_hw_addr_random(nn->netdev);
		return;
	}

	ether_addr_copy(nn->netdev->dev_addr, mac_addr);
	ether_addr_copy(nn->netdev->perm_addr, mac_addr);
}

static unsigned int
nfp_net_pf_get_num_ports(struct nfp_net_pf *pf, struct nfp_device *nfp_dev)
{
	char name[256];
	u16 interface;
	int pcie_pf;
	int err = 0;
	u64 val;

	interface = nfp_cpp_interface(pf->cpp);
	pcie_pf = NFP_CPP_INTERFACE_UNIT_of(interface);

	snprintf(name, sizeof(name), "nfd_cfg_pf%d_num_ports", pcie_pf);

	val = nfp_rtsym_read_le(nfp_dev, name, &err);
	/* Default to one port */
	if (err) {
		if (err != -ENOENT)
			nfp_err(nfp_dev, "Unable to read adapter port count\n");
		val = 1;
	}

	return val;
}

static unsigned int
nfp_net_pf_total_qcs(struct nfp_net_pf *pf, void __iomem *ctrl_bar,
		     unsigned int stride, u32 start_off, u32 num_off)
{
	unsigned int i, min_qc, max_qc;

	min_qc = readl(ctrl_bar + start_off);
	max_qc = min_qc;

	for (i = 0; i < pf->num_ports; i++) {
		/* To make our lives simpler only accept configuration where
		 * queues are allocated to PFs in order (queues of PFn all have
		 * indexes lower than PFn+1).
		 */
		if (max_qc > readl(ctrl_bar + start_off))
			return 0;

		max_qc = readl(ctrl_bar + start_off);
		max_qc += readl(ctrl_bar + num_off) * stride;
		ctrl_bar += NFP_PF_CSR_SLICE_SIZE;
	}

	return max_qc - min_qc;
}

static u8 __iomem *
nfp_net_pf_map_ctrl_bar(struct nfp_net_pf *pf, struct nfp_device *nfp_dev)
{
	const struct nfp_rtsym *ctrl_sym;
	u8 __iomem *ctrl_bar;
	char pf_symbol[256];
	u16 interface;
	int pcie_pf;

	interface = nfp_cpp_interface(pf->cpp);
	pcie_pf = NFP_CPP_INTERFACE_UNIT_of(interface);

	snprintf(pf_symbol, sizeof(pf_symbol), "_pf%d_net_bar0", pcie_pf);

	ctrl_sym = nfp_rtsym_lookup(nfp_dev, pf_symbol);
	if (!ctrl_sym) {
		dev_err(&pf->pdev->dev,
			"Failed to find PF BAR0 symbol %s\n", pf_symbol);
		return NULL;
	}

	if (ctrl_sym->size < pf->num_ports * NFP_PF_CSR_SLICE_SIZE) {
		dev_err(&pf->pdev->dev,
			"PF BAR0 too small to contain %d ports\n",
			pf->num_ports);
		return NULL;
	}

	ctrl_bar = nfp_net_map_area(pf->cpp, "net.ctrl",
				    ctrl_sym->domain, ctrl_sym->target,
				    ctrl_sym->addr, ctrl_sym->size,
				    &pf->ctrl_area);
	if (IS_ERR(ctrl_bar)) {
		dev_err(&pf->pdev->dev, "Failed to map PF BAR0: %ld\n",
			PTR_ERR(ctrl_bar));
		return NULL;
	}

	return ctrl_bar;
}

static void
nfp_net_pf_free_netdevs(struct nfp_net_pf *pf)
{
	struct nfp_net *nn;

	while (!list_empty(&pf->ports)) {
		nn = list_first_entry(&pf->ports, struct nfp_net, port_list);
		list_del(&nn->port_list);

		nfp_net_netdev_free(nn);
	}
}

static struct nfp_net *
nfp_net_pf_alloc_port_netdev(struct nfp_net_pf *pf, void __iomem *ctrl_bar,
			     void __iomem *tx_bar, void __iomem *rx_bar,
			     int stride, struct nfp_net_fw_version *fw_ver)
{
	u32 n_tx_rings, n_rx_rings;
	struct nfp_net *nn;

	n_tx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_TXRINGS);
	n_rx_rings = readl(ctrl_bar + NFP_NET_CFG_MAX_RXRINGS);

	/* Allocate and initialise the netdev */
	nn = nfp_net_netdev_alloc(pf->pdev, n_tx_rings, n_rx_rings);
	if (IS_ERR(nn))
		return nn;

	nn->fw_ver = *fw_ver;
	nn->ctrl_bar = ctrl_bar;
	nn->tx_bar = tx_bar;
	nn->rx_bar = rx_bar;
	nn->is_vf = 0;
	nn->stride_rx = stride;
	nn->stride_tx = stride;

	return nn;
}

static int
nfp_net_pf_init_port_netdev(struct nfp_net_pf *pf, struct nfp_net *nn,
			    struct nfp_device *nfp_dev, unsigned int id)
{
	int err;

	/* Get MAC address */
	nfp_net_get_mac_addr(nn, nfp_dev, id);

	/* Get ME clock frequency from ctrl BAR
	 * XXX for now frequency is hardcoded until we figure out how
	 * to get the value from nfp-hwinfo into ctrl bar
	 */
	nn->me_freq_mhz = 1200;

	/*
	 * Finalise
	 */
	err = nfp_net_netdev_init(nn->netdev);
	if (err)
		return err;

	nfp_net_debugfs_port_add(nn, pf->ddir, id);

	nfp_net_info(nn);

	return 0;
}

static int
nfp_net_pf_alloc_netdevs(struct nfp_net_pf *pf, void __iomem *ctrl_bar,
			 void __iomem *tx_bar, void __iomem *rx_bar,
			 int stride, struct nfp_net_fw_version *fw_ver)
{
	u32 prev_tx_base, prev_rx_base, tgt_tx_base, tgt_rx_base;
	struct nfp_net *nn;
	unsigned int i;
	int err;

	prev_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	prev_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);

	for (i = 0; i < pf->num_ports; i++) {
		tgt_tx_base = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
		tgt_rx_base = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
		tx_bar += (tgt_tx_base - prev_tx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		rx_bar += (tgt_rx_base - prev_rx_base) * NFP_QCP_QUEUE_ADDR_SZ;
		prev_tx_base = tgt_tx_base;
		prev_rx_base = tgt_rx_base;

		nn = nfp_net_pf_alloc_port_netdev(pf, ctrl_bar, tx_bar, rx_bar,
						  stride, fw_ver);
		if (IS_ERR(nn)) {
			err = PTR_ERR(nn);
			goto err_free_prev;
		}
		list_add_tail(&nn->port_list, &pf->ports);

		ctrl_bar += NFP_PF_CSR_SLICE_SIZE;
	}

	return 0;

err_free_prev:
	nfp_net_pf_free_netdevs(pf);
	return err;
}

static int
nfp_net_pf_spawn_netdevs(struct nfp_net_pf *pf, struct nfp_device *nfp_dev,
			 void __iomem *ctrl_bar, void __iomem *tx_bar,
			 void __iomem *rx_bar, int stride,
			 struct nfp_net_fw_version *fw_ver)
{
	unsigned int id, wanted_irqs, num_irqs, ports_left, irqs_left;
	struct nfp_net *nn;
	int err;

	/* Allocate the netdevs and do basic init */
	err = nfp_net_pf_alloc_netdevs(pf, ctrl_bar, tx_bar, rx_bar,
				       stride, fw_ver);
	if (err)
		return err;

	/* Get MSI-X vectors */
	wanted_irqs = 0;
	list_for_each_entry(nn, &pf->ports, port_list)
		wanted_irqs += nfp_net_irqs_wanted(nn);
	pf->irq_entries = kcalloc(wanted_irqs, sizeof(*pf->irq_entries),
				  GFP_KERNEL);
	if (!pf->irq_entries) {
		err = -ENOMEM;
		goto err_nn_free;
	}

	num_irqs = nfp_net_irqs_alloc(pf->pdev, pf->irq_entries,
				      NFP_NET_MIN_PORT_IRQS * pf->num_ports,
				      wanted_irqs);
	if (!num_irqs) {
		nn_warn(nn, "Unable to allocate MSI-X Vectors. Exiting\n");
		err = -ENOMEM;
		goto err_vec_free;
	}

	/* Distribute IRQs to ports */
	irqs_left = num_irqs;
	ports_left = pf->num_ports;
	list_for_each_entry(nn, &pf->ports, port_list) {
		unsigned int n;

		n = DIV_ROUND_UP(irqs_left, ports_left);
		nfp_net_irqs_assign(nn, &pf->irq_entries[num_irqs - irqs_left],
				    n);
		irqs_left -= n;
		ports_left--;
	}

	/* Finish netdev init and register */
	id = 0;
	list_for_each_entry(nn, &pf->ports, port_list) {
		err = nfp_net_pf_init_port_netdev(pf, nn, nfp_dev, id);
		if (err)
			goto err_prev_deinit;

		id++;
	}

	return 0;

err_prev_deinit:
	list_for_each_entry_continue_reverse(nn, &pf->ports, port_list) {
		nfp_net_debugfs_dir_clean(&nn->debugfs_dir);
		nfp_net_netdev_clean(nn->netdev);
	}
	nfp_net_irqs_disable(pf->pdev);
err_vec_free:
	kfree(pf->irq_entries);
err_nn_free:
	nfp_net_pf_free_netdevs(pf);
	return err;
}

/*
 * PCI device functions
 */
static int nfp_net_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *pci_id)
{
	u8 __iomem *ctrl_bar, *tx_bar, *rx_bar;
	u32 total_tx_qcs, total_rx_qcs;
	struct nfp_net_fw_version fw_ver;
	u32 tx_area_sz, rx_area_sz;
	struct nfp_device *nfp_dev;
	struct nfp_net_pf *pf;
	u32 start_q;
	int stride;
	int err;

	pf = kzalloc(sizeof(*pf), GFP_KERNEL);
	if (!pf)
		return -ENOMEM;
	INIT_LIST_HEAD(&pf->ports);
	pci_set_drvdata(pdev, pf);
	pf->pdev = pdev;

	err = pci_enable_device(pdev);
	if (err < 0)
		goto err_free_pf;

	pci_set_master(pdev);

	err = pci_request_regions(pdev, nfp_net_driver_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to reserve pci resources.\n");
		goto err_pci_disable;
	}

	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(NFP_NET_MAX_DMA_BITS));
	if (err)
		goto err_pci_regions;

	pf->cpp = nfp_cpp_from_nfp6000_pcie(pdev, -1);

	if (IS_ERR_OR_NULL(pf->cpp)) {
		err = PTR_ERR(pf->cpp);
		if (err >= 0)
			err = -ENOMEM;
		goto err_pci_regions;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	err = nfp_sriov_attr_add(&pdev->dev);
	if (err < 0)
		goto err_sriov;
#endif

	nfp_dev = nfp_device_from_cpp(pf->cpp);
	if (!nfp_dev) {
		err = -ENODEV;
		goto err_cpp_free;
	}

	err = nfp_net_fw_load(pdev, nfp_dev);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to load FW\n");
		goto err_nfp_close;
	}

	pf->fw_loaded = !!err;

	if (nfp_dev_cpp) {
		pf->nfp_dev_cpp =
			nfp_platform_device_register(pf->cpp, NFP_DEV_CPP_TYPE);
		if (!pf->nfp_dev_cpp)
			dev_err(&pdev->dev,
				"Failed to enable user space access. Ignored\n");
	}

	/* Verify that the board has completed initialization */
	if ((!pf->fw_loaded && nfp_reset) || !nfp_is_ready(nfp_dev)) {
		dev_err(&pdev->dev, "NFP is not ready for NIC operation.\n");
		ctrl_bar = NULL;
		err = -ENOENT;
		goto err_register_fallback;
	}

	pf->num_ports = nfp_net_pf_get_num_ports(pf, nfp_dev);

	ctrl_bar = nfp_net_pf_map_ctrl_bar(pf, nfp_dev);
	if (!ctrl_bar) {
		err = -EIO;
		goto err_register_fallback;
	}

	nfp_net_get_fw_version(&fw_ver, ctrl_bar);
	if (fw_ver.resv || fw_ver.class != NFP_NET_CFG_VERSION_CLASS_GENERIC) {
		dev_err(&pdev->dev, "Unknown Firmware ABI %d.%d.%d.%d\n",
			fw_ver.resv, fw_ver.class, fw_ver.major, fw_ver.minor);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	/* Determine stride */
	if (nfp_net_fw_ver_eq(&fw_ver, 0, 0, 0, 1)) {
		stride = 2;
		dev_warn(&pdev->dev, "OBSOLETE Firmware detected - VF isolation not available\n");
	} else {
		switch (fw_ver.major) {
		case 1 ... 4:
			stride = 4;
			break;
		default:
			dev_err(&pdev->dev, "Unsupported Firmware ABI %d.%d.%d.%d\n",
				fw_ver.resv, fw_ver.class,
				fw_ver.major, fw_ver.minor);
			err = -EINVAL;
			goto err_ctrl_unmap;
		}
	}

	/* Find how many QC structs need to be mapped */
	total_tx_qcs = nfp_net_pf_total_qcs(pf, ctrl_bar, stride,
					    NFP_NET_CFG_START_TXQ,
					    NFP_NET_CFG_MAX_TXRINGS);
	total_rx_qcs = nfp_net_pf_total_qcs(pf, ctrl_bar, stride,
					    NFP_NET_CFG_START_RXQ,
					    NFP_NET_CFG_MAX_RXRINGS);
	if (!total_tx_qcs || !total_rx_qcs) {
		dev_err(&pdev->dev, "Invalid PF QC configuration [%d,%d]\n",
			total_tx_qcs, total_rx_qcs);
		err = -EINVAL;
		goto err_ctrl_unmap;
	}

	tx_area_sz = NFP_QCP_QUEUE_ADDR_SZ * total_tx_qcs;
	rx_area_sz = NFP_QCP_QUEUE_ADDR_SZ * total_rx_qcs;

	/* Map TX queues */
	start_q = readl(ctrl_bar + NFP_NET_CFG_START_TXQ);
	tx_bar = nfp_net_map_area(pf->cpp, "net.tx", 0, 0,
				  NFP_PCIE_QUEUE(start_q),
				  tx_area_sz, &pf->tx_area);
	if (IS_ERR(tx_bar)) {
		dev_err(&pdev->dev, "Failed to map TX area.\n");
		err = PTR_ERR(tx_bar);
		goto err_ctrl_unmap;
	}

	/* Map RX queues */
	start_q = readl(ctrl_bar + NFP_NET_CFG_START_RXQ);
	rx_bar = nfp_net_map_area(pf->cpp, "net.rx", 0, 0,
				  NFP_PCIE_QUEUE(start_q),
				  rx_area_sz, &pf->rx_area);
	if (IS_ERR(rx_bar)) {
		dev_err(&pdev->dev, "Failed to map RX area.\n");
		err = PTR_ERR(rx_bar);
		goto err_unmap_tx;
	}

	pf->ddir = nfp_net_debugfs_device_add(pdev);

	err = nfp_net_pf_spawn_netdevs(pf, nfp_dev, ctrl_bar, tx_bar, rx_bar,
				       stride, &fw_ver);
	if (err)
		goto err_clean_ddir;

	nfp_device_close(nfp_dev);

	return 0;

err_clean_ddir:
	nfp_net_debugfs_dir_clean(&pf->ddir);
	nfp_cpp_area_release_free(pf->rx_area);
err_unmap_tx:
	nfp_cpp_area_release_free(pf->tx_area);
err_ctrl_unmap:
	nfp_cpp_area_release_free(pf->ctrl_area);
err_register_fallback:
	/* Register fallback only if there are problems with finding ctrl_bar
	 * or FW is not operational.
	 */
	if (!ctrl_bar && nfp_fallback) {
		pf->nfp_fallback = true;
		dev_info(&pdev->dev, "Netronome NFP Fallback driver\n");

		nfp_device_close(nfp_dev);
		return 0;
	}
	if (pf->fw_loaded) {
		int ret = nfp_reset_soft(nfp_dev);

		if (ret < 0)
			dev_warn(&pdev->dev,
				 "Couldn't unload firmware: %d\n", ret);
		else
			dev_info(&pdev->dev,
				 "Firmware safely unloaded\n");
	}
err_nfp_close:
	nfp_device_close(nfp_dev);
err_cpp_free:
	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && defined(CONFIG_PCI_IOV)
	nfp_sriov_attr_remove(&pdev->dev);
err_sriov:
#endif
	nfp_cpp_free(pf->cpp);
err_pci_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);
err_free_pf:
	pci_set_drvdata(pdev, NULL);
	kfree(pf);
	return err;
}

static void nfp_net_pci_remove(struct pci_dev *pdev)
{
	struct nfp_net_pf *pf = pci_get_drvdata(pdev);
	struct nfp_net *nn;

	list_for_each_entry(nn, &pf->ports, port_list) {
		nfp_net_debugfs_dir_clean(&nn->debugfs_dir);

		nfp_net_netdev_clean(nn->netdev);
	}

	nfp_net_pf_free_netdevs(pf);

	nfp_net_debugfs_dir_clean(&pf->ddir);

#ifdef CONFIG_PCI_IOV
	nfp_pcie_sriov_disable(pdev);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
	nfp_sriov_attr_remove(&pdev->dev);
#endif
#endif
	if (!pf->nfp_fallback) {
		nfp_net_irqs_disable(pf->pdev);
		kfree(pf->irq_entries);

		nfp_cpp_area_release_free(pf->rx_area);
		nfp_cpp_area_release_free(pf->tx_area);
		nfp_cpp_area_release_free(pf->ctrl_area);
	}

	if (pf->fw_loaded)
		nfp_net_fw_unload(pf);

	if (pf->nfp_dev_cpp)
		nfp_platform_device_unregister(pf->nfp_dev_cpp);

	nfp_cpp_free(pf->cpp);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	pci_set_drvdata(pdev, NULL);
	kfree(pf);
}

static struct pci_driver nfp_net_pci_driver = {
	.name        = nfp_net_driver_name,
	.id_table    = nfp_net_pci_device_ids,
	.probe       = nfp_net_pci_probe,
	.remove      = nfp_net_pci_remove,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	.sriov_configure = nfp_pcie_sriov_configure,
#endif
};

static int __init nfp_net_init(void)
{
	int err;

	mutex_lock(&module_mutex);
	if (find_module("nfp")) {
		pr_err("%s: Cannot be loaded while nfp is loaded\n",
		       nfp_net_driver_name);
		mutex_unlock(&module_mutex);
		return -EBUSY;
	}
	mutex_unlock(&module_mutex);

	pr_info("%s: NFP Network driver, Copyright (C) 2014-2015 Netronome Systems\n",
		nfp_net_driver_name);
	pr_info(NFP_BUILD_DESCRIPTION(nfp));

	err = nfp_cppcore_init();
	if (err < 0)
		goto fail_cppcore_init;

	err = nfp_dev_cpp_init();
	if (err < 0)
		goto fail_dev_cpp_init;

	nfp_net_debugfs_create();

	err = pci_register_driver(&nfp_net_pci_driver);
	if (err < 0)
		goto fail_pci_init;

	return err;

fail_pci_init:
	nfp_net_debugfs_destroy();
	nfp_dev_cpp_exit();
fail_dev_cpp_init:
	nfp_cppcore_exit();
fail_cppcore_init:
	return err;
}

static void __exit nfp_net_exit(void)
{
	pci_unregister_driver(&nfp_net_pci_driver);
	nfp_net_debugfs_destroy();
	nfp_dev_cpp_exit();
	nfp_cppcore_exit();
}

module_init(nfp_net_init);
module_exit(nfp_net_exit);

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NFP network device driver");
MODULE_INFO_NFP();
