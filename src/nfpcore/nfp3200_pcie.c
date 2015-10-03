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
 * nfp3200_pcie.c
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Espen Skoglund <espen.skoglund@netronome.com>
 *
 * Multiplexes the NFP BARs between NFP internal resources and
 * implements the PCIe specific interface for generic CPP bus access.
 *
 * The BARs are managed with refcounts and are allocated/acquired
 * using target, token and offset/size matching.  The generic CPP bus
 * abstraction builds upon this BAR interface.
 */

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include "nfp_cpp.h"
#include "nfp_target.h"

#include "nfp3200/nfp3200.h"
#include "nfp3200/nfp_em.h"
#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_pl.h"

#include "nfp3200_pcie.h"

/* Add your architecture here if it cannot
 * perform atomic readq()/writeq() transactions over
 * the PCI bus.
 */
#if defined(CONFIG_X86_32) || (defined(CONFIG_PPC) && !defined(CONFIG_PPC64))
#define CONFIG_NFP_PCI32
#endif

static const char nfp3200_pcie_driver_name[] = "nfp3200_pcie";

/* The 64-bit BAR mapping containing the PCIe target registers. */
#define NFP_PCIETGT_BAR_INDEX	1

/* Set to 1 to enable a bit more verbose debug output. */
static int nfp3200_debug;
module_param(nfp3200_debug, int, 0644);
#define NFP_PCIE_VERBOSE_DEBUG nfp3200_debug

#define NFP_XPB_PCIE_CSR        NFP_XPB_DEST(17, 1)
#define NFP_PCIE_BARCFG_P2C(_bar)      (0x30000 + (0x4 * (_bar)))
#define   NFP_PCIE_BARCFG_P2C_BASE(_x)                  ((_x) & 0x3fffff)
#define   NFP_PCIE_BARCFG_P2C_BASE_of(_x)               ((_x) & 0x3fffff)
#define   NFP_PCIE_BARCFG_P2C_LEN                       (0x1 << 22)
#define     NFP_PCIE_BARCFG_P2C_LEN_32BIT               (0x0)
#define     NFP_PCIE_BARCFG_P2C_LEN_64BIT               (0x400000)
#define   NFP_PCIE_BARCFG_P2C_MAPTYPE(_x)               (((_x) & 0x3) << 30)
#define   NFP_PCIE_BARCFG_P2C_MAPTYPE_of(_x)            (((_x) >> 30) & 0x3)
#define     NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK            (0)
#define     NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP             (1)
#define   NFP_PCIE_BARCFG_P2C_TGTACT(_x)                (((_x) & 0x1f) << 25)
#define   NFP_PCIE_BARCFG_P2C_TGTACT_of(_x)             (((_x) >> 25) & 0x1f)
#define   NFP_PCIE_BARCFG_P2C_TOKACTSEL(_x)             (((_x) & 0x3) << 23)
#define   NFP_PCIE_BARCFG_P2C_TOKACTSEL_of(_x)          (((_x) >> 23) & 0x3)
#define     NFP_PCIE_BARCFG_P2C_TOKACTSEL_BARCFG        (2)
#define     NFP_PCIE_BARCFG_P2C_TOKACTSEL_NONE          (0)
#define NFP_PCIE_CSR_CFG0              0x0000
#define   NFP_PCIE_CSR_CFG0_TARGET_BUS(_x)              (((_x) & 0xff) << 8)
#define   NFP_PCIE_DMA_CMD_CPL(_x)                      (((_x) & 0xffff) << 16)
#define   NFP_PCIE_DMA_CMD_CPP_ADDR_HI(_x)              ((_x) & 0xff)
#define   NFP_PCIE_DMA_CMD_CPP_ADDR_LO(_x)              (_x)
#define   NFP_PCIE_DMA_CMD_CPP_TARGET(_x)               (((_x) & 0xf) << 8)
#define NFP_PCIE_DMA_CMD_FPCI_HI       0x40020
#define NFP_PCIE_DMA_CMD_FPCI_LO       0x40030
#define   NFP_PCIE_DMA_CMD_LENGTH(_x)                   (((_x) & 0xfff) << 20)
#define   NFP_PCIE_DMA_CMD_PCIE_ADDR_HI(_x)             ((_x) & 0xff)
#define   NFP_PCIE_DMA_CMD_PCIE_ADDR_LO(_x)             (_x)
#define     NFP_PCIE_DMA_CMD_TARGET64_ENABLE            (0x1000)
#define   NFP_PCIE_DMA_CMD_TOKEN(_x)                    (((_x) & 0x3) << 14)
#define NFP_PCIE_DMA_CMD_TPCI_HI       0x40000
#define NFP_PCIE_DMA_CMD_TPCI_LO       0x40010
#define NFP_PCIE_DMA_CTRL_FPCI_HI      0x40048
#define NFP_PCIE_DMA_CTRL_FPCI_LO      0x4004c
#define     NFP_PCIE_DMA_CTRL_QUEUE_STOP_ENABLE         (0x1)
#define NFP_PCIE_DMA_CTRL_TPCI_HI      0x40040
#define NFP_PCIE_DMA_CTRL_TPCI_LO      0x40044
#define NFP_PCIE_DMA_QSTS_FPCI_HI      0x40048
#define NFP_PCIE_DMA_QSTS_FPCI_LO      0x4004c
#define   NFP_PCIE_DMA_QSTS_QUEUE_ACTIVE                (0x1 << 6)
#define NFP_PCIE_DMA_QSTS_TPCI_HI      0x40040
#define NFP_PCIE_DMA_QSTS_TPCI_LO      0x40044
#define NFP_PCIE_EM                    0x20000
#define NFP_PCIE_P2C_CPPMAP_SIZE        (1 << 18)
#define NFP_PCIE_P2C_CPPMAP_TARGET_OFFSET(x)    ((x) << 20)
#define NFP_PCIE_P2C_CPPMAP_TOKEN_OFFSET(x) ((x) << 18)
struct nfp3200_pcie;
struct nfp_cpp_area_priv;

/*
 * struct nfp_bar - describes BAR configuration and usage
 * @nfp:	backlink to owner
 * @barcfg:	cached contents of BAR config CSR
 * @offset:	the BAR's base CPP offset
 * @mask:	mask for the BAR aperture (read only)
 * @bitsize:	bitsize of BAR aperture (read only)
 * @index:	index of the BAR
 * @refcnt:	number of current users
 * @iomem:	mapped IO memory
 * @resource:	iomem resource window
 */
struct nfp_bar {
	struct nfp3200_pcie *nfp;
	u32 barcfg;
	u64 offset;
	u64 mask;
	u32 bitsize;
	int index;
	atomic_t refcnt;

	void __iomem *iomem;
	struct resource *resource;
};

struct nfp3200_pcie {
	struct pci_dev *pdev;
	struct device *dev;

	struct nfp_cpp_operations ops;

	void __iomem *pcietgt;

	/* Revision specific workarounds */
#define NFP_A1_WORKAROUND	BIT(0)
#define NFP_A_WORKAROUND	BIT(1)
	u32 workaround;
	struct {
		u32 vnic_base;
		struct nfp_cpp_area *internal_write_area;
		void __iomem *pciewr;
	} a1;

