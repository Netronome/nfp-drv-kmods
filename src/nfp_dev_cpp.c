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
 * nfp_dev_cpp.c
 * Implements the NFP CPP API interface (/dev/nfp-cpp-N) in /dev.
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */
#include "nfpcore/kcompat.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/sched.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/signal.h>
#endif
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/wait.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_arm.h"
#include "nfpcore/nfp_cpp.h"

#include "nfp_ioctl.h"
#include "nfp_main.h"

#define NFP_DEV_CPP_MAX		128
#define NFP_DEV_CPP_DEBUG	0

static int nfp_dev_cpp_major;
static struct class *nfp_dev_cpp_class;
struct nfp_dev_cpp_area;

/* CPP accesses crossing this boundary are split up.
 * This MUST be smaller than the smallest possible
 * BAR size. 1M should be sufficient.
 */
#define NFP_CPP_MEMIO_BOUNDARY		(1 << 20)

/* Maximum # of virtual channels */
#define NFP_DEV_CPP_CHANNELS	256

struct nfp_dev_cpp {
	struct cdev cdev;
	struct device *dev;
	struct nfp_cpp *cpp;
	wait_queue_head_t channel_wait;
	unsigned long channel_bitmap[BITS_TO_LONGS(NFP_DEV_CPP_CHANNELS)];
	struct {
		struct list_head list; /* protected by event.lock */
		struct mutex lock;
	} event;
	/* List of allocated areas */
	struct {
		struct list_head list; /* protected by area.lock */
		struct mutex lock;
	} area;
	/* List of area request mappings */
	struct {
		struct list_head list; /* protected by area.lock */
		struct mutex lock;
	} req;
	char firmware[NFP_FIRMWARE_MAX];
};

struct nfp_dev_cpp_channel {
	struct nfp_dev_cpp *cdev;
	u16 interface;
};

/* Attached VMAs */
struct nfp_dev_cpp_vma {
	struct nfp_dev_cpp_area *area;
	struct vm_area_struct *vma;
	struct list_head vma_list;
	struct task_struct *task;
};

/* Acquired areas */
struct nfp_dev_cpp_area {
	struct nfp_dev_cpp *cdev;
	u16 interface;
	struct nfp_cpp_area_request req;
	struct list_head req_list; /* protected by cdev->req.lock */
	struct nfp_cpp_area *area;
	struct list_head area_list; /* protected by cdev->area.lock */
	struct {
		struct list_head list;	/* protected by cdev->area.lock */
	} vma;
};

/* Allocated events */
struct nfp_dev_cpp_event {
	u16 interface;
	struct nfp_cpp_event_request req;
	struct nfp_cpp_event *cpp_event;
	struct task_struct *task;
	int signal;
	struct list_head list; /* protected by event.lock */
};

static void trace_cdev_vma(struct nfp_dev_cpp_vma *cvma, loff_t offset, char c)
{
#if NFP_DEV_CPP_DEBUG
	struct nfp_dev_cpp_area *area = cvma->area;
	struct device *dev = area->cdev->dev;

	dev_err(dev, "%p: %c %p:%p @0x%lx(0x%lx)+0x%08llx %d:0x%llx\n",
		current->mm, c, cvma->vma->vm_mm, cvma,
		(unsigned long)cvma->vma->vm_start,
		(unsigned long)(cvma->vma->vm_end -
				vma->vma->vm_start),
		(unsigned long long)offset,
		NFP_CPP_ID_TARGET_of(area->req.cpp_id),
		(unsigned long long)area->req.cpp_addr);
#endif
}

static void nfp_dev_cpp_event_cb(void *opaque)
{
	struct nfp_dev_cpp_event *ev = opaque;
	siginfo_t info = {
		.si_signo = ev->signal,
		.si_errno = 0,
		.si_code = SI_KERNEL,
	};

	send_sig_info(ev->signal, &info, ev->task);
}

static int nfp_dev_cpp_area_alloc(struct nfp_dev_cpp *cdev, u16 interface,
				  struct nfp_cpp_area_request *area_req)
{
	struct nfp_dev_cpp_area *area;
	struct nfp_cpp_area *cpp_area;
	char buff[64];

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return -ENOMEM;

	/* Can we allocate the area? */
	snprintf(buff, sizeof(buff), "cdev@0x%lx", area_req->offset);
	cpp_area = nfp_cpp_area_alloc_with_name(
		cdev->cpp, area_req->cpp_id, buff,
		area_req->cpp_addr, area_req->size);
	if (!cpp_area) {
		kfree(area);
		return -EINVAL;
	}

	area->cdev = cdev;
	area->interface = interface;
	area->area = cpp_area;
	area->req = *area_req;
	list_add_tail(&area->req_list, &cdev->req.list);

	INIT_LIST_HEAD(&area->vma.list);

	/* Poison the area_list entry */
	INIT_LIST_HEAD(&area->area_list);
	list_del(&area->area_list);

	return 0;
}

static void nfp_dev_cpp_area_free(struct nfp_dev_cpp_area *area)
{
	BUG_ON(!list_empty(&area->vma.list));
	list_del(&area->req_list);
	nfp_cpp_area_free(area->area);
	kfree(area);
}

