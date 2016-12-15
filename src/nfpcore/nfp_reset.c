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
 * nfp_reset.c
 * Authors: Mike Aitken <mike.aitken@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 */

#define NFP_SUBSYS	"nfp_reset: "

#include "nfp.h"

#include "nfp_nbi.h"
#include "nfp_nffw.h"

#include "nfp6000/nfp6000.h"
#include "nfp6000/nfp_xpb.h"

#ifndef NFP_APP_PACKET_CREDITS
#define NFP_APP_PACKET_CREDITS	128
#endif

#ifndef NFP_APP_BUFFER_CREDITS
#define NFP_APP_BUFFER_CREDITS	63
#endif

#define NBIX_BASE					(0xa0000)
#define NFP_NBI_MACX					(NBIX_BASE + 0x300000)
#define NFP_NBI_MACX_CSR				(NFP_NBI_MACX + 0x00000)
#define NFP_NBI_MACX_MAC_BLOCK_RST			0x00000
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY1_STAT_RST	BIT(23)
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY0_STAT_RST	BIT(22)
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_TX_RST_MPB		BIT(21)
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_RX_RST_MPB		BIT(20)
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_TX_RST_CORE		BIT(19)
#define  NFP_NBI_MACX_MAC_BLOCK_RST_MAC_RX_RST_CORE		BIT(18)
#define NFP_NBI_MACX_EG_BCP_COUNT	0x00000098
#define   NFP_NBI_MACX_EG_BCP_COUNT_EG_BCC1_of(_x) (((_x) >> 16) & 0x3fff)
#define   NFP_NBI_MACX_EG_BCP_COUNT_EG_BCC_of(_x) (((_x) >> 0) & 0x3fff)
#define NFP_NBI_MACX_IG_BCP_COUNT	0x000000a0
#define   NFP_NBI_MACX_IG_BCP_COUNT_IG_BCC1_of(_x) (((_x) >> 16) & 0x3fff)
#define   NFP_NBI_MACX_IG_BCP_COUNT_IG_BCC_of(_x) (((_x) >> 0) & 0x3fff)
#define NFP_NBI_MACX_ETH(_x)  (NFP_NBI_MACX + 0x40000 + ((_x) & 0x1) * 0x20000)
#define  NFP_NBI_MACX_ETH_SEG_CMD_CONFIG(_x)	\
					(0x00000008 + (0x400 * ((_x) & 0xf)))
#define   NFP_NBI_MACX_ETH_SEG_CMD_CONFIG_ETH_RX_ENA	BIT(1)
#define NFP_NBI_MACX_MAC_SYS_SUPPORT_CTRL			0x00000014
#define  NFP_NBI_MACX_MAC_SYS_SUPPORT_CTRL_SPLIT_MEM_IG	BIT(8)

#define NFP_NBI_DMAX					(NBIX_BASE + 0x000000)
#define NFP_NBI_DMAX_CSR				(NFP_NBI_DMAX + 0x00000)
#define NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG(_x) \
					(0x00000040 + (0x4 * ((_x) & 0x1f)))
#define   NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_BPENUM_of(_x) (((_x) >> 27) & 0x1f)
#define   NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_CTM_of(_x)	(((_x) >> 21) & 0x3f)
#define   NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_PKT_CREDIT_of(_x) \
							(((_x) >> 10) & 0x7ff)
#define   NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_BUF_CREDIT_of(_x) \
							(((_x) >> 0) & 0x3ff)

#define CTMX_BASE                                      (0x60000)
#define NFP_CTMX_CFG                                   (CTMX_BASE + 0x000000)
#define NFP_CTMX_PKT                                   (CTMX_BASE + 0x010000)
#define NFP_CTMX_PKT_MU_PE_ACTIVE_PACKET_COUNT         0x00000400
#define   NFP_CTMX_PKT_MUPESTATS_MU_PE_STAT_of(_x)     (((_x) >> 0) & 0x3ff)

#define NFP_PCIE_DMA					(0x040000)
#define NFP_PCIE_DMA_QSTS0_TOPCI			0x000000e0
#define   NFP_PCIE_DMA_DMAQUEUESTATUS0_DMA_LO_AVAIL_of(_x) \
							(((_x) >> 24) & 0xff)
