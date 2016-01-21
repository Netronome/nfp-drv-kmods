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
 * nfp_power.c
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

#include <linux/kernel.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp6000/nfp_xpb.h"
#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_pl.h"

/* Define this to include code perform a full clear of
 * all ECCable SRAMs when an island is moved from 'reset'
 * to 'on' state.
 *
 * FOR DEBUG ONLY: This adds a significant amount of time,
 *                 and is not required for production use.
 */
#undef NFP_ECC_FULL_CLEAR

#define CTMX_BASE				(0x60000)
#define NFP_CTMX_CFG				(CTMX_BASE + 0x000000)
#define NFP_CTMX_MISC				(CTMX_BASE + 0x020000)

#define	NFP_ECC_CLEARERRORS			0x00000038
#define	NFP_ECC_ECCENABLE_ENABLE		(1 << 0)
#define	NFP_ECC_ECCENABLE			0x00000000

#define	NFP_PCIE_SRAM		(0x000000)
#define	NFP_PCIE_Q(_x)		(0x080000 + ((_x) & 0xff) * 0x800)
#define PCIEX_BASE		(0xa0000)
#define NFP_PCIEX_IM		(PCIEX_BASE + 0x030000)

#define NBIX_BASE		(0xa0000)
#define NFP_NBIX_CSR				(NBIX_BASE + 0x2f0000)
#define NFP_NBIX_CSR_NBIMUXLATE			0x00000000
#define   NFP_NBIX_CSR_NBIMUXLATE_ISLAND1(_x)	(((_x) & 0x3f) << 6)
#define   NFP_NBIX_CSR_NBIMUXLATE_ACCMODE(_x)	(((_x) & 0x7) << 13)
#define   NFP_NBIX_CSR_NBIMUXLATE_ISLAND0(_x)	(((_x) & 0x3f) << 0)
#define	NFP_NBI_TMX		(NBIX_BASE + 0x040000)
#define	NFP_NBI_TMX_Q		(NFP_NBI_TMX + 0x10000)
#define	NFP_NBI_TM		(0x200000)
#define	NFP_NBI_TM_Q_QUEUEDROPCOUNTCLEAR(_x) \
	(0x00003000 + (0x4 * ((_x) & 0x3ff)))
#define	NFP_NBI_TM_TMHEADTAILSRAM_TMHEADTAILENTRY(_x) \
	(0x00068000 + (0x8 * ((_x) & 0x3ff)))
