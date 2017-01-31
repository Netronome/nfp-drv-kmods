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
 * nfp_nbi.h
 * nfp6000 NBI API functions
 * Authors: Anthony Egan <tony.egan@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 */

#ifndef __NFP_NBI_H__
#define __NFP_NBI_H__

#include "nfp.h"
#include "nfp_cpp.h"

/* Implemented in nfp_nbi.c */

/*
 * NFP NBI device handle
 */
struct nfp_nbi_dev;

struct nfp_nbi_dev *nfp_nbi_open(struct nfp_cpp *cpp, int nbi);
void nfp_nbi_close(struct nfp_nbi_dev *nfpnbidev);

int nfp_nbi_index(struct nfp_nbi_dev *nfpnbidev);

int nfp_nbi_mac_regr(struct nfp_nbi_dev *nbi, u32 base, u32 reg, u32 *data);
int nfp_nbi_mac_regw(struct nfp_nbi_dev *nbi, u32 base, u32 reg,
		     u32 mask, u32 data);

/* Offset into CTM */
#define NFP_NBI_MAC_STATS_OFFSET          0xed000

/* Single chan stats - 0:7   chan */
#define NFP_NBI_MAC_STATS_ACCCMD_INIT     0x00000000
/* read access */
#define NFP_NBI_MAC_STATS_ACCCMD_READ     0x01000000
/* reset counters */
#define NFP_NBI_MAC_STATS_ACCCMD_RESET    0x04000000
/* clear ECC/parity errors */
#define NFP_NBI_MAC_STATS_ACCCMD_ECCCLR   0x05000000
/* Packets, Bytes, Bad Packets */
#define NFP_NBI_MAC_STATS_ACCTYPE_PKTS    0x00000000
/* FC_Error */
#define NFP_NBI_MAC_STATS_ACCTYPE_FC      0x00010000
/* RX_CRC_Error */
#define NFP_NBI_MAC_STATS_ACCTYPE_RXCRC   0x00020000

/**
 * MAC statistics are accumulated by the nfp_nbi_mac_statsd daemon into
 * 64-bit counters in a reserved memory area. The following structures
 * define the Ethernet port, Channel and Interlaken statistics
 * counters.
 *
 * Port statistics counters
 */
struct nfp_nbi_mac_portstats {
	u64 RxAlignmentErrors;
	u64 RxCBFCPauseFramesReceived0;
	u64 RxCBFCPauseFramesReceived1;
	u64 RxCBFCPauseFramesReceived2;
	u64 RxCBFCPauseFramesReceived3;
	u64 RxCBFCPauseFramesReceived4;
	u64 RxCBFCPauseFramesReceived5;
	u64 RxCBFCPauseFramesReceived6;
	u64 RxCBFCPauseFramesReceived7;
	u64 RxFrameCheckSequenceErrors;
	u64 RxFrameTooLongErrors;
	u64 RxFramesReceivedOK;
	u64 RxInRangeLengthErrors;
	u64 RxPIfInBroadCastPkts;
	u64 RxPIfInErrors;
	u64 RxPIfInMultiCastPkts;
	u64 RxPIfInUniCastPkts;
	u64 RxPStatsDropEvents;
	u64 RxPStatsFragments;
	u64 RxPStatsJabbers;
	u64 RxPStatsOversizePkts;
	u64 RxPStatsPkts;
	u64 RxPStatsPkts1024to1518octets;
	u64 RxPStatsPkts128to255octets;
	u64 RxPStatsPkts1519toMaxoctets;
	u64 RxPStatsPkts256to511octets;
	u64 RxPStatsPkts512to1023octets;
	u64 RxPStatsPkts64octets;
	u64 RxPStatsPkts65to127octets;
	u64 RxPStatsUndersizePkts;
	u64 RxPauseMacCtlFramesReceived;
	u64 RxVlanReceivedOK;
	u64 TxCBFCPauseFramesTransmitted0;
	u64 TxCBFCPauseFramesTransmitted1;
	u64 TxCBFCPauseFramesTransmitted2;
	u64 TxCBFCPauseFramesTransmitted3;
	u64 TxCBFCPauseFramesTransmitted4;
	u64 TxCBFCPauseFramesTransmitted5;
	u64 TxCBFCPauseFramesTransmitted6;
	u64 TxCBFCPauseFramesTransmitted7;
	u64 TxFramesTransmittedOK;
	u64 TxPIfOutBroadCastPkts;
	u64 TxPIfOutErrors;
	u64 TxPIfOutMultiCastPkts;
	u64 TxPIfOutUniCastPkts;
	u64 TxPStatsPkts1024to1518octets;
	u64 TxPStatsPkts128to255octets;
	u64 TxPStatsPkts1518toMAXoctets;
	u64 TxPStatsPkts256to511octets;
	u64 TxPStatsPkts512to1023octets;
	u64 TxPStatsPkts64octets;
	u64 TxPStatsPkts65to127octets;
	u64 TxPauseMacCtlFramesTransmitted;
	u64 TxVlanTransmittedOK;
	u64 RxPIfInOctets;
	u64 TxPIfOutOctets;
};

