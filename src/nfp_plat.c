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
 * nfp_plat.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

/* This component is only compiled for the NFP's ARM Linux
 */
#include <linux/kernel.h>

#ifdef CONFIG_ARCH_NFP

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "nfpcore/nfp_arm.h"
#include "nfpcore/nfp_cpp.h"

#include "nfpcore/nfp_target.h"

#include "nfpcore/nfp_net_vnic.h"

#include "nfp_main.h"

#define NFP_EXPL_START		(0xde000000)
#define NFP_ARM_EM_START	(0xd6000000 + NFP_ARM_EM)

#define NFP_EXPA_START		0xc0000000
#define NFP_ARM_START		0xd6000000

#define BAR_FLAG_LOCKED		BIT(0)

struct nfp_plat_bar {
	unsigned flags;
	atomic_t usage;
	u32 csr;
};

#define NFP_EM_FILTER_BASE	8
#define NFP_EM_FILTERS		32

struct nfp_plat {
	struct device *dev;

	struct platform_device *nfp_dev_cpp;
	struct platform_device *nfp_net_vnic[4];
	struct nfp_cpp *cpp;
	struct nfp_cpp_operations op;
	int (*target_pushpull)(u32 cpp_id, u64 address);
	spinlock_t lock;			/* Lock for the BAR cache */
	struct nfp_plat_bar bulk_bar[7];	/* Last BULK is for user use */
	struct nfp_plat_bar expa_bar[15];	/* Last EXPA is for user use */
	unsigned long expl_bar_mask[BITS_TO_LONGS(16)];

	void __iomem *gcsr;
	struct device_node *arm_em;
	phys_addr_t expl_phys;
	size_t      expl_size;
	void __iomem *expl_io;
	void __iomem *expl_data;

	unsigned long irq_used[BITS_TO_LONGS(NFP_EM_FILTERS)];
	unsigned int irq[NFP_EM_FILTERS];
};

struct nfp_plat_area_priv {
	/* Always Valid */
	u32 dest;
	u64 addr;
	unsigned long size;
	struct {
		int read;
		int write;
		int bar;
	} width;

	/* Non-null when allocated */
	struct nfp_plat_bar *bar;	/* First bar */
	struct resource resource;

	/* Valid while allocated */
	enum bar_type {
		BAR_INVALID = 0,
		BAR_BULK,
		BAR_EXPA,
	} type;
	int bars;			/* Number of bars contig. allocated */
	int id;				/* ID of first bar */
	u64 offset;
	unsigned long phys_addr;	/* Physical address of the BAR base */
	unsigned long phys_size;	/* Bar total size */
	void __iomem *iomem;
};

struct nfp_plat_event_priv {
	u32 match;
	u32 mask;
	unsigned int type;
	int em_slot;
};

static void bar_lock(struct nfp_plat_bar *bar)
{
	atomic_inc(&bar->usage);
	bar->flags |= BAR_FLAG_LOCKED;
}

static void bar_unlock(struct nfp_plat_bar *bar)
{
	if (atomic_dec_and_test(&bar->usage)) {
		bar->flags &= ~BAR_FLAG_LOCKED;
		bar->csr = 0;
	}
}

static int bar_find(struct nfp_cpp_area *area, enum bar_type type,
		    int bars, u32 csr, int can_alloc)
{
	struct nfp_plat *nfp_priv = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	struct nfp_plat_bar *bar;
	unsigned long bar_base;
	unsigned int bar_max;
	size_t bar_size;
	unsigned int i, j;

	/* Only for unallocated areas... */
	BUG_ON(priv->bar);
	BUG_ON(bars == 0);

	switch (type) {
	case BAR_BULK:
		bar = nfp_priv->bulk_bar;
		bar_max = ARRAY_SIZE(nfp_priv->bulk_bar);
		bar_size = NFP_ARM_GCSR_BULK_SIZE;
		bar_base = 0;
		break;
	case BAR_EXPA:
		bar = nfp_priv->expa_bar;
		bar_max = ARRAY_SIZE(nfp_priv->expa_bar);
		bar_size = NFP_ARM_GCSR_EXPA_SIZE;
		bar_base = NFP_EXPA_START;
		break;
	default:
		return -EINVAL;
	}

	/* Adjust to given limit */
	bar_max = bar_max - bars + 1;

	spin_lock(&nfp_priv->lock);

	/* Find a matching bar */
	for (i = 0; i < bar_max; i++) {
		for (j = 0; j < bars; j++) {
			if ((bar[i + j].flags & BAR_FLAG_LOCKED) == 0)
				break;
			if ((csr + j) !=  bar[i + j].csr)
				break;
		}
		if (j == bars)
			break;
	}
	if (i < bar_max) {
		/* Ooh! We got one! */
		goto found_slot;
	}

	if (!can_alloc) {
		spin_unlock(&nfp_priv->lock);
		return -EEXIST;
	}

	/* Find an unused set. */
	for (i = 0; i < bar_max; i++) {
		for (j = 0; j < bars; j++) {
			if ((bar[i + j].flags & BAR_FLAG_LOCKED))
				break;
		}
		if (j == bars)
			break;
	}

	if (i == bar_max) {
		spin_unlock(&nfp_priv->lock);
		return -ENOSPC;
	}

found_slot:
	priv->id = i;

	for (j = 0; j < bars; j++) {
		bar_lock(&bar[priv->id + j]);
		bar[priv->id + j].csr = csr + j;
	}

	spin_unlock(&nfp_priv->lock);

	priv->bar = &bar[priv->id];
	priv->bars = bars;
	priv->type = type;
	priv->offset = priv->addr & (bar_size - 1);
	priv->phys_addr = bar_base + priv->id * bar_size;
	priv->phys_size = bar_size * bars;

	return 0;
}