#define	NFP_NBI_TM_TMPKTSRAM_TMPKTSRAMENTRY(_x) \
	(0x00060000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_TM_TMREORDERBUF_TMREORDERBUFENTRY(_x) \
	(0x00058000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_TM_TMSLOWDESCSRAM_TMSLOWDESCSRAMENTRY(_x) \
	(0x00050000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_TM_TMBDSRAM_NBIBDSRAMENTRY(_x) \
	(0x00040000 + (0x8 * ((_x) & 0xfff)))
#define	NFP_NBI_TM_TMDESCSRAM_TMDESCSRAMENTRY(_x) \
	(0x00000000 + (0x8 * ((_x) & 0x7fff)))
#define	NFP_NBI_DMA	(0x000000)
#define	NFP_NBI_DMA_BDSRAM_NBIDMABDSRAMENTRY(_x) \
	(0x00000000 + (0x8 * ((_x) & 0xfff)))
#define	NFP_ARM_GCSR	0x400000
#define	NFP_ARM_GCSR_SIDEDOORDATAWRITEHI	0x00000190
#define	NFP_ARM_GCSR_SIDEDOORCECORE_SIDEDOORCE(_x)	(((_x) & 0x3ffff) << 0)
#define	NFP_ARM_GCSR_SIDEDOORBECORE_SIDEDOORCOREWE	(1 << 24)
#define	NFP_ARM_GCSR_SIDEDOORCECORE_SIDEDOORCOREENABLE	(1 << 31)
#define	NFP_ARM_GCSR_SIDEDOORCECORE	0x00000180
#define	NFP_ARM_GCSR_SIDEDOORBECORE_SIDEDOORCOREBE(_x) \
	(((_x) & 0xffffff) << 0)
#define	NFP_CTMX_PKT	(CTMX_BASE + 0x010000)
#define	NFP_ECC_ERRORCOUNTSRESET	0x0000002c

#define	NFP_ARM_GCSR_SIDEDOORDATAWRITELO	0x0000018c
#define	NFP_ARM_GCSR_SIDEDOORDATAWRITEHI	0x00000190
#define	NFP_ARM_GCSR_SIDEDOORBECORE	0x00000194
#define	NFP_ARM_GCSR_SIDEDOORADDRESS	0x00000188
#define	NFP_ARM_GCSR_SIDEDOORCEPL310	0x00000184
#define	NFP_ARM_GCSR_SIDEDOORWEPL310	0x0000019c

#define	NFP_ARM_GCSR_SIDEDOORCEPL310_SIDEDOORCE(_x) \
	(((_x) & 0x3ffff) << 0)
#define	NFP_ARM_GCSR_SIDEDOORCEPL310_SIDEDOORPL310ENABLE	(1 << 31)
#define	NFP_NBI_DMA_BCSRAM_NBIDMABCSRAMENTRY(_x) \
	(0x0000a000 + (0x8 * ((_x) & 0x3ff)))

#define	NFP_NBI_PMX	(NBIX_BASE + 0x280000)
#define	NFP_NBI_PMX_OPCODE	(NFP_NBI_PMX + 0x00000)
#define	NFP_NBI_PMX_RDATA	(NFP_NBI_PMX + 0x20000)
#define	NFP_NBI_PCX	(NBIX_BASE + 0x180000)
#define	NFP_NBI_PCX_PE	(NFP_NBI_PCX + 0x00000)
#define	NFP_NBI_PCX_PE_PICOENGINERUNCONTROL	0x00000008
#define	NFP_NBI_PCX_CHAR	(NFP_NBI_PCX + 0x10000)
#define	NFP_NBI_PC	(0x300000)
#define	NFP_NBI_PC_ALLLOCALSRAM_NBIPRETABLELUT8(_x) \
	(0x00000000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM0_NBIPRETABLELUT8(_x) \
	(0x00030000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM1_NBIPRETABLELUT8(_x) \
	(0x00004000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM2_NBIPRETABLELUT8(_x) \
	(0x00008000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM3_NBIPRETABLELUT8(_x) \
	(0x0000c000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM4_NBIPRETABLELUT8(_x) \
	(0x00010000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM5_NBIPRETABLELUT8(_x) \
	(0x00014000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM6_NBIPRETABLELUT8(_x) \
	(0x00018000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM7_NBIPRETABLELUT8(_x) \
	(0x0001c000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM8_NBIPRETABLELUT8(_x) \
	(0x00020000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM9_NBIPRETABLELUT8(_x) \
	(0x00024000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM10_NBIPRETABLELUT8(_x) \
	(0x00028000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_LOCALSRAM11_NBIPRETABLELUT8(_x) \
	(0x0002c000 + (0x8 * ((_x) & 0x7ff)))
#define	NFP_NBI_PC_SHAREDSRAM0_NBIPRETABLELUT8(_x) \
	(0x00090000 + (0x8 * ((_x) & 0x1fff)))
#define	NFP_NBI_PC_SHAREDSRAM1_NBIPRETABLELUT8(_x) \
	(0x000b0000 + (0x8 * ((_x) & 0x1fff)))
#define	NFP_NBI_PC_PACKETSRAM_NBIPRETABLELUT8(_x) \
	(0x000c0000 + (0x8 * ((_x) & 0x7fff)))
#define	NFP_NBI_PC_ALLSHAREDSRAM_NBIPRETABLELUT8(_x) \
	(0x00080000 + (0x8 * ((_x) & 0x1fff)))
#define	NFP_QCTLR_CFGSTATUSLOW	0x00000008
#define	NFP_QCTLR_CFGSTATUSLOW_READPTR_EN	(1	<< 31)
#define	NFP_QCTLR_CFGSTATUSLOW_READPTR(_x)	(((_x)	& 0x3ffff) << 0)
#define	NFP_QCTLR_CFGSTATUSHIGH	0x0000000c
#define	NFP_QCTLR_CFGSTATUSHIGH_EMPTY	(1	<< 26)
#define	NFP_QCTLR_CFGSTATUSHIGH_WRITEPTR(_x)	(((_x) & 0x3ffff) << 0)

#define	NFP_NBI_PCX_CHAR_CreditConfig		0x00000030
#define	NFP_NBI_PCX_CHAR_CreditConfig_BufCmpCredit(_x)    (((_x) & 0x7) << 0)
#define	NFP_NBI_PCX_CHAR_CreditConfig_BufCmpCredit_of(_x) (((_x) >> 0) & 0x7)

static const struct {
	u32 reset_mask;
	u32 enable_mask;
} target_to_mask[] = {
	[NFP3200_DEVICE_ARM] = {
		.reset_mask = NFP_PL_RE_ARM_CORE_RESET,
		.enable_mask = NFP_PL_RE_ARM_CORE_ENABLE,
	},
	[NFP3200_DEVICE_ARM_GASKET] = {
		.reset_mask = NFP_PL_RE_ARM_GASKET_RESET,
		.enable_mask = NFP_PL_RE_ARM_GASKET_ENABLE,
	},
	[NFP3200_DEVICE_DDR0] = {
		.reset_mask = NFP_PL_RE_DDR0_RESET,
		.enable_mask = NFP_PL_RE_DDR0_ENABLE,
	},
	[NFP3200_DEVICE_DDR1] = {
		.reset_mask = NFP_PL_RE_DDR1_RESET,
		.enable_mask = NFP_PL_RE_DDR1_ENABLE,
	},
	[NFP3200_DEVICE_MECL0] = {
		.reset_mask = NFP_PL_RE_MECL_ME_RESET(1),
		.enable_mask = NFP_PL_RE_MECL_ME_ENABLE(1),
	},
	[NFP3200_DEVICE_MECL1] = {
	.reset_mask = NFP_PL_RE_MECL_ME_RESET(2),
		.enable_mask = NFP_PL_RE_MECL_ME_ENABLE(2),
	},
	[NFP3200_DEVICE_MECL2] = {
	.reset_mask = NFP_PL_RE_MECL_ME_RESET(4),
		.enable_mask = NFP_PL_RE_MECL_ME_ENABLE(4),
	},
	[NFP3200_DEVICE_MECL3] = {
		.reset_mask = NFP_PL_RE_MECL_ME_RESET(8),
		.enable_mask = NFP_PL_RE_MECL_ME_ENABLE(8),
	},
	[NFP3200_DEVICE_MECL4] = {
		.reset_mask = NFP_PL_RE_MECL_ME_RESET(16),
		.enable_mask = NFP_PL_RE_MECL_ME_ENABLE(16),
	},
	[NFP3200_DEVICE_MSF0] = {
		.reset_mask = NFP_PL_RE_MSF0_RESET,
		.enable_mask = NFP_PL_RE_MSF0_ENABLE,
	},
	[NFP3200_DEVICE_MSF1] = {
		.reset_mask = NFP_PL_RE_MSF1_RESET,
		.enable_mask = NFP_PL_RE_MSF1_ENABLE,
	},
	[NFP3200_DEVICE_MU] = {
		.reset_mask = NFP_PL_RE_MU_RESET,
		.enable_mask = NFP_PL_RE_MU_ENABLE,
	},
	[NFP3200_DEVICE_PCIE] = {
	.reset_mask = NFP_PL_RE_PCIE_RESET,
		.enable_mask = NFP_PL_RE_PCIE_ENABLE,
	},
	[NFP3200_DEVICE_QDR0] = {
		.reset_mask = NFP_PL_RE_QDR0_RESET,
		.enable_mask = NFP_PL_RE_QDR0_ENABLE,
	},
	[NFP3200_DEVICE_QDR1] = {
		.reset_mask = NFP_PL_RE_QDR1_RESET,
		.enable_mask = NFP_PL_RE_QDR1_ENABLE,
	},
	[NFP3200_DEVICE_CRYPTO] = {
		.reset_mask = NFP_PL_RE_CRYPTO_RESET,
		.enable_mask = NFP_PL_RE_CRYPTO_ENABLE,
	},
};

static int nfp3200_reset_get(struct nfp_cpp *cpp, unsigned int subdevice,
			     int *reset, int *enable)
{
	u32 r_mask, e_mask, csr;
	int err;

	if (subdevice >= ARRAY_SIZE(target_to_mask))
		return -EINVAL;

	r_mask = target_to_mask[subdevice].reset_mask;
	e_mask = target_to_mask[subdevice].enable_mask;

	if (r_mask == 0 && e_mask == 0)
		return -EINVAL;

	/* Special exception for ARM:
	 *   The NFP_PL_STRAPS register bit 5 overrides the
	 *   reset and enable bits, so if it is on, then
	 *   force them on.
	 */
	if (subdevice == NFP3200_DEVICE_ARM) {
		err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_STRAPS, &csr);
		if (err < 0)
			return err;
	} else {
		csr = 0;
	}

	if (csr & NFP_PL_STRAPS_CFG_PROM_BOOT) {
		csr = (r_mask | e_mask);
	} else {
		err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_RE, &csr);
		if (err < 0)
			return err;
	}

	if (reset)
		*reset = (csr & r_mask) ? 1 : 0;

	if (enable)
		*enable = (csr & e_mask) ? 1 : 0;

	return 0;
}

static int nfp3200_reset_set(struct nfp_cpp *cpp, unsigned int subdevice,
			     int reset, int enable)
{
	u32 csr, r_mask, e_mask;
	u16 interface;
	int err;

	if (subdevice >= ARRAY_SIZE(target_to_mask))
		return -EINVAL;

	/* Disallow changes to the PCIE core if that
	 * is our interface to the device.
	 */
	interface = nfp_cpp_interface(cpp);
	if ((NFP_CPP_INTERFACE_TYPE_of(interface) ==
	     NFP_CPP_INTERFACE_TYPE_PCI) &&
	    (subdevice == NFP3200_DEVICE_PCIE))
		return -EBUSY;

	r_mask = target_to_mask[subdevice].reset_mask;
	e_mask = target_to_mask[subdevice].enable_mask;

	if (r_mask == 0 && e_mask == 0)
		return -EINVAL;

	err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_RE, &csr);
	if (err)
		return err;

	csr = (csr & ~r_mask) | (reset ? r_mask : 0);
	csr = (csr & ~e_mask) | (enable ? e_mask : 0);

	err = nfp_xpb_writel(cpp, NFP_XPB_PL + NFP_PL_RE, csr);
	if (err)
		return err;

	/* If it's the ARM device, clear the
	 * forced setting from the strap register.
	 */
	if (subdevice == NFP3200_DEVICE_ARM ||
	    subdevice == NFP3200_DEVICE_ARM_GASKET) {
		err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_STRAPS, &csr);
		if (err)
			return err;

		csr &= ~NFP_PL_STRAPS_CFG_PROM_BOOT;
		err = nfp_xpb_writel(cpp, NFP_XPB_PL + NFP_PL_STRAPS, csr);
		if (err)
			return err;
	}

	return 0;
}

/* The IMB island mask lists all islands with
 * an IMB. Since the number of islands without
 * an IMB is smaller than the number with,
 * we invert a mask of those without to get
 * the list of those with an IMB.
 *
 * Funny C syntax:
 *
 * 0xFULL => (unsigned long long)0xf
 */
static const u64 imb_island_mask = ~(0 | (0xFULL << 8)	/* NBI */
				     | (0xFULL << 24)	/* IMU */
				     | (0xFULL << 28)	/* EMU */
	);

static int nfp6000_reset_get(struct nfp_cpp *cpp, unsigned int subdevice,
			     int *reset, int *enable)
{
	u32 csr;
	int island, mask, err;

	if (subdevice < NFP6000_DEVICE(1, 0) ||
	    subdevice > NFP6000_DEVICE(63, 7))
		return -EINVAL;

	island = NFP6000_DEVICE_ISLAND_of(subdevice);
	mask = (1 << NFP6000_DEVICE_UNIT_of(subdevice));

	if (!((1ULL << island) & nfp_cpp_island_mask(cpp)))
		return -ENODEV;

	err = nfp_xpb_readl(cpp, (island << 24) | 0x45400, &csr);
	if (err < 0)
		return err;

	*enable = (((csr >> 24) & mask) == mask) ? 1 : 0;
	*reset = (((csr >> 16) & mask) == mask) ? 1 : 0;

	return 0;
}

static int nfp6000_reset_set(struct nfp_cpp *cpp, unsigned int subdevice,
			     int reset, int enable)
{
	u32 csr, mem;
	int island, mask, err;

	if (subdevice < NFP6000_DEVICE(1, 0) ||
	    subdevice > NFP6000_DEVICE(63, 7))
		return -EINVAL;

	island = NFP6000_DEVICE_ISLAND_of(subdevice);
	mask = (1 << NFP6000_DEVICE_UNIT_of(subdevice));

	if (!((1ULL << island) & nfp_cpp_island_mask(cpp)))
		return -ENODEV;

	err = nfp_xpb_readl(cpp, (island << 24) | 0x45400, &csr);
	if (err < 0)
		return err;

	err = nfp_xpb_readl(cpp, (island << 24) | 0x45404, &mem);
	if (err < 0)
		return err;

	/* Determine if the island was down
	 */
	csr &= ~((mask << 24) | (mask << 16));

	if (enable)
		csr |= mask << 24;

	if (reset)
		csr |= mask << 16;

	if (enable || reset)
		mem |= mask;

	/* We must NEVER put the ARM Island into reset, otherwise
	 * there will be no ability to access the XPBM interface!
	 */
	if (island == 1) {
		csr |= 0x01010000;
		mem |= 0x01;
	}

	err = nfp_xpb_writel(cpp, (island << 24) | 0x45404, mem);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, (island << 24) | 0x45400, csr);
	if (err < 0)
		return err;

	return 0;
}

/**
 * nfp_power_get() - Get current device state
 * @nfp:	   NFP Device handle
 * @subdevice:     NFP subdevice
 * @state:	 Power state
 *
 * Return: 0, or -ERRNO
 */
int nfp_power_get(struct nfp_device *nfp, unsigned int subdevice, int *state)
{
	struct nfp_cpp *cpp;
	u32 model;
	int err, reset = 0, enable = 0;

	cpp = nfp_device_cpp(nfp);

	model = nfp_cpp_model(cpp);

	if (NFP_CPP_MODEL_IS_3200(model))
		err = nfp3200_reset_get(cpp, subdevice, &reset, &enable);
	else if (NFP_CPP_MODEL_IS_6000(model))
		err = nfp6000_reset_get(cpp, subdevice, &reset, &enable);
	else
		err = -EINVAL;

	/* Compute P0..P3 from reset/enable
	 */
	if (err >= 0)
		*state = (reset ? 0 : 2) | (enable ? 0 : 1);

	return err;
}

static int eccmon_enable(struct nfp_cpp *cpp, u32 ecc)
{
	u32 tmp;
	int err;

	err = nfp_xpb_writel(cpp, ecc + NFP_ECC_ECCENABLE,
			     NFP_ECC_ECCENABLE_ENABLE);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, ecc + NFP_ECC_CLEARERRORS, 0);
	if (err < 0)
		return err;

	err = nfp_xpb_readl(cpp, ecc + NFP_ECC_ERRORCOUNTSRESET, &tmp);
	if (err < 0)
		return err;

	return 0;
}

#ifdef NFP_ECC_FULL_CLEAR
static int memzap(struct nfp_cpp *cpp, u32 cpp_id, u64 addr, u64 len, u64 value)
{
	struct nfp_cpp_area *area;
	const int mask = sizeof(value) - 1;
	u64 offset = 0;

	if ((len & mask) || (addr & mask))
		return -EINVAL;

	area = nfp_cpp_area_alloc_acquire(cpp, cpp_id, addr, len);
	if (!area)
		return -EINVAL;

	for (; len > 0; len -= sizeof(value), offset += sizeof(value))
		nfp_cpp_area_writeq(area, offset, value);

	nfp_cpp_area_release_free(area);

	return 0;
}

static int muqueue_zap(struct nfp_cpp *cpp, int island)
{
	u32 mum = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0);
	u32 muq = NFP_CPP_ID(NFP_CPP_TARGET_MU, 16, 0);
	int err, i;
	struct nfp_cpp_explicit *expl;
	u64 addr;

	addr = (1ULL << 39) | ((u64)island << 32);
	if (addr >= 24 && addr <= 27)
		addr |= (2 * 1024 * 1024);

	/* Write an empty descriptor at address 0 of the MU,
	 * plus a full cache line
	 */
	for (i = 0; i < 64; i += 8) {
		err = nfp_cpp_writeq(cpp, mum, addr + i, 0);
		if (err < 0)
			return err;
	}

	expl = nfp_cpp_explicit_acquire(cpp);
	if (!expl)
		return -ENOMEM;

	err = nfp_cpp_explicit_set_target(expl, muq, 0, ~0);
	if (err < 0)
		goto exit;

	err = nfp_cpp_explicit_set_posted(expl, 1, 0, 0, 0, 0);
	if (err < 0)
		goto exit;

	for (i = 0; i < 1024; i++) {
		err = nfp_cpp_explicit_set_data(expl, 0, i);
		if (err < 0)
			break;

		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0)
			break;
	}

exit:
	nfp_cpp_explicit_release(expl);

	return err;
}
#endif /* NFP_ECC_FULL_CLEAR */

struct ecc_location {
	u32 base;
	int count;
	int unit;
};

static struct ecc_location const arm_ecc[] = {
	{ 0x0d0000, 1 },
	{ 0x110000, 1 },
	{ 0x120000, 1 },
	{ 0x130000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x140000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x150000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x160000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x170000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x180000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x190000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x1a0000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x1b0000, 1, NFP6000_DEVICE_ARM_ARM },
	{ 0x1c0000, 1, NFP6000_DEVICE_ARM_ARM },
}, pci_ecc[] = {
	{ 0x110000, 1, NFP6000_DEVICE_PCI_PCI },   /* Queue controller ECC */
	{ 0x120000, 4, NFP6000_DEVICE_PCI_PCI },   /* SRAM ECC */
}, cry_ecc[] = {
	{ 0x120000, 1, NFP6000_DEVICE_CRP_CRP },
	{ 0x130000, 16, NFP6000_DEVICE_CRP_CRP },
}, imu_ecc[] = {
	{ 0x210000, 3 },
	{ 0x220000, 2 },
	{ 0x230000, 4 },
	{ 0x240000, 1 },
}, emu_ecc[] = {
	{ 0x1c0000, 16 },			   /*  DCache */
	{ 0x420000, 16, NFP6000_DEVICE_EMU_DDR0 }, /*  Data mover 0 */
	{ 0x430000, 16, NFP6000_DEVICE_EMU_DDR1 }, /*  Data mover 1 */
	{ 0x490000, 16, NFP6000_DEVICE_EMU_DDR0 }, /*  Queue controller */
	{ 0x510000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 0 */
	{ 0x530000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 1 */
	{ 0x550000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 2 */
	{ 0x570000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 3 */
	{ 0x590000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 4 */
	{ 0x5b0000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 5 */
	{ 0x5d0000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 6 */
	{ 0x5f0000, 1,  NFP6000_DEVICE_EMU_DDR0 }, /*  TCache 7 */
}, ila_ecc[] = {
	{ 0x120000, 4,  NFP6000_DEVICE_ILA_ILA },
	{ 0x130000, 1,  NFP6000_DEVICE_ILA_ILA },
}, nbi_ecc[] = {
	{ 0x110000, 1 },
	{ 0x120000, 1 },
	{ 0x130000, 16 },
	{ 0x190000, 2 },
	{ 0x1c0000, 2 },
	{ 0x1e0000, 2 },
	{ 0x1f0000, 2 },
	{ 0x200000, 1 },
	{ 0x210000, 8 },
	{ 0x220000, 2 },
	{ 0x230000, 2 },
	{ 0x250000, 1 },
	{ 0x350000, 12 },
	{ 0x360000, 2 },
	{ 0x370000, 8 },
	{ 0x390000, 1 },
	{ 0x3b0000, 4 },
	{ 0x4a0000, 16, NFP6000_DEVICE_NBI_MAC4 },
	{ 0x4b0000, 16, NFP6000_DEVICE_NBI_MAC4 },
	{ 0x4c0000, 16, NFP6000_DEVICE_NBI_MAC4 },
	{ 0x4d0000, 16, NFP6000_DEVICE_NBI_MAC4 },
	{ 0x4e0000, 16, NFP6000_DEVICE_NBI_MAC4 },
	{ 0x4f0000, 16, NFP6000_DEVICE_NBI_MAC4 },
};

static int nfp6000_island_ecc_init(struct nfp_cpp *cpp,
				   int island, int unit,
				   const struct ecc_location *ecc, int eccs)
{
	int i, err;

	for (i = 0; i < eccs; i++) {
		u32 xpb;
		int j;

		if (ecc[i].unit != unit)
			continue;

		xpb = (island << 24) | ecc[i].base;
		for (j = 0; j < ecc[i].count; j++) {
			err = eccmon_enable(cpp, xpb | (j * 0x40));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int nfp6000_island_ctm_init(struct nfp_cpp *cpp, int island)
{
#ifdef NFP_ECC_FULL_CLEAR
	u32 id = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0);
	u64 addr = (1ULL << 39) | ((u64)island << 32);
#endif /* NFP_ECC_FULL_CLEAR */
	int i, err;
	u32 tmp;

	/* Set up island's CTMs for packet operation
	 * (all CTM islands have IMBs)
	 */
	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) + NFP_CTMX_MISC +
			     0x00c, 0xf);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) +
				  NFP_CTMX_CFG + 0x800,
				  (0xff000000 >> ((island & 1) * 8)) |
				  0xfc00 | (island << 2));
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) +
				  NFP_CTMX_PKT + 0x808, 0x1000);
	if (err < 0)
		return err;

	/* Read-back to ensure correct sequencing */
	err = nfp_xpb_readl(cpp, NFP_XPB_OVERLAY(island) +
				 NFP_CTMX_PKT + 0x808, &tmp);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) +
				  NFP_CTMX_PKT + 0x800, 0x104ff);
	if (err < 0)
		return err;

	/* Read-back to ensure correct sequencing */
	err = nfp_xpb_readl(cpp, NFP_XPB_OVERLAY(island) +
				 NFP_CTMX_PKT + 0x800, &tmp);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) +
				  NFP_CTMX_PKT + 0x804, 0x100ffff);
	if (err < 0)
		return err;

	/* Read-back to ensure correct sequencing */
	err = nfp_xpb_readl(cpp, NFP_XPB_OVERLAY(island) +
				 NFP_CTMX_PKT + 0x804, &tmp);
	if (err < 0)
		return err;

	err = nfp_xpb_writel(cpp, NFP_XPB_OVERLAY(island) +
				  NFP_CTMX_PKT + 0x804, 0x1ffff00);
	if (err < 0)
		return err;

#ifdef NFP_ECC_FULL_CLEAR
	err = memzap(cpp, id, addr, 0x40000, 0);
	if (err < 0)
		return err;
#endif /* NFP_ECC_FULL_CLEAR */

	for (i = 0; i < 12; i++)
		eccmon_enable(cpp, (island << 24) | 0x90000 | (0x40 * i));

	return 0;
}

static int nfp6000_island_cls_init(struct nfp_cpp *cpp, int island)
{
#ifdef NFP_ECC_FULL_CLEAR
	u32 id = NFP_CPP_ID(NFP_CPP_TARGET_CLS, NFP_CPP_ACTION_RW, 0);
	u64 addr = (u64)island << 34;
	int err;

	err = memzap(cpp, id, addr, 0x10000, 0);
	if (err < 0)
		return err;
#endif /* NFP_ECC_FULL_CLEAR */

	eccmon_enable(cpp, (island << 24) | 0xd0000);

	return 0;
}

static int nfp6000_island_arm_init(struct nfp_cpp *cpp, int island, int unit)
{
#ifdef NFP_ECC_FULL_CLEAR
	int i;
	u32 id = NFP_CPP_ID(NFP_CPP_TARGET_ARM, NFP_CPP_ACTION_RW, 0);

	if (unit != 0)
		goto exit_ecc;

	/* Clear the L1 cache via the sidedoor */
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORBECORE,
		       NFP_ARM_GCSR_SIDEDOORBECORE_SIDEDOORCOREWE |
		       NFP_ARM_GCSR_SIDEDOORBECORE_SIDEDOORCOREBE(~0));
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORCECORE,
		       NFP_ARM_GCSR_SIDEDOORCECORE_SIDEDOORCOREENABLE |
		       NFP_ARM_GCSR_SIDEDOORCECORE_SIDEDOORCE(0xffff));
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORDATAWRITELO, 0);
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORDATAWRITEHI, 0);

	for (i = 0; i < 0x800; i++) {
		nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
			       NFP_ARM_GCSR_SIDEDOORADDRESS,
			       0x80000000 | i);
	}

	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR + NFP_ARM_GCSR_SIDEDOORBECORE, 0);
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR + NFP_ARM_GCSR_SIDEDOORCECORE, 0);

	/* Clear the L2 cache via the sidedoor */
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORWEPL310, ~0);
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORCEPL310,
		       NFP_ARM_GCSR_SIDEDOORCEPL310_SIDEDOORPL310ENABLE |
		       NFP_ARM_GCSR_SIDEDOORCEPL310_SIDEDOORCE(0x1fffe));
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORDATAWRITELO, 0);
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
		       NFP_ARM_GCSR_SIDEDOORDATAWRITEHI, 0);

	for (i = 0; i < 0x80000; i += 0x8000) {
		int j;

		for (j = 0; j < 0x800; j += 4) {
			nfp_cpp_writel(cpp, id, NFP_ARM_GCSR +
				       NFP_ARM_GCSR_SIDEDOORADDRESS,
				       i | j);
		}
	}

	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR + NFP_ARM_GCSR_SIDEDOORWEPL310, 0);
	nfp_cpp_writel(cpp, id, NFP_ARM_GCSR + NFP_ARM_GCSR_SIDEDOORCEPL310, 0);

