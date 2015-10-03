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
 * nfp_cpplib.c
 * Library of functions to access the NFP's CPP bus
 * Authors: Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "nfp3200/nfp3200.h"
#include "nfp3200/nfp_xpb.h"
#include "nfp3200/nfp_event.h"
#include "nfp3200/nfp_em.h"
#include "nfp3200/nfp_im.h"
#include "nfp3200/nfp_pl.h"

#include "nfp6000/nfp_xpb.h"

/* NFP3200 MU */
#define NFP_XPB_MU_PCTL0                NFP_XPB_DEST(21, 2)
#define NFP_XPB_MU_PCTL1                NFP_XPB_DEST(21, 3)
#define NFP_MU_PCTL_DTUAWDT            0x00b0
#define   NFP_MU_PCTL_DTUAWDT_NUMBER_RANKS_of(_x)       (((_x) >> 9) & 0x3)
#define   NFP_MU_PCTL_DTUAWDT_ROW_ADDR_WIDTH_of(_x)     (((_x) >> 6) & 0x3)
#define   NFP_MU_PCTL_DTUAWDT_BANK_ADDR_WIDTH_of(_x)    (((_x) >> 3) & 0x3)
#define   NFP_MU_PCTL_DTUAWDT_COLUMN_ADDR_WIDTH_of(_x)  ((_x) & 0x3)

/* NFP6000 PL */
#define NFP_PL_DEVICE_ID                         0x00000004
#define   NFP_PL_DEVICE_ID_PART_NUM_of(_x)       (((_x) >> 16) & 0xffff)
#define   NFP_PL_DEVICE_ID_SKU_of(_x)            (((_x) >> 8) & 0xff)
#define   NFP_PL_DEVICE_ID_MAJOR_REV_of(_x)   (((_x) >> 4) & 0xf)
#define   NFP_PL_DEVICE_ID_MINOR_REV_of(_x)   (((_x) >> 0) & 0xf)

/**
 * nfp_cpp_readl() - Read a u32 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readl(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u32 *value)
{
	int err;
	u32 tmp;

	err = nfp_cpp_read(cpp, cpp_id, address, &tmp, sizeof(tmp));
	*value = le32_to_cpu(tmp);

	return err;
}

/**
 * nfp_cpp_writel() - Write a u32 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writel(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u32 value)
{
	value = cpu_to_le32(value);
	return nfp_cpp_write(cpp, cpp_id, address, &value, sizeof(value));
}

/**
 * nfp_cpp_readq() - Read a u64 word from a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Pointer to read buffer
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_readq(struct nfp_cpp *cpp, u32 cpp_id,
		  unsigned long long address, u64 *value)
{
	int err;
	u64 tmp;

	err = nfp_cpp_read(cpp, cpp_id, address, &tmp, sizeof(tmp));
	*value = le64_to_cpu(tmp);

	return err;
}

/**
 * nfp_cpp_writeq() - Write a u64 word to a CPP location
 * @cpp:	CPP device handle
 * @cpp_id:	CPP ID for operation
 * @address:	Address for operation
 * @value:	Value to write
 *
 * Return: length of the io, or -ERRNO
 */
int nfp_cpp_writeq(struct nfp_cpp *cpp, u32 cpp_id,
		   unsigned long long address, u64 value)
{
	value = cpu_to_le64(value);
	return  nfp_cpp_write(cpp, cpp_id, address, &value, sizeof(value));
}

/* NFP6000 specific */
#define NFP6000_ARM_GCSR_SOFTMODEL0	0x00400144

/* NOTE: This code should not use nfp_xpb_* functions,
 * as those are model-specific
 */
