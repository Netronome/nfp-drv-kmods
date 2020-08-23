// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

/*
 * nfp6000_pcie.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *
 * Multiplexes the NFP BARs between NFP internal resources and
 * implements the PCIe specific interface for generic CPP bus access.
 *
 * The BARs are managed with refcounts and are allocated/acquired
 * using target, token and offset/size matching.  The generic CPP bus
 * abstraction builds upon this BAR interface.
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "nfp_cpp.h"

#include "nfp6000/nfp6000.h"

#include "nfp6000_pcie.h"

#define NFP_PCIE_BAR(_pf)	(0x30000 + ((_pf) & 7) * 0xc0)
#define NFP_PCIE_BAR_EXPLICIT_BAR0(_x, _y) \
	(0x00000080 + (0x40 * ((_x) & 0x3)) + (0x10 * ((_y) & 0x3)))
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_SignalType(_x)     (((_x) & 0x3) << 30)
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_SignalType_of(_x)  (((_x) >> 30) & 0x3)
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_Token(_x)          (((_x) & 0x3) << 28)
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_Token_of(_x)       (((_x) >> 28) & 0x3)
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_Address(_x)        (((_x) & 0xffffff) << 0)
#define   NFP_PCIE_BAR_EXPLICIT_BAR0_Address_of(_x)     (((_x) >> 0) & 0xffffff)
#define NFP_PCIE_BAR_EXPLICIT_BAR1(_x, _y) \
	(0x00000084 + (0x40 * ((_x) & 0x3)) + (0x10 * ((_y) & 0x3)))
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_SignalRef(_x)      (((_x) & 0x7f) << 24)
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_SignalRef_of(_x)   (((_x) >> 24) & 0x7f)
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_DataMaster(_x)     (((_x) & 0x3ff) << 14)
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_DataMaster_of(_x)  (((_x) >> 14) & 0x3ff)
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_DataRef(_x)        (((_x) & 0x3fff) << 0)
#define   NFP_PCIE_BAR_EXPLICIT_BAR1_DataRef_of(_x)     (((_x) >> 0) & 0x3fff)
#define NFP_PCIE_BAR_EXPLICIT_BAR2(_x, _y) \
	(0x00000088 + (0x40 * ((_x) & 0x3)) + (0x10 * ((_y) & 0x3)))
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Target(_x)         (((_x) & 0xf) << 28)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Target_of(_x)      (((_x) >> 28) & 0xf)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Action(_x)         (((_x) & 0x1f) << 23)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Action_of(_x)      (((_x) >> 23) & 0x1f)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Length(_x)         (((_x) & 0x1f) << 18)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_Length_of(_x)      (((_x) >> 18) & 0x1f)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_ByteMask(_x)       (((_x) & 0xff) << 10)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_ByteMask_of(_x)    (((_x) >> 10) & 0xff)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_SignalMaster(_x)   (((_x) & 0x3ff) << 0)
#define   NFP_PCIE_BAR_EXPLICIT_BAR2_SignalMaster_of(_x) (((_x) >> 0) & 0x3ff)

#define   NFP_PCIE_BAR_PCIE2CPP_Action_BaseAddress(_x)  (((_x) & 0x1f) << 16)
#define   NFP_PCIE_BAR_PCIE2CPP_Action_BaseAddress_of(_x) (((_x) >> 16) & 0x1f)
#define   NFP_PCIE_BAR_PCIE2CPP_BaseAddress(_x)         (((_x) & 0xffff) << 0)
#define   NFP_PCIE_BAR_PCIE2CPP_BaseAddress_of(_x)      (((_x) >> 0) & 0xffff)
#define   NFP_PCIE_BAR_PCIE2CPP_LengthSelect(_x)        (((_x) & 0x3) << 27)
#define   NFP_PCIE_BAR_PCIE2CPP_LengthSelect_of(_x)     (((_x) >> 27) & 0x3)
#define     NFP_PCIE_BAR_PCIE2CPP_LengthSelect_32BIT    0
#define     NFP_PCIE_BAR_PCIE2CPP_LengthSelect_64BIT    1
#define     NFP_PCIE_BAR_PCIE2CPP_LengthSelect_0BYTE    3
#define   NFP_PCIE_BAR_PCIE2CPP_MapType(_x)             (((_x) & 0x7) << 29)
#define   NFP_PCIE_BAR_PCIE2CPP_MapType_of(_x)          (((_x) >> 29) & 0x7)
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_FIXED         0
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_BULK          1
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_TARGET        2
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_GENERAL       3
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT0     4
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT1     5
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT2     6
#define     NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT3     7
#define   NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress(_x)  (((_x) & 0xf) << 23)
#define   NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress_of(_x) (((_x) >> 23) & 0xf)
#define   NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress(_x)   (((_x) & 0x3) << 21)
#define   NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress_of(_x) (((_x) >> 21) & 0x3)
#define NFP_PCIE_EM                                     0x020000
#define NFP_PCIE_SRAM                                   0x000000

/* Minimal size of the PCIe cfg memory we depend on being mapped,
 * queue controller and DMA controller don't have to be covered.
 */
#define NFP_PCI_MIN_MAP_SIZE				0x080000

#define NFP_PCIE_P2C_FIXED_SIZE(bar)               (1 << (bar)->bitsize)
#define NFP_PCIE_P2C_BULK_SIZE(bar)                (1 << (bar)->bitsize)
#define NFP_PCIE_P2C_GENERAL_TARGET_OFFSET(bar, x) ((x) << ((bar)->bitsize - 2))
#define NFP_PCIE_P2C_GENERAL_TOKEN_OFFSET(bar, x) ((x) << ((bar)->bitsize - 4))
#define NFP_PCIE_P2C_GENERAL_SIZE(bar)             (1 << ((bar)->bitsize - 4))

#define NFP_PCIE_CFG_BAR_PCIETOCPPEXPANSIONBAR(bar, slot) \
	(0x400 + ((bar) * 8 + (slot)) * 4)

#define NFP_PCIE_CPP_BAR_PCIETOCPPEXPANSIONBAR(bar, slot) \
	(((bar) * 8 + (slot)) * 4)

/* The number of explicit BARs to reserve.
 * Minimum is 0, maximum is 4 on the NFP6000.
 * The NFP3800 can have only one per PF.
 */
static int nfp6000_explicit_bars = 2;
module_param(nfp6000_explicit_bars, int, 0444);
MODULE_PARM_DESC(nfp6000_explicit_bars, "Number of explicit BARs (0-4)");
#define NFP_PCIE_EXPLICIT_BARS	nfp6000_explicit_bars

/* Define to enable a bit more verbose debug output. */
/* Set to 1 to enable a bit more verbose debug output. */
static int nfp6000_debug;
module_param(nfp6000_debug, int, 0644);
MODULE_PARM_DESC(nfp6000_debug, "Enable debugging for the NFP6000 PCIe");
#define NFP_PCIE_VERBOSE_DEBUG nfp6000_debug

#define nfp6000_dbg(arg...)			\
	do {					\
		if (NFP_PCIE_VERBOSE_DEBUG)	\
			dev_dbg(arg);		\
	} while (0)

struct nfp6000_pcie;
struct nfp6000_area_priv;