exit_ecc:
#endif /* NFP_ECC_FULL_CLEAR */
	return nfp6000_island_ecc_init(cpp, island, unit,
				       arm_ecc, ARRAY_SIZE(arm_ecc));
}

static int nfp6000_island_imu_init(struct nfp_cpp *cpp, int island, int unit)
{
#ifdef NFP_ECC_FULL_CLEAR
	u32 id = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0);
	u64 addr = (1ULL << 39) | ((u64)island << 32);
	int err;
#endif /* NFP_ECC_FULL_CLEAR */
	int i;

	if (unit == 0) {
#ifdef NFP_ECC_FULL_CLEAR
		/* DCache init and ECC enable */
		err = memzap(cpp, id, addr, 4 * 1024 * 1024, 0);
		if (err < 0)
			return err;

		/* MQueue init */
		err = muqueue_zap(cpp, island);
		if (err < 0)
			return err;
#endif /* NFP_ECC_FULL_CLEAR */

		for (i = 0; i < 16; i++)
			eccmon_enable(cpp, (island << 24) |
				      0x490000 | (i * 0x40));
	}

	/* Initialize other ECCs */
	return nfp6000_island_ecc_init(cpp, island, unit,
				       imu_ecc, ARRAY_SIZE(imu_ecc));
}