static void bulk_csr(u32 *csr, u32 dest, u64 addr, int width)
{
	*csr = NFP_ARM_GCSR_BULK_CSR(0,	/* Always expansion */
			  NFP_CPP_ID_TARGET_of(dest),
			  NFP_CPP_ID_TOKEN_of(dest),
			  (width = 8) ? NFP_ARM_GCSR_BULK_BAR_LEN_64BIT :
					NFP_ARM_GCSR_BULK_BAR_LEN_32BIT,
			  addr);
}

static void bulk_set(struct nfp_plat *priv, u32 csr, unsigned int id)
{
	int i;

	readl(priv->gcsr + NFP_ARM_GCSR_BULK_BAR(id));
	writel(csr, priv->gcsr + NFP_ARM_GCSR_BULK_BAR(id));
	for (i = 0; i < 10; i++)
		if (readl(priv->gcsr + NFP_ARM_GCSR_BULK_BAR(id)) == csr)
			break;
	if (i == 10)
		dev_err(priv->dev, "BULK%d: %08x != %08x\n",
			id,
			readl(priv->gcsr + NFP_ARM_GCSR_BULK_BAR(id)),
			csr);
}

static void expa_csr(u32 *csr, u32 dest, u64 addr, int width)
{
	unsigned action = NFP_CPP_ID_ACTION_of(dest);
	int is_64 = (width == 8) ? 1 : 0;

	if (action == NFP_CPP_ACTION_RW)
		action = 0;

	*csr = NFP_ARM_GCSR_EXPA_CSR(0,	/* Always expansion */
			  NFP_CPP_ID_TARGET_of(dest),
			  NFP_CPP_ID_TOKEN_of(dest),
			  is_64 ? NFP_ARM_GCSR_EXPA_BAR_LEN_64BIT :
				  NFP_ARM_GCSR_EXPA_BAR_LEN_32BIT,
			  NFP_CPP_ID_ACTION_of(dest),
			  addr);
}

static void expa_set(struct nfp_plat *priv, u32 csr, unsigned int id)
{
	int i;

	readl(priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(id));
	writel(csr, priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(id));
	for (i = 0; i < 10; i++)
		if (readl(priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(id)) == csr)
			break;
	if (i == 10)
		dev_err(priv->dev, "EXPA%d: %08x != %08x\n",
			id,
			readl(priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(id)),
			csr);
}

static u16 nfp_plat_get_interface(struct device *dev)
{
	return NFP_CPP_INTERFACE(NFP_CPP_INTERFACE_TYPE_ARM,
				 0,
				 NFP_CPP_INTERFACE_CHANNEL_PEROPENER);
}

static int nfp_plat_area_init(struct nfp_cpp_area *area, u32 dest, u64 addr,
			      unsigned long size)
{
	struct nfp_plat *nfp_priv = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	int pp;

	pp = nfp_priv->target_pushpull(dest, addr);
	if (pp < 0)
		return pp;

	priv->width.read = PUSH_WIDTH(pp);
	priv->width.write = PULL_WIDTH(pp);
	if (priv->width.read > 0 &&
	    priv->width.write > 0 &&
	    priv->width.read != priv->width.write) {
		return -EINVAL;
	}

	if (priv->width.read)
		priv->width.bar = priv->width.read;
	else
		priv->width.bar = priv->width.write;

	priv->dest = dest;
	priv->addr = addr;
	priv->size = size;
	priv->bar = NULL;

	return 0;
}

void nfp_plat_area_cleanup(struct nfp_cpp_area *area)
{
}

static int bar_find_any(struct nfp_cpp_area *area, int can_allocate)
{
	struct nfp_plat *nfp_priv = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	unsigned long size;
	int bars, err, i;
	int width;
	u32 dest;
	u64 addr;
	u32 csr;

	BUG_ON(priv->bar);

	dest = priv->dest;
	addr = priv->addr;
	size = priv->size;
	width = priv->width.bar;

#define BARS(addr, size, max_size) \
		((((addr) & ((max_size) - 1)) + \
		   (size) + (max_size) - 1) / (max_size))

	bars = BARS(addr, size, NFP_ARM_GCSR_EXPA_SIZE);
	if (bars < ARRAY_SIZE(nfp_priv->expa_bar)) {
		expa_csr(&csr, dest, addr, width);
		err = bar_find(area, BAR_EXPA, bars, csr, can_allocate);
		if (err == 0) {
			for (i = 0; i < bars; i++)
				expa_set(nfp_priv, priv->bar[i].csr,
					 priv->id + i);
			return 0;
		}
	}

	bars = BARS(addr, size, NFP_ARM_GCSR_BULK_SIZE);
	if ((bars < ARRAY_SIZE(nfp_priv->bulk_bar)) &&
	    (NFP_CPP_ID_ACTION_of(dest) == NFP_CPP_ACTION_RW ||
	     NFP_CPP_ID_ACTION_of(dest) == 0 ||
	     NFP_CPP_ID_ACTION_of(dest) == 1)) {
		bulk_csr(&csr, dest, addr, width);

		err = bar_find(area, BAR_BULK, bars, csr, can_allocate);
		if (err == 0) {
			for (i = 0; i < bars; i++)
				bulk_set(nfp_priv, priv->bar[i].csr,
					 priv->id + i);
			return 0;
		}
	}

	return -ENOSPC;
}