/**
 * struct nfp_bar - describes BAR configuration and usage
 * @nfp:	backlink to owner
 * @barcfg:	cached contents of BAR config CSR
 * @base:	the BAR's base CPP offset
 * @mask:       mask for the BAR aperture (read only)
 * @bitsize:	bitsize of BAR aperture (read only)
 * @index:	index of the BAR
 * @refcnt:	number of current users
 * @iomem:	mapped IO memory
 * @resource:	iomem resource window
 */
struct nfp_bar {
	struct nfp6000_pcie *nfp;
	u32 barcfg;
	u64 base;          /* CPP address base */
	u64 mask;          /* Bit mask of the bar */
	u32 bitsize;       /* Bit size of the bar */
	int index;
	atomic_t refcnt;

	void __iomem *iomem;
	struct resource *resource;
};

#define NFP_PCI_BAR_MAX    (PCI_64BIT_BAR_COUNT * 8)

struct nfp6000_pcie {
	struct pci_dev *pdev;
	struct device *dev;

	/* PCI BAR management */
	spinlock_t bar_lock;		/* Protect the PCI2CPP BAR cache */
	int bars;
	struct nfp_bar bar[NFP_PCI_BAR_MAX];
	wait_queue_head_t bar_waiters;

	/* Reserved BAR access */
	struct {
		void __iomem *csr;
		void __iomem *em;
		void __iomem *expl[4];
	} iomem;

	/* Explicit IO access */
	struct {
		struct mutex mutex; /* Lock access to this explicit group */
		u8 master_id;
		u8 signal_ref;
		void __iomem *data;
		struct {
			void __iomem *addr;
			int bitsize;
			int free[4];
		} group[4];
	} expl;

	/* Event management */
	struct nfp_em_manager *event;
};

static u32 nfp_bar_maptype(struct nfp_bar *bar)
{
	return NFP_PCIE_BAR_PCIE2CPP_MapType_of(bar->barcfg);
}

static resource_size_t nfp_bar_resource_len(struct nfp_bar *bar)
{
	return pci_resource_len(bar->nfp->pdev, (bar->index / 8) * 2) / 8;
}

static resource_size_t nfp_bar_resource_start(struct nfp_bar *bar)
{
	return pci_resource_start(bar->nfp->pdev, (bar->index / 8) * 2)
		+ nfp_bar_resource_len(bar) * (bar->index & 7);
}

#define TARGET_WIDTH_32    4
#define TARGET_WIDTH_64    8

static int
compute_bar(const struct nfp6000_pcie *nfp, const struct nfp_bar *bar,
	    u32 *bar_config, u64 *bar_base,
	    int tgt, int act, int tok, u64 offset, size_t size, int width)
{
	int bitsize;
	u32 newcfg;

	if (tgt >= NFP_CPP_NUM_TARGETS)
		return -EINVAL;

	switch (width) {
	case 8:
		newcfg = NFP_PCIE_BAR_PCIE2CPP_LengthSelect(
			NFP_PCIE_BAR_PCIE2CPP_LengthSelect_64BIT);
		break;
	case 4:
		newcfg = NFP_PCIE_BAR_PCIE2CPP_LengthSelect(
			NFP_PCIE_BAR_PCIE2CPP_LengthSelect_32BIT);
		break;
	case 0:
		newcfg = NFP_PCIE_BAR_PCIE2CPP_LengthSelect(
			NFP_PCIE_BAR_PCIE2CPP_LengthSelect_0BYTE);
		break;
	default:
		return -EINVAL;
	}

	if (act != NFP_CPP_ACTION_RW && act != 0) {
		/* Fixed CPP mapping with specific action */
		u64 mask = ~(NFP_PCIE_P2C_FIXED_SIZE(bar) - 1);

		newcfg |= NFP_PCIE_BAR_PCIE2CPP_MapType(
			  NFP_PCIE_BAR_PCIE2CPP_MapType_FIXED);
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress(tgt);
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_Action_BaseAddress(act);
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress(tok);

		if ((offset & mask) != ((offset + size - 1) & mask)) {
			nfp6000_dbg(nfp->dev, "BAR%d: Won't use for Fixed mapping <%#llx,%#llx>, action=%d.  BAR too small (0x%llx).\n",
				    bar->index, offset, offset + size, act,
				    (unsigned long long)mask);
			return -EINVAL;
		}
		offset &= mask;

		nfp6000_dbg(nfp->dev, "BAR%d: Created Fixed mapping %d:%d:%d:0x%#llx-0x%#llx>\n",
			    bar->index, tgt, act, tok, offset, offset + mask);

		bitsize = 40 - 16;
	} else {
		u64 mask = ~(NFP_PCIE_P2C_BULK_SIZE(bar) - 1);

		/* Bulk mapping */
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_BULK);
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress(tgt);
		newcfg |= NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress(tok);

		if ((offset & mask) != ((offset + size - 1) & mask)) {
			nfp6000_dbg(nfp->dev, "BAR%d: Won't use for bulk mapping <%#llx,%#llx>, target=%d, token=%d. BAR too small (%#llx) - (%#llx != %#llx).\n",
				    bar->index, offset, offset + size,
				    tgt, tok, mask, offset & mask,
				    (offset + size - 1) & mask);
			return -EINVAL;
		}

		offset &= mask;

		nfp6000_dbg(nfp->dev, "BAR%d: Created bulk mapping %d:x:%d:%#llx-%#llx\n",
			    bar->index, tgt, tok, offset, offset + ~mask);

		bitsize = 40 - 21;
	}

	if (bar->bitsize < bitsize) {
		nfp6000_dbg(nfp->dev, "BAR%d: Too small for %d:%d:%d\n",
			    bar->index, tgt, tok, act);
		return -EINVAL;
	}

	newcfg |= offset >> bitsize;

	if (bar_base)
		*bar_base = offset;

	if (bar_config)
		*bar_config = newcfg;

	return 0;
}

static int
nfp6000_bar_write(struct nfp6000_pcie *nfp, struct nfp_bar *bar, u32 newcfg)
{
	int base, slot;
	int xbar;

	base = bar->index >> 3;
	slot = bar->index & 7;

	if (nfp->iomem.csr) {
		xbar = NFP_PCIE_CPP_BAR_PCIETOCPPEXPANSIONBAR(base, slot);
		writel(newcfg, nfp->iomem.csr + xbar);
		/* Readback to ensure BAR is flushed */
		readl(nfp->iomem.csr + xbar);
	} else {
		xbar = NFP_PCIE_CFG_BAR_PCIETOCPPEXPANSIONBAR(base, slot);
		pci_write_config_dword(nfp->pdev, xbar, newcfg);
	}

	bar->barcfg = newcfg;

	nfp6000_dbg(nfp->dev, "BAR%d: updated to 0x%08x\n", bar->index, newcfg);

	return 0;
}

static int
reconfigure_bar(struct nfp6000_pcie *nfp, struct nfp_bar *bar,
		int tgt, int act, int tok, u64 offset, size_t size, int width)
{
	u64 newbase;
	u32 newcfg;
	int err;

	err = compute_bar(nfp, bar, &newcfg, &newbase,
			  tgt, act, tok, offset, size, width);
	if (err)
		return err;

	bar->base = newbase;

	return nfp6000_bar_write(nfp, bar, newcfg);
}