int __nfp_cpp_model_autodetect(struct nfp_cpp *cpp, u32 *model)
{
	const u32 arm_id = NFP_CPP_ID(NFP_CPP_TARGET_ARM, 0, 0);
	int err;

	/* Safe to read on the NFP3200 also, returns 0 */
	err = nfp_cpp_readl(cpp, arm_id, NFP6000_ARM_GCSR_SOFTMODEL0, model);
	if (err < 0)
		return err;

	/* Assume NFP3200 if zero */
	if (*model == 0) {
		const u32 xpb = NFP_CPP_ID(13, NFP_CPP_ACTION_RW, 0);
		u32 tmp;
		int mes;

		*model = 0x320000A0;
		/* Get stepping code */
		err = nfp_cpp_readl(cpp, xpb, 0x02000000 |
				(NFP_XPB_PL + NFP_PL_JTAG_ID_CODE), &tmp);
		if (err < 0)
			return -1;
		*model += NFP_PL_JTAG_ID_CODE_REV_ID_of(tmp);
		/* Get ME count */
		err = nfp_cpp_readl(cpp, xpb,
				    0x02000000 | (NFP_XPB_PL + NFP_PL_FUSE),
				    &tmp);
		if (err < 0)
			return err;
		switch (NFP_PL_FUSE_MECL_ME_ENABLE_of(tmp)) {
		case NFP_PL_FUSE_MECL_ME_ENABLE_MES_8:
			mes = 8;
			break;
		case NFP_PL_FUSE_MECL_ME_ENABLE_MES_16:
			mes = 16;
			break;
		case NFP_PL_FUSE_MECL_ME_ENABLE_MES_24:
			mes = 24;
			break;
		case NFP_PL_FUSE_MECL_ME_ENABLE_MES_32:
			mes = 32;
			break;
		case NFP_PL_FUSE_MECL_ME_ENABLE_MES_40:
			mes = 40;
			break;
		default:
			mes = 0;
			break;
		}

		*model |= (mes / 10) << 20;
		*model |= (mes % 10) << 16;
	} else if (NFP_CPP_MODEL_IS_6000(*model)) {
		u32 tmp;
		int err;

		/* The PL's PluDeviceID revision code is authoratative */
		*model &= ~0xff;
		err = nfp_xpb_readl(cpp,
				    NFP_XPB_DEVICE(1, 1, 16) + NFP_PL_DEVICE_ID,
				    &tmp);
		if (err < 0)
			return err;
		*model |= ((NFP_PL_DEVICE_ID_MAJOR_REV_of(tmp) - 1) << 4) |
			   NFP_PL_DEVICE_ID_MINOR_REV_of(tmp);
	}

	return 0;
}

/* THIS FUNCTION IS NOT EXPORTED */

/* Fix up any ARM firmware issues */
static int ddr3200_guess_size(struct nfp_cpp *cpp, int ddr, u32 *size)
{
	int err;
	u32 tmp;
	u32 mask = (ddr == 0) ?
		(NFP_PL_RE_DDR0_ENABLE | NFP_PL_RE_DDR0_RESET) :
		(NFP_PL_RE_DDR1_ENABLE | NFP_PL_RE_DDR1_RESET);
	u32 ddr_ctl = (ddr == 0) ? NFP_XPB_MU_PCTL0 : NFP_XPB_MU_PCTL1;
	int ranks, rowb, rows, colb, cols, bankb, banks;

	err = nfp_xpb_readl(cpp, NFP_XPB_PL + NFP_PL_RE, &tmp);
	if (err < 0)
		return err;

	if ((tmp & mask) != mask) {
		*size = 0;
		return 0;
	}

	err = nfp_xpb_readl(cpp, ddr_ctl + NFP_MU_PCTL_DTUAWDT, &tmp);
	if (err < 0)
		return err;

	ranks = NFP_MU_PCTL_DTUAWDT_NUMBER_RANKS_of(tmp) + 1;
	rowb  = NFP_MU_PCTL_DTUAWDT_ROW_ADDR_WIDTH_of(tmp) + 13;
	rows  = (1 << rowb);
	colb  = NFP_MU_PCTL_DTUAWDT_COLUMN_ADDR_WIDTH_of(tmp) + 9;
	cols  = (1 << colb);
	bankb = NFP_MU_PCTL_DTUAWDT_BANK_ADDR_WIDTH_of(tmp) + 2;
	banks = (1 << bankb);

	*size = ranks * (rows * cols * banks / SZ_1M) * sizeof(u64);
	return 0;
}

