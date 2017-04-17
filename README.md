# Netronome Flow Processor (NFP) Kernel Drivers

These drivers support Netronome's line of Flow Processor devices,
including the NFP4000 and NFP6000 model lines.

The repository builds the `nfp.ko` module which can be used to expose
networking devices (netdevs) and/or user space access to the device
via a character device.

The VF driver for NFP4000 and NFP6000 is available in upstream Linux
kernel since `4.5` release.  The PF driver was added in Linux `4.11`.
This repository contains the same driver as upstream with necessary
compatibility code to make the latest version of the code build for
older kernels. We currently support kernels back to the `3.8` version,
support for older versions can be added if necessary.

Compared to upstream drivers this repository contains:
 - non-PCI transport support to enable building the driver for the
   on-chip control processor;
 - support for netdev-based communication with the on-chip control
   processor;
 - optional low-level user space ABI for accessing card internals.

For more information, please visit: http://www.netronome.com

If questions arise or an issue is identified related the released
driver code, please contact either your local Netronome contact or
email us on: oss-drivers@netronome.com

# Acquiring Firmware

The NFP4000 and NFP6000 devices require application
specific firmware to function.

Please contact support@netronome.com for the latest
firmware for your platform and device.

Once acquired, install the firmware in `/lib/firmware`
(firmware files should be placed in `netronome` subdirectory).

# Building and Installing

Building and installing for the currently running kernel:

    $ make
    $ sudo make install

To clean up use the `clean` target

    $ make clean

For a more verbose build use the `noisy` target

    $ make noisy

To override the kernel version to build for set `KVER`:

    $ make KVER=<version>
    $ sudo make KVER=<version> install

The `Makefile` searches a number of standard locations
for the configured kernel sources.

To override the detected location, set `KSRC`:

    $ make KSRC=<location of kernel source>

## Additional targets:

| Command         | Action                                          |
| --------------- | ----------------------------------------------- |
| make coccicheck | Runs Coccinelle/coccicheck[1]                   |
| make sparse     | Runs sparse, a tool for static code analysis[2] |
| make nfp_net    | Build the driver limited to netdev operation    |

1. Requires `coccinelle` to be installed, e.g.

    $ sudo apt-get install coccinelle

2. Requires the `sparse` tool to be installed, e.g.,

    $ sudo apt-get install sparse

# Troubleshooting

If you're running the driver with user space access enabled you will be
able to use all Netronome's proprietary `nfp-*` tools.  This section only
covers standard debugging interfaces based on kernel infrastructure and
which are always available.

## Probing output

Most basic set of information is printed when driver probes a device.
These include versions of various hardware and firmware components.

## Netdev information

`ethtool -i <ifcname>` provides user with basic set of application FW and
flash FW versions.  Note that driver version for driver built in-tree will
be equal to the kernel version string and for out-of-tree driver it will
either contain the git hash if build inside a git repository or contents
of the `.revision` file.  In both cases out of tree driver build will have
`(o-o-t)` appended to distinguish from in-tree builds.

## DebugFS

`nfp_net` directory contains information about queue state for all netdevs
using the driver.  It can be used to inspect contents of memory rings and
position of driver and hardware pointers for RX, TX and XDP rings.

## PCI BAR access

`ethtool -d <ifcname>` can be used to dump the PCI netdev memory.

## NSP logs access

`ethtool -w <ifcname> data <outfile>` dumps the logs of the Service Processor.

# Kernel Modules

This section describes how kernel modules are structured.

The nfp.ko module provides drivers for both PFs and VFs.  VFs can only
be used as netdevs.  In case of PF one can select whether to load the
driver in netdev mode which will create networking interfaces or only
expose low-level API to the user space and run health monitoring, and
diagnostics.

## PF non-netdev mode

This mode is used by the Netronome SDN products for health monitoring,
loading firmware, and diagnostics.

It provides a low-level interface into the NFP, and does not require
that a NFP firmware be loaded.

## PF netdev mode

In this mode module provides a Linux network device interface on
the NFP's physical function.  It requires appropriate FW image to
be either pre-loaded or available in `/lib/firmware/netronome/` to
work.  Systems using Netronome SDN products currently do use this
mode.

Note that in standard build (i.e. not `make nfp_net`) low-level
user space ABI of non-netdev mode can still be exposed by setting
the `nfp_dev_cpp` parameter to true, but is disabled by default.

## VF driver

The nfp.ko contains a driver used to provide NIC-style access to Virtual
Functions of the device when operating in PCI SR-IOV mode.

*For example, if a physical NFP6000 device was running Netronome SDN,
and had assigned a rule matching `'all 172.16.0.0/24 received'` to VF 5,
then the NFP6000's SR-IOV device `#5` would use this driver to provide a
NIC style interface to the flows that match that rule.*

## nfp6000 quirks

NFP4000/NFP6000 chips need a minor PCI quirk to avoid system crashing
after particular type of PCI config space addresses from user space.
If you're using the NFP on an old kernel you may see this message in
the logs:
```
Error: this kernel does not have quirk_nfp6000
Please contact support@netronome.com for more information
```
Suggested solution is to update your kernel.  The fix is present in
upstream Linux `4.5`, but major distributions have backported it to
older kernels, too.  If updating the kernel is not an option and you
are certain user space will not trigger the types of accesses which
may fault - you can attempt using the ``ignore_quirks'" parameter
although this is not guaranteed to work on systems requiring the fix.

## Module parameters

NOTE: `modinfo nfp.ko` is the authoritative documentation,
this is only presented here as a reference.

| Parameter       | Default | Comment                                         |
| --------------- | ------- | ----------------------------------------------- |
| ignore_quirks   |   false | Ignore the check for NFP6000 PCI quirks         |
| nfp_pf_netdev   |    true | PF driver in [Netdev mode](#pf-netdev-mode)     |
| nfp_fallback    |    true | In netdev mode stay bound even if netdevs failed|
| nfp_dev_cpp     |    true | Enable NFP CPP user space /dev interface        |
| ~~fw_stop_on_fail~~ | ~~false~~ | ~~Fail init if no suitable FW is present~~|
| fw_load_required |  false | Fail init if no suitable FW is present          |
| nfp_net_vnic    |   false | vNIC net devices [1]                            |
| nfp_net_vnic_pollinterval | 10 | Polling interval for Rx/Tx queues (in ms)  |
| nfp_net_vnic_debug | false | Enable debug printk messages                   |
| nfp_reset       |   false | Reset the NFP on init [2]                       |
| nfp_reset_on_exit | false | Reset the NFP on exit                           |
| hwinfo_debug    |   false | Enable to log hwinfo contents on load           |
| board_state     |      15 | HWInfo board.state to wait for. (range: 0..15)  |
| hwinfo_wait     |      10 | Wait N sec for board.state match, -1 = forever  |
| nfp6000_explicit_bars | 4 | Number of explicit BARs. (range: 1..4)          |
| nfp6000_debug   |   false | Enable debugging for the NFP6000 PCIe           |
| nfp6000_firmware | (none) | NFP6000 firmware to load from /lib/firmware/    |

NOTES:

1. The vNIC net device creates a pseudo-NIC for NFP ARM Linux systems.
2. Reset on init will be performed anyway if firmware file is specified.
