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
 * nfp_pl.h
 */

#ifndef NFP3200_NFP_PL_H
#define NFP3200_NFP_PL_H

/* HGID: nfp3200/pl.desc = 03600216d5c8 */
/* Register Type: PluResetsAndEnables */
#define NFP_PL_RE                      0x0008
#define   NFP_PL_RE_ARM_GASKET_RESET                    (0x1 << 31)
#define   NFP_PL_RE_ARM_GASKET_ENABLE                   (0x1 << 30)
#define   NFP_PL_RE_MECL_ME_RESET(_x)                   (((_x) & 0x1f) << 25)
#define   NFP_PL_RE_MECL_ME_RESET_of(_x)                (((_x) >> 25) & 0x1f)
#define   NFP_PL_RE_MECL_ME_ENABLE(_x)                  (((_x) & 0x1f) << 20)
#define   NFP_PL_RE_MECL_ME_ENABLE_of(_x)               (((_x) >> 20) & 0x1f)
#define   NFP_PL_RE_PCIE_ENABLE                         (0x1 << 19)
#define   NFP_PL_RE_ARM_CORE_ENABLE                     (0x1 << 18)
#define   NFP_PL_RE_PCIE_RESET                          (0x1 << 17)
#define   NFP_PL_RE_ARM_CORE_RESET                      (0x1 << 16)
#define   NFP_PL_RE_MSF0_RESET                          (0x1 << 15)
#define   NFP_PL_RE_MSF1_RESET                          (0x1 << 14)
#define   NFP_PL_RE_CRYPTO_RESET                        (0x1 << 13)
#define   NFP_PL_RE_QDR0_RESET                          (0x1 << 12)
#define   NFP_PL_RE_QDR1_RESET                          (0x1 << 11)
#define   NFP_PL_RE_DDR0_RESET                          (0x1 << 10)
#define   NFP_PL_RE_DDR1_RESET                          (0x1 << 9)
#define   NFP_PL_RE_MU_RESET                            (0x1 << 8)
#define   NFP_PL_RE_MSF0_ENABLE                         (0x1 << 7)
#define   NFP_PL_RE_MSF1_ENABLE                         (0x1 << 6)
#define   NFP_PL_RE_CRYPTO_ENABLE                       (0x1 << 5)
#define   NFP_PL_RE_QDR0_ENABLE                         (0x1 << 4)
#define   NFP_PL_RE_QDR1_ENABLE                         (0x1 << 3)
#define   NFP_PL_RE_DDR0_ENABLE                         (0x1 << 2)
#define   NFP_PL_RE_DDR1_ENABLE                         (0x1 << 1)
#define   NFP_PL_RE_MU_ENABLE                           (0x1)
/* Register Type: PluConfig0 */
#define NFP_PL_CFG0                    0x000c
#define   NFP_PL_CFG0_DIV_STG1_of(_x)                   (((_x) >> 20) & 0x1f)
#define   NFP_PL_CFG0_DIV_STG2_of(_x)                   (((_x) >> 15) & 0x1f)
#define   NFP_PL_CFG0_MUL_PLL0_of(_x)                   (((_x) >> 10) & 0x1f)
#define   NFP_PL_CFG0_DIV_ME_of(_x)                     (((_x) >> 5) & 0x1f)
#define   NFP_PL_CFG0_DIV_SRAM(_x)                      ((_x) & 0x1f)
#define   NFP_PL_CFG0_DIV_SRAM_of(_x)                   ((_x) & 0x1f)
/* Register Type: PluFuses */
#define NFP_PL_FUSE                    0x0010
#define   NFP_PL_FUSE_MSF1_ENABLE                       (0x1 << 9)
#define   NFP_PL_FUSE_CRYPTO_ENABLE                     (0x1 << 8)
#define   NFP_PL_FUSE_QDR0_ENABLE                       (0x1 << 7)
#define   NFP_PL_FUSE_QDR1_ENABLE                       (0x1 << 6)
#define   NFP_PL_FUSE_DDR1_ENABLE                       (0x1 << 5)
#define   NFP_PL_FUSE_MECL_ME_ENABLE_of(_x)             (((_x) >> 2) & 0x7)
#define     NFP_PL_FUSE_MECL_ME_ENABLE_MES_8            (0)
#define     NFP_PL_FUSE_MECL_ME_ENABLE_MES_16           (1)
#define     NFP_PL_FUSE_MECL_ME_ENABLE_MES_24           (2)
#define     NFP_PL_FUSE_MECL_ME_ENABLE_MES_32           (3)
#define     NFP_PL_FUSE_MECL_ME_ENABLE_MES_40           (4)
#define   NFP_PL_FUSE_SPEED_of(_x)                      ((_x) & 0x3)
#define     NFP_PL_FUSE_SPEED_SLOW                      (0)
#define     NFP_PL_FUSE_SPEED_MEDIUM                    (1)
#define     NFP_PL_FUSE_SPEED_FAST                      (2)
#define     NFP_PL_FUSE_SPEED_UNLIMITED                 (3)
/* Register Type: PluConfig1 */
#define NFP_PL_CFG1                    0x0014
#define   NFP_PL_CFG1_DIV_SPICLK0(_x)                   (((_x) & 0x1f) << 25)
#define   NFP_PL_CFG1_DIV_SPICLK0_of(_x)                (((_x) >> 25) & 0x1f)
#define   NFP_PL_CFG1_DIV_SPICLK1(_x)                   (((_x) & 0x1f) << 20)
#define   NFP_PL_CFG1_DIV_SPICLK1_of(_x)                (((_x) >> 20) & 0x1f)
#define   NFP_PL_CFG1_DIV_CPP(_x)                       (((_x) & 0x1f) << 15)
#define   NFP_PL_CFG1_DIV_CPP_of(_x)                    (((_x) >> 15) & 0x1f)
#define   NFP_PL_CFG1_DIV_DCLK(_x)                      (((_x) & 0x1f) << 10)
#define   NFP_PL_CFG1_DIV_DCLK_of(_x)                   (((_x) >> 10) & 0x1f)
#define   NFP_PL_CFG1_DIV_XCLK0(_x)                     (((_x) & 0x1f) << 5)
#define   NFP_PL_CFG1_DIV_XCLK0_of(_x)                  (((_x) >> 5) & 0x1f)
#define   NFP_PL_CFG1_DIV_XCLK1(_x)                     ((_x) & 0x1f)
#define   NFP_PL_CFG1_DIV_XCLK1_of(_x)                  ((_x) & 0x1f)
/* Register Type: PluStraps */
#define NFP_PL_STRAPS                  0x0018
#define   NFP_PL_STRAPS_CFG_RST_DIR(_x)                 (((_x) & 0x3) << 7)
#define   NFP_PL_STRAPS_CFG_RST_DIR_of(_x)              (((_x) >> 7) & 0x3)
#define     NFP_PL_STRAPS_CFG_RST_DIR_INPUT_RESET_CHIP  (0)
#define     NFP_PL_STRAPS_CFG_RST_DIR_INPUT_RESET_PCIE  (1)
#define     NFP_PL_STRAPS_CFG_RST_DIR_OUTPUT            (2)
#define     NFP_PL_STRAPS_CFG_RST_DIR_INPUT_OUTPUT_RESET_PCIE (3)
#define   NFP_PL_STRAPS_CFG_PCI_ROOT                    (0x1 << 6)
#define     NFP_PL_STRAPS_CFG_PCI_ROOT_EP               (0x0)
#define     NFP_PL_STRAPS_CFG_PCI_ROOT_RC               (0x40)
#define   NFP_PL_STRAPS_CFG_PROM_BOOT                   (0x1 << 5)
#define     NFP_PL_STRAPS_CFG_PROM_BOOT_XXX_1           (0x0)
#define     NFP_PL_STRAPS_CFG_PROM_BOOT_XXX_2           (0x20)
#define   NFP_PL_STRAPS_PLL0_MULIPLIER(_x)              ((_x) & 0x1f)
#define   NFP_PL_STRAPS_PLL0_MULIPLIER_of(_x)           ((_x) & 0x1f)
/* Register Type: PluPassword */
#define NFP_PL_PASSWORD1               0x001c
#define NFP_PL_PASSWORD2               0x0020
/* Register Type: PluShacAndMEResetsAndEnables */
#define NFP_PL_RE2                     0x0024
#define   NFP_PL_RE2_PCIE_RESET                         (0x1 << 12)
#define   NFP_PL_RE2_SHAC_RESET                         (0x1 << 11)
#define   NFP_PL_RE2_SHAC_ENABLE                        (0x1 << 10)
#define   NFP_PL_RE2_MECL_CTL_RESET(_x)                 (((_x) & 0x1f) << 5)
#define   NFP_PL_RE2_MECL_CTL_RESET_of(_x)              (((_x) >> 5) & 0x1f)
#define   NFP_PL_RE2_MECL_CTL_ENABLE(_x)                ((_x) & 0x1f)
#define   NFP_PL_RE2_MECL_CTL_ENABLE_of(_x)             ((_x) & 0x1f)
/* Register Type: PluConfig2 */
#define NFP_PL_CFG2                    0x0028
#define   NFP_PL_CFG2_SOFT_RESET                        (0x1 << 15)
#define   NFP_PL_CFG2_DIV_OCLK(_x)                      (((_x) & 0x1f) << 10)
#define   NFP_PL_CFG2_DIV_OCLK_of(_x)                   (((_x) >> 10) & 0x1f)
#define   NFP_PL_CFG2_MUL_PLL1(_x)                      (((_x) & 0x1f) << 5)
#define   NFP_PL_CFG2_MUL_PLL1_of(_x)                   (((_x) >> 5) & 0x1f)
#define   NFP_PL_CFG2_MUL_PLL2(_x)                      ((_x) & 0x1f)
#define   NFP_PL_CFG2_MUL_PLL2_of(_x)                   ((_x) & 0x1f)
/* Register Type: PluPLLTuning */
#define NFP_PL_PLL0_TUNING             0x0030
#define NFP_PL_PLL1_TUNING             0x0034
#define NFP_PL_PLL2_TUNING             0x0038
/* Register Type: PLLEnsat */
#define NFP_PL_PLL0_ENSAT              0x003c
#define NFP_PL_PLL1_ENSAT              0x0040
#define NFP_PL_PLL2_ENSAT              0x0044
/* Register Type: PluBisr */
#define NFP_PL_BISR                    0x0048
/* Register Type: PluJTagIdCode */
#define NFP_PL_JTAG_ID_CODE            0x004c
#define   NFP_PL_JTAG_ID_CODE_REV_ID_of(_x)             (((_x) >> 28) & 0xf)
#define   NFP_PL_JTAG_ID_CODE_PART_NUM_of(_x)           (((_x) >> 12) & 0xffff)
#define   NFP_PL_JTAG_ID_CODE_MFR_ID_of(_x)             (((_x) >> 1) & 0x7ff)

#define NFP_XPB_PL			NFP_XPB_DEST(31, 48)
#define NFP_PL_SIZE			SZ_4K

#endif /* NFP3200_NFP_PL_H */
