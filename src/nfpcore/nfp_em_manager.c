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
 * nfp_em_manager.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include "nfp.h"
#include "nfp_cpp.h"

#define NFP_EM_FILTERS                 32

#define NFP_EM_FILTER(_f)              (0x0 + (0x20 * ((_f) & 0x1f)))
/* Register Type: EventFilterStatus */
#define NFP_EM_FILTER_STATUS           0x0000
#define NFP_EM_FILTER_ACK              0x0010
/* Register Type: EventFilterFlag */
#define NFP_EM_FILTER_FLAGS            0x0004
#define   NFP_EM_FILTER_FLAGS_PENDING_STATUS            (0x1 << 1)
#define   NFP_EM_FILTER_FLAGS_STATUS                    (0x1)
/* Register Type: EventFilterMask */
#define NFP_EM_FILTER_MASK             0x0008
#define   NFP_EM_FILTER_MASK_TYPE(_x)                   (((_x) & 0x7) << 24)
#define   NFP_EM_FILTER_MASK_TYPE_of(_x)                (((_x) >> 24) & 0x7)
#define     NFP_EM_FILTER_MASK_TYPE_COUNT32             (0)
#define     NFP_EM_FILTER_MASK_TYPE_COUNT16             (1)
#define     NFP_EM_FILTER_MASK_TYPE_MASK32              (2)
#define     NFP_EM_FILTER_MASK_TYPE_MASK16              (3)
#define     NFP_EM_FILTER_MASK_TYPE_FIRSTEV             (4)
#define     NFP_EM_FILTER_MASK_TYPE_LASTEV              (5)
#define   NFP_EM_FILTER_MASK_EVENT(_x)                  ((_x) & 0xfffff)
#define   NFP_EM_FILTER_MASK_EVENT_of(_x)               ((_x) & 0xfffff)
/* Register Type: EventFilterMatch */
#define NFP_EM_FILTER_MATCH            0x000c
#define   NFP_EM_FILTER_MATCH_EVENT(_x)                 ((_x) & 0xfffff)
#define   NFP_EM_FILTER_MATCH_EVENT_of(_x)              ((_x) & 0xfffff)
/* Register Type: EventCombinedStatus */
#define NFP_EM_ALL_STATUS              0x0400
/* Register Type: EventCombinedPendingStatus */
#define NFP_EM_ALL_PENDING             0x0404
/* Register Type: EventConfig */
#define NFP_EM_CONFIG                  0x0408
/* Register Type: EventFilterStatusCount32 */
#define NFP_EVENT_TYPE_COUNT32         0x0000
#define   NFP_EVENT_COUNT32_CNT32_of(_x)                (_x)
/* Register Type: EventFilterStatusCount16 */
#define NFP_EVENT_TYPE_COUNT16         0x0001
#define   NFP_EVENT_COUNT16_TMOUT(_x)                   (((_x) & 0x7) << 29)
#define   NFP_EVENT_COUNT16_TMOUT_of(_x)                (((_x) >> 29) & 0x7)
#define   NFP_EVENT_COUNT16_UPCNT_of(_x)                (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_COUNT16_OVERRIDE(_x)                (((_x) & 0x3f) << 16)
#define   NFP_EVENT_COUNT16_OVERRIDE_of(_x)             (((_x) >> 16) & 0x3f)
#define   NFP_EVENT_COUNT16_CNT16_of(_x)                ((_x) & 0xffff)
/* Register Type: EventFilterStatusBitmask32 */
#define NFP_EVENT_TYPE_MASK32          0x0002
/* Register Type: EventFilterStatusBitmask16 */
#define NFP_EVENT_TYPE_MASK16          0x0003
#define   NFP_EVENT_MASK16_TMOUT(_x)                    (((_x) & 0x7) << 29)
#define   NFP_EVENT_MASK16_TMOUT_of(_x)                 (((_x) >> 29) & 0x7)
#define   NFP_EVENT_MASK16_UPCNT_of(_x)                 (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_MASK16_OVERRIDE(_x)                 (((_x) & 0x7) << 20)
#define   NFP_EVENT_MASK16_OVERRIDE_of(_x)              (((_x) >> 20) & 0x7)
#define   NFP_EVENT_MASK16_CNT4(_x)                     (((_x) & 0xf) << 16)
#define   NFP_EVENT_MASK16_CNT4_of(_x)                  (((_x) >> 16) & 0xf)
#define   NFP_EVENT_MASK16_MASK16_of(_x)                ((_x) & 0xffff)
/* Register Type: EventFilterStatusEvent */
#define NFP_EVENT_TYPE_FIRSTEV         0x0004
#define NFP_EVENT_TYPE_LASTEV          0x0005
#define   NFP_EVENT_EVENT_TMOUT(_x)                     (((_x) & 0x7) << 29)
#define   NFP_EVENT_EVENT_TMOUT_of(_x)                  (((_x) >> 29) & 0x7)
#define   NFP_EVENT_EVENT_UPCNT_of(_x)                  (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_EVENT_CNT2(_x)                      (((_x) & 0x3) << 20)
#define   NFP_EVENT_EVENT_CNT2_of(_x)                   (((_x) >> 20) & 0x3)
#define   NFP_EVENT_EVENT_EVENT_of(_x)                  ((_x) & 0xfffff)