/* Check if BAR can be used with the given parameters. */
static int matching_bar(struct nfp_bar *bar, u32 tgt, u32 act, u32 tok,
			u64 offset, size_t size, int width)
{
	struct nfp6000_pcie *nfp = bar->nfp;
	u32 maptype;
	int bartgt, baract, bartok;
	int barwidth;

	maptype = NFP_PCIE_BAR_PCIE2CPP_MapType_of(bar->barcfg);
	bartgt = NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress_of(bar->barcfg);
	bartok = NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress_of(bar->barcfg);
	baract = NFP_PCIE_BAR_PCIE2CPP_Action_BaseAddress_of(bar->barcfg);

	barwidth = NFP_PCIE_BAR_PCIE2CPP_LengthSelect_of(bar->barcfg);
	switch (barwidth) {
	case NFP_PCIE_BAR_PCIE2CPP_LengthSelect_32BIT:
		barwidth = 4;
		break;
	case NFP_PCIE_BAR_PCIE2CPP_LengthSelect_64BIT:
		barwidth = 8;
		break;
	case NFP_PCIE_BAR_PCIE2CPP_LengthSelect_0BYTE:
		barwidth = 0;
		break;
	default:
		barwidth = -1;
		break;
	}

	if (NFP_PCIE_VERBOSE_DEBUG) {
		dev_dbg(nfp->dev, "BAR[%d] want: %d:%d:%d:0x%llx-0x%llx (%d bit)\n",
			bar->index, tgt, act, tok,
			offset,
			offset + size - 1, width * 8);

		switch (maptype) {
		case NFP_PCIE_BAR_PCIE2CPP_MapType_FIXED:
			dev_dbg(nfp->dev, "BAR[%d] have: %d:%d:%d:0x%llx-0x%llx (%d bit)\n",
				bar->index,
				bartgt, baract, bartok,
				bar->base,
				(bar->base + (1 << bar->bitsize) - 1),
				barwidth * 8);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_BULK:
			dev_dbg(nfp->dev, "BAR[%d] have: %d:x:%d:0x%llx-0x%llx (%d bit)\n",
				bar->index,
				bartgt, bartok,
				bar->base,
				(bar->base + (1 << bar->bitsize) - 1),
				barwidth * 8);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_TARGET:
			dev_dbg(nfp->dev, "BAR[%d] have: %d:x:-:0x%llx-0x%llx (%d bit)\n",
				bar->index,
				bartgt,
				bar->base,
				(bar->base + (1 << bar->bitsize) - 1),
				barwidth * 8);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_GENERAL:
			dev_dbg(nfp->dev, "BAR[%d] have: -:x:-:0x%llx-0x%llx (%d bit)\n",
				bar->index,
				bar->base,
				(bar->base + (1 << bar->bitsize) - 1),
				barwidth * 8);
			break;
		default:
			dev_dbg(nfp->dev, "BAR[%d] is Explicit Group %d\n",
				bar->index, maptype & 3);
			break;
		}
	}

	switch (maptype) {
	case NFP_PCIE_BAR_PCIE2CPP_MapType_TARGET:
		bartok = -1;
		fallthrough;
	case NFP_PCIE_BAR_PCIE2CPP_MapType_BULK:
		baract = NFP_CPP_ACTION_RW;
		if (act == 0)
			act = NFP_CPP_ACTION_RW;
		fallthrough;
	case NFP_PCIE_BAR_PCIE2CPP_MapType_FIXED:
		break;
	default:
		/* We don't match explicit bars through the area interface */
		return 0;
	}

	/* Make sure to match up the width */
	if (barwidth != width)
		return 0;

	if ((bartgt < 0 || bartgt == tgt) &&
	    (bartok < 0 || bartok == tok) &&
	    (baract == act) &&
	    bar->base <= offset &&
	    (bar->base + (1 << bar->bitsize)) >= (offset + size))
		return 1;

	/* No match */
	return 0;
}

static int
find_matching_bar(struct nfp6000_pcie *nfp,
		  u32 tgt, u32 act, u32 tok, u64 offset, size_t size, int width)
{
	int n;

	for (n = 0; n < nfp->bars; n++) {
		struct nfp_bar *bar = &nfp->bar[n];

		if (matching_bar(bar, tgt, act, tok, offset, size, width)) {
			nfp6000_dbg(nfp->dev, "Found matching BAR%d for <%#llx,%#llx>, target=%d, action=%d, token=%d\n",
				    bar->index, offset,
				    offset + size, tgt, act, tok);
			return n;
		}
	}

	return -1;
}

/* Return EAGAIN if no resource is available */
static int
find_unused_bar_noblock(const struct nfp6000_pcie *nfp,
			int tgt, int act, int tok,
			u64 offset, size_t size, int width)
{
	int n, busy = 0;

	for (n = 0; n < nfp->bars; n++) {
		const struct nfp_bar *bar = &nfp->bar[n];
		int err;

		if (!bar->bitsize)
			continue;

		/* Just check to see if we can make it fit... */
		err = compute_bar(nfp, bar, NULL, NULL,
				  tgt, act, tok, offset, size, width);
		if (err)
			continue;

		if (!atomic_read(&bar->refcnt))
			return n;

		busy++;
	}

	if (WARN(!busy, "No suitable BAR found for request tgt:0x%x act:0x%x tok:0x%x off:0x%llx size:%zd width:%d\n",
		 tgt, act, tok, offset, size, width))
		return -EINVAL;

	return -EAGAIN;
}

static int
find_unused_bar_and_lock(struct nfp6000_pcie *nfp,
			 int tgt, int act, int tok,
			 u64 offset, size_t size, int width)
{
	unsigned long flags;
	int n;

	spin_lock_irqsave(&nfp->bar_lock, flags);

	n = find_unused_bar_noblock(nfp, tgt, act, tok, offset, size, width);
	if (n < 0)
		spin_unlock_irqrestore(&nfp->bar_lock, flags);
	else
		__release(&nfp->bar_lock);

	return n;
}

static void nfp_bar_get(struct nfp6000_pcie *nfp, struct nfp_bar *bar)
{
	atomic_inc(&bar->refcnt);
}

static void nfp_bar_put(struct nfp6000_pcie *nfp, struct nfp_bar *bar)
{
	if (atomic_dec_and_test(&bar->refcnt))
		wake_up_interruptible(&nfp->bar_waiters);
}

static int
nfp_wait_for_bar(struct nfp6000_pcie *nfp, int *barnum,
		 u32 tgt, u32 act, u32 tok, u64 offset, size_t size, int width)
{
	return wait_event_interruptible(nfp->bar_waiters,
		(*barnum = find_unused_bar_and_lock(nfp, tgt, act, tok,
						    offset, size, width))
					!= -EAGAIN);
}

