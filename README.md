# Network Flow Processor (NFP) Kernel Drivers

These drivers support Netronome and Corigine's line of Flow Processor devices,
including the NFP3800, NFP4000, NFP5000, and NFP6000 models, which are also
incorporated in the companies' family of Agilio SmartNICs. The SR-IOV
physical and virtual functions for these devices are supported by the driver.

This repository builds the `nfp.ko` module which can be used to expose
networking devices (netdevs) and/or user space access to the device
via a character device.

The VF driver for NFP3800, NFP4000, NFP5000, and NFP6000 is available in
upstream Linux kernel since `4.5` release. The PF driver was added in Linux
`4.11`. This repository contains the same driver as upstream with necessary
compatibility code to make the latest version of the code build for
older kernels. We currently support kernels back to version `3.8`,
support for older versions can be added if necessary.

Compared to upstream drivers this repository contains:
 - non-PCI transport support to enable building the driver for the
   on-chip control processor;
 - support for netdev-based communication with the on-chip control
   processor;
 - optional low-level user space ABI for accessing card internals.

For more information, please visit: https://www.corigine.com/. Documentation
and software, such as user manuals, firmware, packaged driver, etc., can be
obtained from https://www.corigine.com/DPUDownload.html.
Additional driver documentation is also available in tree at
`/Documentation/networking/device_drivers/ethernet/netronome/nfp.rst`
from the Linux kernel `5.4` release.

If questions arise or an issue is identified related the released
driver code, please contact either your local Corigine contact or
email us on: smartnic-support@corigine.com

# Building and Installing

Requirements: As with most out-of-tree kernel modules make sure you have the
matching kernel headers for kernel <KVER> installed on your system. Usually
something like `linux-headers-<KVER>-generic` for Ubuntu based systems, or
`kernel-devel-<KVER>` for RHEL based systems.

Building and installing for the currently running kernel:
```shell
$ make
$ sudo make install
```

To clean up use the `clean` target:
```shell
$ make clean
```

To override the kernel version to build for set `KVER`:
```shell
$ make KVER=<version>
$ sudo make KVER=<version> install
```

The `Makefile` searches a number of standard locations for the configured
kernel sources. To override the detected location, set `KSRC`:
```shell
$ make KSRC=<location of kernel build>
```

## Additional targets:

| Command           | Action                                             |
| ----------------- | -------------------------------------------------- |
| `make build`      | (Default) Build the driver (kernel module).        |
| `make coccicheck` | Runs Coccinelle/coccicheck (reqires `coccinelle`). |
| `make install`    | Install the driver to the system.                  |
| `make nfp_net`    | Build the driver limited to netdev operation.      |
| `make noisy`      | Verbose build with printing executed commands.     |
| `make sparse`     | Runs `sparse`, a tool for static code analysis.    |
| `make uninstall`  | Remove the driver from the system.                 |

Note: Ensure libraries (coccicheck, sparse) are installed.

# Acquiring Firmware

The NFP devices require application specific firmware to function.
Application firmware can be located either on the host file system or in
the device flash (if supported by management firmware).

Firmware files on the host filesystem contain card type (`AMDA-*` string),
media config etc. They should be placed in `/lib/firmware/netronome` directory
to load firmware from the host file system.

Firmware for basic NIC operation is available in the upstream
`linux-firmware.git` repository, and if your distribution kernel is `4.11`
or newer you will most likely have it on your system already. For
more application specific firmware files, please visit
https://www.corigine.com/DPUDownload.html or contact
smartnic-support@corigine.com.

## Firmware in NVRAM

Recent versions of management firmware supports loading application
firmware from flash when the host driver gets probed. The firmware loading
policy configuration may be used to configure this feature appropriately.

Devlink or ethtool can be used to update the application firmware on the device
flash by providing the appropriate `nic_AMDA*.nffw` file to the respective
command. Users need to take care to write the correct firmware image for the
card and media configuration to flash.

Available storage space in flash depends on the card being used.

## Dealing with multiple projects

NFP hardware is fully programmable, therefore there can be different
firmware images targeting different applications.