/*
 * nfp_dev_cpp_vma_acquire - Acquire the NFP CPP area of a cdev vma
 * @cvma:	vma to acquire
 *
 * Acquire CPP area in the cdev vma
 *
 * Must be called with cdev->area.lock
 */
static int nfp_dev_cpp_vma_acquire(struct nfp_dev_cpp_vma *cvma)
{
	struct nfp_dev_cpp_area *area = cvma->area;
	struct nfp_dev_cpp *cdev = area->cdev;
	int err;

	if (!list_empty(&cvma->vma_list))
		return 0;

	if (list_empty(&area->vma.list)) {
		err = nfp_cpp_area_acquire_nonblocking(area->area);
		if (err < 0)
			return err;

		/* Add to the active mapping list */
		list_add_tail(&area->area_list, &cdev->area.list);
	}

	list_add_tail(&cvma->vma_list, &area->vma.list);

	return 0;
}

/*
 * nfp_dev_cpp_vma_release - Release the NFP CPP area of a cdev vma
 * @cvma:	vma to release
 *
 * Unmap and release vma
 * Must be called with cdev->area.lock
 */
static void nfp_dev_cpp_vma_release(struct nfp_dev_cpp_vma *cvma)
{
	struct nfp_dev_cpp_area *area = cvma->area;

	if (list_empty(&cvma->vma_list))
		return;

	/* Remove VMA from the area's VMA list */
	list_del(&cvma->vma_list);
	/* Mark as empty */
	INIT_LIST_HEAD(&cvma->vma_list);

	/* Was this the last vma? Then release the area,
	 * and remove from cdev allocated area list.
	 */
	if (list_empty(&area->vma.list)) {
		/* Remove from the active mapping list */
		list_del(&area->area_list);

		nfp_cpp_area_release(area->area);
	}
}

/* Evict the first NFP CPP Area held by a VMA, that
 * does *not* contain this_vma
 */
static int nfp_dev_cpp_vma_evict(struct nfp_dev_cpp *cdev,
				 const struct nfp_dev_cpp_vma *this_cvma)
{
	struct nfp_dev_cpp_area *a, *tmp;
	int err = -EAGAIN;

	/* Ok, can't get an area - let's evict someone */
	list_for_each_entry_safe(a, tmp, &cdev->area.list, area_list) {
		if (list_empty(&a->vma.list))
			continue;
		if (!this_cvma || this_cvma->area != a) {
			while (!list_empty(&a->vma.list)) {
				struct nfp_dev_cpp_vma *ctmp;

				ctmp = list_first_entry(&a->vma.list,
							struct nfp_dev_cpp_vma,
							vma_list);
				BUG_ON(ctmp == this_cvma);
				trace_cdev_vma(ctmp, 0,
					       !this_cvma ? 'E' : 'e');
				/* Purge from the mapping */
				zap_vma_ptes(ctmp->vma, ctmp->vma->vm_start,
					     ctmp->vma->vm_end -
					     ctmp->vma->vm_start);
				nfp_dev_cpp_vma_release(ctmp);
			}
			err = 0;
			if (this_cvma)
				break;
		}
	}

	return err;
}

static int nfp_dev_cpp_open(struct inode *inode, struct file *file)
{
	struct nfp_dev_cpp *cdev = container_of(
		inode->i_cdev, struct nfp_dev_cpp, cdev);
	struct nfp_dev_cpp_channel *chan;
	u16 interface;
	int channel;

	interface = nfp_cpp_interface(cdev->cpp);

	if (NFP_CPP_INTERFACE_CHANNEL_of(interface) !=
	    NFP_CPP_INTERFACE_CHANNEL_PEROPENER) {
		/* Single-channel interface */
		channel = NFP_CPP_INTERFACE_CHANNEL_of(interface);

		if (channel >= NFP_DEV_CPP_CHANNELS)
			return -ENODEV;

		/* Already in use! */
		if (test_and_set_bit(channel, cdev->channel_bitmap))
			return -EBUSY;

	} else {
		/* Find a virtual channel we can use */
		for (channel = 0; channel < NFP_DEV_CPP_CHANNELS; channel++) {
			if (test_and_set_bit(channel,
					     cdev->channel_bitmap) == 0)
				break;
		}

		if (channel == NFP_DEV_CPP_CHANNELS)
			return -EBUSY;

		interface = NFP_CPP_INTERFACE(
				NFP_CPP_INTERFACE_TYPE_of(interface),
				NFP_CPP_INTERFACE_UNIT_of(interface),
				channel);
	}

	chan = kmalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan) {
		clear_bit(channel, cdev->channel_bitmap);
		wake_up(&cdev->channel_wait);
		return -ENOMEM;
	}

	chan->interface = interface;
	chan->cdev = cdev;

	file->private_data = chan;

	return 0;
}

