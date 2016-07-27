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
 * nfp_mon_err.c
 * NFP Hardware Error Monitor Driver
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_em.h"
#include "nfp3200/nfp_event.h"
#include "nfp3200/nfp_im.h"

#include "nfp_mon_err.h"

#define NFP_ERR_MAX 128

struct nfp_err_cdev {
	struct cdev cdev;
	struct nfp_cpp *cpp;
	struct nfp_cpp_area *em;
	struct timer_list timer;
	unsigned long timer_interval;

	atomic_t error_count;
	u32 last_error;

	wait_queue_head_t event_waiters;

	struct list_head list;
};

struct nfp_err_cdev_handle {
	struct nfp_err_cdev *cdev;
	u32 prev_count;
};

static int nfp_err_major;
static struct class *nfp_err_class;

/* Event filters used for monitoring */
enum {
	F_MULTI_BIT = 31,
	F_SINGLE_BIT = 30,
	F_ME_ATTN = 29,
	F_QDR0 = 28,
	F_QDR1 = 27,

	F_MASK = 0xf8000000,
};

static unsigned int nfp_mon_err_pollinterval = 100;
module_param(nfp_mon_err_pollinterval, uint, 0444);
MODULE_PARM_DESC(nfp_mon_err_pollinterval, "Polling interval for error checking (in ms)");

/*
 *		Device File Interface
 */

static int nfp_err_open(struct inode *inode, struct file *file)
{
	struct nfp_err_cdev *cdev = container_of(
		inode->i_cdev, struct nfp_err_cdev, cdev);
	struct nfp_err_cdev_handle *handle;

	handle = kmalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->cdev = cdev;
	handle->prev_count = 0;
	file->private_data = handle;

	return 0;
}

static int nfp_err_release(struct inode *inode, struct file *file)
{
	struct nfp_err_cdev_handle *handle = file->private_data;

	kfree(handle);
	file->private_data = NULL;
	return 0;
}

static ssize_t nfp_err_read(struct file *file, char __user *buf,
			    size_t count, loff_t *offp)
{
	struct nfp_err_cdev_handle *handle = file->private_data;
	struct nfp_err_cdev *cdev = handle->cdev;
	struct {
		u32 count;
		u32 event;
	} nfperr;
	u32 newcount;

	if (count != sizeof(nfperr))
		return -EINVAL;

	newcount = atomic_read(&cdev->error_count);
	nfperr.event = cdev->last_error;

	while (newcount == handle->prev_count) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(
			    cdev->event_waiters,
			    atomic_read(&cdev->error_count) !=
			    handle->prev_count))
			return -ERESTARTSYS;

		newcount = atomic_read(&cdev->error_count);
		nfperr.event = cdev->last_error;
	}

	nfperr.count = newcount - handle->prev_count;
	handle->prev_count = newcount;
	if (copy_to_user(buf, &nfperr, count))
		return -EFAULT;

	return count;
}

static unsigned int nfp_err_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct nfp_err_cdev_handle *handle = file->private_data;
	struct nfp_err_cdev *cdev = handle->cdev;
	unsigned int mask = 0;

	poll_wait(file, &cdev->event_waiters, wait);
	if (atomic_read(&cdev->error_count) != handle->prev_count)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations nfp_err_fops = {
	.owner = THIS_MODULE,
	.open = nfp_err_open,
	.read = nfp_err_read,
	.poll = nfp_err_poll,
	.release = nfp_err_release,
};

/*
 *		Error Checking Handlers
 */

static inline void nfp_err_schedule(struct nfp_err_cdev *cdev)
{
	mod_timer(&cdev->timer, jiffies + cdev->timer_interval);
}

static inline u32 nfp_err_triggered(struct nfp_err_cdev *cdev)
{
	u32 tmp;

	nfp_cpp_area_readl(cdev->em, NFP_EM_ALL_STATUS, &tmp);
	return tmp & F_MASK;
}

