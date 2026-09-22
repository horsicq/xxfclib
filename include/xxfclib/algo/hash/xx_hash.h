/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_hash.h
 * @brief MD5, SHA-1 and SHA-256 message digests.
 *
 * These three were each implemented several times inside the library before
 * this module existed -- MD5 four times (Tivoli, SEAMA, DLOB, die_engine),
 * SHA-1 and SHA-256 once each but in places that made them unreachable to
 * anyone else. A digest has exactly one correct answer, so there is no reason
 * for a format reader to carry its own.
 *
 * The shape follows xx_crc: a streaming context for chunked input, a one-shot
 * over a buffer, and a device variant that reads a range through xx_io.
 * Digests are produced as raw bytes; xx_hash_to_hex() renders one when a
 * printable form is wanted.
 *
 * Endianness note: MD5 serialises its state little-endian, SHA-1 and SHA-256
 * big-endian. That is a property of the algorithms, not of the host, and is
 * handled internally -- the digest bytes are identical on any platform.
 */

#ifndef XX_HASH_H
#define XX_HASH_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Digest lengths in bytes. */
#define XX_MD5_DIGEST_SIZE 16
#define XX_SHA1_DIGEST_SIZE 20
#define XX_SHA256_DIGEST_SIZE 32
/** Longest digest this module produces, for sizing a caller's buffer. */
#define XX_HASH_MAX_DIGEST_SIZE 32

/** Which digest a context computes. */
typedef enum xx_hash_type_e {
    XX_HASH_MD5 = 0,
    XX_HASH_SHA1,
    XX_HASH_SHA256
} xx_hash_type_t;

/**
 * @brief Streaming digest context.
 *
 * One struct covers all three algorithms: the state word count and the block
 * handling differ, but the shape (8 state words, a 64-byte block, a 64-bit
 * length) does not. MD5 and SHA-1 use the first four and five words.
 */
typedef struct xx_hash_context {
    xx_hash_type_t type;
    uint32_t state[8];
    uint64_t length;              /**< Total bytes fed in, for the padding. */
    uint8_t buffer[64];           /**< Partial block held between updates. */
    size_t buffered;
    bool initialized;
} xx_hash_context;

/* ========================================================================= */
/* --- Streaming API                                                     --- */
/* ========================================================================= */

/**
 * @brief Begin a digest of the given type.
 * @return false if @p ctx is NULL or @p type is not recognised.
 */
XXFC_API bool xx_hash_init(xx_hash_context *ctx, xx_hash_type_t type);

/** @brief Feed a block of data into an active context. */
XXFC_API void xx_hash_update(xx_hash_context *ctx, const void *data,
                             size_t size);

/**
 * @brief Finish the digest and write it to @p out.
 *
 * @param out       receives xx_hash_digest_size(ctx->type) bytes.
 * @param out_size  capacity of @p out; the call fails if it is too small.
 * @return false when the context was never initialized or @p out is too small.
 *
 * The context is consumed: call xx_hash_init() again to reuse it.
 */
XXFC_API bool xx_hash_final(xx_hash_context *ctx, void *out, size_t out_size);

/** @brief Digest length in bytes for a given type, or 0 if unrecognised. */
XXFC_API size_t xx_hash_digest_size(xx_hash_type_t type);

/* ========================================================================= */
/* --- One-shot API                                                      --- */
/* ========================================================================= */

/**
 * @brief Digest a buffer in one call.
 * @return false on a NULL output, an unknown type, or too small a buffer.
 *         A NULL @p data with @p size 0 is valid and digests the empty string.
 */
XXFC_API bool xx_hash_memory(xx_hash_type_t type, const void *data,
                             size_t size, void *out, size_t out_size);

/**
 * @brief Digest @p size bytes of @p dev starting at @p offset.
 *
 * Reads in chunks, so the range never has to fit in memory. A @p size of -1
 * means "to the end of the device".
 * @return false if the range lies outside the device or a read fails.
 */
XXFC_API bool xx_hash_device(xx_hash_type_t type, xx_io_device *dev,
                             int64_t offset, int64_t size, void *out,
                             size_t out_size, xx_pd_struct *pd);

/* ========================================================================= */
/* --- Convenience shortcuts                                             --- */
/* ========================================================================= */

/** @brief MD5 of a buffer into a 16-byte @p out. */
XXFC_API bool xx_md5_memory(const void *data, size_t size, void *out);
/** @brief SHA-1 of a buffer into a 20-byte @p out. */
XXFC_API bool xx_sha1_memory(const void *data, size_t size, void *out);
/** @brief SHA-256 of a buffer into a 32-byte @p out. */
XXFC_API bool xx_sha256_memory(const void *data, size_t size, void *out);

/**
 * @brief Render a digest as lowercase hex.
 *
 * @param out       receives 2*digest_size+1 bytes including the terminator.
 * @return false when @p out is too small.
 */
XXFC_API bool xx_hash_to_hex(const void *digest, size_t digest_size, char *out,
                             size_t out_size);

/**
 * @brief Compare a computed digest against an expected one.
 *
 * A plain length-checked comparison, provided so callers stop open-coding it
 * (and so the intent reads clearly at the call site). This is not a
 * constant-time compare: these digests authenticate container metadata, not
 * secrets, and no xxfclib reader makes a trust decision on one.
 */
XXFC_API bool xx_hash_equal(const void *a, const void *b, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XX_HASH_H */
