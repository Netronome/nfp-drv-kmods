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

#include <linux/kernel.h>
#include <linux/crc32.h>

#include "crc32.h"

/**
 * crc32_posix_add() - Append data to a POSIX CRC32 calculation
 * @crc32:	Current CRC32 working state (initialize as 0)
 * @buff:	Pointer to buffer of data to add
 * @len:	Size of buffer
 *
 * Return: New CRC32 working state
 */
u32 crc32_posix_add(u32 crc, const void *buff, size_t len)
{
	return crc32_be(crc, buff, len);
}

/**
 * crc32_posix_end() - Finalize POSIX CRC32 working state
 * @crc32:	Current CRC32 working state
 * @total_len:	Total length of data that was CRC32'd
 *
 * Return: Final POSIX CRC32 value
 */
u32 crc32_posix_end(u32 crc, size_t total_len)
{
	/* Extend with the length of the string. */
	while (total_len != 0) {
		u8 c = total_len & 0xff;

		crc = crc32_posix_add(crc, &c, 1);
		total_len >>= 8;
	}
	return ~crc;
}
