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

For more information, please visit: http://www.netronome.com or
http://open-nfp.org/.

If questions arise or an issue is identified related the released
driver code, please contact either your local Netronome contact or
email us on: oss-drivers@netronome.com

# Building and Installing

Building and installing for the currently running kernel:

    $ make
    $ sudo make install

To clean up use the `clean` target:

    $ make clean

To override the kernel version to build for set `KVER`:

    $ make KVER=<version>
    $ sudo make KVER=<version> install

The `Makefile` searches a number of standard locations for the configured
kernel sources.  To override the detected location, set `KSRC`:

    $ make KSRC=<location of kernel build>

## Additional targets:

| Command         | Action                                            |
| --------------- | ------------------------------------------------- |
| make noisy      | Verbose build with printing executed commands     |
| make coccicheck | Runs Coccinelle/coccicheck (reqires `coccinelle`) |
| make sparse     | Runs `sparse`, a tool for static code analysis    |
| make nfp_net    | Build the driver limited to netdev operation      |

# Acquiring Firmware

The NFP4000 and NFP6000 devices require application specific firmware
to function.  Firmware files contain card type (`AMDA-*` string), media
config etc.  They should be placed in `/lib/firmware/netronome` directory.

Firmware for basic NIC operation is available in the upstream
`linux-firmware.git` repository, and if your distribution kernel is `4.11`
or newer you will most likely have it on your system already.  For
more application specific firmware files please contact
support@netronome.com.

## Dealing with multiple projects

NFP hardware is fully programmable therefore there can be different
firmware images targeting different applications.  We recommend placing
actual firmware files in application-named subdirectories in
`/lib/firmware/netronome` and linking the desired files, e.g.:
```
$ tree /lib/firmware/netronome/
/lib/firmware/netronome/
├── bpf
│   ├── nic_AMDA0081-0001_1x40.nffw
│   └── nic_AMDA0081-0001_4x10.nffw
├── flower
│   ├── nic_AMDA0081-0001_1x40.nffw
│   └── nic_AMDA0081-0001_4x10.nffw
├── nic
│   ├── nic_AMDA0081-0001_1x40.nffw
│   └── nic_AMDA0081-0001_4x10.nffw
├── nic_AMDA0081-0001_1x40.nffw -> bpf/nic_AMDA0081-0001_1x40.nffw
└── nic_AMDA0081-0001_4x10.nffw -> bpf/nic_AMDA0081-0001_4x10.nffw

3 directories, 8 files
```
You may need to use hard instead of symbolic links on distributions
which use old `mkinitrd` command instead of `dracut` (e.g. Ubuntu).

After changing firmware files you may need to regenerate the initramfs
image.  Initramfs contains drivers and firmware files your system may
need to boot.  Refer to the documentation of your distribution to find
out how to update initramfs.  Good indication of stale initramfs
is system loading wrong driver or firmware on boot, but when driver is
later reloaded manually everything works correctly.

## Selecting firmware per device

Most commonly all cards on the system use the same type of firmware.
If you want to load specific firmware image for a specific card, you
can use either the PCI bus address or serial number.  Driver will print
which files it's looking for when it recognizes a NFP device:
```
nfp: Looking for firmware file in order of priority:
nfp:  netronome/serial-00-12-34-aa-bb-cc-10-ff.nffw: not found
nfp:  netronome/pci-0000:02:00.0.nffw: not found
nfp:  netronome/nic_AMDA0081-0001_1x40.nffw: found, loading...
```
In this case if file (or link) called *serial-00-12-34-aa-bb-5d-10-ff.nffw*
or *pci-0000:02:00.0.nffw* is present in `/lib/firmware/netronome` this
firmware file will take precedence over `nic_AMDA*` files.

Note that `serial-*` and `pci-*` files are **not** automatically included
in initramfs, you will have to refer to documentation of appropriate tools
to find out how to include them.

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

## Control messages

Control messages which driver is exchanging with the FW on the card are
sent to the `devlink_hwmsg` tracepoint.  Note that `trace_printk` will only
display first 64 bytes of the buffers.  You can use the script from this
repo to display the full messages, dump them to `wireshark` etc.:

https://github.com/jpirko/hwmsg_tracing

# Operation modes

The nfp.ko module provides drivers for both PFs and VFs.  VFs can only
be used as netdevs.  In case of PF one can select whether to load the
driver in netdev mode which will create networking interfaces or only
expose low-level API to the user space and run health monitoring,
diagnostics and control device from user space.

NOTE: if you're using Netronome-provided driver packages some
of the defaults mentioned in this document may have been changed
in the `/etc/modprobe.d/netronome.conf` file.

## PF netdev mode

In this mode module provides a Linux network device interface on
the NFP's physical function.  It requires appropriate FW image to
be either pre-loaded or available in `/lib/firmware/netronome/` to
work.  This is the only mode of operation for the upstream driver.

Developers should use this mode if firmware is exposing vNICs on the
PCI PF device.

By default (i.e. not `make nfp_net` build) low-level user space access
ABIs of non-netdev mode will not be exposed, but can be re-enabled with
appropriate module parameters (`nfp_dev_cpp`).

## PF non-netdev mode

This mode is used by the out-of-tree Netronome SDN products for health
monitoring, loading firmware, and diagnostics.  It is enabled by setting
`nfp_pf_netdev` module parameter to `0`.  Driver in this mode will not
expose any netdevs of the PCI PF.

Developers should use this mode if firmware is only exposing vNICs on
the PCI VF devices.

This mode provides a low-level user space interface into the NFP
(`/dev/nfp-cpp-X` file), which is used by development and debugging tools.
It does not require a NFP firmware be loaded at device probe time.

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
| fw_load_required |  false | Fail init if no suitable FW is present          |
| nfp_net_vnic    |   false | vNIC net devices [1]                            |
| nfp_net_vnic_pollinterval | 10 | Polling interval for Rx/Tx queues (in ms)  |
| nfp_net_vnic_debug | false | Enable debug printk messages                   |
| nfp_reset       |   false | Reset the NFP on init [2]                       |
| nfp_reset_on_exit | false | Reset the NFP on exit                           |
| hwinfo_debug    |   false | Enable to log hwinfo contents on load           |
| hwinfo_wait     |      10 | Wait N sec for board.state match, -1 = forever  |
| nfp6000_explicit_bars | 4 | Number of explicit BARs. (range: 1..4)          |
| nfp6000_debug   |   false | Enable debugging for the NFP6000 PCIe           |
| nfp6000_firmware | (none) | NFP6000 firmware to load from /lib/firmware/    |

NOTES:

1. The vNIC net device creates a pseudo-NIC for NFP ARM Linux systems.
2. Reset on init will be performed anyway if firmware file is specified.