static int
nfp_alloc_bar(struct nfp6000_pcie *nfp,
	      u32 tgt, u32 act, u32 tok,
	      u64 offset, size_t size, int width, int nonblocking)
{
	unsigned long irqflags;
	int barnum, retval;

	if (size > (1 << 24))
		return -EINVAL;

	spin_lock_irqsave(&nfp->bar_lock, irqflags);
	barnum = find_matching_bar(nfp, tgt, act, tok, offset, size, width);
	if (barnum >= 0) {
		/* Found a perfect match. */
		nfp_bar_get(nfp, &nfp->bar[barnum]);
		spin_unlock_irqrestore(&nfp->bar_lock, irqflags);
		return barnum;
	}

	barnum = find_unused_bar_noblock(nfp, tgt, act, tok,
					 offset, size, width);
	if (barnum < 0) {
		if (nonblocking)
			goto err_nobar;

		/* Wait until a BAR becomes available.  The
		 * find_unused_bar function will reclaim the bar_lock
		 * if a free BAR is found.
		 */
		spin_unlock_irqrestore(&nfp->bar_lock, irqflags);
		retval = nfp_wait_for_bar(nfp, &barnum, tgt, act, tok,
					  offset, size, width);
		if (retval)
			return retval;
		__acquire(&nfp->bar_lock);
	}

	nfp_bar_get(nfp, &nfp->bar[barnum]);
	retval = reconfigure_bar(nfp, &nfp->bar[barnum],
				 tgt, act, tok, offset, size, width);
	if (retval < 0) {
		nfp_bar_put(nfp, &nfp->bar[barnum]);
		barnum = retval;
	}

err_nobar:
	spin_unlock_irqrestore(&nfp->bar_lock, irqflags);
	return barnum;
}

/*
 * Sysfs interface for dumping the configuration of the BARs.
 */
static ssize_t show_barcfg(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	static char *bartype[8] = {
		"Fixed", "Bulk", "Target", "General",
		"Expl0", "Expl1", "Expl2", "Expl3"
	};
	struct nfp6000_pcie *nfp = nfp_cpp_priv(dev_get_drvdata(dev));
	int n, maptype, tgtact, tgttok, length, action;
	ssize_t off = 0;
	u64 base;

	for (n = 0; n < nfp->bars; n++) {
		struct nfp_bar *bar = &nfp->bar[n];
		int users = atomic_read(&bar->refcnt);

		if (users == 0 && !NFP_PCIE_VERBOSE_DEBUG)
			continue;

		maptype = NFP_PCIE_BAR_PCIE2CPP_MapType_of(
				bar->barcfg);
		tgtact = NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress_of(
				    bar->barcfg);
		tgttok = NFP_PCIE_BAR_PCIE2CPP_Token_BaseAddress_of(
				bar->barcfg);
		action = NFP_PCIE_BAR_PCIE2CPP_Action_BaseAddress_of(
				bar->barcfg);
		length = NFP_PCIE_BAR_PCIE2CPP_LengthSelect_of(
				bar->barcfg) ? 64 : 32;
		base = NFP_PCIE_BAR_PCIE2CPP_BaseAddress_of(
				bar->barcfg);

		off += scnprintf(buf + off, PAGE_SIZE - off,
				 "BAR%d(%d): %s map, ",
				 bar->index, bar->bitsize, bartype[maptype]);
		switch (maptype) {
		case NFP_PCIE_BAR_PCIE2CPP_MapType_FIXED:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "target: %#x, token: %#x, action: %#x, ",
					 tgtact, tgttok, action);
			base <<= (40 - 16);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_BULK:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "target: %#x, token: %#x, ",
					 tgtact, tgttok);
			base |= (u64)action << 16;
			base <<= (40 - 21);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_TARGET:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "target: %#x, ",
					 tgtact);
			base |= (u64)tgttok << 21;
			base |= (u64)action << 16;
			base <<= (40 - 23);
			break;
		case NFP_PCIE_BAR_PCIE2CPP_MapType_GENERAL:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "action: %#x, ", tgtact);
			base |= (u64)tgtact << 23;
			base |= (u64)tgttok << 21;
			base |= (u64)action << 16;
			base <<= (40 - 27);
			break;
		default:
			break;
		}
		off += scnprintf(buf + off, PAGE_SIZE - off,
				"%d-bit, base: %#llx, users: %d\n", length,
				 base, users);
	}

	return off;
}

static DEVICE_ATTR(barcfg, S_IRUGO, show_barcfg, NULL);

static int nfp6000_pciebars_attr_add(struct device *dev)
{
	return device_create_file(dev, &dev_attr_barcfg);
}

static void nfp6000_pciebars_attr_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_barcfg);
}

static void disable_bars(struct nfp6000_pcie *nfp);

static int bar_cmp(const void *aptr, const void *bptr)
{
	const struct nfp_bar *a = aptr, *b = bptr;

	if (a->bitsize == b->bitsize)
		return a->index - b->index;
	else
		return a->bitsize - b->bitsize;
}

/* Map all PCI bars and fetch the actual BAR configurations from the
 * board.  We assume that the BAR with the PCIe config block is
 * already mapped.
 *
 * BAR0.0: Reserved for General Mapping (for MSI-X access to PCIe SRAM)
 * BAR0.1: Reserved for XPB access (for MSI-X access to PCIe PBA)
 * BAR0.2: --
 * BAR0.3: --
 * BAR0.4: Reserved for Explicit 0.0-0.3 access
 * BAR0.5: Reserved for Explicit 1.0-1.3 access
 * BAR0.6: Reserved for Explicit 2.0-2.3 access
 * BAR0.7: Reserved for Explicit 3.0-3.3 access
 *
 * BAR1.0-BAR1.7: --
 * BAR2.0-BAR2.7: --
 */
