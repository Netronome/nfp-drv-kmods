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
 * nfp_em.h
 * Event-Manager definitions
 */

#ifndef NFP3200_NFP_EM_H
#define NFP3200_NFP_EM_H

/* HGID: nfp3200/evntmgr.desc = eaa9b989b828 */
#define NFP_EM_FILTER(_f)              (0x0 + (0x20 * ((_f) & 0x1f)))
/* Register Type: EventFilterStatus */
#define NFP_EM_FILTER_STATUS           0x0000
#define NFP_EM_FILTER_ACK              0x0010
/* Register Type: EventFilterFlag */
#define NFP_EM_FILTER_FLAGS            0x0004
#define   NFP_EM_FILTER_FLAGS_PENDING_STATUS            (0x1 << 1)
#define   NFP_EM_FILTER_FLAGS_STATUS                    (0x1)
/* Register Type: EventFilterMask */
#define NFP_EM_FILTER_MASK             0x0008
#define   NFP_EM_FILTER_MASK_TYPE(_x)                   (((_x) & 0x7) << 24)
#define   NFP_EM_FILTER_MASK_TYPE_of(_x)                (((_x) >> 24) & 0x7)
#define     NFP_EM_FILTER_MASK_TYPE_COUNT32             (0)
#define     NFP_EM_FILTER_MASK_TYPE_COUNT16             (1)
#define     NFP_EM_FILTER_MASK_TYPE_MASK32              (2)
#define     NFP_EM_FILTER_MASK_TYPE_MASK16              (3)
#define     NFP_EM_FILTER_MASK_TYPE_FIRSTEV             (4)
#define     NFP_EM_FILTER_MASK_TYPE_LASTEV              (5)
#define   NFP_EM_FILTER_MASK_EVENT(_x)                  ((_x) & 0xfffff)
#define   NFP_EM_FILTER_MASK_EVENT_of(_x)               ((_x) & 0xfffff)
/* Register Type: EventFilterMatch */
#define NFP_EM_FILTER_MATCH            0x000c
#define   NFP_EM_FILTER_MATCH_EVENT(_x)                 ((_x) & 0xfffff)
#define   NFP_EM_FILTER_MATCH_EVENT_of(_x)              ((_x) & 0xfffff)
/* Register Type: EventCombinedStatus */
#define NFP_EM_ALL_STATUS              0x0400
/* Register Type: EventCombinedPendingStatus */
#define NFP_EM_ALL_PENDING             0x0404
/* Register Type: EventConfig */
#define NFP_EM_CONFIG                  0x0408
#define NFP_EMX8_FILTER(_f)            (0x0 + (0x40 * ((_f) & 0x1f)))
/* Register Type: EventFilterStatus */
#define NFP_EMX8_FILTER_STATUS         0x0000
#define NFP_EMX8_FILTER_ACK            0x0020
/* Register Type: EventFilterFlag */
#define NFP_EMX8_FILTER_FLAGS          0x0008
/* Register Type: EventFilterMask */
#define NFP_EMX8_FILTER_MASK           0x0010
/* Register Type: EventFilterMatch */
#define NFP_EMX8_FILTER_MATCH          0x0018
/* Register Type: EventCombinedStatus */
#define NFP_EMX8_ALL_STATUS            0x0800
/* Register Type: EventCombinedPendingStatus */
#define NFP_EMX8_ALL_PENDING           0x0808
/* Register Type: EventConfig */
#define NFP_EMX8_CONFIG                0x0810
/* Register Type: EventFilterStatusCount32 */
#define NFP_EVENT_TYPE_COUNT32         0x0000
#define   NFP_EVENT_COUNT32_CNT32_of(_x)                (_x)
/* Register Type: EventFilterStatusCount16 */
#define NFP_EVENT_TYPE_COUNT16         0x0001
#define   NFP_EVENT_COUNT16_TMOUT(_x)                   (((_x) & 0x7) << 29)
#define   NFP_EVENT_COUNT16_TMOUT_of(_x)                (((_x) >> 29) & 0x7)
#define   NFP_EVENT_COUNT16_UPCNT_of(_x)                (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_COUNT16_OVERRIDE(_x)                (((_x) & 0x3f) << 16)
#define   NFP_EVENT_COUNT16_OVERRIDE_of(_x)             (((_x) >> 16) & 0x3f)
#define   NFP_EVENT_COUNT16_CNT16_of(_x)                ((_x) & 0xffff)
/* Register Type: EventFilterStatusBitmask32 */
#define NFP_EVENT_TYPE_MASK32          0x0002
/* Register Type: EventFilterStatusBitmask16 */
#define NFP_EVENT_TYPE_MASK16          0x0003
#define   NFP_EVENT_MASK16_TMOUT(_x)                    (((_x) & 0x7) << 29)
#define   NFP_EVENT_MASK16_TMOUT_of(_x)                 (((_x) >> 29) & 0x7)
#define   NFP_EVENT_MASK16_UPCNT_of(_x)                 (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_MASK16_OVERRIDE(_x)                 (((_x) & 0x7) << 20)
#define   NFP_EVENT_MASK16_OVERRIDE_of(_x)              (((_x) >> 20) & 0x7)
#define   NFP_EVENT_MASK16_CNT4(_x)                     (((_x) & 0xf) << 16)
#define   NFP_EVENT_MASK16_CNT4_of(_x)                  (((_x) >> 16) & 0xf)
#define   NFP_EVENT_MASK16_MASK16_of(_x)                ((_x) & 0xffff)
/* Register Type: EventFilterStatusEvent */
#define NFP_EVENT_TYPE_FIRSTEV         0x0004
#define NFP_EVENT_TYPE_LASTEV          0x0005
#define   NFP_EVENT_EVENT_TMOUT(_x)                     (((_x) & 0x7) << 29)
#define   NFP_EVENT_EVENT_TMOUT_of(_x)                  (((_x) >> 29) & 0x7)
#define   NFP_EVENT_EVENT_UPCNT_of(_x)                  (((_x) >> 23) & 0x3f)
#define   NFP_EVENT_EVENT_CNT2(_x)                      (((_x) & 0x3) << 20)
#define   NFP_EVENT_EVENT_CNT2_of(_x)                   (((_x) >> 20) & 0x3)
#define   NFP_EVENT_EVENT_EVENT_of(_x)                  ((_x) & 0xfffff)

#define NFP_EM_SIZE		SZ_4K

#define NFP_EM_EVENT_TYPE_STATUS_FLAGS	4	/* Status flags event */

#define NFP_EM_EVENT_PIN_ID(pin)	(((((pin) >> 3) & 0x7) << 8) | \
					 (1 << ((pin) & 0x7)))
#define NFP_EM_EVENT_MATCH(source, pin_id, event_type)	(((source) << 16) | \
							 ((pin_id) << 4) | \
							 (event_type))
#define NFP_EM_EVENT_TYPE_of(ev)        ((ev) & 0xf)
#define NFP_EM_EVENT_SOURCE_of(ev)      (((ev) >> 4) & 0xfff)
#define NFP_EM_EVENT_PROVIDER_of(ev)    (((ev) >> 16) & 0xf)

#endif /* NFP3200_NFP_EM_H */