static int nfp_plat_acquire(struct nfp_cpp_area *area)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	phys_addr_t phys_offset;
	int err;

	err = bar_find_any(area, false);
	if (err != 0)
		err = bar_find_any(area, true);

	if (err == 0) {
		phys_offset = priv->phys_addr + priv->offset;

		memset(&priv->resource, 0, sizeof(priv->resource));
		priv->resource.name = nfp_cpp_area_name(area);
		priv->resource.start = phys_offset;
		priv->resource.end   = phys_offset + priv->size - 1;
		priv->resource.flags = IORESOURCE_MEM;
		priv->resource.parent = NULL;
	}

	return err;
}

static void nfp_plat_release(struct nfp_cpp_area *area)
{
	struct nfp_plat *nfp_priv = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	int i;

	BUG_ON(!priv->bar);

	if (priv->iomem)
		iounmap(priv->iomem);

	spin_lock(&nfp_priv->lock);

	for (i = 0; i < priv->bars; i++)
		bar_unlock(&priv->bar[i]);

	spin_unlock(&nfp_priv->lock);

	priv->bar = NULL;
	priv->iomem = NULL;
}

static struct resource *nfp_plat_resource(struct nfp_cpp_area *area)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);

	BUG_ON(!priv->bar);

	return &priv->resource;
}

static phys_addr_t nfp_plat_phys(struct nfp_cpp_area *area)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);

	BUG_ON(!priv->bar);

	return priv->phys_addr + priv->offset;
}

static void __iomem *nfp_plat_iomem(struct nfp_cpp_area *area)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	phys_addr_t phys_offset;

	BUG_ON(!priv->bar);

	if (priv->iomem)
		return priv->iomem;

	phys_offset = priv->phys_addr + priv->offset;

	priv->iomem = ioremap(phys_offset, priv->size);

	return priv->iomem;
}

static int nfp_plat_read(struct nfp_cpp_area *area, void *kernel_vaddr,
			 unsigned long offset, unsigned int length)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	void __iomem *iomem;
	unsigned long i;
	int is_64;

	BUG_ON(!priv->bar);

	if (!priv->width.read)
		return -EINVAL;

	iomem = nfp_cpp_area_iomem(area);
	if (!iomem)
		return -ENOMEM;

	is_64 = (priv->width.read == 8) ? 1 : 0;

	if (is_64) {
		if (((offset % sizeof(u64)) != 0) ||
		    ((length % sizeof(u64)) != 0))
			return -EINVAL;
	} else {
		if (((offset % sizeof(u32)) != 0) ||
		    ((length % sizeof(u32)) != 0))
			return -EINVAL;
	}
	BUG_ON((offset + length) > priv->size);

	if (priv->type == BAR_BULK || priv->type == BAR_EXPA) {
		/* Easy! It's bulk or expansion bar! */
		if (((offset % sizeof(u64)) == 0) &&
		    ((length % sizeof(u64)) == 0)) {
			for (i = 0; i < length; i += sizeof(u64)) {
				u64 tmp = readq(iomem + offset + i);
				*(u64 *)(kernel_vaddr + i) = tmp;
			}
		} else {
			for (i = 0; i < length; i += sizeof(u32)) {
				u32 tmp = readl(iomem + offset + i);
				*(u32 *)(kernel_vaddr + i) = tmp;
			}
		}
		return i;
	}

	/* Explicit BARs are reserved for user usage
	 */
	return -EINVAL;
}

static int nfp_plat_write(struct nfp_cpp_area *area,
			  const void *kernel_vaddr, unsigned long offset,
			  unsigned int length)
{
	struct nfp_plat_area_priv *priv = nfp_cpp_area_priv(area);
	void __iomem *iomem;
	unsigned long i;
	int is_64;

	BUG_ON(!priv->bar);

	if (!priv->width.write)
		return -EINVAL;

	iomem = nfp_cpp_area_iomem(area);
	if (!iomem)
		return -ENOMEM;

	is_64 = (priv->width.write == 8) ? 1 : 0;

	if (is_64) {
		if (((offset % sizeof(u64)) != 0) ||
		    ((length % sizeof(u64)) != 0))
			return -EINVAL;
	} else {
		if (((offset % sizeof(u32)) != 0) ||
		    ((length % sizeof(u32)) != 0))
			return -EINVAL;
	}

	if (priv->type == BAR_BULK || priv->type == BAR_EXPA) {
		/* Easy! It's bulk or expansion bar! */
		if (((offset % sizeof(u64)) == 0) &&
		    ((length % sizeof(u64)) == 0)) {
			for (i = 0; i < length; i += sizeof(u64)) {
				writeq(*(u64 *)(kernel_vaddr + i),
				       iomem + offset + i);
			}
		} else {
			for (i = 0; i < length; i += sizeof(u32)) {
				writel(*(u32 *)(kernel_vaddr + i),
				       iomem + offset + i);
			}
		}
		return i;
	}

	/* Explicit BARs are reserved for user usage
	 */
	return -EINVAL;
}