#define NFP_EM_EVENT_TYPE_STATUS_FLAGS	4	/* Status flags event */

#define NFP_EM_EVENT_PIN_ID(pin) \
	(((((pin) >> 3) & 0x7) << 8) | (1 << ((pin) & 0x7)))
#define NFP_EM_EVENT_MATCH(source, pin_id, event_type) \
	(((source) << 16) | ((pin_id) << 4) | (event_type))
#define NFP_EM_EVENT_TYPE_of(ev)        ((ev) & 0xf)
#define NFP_EM_EVENT_SOURCE_of(ev)      (((ev) >> 4) & 0xfff)
#define NFP_EM_EVENT_PROVIDER_of(ev)    (((ev) >> 16) & 0xf)

struct nfp_em_manager {
	spinlock_t lock;	/* Lock for the event filters */
	int irq;
	void __iomem *em;
	struct {
		u32 match;
		u32 mask;
		int type;
		struct nfp_cpp_event *event;
	} filter[32];
};

static irqreturn_t nfp_em_manager_irq(int irq, void *priv)
{
	struct nfp_em_manager *evm = priv;
	int i;
	u32 tmp;

	tmp = readl(evm->em + NFP_EM_ALL_STATUS);

	for (i = 0; i < ARRAY_SIZE(evm->filter); i++) {
		if (tmp & (1 << i)) {
			nfp_cpp_event_callback(evm->filter[i].event);
			readl(evm->em + NFP_EM_FILTER(i) + NFP_EM_FILTER_ACK);
		}
	}

	return (tmp) ? IRQ_HANDLED : IRQ_NONE;
}

/**
 * nfp_em_manager_create() - Create a new EventMonitor manager
 * @em:		__iomem pointer to the EventMonitor
 * @irq:	IRQ vector assigned to the EventMonitor
 *
 * Return: struct nfp_em_manager pointer, or ERR_PTR()
 */
struct nfp_em_manager *nfp_em_manager_create(void __iomem *em, int irq)
{
	int err;
	struct nfp_em_manager *evm;

	evm = kmalloc(sizeof(*evm), GFP_KERNEL);
	if (!evm)
		return ERR_PTR(-ENOMEM);

	evm->irq = irq;
	evm->em = em;

	spin_lock_init(&evm->lock);

	err = request_irq(evm->irq, nfp_em_manager_irq,
			  IRQF_SHARED, "nfp_em", evm);
	if (err < 0) {
		kfree(evm);
		return ERR_PTR(err);
	}

	return evm;
}

/**
 * nfp_em_manager_destroy() - Release the NFP EventMonitor manager handle
 * @evm:	NFP EventMonitor manager handle, or NULL
 */
void nfp_em_manager_destroy(struct nfp_em_manager *evm)
{
	if (!evm)
		return;

	free_irq(evm->irq, evm);
	kfree(evm);
}

/**
 * nfp_em_manager_acquire() - Bind a EventMonitor filter to a NFP CPP event
 * @evm:	NFP EventMonitor manager handle
 * @event:	NFP CPP Event handle to call when the filter triggers
 * @match:	EventMonitor filter match pattern
 * @mask:	EventMonitor filter mask pattern
 * @type:	EventMonitor filter type
 *
 * Return: filter number >= 0, or -ERRNO
 */
int nfp_em_manager_acquire(struct nfp_em_manager *evm,
			   struct nfp_cpp_event *event,
			   u32 match, u32 mask, u32 type)
{
	unsigned long flags;
	int i;

	if (!evm)
		return -EINVAL;

	spin_lock_irqsave(&evm->lock, flags);
	for (i = 0; i < ARRAY_SIZE(evm->filter); i++) {
		if (!evm->filter[i].event) {
			evm->filter[i].event = event;
			break;
		}
	}
	spin_unlock_irqrestore(&evm->lock, flags);

	if (i == ARRAY_SIZE(evm->filter))
		return -EBUSY;

	evm->filter[i].mask = mask;
	evm->filter[i].match = match;
	evm->filter[i].type = type;
	writel(NFP_EM_FILTER_MASK_TYPE(type) |
	       NFP_EM_FILTER_MASK_EVENT(mask),
	       evm->em + NFP_EM_FILTER(i) + NFP_EM_FILTER_MASK);
	writel(NFP_EM_FILTER_MATCH_EVENT(match),
	       evm->em + NFP_EM_FILTER(i) + NFP_EM_FILTER_MATCH);

	return i;
}

/**
 * nfp_em_manager_release: Unbind a filter from any NFP CPP Event
 * @evm:	NFP EventMonitor manager handle
 * @filter:	Filter number
 */
void nfp_em_manager_release(struct nfp_em_manager *evm, int filter)
{
	unsigned long flags;

	spin_lock_irqsave(&evm->lock, flags);
	writel(NFP_EM_FILTER_MASK_EVENT(0xffffff),
	       evm->em + NFP_EM_FILTER(filter) + NFP_EM_FILTER_MASK);
	writel(NFP_EM_FILTER_MATCH_EVENT(0xffffff),
	       evm->em + NFP_EM_FILTER(filter) + NFP_EM_FILTER_MATCH);
	readl(evm->em + NFP_EM_FILTER(filter) + NFP_EM_FILTER_ACK);
	evm->filter[filter].event = NULL;
	spin_unlock_irqrestore(&evm->lock, flags);
}