static int nfp6000_island_emu_init(struct nfp_cpp *cpp, int island, int unit)
{
#ifdef NFP_ECC_FULL_CLEAR
	int err;
	const u32 id = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 0);
	u64 addr = (u64)((island - 24) + 4) << 35;

	if (unit == NFP6000_DEVICE_EMU_CORE) {
		/* DCache init */
		err = memzap(cpp, id, addr, 3 * 1024 * 1024, 0);
		if (err < 0)
			return err;
	}

	/* EMU main ECC DRAM init is done by the main EMU initialization */

	if (unit == NFP6000_DEVICE_EMU_QUE) {
		/* MQueue init */
		err = muqueue_zap(cpp, island);
		if (err < 0)
			return err;
	}
#endif /* NFP_ECC_FULL_CLEAR */

	return nfp6000_island_ecc_init(cpp, island, unit,
				       emu_ecc, ARRAY_SIZE(emu_ecc));
}

static int nfp6000_island_nbi_b0_workarounds(struct nfp_cpp *cpp, int island)
{
	u32 val, model;
	int err;

	model = nfp_cpp_model(cpp);

	/* Apply only to A* and B* stepping silicon */
	if (!(NFP_CPP_MODEL_IS_6000(model) &&
	      NFP_CPP_MODEL_STEPPING_of(model) < 0x20))
		return 0;

	err = nfp_xpb_readl(cpp, NFP_XPB_ISLAND(island) +
			    NFP_NBI_PCX_CHAR +
			    NFP_NBI_PCX_CHAR_CreditConfig, &val);
	if (err < 0)
		return err;

	val &= ~0x7;
	val |= NFP_NBI_PCX_CHAR_CreditConfig_BufCmpCredit(2);
	return nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
			      NFP_NBI_PCX_CHAR +
			      NFP_NBI_PCX_CHAR_CreditConfig, val);
}