	/* PCI BAR management */
	spinlock_t bar_lock;	/* Lock for the PCI2CPP BAR cache */
	struct nfp_bar bars[PCI_64BIT_BAR_COUNT];
	wait_queue_head_t bar_waiters;

#ifdef CONFIG_NFP_PCI32
	struct mutex dma_lock;	/* Mutex to single-thread DMA access */
	dma_addr_t dma_dev_addr;
	void *dma_cpu_addr;
	u32 dma_qstatus[4];
	unsigned dma_unavailable:1;
#endif

	/* Event management */
	struct nfp_em_manager *event;
};

static inline u32 read_pcie_csr(struct nfp3200_pcie *nfp, u32 off)
{
	/* We deem this to be safe since we don't do it very often */
	return readl(nfp->pcietgt + off);
}

static inline void write_pcie_csr(struct nfp3200_pcie *nfp, u32 off, u32 val)
{
	/*
	 * If write during init with internal target not set up yet,
	 * or write to offset outside of mapped area we have to use
	 * the internal PCIe target.  This should not be a problem for
	 * normal operation; only for the cases where we have a 32bit
	 * system and need to program the DMA engine to access 64bit
	 * CPP targets will we require internal writes (and reads).
	 */
	if (nfp->workaround & NFP_A1_WORKAROUND) {
		u32 cls_offset = NFP_PCIE_P2C_CPPMAP_TARGET_OFFSET(
				NFP_CPP_TARGET_LOCAL_SCRATCH);

		if (!nfp->a1.pciewr || off >= NFP_PCIE_P2C_CPPMAP_SIZE)
			writel(val, nfp->pcietgt + off);
		else
			writel(val, nfp->a1.pciewr + off);

		/* Flush write (required for A1) */
		readl(nfp->bars[NFP_PCIETGT_BAR_INDEX].iomem + cls_offset);
	} else {
		writel(val, nfp->pcietgt + off);
		readl(nfp->pcietgt + off);
	}
}

static inline u32 nfp_bar_maptype(struct nfp_bar *bar)
{
	return NFP_PCIE_BARCFG_P2C_MAPTYPE_of(bar->barcfg);
}

static resource_size_t nfp_bar_resource_start(struct nfp_bar *bar)
{
	return pci_resource_start(bar->nfp->pdev, bar->index * 2);
}

static resource_size_t nfp_bar_resource_len(struct nfp_bar *bar)
{
	return pci_resource_len(bar->nfp->pdev, bar->index * 2);
}

#define CPPMAP_MASK ((u64)(NFP_PCIE_P2C_CPPMAP_SIZE - 1))

static int reconfigure_bar(struct nfp3200_pcie *nfp, int barnum, u32 tgt,
			   u32 act, u32 tok, u64 offset, size_t size, u8 width)
{
	struct nfp_bar *bar = &nfp->bars[barnum];
	u32 newcfg;

	BUG_ON(barnum >= ARRAY_SIZE(nfp->bars));
	BUG_ON(tgt >= NFP_CPP_NUM_TARGETS);

	if (width == 8)
		newcfg = NFP_PCIE_BARCFG_P2C_LEN_64BIT;
	else
		newcfg = NFP_PCIE_BARCFG_P2C_LEN_32BIT;

	if (act != NFP_CPP_ACTION_RW && act != 0) {
		/* Generic CPP mapping with specific action */
		newcfg |= NFP_PCIE_BARCFG_P2C_MAPTYPE(
			  NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP);
		newcfg |= NFP_PCIE_BARCFG_P2C_TGTACT(act);
		newcfg |= NFP_PCIE_BARCFG_P2C_TOKACTSEL(
			NFP_PCIE_BARCFG_P2C_TOKACTSEL_BARCFG);

		if ((((offset & CPPMAP_MASK) + size - 1) & ~CPPMAP_MASK) != 0) {
			dev_dbg(nfp->dev, "Failed to create CPP mapping <%#llx,%#llx>, action=%d.  BAR too small.\n",
				offset, offset + size, act);
			return -EINVAL;
		}
		offset &= ~CPPMAP_MASK;

		if (NFP_PCIE_VERBOSE_DEBUG) {
			dev_dbg(nfp->dev, "Create CPP mapping for BAR%d at <%#llx,%#llx>, %d:[%d]:%d:0\n",
				barnum, offset,
				offset + NFP_PCIE_P2C_CPPMAP_SIZE,
				tgt, act, tok);
		}
	} else if (offset < NFP_PCIE_P2C_CPPMAP_SIZE &&
		   (offset + size - 1) < NFP_PCIE_P2C_CPPMAP_SIZE) {
		/* Generic CPP mapping */
		newcfg |= NFP_PCIE_BARCFG_P2C_MAPTYPE(
			  NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP);
		newcfg |= NFP_PCIE_BARCFG_P2C_TGTACT(0);
		newcfg |= NFP_PCIE_BARCFG_P2C_TOKACTSEL(
			NFP_PCIE_BARCFG_P2C_TOKACTSEL_NONE);

		if ((((offset & CPPMAP_MASK) + size - 1) & ~CPPMAP_MASK) != 0) {
			dev_dbg(nfp->dev, "Failed to create CPP mapping <%#llx,%#llx>.  BAR too small.\n",
				offset, offset + size);
			return -EINVAL;
		}
		offset &= ~CPPMAP_MASK;

		if (NFP_PCIE_VERBOSE_DEBUG) {
			dev_dbg(nfp->dev, "Create CPP mapping for BAR%d at <%#llx,%#llx>, %d:%d:%d:0\n",
				barnum, offset,
				offset + NFP_PCIE_P2C_CPPMAP_SIZE,
				tgt, act, tok);
		}
	} else {
		/* Bulk mapping */
		newcfg |= NFP_PCIE_BARCFG_P2C_MAPTYPE(
			NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK);
		newcfg |= NFP_PCIE_BARCFG_P2C_TGTACT(tgt);
		newcfg |= NFP_PCIE_BARCFG_P2C_TOKACTSEL(tok);

		if ((((offset & bar->mask) + size - 1) & ~bar->mask) != 0) {
			dev_dbg(nfp->dev, "Failed to create bulk mapping <%#llx,%#llx>, [%d]:%d:[%d].  BAR too small.\n",
				offset, offset + size, tgt, act, tok);
			return -EINVAL;
		}
		offset &= ~bar->mask;

		if (NFP_PCIE_VERBOSE_DEBUG) {
			dev_dbg(nfp->dev, "Create bulk mapping for BAR%d at <%#llx,%#llx>, [%d]:%d:[%d]\n",
				barnum, offset,	offset + (1 << bar->bitsize),
				tgt, act, tok);
		}
	}

	bar->offset = offset;
	newcfg |= NFP_PCIE_BARCFG_P2C_BASE(offset >> (40 - 22));

	BUG_ON(barnum == NFP_PCIETGT_BAR_INDEX);

	write_pcie_csr(nfp, NFP_PCIE_BARCFG_P2C(barnum), newcfg);
	if (nfp->workaround & NFP_A1_WORKAROUND) {
		bar->barcfg = newcfg;
	} else {
		bar->barcfg = read_pcie_csr(nfp, NFP_PCIE_BARCFG_P2C(barnum));
		if (bar->barcfg != newcfg) {
			dev_err(nfp->dev, "Could not access NFP via PCIE\n");
			return -ENODEV;
		}
	}
	return 0;
}