static int workaround_resource_table(struct nfp_cpp *cpp,
				     struct nfp_cpp_mutex **mx,
				     u32 ddr0_size, u32 ddr1_size)
{
	u32 ddr = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 1);
	int err;

	dev_warn(nfp_cpp_device(cpp), "WARNING: NFP Resource Table not found at 7:0:1:0x0, injecting workaround\n");
	err = nfp_cpp_resource_init(cpp, mx);
	if (err < 0)
		return err;

	/* The following resources preserve the nfp-bsp-2012.09
	 * series resource list, which is assumed if there
	 * is no existing resource map on a NFP3200
	 */
	err = nfp_cpp_resource_add(cpp, NFP_RESOURCE_ARM_WORKSPACE, ddr,
				   (u64)(ddr0_size + ddr1_size - 512)
				   * SZ_1M, SZ_512M, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, NFP_RESOURCE_NFP_HWINFO,
				   NFP_CPP_ID(NFP_CPP_TARGET_ARM_SCRATCH,
					      NFP_CPP_ACTION_RW, 0),
				   0x0000, 0x800, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, NFP_RESOURCE_ARM_DIAGNOSTIC,
				   NFP_CPP_ID(NFP_CPP_TARGET_ARM_SCRATCH,
					      NFP_CPP_ACTION_RW, 0),
				   0xc000, 0x800, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, NFP_RESOURCE_NFP_NFFW, ddr,
				   0x1000, 0x1000, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, "msix.tbl", ddr, 0x2000, 0x1000, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, "msix.pba", ddr, 0x3000, 0x1000, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, "arm.tty", ddr, 0xb000, 0x3000, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, NFP_RESOURCE_VNIC_PCI_0, ddr,
				   0xe000, 0x1000, NULL);
	if (err < 0)
		return err;

	err = nfp_cpp_resource_add(cpp, "arm.ctrl", ddr, 0xf000, 0x1000, NULL);
	if (err < 0)
		return err;

	return err;
}

int __nfp_cpp_model_fixup(struct nfp_cpp *cpp)
{
	u32 ddr = NFP_CPP_ID(NFP_CPP_TARGET_MU, NFP_CPP_ACTION_RW, 1);
	int err;

	if (NFP_CPP_MODEL_IS_3200(nfp_cpp_model(cpp))) {
		struct nfp_cpp_mutex *lock = NULL;
		u8 resid[8];
		u32 ddr0_size;
		u32 ddr1_size;

		/* See if the DDR is even on */
		err = ddr3200_guess_size(cpp, 0, &ddr0_size);
		if (err < 0) {
			dev_err(nfp_cpp_device(cpp), "ddr3200_guess_size() failed.\n");
			return err;
		}

		if (ddr0_size <= 0)
			return 0;

		err = ddr3200_guess_size(cpp, 1, &ddr1_size);
		if (err < 0) {
			dev_err(nfp_cpp_device(cpp), "ddr3200_guess_size() failed.\n");
			return err;
		}

		err = nfp_cpp_read(cpp, ddr, 8, &resid[0], 8);
		if (err < 0) {
			dev_err(nfp_cpp_device(cpp), "NFP Resource Table could not be read\n");
			return err;
		}

		if (memcmp(resid, NFP_RESOURCE_TABLE_NAME, 8) != 0)
			err = workaround_resource_table(cpp, &lock,
							ddr0_size, ddr1_size);

		if (lock) {
			nfp_cpp_mutex_unlock(lock);
			nfp_cpp_mutex_free(lock);
		}

		if (err < 0) {
			dev_err(nfp_cpp_device(cpp), "NFP Resource Table could not be constructed!\n");
			return err;
		}
	}

	return 0;
}

/* THIS FUNCTION IS NOT EXPORTED */
#define MUTEX_LOCKED(interface)  ((((u32)(interface)) << 16) | 0x000f)
#define MUTEX_UNLOCK(interface)  ((((u32)(interface)) << 16) | 0x0000)