static int enable_bars(struct nfp6000_pcie *nfp, u16 interface)
{
	const u32 barcfg_msix_general =
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_GENERAL) |
		NFP_PCIE_BAR_PCIE2CPP_LengthSelect_32BIT;
	const u32 barcfg_msix_xpb =
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_BULK) |
		NFP_PCIE_BAR_PCIE2CPP_LengthSelect_32BIT |
		NFP_PCIE_BAR_PCIE2CPP_Target_BaseAddress(
			NFP_CPP_TARGET_ISLAND_XPB);
	const u32 barcfg_explicit[4] = {
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT0),
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT1),
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT2),
		NFP_PCIE_BAR_PCIE2CPP_MapType(
			NFP_PCIE_BAR_PCIE2CPP_MapType_EXPLICIT3),
	};
	char status_msg[196] = {};
	int i, err, bars_free;
	struct nfp_bar *bar;
	int expl_groups;
	char *msg, *end;

	msg = status_msg +
		snprintf(status_msg, sizeof(status_msg) - 1, "RESERVED BARs: ");
	end = status_msg + sizeof(status_msg) - 1;

	bar = &nfp->bar[0];
	for (i = 0; i < ARRAY_SIZE(nfp->bar); i++, bar++) {
		struct resource *res;

		res = &nfp->pdev->resource[(i >> 3) * 2];

		/* Skip over BARs that are not IORESOURCE_MEM */
		if (!(resource_type(res) & IORESOURCE_MEM)) {
			bar--;
			continue;
		}

		bar->resource = res;
		bar->barcfg = 0;

		bar->nfp = nfp;
		bar->index = i;
		bar->mask = nfp_bar_resource_len(bar) - 1;
		bar->bitsize = fls(bar->mask);
		bar->base = 0;
		bar->iomem = NULL;
	}

	nfp->bars = bar - &nfp->bar[0];
	if (nfp->bars < 8) {
		dev_err(nfp->dev, "No usable BARs found!\n");
		return -EINVAL;
	}

	bars_free = nfp->bars;

	/* Convert unit ID (0..3) to signal master/data master ID (0x40..0x70)
	 */
	mutex_init(&nfp->expl.mutex);

	nfp->expl.master_id = ((NFP_CPP_INTERFACE_UNIT_of(interface) & 3) + 4)
		<< 4;
	nfp->expl.signal_ref = 0x10;

	/* Configure, and lock, BAR0.0 for General Target use (MSI-X SRAM) */
	bar = &nfp->bar[0];
	if (nfp_bar_resource_len(bar) >= NFP_PCI_MIN_MAP_SIZE)
		bar->iomem = ioremap(nfp_bar_resource_start(bar),
				     nfp_bar_resource_len(bar));
	if (bar->iomem) {
		int pf;

		msg += scnprintf(msg, end - msg, "0.0: General/MSI-X SRAM, ");
		atomic_inc(&bar->refcnt);
		bars_free--;

		nfp6000_bar_write(nfp, bar, barcfg_msix_general);

		nfp->expl.data = bar->iomem + NFP_PCIE_SRAM + 0x1000;

		switch (nfp->pdev->device) {
		case PCI_DEVICE_ID_NETRONOME_NFP3800:
			pf = nfp->pdev->devfn & 7;
			nfp->iomem.csr = bar->iomem + NFP_PCIE_BAR(pf);
			break;
		case PCI_DEVICE_ID_NETRONOME_NFP4000:
		case PCI_DEVICE_ID_NETRONOME_NFP5000:
		case PCI_DEVICE_ID_NETRONOME_NFP6000:
			nfp->iomem.csr = bar->iomem + NFP_PCIE_BAR(0);
			break;
		default:
			dev_err(nfp->dev, "Unsupported device ID: %04hx!\n",
				nfp->pdev->device);
			err = -EINVAL;
			goto err_unmap_bar0;
		}
		nfp->iomem.em = bar->iomem + NFP_PCIE_EM;
	}

	switch (nfp->pdev->device) {
	case PCI_DEVICE_ID_NETRONOME_NFP3800:
		expl_groups = 1;
		break;
	case PCI_DEVICE_ID_NETRONOME_NFP4000:
	case PCI_DEVICE_ID_NETRONOME_NFP5000:
	case PCI_DEVICE_ID_NETRONOME_NFP6000:
		expl_groups = 4;
		break;
	default:
		dev_err(nfp->dev, "Unsupported device ID: %04hx!\n",
			nfp->pdev->device);
		err = -EINVAL;
		goto err_unmap_bar0;
	}

	/* Configure, and lock, BAR0.1 for PCIe XPB (MSI-X PBA) */
	bar = &nfp->bar[1];
	msg += scnprintf(msg, end - msg, "0.1: PCIe XPB/MSI-X PBA, ");
	atomic_inc(&bar->refcnt);
	bars_free--;

	nfp6000_bar_write(nfp, bar, barcfg_msix_xpb);

	/* Use BAR0.4..BAR0.7 for EXPL IO */
	for (i = 0; i < 4; i++) {
		int j;

		if (i >= NFP_PCIE_EXPLICIT_BARS || i >= expl_groups) {
			nfp->expl.group[i].bitsize = 0;
			continue;
		}

		bar = &nfp->bar[4 + i];
		bar->iomem = ioremap(nfp_bar_resource_start(bar),
				     nfp_bar_resource_len(bar));
		if (bar->iomem) {
			msg += scnprintf(msg, end - msg,
					 "0.%d: Explicit%d, ", 4 + i, i);
			atomic_inc(&bar->refcnt);
			bars_free--;

			nfp->expl.group[i].bitsize = bar->bitsize;
			nfp->expl.group[i].addr = bar->iomem;
			nfp6000_bar_write(nfp, bar, barcfg_explicit[i]);

			for (j = 0; j < 4; j++)
				nfp->expl.group[i].free[j] = true;
		}
		nfp->iomem.expl[i] = bar->iomem;
	}

	/* Sort bars by bit size - use the smallest possible first. */
	sort(&nfp->bar[0], nfp->bars, sizeof(nfp->bar[0]),
	     bar_cmp, NULL);

	dev_info(nfp->dev, "%sfree: %d/%d\n", status_msg, bars_free, nfp->bars);

	return 0;

err_unmap_bar0:
	if (nfp->bar[0].iomem)
		iounmap(nfp->bar[0].iomem);
	return err;
}

static void disable_bars(struct nfp6000_pcie *nfp)
{
	struct nfp_bar *bar = &nfp->bar[0];
	int n;

	for (n = 0; n < nfp->bars; n++, bar++) {
		if (bar->iomem) {
			iounmap(bar->iomem);
			bar->iomem = NULL;
		}
	}
}

/*
 * Generic CPP bus access interface.
 */

struct nfp6000_area_priv {
	atomic_t refcnt;

	struct nfp_bar *bar;
	u32 bar_offset;

	u32 target;
	u32 action;
	u32 token;
	u64 offset;
	struct {
		int read;
		int write;
		int bar;
	} width;
	size_t size;

	void __iomem *iomem;
	phys_addr_t phys;
	struct resource resource;
};

static int nfp6000_area_init(struct nfp_cpp_area *area, u32 dest,
			     unsigned long long address, unsigned long size)
{
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);
	u32 target = NFP_CPP_ID_TARGET_of(dest);
	u32 action = NFP_CPP_ID_ACTION_of(dest);
	u32 token = NFP_CPP_ID_TOKEN_of(dest);
	int pp;

	pp = nfp_target_pushpull(NFP_CPP_ID(target, action, token), address);
	if (pp < 0)
		return pp;

	priv->width.read = PUSH_WIDTH(pp);
	priv->width.write = PULL_WIDTH(pp);
	if (priv->width.read > 0 &&
	    priv->width.write > 0 &&
	    priv->width.read != priv->width.write) {
		return -EINVAL;
	}

	if (priv->width.read > 0)
		priv->width.bar = priv->width.read;
	else
		priv->width.bar = priv->width.write;

	atomic_set(&priv->refcnt, 0);
	priv->bar = NULL;

	priv->target = target;
	priv->action = action;
	priv->token = token;
	priv->offset = address;
	priv->size = size;
	memset(&priv->resource, 0, sizeof(priv->resource));

	return 0;
}

static void nfp6000_area_cleanup(struct nfp_cpp_area *area)
{
}

static void priv_area_get(struct nfp_cpp_area *area)
{
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	atomic_inc(&priv->refcnt);
}

static int priv_area_put(struct nfp_cpp_area *area)
{
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	if (WARN_ON(!atomic_read(&priv->refcnt)))
		return 0;

	return atomic_dec_and_test(&priv->refcnt);
}