static int nfp_dev_cpp_release(struct inode *inode, struct file *file)
{
	struct nfp_dev_cpp_channel *chan = file->private_data;
	struct nfp_dev_cpp *cdev = chan->cdev;
	u16 interface = chan->interface;
	struct nfp_dev_cpp_area *req, *rtmp;
	struct nfp_dev_cpp_event *ev, *etmp;

	mutex_lock(&cdev->event.lock);
	list_for_each_entry_safe(ev, etmp, &cdev->event.list, list) {
		if (ev->interface == interface) {
			nfp_cpp_event_free(ev->cpp_event);
			list_del(&ev->list);
			kfree(ev);
		}
	}
	mutex_unlock(&cdev->event.lock);

	mutex_lock(&cdev->req.lock);
	list_for_each_entry_safe(req, rtmp, &cdev->req.list, req_list) {
		if (req->interface == interface) {
			BUG_ON(!list_empty(&req->vma.list));
			nfp_dev_cpp_area_free(req);
		}
	}
	mutex_unlock(&cdev->req.lock);

	clear_bit(NFP_CPP_INTERFACE_CHANNEL_of(interface),
		  cdev->channel_bitmap);

	kfree(file->private_data);
	file->private_data = NULL;

	wake_up(&cdev->channel_wait);
	return 0;
}

static ssize_t nfp_dev_cpp_op(struct file *file,
			      u32 cpp_id, char __user *buff,
			      size_t count, loff_t *offp, int write)
{
	struct nfp_dev_cpp_channel *chan = file->private_data;
	struct nfp_dev_cpp *cdev = chan->cdev;
	struct nfp_cpp *cpp = cdev->cpp;
	struct nfp_cpp_area *area;
	u32 __user *udata;
	u32 tmpbuf[16];
	u32 pos, len;
	int err = 0;
	size_t curlen = count, totlen = 0;

	if (((*offp + count - 1) & ~(NFP_CPP_MEMIO_BOUNDARY - 1)) !=
	    (*offp & ~(NFP_CPP_MEMIO_BOUNDARY - 1))) {
		curlen = NFP_CPP_MEMIO_BOUNDARY -
			(*offp & (NFP_CPP_MEMIO_BOUNDARY - 1));
	}

	while (count > 0) {
		area = nfp_cpp_area_alloc_with_name(
			cpp, cpp_id, "nfp.cdev", *offp, curlen);
		if (!area)
			return -EIO;

		err = nfp_cpp_area_acquire_nonblocking(area);
		if (err == -EAGAIN) {
			/* Try evicting all VMAs... */
			mutex_lock(&cdev->area.lock);
			nfp_dev_cpp_vma_evict(cdev, NULL);
			mutex_unlock(&cdev->area.lock);
			/* Wait for an available area.. */
			err = nfp_cpp_area_acquire(area);
		}
		if (err < 0) {
			nfp_cpp_area_free(area);
			return err;
		}

		for (pos = 0; pos < curlen; pos += len) {
			len = curlen - pos;
			if (len > sizeof(tmpbuf))
				len = sizeof(tmpbuf);
			udata = (u32 __user *)(buff + pos);

			if (write) {
				if (copy_from_user(tmpbuf, udata, len)) {
					err = -EFAULT;
					break;
				}
				err = nfp_cpp_area_write(
					area, pos, tmpbuf, len);
				if (err < 0)
					break;
			} else {
				err = nfp_cpp_area_read(area, pos, tmpbuf, len);
				if (err < 0)
					break;
				if (copy_to_user(udata, tmpbuf, len)) {
					err = -EFAULT;
					break;
				}
			}
		}

		*offp += pos;
		totlen += pos;
		buff += pos;
		nfp_cpp_area_release(area);
		nfp_cpp_area_free(area);

		if (err < 0)
			return err;

		count -= pos;
		curlen = (count > NFP_CPP_MEMIO_BOUNDARY) ?
			NFP_CPP_MEMIO_BOUNDARY : count;
	}

	return totlen;
}

static ssize_t nfp_dev_cpp_read(struct file *file, char __user *buff,
				size_t count, loff_t *offp)
{
	u32 cpp_id = (*offp >> 40) << 8;
	loff_t offset = *offp & ((1ull << 40) - 1);
	int r;

	r = nfp_dev_cpp_op(file, cpp_id, buff, count, &offset, 0);
	*offp = ((loff_t)(cpp_id >> 8) << 40) | offset;
	return r;
}

static ssize_t nfp_dev_cpp_write(struct file *file, const char __user *buff,
				 size_t count, loff_t *offp)
{
	u32 cpp_id = (*offp >> 40) << 8;
	loff_t offset = *offp & ((1ull << 40) - 1);
	int r;

	r = nfp_dev_cpp_op(file, cpp_id,
			   (char __user *)buff, count, &offset, 1);
	*offp = ((loff_t)(cpp_id >> 8) << 40) | offset;
	return r;
}

static int nfp_dev_cpp_range_check(struct list_head *req_list,
				   struct nfp_cpp_area_request *area_req)
{
	struct nfp_dev_cpp_area *area;

	/* Look for colliding offsets */
	list_for_each_entry(area, req_list, req_list) {
		if (area_req->offset < (area->req.offset + area->req.size) &&
		    (area_req->offset + area_req->size) > area->req.offset) {
			return -ETXTBSY;
		}
	}

