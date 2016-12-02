# Netronome Flow Processor (NFP) Kernel Drivers

These drivers support Netronome's line of Flow Processor devices,
including the NFP3200 and NFP6000 model lines.

This archive builds the nfp.ko module which can be used to expose
networking devices (netdevs) and/or user space access to the device
via a character device.

For more information, please see:

  http://www.netronome.com

If questions arise or an issue is identified related the released
driver code, please contact either your local Netronome contact or
email us on:

  oss-drivers@netronome.com


# Acquiring Firmware

The NFP3200 and NFP6000 devices require application
specific firmware to function.

Please contact support@netronome.com for the latest
firmware for your platform and device.

Once acquired, install the firmware to `/lib/firmware`:

For the NFP3200 device family:

    # mkdir -p /lib/firmware/netronome
    # cp nfp3200_net.cat /lib/firmware/netronome

For the NFP6000/NFP4000 device family:

    # mkdir -p /lib/firmware/netronome
    # cp nfp6000_net.cat /lib/firmware/netronome

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

Note that in standard build configuration user space access can still
be enabled for this mode by setting the nfp_dev_cpp parameter to true,
but is disabled by default.

## VF driver

The nfp.ko contains a driver used to provide NIC-style access to Virtual
Functions of the device when operating in PCI SR-IOV mode.

For example, if a physical NFP6000 device was running Netronome SDN,
and had assigned a rule matching 'all 172.16.0.0/24 received' to VF 5,
then the NFP6000's SR-IOV device `#5` would use this driver to provide a
NIC style interface to the flows that match that rule.

## Sources

| Source              | Description                                           |
| ------------------- | ----------------------------------------------------- |
| nfp_main.c          | NFP PF low-level PCIe and FW loading routines         |
| nfp_net_main.c      | NFP NIC PF low-level routines                         |
| nfp_netvf_main.c    | NFP NIC VF low-level routines                         |
| nfp_net_common.c    | NFP NIC common interface functions                    |
| nfp_net_ethtool.c   | NFP NIC ethtool interface support                     |
| nfp_dev_cpp.c       | User space access via /dev/nfp-cpp-N interface        |
| nfpcore/            | NFP Core Library, see ([NFP Core Library](#nfp-core-library)) |

### Parameters

NOTE: `modinfo nfp.ko` is the authoritative documentation,
this is only presented here as a reference.

| Parameter       | Default | Comment                                         |
| --------------- | ------- | ----------------------------------------------- |
| nfp_pf_netdev   |    true | PF driver in [Netdev mode](#pf-netdev-mode)     |
| nfp_fallback    |    true | In netdev mode stay bound even if netdevs failed|
| nfp_dev_cpp     |    true | Enable NFP CPP user space /dev interface        |
| ~~fw_stop_on_fail~~ | ~~false~~ | ~~Fail init if no suitable FW is present~~|
| fw_load_required |  false | Fail init if no suitable FW is present          |
| nfp_net_vnic    |   false | vNIC net devices [1]                            |
| nfp_net_vnic_pollinterval | 10 | Polling interval for Rx/Tx queues (in ms)  |
| nfp_net_vnic_debug | false | Enable debug printk messages                   |
| nfp_mon_err     |   false | ECC Monitor [2]                                 |
| nfp_mon_err_pollinterval | 10 | Polling interval for error checking (in ms) |
| nfp_reset       |   false | Reset the NFP on init [3]                       |
| nfp_reset_on_exit | false | Reset the NFP on exit                           |
| hwinfo_debug    |   false | Enable to log hwinfo contents on load           |
| board_state     |      15 | HWInfo board.state to wait for. (range: 0..15)  |
| hwinfo_wait     |      10 | Wait N sec for board.state match, -1 = forever  |
| nfp6000_explicit_bars | 4 | Number of explicit BARs. (range: 1..4)          |
| nfp6000_debug   |   false | Enable debugging for the NFP6000 PCIe           |
| nfp3200_debug   |   false | Enable debugging for the NFP3200 PCIe           |
| nfp6000_firmware | (none) | NFP6000 firmware to load from /lib/firmware/    |

NOTES:

1. The vNIC net device creates a pseudo-NIC for NFP ARM Linux systems.
2. The 'ECC Monitor' example is for the NFP3200 hardware only.
3. Reset on init will be performed anyway if firmware file is specified.

## NFP Core Library

The NFP Core Library is used by `nfp.ko` kernel module to load firmware,
and provide other low-level accesses to the NFP.

### Sources

All sources are in `src/nfpcore/`:

| Source              | Type      | Description                               |
| ------------------- | ----------|------------------------------------------ |
| crc32.c             | API       | CRC32 library                             |
| nfp3200_pcie.c      | Transport | NFP3200 PCIe interface                    |
| nfp3200_plat.c      | Transport | NFP3200/NFP6000 ARM interface             |
| nfp6000_pcie.c      | Transport | NFP6000 PCIe interface                    |
| nfp_ca.c            | API       | CPP Action firmware file parser           |
| nfp_cppcore.c       | API       | CPP bus core                              |
| nfp_cpplib.c        | API       | CPP bus helper                            |
| nfp_device.c        | API       | NFP chip interface                        |
| nfp_em_manager.c    | API       | NFP Event Monitor                         |
| nfp_export.c        | API       | List of all EXPORT_SYMBOLs                |
| nfp_gpio.c          | API       | NFP GPIO access                           |
| nfp_hwinfo.c        | API       | NFP Hardware Info Database                |
| nfp_i2c.c           | API       | NFP I2C access                            |
| nfp_mip.c           | API       | Microcode Information Page                |
| nfp_mon_err.c       | Example   | ECC error monitor for the NFP3200         |
| nfp_nbi.c           | API       | NFP NBI access                            |
| nfp_nbi_mac_eth.c   | API       | NFP NBI Ethernet MAC access               |
| nfp_nbi_phymod.c    | API       | NFP NBI Ethernet PHY access               |
| nfp_net_vnic.c      | Example   | Pseudo-NIC for interfacing with NFP Linux |
| nfp_nffw.c          | API       | NFP NFP Flow Firmware interface           |
| nfp_nsp.c           | API       | NIC Service Processor interface           |
| nfp_platform.c      | API       | NFP CPP bus device registration           |
| nfp_power.c         | API       | NFP subsystem power control               |
| nfp_reset.c         | API       | NFP controlled soft reset                 |
| nfp_resource.c      | API       | NFP Resource management                   |
| nfp_rtsym.c         | API       | NFP Firmware Runtime Symbol access        |
| nfp_spi.c           | API       | NFP SPI access                            |
| sff_8431.c          | PHY       | PHY driver for SFP/SFP+                   |
| sff_8436.c          | PHY       | PHY driver for QSFP                       |
| sff_8647.c          | PHY       | PHY driver for CXP                        |
