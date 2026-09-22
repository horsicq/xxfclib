/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_BLAKE2_H
#define XXFCLIB_ALGO_BLAKE2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_BLAKE2SP_DIGEST_SIZE 32U

/* Public storage for stack allocation; fields are private implementation state. */
typedef struct xx_blake2sp_node {
    uint32_t words[8];
    uint64_t compressed_size;
    uint8_t pending[64];
    size_t pending_size;
} xx_blake2sp_node;

typedef struct xx_blake2sp_context {
    xx_blake2sp_node leaves[8];
    uint64_t total_size;
    uint32_t state_tag;
} xx_blake2sp_context;

/** Unkeyed BLAKE2sp-256 (eight BLAKE2s leaves), no dynamic allocation.
 * init overwrites the context. update copies/consumes input before returning;
 * NULL data is valid only for size 0. Total input is limited to UINT64_MAX.
 * Input/output must not overlap the context. final is single-use and wipes the
 * context; call init to reuse it. Invalid calls return false without changing
 * the context or output. A one-shot digest may overlap its input buffer.
 */
XXFC_API bool xx_blake2sp_init(xx_blake2sp_context *context);
XXFC_API bool xx_blake2sp_update(xx_blake2sp_context *context,
                                const void *data, size_t size);
XXFC_API bool xx_blake2sp_final(xx_blake2sp_context *context,
                               uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE]);
XXFC_API void xx_blake2sp_clear(xx_blake2sp_context *context);
XXFC_API bool xx_blake2sp_calc(const void *data, size_t size,
                              uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE]);

/** Hash exactly size bytes starting at the nonnegative 64-bit offset.
 * Uses bounded storage, retries short reads, rejects premature EOF/error and
 * cancellation, and leaves digest unchanged on failure. Does not own/close the
 * device or restore its cursor. The requested range must fit int64_t and the
 * device size when reported. pd is optional; its nested progress is released.
 */
XXFC_API bool xx_blake2sp_calc_device(xx_io_device *device, int64_t offset,
                                     int64_t size,
                                     uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE],
                                     xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