/*
 * Check if BAR can be used with the given parameters.
 */
static int matching_bar(struct nfp_bar *bar, u32 tgt, u32 act, u32 tok,
			u64 offset, u8 width, size_t size)
{
	u32 maptype, tgtact, tokactsel;
	struct nfp3200_pcie *nfp = bar->nfp;

	maptype = NFP_PCIE_BARCFG_P2C_MAPTYPE_of(bar->barcfg);
	tgtact = NFP_PCIE_BARCFG_P2C_TGTACT_of(bar->barcfg);
	tokactsel = NFP_PCIE_BARCFG_P2C_TOKACTSEL_of(bar->barcfg);

	if (NFP_PCIE_VERBOSE_DEBUG) {
		dev_dbg(nfp->dev, "BAR[%d] want: %d:%d:%d:0x%llx-0x%llx (%d bit)\n",
			bar->index, tgt, act, tok,
			(unsigned long long)offset,
			(unsigned long long)offset + size - 1, width * 8);

		if (maptype == NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK) {
			dev_dbg(nfp->dev, "BAR[%d] BULK %d:-:%d:0x%llx-0x%llx (%d bit)\n",
				bar->index, tgtact, tokactsel,
				(unsigned long long)bar->offset,
				(unsigned long long)(bar->offset +
						     (1 << bar->bitsize) - 1),
				(bar->barcfg & NFP_PCIE_BARCFG_P2C_LEN_64BIT)
					? 64 : 32);
		} else {
			int barlen = (bar->barcfg &
				NFP_PCIE_BARCFG_P2C_LEN_64BIT) ? 64 : 32;
			switch (tokactsel) {
			case 0:
				dev_dbg(nfp->dev, "BAR[%d] CPP -:x:-:0x%llx-0x%llx (%d bit)\n",
					bar->index,
					bar->offset,
					bar->offset +
						NFP_PCIE_P2C_CPPMAP_SIZE - 1,
					barlen);
				break;
			case 1:
				dev_dbg(nfp->dev, "BAR[%d] CPP -:-:-:0x%llx-0x%llx (%d bit)\n",
					bar->index,
					bar->offset,
					bar->offset +
						NFP_PCIE_P2C_CPPMAP_SIZE - 1,
					barlen);
				break;
			default:
				dev_dbg(nfp->dev, "BAR[%d] CPP -:%d:-:0x%llx-0x%llx (%d bit)\n",
					bar->index, tgtact,
					bar->offset,
					bar->offset +
						NFP_PCIE_P2C_CPPMAP_SIZE - 1,
					barlen);
				break;
			}
		}
	}

	/* Make sure to match up the 64-bit flag */
	if (((bar->barcfg & NFP_PCIE_BARCFG_P2C_LEN_64BIT) ? 1 : 0) !=
		((width == 8) ? 1 : 0))
		return 0;

	/*
	 * Bulk mappings must contain the correct target in the BAR
	 * configuration.
	 */
	if (maptype == NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK &&
	    tgtact == tgt && tokactsel == tok &&
	    bar->offset <= offset &&
	    (act == NFP_CPP_ACTION_RW || act == 0) &&
	    (bar->offset + (1ull << bar->bitsize)) >= (offset + size))
		return 1;

	/* Special case for 'Target 0' access to the PCIe interface */
	if (maptype == NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP &&
	    tgt == 0 && tok == 0 &&
	    (act == NFP_CPP_ACTION_RW || act == 0 || act == 1) &&
	    ((offset + size) <= (NFP_PCIE_P2C_CPPMAP_SIZE << 2))) {
		return 1;
	}

	/*
	 * CPP mappings encode the target and token in the PCIe
	 * address.  Only the lower 18 bits of the PCIe address are
	 * used for the offset (we disregard 30bit BARs with action
	 * selection not set to 1).
	 */
	if (maptype == NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP &&
	    bar->offset <= offset &&
	    (bar->offset + NFP_PCIE_P2C_CPPMAP_SIZE) >= (offset + size)) {
		if (act == NFP_CPP_ACTION_RW || act == 0)
			return tokactsel == NFP_PCIE_BARCFG_P2C_TOKACTSEL_NONE;
		return (tokactsel == NFP_PCIE_BARCFG_P2C_TOKACTSEL_BARCFG) &&
			(tgtact == act);
	}

	/* We don't support mixed mappings for now. */
	return 0;
}

static inline int bar_search_order(int n)
{
	return (n == 0) ? 1 : (n == 1) ? 2 : 0;
}

static int _find_matching_bar(struct nfp3200_pcie *nfp, u32 tgt, u32 act,
			      u32 tok, u64 offset, u8 width, size_t size,
			      int prefetchable)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(nfp->bars); n++) {
		struct nfp_bar *bar = &nfp->bars[bar_search_order(n)];

		if (bar->resource->flags & IORESOURCE_PREFETCH) {
			if (!prefetchable)
				continue;
		} else {
			if (prefetchable)
				continue;
		}

		if (matching_bar(bar, tgt, act, tok, offset, width, size)) {
			if (NFP_PCIE_VERBOSE_DEBUG) {
				dev_dbg(nfp->dev, "Found matching BAR%d for <%#llx,%#llx>, target=%d, action=%d, token=%d\n",
					bar_search_order(n), offset,
					offset + size, tgt, act, tok);
			}
			return bar_search_order(n);
		}
	}

	return -1;
}

/* If the tgt/act/tok is prefetchable, try the IORESOURCE_PREFETCH BARs first,
 */
static int find_matching_bar(struct nfp3200_pcie *nfp, u32 tgt, u32 act,
			     u32 tok, u64 offset, u8 width, size_t size)
{
	int prefetchable;
	int err = -1;

	prefetchable = __nfp_cpp_id_is_prefetchable(NFP_CPP_ID(tgt, act, tok));
	if (prefetchable)
		err = _find_matching_bar(nfp, tgt, act, tok,
					 offset, width, size, 1);
	if (err < 0)
		err = _find_matching_bar(nfp, tgt, act, tok,
					 offset, width, size, 0);

	return err;
}

/* Return -EAGAIN is no slot is available
 */
static int _find_unused_bar_noblock(struct nfp3200_pcie *nfp, int prefetchable)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(nfp->bars); n++) {
		struct nfp_bar *bar = &nfp->bars[bar_search_order(n)];

		if (prefetchable &&
		    (bar->resource->flags & IORESOURCE_PREFETCH) == 0)
			continue;

		if (atomic_read(&bar->refcnt) == 0)
			return bar_search_order(n);
	}

	return -EAGAIN;
}

static int find_unused_bar_noblock(struct nfp3200_pcie *nfp,
				   int tgt, int act, int tok)
{
	int prefetchable;
	int err = -EAGAIN;

	prefetchable = __nfp_cpp_id_is_prefetchable(NFP_CPP_ID(tgt, act, tok));
	if (prefetchable)
		err = _find_unused_bar_noblock(nfp, 1);
	if (err < 0)
		err = _find_unused_bar_noblock(nfp, 0);

	return err;
}

/* Return -EAGAIN is no slot is available
 */