	return 0;
}

/* Hacky thing to handle the old explicit ABI, which worked in
 * an ARM CSR centric manner
 */
static int explicit_csr_to_cmd(struct nfp_cpp_explicit *expl,
			       const unsigned long *csr)
{
	enum nfp_cpp_explicit_signal_mode siga_mode, sigb_mode;
	int signal_master, signal_ref;
	int data_master, data_ref;
	u32 expl1, expl2, post;
	u32 cpp_id;
	int err;

	post = csr[NFP_IOCTL_CPP_EXPL_POST];
	expl1 = csr[NFP_IOCTL_CPP_EXPL1_BAR];
	expl2 = csr[NFP_IOCTL_CPP_EXPL2_BAR];

	cpp_id = NFP_CPP_ID(NFP_ARM_GCSR_EXPL2_BAR_TGT_of(expl2),
			    NFP_ARM_GCSR_EXPL2_BAR_ACT_of(expl2),
			NFP_ARM_GCSR_EXPL2_BAR_TOK_of(expl2));

	err = nfp_cpp_explicit_set_target(expl, cpp_id,
					  NFP_ARM_GCSR_EXPL2_BAR_LEN_of(expl2),
			NFP_ARM_GCSR_EXPL2_BAR_BYTE_MASK_of(expl2));
	if (err < 0)
		return err;

	data_master = NFP_ARM_GCSR_EXPL1_BAR_DATA_MASTER_of(expl1);
	data_ref = NFP_ARM_GCSR_EXPL1_BAR_DATA_REF_of(expl1);

	if (data_master || data_ref) {
		err = nfp_cpp_explicit_set_data(expl, data_master, data_ref);
		if (err < 0)
			return err;
	}

	signal_master = NFP_ARM_GCSR_EXPL2_BAR_SIGNAL_MASTER_of(expl2);
	signal_ref = NFP_ARM_GCSR_EXPL1_BAR_SIGNAL_REF_of(expl1);

	if (signal_master || signal_ref) {
		err = nfp_cpp_explicit_set_signal(expl, signal_master,
						  signal_ref);
		if (err < 0)
			return err;
	}

	siga_mode = NFP_SIGNAL_NONE;
	if (post & NFP_ARM_GCSR_EXPL_POST_SIG_A_VALID) {
		switch (post & (NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS | (1 << 2))) {
		case NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PUSH:
		    siga_mode = NFP_SIGNAL_PUSH_OPTIONAL;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PUSH | (1 << 2):
		    siga_mode = NFP_SIGNAL_PUSH;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PULL:
		    siga_mode = NFP_SIGNAL_PULL_OPTIONAL;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PULL | (1 << 2):
		    siga_mode = NFP_SIGNAL_PULL;
		    break;
		}
	}

	sigb_mode = NFP_SIGNAL_NONE;
	if (post & NFP_ARM_GCSR_EXPL_POST_SIG_B_VALID) {
		switch (post & (NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS | (1 << 3))) {
		case NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PUSH:
		    sigb_mode = NFP_SIGNAL_PUSH_OPTIONAL;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PUSH | (1 << 3):
		    sigb_mode = NFP_SIGNAL_PUSH;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PULL:
		    sigb_mode = NFP_SIGNAL_PULL_OPTIONAL;
		    break;
		case NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PULL | (1 << 3):
		    sigb_mode = NFP_SIGNAL_PULL;
		    break;
		}
	}

	err = nfp_cpp_explicit_set_posted(expl,
					  (expl1 &
					   NFP_ARM_GCSR_EXPL1_BAR_POSTED) ?
									1 : 0,
			NFP_ARM_GCSR_EXPL_POST_SIG_A_of(post), siga_mode,
			NFP_ARM_GCSR_EXPL_POST_SIG_B_of(post), sigb_mode);
	if (err < 0)
		return err;

	return 0;
}

static int do_cpp_event_acquire(struct nfp_dev_cpp *cdev, u16 interface,
				struct nfp_cpp_event_request *event_req)
{
	struct nfp_dev_cpp_event *event;
	int err;

	if (event_req->signal <= 0 || event_req->signal >= _NSIG)
		return -EINVAL;

	event = kmalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->signal = event_req->signal;
	event->cpp_event = nfp_cpp_event_alloc(cdev->cpp,
			event_req->match, event_req->mask, event_req->type);
	if (IS_ERR(event->cpp_event)) {
		err = PTR_ERR(event->cpp_event);
		kfree(event);
		return err;
	}

	err = nfp_cpp_event_as_callback(event->cpp_event,
					nfp_dev_cpp_event_cb, event);
	if (err < 0) {
		nfp_cpp_event_free(event->cpp_event);
		kfree(event);
		return err;
	}

	event->interface = interface;
	event->task = current;
	event->req = *event_req;
	mutex_lock(&cdev->event.lock);
	list_add_tail(&event->list, &cdev->event.list);
	mutex_unlock(&cdev->event.lock);

	return 0;
}

static int do_cpp_event_release(struct nfp_dev_cpp *cdev, u16 interface,
				struct nfp_cpp_event_request *event_req)
{
	struct nfp_dev_cpp_event *event, *etmp;