#define MUTEX_IS_LOCKED(value)   (((value) & 0xffff) == 0x000f)
#define MUTEX_IS_UNLOCKED(value) (((value) & 0xffff) == 0x0000)

/* If you need more than 65536 recursive locks, please
 * rethink your code.
 */
#define MUTEX_DEPTH_MAX         0xffff

struct nfp_cpp_mutex {
	struct nfp_cpp *cpp;
	int target;
	u16 depth;
	unsigned long long address;
	u32 key;
};

static int _nfp_cpp_mutex_validate(u32 model, u16 interface,
				   int *target, unsigned long long address)
{
	/* Not permitted on invalid interfaces */
	if (NFP_CPP_INTERFACE_TYPE_of(interface) ==
			NFP_CPP_INTERFACE_TYPE_INVALID)
		return -EINVAL;

	/* Address must be 64-bit aligned */
	if (address & 7)
		return -EINVAL;

	if (NFP_CPP_MODEL_IS_3200(model)) {
		if (*target != NFP_CPP_TARGET_MU)
			return -EINVAL;
	} else if (NFP_CPP_MODEL_IS_6000(model)) {
		if (*target != NFP_CPP_TARGET_MU)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * nfp_cpp_mutex_init() - Initialize a mutex location
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	Unique 32-bit value for this mutex
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * will initialize 64 bits of data at the location.
 *
 * This creates the initial mutex state, as locked by this
 * nfp_cpp_interface().
 *
 * This function should only be called when setting up
 * the initial lock state upon boot-up of the system.
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_init(struct nfp_cpp *cpp,
		       int target, unsigned long long address, u32 key)
{
	u16 interface = nfp_cpp_interface(cpp);
	u32 muw = NFP_CPP_ID(target, 4, 0);    /* atomic_write */
	u32 model = nfp_cpp_model(cpp);
	int err;

	err = _nfp_cpp_mutex_validate(model, interface, &target, address);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address + 4, key);
	if (err)
		return err;

	err = nfp_cpp_writel(cpp, muw, address,
			     MUTEX_LOCKED(nfp_cpp_interface(cpp)));
	if (err)
		return err;

	return 0;
}

/**
 * nfp_cpp_mutex_alloc() - Create a mutex handle
 * @cpp:	NFP CPP handle
 * @target:	NFP CPP target ID (ie NFP_CPP_TARGET_CLS or NFP_CPP_TARGET_MU)
 * @address:	Offset into the address space of the NFP CPP target ID
 * @key:	32-bit unique key (must match the key at this location)
 *
 * The CPP target:address must point to a 64-bit aligned location, and
 * reserve 64 bits of data at the location for use by the handle.
 *
 * Only target/address pairs that point to entities that support the
 * MU Atomic Engine's CmpAndSwap32 command are supported.
 *
 * Return:	A non-NULL struct nfp_cpp_mutex * on success, NULL on failure.
 */
struct nfp_cpp_mutex *nfp_cpp_mutex_alloc(struct nfp_cpp *cpp, int target,
					  unsigned long long address, u32 key)
{
	u16 interface = nfp_cpp_interface(cpp);
	u32 model = nfp_cpp_model(cpp);
	u32 mur = NFP_CPP_ID(target, 3, 0);    /* atomic_read */
	struct nfp_cpp_mutex *mutex;
	int err;
	u32 tmp;

	err = _nfp_cpp_mutex_validate(model, interface, &target, address);
	if (err)
		return NULL;

	err = nfp_cpp_readl(cpp, mur, address + 4, &tmp);
	if (err < 0)
		return NULL;

	if (tmp != key)
		return NULL;

	mutex = kzalloc(sizeof(*mutex), GFP_KERNEL);
	if (!mutex)
		return NULL;

	mutex->cpp = cpp;
	mutex->target = target;
	mutex->address = address;
	mutex->key = key;
	mutex->depth = 0;

	return mutex;
}

/**
 * nfp_cpp_mutex_free() - Free a mutex handle - does not alter the lock state
 * @mutex:	NFP CPP Mutex handle
 */
void nfp_cpp_mutex_free(struct nfp_cpp_mutex *mutex)
{
	kfree(mutex);
}

/**
 * nfp_cpp_mutex_lock() - Lock a mutex handle, using the NFP MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_lock(struct nfp_cpp_mutex *mutex)
{
	int err;
	unsigned int timeout_ms = 1;	/* Sleep for 1ms */

	/* We can't use a waitqueue here, because the unlocker
	 * might be on a separate CPU.
	 *
	 * So just wait for now.
	 */
	for (;;) {
		err = nfp_cpp_mutex_trylock(mutex);
		if (err != -EBUSY)
			break;

		err = msleep_interruptible(timeout_ms);
		if (err != 0)
			return -ERESTARTSYS;
	}

	return 0;
}