When using application firmware from host, we recommend placing
actual firmware files in application-named subdirectories in
`/lib/firmware/netronome` and linking the desired files, e.g.:
```
$ tree /lib/firmware/netronome/
/lib/firmware/netronome/
├── bpf
│   ├── nic_AMDA0058-0011_2x40.nffw
│   ├── nic_AMDA0058-0012_2x40.nffw
│   ├── nic_AMDA0078-0011_1x100.nffw
│   ├── nic_AMDA0081-0001_1x40.nffw
│   ├── nic_AMDA0081-0001_4x10.nffw
│   ├── nic_AMDA0096-0001_2x10.nffw
│   ├── nic_AMDA0097-0001_2x40.nffw
│   ├── nic_AMDA0097-0001_4x10_1x40.nffw
│   ├── nic_AMDA0097-0001_8x10.nffw
│   ├── nic_AMDA0099-0001_1x10_1x25.nffw
│   ├── nic_AMDA0099-0001_2x10.nffw
│   └── nic_AMDA0099-0001_2x25.nffw
├── flower
│   ├── nic_AMDA0058-0011_1x100.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0011_2x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0011_4x10_1x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0011_8x10.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0012_1x100.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0012_2x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0012_4x10_1x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058-0012_8x10.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0011_1x100.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0011_2x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0011_4x10_1x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0011_8x10.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0012_1x100.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0012_2x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0012_4x10_1x40.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0078-0012_8x10.nffw -> nic_AMDA0058.nffw
│   ├── nic_AMDA0081-0001_1x40.nffw -> nic_AMDA0081.nffw
│   ├── nic_AMDA0081-0001_4x10.nffw -> nic_AMDA0081.nffw
│   ├── nic_AMDA0081.nffw -> nic_AMDA0097.nffw
│   ├── nic_AMDA0096-0001_2x10.nffw -> nic_AMDA0096.nffw
│   ├── nic_AMDA0096.nffw
│   ├── nic_AMDA0097-0001_2x40.nffw -> nic_AMDA0097.nffw
│   ├── nic_AMDA0097-0001_4x10_1x40.nffw -> nic_AMDA0097.nffw
│   ├── nic_AMDA0097-0001_8x10.nffw -> nic_AMDA0097.nffw
│   ├── nic_AMDA0097.nffw
│   ├── nic_AMDA0099-0001_1x10_1x25.nffw -> nic_AMDA0099.nffw
│   ├── nic_AMDA0099-0001_2x10.nffw -> nic_AMDA0099.nffw
│   ├── nic_AMDA0099-0001_2x25.nffw -> nic_AMDA0099.nffw
│   └── nic_AMDA0099.nffw
├── nic
│   ├── nic_AMDA0058-0011_2x40.nffw
│   ├── nic_AMDA0058-0012_2x40.nffw
│   ├── nic_AMDA0078-0011_1x100.nffw
│   ├── nic_AMDA0081-0001_1x40.nffw
│   ├── nic_AMDA0081-0001_4x10.nffw
│   ├── nic_AMDA0096-0001_2x10.nffw
│   ├── nic_AMDA0097-0001_2x40.nffw
│   ├── nic_AMDA0097-0001_4x10_1x40.nffw
│   ├── nic_AMDA0097-0001_8x10.nffw
│   ├── nic_AMDA0099-0001_1x10_1x25.nffw
│   ├── nic_AMDA0099-0001_2x10.nffw
│   └── nic_AMDA0099-0001_2x25.nffw
├── nic-sriov
│   ├── nic_AMDA0058-0011_2x40.nffw
│   ├── nic_AMDA0058-0012_2x40.nffw
│   ├── nic_AMDA0078-0011_1x100.nffw
│   ├── nic_AMDA0081-0001_1x40.nffw
│   ├── nic_AMDA0081-0001_4x10.nffw
│   ├── nic_AMDA0096-0001_2x10.nffw
│   ├── nic_AMDA0097-0001_2x40.nffw
│   ├── nic_AMDA0097-0001_4x10_1x40.nffw
│   ├── nic_AMDA0097-0001_8x10.nffw
│   ├── nic_AMDA0099-0001_1x10_1x25.nffw
│   ├── nic_AMDA0099-0001_2x10.nffw
│   └── nic_AMDA0099-0001_2x25.nffw
├── nic_AMDA0058-0011_2x40.nffw -> nic/nic_AMDA0058-0011_2x40.nffw
├── nic_AMDA0058-0012_2x40.nffw -> nic/nic_AMDA0058-0012_2x40.nffw
├── nic_AMDA0078-0011_1x100.nffw -> nic/nic_AMDA0078-0011_1x100.nffw
├── nic_AMDA0081-0001_1x40.nffw -> nic/nic_AMDA0081-0001_1x40.nffw
├── nic_AMDA0081-0001_4x10.nffw -> nic/nic_AMDA0081-0001_4x10.nffw
├── nic_AMDA0096-0001_2x10.nffw -> nic/nic_AMDA0096-0001_2x10.nffw
├── nic_AMDA0097-0001_2x40.nffw -> nic/nic_AMDA0097-0001_2x40.nffw
├── nic_AMDA0097-0001_4x10_1x40.nffw -> nic/nic_AMDA0097-0001_4x10_1x40.nffw
├── nic_AMDA0097-0001_8x10.nffw -> nic/nic_AMDA0097-0001_8x10.nffw
├── nic_AMDA0099-0001_1x10_1x25.nffw -> nic/nic_AMDA0099-0001_1x10_1x25.nffw
├── nic_AMDA0099-0001_2x10.nffw -> nic/nic_AMDA0099-0001_2x10.nffw
└── nic_AMDA0099-0001_2x25.nffw -> nic/nic_AMDA0099-0001_2x25.nffw

4 directories, 78 files
```
You may need to use hard- instead of symbolic-links on distributions
which use old `mkinitrd` command instead of `dracut` (e.g. Ubuntu).

