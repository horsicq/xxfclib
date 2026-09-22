/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zpaq.h @brief ZPAQ archive reader (identification only). */

#ifndef XXFCLIB_FORMAT_ZPAQ_H
#define XXFCLIB_FORMAT_ZPAQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ZPAQ archive.
 *
 * ZPAQ blocks carry their own decompression program in a bytecode the decoder
 * has to interpret, which is why there is no fixed codec to implement and why
 * XArchive's XZPAQ delegates to an external backend. This reader locates and
 * validates the first block header and stops there.
 *
 * A block may be preceded by a 13-byte locator tag, which lets a ZPAQ stream
 * be found inside another file; @ref xx_zpaq_get_block_offset reports where
 * the block actually starts.
 */
typedef struct xx_zpaq {
    Abstractformat format;
    int64_t block_offset;  /**< 0, or 13 when the locator tag is present. */
    bool has_tag;          /**< True when the 13-byte locator tag precedes it. */
    uint8_t level;         /**< Block level, 1 or 2. */
    uint16_t header_size;  /**< Size of the block's bytecode header. */
} xx_zpaq;

typedef xx_zpaq xx_zpaq_t;
typedef xx_zpaq XZpaq;

XXFC_API void xx_zpaq_init(xx_zpaq *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_zpaq *xx_zpaq_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_zpaq_destroy(xx_zpaq *archive);
XXFC_API void xx_zpaq_free(xx_zpaq *archive);

XXFC_API bool xx_zpaq_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zpaq_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_zpaq_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_zpaq_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

/** @brief Where the first block starts, relative to the base address. */
XXFC_API int64_t xx_zpaq_get_block_offset(const xx_zpaq *archive);
/** @brief True when the 13-byte locator tag precedes the first block. */
XXFC_API bool xx_zpaq_has_tag(const xx_zpaq *archive);
/** @brief Block level, 1 or 2. */
XXFC_API uint8_t xx_zpaq_get_level(const xx_zpaq *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZPAQ_H */