static int nfp6000_area_acquire(struct nfp_cpp_area *area)
{
	struct nfp6000_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);
	int barnum, err;

	if (priv->bar) {
		/* Already allocated. */
		priv_area_get(area);
		return 0;
	}

	barnum = nfp_alloc_bar(nfp, priv->target, priv->action, priv->token,
			       priv->offset, priv->size, priv->width.bar, 1);

	if (barnum < 0) {
		nfp6000_dbg(nfp->dev, "Failed to allocate bar %d:%d:%d:0x%llx: %d\n",
			    priv->target, priv->action,
			    priv->token, priv->offset, barnum);
		err = barnum;
		goto err_alloc_bar;
	}
	priv->bar = &nfp->bar[barnum];

	/* Calculate offset into BAR. */
	if (nfp_bar_maptype(priv->bar) ==
	    NFP_PCIE_BAR_PCIE2CPP_MapType_GENERAL) {
		priv->bar_offset = priv->offset &
			(NFP_PCIE_P2C_GENERAL_SIZE(priv->bar) - 1);
		priv->bar_offset += NFP_PCIE_P2C_GENERAL_TARGET_OFFSET(
			priv->bar, priv->target);
		priv->bar_offset += NFP_PCIE_P2C_GENERAL_TOKEN_OFFSET(
			priv->bar, priv->token);
	} else {
		priv->bar_offset = priv->offset & priv->bar->mask;
	}

	/* We don't actually try to acquire the resource area using
	 * request_resource.  This would prevent sharing the mapped
	 * BAR between multiple CPP areas and prevent us from
	 * effectively utilizing the limited amount of BAR resources.
	 */
	priv->phys = nfp_bar_resource_start(priv->bar) + priv->bar_offset;
	priv->resource.name = nfp_cpp_area_name(area);
	priv->resource.start = priv->phys;
	priv->resource.end = priv->resource.start + priv->size - 1;
	priv->resource.flags = IORESOURCE_MEM;

	/* If the bar is already mapped in, use its mapping */
	if (priv->bar->iomem)
		priv->iomem = priv->bar->iomem + priv->bar_offset;
	else
		/* Must have been too big. Sub-allocate. */
		priv->iomem = ioremap(priv->phys, priv->size);

	if (IS_ERR_OR_NULL(priv->iomem)) {
		dev_err(nfp->dev, "Can't ioremap() a %d byte region of BAR %d\n",
			(int)priv->size, priv->bar->index);
		err = !priv->iomem ? -ENOMEM : PTR_ERR(priv->iomem);
		priv->iomem = NULL;
		goto err_iomem_remap;
	}

	priv_area_get(area);
	return 0;

err_iomem_remap:
	nfp_bar_put(nfp, priv->bar);
	priv->bar = NULL;
err_alloc_bar:
	return err;
}

static void nfp6000_area_release(struct nfp_cpp_area *area)
{
	struct nfp6000_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	if (!priv_area_put(area))
		return;

	if (!priv->bar->iomem)
		iounmap(priv->iomem);

	nfp_bar_put(nfp, priv->bar);

	priv->bar = NULL;
	priv->iomem = NULL;
}

static phys_addr_t nfp6000_area_phys(struct nfp_cpp_area *area)
{
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->phys;
}

static void __iomem *nfp6000_area_iomem(struct nfp_cpp_area *area)
{
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->iomem;
}

static struct resource *nfp6000_area_resource(struct nfp_cpp_area *area)
{
	/* Use the BAR resource as the resource for the CPP area.
	 * This enables us to share the BAR among multiple CPP areas
	 * without resource conflicts.
	 */
	struct nfp6000_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->bar->resource;
}

static int nfp6000_area_read(struct nfp_cpp_area *area, void *kernel_vaddr,
			     unsigned long offset, unsigned int length)
{
	u64 __maybe_unused *wrptr64 = kernel_vaddr;
	const u64 __iomem __maybe_unused *rdptr64;
	struct nfp6000_area_priv *priv;
	u32 *wrptr32 = kernel_vaddr;
	const u32 __iomem *rdptr32;
	int n, width;

	priv = nfp_cpp_area_priv(area);
	rdptr64 = priv->iomem + offset;
	rdptr32 = priv->iomem + offset;

	if (offset + length > priv->size)
		return -EFAULT;

	width = priv->width.read;
	if (width <= 0)
		return -EINVAL;

	/* MU reads via a PCIe2CPP BAR support 32bit (and other) lengths */
	if (priv->target == (NFP_CPP_TARGET_MU & NFP_CPP_TARGET_ID_MASK) &&
	    priv->action == NFP_CPP_ACTION_RW &&
	    (offset % sizeof(u64) == 4 || length % sizeof(u64) == 4))
		width = TARGET_WIDTH_32;

	/* Unaligned? Translate to an explicit access */
	if ((priv->offset + offset) & (width - 1))
		return nfp_cpp_explicit_read(nfp_cpp_area_cpp(area),
					     NFP_CPP_ID(priv->target,
							priv->action,
							priv->token),
					     priv->offset + offset,
					     kernel_vaddr, length, width);

	if (WARN_ON(!priv->bar))
		return -EFAULT;

	switch (width) {
	case TARGET_WIDTH_32:
		if (offset % sizeof(u32) != 0 || length % sizeof(u32) != 0)
			return -EINVAL;

		for (n = 0; n < length; n += sizeof(u32))
			*wrptr32++ = __raw_readl(rdptr32++);
		return n;
#ifdef __raw_readq
	case TARGET_WIDTH_64:
		if (offset % sizeof(u64) != 0 || length % sizeof(u64) != 0)
			return -EINVAL;

		for (n = 0; n < length; n += sizeof(u64))
			*wrptr64++ = __raw_readq(rdptr64++);
		return n;
#endif
	default:
		return -EINVAL;
	}
}

static int
nfp6000_area_write(struct nfp_cpp_area *area,
		   const void *kernel_vaddr,
		   unsigned long offset, unsigned int length)
{
	const u64 __maybe_unused *rdptr64 = kernel_vaddr;
	u64 __iomem __maybe_unused *wrptr64;
	const u32 *rdptr32 = kernel_vaddr;
	struct nfp6000_area_priv *priv;
	u32 __iomem *wrptr32;
	int n, width;

	priv = nfp_cpp_area_priv(area);
	wrptr64 = priv->iomem + offset;
	wrptr32 = priv->iomem + offset;

	if (offset + length > priv->size)
		return -EFAULT;

	width = priv->width.write;
	if (width <= 0)
		return -EINVAL;

	/* MU writes via a PCIe2CPP BAR support 32bit (and other) lengths */
	if (priv->target == (NFP_CPP_TARGET_ID_MASK & NFP_CPP_TARGET_MU) &&
	    priv->action == NFP_CPP_ACTION_RW &&
	    (offset % sizeof(u64) == 4 || length % sizeof(u64) == 4))
		width = TARGET_WIDTH_32;

	/* Unaligned? Translate to an explicit access */
	if ((priv->offset + offset) & (width - 1))
		return nfp_cpp_explicit_write(nfp_cpp_area_cpp(area),
					      NFP_CPP_ID(priv->target,
							 priv->action,
							 priv->token),
					      priv->offset + offset,
					      kernel_vaddr, length, width);

	if (WARN_ON(!priv->bar))
		return -EFAULT;

	switch (width) {
	case TARGET_WIDTH_32:
		if (offset % sizeof(u32) != 0 || length % sizeof(u32) != 0)
			return -EINVAL;

		for (n = 0; n < length; n += sizeof(u32)) {
			__raw_writel(*rdptr32++, wrptr32++);
			wmb();
		}
		return n;
#ifdef __raw_writeq
	case TARGET_WIDTH_64:
		if (offset % sizeof(u64) != 0 || length % sizeof(u64) != 0)
			return -EINVAL;

		for (n = 0; n < length; n += sizeof(u64)) {
			__raw_writeq(*rdptr64++, wrptr64++);
			wmb();
		}
		return n;
#endif
	default:
		return -EINVAL;
	}
}