After changing firmware files you may need to regenerate the initramfs
image to ensure the correct firmware is loaded during system startup.
Initramfs contains drivers and firmware files your system may
need to boot. Refer to the documentation of your distribution to find
out how to update initramfs. A good indication of stale initramfs
is the system loading the wrong driver or firmware on boot, but when the
driver is later reloaded manually, everything works correctly.

## Selecting firmware per device

Most commonly all cards on the system use the same type of firmware.
If you want to load specific firmware image for a specific card, you
can use either the PCI bus address or serial number. Driver will print
which files it's looking for when it recognizes a NFP device:
```
$ dmesg | grep nfp
nfp <pci dbdf>: nfp: Looking for firmware file in order of priority:
nfp <pci dbdf>: nfp:  netronome/serial-00-12-34-aa-bb-cc-10-ff.nffw: not found
nfp <pci dbdf>: nfp:  netronome/pci-0000:02:00.0.nffw: not found
nfp <pci dbdf>: nfp:  netronome/nic_AMDA0081-0001_1x40.nffw: found
nfp <pci dbdf>: nfp:  Soft-resetting the NFP
nfp <pci dbdf>: nfp_nsp: Firmware from driver loaded, no FW selection policy HWInfo key found
nfp <pci dbdf>: Finished loading FW image
```
In this case, if a file (or link) called *serial-00-12-34-aa-bb-5d-10-ff.nffw*
or *pci-0000:02:00.0.nffw* is present in `/lib/firmware/netronome` this
firmware file will take precedence over `nic_AMDA*` files. This enables
you to specify which application firmware to load per card, which may be
useful when multiple cards are installed in the system.

Note that `serial-*` and `pci-*` files are **not** automatically included
in initramfs, you will have to refer to documentation of appropriate tools
to find out how to include them.

# TC Flower Usage

The nfp.ko module provides offload capabilities for several TC flower
features (with `flower` firmware loaded). The list of features include:

Match:
* Ingress Port.
* MAC source and destination address.
* VLAN tag control information.
* IPv4 and 6 source and destination address.
* Transport source and destination port.
* VXLAN/NVGRE/GENEVE header fields.
* MPLS header fields.

