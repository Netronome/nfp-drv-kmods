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

#ifndef NFP_SUBSYS
#define NFP_SUBSYS ""
#endif

#define nfp_err(nfp, fmt, args...) \
	dev_err(nfp_cpp_device(nfp_device_cpp(nfp))->parent, \
		NFP_SUBSYS fmt, ## args)
#define nfp_warn(nfp, fmt, args...) \
	dev_warn(nfp_cpp_device(nfp_device_cpp(nfp))->parent, \
		 NFP_SUBSYS fmt, ## args)
#define nfp_info(nfp, fmt, args...) \
	dev_info(nfp_cpp_device(nfp_device_cpp(nfp))->parent, \
		NFP_SUBSYS fmt, ## args)
#define nfp_dbg(nfp, fmt, args...) \
	dev_dbg(nfp_cpp_device(nfp_device_cpp(nfp))->parent, \
		NFP_SUBSYS fmt, ## args)
#define nfp_trace(nfp, fmt, args...) \
	do { \
		struct device *_dev; \
		_dev = nfp_cpp_device(nfp_device_cpp(nfp))->parent; \
		trace_printk("%s %s: " NFP_SUBSYS fmt, \
		     dev_driver_string(_dev), dev_name(_dev)); \
	} while (0)

struct nfp_cpp;

/* Opaque NFP device handle. */
struct nfp_device;

/* Maximum device number for an NFP device. */
#define NFP_MAX_DEVICE_NUM              63

/* Implemented in nfp_device.c */

struct nfp_device *nfp_device_open(unsigned int devnum);
struct nfp_device *nfp_device_from_cpp(struct nfp_cpp *cpp);
void nfp_device_close(struct nfp_device *dev);

int nfp_device_id(struct nfp_device *nfp);
struct nfp_cpp *nfp_device_cpp(struct nfp_device *dev);

void *nfp_device_private(struct nfp_device *dev,
			 void *(*constructor)(struct nfp_device *dev));
void *nfp_device_private_alloc(struct nfp_device *dev, size_t private_size,
			       void (*destructor)(void *private_data));

int nfp_device_trylock(struct nfp_device *nfp);
int nfp_device_lock(struct nfp_device *nfp);
int nfp_device_unlock(struct nfp_device *nfp);

/* Implemented in nfp_hwinfo.c */

const char *nfp_hwinfo_lookup(struct nfp_device *nfp, const char *lookup);

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
 * NFP3200 specific subdevice identifiers
 */
#define NFP3200_DEVICE(x)               ((x) & 0x1f)
#define     NFP3200_DEVICE_ARM          1
#define     NFP3200_DEVICE_ARM_GASKET   2
#define     NFP3200_DEVICE_DDR0         3
#define     NFP3200_DEVICE_DDR1         4
#define     NFP3200_DEVICE_MECL0        5
#define     NFP3200_DEVICE_MECL1        6
#define     NFP3200_DEVICE_MECL2        7
#define     NFP3200_DEVICE_MECL3        8
#define     NFP3200_DEVICE_MECL4        9
#define     NFP3200_DEVICE_MSF0         10
#define     NFP3200_DEVICE_MSF1         11
#define     NFP3200_DEVICE_MU           12
#define     NFP3200_DEVICE_PCIE         13
#define     NFP3200_DEVICE_QDR0         14
#define     NFP3200_DEVICE_QDR1         15
#define     NFP3200_DEVICE_CRYPTO       16

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

int nfp_power_get(struct nfp_device *dev, unsigned int subdevice, int *state);
int nfp_power_set(struct nfp_device *dev, unsigned int subdevice, int state);

/* Implemented in nfp_reset.c */

int nfp_reset_soft(struct nfp_device *nfp);

/* Implemented in nfp_armsp.c */

#define SPCODE_NOOP             0       /* No operation */
#define SPCODE_SOFT_RESET       1       /* Soft reset the NFP */
#define SPCODE_FW_DEFAULT       2       /* Load default (UNDI) FW */
#define SPCODE_PHY_INIT         3       /* Initialize the PHY */
#define SPCODE_MAC_INIT         4       /* Initialize the MAC */
#define SPCODE_PHY_RXADAPT      5       /* Re-run PHY RX Adaptation */
#define SPCODE_FW_LOAD          6       /* Load fw from buffer, len in option */
#define SPCODE_ETH_RESCAN       7       /* Rescan ETHs, update ETH_TABLE */
#define SPCODE_ETH_CONTROL      8       /* Perform ETH control action */

int nfp_nsp_command(struct nfp_device *nfp, uint16_t spcode, u32 option,
		    u32 buff_cpp, u64 buff_addr);

/* Implemented in nfp_resource.c */

#define NFP_RESOURCE_ENTRY_NAME_SZ  8

/* NFP BSP Resource Reservation Entry
 */
struct nfp_resource_entry {
	struct nfp_resource_entry_mutex {
		u32 owner;       /* NFP CPP Lock, interface owner */
		u32 key;         /* NFP CPP Lock, posix_crc32(name, 8) */
	} mutex;
	struct nfp_resource_entry_region {
		/* ASCII, zero padded name */
		u8  name[NFP_RESOURCE_ENTRY_NAME_SZ];
		u32 reserved_0x10;     /* -- reserved -- */
		u8  reserved_0x11;     /* -- reserved -- */
		u8  cpp_action;        /* CPP Action */
		u8  cpp_token;         /* CPP Token */
		u8  cpp_target;        /* CPP Target ID */
		u32 page_offset;       /* 256-byte page offset into
					     * target's CPP address */
		u32 page_size;         /* size, in 256-byte pages */
	} region;
} __attribute__((__packed__));

/**
 * NFP Resource Table self-identifier
 */
#define NFP_RESOURCE_TABLE_NAME     "nfp.res"
#define NFP_RESOURCE_TABLE_KEY      0x00000000  /* Special key for entry 0 */

/* All other keys are CRC32-POSIX of the 8-byte identification string */

/**
 * ARM Linux/Application Workspace
 */
#define NFP_RESOURCE_ARM_WORKSPACE      "arm.mem"

/**
 * ARM Linux Flattended Device Tree
 */
#define NFP_RESOURCE_ARM_FDT            "arm.fdt"

/**
 * ARM/PCI vNIC Interfaces 0..3
 */
#define NFP_RESOURCE_VNIC_PCI_0         "vnic.p0"
#define NFP_RESOURCE_VNIC_PCI_1         "vnic.p1"
#define NFP_RESOURCE_VNIC_PCI_2         "vnic.p2"
#define NFP_RESOURCE_VNIC_PCI_3         "vnic.p3"

/**
 * NFP Hardware Info Database
 */
#define NFP_RESOURCE_NFP_HWINFO         "nfp.info"

/**
 * ARM Diagnostic Area
 */
#define NFP_RESOURCE_ARM_DIAGNOSTIC     "arm.diag"

/**
 * Netronone Flow Firmware Table
 */
#define NFP_RESOURCE_NFP_NFFW           "nfp.nffw"

/**
 * MAC Statistics Accumulator
 */
#define NFP_RESOURCE_MAC_STATISTICS     "mac.stat"

int nfp_cpp_resource_init(struct nfp_cpp *cpp,
			  struct nfp_cpp_mutex **resource_mutex);

int nfp_cpp_resource_table(struct nfp_cpp *cpp, int *target,
			   u64 *base, size_t *sizep);

struct nfp_resource *nfp_resource_acquire(struct nfp_device *nfp,
					  const char *name);

void nfp_resource_release(struct nfp_resource *res);

int nfp_cpp_resource_add(struct nfp_cpp *cpp, const char *name,
			 u32 cpp_id, u64 address, u64 size,
			 struct nfp_cpp_mutex **resource_mutex);

u32 nfp_resource_cpp_id(struct nfp_resource *res);

const char *nfp_resource_name(struct nfp_resource *res);

u64 nfp_resource_address(struct nfp_resource *res);

u64 nfp_resource_size(struct nfp_resource *res);

#endif /* !__NFP_H__ */
