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
 * kcompat.h
 * Common declarations for kernel backwards compatibility.
 */
#ifndef __KERNEL__NFP_COMPAT_H__
#define __KERNEL__NFP_COMPAT_H__

#include <linux/version.h>

/* RHEL has a tendency to heavily patch their kernels.  Sometimes it
 * is necessary to check for specific RHEL releases and not just for
 * Linux kernel version.  Define RHEL version macros for Linux kernels
 * which don't have them.
 */
#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) (((a) << 8) + (b))
#endif
#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif

#include <linux/if_tun.h>
#ifdef TUNSETSTEERINGEBPF
#define LINUX_RELEASE_4_16	1
#else
#define LINUX_RELEASE_4_16	0
#endif

#define VER_VANILLA_LT(x, y)						\
	(!RHEL_RELEASE_CODE && LINUX_VERSION_CODE < KERNEL_VERSION(x, y, 0))
#define VER_VANILLA_GE(x, y)						\
	(!RHEL_RELEASE_CODE && LINUX_VERSION_CODE >= KERNEL_VERSION(x, y, 0))
#define VER_RHEL_LT(x, y)						\
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(x, y))
#define VER_RHEL_GE(x, y)						\
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(x, y))
#define VER_IS_VANILLA	!RHEL_RELEASE_CODE

#define COMPAT__USE_DMA_SKIP_SYNC	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
#define COMPAT__HAS_DEVLINK	(VER_VANILLA_GE(4, 6) || VER_RHEL_GE(7, 4))

#define COMPAT__CAN_HAVE_MULTIPLE_MOD_TABLES \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0))

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#if VER_VANILLA_GE(4, 9) || VER_RHEL_GE(7, 5)
#include <linux/bitfield.h>
#endif
#include <linux/random.h>

#ifndef PCI_VENDOR_ID_NETRONOME
#define PCI_VENDOR_ID_NETRONOME		0x19ee
#endif
#ifndef PCI_DEVICE_ID_NETRONOME_NFP4000
#define PCI_DEVICE_ID_NETRONOME_NFP4000	0x4000
#endif
#ifndef PCI_DEVICE_ID_NETRONOME_NFP6000
#define PCI_DEVICE_ID_NETRONOME_NFP6000	0x6000
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
#include <linux/sizes.h>
#else
# if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
#  include <asm-generic/sizes.h>
# else
#   define SZ_1M	(1024 * 1024)
#   define SZ_512M	(512 * 1024 * 1024)
# endif
#endif

#include <linux/bitops.h>
#ifndef BIT
#define BIT(nr)			(1UL << (nr))
#endif
#ifndef BIT_ULL
#define BIT_ULL(nr)		(1ULL << (nr))
#endif

#ifndef GENMASK
#define GENMASK(h, l) \
	((~0UL << (l)) & (~0UL >> (BITS_PER_LONG - (h) - 1)))
#endif

#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l) \
	((~0ULL << (l)) & (~0ULL >> (BITS_PER_LONG_LONG - (h) - 1)))
#endif

#ifndef dma_rmb
#define dma_rmb() rmb()
#endif

#ifndef READ_ONCE
#define READ_ONCE(x)	(x)
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 27))
#if defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64) || \
	defined(CONFIG_PHYS_ADDR_T_64BIT)
/* phys_addr_t was introduced in mainline after 2.6.27 but some older
 * vendor kernels define it as well. Use a #define to override these
 * definitions. */
#define phys_addr_t u64
#else
#define phys_addr_t u32
#endif
#endif /* KERNEL_VERSION(2, 6, 27)  */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
typedef unsigned long uintptr_t;
#endif

#ifndef __maybe_unused
#define __maybe_unused  __attribute__((unused))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28)
#define IORESOURCE_TYPE_BITS	0x00000f00
static inline unsigned long resource_type(const struct resource *res)
{
	return res->flags & IORESOURCE_TYPE_BITS;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 21)
static inline void __iomem *compat_devm_ioremap_nocache(struct device *dev,
							resource_size_t offset,
							unsigned long size)
{
	return ioremap_nocache(offset, size);
}

static inline void compat_devm_iounmap(struct device *dev, void __iomem *addr)
{
	iounmap(addr);
}

#undef devm_ioremap_nocache
#undef devm_iounmap
#define devm_ioremap_nocache(_d, _o, _s) compat_devm_ioremap_nocache(_d, _o, _s)
#define devm_iounmap(_d, _a) compat_devm_iounmap(_d, _a)
#endif /* < 2.6.21 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
static inline int compat_kstrtoul(const char *str, int base, unsigned long *res)
{
	char *cp;
	*res = simple_strtoul(str, &cp, base);
	if (cp && *cp == '\n')
		cp++;

	return (!cp || *cp != 0 || (cp - str) == 0) ? -EINVAL : 0;
}