static inline u32 nfp_err_ack(struct nfp_err_cdev *cdev, int fnum)
{
	u32 tmp;

	nfp_cpp_area_readl(cdev->em, NFP_EM_FILTER(fnum) + NFP_EM_FILTER_ACK,
			   &tmp);

	return tmp;
}

static void nfp_err_handle(struct nfp_err_cdev *cdev, u32 trigmask)
{
	u32 status, lastevent = 0;
	int cnt = 0;

	if (trigmask & (1 << F_MULTI_BIT)) {
		status = nfp_err_ack(cdev, F_MULTI_BIT);
		cnt += NFP_EVENT_EVENT_CNT2_of(status);
		lastevent = NFP_EVENT_EVENT_EVENT_of(status);
	}

	if (trigmask & (1 << F_SINGLE_BIT)) {
		status = nfp_err_ack(cdev, F_SINGLE_BIT);
		cnt += NFP_EVENT_EVENT_CNT2_of(status);
		lastevent = NFP_EVENT_EVENT_EVENT_of(status);
	}

	if (trigmask & (1 << F_ME_ATTN)) {
		status = nfp_err_ack(cdev, F_ME_ATTN);
		cnt += NFP_EVENT_EVENT_CNT2_of(status);
		lastevent = NFP_EVENT_EVENT_EVENT_of(status);
	}

	if (trigmask & (1 << F_QDR0)) {
		status = nfp_err_ack(cdev, F_QDR0);
		cnt += NFP_EVENT_EVENT_CNT2_of(status);
		lastevent = NFP_EVENT_EVENT_EVENT_of(status);
	}

	if (trigmask & (1 << F_QDR1)) {
		status = nfp_err_ack(cdev, F_QDR1);
		cnt += NFP_EVENT_EVENT_CNT2_of(status);
		lastevent = NFP_EVENT_EVENT_EVENT_of(status);
	}

	if (cnt == 0)
		/* False alarm.  No event observed. */
		return;

	cdev->last_error = lastevent;
	atomic_add(cnt, &cdev->error_count);
	wake_up_interruptible(&cdev->event_waiters);
}

static void nfp_err_timer(unsigned long data)
{
	struct nfp_err_cdev *cdev = (struct nfp_err_cdev *)data;
	u32 trigmask;

	BUG_ON(!cdev);

	trigmask = nfp_err_triggered(cdev);
	if (trigmask)
		nfp_err_handle(cdev, trigmask);

	nfp_err_schedule(cdev);
}

/*
 *		CPP device specific initialization
 */

static void setup_filter(struct nfp_cpp_area *em, int fnum,
			 u32 evmask, u32 evmatch)
{
	u32 tmp;

	nfp_cpp_area_writel(
		em, NFP_EM_FILTER(fnum) + NFP_EM_FILTER_MASK,
		NFP_EM_FILTER_MASK_TYPE(NFP_EM_FILTER_MASK_TYPE_LASTEV) |
		NFP_EM_FILTER_MASK_EVENT(evmask));
	nfp_cpp_area_writel(em, NFP_EM_FILTER(fnum) + NFP_EM_FILTER_MATCH,
			    NFP_EM_FILTER_MATCH_EVENT(evmatch));
	nfp_cpp_area_readl(em, NFP_EM_FILTER(fnum) + NFP_EM_FILTER_ACK, &tmp);
}

/* SHaC IM Base */
#define NFP_XPB_SHAC_IM		NFP_XPB_DEST(31, 5)

#define NFP_SHAC_IM_SRAM0_ERR	13	/* QDR SRAM0 error */
#define NFP_SHAC_IM_SRAM1_ERR	14	/* QDR SRAM1 error */

#define QDR_PARITY_EVENT_MASK					\
	(NFP_IM_EVENT_CONFIG_MASK(NFP_SHAC_IM_SRAM0_ERR) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_SHAC_IM_SRAM1_ERR))
#define QDR_PARITY_EVENT_CONFIG					\
	(NFP_IM_EVENT_CONFIG(NFP_SHAC_IM_SRAM0_ERR,		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_SHAC_IM_SRAM1_ERR,		\
			     NFP_IM_EVENT_EDGE_POSITIVE))