static int nfp6000_island_nbi_init(struct nfp_cpp *cpp, int island, int unit)
{
	if (unit == 0) {
		int err;
#ifdef NFP_ECC_FULL_CLEAR
		u32 nbi = NFP_CPP_ID(NFP_CPP_TARGET_NBI,
					  NFP_CPP_ACTION_RW, 0);
		u64 addr = (u64)(island - 8) << 38;
		int i;

		/* Initialize NbiDmaBD SRAM */
		err = memzap(cpp, nbi, addr + NFP_NBI_DMA +
			     NFP_NBI_DMA_BDSRAM_NBIDMABDSRAMENTRY(0),
			     4096 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize NbiDmaBC SRAM */
		err = memzap(cpp, nbi, addr + NFP_NBI_DMA +
			     NFP_NBI_DMA_BCSRAM_NBIDMABCSRAMENTRY(0),
			     1024 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize TMDescSram */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMDESCSRAM_TMDESCSRAMENTRY(0),
			     32768 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize NbiBDSram */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMBDSRAM_NBIBDSRAMENTRY(0),
			     4096 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize TMSlowDescSram */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMSLOWDESCSRAM_TMSLOWDESCSRAMENTRY(0),
			     2048 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize TMReorderBuf */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMREORDERBUF_TMREORDERBUFENTRY(0),
			     2048 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize TMPktSramEntry */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMPKTSRAM_TMPKTSRAMENTRY(0),
			     2048 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize TMHeadTailSram */
		err = memzap(cpp, nbi, addr + NFP_NBI_TM +
			     NFP_NBI_TM_TMHEADTAILSRAM_TMHEADTAILENTRY(0),
			     1024 * 8, 0);
		if (err < 0)
			return err;

		/* Initialize NFP_NBI_TM_Q_QUEUEDROPCOUNTCLEAR */
		for (i = 0; i < 0x400; i++) {
			u32 tmp;

			err = nfp_xpb_readl(cpp, NFP_XPB_ISLAND(island) +
					    NFP_NBI_TMX_Q +
					    NFP_NBI_TM_Q_QUEUEDROPCOUNTCLEAR(i),
					    &tmp);
			if (err < 0)
				return err;
		}

#define NBI_PCZAP(x, count) do { \
	for (i = 0; i < count; i++) { \
		err = nfp_cpp_writeq(cpp, nbi, addr + NFP_NBI_PC + \
				     x(i), 0); \
		if (err < 0)  \
			return err; \
	} \
} while (0)

		/* Initialize PC */
		nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) + NFP_NBI_PCX_PE +
			       NFP_NBI_PCX_PE_PICOENGINERUNCONTROL, 0x3ffffff1);
		NBI_PCZAP(NFP_NBI_PC_ALLLOCALSRAM_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM0_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM1_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM2_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM3_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM4_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM5_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM6_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM7_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM8_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM9_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM10_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_LOCALSRAM11_NBIPRETABLELUT8, 2048);
		NBI_PCZAP(NFP_NBI_PC_ALLSHAREDSRAM_NBIPRETABLELUT8, 8192);
		NBI_PCZAP(NFP_NBI_PC_SHAREDSRAM0_NBIPRETABLELUT8, 8192);
		NBI_PCZAP(NFP_NBI_PC_SHAREDSRAM1_NBIPRETABLELUT8, 8192);
		NBI_PCZAP(NFP_NBI_PC_PACKETSRAM_NBIPRETABLELUT8, 32768);

		err = memzap(cpp, nbi, addr + NFP_NBI_PC + 0,
			     16 * 64 * 1024, 0);
		if (err < 0)
			return err;
#endif /* NFP_ECC_FULL_CLEAR */

		err = nfp6000_island_nbi_b0_workarounds(cpp, island);
		if (err < 0)
			return err;

#ifdef NFP_ECC_FULL_CLEAR
		/* Initialize PM */
		for (i = 0; i < 0x10000; i += 4) {
			err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
					     NFP_NBI_PMX_OPCODE + i, 0);
			if (err < 0)
				return err;
			err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
					     NFP_NBI_PMX_RDATA + i, 0);
			if (err < 0)
				return err;
		}