#define kstrtoul(str, base, res) compat_kstrtoul(str, base, res)
#endif /* < KERNEL_VERSION(3, 0, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#include <linux/netdevice.h>
#define compat_netdev_printk(dev, level, fmt, args...) \
	({ printk("%s%s: " fmt, level, dev->name, ##args); })
#ifndef netdev_info
#define netdev_info(dev, format, args...) \
	compat_netdev_printk(dev, KERN_INFO, format, ##args)
#endif
#ifndef netdev_dbg
#define netdev_dbg(dev, format, args...) \
	compat_netdev_printk(dev, KERN_DEBUG, format, ##args)
#endif
#ifndef netdev_warn
#define netdev_warn(dev, format, args...) \
	compat_netdev_printk(dev, KERN_WARNING, format, ##args)
#endif
#ifndef netdev_err
#define netdev_err(dev, format, args...) \
	compat_netdev_printk(dev, KERN_ERR, format, ##args)
#endif
#endif /* < KERNEL_VERSION(3, 0, 0) */

#if VER_VANILLA_LT(3, 12) || VER_RHEL_LT(7, 1)
static inline int PTR_ERR_OR_ZERO(const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	return 0;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
static inline
int compat_dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);

	if (rc == 0)
		dma_set_coherent_mask(dev, mask);

	return rc;
}
#define dma_set_mask_and_coherent(dev, mask) \
	compat_dma_set_mask_and_coherent(dev, mask)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
#define request_firmware_direct	request_firmware

static inline int _pci_enable_msi_range(struct pci_dev *dev,
					int minvec, int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msi_block(dev, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}

#define pci_enable_msi_range(dev, minv, maxv) \
	_pci_enable_msi_range(dev, minv, maxv)

static inline int compat_pci_enable_msix_range(struct pci_dev *dev,
					       struct msix_entry *entries,
					       int minvec, int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msix(dev, entries, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}

#define pci_enable_msix_range(dev, entries, minv, maxv) \
	compat_pci_enable_msix_range(dev, entries, minv, maxv)
#endif /* < 3.14 */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 26)
#include <linux/mm.h>
/*
 * This function was introduced after 2.6.26 and the implementation here
 * is suboptimal in that it potentially allocates more memory than necessary.
 * The in kernel implementation of alloc_pages_exact() calls a non-exported
 * function (split_page()) which we can't use in this wrapper.
 */
static inline void *alloc_pages_exact(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr;

	addr = __get_free_pages(gfp_mask, order);

	return (void *)addr;
}

static inline void free_pages_exact(void *virt, size_t size)
{
	unsigned long addr = (unsigned long)virt;
	unsigned long end = addr + PAGE_ALIGN(size);

	while (addr < end) {
		free_page(addr);
		addr += PAGE_SIZE;
	}
}
#endif /* KERNEL_VERSION(2, 6, 26)  */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 25)
#include <linux/device.h>
static inline const char *dev_name(const struct device *dev)
{
	return kobject_name(&dev->kobj);
}
#endif /* KERNEL_VERSION(2, 6, 25)  */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29)
#include <linux/ftrace.h>
#define trace_printk ftrace_printk
#endif
#else
#define trace_printk(args...)
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 24))
#define _NEED_PROC_CREATE
#endif

#if (RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(5, 0))
#undef _NEED_PROC_CREATE
#endif

#ifdef _NEED_PROC_CREATE
#include <linux/proc_fs.h>

static inline struct proc_dir_entry *proc_create(const char *name, mode_t mode,
						 struct proc_dir_entry *parent,
						 const struct file_operations
						 *proc_fops)
{
	struct proc_dir_entry *pde;

	pde = create_proc_entry(name, mode, parent);
	if (pde)
		pde->proc_fops = proc_fops;

	return pde;
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24)
#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL << (n)) - 1))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20)
/* Copied from fs/seq_file.c.  In the older kernels, the
 * seq_operations parameter is declared without the const, breaking
 * the build. */
#include <linux/seq_file.h>
#define seq_open(file, op) const_seq_open(file, op)
static inline int const_seq_open(struct file *file,
				 const struct seq_operations *op)
{
	struct seq_file *p = file->private_data;

	if (!p) {
		p = kmalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		file->private_data = p;
	}
	memset(p, 0, sizeof(*p));
	mutex_init(&p->lock);
	p->op = (struct seq_operations *)op;
	file->f_version = 0;
	file->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);
	return 0;
}
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 33)
#ifndef sysfs_attr_init
#define sysfs_attr_init(x) do { } while (0)
#endif
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 32)
static inline long compat_IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

#undef IS_ERR_OR_NULL
#define IS_ERR_OR_NULL(x) compat_IS_ERR_OR_NULL(x)
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 29)
int pci_enable_msi(struct pci_dev *dev);
static inline int pci_enable_msi_block(struct pci_dev *dev, unsigned int nvec)
{
	if (nvec > 1)
		return 1;
	return pci_enable_msi(dev);
}
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 28) && defined(CONFIG_X86_32)
static inline __u64 readq(const void __iomem *addr)
{
	const u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

static inline void writeq(__u64 val, void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
#define HAVE_NET_DEVICE_OPS
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
#define pci_stop_and_remove_bus_device pci_remove_bus_device
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#define PDE_DATA(inode) (PROC_I(inode)->pde->data)
#endif

#include <linux/mm.h>
#ifndef VM_RESERVED
#define VM_RESERVED (VM_DONTEXPAND | VM_DONTDUMP)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0))
#ifndef SIZE_MAX
#define SIZE_MAX        (~(size_t)0)
#endif
static inline void *_kmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return __kmalloc(n * size, flags);
}

#define kmalloc_array(n, size, flags) _kmalloc_array(n, size, flags)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0))
#define SUPPORTED_40000baseKR4_Full   BIT(23)
#define SUPPORTED_40000baseCR4_Full   BIT(24)
#define SUPPORTED_40000baseSR4_Full   BIT(25)
#define SUPPORTED_40000baseLR4_Full   BIT(26)

#define ADVERTISED_40000baseKR4_Full  BIT(23)
#define ADVERTISED_40000baseCR4_Full  BIT(24)
#define ADVERTISED_40000baseSR4_Full  BIT(25)
#define ADVERTISED_40000baseLR4_Full  BIT(26)
#endif

/* SR-IOV related compat */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)) && \
	(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6, 5))