#define NFP_XPB_MECL_CLS_IM(cl)	        NFP_XPB_DEST((cl), 2)
#define NFP_MECL_IM_MEATTN(me)		(16 + (me))

#define ME_ATTN_EVENT_MASK					\
	(NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(0)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(1)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(2)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(3)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(4)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(5)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(6)) |	\
	 NFP_IM_EVENT_CONFIG_MASK(NFP_MECL_IM_MEATTN(7)))
#define ME_ATTN_EVENT_CONFIG					\
	(NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(0),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(1),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(2),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(3),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(4),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(5),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(6),		\
			     NFP_IM_EVENT_EDGE_POSITIVE) |	\
	 NFP_IM_EVENT_CONFIG(NFP_MECL_IM_MEATTN(7),		\
			     NFP_IM_EVENT_EDGE_POSITIVE))

#define NFP_ARM_EM                     0x300000
#define NFP_PCIE_EM                    0x20000

static int nfp_err_setup_filters(struct nfp_err_cdev *cdev)
{
	u32 cppid = NFP_CPP_ID(0, NFP_CPP_ACTION_RW, 0);
	int iftype = NFP_CPP_INTERFACE_TYPE_of(nfp_cpp_interface(cdev->cpp));
	u32 emoff = (iftype == NFP_CPP_INTERFACE_TYPE_PCI) ?
				NFP_PCIE_EM :
				NFP_ARM_EM;
	u32 evmask, evmatch, evcfg;
	int err, n;

	/* Get a cpp_area mapping the whole event manager */
	cdev->em = nfp_cpp_area_alloc_with_name(
		cdev->cpp, cppid, "nfp_err.em", emoff, NFP_EM_CONFIG + 4);
	if (!cdev->em)
		return -ENOMEM;

	err = nfp_cpp_area_acquire(cdev->em);
	if (err)
		goto err_acquire;

	/* Multi bit ECC errors */
	evmask = NFP_EM_EVENT_MATCH(0, 0, 0xf);
	evmatch =  NFP_EM_EVENT_MATCH(0, 0, NFP_EVENT_TYPE_ECC_MULTI_ERROR);
	setup_filter(cdev->em, F_MULTI_BIT, evmask, evmatch);

	/* Single bit ECC errors */
	evmask = NFP_EM_EVENT_MATCH(0, 0, 0xf);
	evmatch =  NFP_EM_EVENT_MATCH(0, 0, NFP_EVENT_TYPE_ECC_SINGLE_ERROR);
	setup_filter(cdev->em, F_SINGLE_BIT, evmask, evmatch);

	/* ME Attention signals */
	evmask = NFP_EM_EVENT_MATCH(0x8, 0xf00, 0xf);
	evmatch =  NFP_EM_EVENT_MATCH(0, 0x200, NFP_EVENT_TYPE_STATUS);
	setup_filter(cdev->em, F_ME_ATTN, evmask, evmatch);

	/* QDR0 Parity errors */
	evmask = NFP_EM_EVENT_MATCH(0xf, 0xf20, 0xf);
	evmatch =  NFP_EM_EVENT_MATCH(
		NFP_EVENT_SOURCE_SHAC, 0x120, NFP_EVENT_TYPE_STATUS);
	setup_filter(cdev->em, F_QDR0, evmask, evmatch);

	/* QDR1 Parity errors */
	evmask = NFP_EM_EVENT_MATCH(0xf, 0xf40, 0xf);
	evmatch =  NFP_EM_EVENT_MATCH(
		NFP_EVENT_SOURCE_SHAC, 0x140, NFP_EVENT_TYPE_STATUS);
	setup_filter(cdev->em, F_QDR1, evmask, evmatch);

	/* Configure SHaC IM to generate events for QDR parity errors */
	err = nfp_xpb_readl(cdev->cpp, NFP_XPB_SHAC_IM +
		      NFP_IM_EVENT_CONFIG_N(NFP_SHAC_IM_SRAM0_ERR), &evcfg);
	if (err < 0)
		goto err_acquire;
	evcfg = (evcfg & ~QDR_PARITY_EVENT_MASK) | QDR_PARITY_EVENT_CONFIG;
	err = nfp_xpb_writel(cdev->cpp, NFP_XPB_SHAC_IM +
		       NFP_IM_EVENT_CONFIG_N(NFP_SHAC_IM_SRAM0_ERR), evcfg);
	if (err < 0)
		goto err_acquire;

	/* Configure all ME Clusters to generate events for ME Attn */
	for (n = 0; n < 5; n++) {
		err = nfp_xpb_readl(cdev->cpp, NFP_XPB_MECL_CLS_IM(n) +
			NFP_IM_EVENT_CONFIG_N(NFP_MECL_IM_MEATTN(0)), &evcfg);
		if (err < 0)
			goto err_acquire;
		evcfg = (evcfg & ~ME_ATTN_EVENT_MASK) | ME_ATTN_EVENT_CONFIG;
		err = nfp_xpb_writel(
			cdev->cpp, NFP_XPB_MECL_CLS_IM(n) +
			NFP_IM_EVENT_CONFIG_N(NFP_MECL_IM_MEATTN(0)), evcfg);
		if (err < 0)
			goto err_acquire;
	}

	return 0;

err_acquire:
	nfp_cpp_area_free(cdev->em);
	return err;
}