#endif /* NFP_ECC_FULL_CLEAR */

		/* Initialize NBIMUXLATE */
		err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
				     NFP_NBIX_CSR + NFP_NBIX_CSR_NBIMUXLATE,
				     NFP_NBIX_CSR_NBIMUXLATE_ACCMODE(7) |
				     NFP_NBIX_CSR_NBIMUXLATE_ISLAND1(24) |
				     NFP_NBIX_CSR_NBIMUXLATE_ISLAND0(0));
		if (err < 0)
			return err;
	}

	return nfp6000_island_ecc_init(cpp, island, unit,
				       nbi_ecc, ARRAY_SIZE(nbi_ecc));
}

static int nfp6000_island_pci_init(struct nfp_cpp *cpp, int island, int unit)
{
	const u32 pci_w = NFP_CPP_ID(NFP_CPP_TARGET_PCIE, 3, 0);
	u64 addr = ((u64)(island - 4) << 38);
	int i;

	if (unit == NFP6000_DEVICE_PCI_PCI) {
		int err;

		/* Initialize the PCI Queue Controller */
		err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
				     NFP_PCIEX_IM + 0x50, 0x10a01);
		if (err < 0)
			return err;

		err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
				     NFP_PCIEX_IM + 0x54, 0x00001);
		if (err < 0)
			return err;

		err = nfp_xpb_writel(cpp, NFP_XPB_ISLAND(island) +
				     NFP_PCIEX_IM + 0x54, 0x10000);
		if (err < 0)
			return err;

		/* Clear out the PCI Queue Controller */
		for (i = 0; i < 256; i++) {
			nfp_cpp_writel(cpp, pci_w, addr + NFP_PCIE_Q(i) +
				       NFP_QCTLR_CFGSTATUSLOW,
				       NFP_QCTLR_CFGSTATUSLOW_READPTR_EN |
				       NFP_QCTLR_CFGSTATUSLOW_READPTR(0));
			nfp_cpp_writel(cpp, pci_w, addr + NFP_PCIE_Q(i) +
				       NFP_QCTLR_CFGSTATUSHIGH,
				       NFP_QCTLR_CFGSTATUSHIGH_EMPTY |
				       NFP_QCTLR_CFGSTATUSHIGH_WRITEPTR(0));
		}
	}