struct nfp6000_explicit_priv {
	struct nfp6000_pcie *nfp;
	struct {
		int group;
		int area;
	} bar;
	int bitsize;
	void __iomem *data;
	void __iomem *addr;
};

static int nfp6000_explicit_acquire(struct nfp_cpp_explicit *expl)
{
	struct nfp6000_pcie *nfp = nfp_cpp_priv(nfp_cpp_explicit_cpp(expl));
	struct nfp6000_explicit_priv *priv = nfp_cpp_explicit_priv(expl);
	int i, j;

	mutex_lock(&nfp->expl.mutex);
	for (i = 0; i < ARRAY_SIZE(nfp->expl.group); i++) {
		if (!nfp->expl.group[i].bitsize)
			continue;

		for (j = 0; j < ARRAY_SIZE(nfp->expl.group[i].free); j++) {
			u16 data_offset;

			if (!nfp->expl.group[i].free[j])
				continue;

			priv->nfp = nfp;
			priv->bar.group = i;
			priv->bar.area = j;
			priv->bitsize = nfp->expl.group[i].bitsize - 2;

			data_offset = (priv->bar.group << 9) +
				(priv->bar.area << 7);
			priv->data = nfp->expl.data + data_offset;
			priv->addr = nfp->expl.group[i].addr +
				(priv->bar.area << priv->bitsize);
			nfp->expl.group[i].free[j] = false;

			mutex_unlock(&nfp->expl.mutex);
			return 0;
		}
	}
	mutex_unlock(&nfp->expl.mutex);

	return -EAGAIN;
}

static void nfp6000_explicit_release(struct nfp_cpp_explicit *expl)
{
	struct nfp6000_explicit_priv *priv = nfp_cpp_explicit_priv(expl);
	struct nfp6000_pcie *nfp = priv->nfp;

	mutex_lock(&nfp->expl.mutex);
	nfp->expl.group[priv->bar.group].free[priv->bar.area] = true;
	mutex_unlock(&nfp->expl.mutex);
}

static int nfp6000_explicit_put(struct nfp_cpp_explicit *expl,
				const void *buff, size_t len)
{
	struct nfp6000_explicit_priv *priv = nfp_cpp_explicit_priv(expl);
	const u32 *src = buff;
	size_t i;

	for (i = 0; i < len; i += sizeof(u32))
		writel(*(src++), priv->data + i);

	return i;
}

static int
nfp6000_explicit_do(struct nfp_cpp_explicit *expl,
		    const struct nfp_cpp_explicit_command *cmd, u64 address)
{
	struct nfp6000_explicit_priv *priv = nfp_cpp_explicit_priv(expl);
	u8 signal_master, signal_ref, data_master;
	struct nfp6000_pcie *nfp = priv->nfp;
	int sigmask = 0;
	u16 data_ref;
	u32 csr[3];

	if (cmd->siga_mode)
		sigmask |= 1 << cmd->siga;
	if (cmd->sigb_mode)
		sigmask |= 1 << cmd->sigb;

	signal_master = cmd->signal_master;
	if (!signal_master)
		signal_master = nfp->expl.master_id;

	signal_ref = cmd->signal_ref;
	if (signal_master == nfp->expl.master_id)
		signal_ref = nfp->expl.signal_ref +
			((priv->bar.group * 4 + priv->bar.area) << 1);

	data_master = cmd->data_master;
	if (!data_master)
		data_master = nfp->expl.master_id;

	data_ref = cmd->data_ref;
	if (data_master == nfp->expl.master_id)
		data_ref = 0x1000 +
			(priv->bar.group << 9) + (priv->bar.area << 7);

	csr[0] = NFP_PCIE_BAR_EXPLICIT_BAR0_SignalType(sigmask) |
		NFP_PCIE_BAR_EXPLICIT_BAR0_Token(
			NFP_CPP_ID_TOKEN_of(cmd->cpp_id)) |
		NFP_PCIE_BAR_EXPLICIT_BAR0_Address(address >> 16);

	csr[1] = NFP_PCIE_BAR_EXPLICIT_BAR1_SignalRef(signal_ref) |
		NFP_PCIE_BAR_EXPLICIT_BAR1_DataMaster(data_master) |
		NFP_PCIE_BAR_EXPLICIT_BAR1_DataRef(data_ref);

	csr[2] = NFP_PCIE_BAR_EXPLICIT_BAR2_Target(
			NFP_CPP_ID_TARGET_of(cmd->cpp_id)) |
		NFP_PCIE_BAR_EXPLICIT_BAR2_Action(
			NFP_CPP_ID_ACTION_of(cmd->cpp_id)) |
		NFP_PCIE_BAR_EXPLICIT_BAR2_Length(cmd->len) |
		NFP_PCIE_BAR_EXPLICIT_BAR2_ByteMask(cmd->byte_mask) |
		NFP_PCIE_BAR_EXPLICIT_BAR2_SignalMaster(signal_master);

	if (NFP_PCIE_VERBOSE_DEBUG) {
		int i;

		for (i = 0; i < 3; i++)
			dev_dbg(nfp->dev, "EXPL%d.%d: BAR%d = 0x%08x\n",
				priv->bar.group, priv->bar.area, i, csr[i]);
	}

	if (nfp->iomem.csr) {
		writel(csr[0], nfp->iomem.csr +
		       NFP_PCIE_BAR_EXPLICIT_BAR0(priv->bar.group,
						  priv->bar.area));
		writel(csr[1], nfp->iomem.csr +
		       NFP_PCIE_BAR_EXPLICIT_BAR1(priv->bar.group,
						  priv->bar.area));
		writel(csr[2], nfp->iomem.csr +
		       NFP_PCIE_BAR_EXPLICIT_BAR2(priv->bar.group,
						  priv->bar.area));
		/* Readback to ensure BAR is flushed */
		readl(nfp->iomem.csr +
		      NFP_PCIE_BAR_EXPLICIT_BAR0(priv->bar.group,
						 priv->bar.area));
		readl(nfp->iomem.csr +
		      NFP_PCIE_BAR_EXPLICIT_BAR1(priv->bar.group,
						 priv->bar.area));
		readl(nfp->iomem.csr +
		      NFP_PCIE_BAR_EXPLICIT_BAR2(priv->bar.group,
						 priv->bar.area));
	} else {
		pci_write_config_dword(nfp->pdev, 0x400 +
				       NFP_PCIE_BAR_EXPLICIT_BAR0(
					       priv->bar.group, priv->bar.area),
				       csr[0]);

		pci_write_config_dword(nfp->pdev, 0x400 +
				       NFP_PCIE_BAR_EXPLICIT_BAR1(
					       priv->bar.group, priv->bar.area),
				       csr[1]);

		pci_write_config_dword(nfp->pdev, 0x400 +
				       NFP_PCIE_BAR_EXPLICIT_BAR2(
					       priv->bar.group, priv->bar.area),
				       csr[2]);
	}

	nfp6000_dbg(nfp->dev, "EXPL%d.%d: Kickoff 0x%llx (@0x%08x)\n",
		    priv->bar.group, priv->bar.area, address,
		    (unsigned int)(address & ((1 << priv->bitsize) - 1)));

	/* Issue the 'kickoff' transaction */
	readb(priv->addr + (address & ((1 << priv->bitsize) - 1)));

	return sigmask;
}