static int nfp_err_release_filters(struct nfp_err_cdev *cdev)
{
	int err;
	u32 evcfg, n;

	/* Disable events for QDR parity errors */
	err = nfp_xpb_readl(cdev->cpp, NFP_XPB_SHAC_IM +
		      NFP_IM_EVENT_CONFIG_N(NFP_SHAC_IM_SRAM0_ERR), &evcfg);
	if (err < 0)
		return err;

	evcfg &= ~QDR_PARITY_EVENT_MASK;
	err = nfp_xpb_writel(cdev->cpp, NFP_XPB_SHAC_IM +
		       NFP_IM_EVENT_CONFIG_N(NFP_SHAC_IM_SRAM0_ERR), evcfg);
	if (err < 0)
		return err;

	/* Disable events for ME Attn signals */
	for (n = 0; n < 5; n++) {
		err = nfp_xpb_readl(cdev->cpp, NFP_XPB_MECL_CLS_IM(n) +
			NFP_IM_EVENT_CONFIG_N(NFP_MECL_IM_MEATTN(0)), &evcfg);
		if (err < 0)
			return err;
		evcfg &= ~ME_ATTN_EVENT_MASK;
		err = nfp_xpb_writel(
			cdev->cpp, NFP_XPB_MECL_CLS_IM(n) +
			NFP_IM_EVENT_CONFIG_N(NFP_MECL_IM_MEATTN(0)), evcfg);
		if (err < 0)
			return err;
	}

	nfp_cpp_area_release_free(cdev->em);

	return 0;
}

/*
 * nfp_err_add_device - callback for CPP devices being added
 * @pdev:	Platform device
 */