/**
 * nfp_cpp_mutex_unlock() - Unlock a mutex handle, using the MU Atomic Engine
 * @mutex:	NFP CPP Mutex handle
 *
 * Return: 0 on success, or -errno on failure
 */
int nfp_cpp_mutex_unlock(struct nfp_cpp_mutex *mutex)
{
	u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	struct nfp_cpp *cpp = mutex->cpp;
	u32 key, value;
	u16 interface = nfp_cpp_interface(cpp);
	int err;

	if (mutex->depth > 1) {
		mutex->depth--;
		return 0;
	}

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	err = nfp_cpp_readl(mutex->cpp, mur, mutex->address, &value);
	if (err < 0)
		return err;

	if (value != MUTEX_LOCKED(interface))
		return -EACCES;

	err = nfp_cpp_writel(cpp, muw, mutex->address, MUTEX_UNLOCK(interface));
	if (err < 0)
		return err;

	mutex->depth = 0;
	return 0;
}

/**
 * nfp_cpp_mutex_trylock() - Attempt to lock a mutex handle
 * @mutex:	NFP CPP Mutex handle
 *
 * Return:      0 if the lock succeeded, -errno on failure
 */
int nfp_cpp_mutex_trylock(struct nfp_cpp_mutex *mutex)
{
	u32 mur = NFP_CPP_ID(mutex->target, 3, 0);    /* atomic_read */
	u32 muw = NFP_CPP_ID(mutex->target, 4, 0);    /* atomic_write */
	u32 mus = NFP_CPP_ID(mutex->target, 5, 3);    /* test_set_imm */
	u32 key, value, tmp;
	struct nfp_cpp *cpp = mutex->cpp;
	int err;

	if (mutex->depth > 0) {
		if (mutex->depth == MUTEX_DEPTH_MAX)
			return -E2BIG;
		mutex->depth++;
		return 0;
	}

	/* Verify that the lock marker is not damaged */
	err = nfp_cpp_readl(cpp, mur, mutex->address + 4, &key);
	if (err < 0)
		return err;

	if (key != mutex->key)
		return -EPERM;

	/* Compare against the unlocked state, and if true,
	 * write the interface id into the top 16 bits, and
	 * mark as locked.
	 */
	value = MUTEX_LOCKED(nfp_cpp_interface(cpp));

	/* We use test_set_imm here, as it implies a read
	 * of the current state, and sets the bits in the
	 * bytemask of the command to 1s. Since the mutex
	 * is guaranteed to be 64-bit aligned, the bytemask
	 * of this 32-bit command is ensured to be 8'b00001111,
	 * which implies that the lower 4 bits will be set to
	 * ones regardless of the initial state.
	 *
	 * Since this is a 'Readback' operation, with no Pull
	 * data, we can treat this as a normal Push (read)
	 * atomic, which returns the original value.
	 */
	err = nfp_cpp_readl(cpp, mus, mutex->address, &tmp);
	if (err < 0)
		return err;

	/* Was it unlocked? */
	if (MUTEX_IS_UNLOCKED(tmp)) {
		/* The read value can only be 0x....0000 in the unlocked state.
		 * If there was another contending for this lock, then
		 * the lock state would be 0x....000f
		 */

		/* Write our owner ID into the lock
		 * While not strictly necessary, this helps with
		 * debug and bookkeeping.
		 */
		err = nfp_cpp_writel(cpp, muw, mutex->address, value);
		if (err < 0)
			return err;

		mutex->depth = 1;
		return 0;
	}

	/* Already locked by us? Success! */
	if (tmp == value) {
		mutex->depth = 1;
		return 0;
	}

	return MUTEX_IS_LOCKED(tmp) ? -EBUSY : -EINVAL;
}