static irqreturn_t nfp_plat_irq(int irq, void *priv)
{
	struct nfp_cpp_event *event = priv;

	nfp_cpp_event_callback(event);

	return IRQ_HANDLED;
}

static int nfp_plat_event_acquire(struct nfp_cpp_event *event,
				  u32 match, u32 mask, unsigned int type)
{
	struct nfp_plat_event_priv *event_priv = nfp_cpp_event_priv(event);
	struct nfp_plat *priv = nfp_cpp_priv(nfp_cpp_event_cpp(event));
	int err, em_slot;
	unsigned int irq;
	u32 spec[4];

	/* Only type 0 filters are supported */
	if (type != 0)
		return -EINVAL;

	event_priv->match = match;
	event_priv->mask = mask;
	event_priv->type = type;
	event_priv->em_slot = -1;

	spin_lock(&priv->lock);

	em_slot = find_first_zero_bit(&priv->irq_used, 32);
	if (em_slot >= 32) {
		spin_unlock(&priv->lock);
		return -ENOSPC;
	}
	BUG_ON(test_bit(em_slot, priv->irq_used));

	set_bit(em_slot, priv->irq_used);
	spin_unlock(&priv->lock);

	event_priv->em_slot = em_slot;
	spec[0] = event_priv->em_slot * 32;
	spec[1] = event_priv->match;
	spec[2] = event_priv->mask;
	spec[3] = event_priv->type;

	irq = irq_create_of_mapping(priv->arm_em, spec, 4);
	priv->irq[em_slot] = irq;

	err = request_irq(irq, nfp_plat_irq, 0, "nfp_plat", event);
	if (err < 0) {
		spin_lock(&priv->lock);
		clear_bit(em_slot, priv->irq_used);
		spin_unlock(&priv->lock);
	}

	return err;
}

static void nfp_plat_event_release(struct nfp_cpp_event *event)
{
	struct nfp_plat_event_priv *event_priv = nfp_cpp_event_priv(event);
	struct nfp_plat *priv = nfp_cpp_priv(nfp_cpp_event_cpp(event));
	int irq;

	spin_lock(&priv->lock);
	clear_bit(event_priv->em_slot, priv->irq_used);
	irq = priv->irq[event_priv->em_slot];
	priv->irq[event_priv->em_slot] = -1;
	spin_unlock(&priv->lock);

	free_irq(irq, event);

	irq_dispose_mapping(irq);
}

#define NFP_EXPL_POST 0
#define NFP_EXPL1_BAR 1
#define NFP_EXPL2_BAR 2

struct nfp_plat_explicit_priv {
	int index;
};

#define EXPL_BASE		  0xf800
#define EXPL_INDEX_TO_OFFSET(n)	  ((n) << 5)
#define EXPL_INDEX_TO_DATA_REF(n) ({ u16 offset = EXPL_BASE + \
				     EXPL_INDEX_TO_OFFSET(n); \
				     (offset & 0x3ff0) | \
				     ((offset & 0xc000) >> 14); })
#define EXPL_INDEX_TO_SIGNAL_REF(n)  (0x60 + ((n) << 1))

int nfp_plat_explicit_acquire(struct nfp_cpp_explicit *expl)
{
	struct nfp_plat_explicit_priv *expl_priv = nfp_cpp_explicit_priv(expl);
	struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl);
	struct nfp_plat *priv = nfp_cpp_priv(cpp);
	int i;

	/* The last EXPL is for user use! */
	for (i = 0; i < 7; i++) {
		if (!test_and_set_bit(i, priv->expl_bar_mask)) {
			expl_priv->index = i;
			return 0;
		}
	}

	return -EBUSY;
}

/* Release an explicit transaction handle */
void nfp_plat_explicit_release(struct nfp_cpp_explicit *expl)
{
	struct nfp_plat_explicit_priv *expl_priv = nfp_cpp_explicit_priv(expl);
	struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl);
	struct nfp_plat *priv = nfp_cpp_priv(cpp);

	clear_bit(expl_priv->index, priv->expl_bar_mask);
}