static int find_unused_bar_and_lock(struct nfp3200_pcie *nfp,
				    int tgt, int act, int tok)
{
	int n;
	unsigned long flags;

	spin_lock_irqsave(&nfp->bar_lock, flags);

	n = find_unused_bar_noblock(nfp, tgt, act, tok);
	if (n < 0)
		spin_unlock_irqrestore(&nfp->bar_lock, flags);

	return n;
}

static inline void nfp_bar_get(struct nfp3200_pcie *nfp, struct nfp_bar *bar)
{
	atomic_inc(&bar->refcnt);
}

static inline void nfp_bar_put(struct nfp3200_pcie *nfp, struct nfp_bar *bar)
{
	if (atomic_dec_and_test(&bar->refcnt))
		wake_up_interruptible(&nfp->bar_waiters);
}

/* Allocate BAR.
 * Return -EAGAIN is there is no available slot
 */
static int nfp_alloc_bar(struct nfp3200_pcie *nfp, u32 tgt, u32 act, u32 tok,
			 u64 offset, u8 width, size_t size, int nonblocking)
{
	int barnum, retval;
	u64 size2;
	unsigned long irqflags;

	if (size > (1 << 24))
		return -EINVAL;

	/*
	 * Make sure that size is sufficiently large and that offset
	 * is properly aligned.
	 */
	for (size2 = 1; size2 < size; size2 <<= 1)
		;
	while ((offset & ~(size2 - 1)) != ((offset + size - 1) & ~(size2 - 1)))
		size2 <<= 1;
	offset &= ~(size2 - 1);

	spin_lock_irqsave(&nfp->bar_lock, irqflags);
	barnum = find_matching_bar(nfp, tgt, act, tok, offset, width, size2);
	if (barnum >= 0) {
		/* Found a perfect match. */
		nfp_bar_get(nfp, &nfp->bars[barnum]);
		spin_unlock_irqrestore(&nfp->bar_lock, irqflags);
		return barnum;
	}

	barnum = find_unused_bar_noblock(nfp, tgt, act, tok);
	if (barnum < 0) {
		if (nonblocking)
			goto err_nobar;

		/*
		 * Wait until a BAR becomes available.  The
		 * find_unused_bar function will reclaim the bar_lock
		 * if a free BAR is found.
		 */
		spin_unlock_irqrestore(&nfp->bar_lock, irqflags);
		retval = wait_event_interruptible(
			nfp->bar_waiters,
			-EAGAIN !=
			(barnum = find_unused_bar_and_lock(nfp,
							   tgt, act, tok)));
		if (retval)
			return retval;
	}

	nfp_bar_get(nfp, &nfp->bars[barnum]);
	retval = reconfigure_bar(nfp, barnum, tgt, act, tok, offset,
				 size2, width);
	if (retval < 0) {
		nfp_bar_put(nfp, &nfp->bars[barnum]);
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
	static char *bartype[4] = { "bulk", "CPP", "mixed", "invalid" };
	struct nfp3200_pcie *nfp = nfp_cpp_priv(dev_get_drvdata(dev));
	struct nfp_bar *bar = nfp->bars;
	int n, maptype, tgtact, tokactsel, length;
	ssize_t off = 0;
	u64 base;

	BUG_ON(!nfp);
	for (n = 0; n < ARRAY_SIZE(nfp->bars); n++, bar++) {
		maptype = NFP_PCIE_BARCFG_P2C_MAPTYPE_of(bar->barcfg) & 0x3;
		tgtact = NFP_PCIE_BARCFG_P2C_TGTACT_of(bar->barcfg);
		tokactsel = NFP_PCIE_BARCFG_P2C_TOKACTSEL_of(bar->barcfg);
		length = (bar->barcfg & NFP_PCIE_BARCFG_P2C_LEN_64BIT)
			? 64 : 32;
		base = ((u64)NFP_PCIE_BARCFG_P2C_BASE_of(bar->barcfg))	<<
			(40 - 22);

		off += scnprintf(buf + off, PAGE_SIZE - off,
				 "BAR%d: %s map, ", n, bartype[maptype]);
		switch (maptype) {
		case NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "target: %#x, token: %#x, ",
					 tgtact, tokactsel);
			break;
		case NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP:
			if (tokactsel == 2 || tokactsel == 3)
				off += scnprintf(buf + off, PAGE_SIZE - off,
						 "action: %#x, ", tgtact);
			break;
		default:
			off += scnprintf(buf + off, PAGE_SIZE - off,
					 "tgtact: %#x, tokactsel: %#x, ",
					 tgtact, tokactsel);
			break;
		}
		off += scnprintf(buf + off, PAGE_SIZE - off,
				"%d-bit, base: %#llx, users: %d\n", length,
				 base, atomic_read(&bar->refcnt));
	}

	return off;
}

static DEVICE_ATTR(barcfg, S_IRUGO, show_barcfg, NULL);

static void disable_bars(struct nfp3200_pcie *nfp);

/*
 * Map all PCI bars and fetch the actual BAR configurations from the
 * board.  We assume that the BAR with the PCIe config block is
 * already mapped.
 */
static int enable_bars(struct nfp3200_pcie *nfp)
{
	struct nfp_bar *bar = nfp->bars;
	u32 barcfg;
	int n, retval = 0;

	BUG_ON(!nfp->dev);
	BUG_ON(!nfp->pdev);

	for (n = 0; n < ARRAY_SIZE(nfp->bars); n++, bar++) {
		bar->resource = &nfp->pdev->resource[n * 2];
		bar->barcfg = read_pcie_csr(nfp, NFP_PCIE_BARCFG_P2C(n));

		bar->nfp = nfp;
		bar->index = n;
		bar->mask = nfp_bar_resource_len(bar) - 1;
		bar->offset = NFP_PCIE_BARCFG_P2C_BASE_of(bar->barcfg)
			<< (40 - 22);

		/* The PCIe target BAR must be immutable. */
		atomic_set(&bar->refcnt, (n == NFP_PCIETGT_BAR_INDEX) ? 1 : 0);

		if (bar->mask == ((1 << 24) - 1)) {
			bar->bitsize = 24;
		} else if (bar->mask == ((1 << 30) - 1)) {
			bar->bitsize = 30;
		} else {
			dev_err(nfp->dev, "Invalid BAR%d size: %#x\n",
				n, (u32)bar->mask + 1);
			retval = -EIO;
			goto error;
		}

		/* Mask out unused base address bits. */
		if (NFP_PCIE_BARCFG_P2C_MAPTYPE_of(bar->barcfg) ==
			NFP_PCIE_BARCFG_P2C_MAPTYPE_BULK)
			bar->offset &= ~bar->mask;
		else if (NFP_PCIE_BARCFG_P2C_MAPTYPE_of(bar->barcfg) ==
			 NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP &&
			 bar->bitsize == 30 &&
			 NFP_PCIE_BARCFG_P2C_TOKACTSEL_of(bar->barcfg) != 1)
			bar->offset &= ~(0xffffUL << 24);

		/* This is permitted to fail, and is expected
		 * to fail for 1G bars on ARM or 32-bit x86
		 */
		bar->iomem = devm_ioremap_nocache(
				&nfp->pdev->dev,
				nfp_bar_resource_start(bar),
				1 << bar->bitsize);

		if (IS_ERR_OR_NULL(bar->iomem)) {
			dev_err(&nfp->pdev->dev,
				"BAR%d: Can't map in the %uKB area\n",
				n, (1 << bar->bitsize) / 1024);
			bar->iomem = NULL;
		}
	}

	/*
	 * Sanity checking for the BAR1 configuration.  This is the
	 * BAR which is supposed to map the PCIe block so that we can
	 * reconfigure the BARs.
	 */
	barcfg = nfp->bars[NFP_PCIETGT_BAR_INDEX].barcfg;
	if (NFP_PCIE_BARCFG_P2C_MAPTYPE_of(barcfg) !=
		NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP ||
		NFP_PCIE_BARCFG_P2C_TOKACTSEL_of(barcfg) != 0 ||
		NFP_PCIE_BARCFG_P2C_BASE_of(barcfg) != 0) {
		dev_err(nfp->dev, "Invalid BAR%d configuration 0x%x\n",
			NFP_PCIETGT_BAR_INDEX, barcfg);
		retval = -EIO;
		goto error;
	}

	return 0;

error:
	disable_bars(nfp);
	return retval;
}