	mutex_lock(&cdev->event.lock);
	list_for_each_entry_safe(event, etmp,
				 &cdev->event.list, list) {
		/* Ignore entries we don't own */
		if (event->interface != interface)
			continue;

		if (memcmp(&event_req, &event->req,
			   sizeof(event_req)) == 0) {
			nfp_cpp_event_free(event->cpp_event);
			list_del(&event->list);
			kfree(event);
		}
	}
	mutex_unlock(&cdev->event.lock);

	return 0;
}

static int do_cpp_expl_request(struct nfp_dev_cpp_channel *chan,
			       struct nfp_cpp_explicit_request *explicit_req)
{
	int err, ret = -EINVAL;
	struct nfp_cpp_explicit *expl;

	expl = nfp_cpp_explicit_acquire(chan->cdev->cpp);
	if (!expl)
		return -ENOMEM;

	err = explicit_csr_to_cmd(expl, &explicit_req->csr[0]);
	if (err < 0)
		goto exit;

	err = nfp_cpp_explicit_put(expl, &explicit_req->data[0],
				   explicit_req->in);
	if (err < 0)
		goto exit;

	err = nfp_cpp_explicit_do(expl, explicit_req->address);
	ret = err;
	if (err < 0)
		goto exit;

	err = nfp_cpp_explicit_get(expl, &explicit_req->data[0],
				   explicit_req->out);
	if (err < 0)
		goto exit;

	err = ret;

exit:
	nfp_cpp_explicit_release(expl);

	return err;
}

static void do_cpp_identification(struct nfp_dev_cpp_channel *chan,
				  struct nfp_cpp_identification *ident)
{
	int total;

	total = offsetof(struct nfp_cpp_identification, size)
		+ sizeof(ident->size);

	if (ident->size >= offsetof(struct nfp_cpp_identification, model)
			+ sizeof(ident->model)) {
		ident->model = nfp_cpp_model(chan->cdev->cpp);
		total = offsetof(struct nfp_cpp_identification, model)
			+ sizeof(ident->model);
	}
	if (ident->size >= offsetof(struct nfp_cpp_identification, interface)
			+ sizeof(ident->interface)) {
		ident->interface = chan->interface;
		total = offsetof(struct nfp_cpp_identification, interface)
			+ sizeof(ident->interface);
	}
	if (ident->size >= offsetof(struct nfp_cpp_identification, serial_hi)
			+ sizeof(ident->serial_hi)) {
		const u8 *serial;

		nfp_cpp_serial(chan->cdev->cpp, &serial);
		ident->serial_hi = (serial[0] <<  8) |
				   (serial[1] <<  0);
		ident->serial_lo = (serial[2] << 24) |
				   (serial[3] << 16) |
				   (serial[4] <<  8) |
				   (serial[5] <<  0);
		total = offsetof(struct nfp_cpp_identification, serial_hi)
			+ sizeof(ident->serial_hi);
	}

	/* Modify size to our actual size */
	ident->size = total;
}

static int do_fw_load(struct nfp_dev_cpp *cdev)
{
	const struct firmware *fw;
	struct nfp_nsp *nsp;
	int err;

	err = request_firmware(&fw, cdev->firmware, cdev->dev);
	if (err < 0)
		return err;

	nsp = nfp_nsp_open(cdev->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		goto exit_release_fw;
	}

	err = nfp_nsp_load_fw(nsp, fw);
	nfp_nsp_close(nsp);

exit_release_fw:
	release_firmware(fw);

	return err;
}

static int do_cpp_area_request(struct nfp_dev_cpp *cdev, u16 interface,
			       struct nfp_cpp_area_request *area_req)
{
	int err = 0;

	if ((area_req->size & ~PAGE_MASK) != 0)
		return -EINVAL;

	if (area_req->offset != ~0 && (area_req->offset & ~PAGE_MASK) != 0)
		return -EINVAL;

	mutex_lock(&cdev->req.lock);

	if (area_req->offset == ~0) {
		/* Look for the first available slot */
		area_req->offset = 0;
		err = nfp_dev_cpp_range_check(&cdev->req.list, area_req);
		if (err < 0) {
			struct nfp_dev_cpp_area *area;

			list_for_each_entry(area, &cdev->req.list, req_list) {
				area_req->offset = area->req.offset +
						   area->req.size;
				err = nfp_dev_cpp_range_check(&cdev->req.list,
							      area_req);
				if (err == 0)
					break;
			}
		}
	}

	if (err >= 0) {
		/* Look for colliding offsets */
		err = nfp_dev_cpp_range_check(&cdev->req.list, area_req);
		if (err >= 0)
			err = nfp_dev_cpp_area_alloc(cdev, interface, area_req);
	}

	mutex_unlock(&cdev->req.lock);

	return err;
}

static int do_cpp_area_release(struct nfp_dev_cpp *cdev, u16 interface,
			       struct nfp_cpp_area_request *area_req)
{
	struct nfp_dev_cpp_area *area, *atmp;
	int err = -ENOENT;

	mutex_lock(&cdev->req.lock);