/* Perform the transaction */
static int nfp_plat_explicit_do(struct nfp_cpp_explicit *expl,
				const struct nfp_cpp_explicit_command *cmd,
				u64 address)
{
	struct nfp_plat_explicit_priv *expl_priv = nfp_cpp_explicit_priv(expl);
	struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl);
	u16 signal_master, data_master, default_master;
	struct nfp_plat *priv = nfp_cpp_priv(cpp);
	void __iomem *expl_io = priv->expl_io;
	void __iomem *gcsr = priv->gcsr;
	int err, index = expl_priv->index;
	u32 expl1, expl2, post;
	u16 data_ref, signal_ref;
	u32 required = 0;
	u32 model = nfp_cpp_model(cpp);

	if (NFP_CPP_MODEL_IS_6000(model))
		default_master = 0x11;
	else
		default_master = 0x01;

	if (cmd->data_master == 0)
		data_master = default_master;
	else
		data_master = cmd->data_master;

	if (cmd->data_master == 0 && cmd->data_ref == 0)
		data_ref = EXPL_INDEX_TO_DATA_REF(index);
	else
		data_ref = cmd->data_ref;

	if (cmd->signal_master == 0)
		signal_master = default_master;
	else
		signal_master = cmd->signal_master;

	if (cmd->signal_master == 0 && cmd->signal_ref == 0)
		signal_ref = EXPL_INDEX_TO_SIGNAL_REF(index);
	else
		signal_ref = cmd->signal_ref;

	expl1 = NFP_ARM_GCSR_EXPL1_BAR_POSTED
		| NFP_ARM_GCSR_EXPL1_BAR_DATA_MASTER(data_master)
		| NFP_ARM_GCSR_EXPL1_BAR_DATA_REF(data_ref)
		| NFP_ARM_GCSR_EXPL1_BAR_SIGNAL_REF(signal_ref);

	expl2 = NFP_ARM_GCSR_EXPL2_BAR_TGT(NFP_CPP_ID_TARGET_of(cmd->cpp_id))
		| NFP_ARM_GCSR_EXPL2_BAR_ACT(NFP_CPP_ID_ACTION_of(cmd->cpp_id))
		| NFP_ARM_GCSR_EXPL2_BAR_TOK(NFP_CPP_ID_TOKEN_of(cmd->cpp_id))
		| NFP_ARM_GCSR_EXPL2_BAR_LEN(cmd->len)
		| NFP_ARM_GCSR_EXPL2_BAR_BYTE_MASK(cmd->byte_mask)
		| NFP_ARM_GCSR_EXPL2_BAR_SIGNAL_MASTER(signal_master);

	post = 0;
	if (cmd->posted) {
		post = NFP_ARM_GCSR_EXPL1_BAR_POSTED
			| NFP_ARM_GCSR_EXPL_POST_SIG_A(cmd->siga)
			| NFP_ARM_GCSR_EXPL_POST_SIG_B(cmd->sigb);

		switch (cmd->siga_mode) {
		case NFP_SIGNAL_PUSH:
			/* Fallthrough */
			required |= NFP_ARM_GCSR_EXPL_POST_SIG_A_RCVD;
		case NFP_SIGNAL_PUSH_OPTIONAL:
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_A_VALID;
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PUSH;
			break;
		case NFP_SIGNAL_PULL:
			required |= NFP_ARM_GCSR_EXPL_POST_SIG_A_RCVD;
			/* Fallthrough */
		case NFP_SIGNAL_PULL_OPTIONAL:
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_A_VALID;
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_A_BUS_PULL;
			break;
		case NFP_SIGNAL_NONE:
			break;
		}

		switch (cmd->sigb_mode) {
		case NFP_SIGNAL_PUSH:
			required |= NFP_ARM_GCSR_EXPL_POST_SIG_B_RCVD;
			/* Fallthrough */
		case NFP_SIGNAL_PUSH_OPTIONAL:
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_B_VALID;
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PUSH;
			break;
		case NFP_SIGNAL_PULL:
			required |= NFP_ARM_GCSR_EXPL_POST_SIG_B_RCVD;
			/* Fallthrough */
		case NFP_SIGNAL_PULL_OPTIONAL:
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_B_VALID;
			post |= NFP_ARM_GCSR_EXPL_POST_SIG_B_BUS_PULL;
			break;
		case NFP_SIGNAL_NONE:
			break;
		}
	}

	if (signal_master == default_master) {
		int ref = EXPL_INDEX_TO_SIGNAL_REF(index);

		post &= ~(NFP_ARM_GCSR_EXPL_POST_SIG_A(~0) |
			  NFP_ARM_GCSR_EXPL_POST_SIG_B(~0));
		post |= NFP_ARM_GCSR_EXPL_POST_SIG_A(ref) |
			NFP_ARM_GCSR_EXPL_POST_SIG_B(ref | 1);
	    }

	/* Write the EXPL0_BAR csr */
	writel(NFP_ARM_GCSR_EXPL0_BAR_ADDR(address),
	       gcsr + NFP_ARM_GCSR_EXPL0_BAR(index));

	writel(expl1, gcsr + NFP_ARM_GCSR_EXPL1_BAR(index));

	/* Write the EXPL2_BAR csr */
	writel(expl2, gcsr + NFP_ARM_GCSR_EXPL2_BAR(index));

	/* Write the EXPL_POST csr */
	writel(post & ~NFP_ARM_GCSR_EXPL_POST_CMD_COMPLETE, gcsr +
	       NFP_ARM_GCSR_EXPL_POST(index));
	/* Start the transaction, by doing a dummy read from the
	 * ARM Gasket area
	 */
	readb(expl_io + (NFP_ARM_GCSR_EXPL_SIZE * index) +
	      (address & (NFP_ARM_GCSR_EXPL_SIZE - 1)));

	/* If we have been told to wait for one or more
	 * signals to return, do so.
	 * This *cannot* sleep, as we are simulating a
	 * hardware operation that has no timeout.
	 */
	if (required) {
		do {
			post = readl(gcsr + NFP_ARM_GCSR_EXPL_POST(index));
			post = readl(gcsr + NFP_ARM_GCSR_EXPL_POST(index));
		} while (((post & required) != required));
	}

	/* Calculate the return mask */
	err = 0;
	if (post & NFP_ARM_GCSR_EXPL_POST_SIG_A_RCVD)
		err |= NFP_SIGNAL_MASK_A;
	if (post & NFP_ARM_GCSR_EXPL_POST_SIG_B_RCVD)
		err |= NFP_SIGNAL_MASK_B;

	return err;
}