static int nfp6000_explicit_get(struct nfp_cpp_explicit *expl,
				void *buff, size_t len)
{
	struct nfp6000_explicit_priv *priv = nfp_cpp_explicit_priv(expl);
	u32 *dst = buff;
	size_t i;

	for (i = 0; i < len; i += sizeof(u32))
		*(dst++) = readl(priv->data + i);

	return i;
}

static int nfp6000_init(struct nfp_cpp *cpp)
{
	nfp_cpp_area_cache_add(cpp, SZ_64K);
	nfp_cpp_area_cache_add(cpp, SZ_64K);
	nfp_cpp_area_cache_add(cpp, SZ_256K);

	return nfp6000_pciebars_attr_add(nfp_cpp_device(cpp));
}

struct nfp6000_event_priv {
	int filter;
};

static int nfp6000_event_acquire(struct nfp_cpp_event *event, u32 match,
				 u32 mask, u32 type)
{
	struct nfp6000_event_priv *ev = nfp_cpp_event_priv(event);
	struct nfp_cpp *cpp = nfp_cpp_event_cpp(event);
	struct nfp6000_pcie *nfp = nfp_cpp_priv(cpp);
	int filter;

	filter = nfp_em_manager_acquire(nfp->event, event, match, mask, type);
	if (filter < 0)
		return filter;

	ev->filter = filter;

	return 0;
}

static void nfp6000_event_release(struct nfp_cpp_event *event)
{
	struct nfp_cpp *cpp = nfp_cpp_event_cpp(event);
	struct nfp6000_pcie *nfp = nfp_cpp_priv(cpp);
	struct nfp6000_event_priv *ev = nfp_cpp_event_priv(event);

	nfp_em_manager_release(nfp->event, ev->filter);
}

static void nfp6000_free(struct nfp_cpp *cpp)
{
	struct nfp6000_pcie *nfp = nfp_cpp_priv(cpp);

	nfp6000_pciebars_attr_remove(nfp_cpp_device(cpp));
	nfp_em_manager_destroy(nfp->event);
	disable_bars(nfp);
	kfree(nfp);
}

static int nfp6000_read_serial(struct device *dev, u8 *serial)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u64 dsn;

	dsn = pci_get_dsn(pdev);
	if (!dsn) {
		dev_err(dev, "can't find PCIe Serial Number Capability\n");
		return -EINVAL;
	}

	put_unaligned_be32((u32)(dsn >> 32), serial);
	put_unaligned_be16((u16)(dsn >> 16), serial + 4);

	return 0;
}

static int nfp6000_get_interface(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u64 dsn;

	dsn = pci_get_dsn(pdev);
	if (!dsn) {
		dev_err(dev, "can't find PCIe Serial Number Capability\n");
		return -EINVAL;
	}

	return dsn & 0xffff;
}

static const struct nfp_cpp_operations nfp6000_pcie_ops = {
	.owner			= THIS_MODULE,

	.init			= nfp6000_init,
	.free			= nfp6000_free,

	.read_serial		= nfp6000_read_serial,
	.get_interface		= nfp6000_get_interface,

	.area_priv_size		= sizeof(struct nfp6000_area_priv),
	.area_init		= nfp6000_area_init,
	.area_cleanup		= nfp6000_area_cleanup,
	.area_acquire		= nfp6000_area_acquire,
	.area_release		= nfp6000_area_release,
	.area_phys		= nfp6000_area_phys,
	.area_iomem		= nfp6000_area_iomem,
	.area_resource		= nfp6000_area_resource,
	.area_read		= nfp6000_area_read,
	.area_write		= nfp6000_area_write,

	.explicit_priv_size	= sizeof(struct nfp6000_explicit_priv),
	.explicit_acquire	= nfp6000_explicit_acquire,
	.explicit_release	= nfp6000_explicit_release,
	.explicit_put		= nfp6000_explicit_put,
	.explicit_do		= nfp6000_explicit_do,
	.explicit_get		= nfp6000_explicit_get,

	.event_priv_size	= sizeof(struct nfp6000_event_priv),
	.event_acquire		= nfp6000_event_acquire,
	.event_release		= nfp6000_event_release,
};

/**
 * nfp_cpp_from_nfp6000_pcie() - Build a NFP CPP bus from a NFP6000 PCI device
 * @pdev:	NFP6000 PCI device
 * @event_irq:	IRQ bound to the event manager (optional)
 *
 * Return: NFP CPP handle
 */
struct nfp_cpp *nfp_cpp_from_nfp6000_pcie(struct pci_dev *pdev, int event_irq)
{
	struct nfp6000_pcie *nfp;
	u16 interface;
	int err;

	/*  Finished with card initialization. */
	dev_info(&pdev->dev,
		 "Netronome Flow Processor NFP4000/NFP5000/NFP6000 PCIe Card Probe\n");
	pcie_print_link_status(pdev);

	nfp = kzalloc(sizeof(*nfp), GFP_KERNEL);
	if (!nfp) {
		err = -ENOMEM;
		goto err_ret;
	}

	nfp->dev = &pdev->dev;
	nfp->pdev = pdev;
	init_waitqueue_head(&nfp->bar_waiters);
	spin_lock_init(&nfp->bar_lock);

	interface = nfp6000_get_interface(&pdev->dev);

	if (NFP_CPP_INTERFACE_TYPE_of(interface) !=
	    NFP_CPP_INTERFACE_TYPE_PCI) {
		dev_err(&pdev->dev,
			"Interface type %d is not the expected %d\n",
			NFP_CPP_INTERFACE_TYPE_of(interface),
			NFP_CPP_INTERFACE_TYPE_PCI);
		err = -ENODEV;
		goto err_free_nfp;
	}

	if (NFP_CPP_INTERFACE_CHANNEL_of(interface) !=
	    NFP_CPP_INTERFACE_CHANNEL_PEROPENER) {
		dev_err(&pdev->dev, "Interface channel %d is not the expected %d\n",
			NFP_CPP_INTERFACE_CHANNEL_of(interface),
			NFP_CPP_INTERFACE_CHANNEL_PEROPENER);
		err = -ENODEV;
		goto err_free_nfp;
	}

	err = enable_bars(nfp, interface);
	if (err)
		goto err_free_nfp;

	if (nfp->iomem.em && event_irq >= 0) {
		nfp->event = nfp_em_manager_create(nfp->iomem.em, event_irq);
		if (IS_ERR_OR_NULL(nfp->event)) {
			err = nfp->event ? PTR_ERR(nfp->event) : -ENOMEM;
			goto err_bar_disable;
		}
	}

	/* Probe for all the common NFP devices */
	return nfp_cpp_from_operations(&nfp6000_pcie_ops, &pdev->dev, nfp);

err_bar_disable:
	disable_bars(nfp);
err_free_nfp:
	kfree(nfp);
err_ret:
	dev_err(&pdev->dev, "NFP6000 PCI setup failed\n");
	return ERR_PTR(err);
}
