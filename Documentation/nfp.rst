.. SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
   Copyright (C) 2019 Netronome Systems, Inc.

=============================================
Netronome Flow Processor (NFP) Network Driver
=============================================

Statistics
==========

Following device statistics are available through the ``ethtool -S`` interface:

.. flat-table:: NFP device statistics
   :header-rows: 1
   :widths: 3 1 11

   * - Name
     - ID
     - Meaning

   * - dev_rx_discards
     - 1
     - Packet can be discarded on the RX path for one of the following reasons:

        * The NIC is not in promisc mode, and the destination MAC address
          doesn't match the interfaces' MAC address.
        * The received packet is larger than the max buffer size on the host.
          I.e. it exceeds the Layer 3 MRU.
        * There is no freelist descriptor available on the host for the packet.
          It is likely that the NIC couldn't cache one in time.
        * A BPF program discarded the packet.
        * The datapath drop action was executed.
        * The MAC discarded the packet due to lack of ingress buffer space
          on the NIC.

   * - dev_rx_errors
     - 2
     - A packet can be counted (and dropped) as RX error for the following
       reasons:

       * A problem with the VEB lookup (only when SR-IOV is used).
       * A physical layer problem that causes Ethernet errors, like FCS or
         alignment errors. The cause is usually faulty cables or SFPs.

   * - dev_rx_bytes
     - 3
     - Total number of bytes received.

   * - dev_rx_uc_bytes
     - 4
     - Unicast bytes received.

   * - dev_rx_mc_bytes
     - 5
     - Multicast bytes received.

   * - dev_rx_bc_bytes
     - 6
     - Broadcast bytes received.

   * - dev_rx_pkts
     - 7
     - Total number of packets received.

   * - dev_rx_mc_pkts
     - 8
     - Multicast packets received.

   * - dev_rx_bc_pkts
     - 9
     - Broadcast packets received.

   * - dev_tx_discards
     - 10
     - A packet can be discarded in the TX direction if the MAC is
       being flow controlled and the NIC runs out of TX queue space.

   * - dev_tx_errors
     - 11
     - A packet can be counted as TX error (and dropped) for one for the
       following reasons:

       * The packet is an LSO segment, but the Layer 3 or Layer 4 offset
         could not be determined. Therefore LSO could not continue.
       * An invalid packet descriptor was received over PCIe.
       * The packet Layer 3 length exceeds the device MTU.
       * An error on the MAC/physical layer. Usually due to faulty cables or
         SFPs.
       * A CTM buffer could not be allocated.
       * The packet offset was incorrect and could not be fixed by the NIC.

   * - dev_tx_bytes
     - 12
     - Total number of bytes transmitted.

   * - dev_tx_uc_bytes
     - 13
     - Unicast bytes transmitted.

   * - dev_tx_mc_bytes
     - 14
     - Multicast bytes transmitted.

   * - dev_tx_bc_bytes
     - 15
     - Broadcast bytes transmitted.

   * - dev_tx_pkts
     - 16
     - Total number of packets transmitted.

   * - dev_tx_mc_pkts
     - 17
     - Multicast packets transmitted.

   * - dev_tx_bc_pkts
     - 18
     - Broadcast packets transmitted.

Note that statistics unknown to the driver will be displayed as
``dev_unknown_stat$ID``, where ``$ID`` refers to the second column
above.