static void disable_bars(struct nfp3200_pcie *nfp)
{
	struct nfp_bar *bar = nfp->bars;
	int n;

	for (n = 0; n < ARRAY_SIZE(nfp->bars); n++, bar++) {
		if (bar->iomem)
			devm_iounmap(&nfp->pdev->dev, bar->iomem);
	}
}

/*
 * Generic CPP bus access interface.
 */

struct nfp_cpp_area_priv {
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

	struct nfp_cpp_area_priv *next;
};

static int nfp_cpp_pcie_area_init(
	struct nfp_cpp_area *area, u32 dest,
	unsigned long long address, unsigned long size)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	u32 target = NFP_CPP_ID_TARGET_of(dest);
	u32 action = NFP_CPP_ID_ACTION_of(dest);
	u32 token = NFP_CPP_ID_TOKEN_of(dest);
	int pp;

	/* This was in nfp_cpp_read/write previously, but the NFP-6xxx CPP Area
	 * API can handle unaligned access so we move the 4-byte alignment
	 * check into the CPP Area implementation that requires it.
	 */
	if ((address % sizeof(u32)) != 0 ||
	    (size % sizeof(u32)) != 0)
		return -EINVAL;

	/* Special 'Target 0' case */
	if (target == 0 &&
	    (action == NFP_CPP_ACTION_RW || action == 0 || action == 1) &&
	    token == 0 &&
	    (address + size) <= (NFP_PCIE_P2C_CPPMAP_SIZE << 2))
		pp = PUSHPULL(P32, P32);
	else
		pp = nfp3200_target_pushpull(dest, address);

	if (pp < 0) {
		if (NFP_PCIE_VERBOSE_DEBUG) {
			dev_dbg(nfp->dev, "Can't detemermine push/pull sizes for %d:%d:%d:0x%llx: (0x%x)\n",
				target, action, token, address,
				(unsigned)size);
		}
		return pp;
	}

	priv->width.read = PUSH_WIDTH(pp);
	priv->width.write = PULL_WIDTH(pp);

	/* Special exception for the 2nd 4K page of DDR on
	 * A0/A1 hardware. This is 'known safe' for that
	 * specific use case (nfp_net_vnic), and prevents
	 * the locking of all three BARs by the default NFP driver.
	 */
	if (nfp->workaround & NFP_A1_WORKAROUND) {
		if (target == NFP_CPP_TARGET_MU &&
		    (action == NFP_CPP_ACTION_RW ||
		     action == 0 ||
		     action == 1) &&
		    (address >= nfp->a1.vnic_base &&
		     (address + size) <= (nfp->a1.vnic_base + 0x1000))) {
			if (action == NFP_CPP_ACTION_RW || action == 0)
				priv->width.read = 4;
			if (action == NFP_CPP_ACTION_RW || action == 1)
				priv->width.write = 4;
		}
	}

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
	priv->next = NULL;
	memset(&priv->resource, 0, sizeof(priv->resource));

	return 0;
}

static void nfp_cpp_pcie_area_cleanup(struct nfp_cpp_area *area)
{
}

static void priv_area_get(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);

	atomic_inc(&priv->refcnt);
}

static int priv_area_put(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);

	BUG_ON(!atomic_read(&priv->refcnt));
	return atomic_dec_and_test(&priv->refcnt);
}

/*
 * Acquire area.
 * Return -EGAIN if there is no available slot.
 */
static int nfp_cpp_pcie_area_acquire(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	int barnum, err;

	if (priv->bar) {
		/* Already allocated. */
		priv_area_get(area);
		BUG_ON(!priv->iomem);
		return 0;
	}

	barnum = nfp_alloc_bar(nfp, priv->target, priv->action, priv->token,
			       priv->offset, priv->width.bar, priv->size, 1);

	if (barnum < 0) {
		if (NFP_PCIE_VERBOSE_DEBUG) {
			dev_dbg(nfp->dev, "Failed to allocate bar %d:%d:%d:0x%llx: %d\n",
				priv->target, priv->action, priv->token,
				priv->offset, barnum);
		}
		err = barnum;
		goto err_alloc_bar;
	}
	priv->bar = &nfp->bars[barnum];

	/* Calculate offset into BAR. */
	if (nfp_bar_maptype(priv->bar) == NFP_PCIE_BARCFG_P2C_MAPTYPE_CPP) {
		/* Special case for 'Target 0' access */
		if (priv->target == 0)
			priv->bar_offset =
				priv->offset &
				((NFP_PCIE_P2C_CPPMAP_SIZE << 2) - 1);
		else
			priv->bar_offset =
				priv->offset & (NFP_PCIE_P2C_CPPMAP_SIZE - 1);
		priv->bar_offset +=
			NFP_PCIE_P2C_CPPMAP_TARGET_OFFSET(priv->target);
		priv->bar_offset +=
			NFP_PCIE_P2C_CPPMAP_TOKEN_OFFSET(priv->token);
	} else {
		priv->bar_offset = priv->offset & priv->bar->mask;
	}

	/*
	 * We don't actually try to acquire the resource area using
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
	if (priv->bar->iomem) {
		priv->iomem = priv->bar->iomem + priv->bar_offset;
	} else {
		/* Must have been too big. Sub-allocate. */
		priv->iomem = devm_ioremap_nocache(
			&nfp->pdev->dev, priv->phys, priv->size);
	}
	if (IS_ERR_OR_NULL(priv->iomem)) {
		dev_err(nfp->dev, "Can't ioremap() a %d byte region of BAR %d\n",
			(int)priv->size, barnum);
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

static void nfp_cpp_pcie_area_release(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));

	BUG_ON(!priv->bar);
	BUG_ON(!priv->iomem);

	if (priv_area_put(area)) {
		if (!priv->bar->iomem)
			devm_iounmap(&nfp->pdev->dev, priv->iomem);

		nfp_bar_put(nfp, priv->bar);

		priv->bar = NULL;
		priv->iomem = NULL;
	}
}

static phys_addr_t nfp_cpp_pcie_area_phys(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->phys;
}

static void __iomem *nfp_cpp_pcie_area_iomem(struct nfp_cpp_area *area)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->iomem;
}

static struct resource *nfp_cpp_pcie_area_resource(struct nfp_cpp_area *area)
{
	/*
	 * Use the BAR resource as the resource for the CPP area.
	 * This enables us to share the BAR among multiple CPP areas
	 * without resource conflicts.
	 */
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);

	return priv->bar->resource;
}

