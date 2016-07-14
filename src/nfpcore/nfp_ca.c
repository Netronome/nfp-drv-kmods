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
 * nfp_ca.h
 * Authors: Mike Aitken <mike.aitken@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 *          Francois H. Theron <francois.theron@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/zlib.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "nfp.h"
#include "nfp_cpp.h"

#include "crc32.h"

/* Define to 1 to dump CPP CA Replay statistics
 */
#define DEBUG_CA_CPP_STATS  0

/* up to 32 IDs, and up to 7 words of control information */
#define NFP_CA_(id)         ((id) << 3)
#define NFP_CA(id, type)    (NFP_CA_(id) | (sizeof(type) / sizeof(u32)))
#define NFP_CA_LEN(ca)      ((ca) & 0x7)
#define NFP_CA_SZ(ca)       (1 + NFP_CA_LEN(ca) * 4)

struct nfp_ca_start {
	u32 magic;
	u32 bytes;
};

#define NFP_CA_START_MAGIC  0x0066424e  /* "NBf\000" */
#define NFP_CA_ZSTART_MAGIC 0x007a424e  /* "NBz\000" - zlib compressed */

#define NFP_CA_START        NFP_CA(0, struct nfp_ca_start)
#define NFP_CA_END          NFP_CA(0, u32) /* u32 is CRC32 */

#define NFP_CA_CPP_ID       NFP_CA(1, u32)
#define NFP_CA_CPP_ADDR     NFP_CA(2, u64)
#define NFP_CA_READ_4       NFP_CA(3, u32)
#define NFP_CA_READ_8       NFP_CA(4, u64)
#define NFP_CA_WRITE_4      NFP_CA(5, u32)
#define NFP_CA_WRITE_8      NFP_CA(6, u64)
#define NFP_CA_INC_READ_4   NFP_CA(7, u32)
#define NFP_CA_INC_READ_8   NFP_CA(8, u64)
#define NFP_CA_INC_WRITE_4  NFP_CA(9, u32)
#define NFP_CA_INC_WRITE_8  NFP_CA(10, u64)
#define NFP_CA_ZERO_4       NFP_CA_(11)
#define NFP_CA_ZERO_8       NFP_CA_(12)
#define NFP_CA_INC_ZERO_4   NFP_CA_(13)
#define NFP_CA_INC_ZERO_8   NFP_CA_(14)
#define NFP_CA_READ_IGNV_4  NFP_CA(15, u32) /* Ignore read value */
#define NFP_CA_READ_IGNV_8  NFP_CA(16, u64)
#define NFP_CA_INC_READ_IGNV_4  NFP_CA(17, u32)
#define NFP_CA_INC_READ_IGNV_8  NFP_CA(18, u64)
#define NFP_CA_POLL_4           NFP_CA(19, u32)
#define NFP_CA_POLL_8           NFP_CA(20, u64)
#define NFP_CA_MASK_4           NFP_CA(21, u32)
#define NFP_CA_MASK_8           NFP_CA(22, u64)

static inline void cpu_to_ca32(u8 *byte, u32 val)
{
	int i;

	for (i = 0; i < 4; i++)
		byte[i] = (val >> (8 * i)) & 0xff;
}

static inline u32 ca32_to_cpu(const u8 *byte)
{
	int i;
	u32 val = 0;

	for (i = 0; i < 4; i++)
		val |= ((u32)byte[i]) << (8 * i);

	return val;
}

static inline u64 ca64_to_cpu(const u8 *byte)
{
	int i;
	u64 val = 0;

	for (i = 0; i < 8; i++)
		val |= ((u64)byte[i]) << (8 * i);

	return val;
}

enum nfp_ca_action {
	NFP_CA_ACTION_NONE          = 0,
	NFP_CA_ACTION_READ32        = 1,
	NFP_CA_ACTION_READ64        = 2,
	NFP_CA_ACTION_WRITE32       = 3,
	NFP_CA_ACTION_WRITE64       = 4,
	NFP_CA_ACTION_READ_IGNV32   = 5, /* Read and ignore value */
	NFP_CA_ACTION_READ_IGNV64   = 6,
	NFP_CA_ACTION_POLL32        = 7,
	NFP_CA_ACTION_POLL64        = 8
};

typedef int (*nfp_ca_callback)(void *priv, enum nfp_ca_action action,
			       u32 cpp_id, u64 cpp_addr,
			       u64 val, u64 mask);

