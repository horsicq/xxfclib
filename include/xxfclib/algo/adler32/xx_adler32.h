/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_adler32.h
 * @brief The Adler-32 checksum (RFC 1950).
 *
 * Implemented five separate times inside the library before this module
 * existed -- in the zlib stream wrapper, the zlib reader, lzop, zcmp and
 * die_engine. It is nine lines of arithmetic with one subtlety (the modulo
 * must not be deferred so long that the accumulator overflows), so a single
 * correct copy is worth more than five plausible ones.
 */

#ifndef XX_ADLER32_H
#define XX_ADLER32_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The value of an empty Adler-32, and the correct seed for a new run. */
#define XX_ADLER32_INIT 1U

/**
 * @brief Continue an Adler-32 over another block.
 * @param adler running value; pass XX_ADLER32_INIT for the first block.
 * @return the updated checksum, high half `b`, low half `a`.
 */
XXFC_API uint32_t xx_adler32_update(uint32_t adler, const void *data, size_t size);

/** @brief Adler-32 of a whole buffer. */
XXFC_API uint32_t xx_adler32(const void *data, size_t size);

/**
 * @brief Adler-32 over a clamped device range, starting with XX_ADLER32_INIT.
 * Negative or oversized size reads to EOF. Invalid or empty ranges return
 * XX_ADLER32_INIT; a read failure returns the checksum of completed cache
 * windows. Buffered devices retain their logical cursor. Ordinary devices are
 * borrowed through a temporary cache and their underlying cursor may advance.
 */
XXFC_API uint32_t xx_adler32_device(xx_io_device *device, int64_t offset, int64_t size);

#ifdef __cplusplus
}
#endif

#endif /* XX_ADLER32_H */