/* Write data to send */
int nfp_plat_explicit_put(struct nfp_cpp_explicit *expl,
			  const void *buff, size_t len)
{
	struct nfp_plat_explicit_priv *expl_priv = nfp_cpp_explicit_priv(expl);
	struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl);
	struct nfp_plat *priv = nfp_cpp_priv(cpp);

	if (len > 128)
		return -EINVAL;

	memcpy(priv->expl_data + EXPL_INDEX_TO_OFFSET(expl_priv->index),
	       buff, len);

	return len;
}

/* Read data received */
int nfp_plat_explicit_get(struct nfp_cpp_explicit *expl, void *buff, size_t len)
{
	struct nfp_plat_explicit_priv *expl_priv = nfp_cpp_explicit_priv(expl);
	struct nfp_cpp *cpp = nfp_cpp_explicit_cpp(expl);
	struct nfp_plat *priv = nfp_cpp_priv(cpp);

	if (len > 128)
		return -EINVAL;

	memcpy(buff, priv->expl_data + EXPL_INDEX_TO_OFFSET(expl_priv->index),
	       len);

	return len;
}

const struct nfp_cpp_operations nfp_plat_template = {
	.get_interface = nfp_plat_get_interface,

	.area_priv_size = sizeof(struct nfp_plat_area_priv),
	.area_init = nfp_plat_area_init,
	.area_cleanup = nfp_plat_area_cleanup,
	.area_acquire = nfp_plat_acquire,
	.area_release = nfp_plat_release,
	.area_phys = nfp_plat_phys,
	.area_resource = nfp_plat_resource,
	.area_iomem = nfp_plat_iomem,
	.area_read = nfp_plat_read,
	.area_write = nfp_plat_write,

	.explicit_priv_size = sizeof(struct nfp_plat_explicit_priv),
	.explicit_acquire = nfp_plat_explicit_acquire,
	.explicit_release = nfp_plat_explicit_release,
	.explicit_put = nfp_plat_explicit_put,
	.explicit_get = nfp_plat_explicit_get,
	.explicit_do = nfp_plat_explicit_do,

	.event_priv_size = sizeof(struct nfp_plat_event_priv),
	.event_acquire = nfp_plat_event_acquire,
	.event_release = nfp_plat_event_release,
};

static const struct of_device_id nfp_plat_match[] = {
	{
		.compatible = "netronome,nfp6000-arm-cpp",
		.data = nfp6000_target_pushpull
	}, {
	},
};
MODULE_DEVICE_TABLE(of, nfp_plat_match);

#define BARTYPE_EXPA	1
#define BARTYPE_EXPL	2