static inline unsigned int pci_sriov_get_totalvfs(struct pci_dev *pdev)
{
	u16 total = 0;
	int pos;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (pos)
		pci_read_config_word(pdev, pos + PCI_SRIOV_TOTAL_VF, &total);

	return total;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0))
/* In 3.2+ this is part of the  pci_dev_flags enum */
#define PCI_DEV_FLAGS_ASSIGNED 4
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0)) && \
	(RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(6, 5))
static inline int pci_vfs_assigned(struct pci_dev *pdev)
{
	struct pci_dev *vfdev;
	unsigned int vfs_assigned = 0;
	unsigned short dev_id;
	int pos;

	/* only search if we are a PF */
	if (!pdev->is_physfn)
		return 0;

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_SRIOV);
	if (!pos)
		return 0;

	/*
	 * determine the device ID for the VFs, the vendor ID will be the
	 * same as the PF so there is no need to check for that one
	 */
	pci_read_config_word(pdev, pos + PCI_SRIOV_VF_DID, &dev_id);

	/* loop through all the VFs to see if we own any that are assigned */
	vfdev = pci_get_device(pdev->vendor, dev_id, NULL);
	while (vfdev) {
		/*
		 * It is considered assigned if it is a virtual function with
		 * our dev as the physical function and the assigned bit is set
		 */
		if (vfdev->is_virtfn && (vfdev->physfn == pdev) &&
		    (vfdev->dev_flags & PCI_DEV_FLAGS_ASSIGNED))
			vfs_assigned++;

		vfdev = pci_get_device(pdev->vendor, dev_id, vfdev);
	}

	return vfs_assigned;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
static inline void compat_ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}

#define ether_addr_copy(dst, src) compat_ether_addr_copy(dst, src)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#define list_first_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0))
static inline int compat_kstrtol(const char *cp, int base, long *valp)
{
	char *tmp;
	long val;

	val = simple_strtol(cp, &tmp, base);
	if (!tmp || *tmp != 0)
		return -EINVAL;

	if (valp)
		*valp = val;

	return 0;
}

#define kstrtol(cp, base, valp) compat_kstrtol(cp, base, valp)
#endif

