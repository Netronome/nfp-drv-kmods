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
 * nfp.h
 * Interface for NFP device access and query functions.
 */

#ifndef __NFP_H__
#define __NFP_H__

#include <linux/device.h>

#include "kcompat.h"

#include "nfp_cpp.h"

struct nfp_cpp;

/* Implemented in nfp_device.c */

int nfp_device_lock(struct nfp_cpp *cpp);
int nfp_device_unlock(struct nfp_cpp *cpp);

/* Implemented in nfp_hwinfo.c */

const char *nfp_hwinfo_lookup(struct nfp_cpp *cpp, const char *lookup);

/* Implemented in nfp_power.c */

/*
 * NFP Device Power States
 *
 * NFP_DEVICE_STATE_P0          Clocked, reset released
 * NFP_DEVICE_STATE_P1          Not clocked, reset released
 * NFP_DEVICE_STATE_P2          Clocked, held in reset
 * NFP_DEVICE_STATE_P3          No clocking, held in reset
 *
 * NOTE: Transitioning a device from P0 to power state P2
 *       or P3 will imply that all running configuration
 *       of the device will be lost, and the device must
 *       be re-initialized when P0 state is re-entered.
 */
#define NFP_DEVICE_STATE_P0     0
#define NFP_DEVICE_STATE_P1     1
#define NFP_DEVICE_STATE_P2     2
#define NFP_DEVICE_STATE_P3     3

/*
 * Friendly aliases of the above device states
 */
#define NFP_DEVICE_STATE_ON             NFP_DEVICE_STATE_P0
#define NFP_DEVICE_STATE_SUSPEND        NFP_DEVICE_STATE_P1
#define NFP_DEVICE_STATE_RESET          NFP_DEVICE_STATE_P2
#define NFP_DEVICE_STATE_OFF            NFP_DEVICE_STATE_P3

/*
 * NFP6000 specific subdevice identifiers
 */
#define NFP6000_DEVICE(island, unit) \
	((((island) & 0x3f) << 8) | ((unit) & 0xf))
#define NFP6000_DEVICE_ISLAND_of(x)	(((x) >> 8) & 0x3f)
#define NFP6000_DEVICE_UNIT_of(x)	(((x) >> 0) & 0x0f)

#define NFP6000_DEVICE_ARM(dev, unit)   NFP6000_DEVICE((dev) + 1, unit)
#define     NFP6000_DEVICE_ARM_CORE  0
#define     NFP6000_DEVICE_ARM_ARM   1
#define     NFP6000_DEVICE_ARM_GSK   2
#define     NFP6000_DEVICE_ARM_PRH   3
#define     NFP6000_DEVICE_ARM_MEG0  4
#define     NFP6000_DEVICE_ARM_MEG1  5
#define NFP6000_DEVICE_PCI(dev, unit)    NFP6000_DEVICE((dev) + 4, unit)
#define     NFP6000_DEVICE_PCI_CORE  0
#define     NFP6000_DEVICE_PCI_PCI   1
#define     NFP6000_DEVICE_PCI_MEG0  2
#define     NFP6000_DEVICE_PCI_MEG1  3
#define NFP6000_DEVICE_NBI(dev, unit)    NFP6000_DEVICE((dev) + 8, unit)
#define     NFP6000_DEVICE_NBI_CORE  0
#define     NFP6000_DEVICE_NBI_MAC4  4
#define     NFP6000_DEVICE_NBI_MAC5  5
#define NFP6000_DEVICE_CRP(dev, unit)    NFP6000_DEVICE((dev) + 12, unit)
#define     NFP6000_DEVICE_CRP_CORE  0
#define     NFP6000_DEVICE_CRP_CRP   1
#define     NFP6000_DEVICE_CRP_MEG0  2
#define     NFP6000_DEVICE_CRP_MEG1  3
#define NFP6000_DEVICE_EMU(dev, unit)    NFP6000_DEVICE((dev) + 24, unit)
#define     NFP6000_DEVICE_EMU_CORE  0
#define     NFP6000_DEVICE_EMU_QUE   1
#define     NFP6000_DEVICE_EMU_LUP   2
#define     NFP6000_DEVICE_EMU_DAL   3
#define     NFP6000_DEVICE_EMU_EXT   4
#define     NFP6000_DEVICE_EMU_DDR0  5
#define     NFP6000_DEVICE_EMU_DDR1  6
#define NFP6000_DEVICE_IMU(dev, unit)    NFP6000_DEVICE((dev) + 28, unit)
#define     NFP6000_DEVICE_IMU_CORE  0
#define     NFP6000_DEVICE_IMU_STS   1
#define     NFP6000_DEVICE_IMU_LBL   2
#define     NFP6000_DEVICE_IMU_CLU   3
#define     NFP6000_DEVICE_IMU_NLU   4
#define NFP6000_DEVICE_FPC(dev, unit)    NFP6000_DEVICE((dev) + 32, unit)
#define     NFP6000_DEVICE_FPC_CORE  0
#define     NFP6000_DEVICE_FPC_MEG0  1
#define     NFP6000_DEVICE_FPC_MEG1  2
#define     NFP6000_DEVICE_FPC_MEG2  3
#define     NFP6000_DEVICE_FPC_MEG3  4
#define     NFP6000_DEVICE_FPC_MEG4  5
#define     NFP6000_DEVICE_FPC_MEG5  6
#define NFP6000_DEVICE_ILA(dev, unit)    NFP6000_DEVICE((dev) + 48, unit)
#define     NFP6000_DEVICE_ILA_CORE  0
#define     NFP6000_DEVICE_ILA_ILA   1
#define     NFP6000_DEVICE_ILA_MEG0  2
#define     NFP6000_DEVICE_ILA_MEG1  3