#ifdef CONFIG_NFP_PCI32
/*
 * In some cases the PCIe host is not able to perform 64bit read/write
 * accesses over the PCI bus.  If so, we use the NFP3200 PCIe DMA engine
 * to perform 64-bit accesses to the CPP targets.  However, to ensure
 * that there is no conflict with the microengines also using the DMA
 * engine we only do so unless we see that someone else has been using
 * the DMA engines.  This is done by inspecting that the qstatus of
 * all queues are the same as we left them last time.
 */

#define NFP_DMA_MAX	(4096 - 8)

enum dmaq {
	DMAQ_TPCI_HI = 0, DMAQ_TPCI_LO, DMAQ_FPCI_HI, DMAQ_FPCI_LO,
};

static u32 dmaq_cmd[4] = {
	NFP_PCIE_DMA_CMD_TPCI_HI, NFP_PCIE_DMA_CMD_TPCI_LO,
	NFP_PCIE_DMA_CMD_FPCI_HI, NFP_PCIE_DMA_CMD_FPCI_LO,
};

static u32 dmaq_ctrl[4] = {
	NFP_PCIE_DMA_CTRL_TPCI_HI, NFP_PCIE_DMA_CTRL_TPCI_LO,
	NFP_PCIE_DMA_CTRL_FPCI_HI, NFP_PCIE_DMA_CTRL_FPCI_LO,
};

static u32 dmaq_qsts[4] = {
	NFP_PCIE_DMA_QSTS_TPCI_HI, NFP_PCIE_DMA_QSTS_TPCI_LO,
	NFP_PCIE_DMA_QSTS_FPCI_HI, NFP_PCIE_DMA_QSTS_FPCI_LO,
};

static int nfp_dma_available(struct nfp3200_pcie *nfp)
{
	u32 n, qsts;

	if (nfp->dma_unavailable)
		return -EIO;

	for (n = 0; n < ARRAY_SIZE(nfp->dma_qstatus); n++) {
		qsts = read_pcie_csr(nfp, dmaq_qsts[n]);
		if (qsts != nfp->dma_qstatus[n]) {
			/* Somebody else has used the DMA engine. */
			nfp->dma_unavailable = 1;
			return -EIO;
		}
	}

	return 0;
}

static void nfp_dma_log_qstatus(struct nfp3200_pcie *nfp)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(nfp->dma_qstatus); n++)
		nfp->dma_qstatus[n] = read_pcie_csr(nfp, dmaq_qsts[n]);
}

static void nfp_dma_enqueue(struct nfp3200_pcie *nfp, u32 dmaq, u32 dmacmd[])
{
	u32 cmd = dmaq_cmd[dmaq & 0x3];

	write_pcie_csr(nfp, cmd + 0x0, dmacmd[0]);
	write_pcie_csr(nfp, cmd + 0x4, dmacmd[1]);
	write_pcie_csr(nfp, cmd + 0x8, dmacmd[2]);
	write_pcie_csr(nfp, cmd + 0xc, dmacmd[3]);
}

static int nfp_dma_drain(struct nfp3200_pcie *nfp, u32 dmaq)
{
	u32 n, status;
	u32 qsts = dmaq_qsts[dmaq & 0x3];

	for (n = 0; n < 500; n++) {
		status = read_pcie_csr(nfp, qsts);
		if (!(status & NFP_PCIE_DMA_QSTS_QUEUE_ACTIVE))
			return 0;
		mdelay(1);
	}

	return -EIO;
}

static void nfp_dma_enable(struct nfp3200_pcie *nfp, u32 dmaq)
{
	u32 ctrl = dmaq_ctrl[dmaq & 0x3];
	u32 control = read_pcie_csr(nfp, ctrl);

	control &= ~NFP_PCIE_DMA_CTRL_QUEUE_STOP_ENABLE;
	write_pcie_csr(nfp, ctrl, control);
}

static void nfp_dma_abort(struct nfp3200_pcie *nfp, u32 dmaq)
{
	u32 ctrl = dmaq_ctrl[dmaq & 0x3];
	u32 control = read_pcie_csr(nfp, ctrl);

	control |= NFP_PCIE_DMA_CTRL_QUEUE_STOP_ENABLE;
	write_pcie_csr(nfp, ctrl, control);
}

static int nfp_dma_op64(struct nfp_cpp_area *area, u32 dmaq, void *buff,
			unsigned long offset, size_t size)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(nfp_cpp_area_cpp(area));
	u32 dmacmd[4], dma_incr;
	u64 total;
	int err;

	offset += priv->offset;

	BUG_ON(size % sizeof(u64));
	mutex_lock(&nfp->dma_lock);

	err = nfp_dma_available(nfp);
	if (err) {
		mutex_unlock(&nfp->dma_lock);
		return err;
	}

	nfp_dma_enable(nfp, dmaq);

	err = 0;
	for (total = 0; total < size; total += dma_incr) {
		if ((size - total) > NFP_DMA_MAX)
			dma_incr = NFP_DMA_MAX;
		else
			dma_incr = size - total;

		if (dmaq == DMAQ_FPCI_HI || dmaq == DMAQ_FPCI_LO)
			memcpy(nfp->dma_cpu_addr, buff + total, dma_incr);

		dmacmd[0] = NFP_PCIE_DMA_CMD_CPP_ADDR_LO(
			((offset + total) >> 0) & 0xffffffff);
		dmacmd[1] = NFP_PCIE_DMA_CMD_CPP_ADDR_HI(
			((offset + total) >> 32) & 0xff) |
			NFP_PCIE_DMA_CMD_CPP_TARGET(priv->target) |
			NFP_PCIE_DMA_CMD_TARGET64_ENABLE |
			NFP_PCIE_DMA_CMD_TOKEN(priv->token) |
			NFP_PCIE_DMA_CMD_CPL(0);
		dmacmd[2] = NFP_PCIE_DMA_CMD_PCIE_ADDR_LO(
			(((u64)nfp->dma_dev_addr) >> 0) & 0xffffffff);
		dmacmd[3] = NFP_PCIE_DMA_CMD_PCIE_ADDR_HI(
			(((uint64_t)nfp->dma_dev_addr) >> 32) & 0xff) |
			NFP_PCIE_DMA_CMD_LENGTH(dma_incr);

		nfp_dma_enqueue(nfp, dmaq, dmacmd);

		err = nfp_dma_drain(nfp, dmaq);
		if (err) {
			dev_err(nfp->dev, "DMA aborted. NFP must be reset.\n");
			nfp_dma_abort(nfp, dmaq);
			break;
		}

		if (dmaq == DMAQ_TPCI_HI || dmaq == DMAQ_TPCI_LO)
			memcpy(buff + total, nfp->dma_cpu_addr, dma_incr);
	}

	nfp_dma_log_qstatus(nfp);
	mutex_unlock(&nfp->dma_lock);
	return err ? err : size;
}

static int nfp_dma_read64(struct nfp_cpp_area *area, void *buff,
			  unsigned long offset, size_t size)
{
	return nfp_dma_op64(area, DMAQ_TPCI_HI, buff, offset, size);
}