	list_for_each_entry_safe(area, atmp,
				 &cdev->req.list, req_list) {
		if (area->req.offset == area_req->offset) {
			mutex_lock(&cdev->area.lock);
			err = list_empty(&area->vma.list) ? 0 : -EBUSY;
			mutex_unlock(&cdev->area.lock);
			if (err == 0)
				nfp_dev_cpp_area_free(area);
			break;
		}
	}

	mutex_unlock(&cdev->req.lock);

	return err;
}

/* Manage the CPP Areas
 */
#ifdef HAVE_UNLOCKED_IOCTL
static long nfp_dev_cpp_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg)
#else
static int nfp_dev_cpp_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
#endif
{
	struct nfp_dev_cpp_channel *chan = filp->private_data;
	struct nfp_dev_cpp *cdev = chan->cdev;
	struct nfp_cpp_area_request area_req;
	struct nfp_cpp_event_request event_req;
	struct nfp_cpp_explicit_request explicit_req;
	struct nfp_cpp_identification ident;
	void __user *data = (void __user *)arg;
	int err;

	switch (cmd) {
	case NFP_IOCTL_CPP_IDENTIFICATION:
		/* Get the size parameter */
		if (!arg)
			return sizeof(ident);

		if (copy_from_user(&ident.size, data, sizeof(ident.size)))
			return -EFAULT;

		do_cpp_identification(chan, &ident);

		/* Write back the data */
		if (copy_to_user(data, &ident, ident.size))
			return -EFAULT;

		return sizeof(ident);

	case NFP_IOCTL_FIRMWARE_LOAD:
		if (copy_from_user(cdev->firmware, data,
				   sizeof(cdev->firmware)))
			return -EFAULT;

		cdev->firmware[sizeof(cdev->firmware) - 1] = 0;

		return do_fw_load(cdev);

	case NFP_IOCTL_FIRMWARE_LAST:
		if (copy_to_user(data, cdev->firmware, sizeof(cdev->firmware)))
			return -EFAULT;

		return 0;

	case NFP_IOCTL_CPP_AREA_REQUEST:
		if (copy_from_user(&area_req, data, sizeof(area_req)))
			return -EFAULT;

		err = do_cpp_area_request(chan->cdev, chan->interface,
					  &area_req);
		if (err < 0)
			return err;

		/* Write back the found slot */
		if (copy_to_user(data, &area_req, sizeof(area_req)))
			return -EFAULT;

		return 0;

	case NFP_IOCTL_CPP_AREA_RELEASE:
	case NFP_IOCTL_CPP_AREA_RELEASE_OBSOLETE:
		if (cmd == NFP_IOCTL_CPP_AREA_RELEASE) {
			err = copy_from_user(&area_req, data, sizeof(area_req));
		} else {
			/* OBSOLETE version */
			err = copy_from_user(&area_req.offset, data,
					     sizeof(area_req.offset));
		}
		if (err)
			return -EFAULT;

		return do_cpp_area_release(cdev, chan->interface, &area_req);

	case NFP_IOCTL_CPP_EXPL_REQUEST:
		if (copy_from_user(&explicit_req, data, sizeof(explicit_req)))
			return -EFAULT;

		err = do_cpp_expl_request(chan, &explicit_req);
		if (err < 0)
			return err;

		if (copy_to_user(data, &explicit_req, sizeof(explicit_req)))
			return -EFAULT;

		return 0;

	case NFP_IOCTL_CPP_EVENT_ACQUIRE:
		if (copy_from_user(&event_req, data, sizeof(event_req)))
			return -EFAULT;

		return do_cpp_event_acquire(chan->cdev, chan->interface,
					    &event_req);

	case NFP_IOCTL_CPP_EVENT_RELEASE:
		if (copy_from_user(&event_req, data, sizeof(event_req)))
			return -EFAULT;

		return do_cpp_event_release(chan->cdev, chan->interface,
					    &event_req);
	default:
		return -EINVAL;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static int nfp_cpp_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#else
static int nfp_cpp_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#endif
	struct nfp_dev_cpp_vma *cvma = vma->vm_private_data;
	struct nfp_dev_cpp_area *area;
	struct nfp_dev_cpp *cdev;
	off_t offset;
	int err;

	if (!cvma)
		return VM_FAULT_SIGBUS;

	area = cvma->area;
	cdev = area->cdev;

	/* We don't use vmf->pgoff since that has the fake offset */
	offset = compat_vmf_get_addr(vmf) - vma->vm_start;
	if (offset >= area->req.size)
		return VM_FAULT_SIGBUS;

	mutex_lock(&cdev->area.lock);
	if (!list_empty(&cvma->vma_list)) {
		/* If cvma->vma_list is part of a list, then
		 * we have already been attached to an acquired area
		 */
		err = 1;
	} else {
		err = nfp_dev_cpp_vma_acquire(cvma);
		if (err < 0) {
			err = nfp_dev_cpp_vma_evict(cdev, cvma);
			if (err == 0)
				err = nfp_dev_cpp_vma_acquire(cvma);
		}
	}
	mutex_unlock(&cdev->area.lock);

	if (err < 0)
		/* We'll try again later... */
		schedule();

	trace_cdev_vma(cvma, offset,
		       (err < 0) ?
			     '!' : ((err == 0) ?
					   '+' : '='));

	if (err >= 0) {
		phys_addr_t io;
		/*
		 * We know we have an acquired area now: Get its
		 * physical offset and map it.
		 */
		io = nfp_cpp_area_phys(area->area) + offset;
		BUG_ON(io == 0);
		vm_insert_pfn(vma, compat_vmf_get_addr(vmf), io >> PAGE_SHIFT);
		err = 0;
	}

	switch (err) {
	case 0:
	case -ERESTARTSYS:
	case -EAGAIN:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}

static void nfp_cpp_mmap_open(struct vm_area_struct *vma)
{
	struct nfp_dev_cpp_vma *cvma;
	struct nfp_dev_cpp_area *area =
		((struct nfp_dev_cpp_vma *)vma->vm_private_data)->area;

	cvma = kmalloc(sizeof(*cvma), GFP_KERNEL);
	if (cvma) {
		cvma->area = area;
		cvma->vma = vma;
		INIT_LIST_HEAD(&cvma->vma_list);

		/* Make sure we don't have a stale mapping */
		zap_vma_ptes(cvma->vma, cvma->vma->vm_start,
			     cvma->vma->vm_end - cvma->vma->vm_start);
		trace_cdev_vma(cvma, 0, 'o');
	}

	vma->vm_private_data = cvma;
}

static void nfp_cpp_mmap_close(struct vm_area_struct *vma)
{
	struct nfp_dev_cpp_vma *cvma = vma->vm_private_data;

	if (cvma) {
		struct nfp_dev_cpp *cdev = cvma->area->cdev;

		mutex_lock(&cdev->area.lock);
		trace_cdev_vma(cvma, 0, 'c');
		nfp_dev_cpp_vma_release(cvma);
		mutex_unlock(&cdev->area.lock);

		kfree(cvma);
	}
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
static struct vm_operations_struct nfp_cpp_mmap_ops = {
#else
static const struct vm_operations_struct nfp_cpp_mmap_ops = {
#endif
	.fault = nfp_cpp_mmap_fault,
	.open = nfp_cpp_mmap_open,
	.close = nfp_cpp_mmap_close,
};

static int nfp_dev_cpp_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct nfp_dev_cpp_channel *chan = filp->private_data;
	struct nfp_dev_cpp *cdev = chan->cdev;
	struct nfp_dev_cpp_vma *cvma;
	struct nfp_dev_cpp_area *area;
	unsigned long offset;
	unsigned long size;
	int err;

	offset = vma->vm_pgoff << PAGE_SHIFT;
	size = vma->vm_end - vma->vm_start;

	/* Do we have an area that matches this? */
	err = -EINVAL;
	mutex_lock(&cdev->req.lock);
	list_for_each_entry(area, &cdev->req.list, req_list) {
		if ((area->req.offset <= offset) &&
		    (area->req.offset + area->req.size) >= (offset + size)) {
			err = 0;
			break;
		}
	}
	mutex_unlock(&cdev->req.lock);
	if (err < 0)
		return err;

	cvma = kmalloc(sizeof(*cvma), GFP_KERNEL);
	if (!cvma)
		return -ENOMEM;

	cvma->area = area;
	cvma->vma = vma;
	INIT_LIST_HEAD(&cvma->vma_list);

#ifndef VM_RESERVED
 #define VM_RESERVED 0
#endif
#ifndef VM_DONTDUMP
 #define VM_DONTDUMP 0
#endif
	/* 'area' is now an area we've previously allocated */
	vma->vm_flags |= VM_RESERVED | VM_IO | VM_PFNMAP
		       | VM_DONTEXPAND | VM_DONTDUMP | VM_SHARED;
	vma->vm_ops = &nfp_cpp_mmap_ops;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_private_data = cvma;

	trace_cdev_vma(cvma, 0, 'm');

	return 0;
}

static const struct file_operations nfp_cpp_fops = {
	.owner   = THIS_MODULE,
	.open    = nfp_dev_cpp_open,
	.release = nfp_dev_cpp_release,
	.mmap    = nfp_dev_cpp_mmap,
	.read    = nfp_dev_cpp_read,
	.write   = nfp_dev_cpp_write,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl   = nfp_dev_cpp_ioctl,
#else
	.ioctl   = nfp_dev_cpp_ioctl,
#endif
};

static ssize_t show_firmware(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct nfp_dev_cpp *cdev = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", &cdev->firmware[0]);
}

static ssize_t store_firmware(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct nfp_dev_cpp *cdev = dev_get_drvdata(dev);
	const char *cp;
	int err;

	cp = strnchr(buf, count, '\n');
	if (!cp)
		cp = buf + count;

	if ((cp - buf) > (sizeof(cdev->firmware) - 1))
		return -EINVAL;

	memcpy(&cdev->firmware[0], buf, (cp - buf));
	cdev->firmware[cp - buf] = 0;

	err = do_fw_load(cdev);

	return (err < 0) ? err : count;
}

static DEVICE_ATTR(firmware, S_IRUGO | S_IWUSR, show_firmware, store_firmware);

/** Register a cdev interface for a NFP CPP interface.
 */
static int nfp_dev_cpp_probe(struct platform_device *pdev)
{
	struct nfp_platform_data *pdata;
	struct nfp_dev_cpp *cdev;
	struct nfp_cpp *cpp;
	int err = -EINVAL;
	int id;

	pdata = nfp_platform_device_data(pdev);
	BUG_ON(!pdata);

	id = pdata->nfp;

	cpp = nfp_cpp_from_device_id(id);

	BUG_ON(!cpp);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev) {
		nfp_cpp_free(cpp);
		return -ENOMEM;
	}

	cdev->cpp = cpp;
	INIT_LIST_HEAD(&cdev->event.list);
	mutex_init(&cdev->event.lock);

	INIT_LIST_HEAD(&cdev->area.list);
	mutex_init(&cdev->area.lock);

	INIT_LIST_HEAD(&cdev->req.list);
	mutex_init(&cdev->req.lock);

	init_waitqueue_head(&cdev->channel_wait);

	cdev_init(&cdev->cdev, &nfp_cpp_fops);
	cdev->cdev.owner = THIS_MODULE;
	err = cdev_add(&cdev->cdev, MKDEV(nfp_dev_cpp_major, id), 1);
	if (err)
		goto err_cpp;

	cdev->dev = device_create(nfp_dev_cpp_class, nfp_cpp_device(cpp),
			MKDEV(nfp_dev_cpp_major, id),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
			    NULL,
#endif
			    "nfp-cpp-%d", id);
	if (IS_ERR(cdev->dev)) {
		err = PTR_ERR(cdev->dev);
		goto err_cpp_dev;
	}

	dev_set_drvdata(cdev->dev, cdev);
	err = device_create_file(cdev->dev, &dev_attr_firmware);
	if (err < 0)
		goto err_attrib;

	platform_set_drvdata(pdev, cdev);

	return 0;

err_attrib:
	device_destroy(nfp_dev_cpp_class, MKDEV(nfp_dev_cpp_major, id));
err_cpp_dev:
	cdev_del(&cdev->cdev);
err_cpp:
	nfp_cpp_free(cdev->cpp);
	kfree(cdev);
	return err;
}

/*
 * Remove a NFP CPP /dev entry
 */
static int nfp_dev_cpp_remove(struct platform_device *pdev)
{
	struct nfp_dev_cpp *cdev = platform_get_drvdata(pdev);
	unsigned int minor = MINOR(cdev->cdev.dev);

	if (!bitmap_empty(cdev->channel_bitmap, NFP_DEV_CPP_CHANNELS)) {
		dev_err(&pdev->dev, "Unexpected device removal while busy - waiting...\n");
		wait_event(cdev->channel_wait,
			   bitmap_empty(cdev->channel_bitmap,
					NFP_DEV_CPP_CHANNELS));
	}

	BUG_ON(!list_empty(&cdev->event.list));
	BUG_ON(!list_empty(&cdev->area.list));
	BUG_ON(!list_empty(&cdev->req.list));

	platform_set_drvdata(pdev, NULL);

	device_remove_file(cdev->dev, &dev_attr_firmware);

	device_destroy(nfp_dev_cpp_class, MKDEV(nfp_dev_cpp_major, minor));

	cdev_del(&cdev->cdev);

	nfp_cpp_free(cdev->cpp);
	kfree(cdev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver nfp_dev_cpp_driver = {
	.probe = nfp_dev_cpp_probe,
	.remove = nfp_dev_cpp_remove,
	.driver = {
		.name = "nfp-dev-cpp",
	},
};

/**
 * nfp_dev_cpp_init() - Register the NFP CPP /dev driver
 *
 * Return: 0, or -ERRNO
 */
int nfp_dev_cpp_init(void)
{
	dev_t dev_id;
	int err = -EINVAL;

	nfp_dev_cpp_class = class_create(THIS_MODULE, "nfp-dev-cpp");
	if (IS_ERR(nfp_dev_cpp_class))
		return PTR_ERR(nfp_dev_cpp_class);

	err = alloc_chrdev_region(&dev_id, 0, NFP_DEV_CPP_MAX, "nfp-dev-cpp");
	if (err)
		goto err_cpp;
	nfp_dev_cpp_major = MAJOR(dev_id);

	err = platform_driver_register(&nfp_dev_cpp_driver);
	if (err)
		goto err_plat;

	return 0;

err_plat:
	unregister_chrdev_region(MKDEV(nfp_dev_cpp_major, 0), NFP_DEV_CPP_MAX);
err_cpp:
	class_destroy(nfp_dev_cpp_class);
	return err;
}

/**
 * nfp_dev_cpp_exit() - Unregister the NFP CPP /dev driver
 */
void nfp_dev_cpp_exit(void)
{
	platform_driver_unregister(&nfp_dev_cpp_driver);
	class_destroy(nfp_dev_cpp_class);
	unregister_chrdev_region(MKDEV(nfp_dev_cpp_major, 0), NFP_DEV_CPP_MAX);
}