#define NFP_PCIE_DMA_QSTS1_TOPCI                        0x000000e4
#define   NFP_PCIE_DMA_DMAQUEUESTATUS1_DMA_HI_AVAIL_of(_x) \
							(((_x) >> 24) & 0xff)
#define   NFP_PCIE_DMA_DMAQUEUESTATUS1_DMA_MED_AVAIL_of(_x) \
							(((_x) >> 8) & 0xff)

#define NFP_PCIE_Q(_x)			(0x080000 + ((_x) & 0xff) * 0x800)
#define NFP_QCTLR_STS_LO                                     0x00000008
#define   NFP_QCTLR_STS_LO_RPTR_ENABLE				BIT(31)
#define NFP_QCTLR_STS_HI                                     0x0000000c
#define   NFP_QCTLR_STS_HI_EMPTY				BIT(26)

static int nfp6000_island_reset(struct nfp_cpp *cpp, int nbi_mask)
{
	int err;
	int i, u;
	int state = NFP_DEVICE_STATE_RESET;

	/* Reset NBI cores */
	for (i = 0; i < 2; i++) {
		if ((nbi_mask & BIT(i)) == 0)
			continue;

		err = nfp_power_set(cpp,
				    NFP6000_DEVICE_NBI(i,
						       NFP6000_DEVICE_NBI_CORE),
				    state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			return err;
		}
	}

	/* Reset ILA cores */
	for (i = 0; i < 2; i++) {
		for (u = NFP6000_DEVICE_ILA_MEG1; u >= 0; u--) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_ILA(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset FPC cores */
	for (i = 0; i < 7; i++) {
		for (u = NFP6000_DEVICE_FPC_MEG5; u >= 0; u--) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_FPC(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset IMU islands */
	for (i = 0; i < 2; i++) {
		for (u = NFP6000_DEVICE_IMU_NLU; u >= 0; u--) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_IMU(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset CRP islands */
	for (i = 0; i < 2; i++) {
		for (u = NFP6000_DEVICE_CRP_MEG1; u >= 0; u--) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_CRP(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset PCI islands (MEGs only!) */
	for (i = 0; i < 4; i++) {
		for (u = NFP6000_DEVICE_PCI_MEG1; u >= NFP6000_DEVICE_PCI_MEG0;
		     u--) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_PCI(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	return 0;
}

static int nfp6000_island_on(struct nfp_cpp *cpp, int nbi_mask)
{
	int err;
	int i, u;
	int state = NFP_DEVICE_STATE_ON;

	/* Reset NBI cores */
	for (i = 0; i < 2; i++) {
		if ((nbi_mask & BIT(i)) == 0)
			continue;

		err = nfp_power_set(cpp,
				    NFP6000_DEVICE_NBI(i,
						       NFP6000_DEVICE_NBI_CORE),
				    state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			return err;
		}
	}

	/* Reset ILA cores */
	for (i = 0; i < 2; i++) {
		for (u = 0; u <= NFP6000_DEVICE_ILA_MEG1; u++) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_ILA(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset FPC cores */
	for (i = 0; i < 7; i++) {
		for (u = 0; u <= NFP6000_DEVICE_FPC_MEG5; u++) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_FPC(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset IMU islands */
	for (i = 0; i < 2; i++) {
		for (u = 0; u <= NFP6000_DEVICE_IMU_NLU; u++) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_IMU(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset CRP islands */
	for (i = 0; i < 2; i++) {
		for (u = 0; u <= NFP6000_DEVICE_CRP_MEG1; u++) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_CRP(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	/* Reset PCI islands (MEGs only!) */
	for (i = 0; i < 4; i++) {
		for (u = NFP6000_DEVICE_PCI_MEG0; u <= NFP6000_DEVICE_PCI_MEG1;
		     u++) {
			err = nfp_power_set(cpp,
					    NFP6000_DEVICE_PCI(i, u), state);
			if (err < 0) {
				if (err == -ENODEV)
					break;
				return err;
			}
		}
	}

	return 0;
}

#define NFP_ME_CTXENABLES		0x00000018
#define  NFP_ME_CTXENABLES_INUSECONTEXTS	BIT(31)
#define  NFP_ME_CTXENABLES_CTXENABLES(_x)	(((_x) & 0xff) << 8)
#define  NFP_ME_CTXENABLES_CSECCERROR		BIT(29)
#define  NFP_ME_CTXENABLES_BREAKPOINT		BIT(27)
#define  NFP_ME_CTXENABLES_REGISTERPARITYERR	BIT(25)
#define NFP_ME_ACTCTXSTATUS		0x00000044
#define NFP_ME_ACTCTXSTATUS_AB0			BIT(31)

#define NFP_CT_ME(_x)			(0x00010000 + (((_x + 4) & 0xf) << 10))

static int nfp6000_stop_me(struct nfp_cpp *cpp, int island, int menum)
{
	int err;
	u32 tmp;
	u32 me_r = NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 2, 1);
	u32 me_w = NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 3, 1);
	u64 mecsr = (island << 24) | NFP_CT_ME(menum);

	err = nfp_cpp_readl(cpp, me_r, mecsr + NFP_ME_CTXENABLES, &tmp);
	if (err < 0)
		return err;

	tmp &= ~(NFP_ME_CTXENABLES_INUSECONTEXTS |
		 NFP_ME_CTXENABLES_CTXENABLES(0xff));
	tmp &= ~NFP_ME_CTXENABLES_CSECCERROR;
	tmp &= ~NFP_ME_CTXENABLES_BREAKPOINT;
	tmp &= ~NFP_ME_CTXENABLES_REGISTERPARITYERR;

	err = nfp_cpp_writel(cpp, me_w, mecsr + NFP_ME_CTXENABLES, tmp);
	if (err < 0)
		return err;

	mdelay(1);

	/* This may seem like a rushed test, but in the 1 microsecond sleep
	 * the ME has executed about a 1000 instructions and even more during
	 * the time it took the host to execute this code and for the CPP
	 * command to reach the CSR in the test read anyway.
	 *
	 * If one of those instructions did not swap out, the code is a very
	 * inefficient single-threaded sequence of instructions which would
	 * be very rare or very specific.
	*/

	err = nfp_cpp_readl(cpp, me_r, mecsr + NFP_ME_ACTCTXSTATUS, &tmp);
	if (err < 0)
		return err;

	if (tmp & NFP_ME_ACTCTXSTATUS_AB0) {
		nfp_err(cpp, "ME%d.%d did not stop after 1000us\n",
			island, menum);
		return -EIO;
	}

	return 0;
}

static int nfp6000_stop_me_island(struct nfp_cpp *cpp, int island)
{
	int i, err;
	int meg_device, megs;

	switch (island) {
	case 1:
		/* ARM MEs are not touched */
		return 0;
	case 4:
	case 5:
	case 6:
	case 7:
		meg_device = NFP6000_DEVICE_PCI_MEG0;
		megs = 2;
		break;
	case 12:
	case 13:
		meg_device = NFP6000_DEVICE_CRP_MEG0;
		megs = 2;
		break;
	case 32:
	case 33:
	case 34:
	case 35:
	case 36:
	case 37:
	case 38:
		meg_device = NFP6000_DEVICE_FPC_MEG0;
		megs = 6;
		break;
	case 48:
	case 49:
		meg_device = NFP6000_DEVICE_ILA_MEG0;
		megs = 2;
		break;
	default:
		return 0;
	}

	for (i = 0; i < megs; i++) {
		int state;

		err = nfp_power_get(cpp,
				    NFP6000_DEVICE(island, meg_device + i),
				    &state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			return err;
		}

		if (state != NFP_DEVICE_STATE_ON)
			continue;

		err = nfp6000_stop_me(cpp, island, i * 2 + 0);
		if (err < 0)
			return err;

		err = nfp6000_stop_me(cpp, island, i * 2 + 1);
		if (err < 0)
			return err;
	}

	return 0;
}

static int nfp6000_nbi_mac_check_freebufs(struct nfp_cpp *cpp,
					  struct nfp_nbi_dev *nbi)
{
	u32 tmp;
	int err, ok, split;
	const int timeout_ms = 500;
	struct timespec ts, timeout = {
		.tv_sec = 0,
		.tv_nsec = timeout_ms * 1000 * 1000,
	};
	const int igsplit = 1007;
	const int egsplit = 495;

	err = nfp_nbi_mac_regr(nbi, NFP_NBI_MACX_CSR,
			       NFP_NBI_MACX_MAC_SYS_SUPPORT_CTRL, &tmp);
	if (err < 0)
		return err;

	split = tmp & NFP_NBI_MACX_MAC_SYS_SUPPORT_CTRL_SPLIT_MEM_IG;

	ts = CURRENT_TIME;
	timeout = timespec_add(ts, timeout);

	ok = 1;
	do {
		int igcount, igcount1, egcount, egcount1;

		err = nfp_nbi_mac_regr(nbi, NFP_NBI_MACX_CSR,
				       NFP_NBI_MACX_IG_BCP_COUNT, &tmp);
		if (err < 0)
			return err;

		igcount = NFP_NBI_MACX_IG_BCP_COUNT_IG_BCC_of(tmp);
		igcount1 = NFP_NBI_MACX_IG_BCP_COUNT_IG_BCC1_of(tmp);

		err = nfp_nbi_mac_regr(nbi, NFP_NBI_MACX_CSR,
				       NFP_NBI_MACX_EG_BCP_COUNT, &tmp);
		if (err < 0)
			return err;

		egcount = NFP_NBI_MACX_EG_BCP_COUNT_EG_BCC_of(tmp);
		egcount1 = NFP_NBI_MACX_EG_BCP_COUNT_EG_BCC1_of(tmp);

		if (split) {
			ok &= (igcount >= igsplit);
			ok &= (egcount >= egsplit);
			ok &= (igcount1 >= igsplit);
			ok &= (egcount1 >= egsplit);
		} else {
			ok &= (igcount >= igsplit * 2);
			ok &= (egcount >= egsplit * 2);
		}

		if (!ok) {
			ts = CURRENT_TIME;
			if (timespec_compare(&ts, &timeout) >= 0) {
				nfp_err(cpp, "After %dms, NBI%d did not flush all packet buffers\n",
					timeout_ms, nfp_nbi_index(nbi));
				if (split) {
					nfp_err(cpp, "\t(ingress %d/%d != %d/%d, egress %d/%d != %d/%d)\n",
						igcount, igcount1,
						igsplit, igsplit,
						egcount, egcount1,
						egsplit, egsplit);
				} else {
					nfp_err(cpp, "\t(ingress %d != %d, egress %d != %d)\n",
						igcount, igsplit,
						egcount, egsplit);
				}
				return -ETIMEDOUT;
			}
		}
	} while (!ok);

	return 0;
}

static int nfp6000_nbi_check_dma_credits(struct nfp_cpp *cpp,
					 struct nfp_nbi_dev *nbi,
					 const u32 *bpe, int bpes)
{
	int err, p;
	u32 tmp;

	if (bpes < 1)
		return 0;

	for (p = 0; p < bpes; p++) {
		int ctm, pkt, buf, bp;
		int ctmb, pktb, bufb;

		err = nfp_nbi_mac_regr(nbi, NFP_NBI_DMAX_CSR,
				       NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG(p),
				       &tmp);
		if (err < 0)
			return err;

		bp = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_BPENUM_of(tmp);

		ctm = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_CTM_of(tmp);
		ctmb = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_CTM_of(bpe[bp]);

		pkt = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_PKT_CREDIT_of(tmp);
		pktb = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_PKT_CREDIT_of(bpe[bp]);

		buf = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_BUF_CREDIT_of(tmp);
		bufb = NFP_NBI_DMAX_CSR_NBI_DMA_BPE_CFG_BUF_CREDIT_of(bpe[bp]);

		if (ctm != ctmb) {
			nfp_err(cpp, "NBI%d DMA BPE%d targets CTM%d, expected CTM%d\n",
				nfp_nbi_index(nbi), bp, ctm, ctmb);
			return -EBUSY;
		}

		if (pkt != pktb) {
			nfp_err(cpp, "NBI%d DMA BPE%d (CTM%d) outstanding packets (%d != %d)\n",
				nfp_nbi_index(nbi), bp, ctm, pkt, pktb);
			return -EBUSY;
		}

		if (buf != bufb) {
			nfp_err(cpp, "NBI%d DMA BPE%d (CTM%d) outstanding buffers (%d != %d)\n",
				nfp_nbi_index(nbi), bp, ctm, buf, bufb);
			return -EBUSY;
		}
	}

	return 0;
}

#define BPECFG_MAGIC_CHECK(x)	(((x) & 0xffffff00) == 0xdada0100)
#define BPECFG_MAGIC_COUNT(x)	((x) & 0x000000ff)

static int bpe_lookup(struct nfp_cpp *cpp, int nbi, u32 *bpe, int bpe_max)
{
	int err, i;
	const struct nfp_rtsym *sym;
	u32 id, tmp;
	u32 __iomem *ptr;
	struct nfp_cpp_area *area;
	char buff[] = "nbi0_dma_bpe_credits";

	buff[3] += nbi;

	sym = nfp_rtsym_lookup(cpp, buff);
	if (!sym) {
		nfp_info(cpp, "%s: Symbol not present\n", buff);
		return 0;
	}

	id = NFP_CPP_ISLAND_ID(sym->target, NFP_CPP_ACTION_RW, 0, sym->domain);
	area = nfp_cpp_area_alloc_acquire(cpp, id, sym->addr, sym->size);
	if (IS_ERR_OR_NULL(area)) {
		nfp_err(cpp, "%s: Can't acquire area\n", buff);
		return area ? PTR_ERR(area) : -ENOMEM;
	}

	ptr = nfp_cpp_area_iomem(area);
	if (IS_ERR_OR_NULL(ptr)) {
		nfp_err(cpp, "%s: Can't map area\n", buff);
		err = ptr ? PTR_ERR(ptr) : -ENOMEM;
		goto exit;
	}

	tmp = readl(ptr++);
	if (!BPECFG_MAGIC_CHECK(tmp)) {
		nfp_err(cpp, "%s: Magic value (0x%08x) unrecognized\n",
			buff, tmp);
		err = -EINVAL;
		goto exit;
	}

	if (BPECFG_MAGIC_COUNT(tmp) > bpe_max) {
		nfp_err(cpp, "%s: Magic count (%d) too large (> %d)\n",
			buff, BPECFG_MAGIC_COUNT(tmp), bpe_max);
		err = -EINVAL;
		goto exit;
	}

	for (i = 0; i < bpe_max; i++)
		bpe[i] = readl(ptr++);

	err = BPECFG_MAGIC_COUNT(tmp);

exit:
	nfp_cpp_area_release_free(area);
	return err;
}

/* Determine if a given PCIe's DMA Queues are empty */
static int nfp6000_check_empty_pcie_dma_queues(struct nfp_cpp *cpp,
					       int pci_island, int *empty)
{
	u32 tmp;
	const int dma_low = 128, dma_med = 64, dma_hi = 64;
	int hi, med, low, ok, err;
	const u32 pci = NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_PCIE,
					       2, 0, pci_island + 4);

	ok = 1;
	err = nfp_cpp_readl(cpp, pci, NFP_PCIE_DMA +
			    NFP_PCIE_DMA_QSTS0_TOPCI, &tmp);
	if (err < 0)
		return err;

	low = NFP_PCIE_DMA_DMAQUEUESTATUS0_DMA_LO_AVAIL_of(tmp);

	err = nfp_cpp_readl(cpp, pci, NFP_PCIE_DMA +
			    NFP_PCIE_DMA_QSTS1_TOPCI, &tmp);
	if (err < 0)
		return err;

	med = NFP_PCIE_DMA_DMAQUEUESTATUS1_DMA_MED_AVAIL_of(tmp);
	hi  = NFP_PCIE_DMA_DMAQUEUESTATUS1_DMA_HI_AVAIL_of(tmp);

	ok &= low == dma_low;
	ok &= med == dma_med;
	ok &= hi  == dma_hi;

	*empty = ok;
	return 0;
}

#define NFP_MON_PCIE_ABI(x)	(0x50434900 | ((x & 0xf) << 4))
#define NFP_MON_PCIE_MAGIC	0x00
#define NFP_MON_PCIE_CTL(x)	(0x10 + (((x) & 3) * 4))

static int nfp6000_pcie_monitor_set(struct nfp_cpp *cpp, int pci, u32 flags)
{
	u32 cls = NFP_CPP_ID(NFP_CPP_TARGET_CLS, NFP_CPP_ACTION_RW, 0);
	u64 base = (1ULL << 34) | 0x4000;
	u32 tmp;
	int err;

	/* Get PCIe Monitor ABI */
	err = nfp_cpp_readl(cpp, cls, base + NFP_MON_PCIE_MAGIC, &tmp);
	if (err < 0)
		return err;

	/* Mask off ABI minor */
	tmp &= ~0xf;

	if (tmp != NFP_MON_PCIE_ABI(0))
		return 0;

	return nfp_cpp_writel(cpp, cls, base + NFP_MON_PCIE_CTL(pci), flags);
}

/* Perform a soft reset of the NFP6000:
 *   - Disable traffic ingress
 *   - Verify all NBI MAC packet buffers have returned
 *   - Wait for PCIE DMA Queues to empty
 *   - Stop all MEs
 *   - Clear all PCIe DMA Queues
 *   - Reset MAC NBI gaskets
 *   - Verify that all NBI/MAC buffers/credits have returned
 *   - Soft reset subcomponents relevant to this model
 *     - TODO: Crypto reset
 */
static int nfp6000_reset_soft(struct nfp_cpp *cpp)
{
	struct nfp_nbi_dev *nbi[2] = {};
	struct nfp_resource *res;
	int mac_enable[2];
	int i, p, err, nbi_mask = 0;
	u32 bpe[2][32];
	int bpes[2];

	/* Lock out the MAC from any stats updaters,
	 * such as the NSP
	 */
	res = nfp_resource_acquire(cpp, NFP_RESOURCE_MAC_STATISTICS);
	if (!res)
		return -EBUSY;

	for (i = 0; i < 2; i++) {
		u32 tmp;
		int state;

		err = nfp_power_get(cpp, NFP6000_DEVICE_NBI(i, 0), &state);
		if (err < 0) {
			if (err == -ENODEV) {
				nbi[i] = NULL;
				continue;
			}
			goto exit;
		}

		if (state != NFP_DEVICE_STATE_ON) {
			nbi[i] = NULL;
			continue;
		}

		nbi[i] = nfp_nbi_open(cpp, i);
		if (!nbi[i])
			continue;

		nbi_mask |= BIT(i);

		err = nfp_nbi_mac_regr(nbi[i], NFP_NBI_MACX_CSR,
				       NFP_NBI_MACX_MAC_BLOCK_RST,
				       &tmp);
		if (err < 0)
			goto exit;

		mac_enable[i] = 0;
		if (!(tmp & NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY0_STAT_RST))
			mac_enable[i] |= BIT(0);
		if (!(tmp & NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY1_STAT_RST))
			mac_enable[i] |= BIT(1);

		/* No MACs at all? Then we don't care. */
		if (mac_enable[i] == 0) {
			nfp_nbi_close(nbi[i]);
			nbi[i] = NULL;
			continue;
		}

		/* Make sure we have the BPE list */
		err = bpe_lookup(cpp, i, &bpe[i][0], ARRAY_SIZE(bpe[i]));
		if (err < 0)
			goto exit;

		bpes[i] = err;
	}

	/* Verify that traffic ingress is disabled */
	for (i = 0; i < 2; i++) {
		if (!nbi[i])
			continue;

		for (p = 0; p < 24; p++) {
			u32 r, mask, tmp;

			mask =  NFP_NBI_MACX_ETH_SEG_CMD_CONFIG_ETH_RX_ENA;
			r =  NFP_NBI_MACX_ETH_SEG_CMD_CONFIG(p % 12);

			err = nfp_nbi_mac_regr(nbi[i],
					       NFP_NBI_MACX_ETH(p / 12),
					       r, &tmp);
			if (err < 0) {
				nfp_err(cpp, "Can't verify RX is disabled for port %d.%d\n",
					i, p);
				goto exit;
			}

			if (tmp & mask) {
				nfp_warn(cpp, "HAZARD: RX for traffic was not disabled by firmware for port %d.%d\n",
					 i, p);
			}

			err = nfp_nbi_mac_regw(nbi[i], NFP_NBI_MACX_ETH(p / 12),
					       r, mask, 0);
			if (err < 0) {
				nfp_err(cpp, "Can't disable RX traffic for port %d.%d\n",
					i, p);
				goto exit;
			}
		}
	}

	/* Wait for packets to drain from NBI to NFD or to be freed.
	 * Worst case guess is:
	 *      512 pkts per CTM, 12 MEs per CTM, 800MHz clock rate
	 *      ~1000 cycles to sink a single packet.
	 *      512/12 = 42 pkts per ME, therefore 1000*42=42,000 cycles
	 *      42K cycles at 800Mhz = 52.5us. Round up to 60us.
	 *
	 * TODO: Account for cut-through traffic.
	 */
	usleep_range(60, 100);

	/* Verify all NBI MAC packet buffers have returned */
	for (i = 0; i < 2; i++) {
		if (!nbi[i])
			continue;

		err = nfp6000_nbi_mac_check_freebufs(cpp, nbi[i]);
		if (err < 0)
			goto exit;
	}

	/* Wait for PCIE DMA Queues to empty.
	 *
	 *  How we calculate the wait time for DMA Queues to be empty:
	 *
	 *  Max CTM buffers that could be enqueued to one island:
	 *  512 x (7 ME islands + 2 other islands) = 4608 CTM buffers
	 *
	 *  The minimum rate at which NFD would process that ring would
	 *  occur if NFD records the queues as "up" so that it DMAs the
	 *  whole packet to the host, and if the CTM buffers in the ring
	 *  are all associated with jumbo frames.
	 *
	 *  Jumbo frames are <10kB, and NFD 3.0 processes ToPCI jumbo
	 *  frames at ±35Gbps (measured on star fighter card).
	 *  35e9 / 10 x 1024 x 8 = 427kpps.
	 *
	 *  The time to empty a ring holding 4608 packets at 427kpps
	 *  is 10.79ms.
	 *
	 *  To be conservative we round up to nearest whole number, i.e. 11ms.
	 */
	mdelay(11);

	/* Check all PCIE DMA Queues are empty. */
	for (i = 0; i < 4; i++) {
		int state;
	int empty;
		unsigned int subdev = NFP6000_DEVICE_PCI(i,
					NFP6000_DEVICE_PCI_PCI);

		err = nfp_power_get(cpp, subdev, &state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			goto exit;
		}

		if (state != NFP_DEVICE_STATE_ON)
			continue;

		err = nfp6000_check_empty_pcie_dma_queues(cpp, i, &empty);
		if (err < 0)
			goto exit;

		if (!empty) {
			nfp_err(cpp, "PCI%d DMA queues did not drain\n", i);
			err = -ETIMEDOUT;
			goto exit;
		}

		/* Set ARM PCIe Monitor to defaults */
		err = nfp6000_pcie_monitor_set(cpp, i, 0);
		if (err < 0)
			goto exit;
	}

	/* Stop all MEs */
	for (i = 0; i < 64; i++) {
		err = nfp6000_stop_me_island(cpp, i);
		if (err < 0)
			goto exit;
	}

	/* Verify again that PCIe DMA Queues are now empty */
	for (i = 0; i < 4; i++) {
		int state;
	int empty;
		unsigned int subdev = NFP6000_DEVICE_PCI(i,
					NFP6000_DEVICE_PCI_PCI);

		err = nfp_power_get(cpp, subdev, &state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			goto exit;
		}

		if (state != NFP_DEVICE_STATE_ON)
			continue;

		err = nfp6000_check_empty_pcie_dma_queues(cpp, i, &empty);
		if (err < 0)
			goto exit;

		if (!empty) {
			nfp_err(cpp, "PCI%d DMA queue is not empty\n", i);
			err = -ETIMEDOUT;
			goto exit;
		}
	}

	/* Clear all PCIe DMA Queues */
	for (i = 0; i < 4; i++) {
		unsigned int subdev = NFP6000_DEVICE_PCI(i,
					NFP6000_DEVICE_PCI_PCI);
		int state;
		const u32 pci = NFP_CPP_ISLAND_ID(NFP_CPP_TARGET_PCIE,
						       3, 0, i + 4);

		err = nfp_power_get(cpp, subdev, &state);
		if (err < 0) {
			if (err == -ENODEV)
				continue;
			goto exit;
		}

		if (state != NFP_DEVICE_STATE_ON)
			continue;

		for (p = 0; p < 256; p++) {
			u32 q = NFP_PCIE_Q(p);

			err = nfp_cpp_writel(cpp, pci, q + NFP_QCTLR_STS_LO,
					     NFP_QCTLR_STS_LO_RPTR_ENABLE);
			if (err < 0)
				goto exit;

			err = nfp_cpp_writel(cpp, pci, q + NFP_QCTLR_STS_HI,
					     NFP_QCTLR_STS_HI_EMPTY);
			if (err < 0)
				goto exit;
		}
	}

	/* Reset MAC NBI gaskets */
	for (i = 0; i < 2; i++) {
		u32 mask = NFP_NBI_MACX_MAC_BLOCK_RST_MAC_TX_RST_MPB |
			NFP_NBI_MACX_MAC_BLOCK_RST_MAC_RX_RST_MPB |
			NFP_NBI_MACX_MAC_BLOCK_RST_MAC_TX_RST_CORE |
			NFP_NBI_MACX_MAC_BLOCK_RST_MAC_RX_RST_CORE |
			NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY0_STAT_RST |
			NFP_NBI_MACX_MAC_BLOCK_RST_MAC_HY1_STAT_RST;

		if (!nbi[i])
			continue;

		err = nfp_nbi_mac_regw(nbi[i], NFP_NBI_MACX_CSR,
				       NFP_NBI_MACX_MAC_BLOCK_RST, mask, mask);
		if (err < 0)
			goto exit;

		err = nfp_nbi_mac_regw(nbi[i], NFP_NBI_MACX_CSR,
				       NFP_NBI_MACX_MAC_BLOCK_RST, mask, 0);
		if (err < 0)
			goto exit;
	}

	/* Wait for the reset to propagate */
	usleep_range(60, 100);

	/* Verify all NBI MAC packet buffers have returned */
	for (i = 0; i < 2; i++) {
		if (!nbi[i])
			continue;

		err = nfp6000_nbi_mac_check_freebufs(cpp, nbi[i]);
		if (err < 0)
			goto exit;
	}

	/* Verify that all NBI/MAC credits have returned */
	for (i = 0; i < 2; i++) {
		if (!nbi[i])
			continue;

		err = nfp6000_nbi_check_dma_credits(cpp, nbi[i],
						    &bpe[i][0], bpes[i]);
		if (err < 0)
			goto exit;
	}

	/* Soft reset subcomponents relevant to this model */
	err = nfp6000_island_reset(cpp, nbi_mask);
	if (err < 0)
		goto exit;

	err = nfp6000_island_on(cpp, nbi_mask);
	if (err < 0)
		goto exit;

exit:
	/* No need for NBI access anymore.. */
	for (i = 0; i < 2; i++) {
		if (nbi[i])
			nfp_nbi_close(nbi[i]);
	}

	nfp_resource_release(res);

	return err;
}

/**
 * nfp_reset_soft() - Perform a soft reset of the NFP
 * @cpp:	NFP CPP handle
 *
 * Return: 0, or -ERRNO
 */
int nfp_reset_soft(struct nfp_cpp *cpp)
{
	struct nfp_cpp_area *area;
	struct nfp_resource *res;
	int i, err;

	/* Claim the nfp.nffw resource page */
	res = nfp_resource_acquire(cpp, NFP_RESOURCE_NFP_NFFW);
	if (IS_ERR(res)) {
		nfp_err(cpp, "Can't aquire %s resource\n",
			NFP_RESOURCE_NFP_NFFW);
		return -EBUSY;
	}

	err = nfp6000_reset_soft(cpp);
	if (err < 0)
		goto exit;

	/* Clear all NFP NFFW page */
	area = nfp_cpp_area_alloc_acquire(cpp, nfp_resource_cpp_id(res),
					  nfp_resource_address(res),
					  nfp_resource_size(res));
	if (!area) {
		nfp_err(cpp, "Can't acquire area for %s resource\n",
			NFP_RESOURCE_NFP_NFFW);
		err = -ENOMEM;
		goto exit;
	}

	for (i = 0; i < nfp_resource_size(res); i += 8) {
		err = nfp_cpp_area_writeq(area, i, 0);
		if (err < 0)
			break;
	}
	nfp_cpp_area_release_free(area);

	if (err < 0) {
		nfp_err(cpp, "Can't erase area of %s resource\n",
			NFP_RESOURCE_NFP_NFFW);
		goto exit;
	}

	/* Invalidate old MIP and rtsymtab data */
	nfp_mip_reload(cpp);

	err = 0;

exit:
	nfp_resource_release(res);

	return err;
}
