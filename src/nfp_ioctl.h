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
 * nfp_ioctl.h
 * ioctl definitions for the /dev/nfp-cpp-N devices
 * Author: Jason McMullan <jason.mcmullan@netronome.com>
 */
#ifndef NFP_IOCTL_H
#define NFP_IOCTL_H

/* NFP IOCTLs */
#define NFP_IOCTL	'n'

struct nfp_cpp_area_request {
	unsigned long   offset;		/* Offset for use by mmap() */
	unsigned int	cpp_id;		/* NFP_CPP_ID */
	unsigned long long cpp_addr;	/* Offset into area */
	unsigned long	size;		/* Requested area size */
};

/*
 * Request a NFP area, for later mapping via mmap()
 *
 * cmd is NFP_IOCTL_CPP_AREA_REQUEST,
 * Returns 0 on success and fills in 'offset', and an error on failure.
 */
#define NFP_IOCTL_CPP_AREA_REQUEST \
	_IOWR(NFP_IOCTL, 0x80, struct nfp_cpp_area_request)

/*
 * Release a NFP area, acquired earlier
 *
 * cmd is NFP_IOCTL_CPP_AREA_RELEASE, arg previously opened area
 * Match is done by all fields of the request, including offset
 */
#define NFP_IOCTL_CPP_AREA_RELEASE \
	_IOW(NFP_IOCTL, 0x81, struct nfp_cpp_area_request)

/*
 * Release a NFP area, acquired earlier (OBSOLETE API)
 *
 * cmd is NFP_IOCTL_CPP_AREA_RELEASE, arg offset from previous
 * call to NFP_IOCTL_CPP_AREA_REQUEST
 * Match is done by offset
 */
#define NFP_IOCTL_CPP_AREA_RELEASE_OBSOLETE \
	_IOW(NFP_IOCTL, 0x81, unsigned long)

#define NFP_IOCTL_CPP_EXPL_POST		0
#define NFP_IOCTL_CPP_EXPL1_BAR		1
#define NFP_IOCTL_CPP_EXPL2_BAR		2

/* When computing the CSR values, keep in mind:
 *  data_ref:      Only bit 2 is preserved
 *  signal_ref:    Only bit 0 is preserved
 */
struct nfp_cpp_explicit_request {
	unsigned long   csr[3];		/* CSR values */
	int		in, out;	/* in and out data length (# u32) */
	u32		data[32];	/* Data in/out */
	u64		address;	/* CPP address */
};

/*
 * Perform a NFP explicit transaction
 *
 * cmd is NFP_IOCTL_CPP_EXPL_REQUEST,
 * Returns 0 on success, and an error on failure.
 */
#define NFP_IOCTL_CPP_EXPL_REQUEST \
	_IOW(NFP_IOCTL, 0x82, struct nfp_cpp_explicit_request)

/* This struct should only use u32 types where possible,
 * to reduce problems with packing differences on compilers.
 */
struct nfp_cpp_identification {
	u32	size;		/* Size of this structure, in bytes */
	u32	model;		/* NFP CPP model ID */
	u32	interface;	/* NFP CPP interface ID */
	u32	serial_lo;	/* Lower 32 of 48 bit serial number */
	u32	serial_hi;	/* Upper 16 of 48 bit serial number */
};

/**
 * Define a NFP event request
 */
struct nfp_cpp_event_request {
	int signal;
	int type;
	unsigned int match;
	unsigned int mask;
};

/**
 * Request a NFP event to be bound to a signal
 */
#define NFP_IOCTL_CPP_EVENT_ACQUIRE \
	_IOW(NFP_IOCTL, 0x83, struct nfp_cpp_event_request)

/**
 * Release a NFP event that was bound to a signal
 */
#define NFP_IOCTL_CPP_EVENT_RELEASE \
	_IOW(NFP_IOCTL, 0x84, struct nfp_cpp_event_request)

#define NFP_FIRMWARE_MAX  256           /* Maximum length of a firmware name */

/**
 * Request a firmware load
 */
#define NFP_IOCTL_FIRMWARE_LOAD \
	_IOW(NFP_IOCTL, 0x8d, char[NFP_FIRMWARE_MAX])

/**
 * Get the name of the last firmware load attempt
 */
#define NFP_IOCTL_FIRMWARE_LAST \
	_IOR(NFP_IOCTL, 0x8e, char[NFP_FIRMWARE_MAX])

/**
 * Request a NFP Identification structure
 *
 * cmd is NFP_IOCTL_CPP_IDENTIFICATION
 * The 'size' member of the passed-in struct nfp_cpp_identification
 * *must* be filled. Only members of the identification structure
 * up to 'size' will be filled in.
 *
 * Returns the total length supported on success, and an error on failure.
 */
#define NFP_IOCTL_CPP_IDENTIFICATION \
	_IOW(NFP_IOCTL, 0x8f, u32)

#endif /* NFP_IOCTL_H */