static int nfp_plat_bar_scan(struct nfp_plat *priv,
			     u32 arm_addr, u32 arm_size,
			     u32 cpp_id, u64 cpp_addr, unsigned long *used)
{
	int pp, target, action, token, is_64;
	struct device *dev = priv->dev;
	int bar, type = 0;
	u32 csr;

	target = NFP_CPP_ID_TARGET_of(cpp_id);
	action = NFP_CPP_ID_ACTION_of(cpp_id);
	token = NFP_CPP_ID_TOKEN_of(cpp_id);
	pp = priv->target_pushpull(cpp_id, cpp_addr);
	is_64 = (PUSH_WIDTH(pp) == 8) ? 1 : 0;

	switch (arm_size) {
	case 0x20000000:
		bar = arm_addr >> 29;

		if (arm_addr & ~0xe0000000) {
			dev_warn(dev, "BULK BAR%d: ARM address is unaligned, ignoring\n",
				 bar);
			return -EINVAL;
		}

		bar_lock(&priv->bulk_bar[bar]);
		set_bit(bar, used);

		if (cpp_addr & ~0xffe0000000) {
			dev_warn(dev, "BULK BAR%d: CPP address is unaligned\n",
				 bar);
			return -EINVAL;
		}

		/* Preserve, don't modify */
		if (cpp_id == 0) {
			priv->bulk_bar[bar].csr = readl(priv->gcsr +
					NFP_ARM_GCSR_BULK_BAR(bar));
			dev_info(dev, "BULK BAR%d: Preserved as 0x%08x\n",
				 bar, priv->bulk_bar[bar].csr);
			break;
		}

		if ((cpp_id & 0xffffff00) == 0xffffff00) {
			type = BARTYPE_EXPA;
			csr = NFP_ARM_GCSR_BULK_CSR(1, 0, 0, 0, 0);
			dev_info(dev, "BULK BAR%d: Expansion\n", bar);
		} else {
			csr = NFP_ARM_GCSR_BULK_CSR(0, target, token, is_64,
						    cpp_addr);
		}

		priv->bulk_bar[bar].csr = csr;
		writel(csr, priv->gcsr + NFP_ARM_GCSR_BULK_BAR(bar));
		/* Read-back to flush the CSR */
		readl(priv->gcsr + NFP_ARM_GCSR_BULK_BAR(bar));
		break;
	case 0x02000000:
		bar = (arm_addr >> 25) & 0xf;

		if (arm_addr < 0xc0000000 || arm_addr >= 0xe0000000) {
			dev_warn(dev, "EXPA BAR%d: ARM address outside of range 0x%08x-0x%08x\n",
				 bar, 0xc0000000, 0xde000000);
			return -EINVAL;
		}
		if (arm_addr & ~0xfe000000) {
			dev_warn(dev, "EXPA BAR%d: ARM address is unaligned\n",
				 bar);
			return -EINVAL;
		}

		bar_lock(&priv->expa_bar[bar]);
		set_bit(bar + 8, used);

		/* Preserve, do not modify? */
		if (cpp_id == 0) {
			priv->expa_bar[bar].csr = readl(priv->gcsr +
					NFP_ARM_GCSR_EXPA_BAR(bar));
			dev_info(dev, "EXPA BAR%d: Preserved as 0x%08x\n",
				 bar, priv->expa_bar[bar].csr);
			break;
		}

		if (cpp_addr & ~0xfffe000000) {
			dev_warn(dev, "EXPA BAR%d: CPP address is unaligned\n",
				 bar);
			return -EINVAL;
		}

		if ((cpp_id & 0xffffff00) == 0xffffff00) {
			/* Promote to EXPL */
			dev_warn(dev, "EXPA BAR%d: Explicit\n", bar);
			type = BARTYPE_EXPL;
			csr  = NFP_ARM_GCSR_EXPA_CSR(1, 0, 0, 0, 0, 0);
		} else {
			csr  = NFP_ARM_GCSR_EXPA_CSR(0, target, action, token,
						     is_64, cpp_addr);
		}
		priv->expa_bar[bar].csr = csr;
		writel(csr, priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(bar));
		/* Read-back to flush the CSR */
		readl(priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(bar));
		break;
	default:
		dev_warn(dev, "Illegal ARM range size 0x%08x\n", arm_size);
		break;
	}

	return type;
}

/* Sanity checking, and reserve BARs in the 'ranges' property
 *
 */
static int nfp_plat_of(struct nfp_plat *priv)
{
	struct device_node *dn = priv->dev->of_node;
	struct device *dev = priv->dev;
	DECLARE_BITMAP(used, 8 + 16 + 8);
	struct property *prop;
	struct resource res;
	const __be32 *ptr;
	u32 tmp;
	int err;
	int i;

	priv->arm_em = of_irq_find_parent(dn);
	if (!priv->arm_em) {
		dev_err(dev, "Can't find interrupt parent\n");
		return -EINVAL;
	}

	/* Mark as used the EM filters we won't be using */
	for (i = 0; i < NFP_EM_FILTER_BASE; i++)
		set_bit(i, priv->irq_used);

	if (of_property_read_u32(dn, "#address-cells", &tmp) < 0 || tmp != 2) {
		dev_err(dev, "#address-cells <%d> != 2\n", tmp);
		return -EINVAL;
	}

	if (of_property_read_u32(dn, "#size-cells", &tmp) < 0 || tmp != 1) {
		dev_err(dev, "#size-cells <%d> != 1\n", tmp);
		return -EINVAL;
	}

	if (of_address_to_resource(dn, 0, &res) < 0) {
		dev_err(dev, "Can't get 'reg' range 0 for BULK\n");
		return -EINVAL;
	}

	dev_info(dev, "BARs at 0x%08x\n", res.start);

	if (resource_size(&res) < (4 * (8 + 16 + (8 * 4)))) {
		dev_err(dev, "Size of 'reg' range 0 is too small: 0x%x\n",
			resource_size(&res));
		return -EINVAL;
	}

	priv->gcsr = of_iomap(dn, 0);
	if (!priv->gcsr) {
		dev_err(dev, "Can't map the 'reg' index 0 ('csr')\n");
		return -ENOMEM;
	}

	of_property_for_each_u32(dn, "ranges", prop, ptr, tmp) {
		u32 cpp_id;
		u64 cpp_addr;

		u32 arm_addr;
		u32 arm_size;

		cpp_id = tmp & 0xffffff00;
		cpp_addr = (u64)(tmp & 0xff) << 32;

		ptr = of_prop_next_u32(prop, ptr, &tmp);
		if (!ptr) {
			dev_err(dev, "Property 'ranges' is 3 cells short!\n");
			iounmap(priv->gcsr);
			return -EINVAL;
		}
		cpp_addr |= tmp;

		ptr = of_prop_next_u32(prop, ptr, &tmp);
		if (!ptr) {
			dev_err(dev, "Property 'ranges' is 2 cells short!\n");
			iounmap(priv->gcsr);
			return -EINVAL;
		}
		arm_addr = tmp;

		ptr = of_prop_next_u32(prop, ptr, &tmp);
		if (!ptr) {
			dev_err(dev, "Property 'ranges' is 1 cell short!\n");
			iounmap(priv->gcsr);
			return -EINVAL;
		}
		arm_size = tmp;

		err = nfp_plat_bar_scan(priv, arm_addr, arm_size,
					cpp_id, cpp_addr, used);
		if (err < 0) {
			iounmap(priv->gcsr);
			return err;
		}
		if (err == BARTYPE_EXPL) {
			priv->expl_phys = arm_addr;
			priv->expl_size = arm_size;
		}
	}

	priv->expl_io = ioremap(priv->expl_phys, priv->expl_size);
	if (!priv->expl_io) {
		iounmap(priv->gcsr);
		return -ENOMEM;
	}

	/* We (ab)use part of the ARM Gasket Scratch for explicit data */
	priv->expl_data = ioremap(NFP_ARM_START + EXPL_BASE, 32 * 7);
	if (!priv->expl_data) {
		iounmap(priv->expl_io);
		iounmap(priv->gcsr);
		return -ENOMEM;
	}

	/* Clear out BULK BARs */
	for (i = 0; i < 8; i++) {
		if (!test_bit(i, used)) {
			writel(0, priv->gcsr + NFP_ARM_GCSR_BULK_BAR(i));
			readl(priv->gcsr + NFP_ARM_GCSR_BULK_BAR(i));
		}
	}

	/* Clear out EXPA BARs */
	for (i = 0; i < 16; i++) {
		if (!test_bit(8 + i, used)) {
			writel(0, priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(i));
			readl(priv->gcsr + NFP_ARM_GCSR_EXPA_BAR(i));
		}
	}

	/* Clear out EXPL BARs */
	for (i = 0; i < 8; i++) {
		if (!test_bit(8 + 16 + i, used)) {
			writel(0, priv->gcsr + NFP_ARM_GCSR_EXPL0_BAR(i));
			writel(0, priv->gcsr + NFP_ARM_GCSR_EXPL1_BAR(i));
			writel(0, priv->gcsr + NFP_ARM_GCSR_EXPL2_BAR(i));
			writel(0, priv->gcsr + NFP_ARM_GCSR_EXPL_POST(i));
		}
	}

	return 0;
}