#ifdef NFP_ECC_FULL_CLEAR
	if (unit == NFP6000_DEVICE_PCI_PCI) {
		const u32 pci_r = NFP_CPP_ID(NFP_CPP_TARGET_PCIE, 2, 0);
		u32 tmp;

		/* Clear out the PCI SRAM:
		 */
		for (i = 0; i < 0x10000; i += 4)
			nfp_cpp_writel(cpp, pci_w, addr + NFP_PCIE_SRAM + i, 0);

		nfp_cpp_readl(cpp, pci_r, addr + NFP_PCIE_SRAM, &tmp);
	}
#endif /* NFP_ECC_FULL_CLEAR */

	return nfp6000_island_ecc_init(cpp, island, unit,
				       pci_ecc, ARRAY_SIZE(pci_ecc));
}

static int nfp6000_island_imb_init(struct nfp_cpp *cpp, int island, int unit)
{
	int i, err;

	if (!((1ULL << island) & imb_island_mask))
		return 0;

	if (unit != 0)
		return 0;

	/* The ARM island's IMB has already been initialized
	 * earlier - don't do it here.
	 */
	if (island == 1)
		goto ctm_init;

	for (i = 0; i < 16; i++) {
		u32 xpb_src = 0x000a0000 + (i * 4);
		u32 xpb_dst = (island << 24) | xpb_src;
		u32 tmp;

		err = nfp_xpb_readl(cpp, xpb_src, &tmp);
		if (err < 0)
			return err;
		err = nfp_xpb_writel(cpp, xpb_dst, tmp);
		if (err < 0)
			return err;
	}

ctm_init:

	if (island >= 4 && island <= 7)
		return 0;

	err = nfp6000_island_ctm_init(cpp, island);
	if (err < 0)
		return err;
	err = nfp6000_island_cls_init(cpp, island);
	if (err < 0)
		return err;

	return 0;
}