Action:
* Push/Pop Vlan.
* Drop.
* Output to Port.
* VXLAN/NVGRE/GENEVE Entunnel.
* Set MAC source and destination address.
* Set IPv4 and 6 source and destination address.
* Set transport source and destination port.

Before configuring filters, it is vital to remember to set up a queueing
discipline. A simple example making use of the ingress qdisc follows:
```shell
tc qdisc add dev <ifcname> handle ffff: ingress
```

Some filter examples follow:
Match ipv4 type and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower action mirred \
egress redirect dev <ifcname>
```

Match destination MAC address and drop:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower dst_mac \
02:12:23:34:45:56 action drop
```

Match vlan id, pop vlan and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol 802.1Q flower vlan_id 600 \
action vlan pop pipe mirred egress redirect dev <ifcname>
```

Match source IPv6 address, push vlan and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ipv6 flower src_ip 22::22 \
action vlan push id 250 pipe mirred egress redirect dev <ifcname>
```

Match destination IPv6 address, set source MAC address and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ipv6 flower dst_ip 11::11 \
action pedit ex munge eth src set 11:22:33:44:55:66 pipe mirred egress \
redirect dev <ifcname>
```

Match source IPv4 address, set source IPv4 address and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower src_ip \
10.20.30.40 action pedit ex munge ip src set 20.30.40.50 pipe mirred \
egress redirect dev <ifcname>
```

Match TCP type, set source TCP port and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower ip_proto tcp \
action pedit ex munge tcp sport set 4282 pipe mirred egress redirect \
dev <ifcname>
```

Match UDP type, set destination UDP port and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower ip_proto udp \
action pedit ex munge udp dport set 4000 pipe mirred egress redirect \
dev <ifcname>
```