/*
 * nfp_ca_null() - Null callback used for CRC calculation
 */
static int nfp_ca_null(void *priv, enum nfp_ca_action action,
		       u32 cpp_id, u64 cpp_addr,
		       u64 val, u64 mask)
{
	return 0;
}

#define CA_CPP_AREA_SIZE   (64ULL * 1024)

struct ca_cpp {
	struct nfp_cpp *cpp;

	u8 buff[128];
	size_t buff_size;
	u64 buff_offset;

#if DEBUG_CA_CPP_STATS
	struct {
		u32 actions;
	} stats;
#endif
};

static int ca6000_cpp_write_ustore(struct ca_cpp *ca, u32 id,
				   u64 addr, void *ptr, size_t len);

static int ca_cpp_write(struct ca_cpp *ca, u32 id, u64 addr,
			void *ptr, size_t len)
{
	if (NFP_CPP_MODEL_IS_6000(nfp_cpp_model(ca->cpp))) {
		if (id == NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 65, 0))
			return ca6000_cpp_write_ustore(ca, id, addr, ptr, len);
	}

	return nfp_cpp_write(ca->cpp, id, addr, ptr, len);
}

static int ca_cpp_read(struct ca_cpp *ca, u32 id, u64 addr,
		       void *ptr, size_t len)
{
	return nfp_cpp_read(ca->cpp, id, addr, ptr, len);
}

/** Translate a special microengine codestore write action into the necessary
 * sequence of writes and reads.
 *
 * A write with cppid = NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 65, 0) should be
 * handled with this function instead.
 *
 * The format of the address is:
 * addr<39:32> = island_id
 * addr<31:24> = me_num (0 based, within island)
 * addr<23:0> = byte codestore address
 */
static int ca6000_cpp_write_ustore(struct ca_cpp *ca, u32 id,
				   u64 addr, void *ptr, size_t len)
{
	int err = 0;
	u64 uw = *((u64 *)ptr);
	u32 uwlo = uw & 0xFFFFffff;
	u32 uwhi = (uw >> 32) & 0xFFFFffff;
	u32 iid = (addr >> 32) & 0x3F;
	u32 menum = (addr >> 24) & 0xF;
	u32 uaddr = ((addr >> 3) & 0xFFFF);
	int enable_cs =  ((uw >> 61) & 6) == 6;
	int disable_cs = ((uw >> 61) & 5) == 5;
	u32 csr_base = (iid << 24) | (1 << 16) | ((menum + 4) << 10);

	/* Clear top control bits */
	uw &= ~(7ULL << 61);

	if (enable_cs) {
		/* Set UstorAddr */
		uaddr |= (1 << 31);
		err = ca_cpp_write(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 3, 1),
				   (csr_base | 0x000), &uaddr, sizeof(uaddr));
		if (err != sizeof(uaddr))
			return err;
		err = ca_cpp_read(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 2, 1),
				  (csr_base | 0x000), &uaddr, sizeof(uaddr));
		if (err != sizeof(uaddr))
			return err;
	}

	err = ca_cpp_write(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 3, 1),
			   (csr_base | 0x004), &uwlo, sizeof(uwlo));
	if (err != sizeof(uwlo))
		return err;
	err = ca_cpp_read(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 2, 1),
			  (csr_base | 0x004), &uwlo, sizeof(uwlo));
	if (err != sizeof(uaddr))
		return err;

	err = ca_cpp_write(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 3, 1),
			   (csr_base | 0x008), &uwhi, sizeof(uwhi));
	if (err != sizeof(uwhi))
		return err;
	err = ca_cpp_read(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 2, 1),
			  (csr_base | 0x008), &uwhi, sizeof(uwhi));
	if (err != sizeof(uwhi))
		return err;

	if (disable_cs) {
		uaddr &= ~(1 << 31);
		err = ca_cpp_write(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 3, 1),
				   (csr_base | 0x000), &uaddr, sizeof(uaddr));
		if (err != sizeof(uaddr))
			return err;
		err = ca_cpp_read(ca, NFP_CPP_ID(NFP_CPP_TARGET_CT_XPB, 2, 1),
				  (csr_base | 0x000), &uaddr, sizeof(uaddr));
		if (err != sizeof(uaddr))
			return err;
	}

	return sizeof(uw);
}