static inline u8 __nfp_bytemask_of(int width, u64 addr)
{
	u8 byte_mask;

	if (width == 8)
		byte_mask = 0xff;
	else if (width == 4)
		byte_mask = 0x0f << (addr & 4);
	else if (width == 2)
		byte_mask = 0x03 << (addr & 6);
	else if (width == 1)
		byte_mask = 0x01 << (addr & 7);
	else
		byte_mask = 0;

	return byte_mask;
}

int __nfp_cpp_explicit_read(struct nfp_cpp *cpp, u32 cpp_id,
			    u64 addr, void *buff, size_t len, int width_read)
{
	struct nfp_cpp_explicit *expl;
	int cnt, incr = 16 * width_read;
	char *tmp = buff;
	int err;
	u8 byte_mask;

	expl = nfp_cpp_explicit_acquire(cpp);

	if (!expl)
		return -EBUSY;

	if (incr > 128)
		incr = 128;

	if (len & (width_read - 1))
		return -EINVAL;

	/* Translate a NFP_CPP_ACTION_RW to action 0
	 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 0,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	if (incr > len)
		incr = len;

	byte_mask = __nfp_bytemask_of(width_read, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id,
				    (incr / width_read) - 1, byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1,
				    0, NFP_SIGNAL_PUSH,
				    0, NFP_SIGNAL_NONE);

	for (cnt = 0; cnt < len; cnt += incr, addr += incr, tmp += incr) {
		if ((cnt + incr) > len) {
			incr = (len - cnt);
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    (incr / width_read) - 1,
						    0xff);
		}
		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
		err = nfp_cpp_explicit_get(expl, tmp, incr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
	}

	nfp_cpp_explicit_release(expl);

	return len;
}

int __nfp_cpp_explicit_write(struct nfp_cpp *cpp, u32 cpp_id, u64 addr,
			     const void *buff, size_t len, int width_write)
{
	struct nfp_cpp_explicit *expl;
	int cnt, incr = 16 * width_write;
	const char *tmp = buff;
	int err;
	u8 byte_mask;

	expl = nfp_cpp_explicit_acquire(cpp);

	if (!expl)
		return -EBUSY;

	if (incr > 128)
		incr = 128;

	if (len & (width_write - 1))
		return -EINVAL;

	/* Translate a NFP_CPP_ACTION_RW to action 1
	 */
	if (NFP_CPP_ID_ACTION_of(cpp_id) == NFP_CPP_ACTION_RW)
		cpp_id = NFP_CPP_ID(NFP_CPP_ID_TARGET_of(cpp_id), 1,
				    NFP_CPP_ID_TOKEN_of(cpp_id));

	if (incr > len)
		incr = len;

	byte_mask = __nfp_bytemask_of(width_write, addr);

	nfp_cpp_explicit_set_target(expl, cpp_id, (incr / width_write) - 1,
				    byte_mask);
	nfp_cpp_explicit_set_posted(expl, 1,
				    0, NFP_SIGNAL_PULL,
				    0, NFP_SIGNAL_NONE);

	for (cnt = 0; cnt < len; cnt += incr, addr += incr, tmp += incr) {
		if ((cnt + incr) > len) {
			incr = (len - cnt);
			nfp_cpp_explicit_set_target(expl, cpp_id,
						    (incr / width_write) - 1,
						    0xff);
		}
		err = nfp_cpp_explicit_put(expl, tmp, incr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
		err = nfp_cpp_explicit_do(expl, addr);
		if (err < 0) {
			nfp_cpp_explicit_release(expl);
			return err;
		}
	}

	nfp_cpp_explicit_release(expl);

	return len;
}