Match VXLAN Key ID and Outer UDP destination port and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower enc_dst_port \
4789 enc_dst_ip 10.20.30.40 enc_key_id 123 action mirred egress redirect \
dev <ifcname>
```

Match TCP type, encapsulate in VXLAN and output:
```shell
tc filter add dev <ifcname> parent ffff: protocol ip flower ip_proto tcp \
action tunnel_key set id 123 src_ip 10.0.0.1 dst_ip 10.0.0.2 dst_port 4789 \
action mirred egress redirect dev <vxlan_vtep>
```

Helpful tips:
Dump filter, example:
```shell
tc -s filter show dev <ifcname> parent ffff:
```

Keep an eye on filters:
```shell
tc -s monitor
```

Remove filter, example:
```shell
tc filter del dev <ifcname> parent ffff:
```

Ask for help:
```shell
tc filter add flower help
tc actions help
tc qdisc help
```

# Troubleshooting

If you're running the driver with user space access enabled you will be
able to use all Corigine's proprietary `nfp-*` tools. This section only
covers standard debugging interfaces based on kernel infrastructure and
which are always available.

## Probing output

Most basic set of information is printed when driver probes a device.
These include versions of various hardware and firmware components.

## Netdev information

`ethtool -i <ifcname>` provides the user with a basic set of application FW and
flash FW versions. Note that the driver version for the driver built in-tree
will be equal to the kernel version string and for the out-of-tree driver it
will either contain the git hash if build inside a git repository or contents
of the `.revision` file. In both cases, the out of tree driver build will have
`(o-o-t)` appended to distinguish it from in-tree builds.

## DebugFS

`nfp_net` directory contains information about queue state for all netdevs
using the driver. It can be used to inspect the contents of memory rings and
the position of driver and hardware pointers for RX, TX and XDP rings.

## PCI BAR access

`ethtool -d <ifcname>` can be used to dump the PCI netdev memory.

## NSP logs access

The `tools/dump_nsp_logs.sh` script can be used to dump the logs of the Service
Processor. The script will read the log using standard `ethtool` APIs,
however, if the system is unable to initialize fully it can also use the
Corigine vendor debug tools (if installed).

# Operation modes

The `nfp.ko` module provides drivers for both PFs and VFs. VFs can only
be used as netdevs. In the case of PF, one can select whether to load the
driver in netdev mode, which will create networking interfaces, or only
expose low-level API to the user space and run health monitoring,
diagnostics and control device from user space.

NOTE: The defaults can be overridden by .conf files in /etc/modprobe.d. If
unexpected behaviour is observed, check for any files overriding the defaults
for the nfp.ko module.

## PF netdev mode

In this mode module provides a Linux network device interface on
the NFP's physical function. It requires appropriate FW image to
be either pre-loaded or available in `/lib/firmware/netronome/` to
work. This is the only mode of operation for the upstream driver.

Developers should use this mode if firmware is exposing vNICs on the
PCI PF device.

By default (i.e. not `make nfp_net` build) low-level user space access
ABIs of non-netdev mode will not be exposed, but can be re-enabled with
appropriate module parameters (`nfp_dev_cpp`).

## PF non-netdev mode

This mode is used by the out-of-tree Corigine SmartNIC products for health
monitoring, loading firmware, and diagnostics. It is enabled by setting
`nfp_pf_netdev` module parameter to `0`. Driver in this mode will not
expose any netdevs of the PCI PF.

Developers should use this mode if the firmware is only exposing vNICs on
the PCI VF devices.

This mode provides a low-level user space interface into the NFP
(`/dev/nfp-cpp-<X>` file), which is used by development and debugging tools.
It does not require firmware be loaded at device probe time.

## VF driver

The nfp.ko contains a driver used to provide NIC-style access to Virtual
Functions of the device when operating in PCI SR-IOV mode.

## nfp6000 quirks

NFP4000/NFP5000/NFP6000 chips need a minor PCI quirk to avoid system crashing
after particular type of PCI config space addresses from user space.
If you're using the NFP on an old kernel you may see this message in
the logs:
```
Error: this kernel does not have quirk_nfp6000
Please contact smartnic-support@corigine.com for more information
```
Suggested solution is to update your kernel. The fix is present in
upstream Linux `4.5`, but major distributions have backported it to
older kernels, too. If updating the kernel is not an option and you
are certain user space will not trigger the types of accesses which
may fault - you can attempt using the `ignore_quirks` parameter
although this is not guaranteed to work on systems requiring the fix.

## Module parameters

NOTE: `modinfo nfp.ko` is the authoritative documentation,
this is only presented here as a reference.

| Parameter             | Default | Comment                                                                     |
| --------------------- | ------- | --------------------------------------------------------------------------- |
| force_40b_dma         | false   | Force using 40b dma mask, which allows new HW to use NFD3 firmware          |
| hwinfo_debug          | false   | Enable to log hwinfo contents on load                                       |
| hwinfo_wait           | 20      | -1 for no timeout, or N seconds to wait for hwinfo                          |
| ignore_quirks         | false   | Ignore quirks and load even if the kernel does not have quirk_nfp6000       |
| nfp6000_debug         | false   | Enable debugging for the NFP6000 PCIe                                       |
| nfp6000_explicit_bars | 4       | Number of explicit BARs (0-4)                                               |
| nfp_ctrl_debug        | false   | Create debug netdev for sniffing and injecting FW control messages          |
| nfp_dev_cpp           | !nfp_pf_netdev               | Enable NFP CPP user space /dev interface               |
| nfp_fallback          | nfp_pf_netdev && nfp_dev_cpp | Stay bound to device even if no suitable FW is present |
| nfp_mon_event         | !nfp_pf_netdev               | Event monitor support                                  |
| nfp_net_vnic          | false   | vNIC net devices [1]                                                        |
| nfp_net_vnic_debug    | false   | Enable debug printk messages                                                |
| nfp_net_vnic_pollinterval | 1   | Polling interval for Rx/Tx queues (in ms)                                   |
| nfp_pf_netdev         | true    | PF driver in [Netdev mode](#pf-netdev-mode)                                 |
| nfp_roce_enabled      | false   | Enable RoCE interface registration                                          |
| nfp_roce_ints_num     | 4       | Number of RoCE interrupt vectors                                            |

NOTES:
1. The vNIC net device creates a pseudo-NIC for NFP ARM Linux systems.