/* v3.17.0
 * do_getttimeofday() moved from linux/time.h to linux/timekeeping.h
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0))
#include <linux/time.h>
#else
#include <linux/timekeeping.h>
#endif

/* v3.17.0
 * alloc_netdev() takes an additional parameter
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0))
#define NET_NAME_UNKNOWN ""
static inline
struct net_device *compat_alloc_netdev(int sizeof_priv,
				       const char *name,
				       const char *assign_type,
				       void (*setup)(struct net_device *))
{
	return alloc_netdev(sizeof_priv, name, setup);
}

#undef alloc_netdev
#define alloc_netdev(sz, nm, ty, setup) compat_alloc_netdev(sz, nm, ty, setup)
#endif

/* v4.3
 * 4a7cc8316705 ("genirq/MSI: Move msi_list from struct pci_dev to
 *		 struct device")
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
#define compat_for_each_msi(_desc, _dev)			\
	list_for_each_entry((_desc), &(_dev)->msi_list, list)
#else
#define compat_for_each_msi(_desc, _dev)	\
	list_for_each_entry((_desc), &(_dev)->dev.msi_list, list)
#endif

#if !COMPAT__HAS_DEVLINK
struct devlink_port {
	int dummy;
};

struct devlink_ops {
	int dummy;
};

struct devlink {
	int dummy;
};

static inline struct devlink *
devlink_alloc(const struct devlink_ops *ops, size_t priv_size)
{
	return kzalloc(priv_size, GFP_KERNEL);
}

static inline void *devlink_priv(struct devlink *p)
{
	return p;
}

static inline struct devlink *priv_to_devlink(void *p)
{
	return p;
}

static inline int devlink_register(struct devlink *p, struct device *d)
{
	return 0;
}

static inline void devlink_unregister(struct devlink *d) { }

static inline void devlink_free(struct devlink *p)
{
	kfree(p);
}
#endif

#if VER_VANILLA_LT(4, 7) || VER_RHEL_LT(7, 4)
static inline void
netif_trans_update(struct net_device *netdev)
{
	netdev->trans_start = jiffies;
}
#endif

#if VER_VANILLA_LT(4, 8) || VER_RHEL_LT(7, 4)
enum devlink_eswitch_mode {
	DEVLINK_ESWITCH_MODE_LEGACY,
	DEVLINK_ESWITCH_MODE_SWITCHDEV,
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
static inline const struct file_operations *
compat_debugfs_real_fops(const struct file *file)
{
	return file->f_path.dentry->d_fsdata;
}
#define debugfs_real_fops compat_debugfs_real_fops
#else
/* WARNING: this one only works for the DebugFS TX vs XDP ring use case!!! */
#define debugfs_real_fops(x) &nfp_tx_q_fops
#endif /* >= 4.8 */
#endif

#if VER_VANILLA_LT(4, 9) || VER_RHEL_LT(7, 5)
#define _c1(x)  ((x) & 1)
#define _c2(x)  ((((x)& 0x0003) &&  !_c1(x)) * ( _c1((x) >>  1) +  1) +  _c1(x))
#define _c4(x)  ((((x)& 0x000f) &&  !_c2(x)) * ( _c2((x) >>  2) +  2) +  _c2(x))
#define _c8(x)  ((((x)& 0x00ff) &&  !_c4(x)) * ( _c4((x) >>  4) +  4) +  _c4(x))
#define _c16(x) ((((x)& 0xffff) &&  !_c8(x)) * ( _c8((x) >>  8) +  8) +  _c8(x))
#define _c32(x) ((((x)&    ~0U) && !_c16(x)) * (_c16((x) >> 16) + 16) + _c16(x))
#define _c64(x) ((((x)&  ~0ULL) && !_c32(x)) * (_c32((x) >> 32) + 32) + _c32(x))

#define c64(x) (_c64(x) - 1)

#define FIELD_GET(MASK, val)  ((((u64)val) & (MASK)) >> c64((u64)MASK))
#define FIELD_PREP(MASK, val)  ((((u64)val) << c64((u64)MASK)) & (MASK))
#define __bf_shf	c64

#define __BF_FIELD_CHECK(_mask, _reg, _val, _pfx)	do {} while (0)
#endif

#ifndef FIELD_FIT
#define FIELD_FIT(mask, val)	(!((((u64)val) << __bf_shf(mask)) & ~(mask)))
#endif

static inline unsigned long compat_vmf_get_addr(struct vm_fault *vmf)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	return vmf->address;
#else
	return (unsigned long)vmf->virtual_address;
#endif
}

#if !COMPAT__USE_DMA_SKIP_SYNC
#undef DMA_ATTR_SKIP_CPU_SYNC
#define DMA_ATTR_SKIP_CPU_SYNC 0
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
struct netlink_ext_ack;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
#define pci_enable_msix pci_enable_msix_exact
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
static inline void timer_setup(struct timer_list *t, void (*f)(unsigned long),
			       unsigned int flags)
{
	__setup_timer(t, f, (unsigned long)t, flags);
}

#define from_timer(var, callback_timer, timer_fieldname)		\
	container_of((void *)callback_timer, typeof(*var), timer_fieldname)
#endif

/* Kconfig will add this variable for RHEL 7.5+, however, we intentionally
 * disable support for this feature.
 */
#if VER_RHEL_GE(7, 5)
#undef CONFIG_NFP_APP_FLOWER
#endif

#if !LINUX_RELEASE_4_16
struct xdp_rxq_info {
	int empty;
};
#endif

#endif /* __KERNEL__NFP_COMPAT_H__ */