static int nfp_dma_write64(struct nfp_cpp_area *area, const void *buff,
			   unsigned long offset, size_t size)
{
	return nfp_dma_op64(area, DMAQ_FPCI_HI, (void *)buff, offset, size);
}
#endif /* CONFIG_NFP_PCI32 */

static int nfp_cpp_pcie_area_read(struct nfp_cpp_area *area, void *kernel_vaddr,
				  unsigned long offset, unsigned int length)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	const u32 __iomem *rdptr32 = priv->iomem + offset;
	const u64 __iomem __maybe_unused *rdptr64 = priv->iomem + offset;
	u32 *wrptr32 = kernel_vaddr;
	u64 __maybe_unused *wrptr64 = kernel_vaddr;
	int is_64;
	int n;

	if (!priv->width.read)
		return -EINVAL;

	is_64 = (priv->width.read == 8) ? 1 : 0;

	/* MU reads via a PCIe2CPP BAR supports 32bit (and others) lengths */
	if ((priv->target == (NFP_CPP_TARGET_ID_MASK & NFP_CPP_TARGET_MU)) &&
	    (priv->action == NFP_CPP_ACTION_RW))
		is_64 = 0;

	if ((offset + length) > priv->size)
		return -EFAULT;

	if (is_64) {
		if (((offset % sizeof(u64)) != 0) ||
		    ((length % sizeof(u64)) != 0))
			return -EINVAL;
	} else {
		if (((offset % sizeof(u32)) != 0) ||
		    ((length % sizeof(u32)) != 0))
			return -EINVAL;
	}

	BUG_ON(!priv->bar);

#ifdef CONFIG_NFP_PCI32
	if (is_64) {
		n = nfp_dma_read64(area, kernel_vaddr, offset, length);
#else
	if (((offset % sizeof(u64)) == 0) && ((length % sizeof(u64)) == 0)) {
		for (n = 0; n < length; n += sizeof(u64))
			*wrptr64++ = __raw_readq(rdptr64++);
#endif
	} else {
		for (n = 0; n < length; n += sizeof(u32))
			*wrptr32++ = __raw_readl(rdptr32++);
	}

	return n;
}

static int nfp_cpp_pcie_area_write(struct nfp_cpp_area *area,
				   const void *kernel_vaddr,
				   unsigned long offset, unsigned int length)
{
	struct nfp_cpp_area_priv *priv = nfp_cpp_area_priv(area);
	const u32 *rdptr32 = kernel_vaddr;
	const u64 __maybe_unused *rdptr64 = kernel_vaddr;
	u32 __iomem *wrptr32 = priv->iomem + offset;
	u64 __iomem __maybe_unused *wrptr64 = priv->iomem + offset;
	int is_64;
	int n;

	if ((offset + length) > priv->size)
		return -EFAULT;

	if (!priv->width.write)
		return -EINVAL;

	is_64 = (priv->width.write == 8) ? 1 : 0;

	/* MU writes via a PCIe2CPP BAR supports 32bit (and other) lengths */
	if ((priv->target == (NFP_CPP_TARGET_ID_MASK & NFP_CPP_TARGET_MU)) &&
	    (priv->action == NFP_CPP_ACTION_RW))
		is_64 = 0;

	if (is_64) {
		if (((offset % sizeof(u64)) != 0) ||
		    ((length % sizeof(u64)) != 0))
			return -EINVAL;
	} else {
		if (((offset % sizeof(u32)) != 0) ||
		    ((length % sizeof(u32)) != 0))
			return -EINVAL;
	}
	BUG_ON(!priv->bar);

#ifdef CONFIG_NFP_PCI32
	if (is_64) {
		n = nfp_dma_write64(area, kernel_vaddr, offset, length);
#else
	if (((offset % sizeof(u64)) == 0) && ((length % sizeof(u64)) == 0)) {
		for (n = 0; n < length; n += sizeof(u64))
			__raw_writeq(*rdptr64++, wrptr64++);
#endif
	} else {
		for (n = 0; n < length; n += sizeof(u32))
			__raw_writel(*rdptr32++, wrptr32++);
	}

	return n;
}

struct nfp3200_event_priv {
	int filter;
};

static int nfp3200_event_acquire(struct nfp_cpp_event *event, u32 match,
				 u32 mask, u32 type)
{
	struct nfp_cpp *cpp = nfp_cpp_event_cpp(event);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(cpp);
	struct nfp3200_event_priv *ev = nfp_cpp_event_priv(event);
	int filter;

	filter = nfp_em_manager_acquire(nfp->event, event, match, mask, type);
	if (filter < 0)
		return filter;

	ev->filter = filter;

	return 0;
}

static void nfp3200_event_release(struct nfp_cpp_event *event)
{
	struct nfp_cpp *cpp = nfp_cpp_event_cpp(event);
	struct nfp3200_pcie *nfp = nfp_cpp_priv(cpp);
	struct nfp3200_event_priv *ev = nfp_cpp_event_priv(event);

	nfp_em_manager_release(nfp->event, ev->filter);
}

static int nfp_cpp_pcie_init(struct nfp_cpp *cpp)
{
	struct nfp3200_pcie *nfp = nfp_cpp_priv(cpp);
	u32 tmp;
	int err;

	/* Determine if we need NFP workarounds */
	err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_JTAG_ID_CODE, &tmp);
	if (err < 0) {
		dev_err(nfp->dev, "nfp_xpb_readl() failed.\n");
		goto err_pcie_write_create;
	}
	tmp = NFP_PL_JTAG_ID_CODE_REV_ID_of(tmp) + 0xA0;
	if (tmp == 0xA1) {
		dev_info(nfp->dev, "Workaround: NFP3200 A1\n");
		nfp->workaround |= NFP_A1_WORKAROUND;
	}
	if (tmp < 0xB0) {
		dev_info(nfp->dev, "Workaround: NFP3200 A revs\n");
		nfp->workaround |= NFP_A_WORKAROUND;
	}

	if (nfp->workaround & NFP_A1_WORKAROUND) {
		nfp->a1.internal_write_area = nfp_cpp_area_alloc_with_name(
			cpp, NFP_CPP_ID(NFP_CPP_TARGET_PCIE, 3, 0),
			"pcie.internal_write", 0, NFP_PCIE_P2C_CPPMAP_SIZE);
		if (!nfp->a1.internal_write_area)
			goto err_pcie_write_create;
		err = nfp_cpp_area_acquire(nfp->a1.internal_write_area);
		if (err)
			goto err_pcie_write_acquire;
		nfp->a1.pciewr =
			nfp_cpp_area_iomem(nfp->a1.internal_write_area);

		/* ARM vNIC base - see workaround in nfp_cpp_pcie_area_init */
		nfp->a1.vnic_base = 0xe000;

		/*
		 * To avoid ECC errors, write something to CLS address used
		 * for flush reads.  Hopefully CLS1 address 0 is not used by
		 * anyone yet.
		 */
		writel(0, nfp->bars[NFP_PCIETGT_BAR_INDEX].iomem +
			NFP_PCIE_P2C_CPPMAP_TARGET_OFFSET(
				NFP_CPP_TARGET_LOCAL_SCRATCH));
	}

	if (nfp->workaround & NFP_A_WORKAROUND) {
		u32 tmp;
		/* Workaround for YDS-152: Make RID override work */
		err = nfp_xpb_readl(cpp, NFP_XPB_PCIE_CSR + NFP_PCIE_CSR_CFG0,
				    &tmp);
		if (err < 0) {
			dev_err(nfp->dev, "nfp_xpb_readl() failed.\n");
			goto err_pcie_write_acquire;
		}
		tmp |= NFP_PCIE_CSR_CFG0_TARGET_BUS(nfp->pdev->bus->number);
		err = nfp_xpb_writel(cpp, NFP_XPB_PCIE_CSR + NFP_PCIE_CSR_CFG0,
				     tmp);
		if (err < 0) {
			dev_err(nfp->dev, "nfp_xpb_writel() failed.\n");
			goto err_pcie_write_acquire;
		}
	}

	/* Enable model autodetect */
	if (nfp->pdev->subsystem_device != PCI_DEVICE_NFP3200)
		nfp->ops.model = 0;

	err = device_create_file(nfp_cpp_device(cpp), &dev_attr_barcfg);
	if (err < 0)
		goto err_attr_create;

	return 0;

err_attr_create:
err_pcie_write_acquire:
	nfp_cpp_area_free(nfp->a1.internal_write_area);
err_pcie_write_create:

	return err;
}