static int nfp6000_island_init(struct nfp_cpp *cpp, unsigned int subdevice)
{
	int island = NFP6000_DEVICE_ISLAND_of(subdevice);
	int unit   = NFP6000_DEVICE_UNIT_of(subdevice);
	int err;

	/* Initialize the island's CTM and CLS */
	err = nfp6000_island_imb_init(cpp, island, unit);
	if (err < 0)
		return err;

	if (island >= 1 && island <= 3) {
		err = nfp6000_island_arm_init(cpp, island, unit);
	} else if (island >= 4 && island <= 7) {
		err = nfp6000_island_pci_init(cpp, island, unit);
	} else if (island >= 8 && island <= 11) {
		err = nfp6000_island_nbi_init(cpp, island, unit);
	} else if (island >= 12 && island <= 15) {
		err = nfp6000_island_ecc_init(cpp, island, unit,
					      cry_ecc, ARRAY_SIZE(cry_ecc));
	} else if (island >= 24 && island <= 27) {
		err = nfp6000_island_emu_init(cpp, island, unit);
	} else if (island >= 28 && island <= 31) {
		err = nfp6000_island_imu_init(cpp, island, unit);
	} else if (island >= 32 && island <= 39) {
		/* ME islands area already covered by the IMB init */
	} else if (island >= 48 && island <= 51) {
		err = nfp6000_island_ecc_init(cpp, island, unit,
					      ila_ecc, ARRAY_SIZE(ila_ecc));
	}

	return err;
}

/**
 * nfp_power_set() - Set device power state
 * @nfp:	   NFP Device handle
 * @subdevice:     NFP subdevice
 * @state:	   Power state
 *
 * Return: 0, or -ERRNO
 */
int nfp_power_set(struct nfp_device *nfp, unsigned int subdevice, int state)
{
	struct nfp_cpp *cpp;
	u32 model;
	int err, curr_state;

	err = nfp_power_get(nfp, subdevice, &curr_state);
	if (err < 0)
		return err;

	cpp = nfp_device_cpp(nfp);

	model = nfp_cpp_model(cpp);

	/* Transition to final state */
	while (state != curr_state) {
		int next_state;
		int enable;
		int reset;

		/* Ensure that we transition through P2
		 * to reach P0 or P1 from P3.
		 *
		 * Translated:
		 *
		 * Ensure that we transition through RESET
		 * to reach ON or SUSPEND from OFF.
		 */
		if (state == NFP_DEVICE_STATE_P0 ||
		    state == NFP_DEVICE_STATE_P1) {
			if (curr_state > NFP_DEVICE_STATE_P2)
				next_state = NFP_DEVICE_STATE_P2;
			else
				next_state = state;
		} else {
			next_state = state;
		}

		enable = (~next_state >> 0) & 1;
		reset = (~next_state >> 1) & 1;

		if (NFP_CPP_MODEL_IS_3200(model))
			err = nfp3200_reset_set(cpp, subdevice, reset, enable);
		else if (NFP_CPP_MODEL_IS_6000(model))
			err = nfp6000_reset_set(cpp, subdevice, reset, enable);
		else
			err = -EINVAL;

		if (err < 0)
			break;

		if (NFP_CPP_MODEL_IS_6000(model)) {
			/* If transitioned from RESET to ON, load the IMB */
			if (next_state == NFP_DEVICE_STATE_P0 &&
			    curr_state == NFP_DEVICE_STATE_P2) {
				nfp6000_island_init(cpp, subdevice);
			}
		}

		curr_state = next_state;
	}

	return err;
}