static int nfp_ca_cb_cpp(void *priv, enum nfp_ca_action action,
			 u32 cpp_id, u64 cpp_addr, u64 val, u64 mask)
{
	struct ca_cpp *ca = priv;
	struct nfp_cpp *cpp = ca->cpp;
	u32 tmp32;
	u64 tmp64;
	static unsigned int cnt;
	int timeout = 100; /* 100 ms */
	int pcount = 0;
	int poll_action = 0;
	int bit_len = 0;
	int err;

#if DEBUG_CA_CPP_STATS
	ca->stats.actions++;
#endif
	cnt++;

	switch (action) {
	case NFP_CA_ACTION_POLL32:
	case NFP_CA_ACTION_POLL64:
		timeout = 2000; /* Allow 2 seconds for a poll before failing. */
		poll_action = 1;
		/* Fall through */

	case NFP_CA_ACTION_READ32:
	case NFP_CA_ACTION_READ64:
		do {
			if ((action == NFP_CA_ACTION_READ32) ||
			    (action == NFP_CA_ACTION_POLL32))
				bit_len = 32;
			else
				bit_len = 64;

			if (bit_len == 32) {
				err = ca_cpp_read(ca, cpp_id, cpp_addr,
						  &tmp32, sizeof(tmp32));
				tmp64 = tmp32;
			} else {
				err = ca_cpp_read(ca, cpp_id, cpp_addr,
						  &tmp64, sizeof(tmp64));
			}
			if (err < 0)
				break;

			if (val != (tmp64 & mask)) {
				/* 'about 1ms' - see
				 * Documentation/timers/timers-howto.txt
				 * for why it is poor practice to use
				 * msleep() for < 20ms sleeps.
				 */
				usleep_range(800, 1200);
				timeout--;
				pcount++;
			} else {
				break;
			}
		} while (timeout > 0);
		if (timeout == 0) {
			dev_warn(nfp_cpp_device(cpp),
				 "%sMISMATCH[%u] in %dms: %c%d 0x%08x 0x%010llx 0x%0*llx != 0x%0*llx\n",
				 (poll_action) ? "FATAL " : "", cnt, pcount,
				 (poll_action) ? 'P' : 'R',
				 bit_len, cpp_id, (unsigned long long)cpp_addr,
				 (bit_len == 32) ? 8 : 16,
				 (unsigned long long)val,
				 (bit_len == 32) ? 8 : 16,
				 (unsigned long long)tmp64);

			if (poll_action)
				err = -ETIMEDOUT;
			else
				err = 0;
		} else if (pcount > 0) {
			dev_warn(nfp_cpp_device(cpp),
				 "MATCH[%u] in %dms: %c%d 0x%08x 0x%010llx 0x%0*llx == 0x%0*llx\n",
				 cnt, pcount,
				 (poll_action) ? 'P' : 'R',
				 bit_len, cpp_id, (unsigned long long)cpp_addr,
				 (bit_len == 32) ? 8 : 16,
				 (unsigned long long)val,
				 (bit_len == 32) ? 8 : 16,
				 (unsigned long long)tmp64);
		}
		break;

	case NFP_CA_ACTION_READ_IGNV32:
		err = ca_cpp_read(ca, cpp_id, cpp_addr,
				  &tmp32, sizeof(tmp32));
		break;
	case NFP_CA_ACTION_READ_IGNV64:
		err = ca_cpp_read(ca, cpp_id, cpp_addr,
				  &tmp64, sizeof(tmp64));
		break;
	case NFP_CA_ACTION_WRITE32:
		if (~(u32)mask) {
			err = ca_cpp_read(ca, cpp_id, cpp_addr,
					  &tmp32, sizeof(tmp32));
			if (err < 0)
				return err;

			val |= tmp32 & ~mask;
		}
		tmp32 = val;
		err = ca_cpp_write(ca, cpp_id, cpp_addr,
				   &tmp32, sizeof(tmp32));
		break;
	case NFP_CA_ACTION_WRITE64:
		if (~(u64)mask) {
			err = ca_cpp_read(ca, cpp_id, cpp_addr,
					  &tmp64, sizeof(tmp64));
			if (err < 0)
				return err;

			val |= tmp64 & ~mask;
		}
		tmp64 = val;
		err = ca_cpp_write(ca, cpp_id, cpp_addr, &tmp64, sizeof(tmp64));
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int uncompress(u8 *out, size_t out_size, const u8 *in, size_t in_size)
{
	int err, ws_size;
	z_stream zs = {};

	ws_size = zlib_inflate_workspacesize();

	zs.next_in = in;
	zs.avail_in = in_size;
	zs.next_out = out;
	zs.avail_out = out_size;
	zs.workspace = kmalloc(ws_size, GFP_KERNEL);
	if (!zs.workspace)
		return -ENOMEM;

	err = zlib_inflateInit(&zs);
	if (err != Z_OK) {
		err = (err == Z_MEM_ERROR) ? -ENOMEM : -EIO;
		goto exit;
	}

	err = zlib_inflate(&zs, Z_FINISH);
	if (err != Z_STREAM_END) {
		err = (err == Z_MEM_ERROR) ? -ENOMEM : -EIO;
		goto exit;
	}

	zlib_inflateEnd(&zs);
	err = 0;

exit:
	kfree(zs.workspace);
	return err;
}

/*
 * nfp_ca_parse - Parse a CPP Action replay file
 * @cpp:   CPP handle
 * @buff:  Buffer with trace data
 * @bytes: Length of buffer
 * @cb:    A callback function to be called on each item in the trace.
 */
static int nfp_ca_parse(const void *buff, size_t bytes,
			nfp_ca_callback cb, void *priv)
{
	const u8 *byte = buff;
	u8 *zbuff = NULL;
	u32 cpp_id = 0;
	u64 cpp_addr = 0;
	size_t loc, usize;
	u8 ca;
	int err = -EINVAL;
	u32 mask32 = ~0U;
	u64 mask64 = ~0ULL;

	/* File too small? */
	if (bytes < (NFP_CA_SZ(NFP_CA_START) + NFP_CA_SZ(NFP_CA_END)))
		return -EINVAL;

	ca = byte[0];
	if (ca != NFP_CA_START)
		return -EINVAL;

	switch (ca32_to_cpu(&byte[1])) {
	case NFP_CA_ZSTART_MAGIC:
		/* Decompress first... */
		usize = ca32_to_cpu(&byte[5]);

		/* We use vmalloc() since kmalloc() requests contigous pages,
		 * and this gets increasingly unlikely as the size of the
		 * area to allocate increases.
		 *
		 * As uncompressed NFP firmwares can exceed 32M in size,
		 * we will use vmalloc() to allocate the firmware's
		 * uncompressed buffer.
		 */
		zbuff = vmalloc(usize);
		if (!zbuff)
			return -ENOMEM;

		usize -= NFP_CA_SZ(NFP_CA_START);
		err = uncompress((u8 *)zbuff + NFP_CA_SZ(NFP_CA_START),
				 usize, &byte[NFP_CA_SZ(NFP_CA_START)],
				 bytes - NFP_CA_SZ(NFP_CA_START));
		if (err < 0) {
			vfree(zbuff);
			/* Uncompression error */
			return err;
		}

		/* Patch up start to look like a NFP_CA_START */
		usize += NFP_CA_SZ(NFP_CA_START);
		zbuff[0] = NFP_CA_START;
		cpu_to_ca32(&zbuff[1], NFP_CA_START_MAGIC);
		cpu_to_ca32(&zbuff[5], usize);

		bytes = usize;
		byte = zbuff;
		/* FALLTHROUGH */
	case NFP_CA_START_MAGIC:
		/* Uncompressed start */
		usize = ca32_to_cpu(&byte[5]);
		if (usize < bytes) {
			/* Too small! */
			err = -ENOSPC;
			goto exit;
		}
		break;
	default:
		return -ENOSPC;
	}

	/* CRC check before processing */
	if (cb != nfp_ca_null) {
		err = nfp_ca_parse(byte, bytes, nfp_ca_null, NULL);
		if (err < 0)
			goto exit;
	}

	err = 0;
	for (loc = NFP_CA_SZ(NFP_CA_START); loc < bytes;
			loc += NFP_CA_SZ(byte[loc])) {
		const u8 *vp = &byte[loc + 1];
		u32 tmp32;
		u64 tmp64;

		ca = byte[loc];
		if (ca == NFP_CA_END) {
			loc += NFP_CA_SZ(NFP_CA_END);
			break;
		}

		switch (ca) {
		case NFP_CA_CPP_ID:
			cpp_id = ca32_to_cpu(vp);
			err = 0;
			break;
		case NFP_CA_CPP_ADDR:
			cpp_addr = ca64_to_cpu(vp);
			err = 0;
			break;
		case NFP_CA_INC_READ_4:
			cpp_addr += 4;
			/* FALLTHROUGH */
		case NFP_CA_READ_4:
			tmp32 = ca32_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_READ32,
				 cpp_id, cpp_addr, tmp32, mask32);
			break;
		case NFP_CA_INC_READ_8:
			cpp_addr += 8;
			/* FALLTHROUGH */
		case NFP_CA_READ_8:
			tmp64 = ca64_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_READ64,
				 cpp_id, cpp_addr, tmp64, mask64);
			break;
		case NFP_CA_POLL_4:
			tmp32 = ca32_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_POLL32,
				 cpp_id, cpp_addr, tmp32, mask32);
			break;
		case NFP_CA_POLL_8:
			tmp64 = ca64_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_POLL64,
				 cpp_id, cpp_addr, tmp64, mask64);
			break;
		case NFP_CA_INC_READ_IGNV_4:
			cpp_addr += 4;
			/* FALLTHROUGH */
		case NFP_CA_READ_IGNV_4:
			tmp32 = ca32_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_READ_IGNV32,
				 cpp_id, cpp_addr, tmp32, mask32);
			break;
		case NFP_CA_INC_READ_IGNV_8:
			cpp_addr += 8;
			/* FALLTHROUGH */
		case NFP_CA_READ_IGNV_8:
			tmp64 = ca64_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_READ_IGNV64,
				 cpp_id, cpp_addr, tmp64, mask64);
			break;
		case NFP_CA_INC_WRITE_4:
		case NFP_CA_INC_ZERO_4:
			cpp_addr += 4;
			/* FALLTHROUGH */
		case NFP_CA_WRITE_4:
		case NFP_CA_ZERO_4:
			if (ca == NFP_CA_INC_ZERO_4 || ca == NFP_CA_ZERO_4)
				tmp32 = 0;
			else
				tmp32 = ca32_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_WRITE32,
				 cpp_id, cpp_addr, tmp32, mask32);
			break;
		case NFP_CA_INC_WRITE_8:
		case NFP_CA_INC_ZERO_8:
			cpp_addr += 8;
			/* FALLTHROUGH */
		case NFP_CA_WRITE_8:
		case NFP_CA_ZERO_8:
			if (ca == NFP_CA_INC_ZERO_8 || ca == NFP_CA_ZERO_8)
				tmp64 = 0;
			else
				tmp64 = ca64_to_cpu(vp);
			err = cb(priv, NFP_CA_ACTION_WRITE64,
				 cpp_id, cpp_addr, tmp64, mask64);
			break;
		case NFP_CA_MASK_4:
			mask32 = ca32_to_cpu(vp);
			break;
		case NFP_CA_MASK_8:
			mask64 = ca64_to_cpu(vp);
			break;
		default:
			err = -EINVAL;
			break;
		}
		if (err < 0)
			goto exit;
	}

	if (ca == NFP_CA_END && loc == bytes) {
		if (cb == nfp_ca_null) {
			u32 crc;

			loc -= NFP_CA_SZ(NFP_CA_END);
			crc = crc32_posix(byte, loc);
			if (crc != ca32_to_cpu(&byte[loc + 1])) {
				err = -EINVAL;
				goto exit;
			}
		}
		err = 0;
	}

exit:
	vfree(zbuff);

	return err;
}

/**
 * nfp_ca_replay - Replay a CPP Action trace
 * @cpp:       CPP handle
 * @ca_buffer: Buffer with trace
 * @ca_size:   Size of Buffer
 *
 * The function performs two passes of the buffer.  The first is to
 * calculate and verify the CRC at the end of the buffer, and the
 * second replays the transaction set.
 *
 * Return: 0, or -ERRNO
 */
int nfp_ca_replay(struct nfp_cpp *cpp, const void *ca_buffer, size_t ca_size)
{
	struct ca_cpp ca_cpp = { NULL };
	int err;

	ca_cpp.cpp = cpp;

	err = nfp_ca_parse(ca_buffer, ca_size, nfp_ca_cb_cpp, &ca_cpp);

#if DEBUG_CA_CPP_STATS
	dev_info(nfp_cpp_device(cpp),
		 "%s: Actions: %d\n", __func__,
		 ca_cpp.stats.actions);

#endif

	return err;
}
