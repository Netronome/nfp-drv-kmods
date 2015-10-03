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
 * nfp_modinfo.h
 * Common declarations for defining module build information.
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */

 #ifndef __KERNEL__NFP_MODINFO_H__
#define __KERNEL__NFP_MODINFO_H__

#include "nfp_build_info.h"	/* dynamically generated filed */

#define MODULE_INFO_NFP() \
	MODULE_INFO(nfp_src_version, NFP_SRC_VERSION); \
	MODULE_INFO(nfp_src_path, NFP_SRC_PATH); \
	MODULE_INFO(nfp_build_user_id, NFP_BUILD_USER_ID); \
	MODULE_INFO(nfp_build_user, NFP_BUILD_USER); \
	MODULE_INFO(nfp_build_host, NFP_BUILD_HOST); \
	MODULE_INFO(nfp_build_path, NFP_BUILD_PATH)

#define NFP_BUILD_DESCRIPTION(drvname)				\
	#drvname " src version: " NFP_SRC_VERSION "\n"		\
	#drvname " src path: " NFP_SRC_PATH "\n"		\
	#drvname " build user id: " NFP_BUILD_USER_ID "\n"	\
	#drvname " build user: " NFP_BUILD_USER "\n"		\
	#drvname " build host: " NFP_BUILD_HOST "\n"		\
	#drvname " build path: " NFP_BUILD_PATH "\n"

#endif	/* __KERNEL__NFP_MODINFO_H__ */