static int nfp_err_plat_probe(struct platform_device *pdev)
{
	struct nfp_err_cdev *cdev;
	struct device *dev;
	int id, err;
	struct nfp_cpp *cpp;
	struct nfp_platform_data *pdata;

	pdata = nfp_platform_device_data(pdev);
	BUG_ON(!pdata);

	id = pdata->nfp;

	if (id >= NFP_ERR_MAX) {
		dev_err(&pdev->dev, "NFP Device %d: Exceeds limit of %d NFP Error Monitors\n",
			id, NFP_ERR_MAX);
		return -ENOSPC;
	} else if (id < 0) {
		dev_err(&pdev->dev, "Invalid cpp_id: %d\n", id);
		return -EINVAL;
	}

	cpp = nfp_cpp_from_device_id(id);

	BUG_ON(!cpp);

	cdev = kmalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->cpp = cpp;
	cdev_init(&cdev->cdev, &nfp_err_fops);
	cdev->cdev.owner = THIS_MODULE;
	err = cdev_add(&cdev->cdev, MKDEV(nfp_err_major, id), 1);
	if (err) {
		dev_err(&pdev->dev, "Failed to add nfp_err cdev\n");
		goto err_cdev_add;
	}

	atomic_set(&cdev->error_count, 0);
	cdev->last_error = 0;

	dev = device_create(nfp_err_class, nfp_cpp_device(cpp),
			    MKDEV(nfp_err_major, id),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
			    NULL,
#endif
			    "nfp-err-%d", id);
	if (IS_ERR(dev)) {
		put_device(dev);
		dev_err(&pdev->dev, "Failed to create nfp_err device\n");
		err = PTR_ERR(dev);
		goto err_device_create;
	}

	init_waitqueue_head(&cdev->event_waiters);

	err = nfp_err_setup_filters(cdev);
	if (err) {
		dev_err(dev, "Failed to setup event filters\n");
		goto err_setup_filters;
	}

	/* Setup a timer for polling at regular intervals. */
	init_timer(&cdev->timer);
	cdev->timer.function = nfp_err_timer;
	cdev->timer.data = (unsigned long)cdev;
	cdev->timer_interval = nfp_mon_err_pollinterval * HZ / 1000;
	if (!cdev->timer_interval)
		cdev->timer_interval = 1;
	nfp_err_schedule(cdev);

	platform_set_drvdata(pdev, cdev);

	return 0;

err_setup_filters:
	device_destroy(nfp_err_class, MKDEV(nfp_err_major, id));
err_device_create:
	cdev_del(&cdev->cdev);
err_cdev_add:
	nfp_cpp_free(cpp);
	kfree(cdev);
	return err;
}

/*
 * nfp_err_remove_device - callback for removing CPP devices
 * @pdev:	Platform device
 */
static int nfp_err_plat_remove(struct platform_device *pdev)
{
	struct nfp_err_cdev *cdev = platform_get_drvdata(pdev);
	int id;

	platform_set_drvdata(pdev, NULL);

	del_timer_sync(&cdev->timer);
	nfp_err_release_filters(cdev);

	id = nfp_cpp_device_id(cdev->cpp);
	device_destroy(nfp_err_class, MKDEV(nfp_err_major, id));
	cdev_del(&cdev->cdev);
	nfp_cpp_free(cdev->cpp);
	kfree(cdev);

	return 0;
}

static struct platform_driver nfp_err_plat_driver = {
	.probe = nfp_err_plat_probe,
	.remove = nfp_err_plat_remove,
	.driver = {
		.name = "nfp-mon-err",
	},
};

/**
 * nfp_mon_err_init() - Register the NFP error monitor driver
 *
 * Return: 0, or -ERRNO
 */
int __init nfp_mon_err_init(void)
{
	dev_t dev_id;
	int err;

	nfp_err_class = class_create(THIS_MODULE, "nfp-mon-err");
	if (IS_ERR(nfp_err_class))
		return PTR_ERR(nfp_err_class);

	err = alloc_chrdev_region(&dev_id, 0, NFP_ERR_MAX, "nfp-mon-err");
	if (err)
		goto err_chrdev;
	nfp_err_major = MAJOR(dev_id);

	err = platform_driver_register(&nfp_err_plat_driver);
	if (err)
		goto err_plat;

	pr_info("%s: NFP Error Monitor Driver, Copyright (C) 2011,2015 Netronome Systems\n",
		NFP_MON_ERR_TYPE);

	return 0;

err_plat:
	unregister_chrdev_region(MKDEV(nfp_err_major, 0), NFP_ERR_MAX);
err_chrdev:
	class_destroy(nfp_err_class);
	return err;
}

/**
 * nfp_mon_err_exit() - Unregister the NFP error monitor driver
 */
void nfp_mon_err_exit(void)
{
	platform_driver_unregister(&nfp_err_plat_driver);
	unregister_chrdev_region(MKDEV(nfp_err_major, 0), NFP_ERR_MAX);
	class_destroy(nfp_err_class);
}
