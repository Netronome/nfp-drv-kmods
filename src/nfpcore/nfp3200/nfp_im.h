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
 * nfp_im.h
 */

#ifndef NFP3200_NFP_IM_H
#define NFP3200_NFP_IM_H

/* HGID: nfp3200/intmgr.desc = 5702fafbfb92 */
/* Register Type: IMgrStatus */
#define NFP_IM_PIN_STATUS              0x0000
/* Register Type: IMgrEnable */
#define NFP_IM_PIN_DISABLE             0x0004
#define NFP_IM_PIN_ENABLE_LOW          0x000c
#define NFP_IM_PIN_ENABLE_MED          0x0014
#define NFP_IM_PIN_ENABLE_HIGH         0x001c
/* Register Type: IMgrEnabledStatus */
#define NFP_IM_PIN_STATUS_LOW          0x0008
#define NFP_IM_INTR_STS_MI             0x0010
#define NFP_IM_PIN_STATUS_HIGH         0x0018
/* Register Type: IMgrConfig */
#define NFP_IM_MODULE_CONFIG           0x0020
/* Register Type: IMgrStatusEventConfig */
#define NFP_IM_EVENT_CONFIG_0          0x0028
#define NFP_IM_EVENT_CONFIG_1          0x002c
#define   NFP_IM_EVENT_CONFIG_EDGE_15(_x)               (((_x) & 0x3) << 30)
#define   NFP_IM_EVENT_CONFIG_EDGE_15_of(_x)            (((_x) >> 30) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_14(_x)               (((_x) & 0x3) << 28)
#define   NFP_IM_EVENT_CONFIG_EDGE_14_of(_x)            (((_x) >> 28) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_13(_x)               (((_x) & 0x3) << 26)
#define   NFP_IM_EVENT_CONFIG_EDGE_13_of(_x)            (((_x) >> 26) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_12(_x)               (((_x) & 0x3) << 24)
#define   NFP_IM_EVENT_CONFIG_EDGE_12_of(_x)            (((_x) >> 24) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_11(_x)               (((_x) & 0x3) << 22)
#define   NFP_IM_EVENT_CONFIG_EDGE_11_of(_x)            (((_x) >> 22) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_10(_x)               (((_x) & 0x3) << 20)
#define   NFP_IM_EVENT_CONFIG_EDGE_10_of(_x)            (((_x) >> 20) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_9(_x)                (((_x) & 0x3) << 18)
#define   NFP_IM_EVENT_CONFIG_EDGE_9_of(_x)             (((_x) >> 18) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_8(_x)                (((_x) & 0x3) << 16)
#define   NFP_IM_EVENT_CONFIG_EDGE_8_of(_x)             (((_x) >> 16) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_7(_x)                (((_x) & 0x3) << 14)
#define   NFP_IM_EVENT_CONFIG_EDGE_7_of(_x)             (((_x) >> 14) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_6(_x)                (((_x) & 0x3) << 12)
#define   NFP_IM_EVENT_CONFIG_EDGE_6_of(_x)             (((_x) >> 12) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_5(_x)                (((_x) & 0x3) << 10)
#define   NFP_IM_EVENT_CONFIG_EDGE_5_of(_x)             (((_x) >> 10) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_4(_x)                (((_x) & 0x3) << 8)
#define   NFP_IM_EVENT_CONFIG_EDGE_4_of(_x)             (((_x) >> 8) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_3(_x)                (((_x) & 0x3) << 6)
#define   NFP_IM_EVENT_CONFIG_EDGE_3_of(_x)             (((_x) >> 6) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_2(_x)                (((_x) & 0x3) << 4)
#define   NFP_IM_EVENT_CONFIG_EDGE_2_of(_x)             (((_x) >> 4) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_1(_x)                (((_x) & 0x3) << 2)
#define   NFP_IM_EVENT_CONFIG_EDGE_1_of(_x)             (((_x) >> 2) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_0(_x)                ((_x) & 0x3)
#define   NFP_IM_EVENT_CONFIG_EDGE_0_of(_x)             ((_x) & 0x3)
/* Register Type: IMgrEventOut */
#define NFP_IM_EVENT_TEST              0x0030
#define   NFP_IM_EVENT_TEST_SOURCE(_x)                  (((_x) & 0xfff) << 4)
#define   NFP_IM_EVENT_TEST_TYPE(_x)                    ((_x) & 0xf)
/* Register Type: IMgrPerformanceAnalyzerControl */
#define NFP_IM_PERF_CONTROL            0x0034
/* Register Type: IMgrCaptureTimerStatus */
#define NFP_IM_CAP_TIMER_STATUS        0x0038
/* Register Type: IMgrCaptureTimerValue */
#define NFP_IM_CAP_TIMER_VALUE         0x003c
#define   NFP_IM_CAP_TIMER_VALUE_VALID                  (0x1 << 31)
#define   NFP_IM_CAP_TIMER_VALUE_VALUE_of(_x)           ((_x) & 0x7fffffff)
/* Register Type: IMgrStatus */
#define NFP_IMX8_PIN_STATUS            0x0000
/* Register Type: IMgrEnable */
#define NFP_IMX8_PIN_DISABLE           0x0008
#define NFP_IMX8_PIN_ENABLE_LOW        0x0018
#define NFP_IMX8_PIN_ENABLE_MED        0x0028
#define NFP_IMX8_PIN_ENABLE_HIGH       0x0038
/* Register Type: IMgrEnabledStatus */
#define NFP_IMX8_PIN_STATUS_LOW        0x0010
#define NFP_IMX8_INTR_STS_MI           0x0020
#define NFP_IMX8_PIN_STATUS_HIGH       0x0030
/* Register Type: IMgrConfig */
#define NFP_IMX8_MODULE_CONFIG         0x0040
/* Register Type: IMgrStatusEventConfig */
#define NFP_IMX8_EVENT_CONFIG_0        0x0050
#define NFP_IMX8_EVENT_CONFIG_1        0x0058
/* Register Type: IMgrEventOut */
#define NFP_IMX8_EVENT_TEST            0x0060
/* Register Type: IMgrPerformanceAnalyzerControl */
#define NFP_IMX8_PERF_CONTROL          0x0068
/* Register Type: IMgrCaptureTimerStatus */
#define NFP_IMX8_CAP_TIMER_STATUS      0x0070
/* Register Type: IMgrCaptureTimerValue */
#define NFP_IMX8_CAP_TIMER_VALUE       0x0078

#define NFP_IM_EVENT_CONFIG_N(pin) \
	((pin) < 16 ? NFP_IM_EVENT_CONFIG_0 : NFP_IM_EVENT_CONFIG_1)
#define NFP_IM_EVENT_EDGE_IGNORED	0
#define NFP_IM_EVENT_EDGE_NEGATIVE	1
#define NFP_IM_EVENT_EDGE_POSITIVE	2
#define NFP_IM_EVENT_EDGE_ANY		3
#define NFP_IM_EVENT_CONFIG(pin, edge)	((edge) << (((pin) & 0xf) * 2))
#define NFP_IM_EVENT_CONFIG_MASK(pin)	(3 << (((pin) & 0xf) * 2))
#define NFP_IM_PIN_MASK(pin)		(1 << ((pin) & 0x1f))

#endif /* NFP3200_NFP_IM_H */