static void nfp_cpp_pcie_free(struct nfp_cpp *cpp)
{
	struct nfp3200_pcie *nfp = nfp_cpp_priv(cpp);
	struct pci_dev *pdev = nfp->pdev;

	BUG_ON(!nfp);

	device_remove_file(nfp_cpp_device(cpp), &dev_attr_barcfg);

	if (nfp->workaround & NFP_A1_WORKAROUND) {
		nfp->a1.pciewr = NULL;
		nfp_cpp_area_release_free(nfp->a1.internal_write_area);
	}

	nfp_em_manager_destroy(nfp->event);
	disable_bars(nfp);
	devm_iounmap(&pdev->dev, nfp->pcietgt);
#ifdef CONFIG_NFP_PCI32
	dma_free_coherent(&pdev->dev, NFP_DMA_MAX, nfp->dma_cpu_addr,
			  nfp->dma_dev_addr);
#endif
	kfree(nfp);
}

/**
 * nfp_cpp_from_nfp3200_pcie() - Build a NFP CPP bus from a NFP3200 PCI device
 * @pdev:	NFP3200 PCI device
 * @event_irq:	IRQ bound to the event manager (optional)
 *
 * Return: NFP CPP handle
 */
struct nfp_cpp *nfp_cpp_from_nfp3200_pcie(struct pci_dev *pdev, int event_irq)
{
	struct nfp_cpp_operations *ops;
	struct nfp3200_pcie *nfp;
	int err;

	/*  Finished with card initialization. */
	dev_info(&pdev->dev,
		 "Netronome Flow Processor (NFP3200) PCIe Card Probe\n");

	nfp = kzalloc(sizeof(*nfp), GFP_KERNEL);
	if (!nfp) {
		err = -ENOMEM;
		goto err_nfpmem_alloc;
	}

	nfp->dev = &pdev->dev;
	nfp->pdev = pdev;
	init_waitqueue_head(&nfp->bar_waiters);
	spin_lock_init(&nfp->bar_lock);

	ops = &nfp->ops;
	ops->priv = nfp;
	ops->parent = &pdev->dev;

	/* The NFP3200 only has one PCIe EP */
	/* We support multiple virtual channels over this interface */
	ops->interface = NFP_CPP_INTERFACE(NFP_CPP_INTERFACE_TYPE_PCI,
			0, NFP_CPP_INTERFACE_CHANNEL_PEROPENER);

	/* In boot-from-pcie mode, the ARM is in reset,
	 * so it is not safe to autodetect the model using
	 * the CSRs in the ARM Scratch.
	 *
	 * Provide a reasonable default.
	 */
	ops->model = 0x320000A1;

	ops->init = nfp_cpp_pcie_init;
	ops->free = nfp_cpp_pcie_free;

	ops->area_init = nfp_cpp_pcie_area_init;
	ops->area_cleanup = nfp_cpp_pcie_area_cleanup;
	ops->area_acquire = nfp_cpp_pcie_area_acquire;
	ops->area_release = nfp_cpp_pcie_area_release;
	ops->area_phys = nfp_cpp_pcie_area_phys;
	ops->area_iomem = nfp_cpp_pcie_area_iomem;
	ops->area_resource = nfp_cpp_pcie_area_resource;
	ops->area_read = nfp_cpp_pcie_area_read;
	ops->area_write = nfp_cpp_pcie_area_write;

	ops->area_priv_size = sizeof(struct nfp_cpp_area_priv);
	ops->owner = THIS_MODULE;

	ops->event_priv_size = sizeof(struct nfp3200_event_priv);
	ops->event_acquire = nfp3200_event_acquire;
	ops->event_release = nfp3200_event_release;

	nfp->pcietgt = devm_ioremap_nocache(
		&pdev->dev,
		pci_resource_start(pdev, NFP_PCIETGT_BAR_INDEX * 2),
		pci_resource_len(pdev, NFP_PCIETGT_BAR_INDEX * 2));
	if (IS_ERR_OR_NULL(nfp->pcietgt)) {
		dev_err(&pdev->dev, "Failed to map PCIe target registers.\n");
		err = !nfp->pcietgt ? -ENOMEM : PTR_ERR(nfp->pcietgt);
		goto err_pcietgt;
	}

	err = enable_bars(nfp);
	if (err)
		goto err_enable_bars;

#ifdef CONFIG_NFP_PCI32
	/*
	 * PCIe host CPU can not do 64bit PCI transactions.  Set up
	 * host buffers for doing these transactions with the DMA
	 * engine instead.
	 */
	mutex_init(&nfp->dma_lock);
	nfp->dma_cpu_addr = dma_alloc_coherent(
		&pdev->dev, NFP_DMA_MAX, &nfp->dma_dev_addr, GFP_KERNEL);
	if (IS_ERR_OR_NULL(nfp->dma_cpu_addr)) {
		if (!nfp->dma_cpu_addr)
			err = -ENOMEM;
		else
			err = PTR_ERR(nfp->dma_cpu_addr);
		goto err_dma_addr;
	}
	nfp->dma_unavailable = 0;
	nfp_dma_log_qstatus(nfp);
#endif

	if (event_irq >= 0) {
		nfp->event = nfp_em_manager_create(nfp->pcietgt + NFP_PCIE_EM,
						   event_irq);
		if (IS_ERR_OR_NULL(nfp->event)) {
			err = nfp->event ? PTR_ERR(nfp->event) : -ENOMEM;
			goto err_em_init;
		}
	}

	/* Probe for all the common NFP devices */
	dev_info(&pdev->dev, "Found a NFP3200 on the PCIe bus.\n");
	return nfp_cpp_from_operations(&nfp->ops);

err_em_init:
#ifdef CONFIG_NFP_PCI32
	dma_free_coherent(&pdev->dev, NFP_DMA_MAX, nfp->dma_cpu_addr,
			  nfp->dma_dev_addr);
err_dma_addr:
#endif
	disable_bars(nfp);
err_enable_bars:
	devm_iounmap(&pdev->dev, nfp->pcietgt);
err_pcietgt:
	kfree(nfp);
err_nfpmem_alloc:
	dev_err(&pdev->dev, "NFP3200 PCI setup failed\n");
	return ERR_PTR(err);
}