/**
 * Channel statistics counters
 */
struct nfp_nbi_mac_chanstats {
	u64 RxCIfInErrors;
	u64 RxCIfInUniCastPkts;
	u64 RxCIfInMultiCastPkts;
	u64 RxCIfInBroadCastPkts;
	u64 RxCStatsPkts;
	u64 RxCStatsPkts64octets;
	u64 RxCStatsPkts65to127octets;
	u64 RxCStatsPkts128to255octets;
	u64 RxCStatsPkts256to511octets;
	u64 RxCStatsPkts512to1023octets;
	u64 RxCStatsPkts1024to1518octets;
	u64 RxCStatsPkts1519toMaxoctets;
	u64 RxChanFramesReceivedOK;
	u64 RxChanVlanReceivedOK;
	u64 TxCIfOutBroadCastPkts;
	u64 TxCIfOutErrors;
	u64 TxCIfOutUniCastPkts;
	u64 TxChanFramesTransmittedOK;
	u64 TxChanVlanTransmittedOK;
	u64 TxCIfOutMultiCastPkts;
	u64 RxCIfInOctets;
	u64 RxCStatsOctets;
	u64 TxCIfOutOctets;
};

/**
 * Interlaken single channel statistics counters
 */
struct nfp_nbi_mac_ilkstats {
	u64 LkTxStatsFill;
	u64 LkTxStatsParity;
	u64 LkTxStatsRdParity;
	u64 LkTxStatsWrParity;

	u64 LkTxStatsWrByte;
	u64 LkTxStatsWrPkt;
	u64 LkTxStatsWrErr;
	u64 LkTxStatsRdByte;
	u64 LkTxStatsRdPkt;
	u64 LkTxStatsRdErr;

	u64 LkRxStatsFill;
	u64 LkRxStatsParity;
	u64 LkRxStatsRdParity;
	u64 LkRxStatsWrParity;

	u64 LkRxStatsWrByte;
	u64 LkRxStatsWrPkt;
	u64 LkRxStatsWrErr;
	u64 LkRxStatsRdByte;
	u64 LkRxStatsRdPkt;
	u64 LkRxStatsRdErr;
};

int nfp_nbi_mac_stats_read_port(struct nfp_nbi_dev *nbi, int port,
				struct nfp_nbi_mac_portstats *stats);

int nfp_nbi_mac_stats_read_chan(struct nfp_nbi_dev *nbi, int chan,
				struct nfp_nbi_mac_chanstats *stats);

int nfp_nbi_mac_stats_read_ilks(struct nfp_nbi_dev *nbi, int core,
				struct nfp_nbi_mac_ilkstats *stats);

/* Implemented in nfp_nbi_mac_eth.c */

#define NFP_NBI_MAC_DQDWRR_TO 1000	/* wait for dwrr register access */

#define NFP_NBI_MAC_CHAN_MAX            127
#define NFP_NBI_MAC_CHAN_PAUSE_WM_MAX  2047
#define NFP_NBI_MAC_PORT_HWM_MAX       2047
#define NFP_NBI_MAC_PORT_HWM_DELTA_MAX   31

#define NFP_NBI_MAC_ENET_OFF		0
#define NFP_NBI_MAC_ILK			1
#define NFP_NBI_MAC_ENET_10M		2
#define NFP_NBI_MAC_ENET_100M		3
#define NFP_NBI_MAC_ENET_1G		4
#define NFP_NBI_MAC_ENET_10G		5
#define NFP_NBI_MAC_ENET_40G		6
#define NFP_NBI_MAC_ENET_100G		7

#define NFP_NBI_MAC_SINGLE_LANE(l) ((l == NFP_NBI_MAC_ENET_10M)  || \
				(l == NFP_NBI_MAC_ENET_100M) || \
				(l == NFP_NBI_MAC_ENET_1G)   || \
				(l == NFP_NBI_MAC_ENET_10G))

#define NFP_NBI_MAC_ONEG_MODE(l) ((l == NFP_NBI_MAC_ENET_10M)  || \
			      (l == NFP_NBI_MAC_ENET_100M) || \
			      (l == NFP_NBI_MAC_ENET_1G))

int nfp_nbi_mac_acquire(struct nfp_nbi_dev *nbi);
int nfp_nbi_mac_release(struct nfp_nbi_dev *nbi);

int nfp_nbi_mac_eth_ifdown(struct nfp_nbi_dev *nbi, int core, int port);
int nfp_nbi_mac_eth_ifup(struct nfp_nbi_dev *nbi, int core, int port);
int nfp_nbi_mac_eth_read_linkstate(struct nfp_nbi_dev *nbi,
				   int core, int port, u32 *linkstate);
int nfp_nbi_mac_eth_write_mac_addr(struct nfp_nbi_dev *nbi,
				   int core, int port, u64 hwaddr);
int nfp_nbi_mac_eth_read_mac_addr(struct nfp_nbi_dev *nbi, int core,
				  int port, u64 *waddr);
int nfp_nbi_mac_eth_read_mode(struct nfp_nbi_dev *nbi, int core, int port);

#endif
