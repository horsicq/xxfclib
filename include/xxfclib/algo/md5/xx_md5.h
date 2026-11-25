/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_md5.h @brief MD5 streaming, buffer, and device digests. */
#ifndef XX_MD5_H
#define XX_MD5_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_MD5_BLOCK_SIZE 64U
#define XX_MD5_DIGEST_SIZE 16

/** Copyable snapshot of an unfinished MD5 digest. Final consumes the context. */
typedef struct xx_md5_context {
    uint32_t state[4];
    uint64_t length;
    uint8_t buffer[XX_MD5_BLOCK_SIZE];
    size_t buffered;
    bool initialized;
} xx_md5_context;

XXFC_API bool xx_md5_init(xx_md5_context *context);
/** Feed bytes to an initialized context. NULL input is valid for size 0. */
XXFC_API void xx_md5_update(xx_md5_context *context, const void *data, size_t size);
/** Write 16 digest bytes. Invalid output leaves the context intact. */
XXFC_API bool xx_md5_final(xx_md5_context *context, void *digest, size_t digest_size);
/** Digest a buffer into 16 bytes. NULL input with size 0 is valid. */
XXFC_API bool xx_md5_memory(const void *data, size_t size, void *digest);
/** Digest a strict range of a seekable, known-size device.
 * Negative size means the remaining bytes; empty ranges at EOF are valid.
 * Oversized ranges, read failures, and cancellation return false without
 * writing the digest. Reads retry short transfers and advance the cursor.
 * The device is borrowed. */
XXFC_API bool xx_md5_device(xx_io_device *device, int64_t offset, int64_t size, void *digest, size_t digest_size, xx_pd_struct *pd);

/** Allocate uppercase hexadecimal for a 16-byte MD5 digest.
 * Returns NULL for NULL input or allocation failure. Free with xx_str_free. */
XXFC_API char *xx_md5_digest_hex(const void *digest);
/** Allocate the uppercase MD5 of a buffer. Invalid input produces 32 zeroes.
 * NULL input with size 0 hashes the empty buffer. Free with xx_str_free.
 * Returns NULL on allocation failure. */
XXFC_API char *xx_md5_hex(const void *data, size_t size);
/** Allocate the uppercase MD5 of a clamped device range.
 * Negative size means the remaining bytes; oversized ranges stop at EOF.
 * Invalid/unknown-size devices, invalid offsets, and empty ranges hash the
 * empty buffer. Read/cache failures produce 32 zeroes. Buffered inputs keep
 * their logical cursor; ordinary inputs use a temporary borrowed buffer whose
 * reads reposition the source. Returns NULL on output allocation failure.
 * Free with xx_str_free. */
XXFC_API char *xx_md5(xx_io_device *device, int64_t offset, int64_t size);

#ifdef __cplusplus
}
#endif
#endif /* XX_MD5_H */
