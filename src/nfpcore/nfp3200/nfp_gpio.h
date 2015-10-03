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
 * nfp_gpio.h
 * GPIO field definitions
 */

#ifndef NFP3200_NFP_GPIO_H
#define NFP3200_NFP_GPIO_H

/* HGID: nfp3200/gpio.desc = 9aa6ddb03994 */
/* Register Type: GpioLevels */
#define NFP_GPIO_PLR                   0x0000
#define   NFP_GPIO_PLR_MASK_of(_x)                      ((_x) & 0xfff)
/* Register Type: GpioPinDirection */
#define NFP_GPIO_PDPR                  0x0004
#define   NFP_GPIO_PDPR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_PDPR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioPinMakeOutputs */
#define NFP_GPIO_PDSR                  0x0008
#define   NFP_GPIO_PDSR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioPinMakeInputs */
#define NFP_GPIO_PDCR                  0x000c
#define   NFP_GPIO_PDCR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioSetOutputValues */
#define NFP_GPIO_POPR                  0x0010
#define   NFP_GPIO_POPR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_POPR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioSetOutputsHigh */
#define NFP_GPIO_POSR                  0x0014
#define   NFP_GPIO_POSR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioSetOutputsLow */
#define NFP_GPIO_POCR                  0x0018
#define   NFP_GPIO_POCR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioEdgeDetectRising */
#define NFP_GPIO_REDR                  0x001c
#define   NFP_GPIO_REDR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_REDR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioEdgeDetectFalling */
#define NFP_GPIO_FEDR                  0x0020
#define   NFP_GPIO_FEDR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_FEDR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioEdgeDetectStatus */
#define NFP_GPIO_EDSR                  0x0024
#define   NFP_GPIO_EDSR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_EDSR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioLevelSensitiveHigh */
#define NFP_GPIO_LSHR                  0x0028
#define   NFP_GPIO_LSHR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_LSHR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioLevelSensitiveLow */
#define NFP_GPIO_LSLR                  0x002c
#define   NFP_GPIO_LSLR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_LSLR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioLevelSensitiveStatus */
#define NFP_GPIO_LDSR                  0x0030
#define   NFP_GPIO_LDSR_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_LDSR_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioInterruptEnables */
#define NFP_GPIO_INER                  0x0034
#define   NFP_GPIO_INER_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_INER_MASK_of(_x)                     ((_x) & 0xfff)
/* Register Type: GpioInterruptEnableSet */
#define NFP_GPIO_INSR                  0x0038
#define   NFP_GPIO_INSR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioInterruptEnableReset */
#define NFP_GPIO_INCR                  0x003c
#define   NFP_GPIO_INCR_MASK(_x)                        ((_x) & 0xfff)
/* Register Type: GpioInterruptStatus */
#define NFP_GPIO_INST                  0x0040
#define   NFP_GPIO_INST_MASK(_x)                        ((_x) & 0xfff)
#define   NFP_GPIO_INST_MASK_of(_x)                     ((_x) & 0xfff)

#define NFP_XPB_GPIO	NFP_XPB_DEST(31, 1)
#define NFP_GPIO_SIZE	SZ_4K

#endif /* NFP3200_NFP_GPIO_H */