int nfp_power_get(struct nfp_cpp *cpp, unsigned int subdevice, int *state);
int nfp_power_set(struct nfp_cpp *cpp, unsigned int subdevice, int state);

/* Implemented in nfp_reset.c */

int nfp_reset_soft(struct nfp_cpp *cpp);

/* Implemented in nfp_nsp.c */

enum nfp_nsp_cmd {
	SPCODE_NOOP             = 0, /* No operation */
	SPCODE_SOFT_RESET       = 1, /* Soft reset the NFP */
	SPCODE_FW_DEFAULT       = 2, /* Load default (UNDI) FW */
	SPCODE_PHY_INIT         = 3, /* Initialize the PHY */
	SPCODE_MAC_INIT         = 4, /* Initialize the MAC */
	SPCODE_PHY_RXADAPT      = 5, /* Re-run PHY RX Adaptation */
	SPCODE_FW_LOAD          = 6, /* Load fw from buffer, len in option */
	SPCODE_ETH_RESCAN       = 7, /* Rescan ETHs, update ETH_TABLE */
	SPCODE_ETH_CONTROL      = 8, /* Perform ETH control action */
	__MAX_SPCODE,
};

struct nfp_nsp;

struct nfp_nsp *nfp_nsp_open(struct nfp_cpp *cpp);
void nfp_nsp_close(struct nfp_nsp *state);
int nfp_nsp_command(struct nfp_nsp *state, uint16_t spcode, u32 option,
		    u32 buff_cpp, u64 buff_addr);
int nfp_nsp_command_buf(struct nfp_nsp *state, u16 code, u32 option,
			const void *in_buf, unsigned int in_size,
			void *out_buf, unsigned int out_size);

/* Implemented in nfp_resource.c */

#define NFP_RESOURCE_TBL_TARGET		NFP_CPP_TARGET_MU
#define NFP_RESOURCE_TBL_BASE		0x8100000000ULL

/* NFP Resource Table self-identifier */
#define NFP_RESOURCE_TBL_NAME		"nfp.res"
#define NFP_RESOURCE_TBL_KEY		0x00000000 /* Special key for entry 0 */

/* All other keys are CRC32-POSIX of the 8-byte identification string */

/* ARM/PCI vNIC Interfaces 0..3 */
#define NFP_RESOURCE_VNIC_PCI_0		"vnic.p0"
#define NFP_RESOURCE_VNIC_PCI_1		"vnic.p1"
#define NFP_RESOURCE_VNIC_PCI_2		"vnic.p2"
#define NFP_RESOURCE_VNIC_PCI_3		"vnic.p3"

/* NFP Hardware Info Database */
#define NFP_RESOURCE_NFP_HWINFO		"nfp.info"

/* Service Processor */
#define NFP_RESOURCE_NSP		"nfp.sp"

/* Netronone Flow Firmware Table */
#define NFP_RESOURCE_NFP_NFFW		"nfp.nffw"

/* MAC Statistics Accumulator */
#define NFP_RESOURCE_MAC_STATISTICS	"mac.stat"

struct nfp_resource *
nfp_resource_acquire(struct nfp_cpp *cpp, const char *name);

void nfp_resource_release(struct nfp_resource *res);

u32 nfp_resource_cpp_id(struct nfp_resource *res);

const char *nfp_resource_name(struct nfp_resource *res);

u64 nfp_resource_address(struct nfp_resource *res);

u64 nfp_resource_size(struct nfp_resource *res);

#endif /* !__NFP_H__ */