static int nfp_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	int i, err, vnic_units;
	struct nfp_plat *priv;
	u32 model;

	of_id = of_match_device(nfp_plat_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "Failed to find devtree node\n");
		return -EFAULT;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);

	priv->target_pushpull = of_id->data;
	priv->dev = &pdev->dev;
	spin_lock_init(&priv->lock);

	err = nfp_plat_of(priv);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't initialize device\n");
		kfree(priv);
		return err;
	}

	priv->op = nfp_plat_template;
	/* We support multiple virtual channels over this interface */
	priv->op.priv = priv;

	platform_set_drvdata(pdev, priv);

	priv->cpp = nfp_cpp_from_operations(&priv->op, priv->dev);
	BUG_ON(!priv->cpp);

	model = nfp_cpp_model(priv->cpp);

	if (nfp_dev_cpp)
		priv->nfp_dev_cpp = nfp_platform_device_register(priv->cpp,
							    NFP_DEV_CPP_TYPE);

	if (nfp_net_vnic) {
		if (NFP_CPP_MODEL_IS_6000(model))
			vnic_units = 4;
		else
			vnic_units = 0;
	} else {
		vnic_units = 0;
	}

	for (i = 0; i < ARRAY_SIZE(priv->nfp_net_vnic); i++) {
		struct platform_device *pdev;

		if (i >= vnic_units)
			break;

		pdev = nfp_platform_device_register_unit(priv->cpp,
							 NFP_NET_VNIC_TYPE,
							 i, NFP_NET_VNIC_UNITS);
		priv->nfp_net_vnic[i] = pdev;
	}

	return 0;
}

static int nfp_plat_remove(struct platform_device *pdev)
{
	struct nfp_plat *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->nfp_net_vnic); i++)
		nfp_platform_device_unregister(priv->nfp_net_vnic[i]);

	nfp_platform_device_unregister(priv->nfp_dev_cpp);
	nfp_cpp_free(priv->cpp);

	iounmap(priv->expl_io);
	iounmap(priv->expl_data);
	iounmap(priv->gcsr);

	kfree(priv);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver nfp_plat_driver = {
	.probe = nfp_plat_probe,
	.remove	 = __exit_p(nfp_plat_remove),
	.driver = {
		.name = "nfp_plat",
		.of_match_table = of_match_ptr(nfp_plat_match),
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
		.owner = THIS_MODULE,
#endif
	},
};

/**
 * nfp_plat_init() - Register the NFP ARM platform driver
 */
int nfp_plat_init(void)
{
	return platform_driver_register(&nfp_plat_driver);
}

/**
 * nfp_plat_exit() - Unregister the NFP ARM platform driver
 */
void nfp_plat_exit(void)
{
	platform_driver_unregister(&nfp_plat_driver);
}

#endif /* CONFIG_ARCH_NFP */
