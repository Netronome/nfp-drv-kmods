/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

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
