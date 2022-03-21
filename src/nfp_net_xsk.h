/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc */
/* Copyright (C) 2021 Corigine, Inc */

#ifndef _NFP_XSK_H_
#define _NFP_XSK_H_

static inline int nfp_net_rx_space(struct nfp_net_rx_ring *rx_ring)
{
	return rx_ring->cnt - rx_ring->wr_p + rx_ring->rd_p - 1;
}

static inline int nfp_net_tx_space(struct nfp_net_tx_ring *tx_ring)
{
	return tx_ring->cnt - tx_ring->wr_p + tx_ring->rd_p - 1;
}

#ifdef COMPAT__HAVE_XDP_SOCK_DRV
#include <net/xdp_sock_drv.h>

#define NFP_NET_XSK_TX_BATCH 16		/* XSK TX transmission batch size. */

static inline bool nfp_net_has_xsk_pool_slow(struct nfp_net_dp *dp,
					     unsigned int qid)
{
	return dp->xdp_prog && dp->xsk_pools[qid];
}

void nfp_net_xsk_rx_unstash(struct nfp_net_xsk_rx_buf *rxbuf);
void nfp_net_xsk_rx_free(struct nfp_net_xsk_rx_buf *rxbuf);
void nfp_net_xsk_rx_drop(struct nfp_net_r_vector *r_vec,
			 struct nfp_net_xsk_rx_buf *xrxbuf);
int nfp_net_xsk_setup_pool(struct net_device *netdev, struct xsk_buff_pool *pool,
			   u16 queue_id);

void nfp_net_xsk_rx_bufs_free(struct nfp_net_rx_ring *rx_ring);

void nfp_net_xsk_rx_ring_fill_freelist(struct nfp_net_rx_ring *rx_ring);

int nfp_net_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags);

#else /* !COMPAT__HAVE_XDP_SOCK_DRV */

static inline bool nfp_net_has_xsk_pool_slow(struct nfp_net_dp *dp,
					     unsigned int qid)
{
	return false;
}

static inline
void nfp_net_xsk_rx_unstash(struct nfp_net_xsk_rx_buf *rxbuf) {}
static inline
void nfp_net_xsk_rx_free(struct nfp_net_xsk_rx_buf *rxbuf) {}
static inline
void nfp_net_xsk_rx_drop(struct nfp_net_r_vector *r_vec,
			 struct nfp_net_xsk_rx_buf *xrxbuf)
{
}

static inline
int nfp_net_xsk_setup_pool(struct net_device *netdev, struct xsk_buff_pool *pool,
			   u16 queue_id)
{
	return 0;
}

static inline
void nfp_net_xsk_rx_bufs_free(struct nfp_net_rx_ring *rx_ring) {}

static inline
void nfp_net_xsk_rx_ring_fill_freelist(struct nfp_net_rx_ring *rx_ring)
{
}

static inline
int nfp_net_xsk_wakeup(struct net_device *netdev, u32 queue_id, u32 flags)
{
	return 0;
}
#endif /* COMPAT__HAVE_XDP_SOCK_DRV */

#endif /* _NFP_XSK_H_ */
