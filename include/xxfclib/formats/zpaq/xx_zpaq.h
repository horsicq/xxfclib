/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_zpaq.h @brief ZPAQ archive reader (journaling and streaming). */

#ifndef XXFCLIB_FORMAT_ZPAQ_H
#define XXFCLIB_FORMAT_ZPAQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xx_zpaq_scan;

/**
 * @brief A ZPAQ archive (levels 1 and 2).
 *
 * A ZPAQ archive is a run of blocks. Every block carries its own model as a
 * list of context-mixing components plus a program for the ZPAQL virtual
 * machine that computes their contexts, and its first segment may carry a
 * second ZPAQL program that post-processes the decoded bytes (LZ77, BWT,
 * E8E9, ...). This reader interprets both programs and all nine component
 * types, so it decodes any conforming block.
 *
 * Two archive layouts are listed:
 *  - streaming (zpaq 1.x, zpaq 7 "-method s..."): each segment is a named
 *    file; an unnamed segment continues the previous file;
 *  - journaling (zpaq 6/7): "jDC" transactions of c (header), d (data
 *    fragments), h (fragment table) and i (index) blocks. The listing is the
 *    newest version of every file, as "zpaq x" extracts it.
 *
 * A block may be preceded by a 13-byte locator tag;
 * @ref xx_zpaq_get_block_offset reports where the first block starts.
 * Encrypted archives (no plaintext signature) are not recognised.
 */
typedef struct xx_zpaq {
    Abstractformat format;
    int64_t block_offset;  /**< 0, or 13 when the locator tag is present. */
    bool has_tag;          /**< True when the 13-byte locator tag precedes it. */
    uint8_t level;         /**< Block level, 1 or 2. */
    uint16_t header_size;  /**< Size of the block's bytecode header. */
    struct xx_zpaq_scan *scan; /**< Archive walk, built on first use. */
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

XXFC_API xx_archive_record_state *xx_zpaq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zpaq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zpaq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zpaq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zpaq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief Where the first block starts, relative to the base address. */
XXFC_API int64_t xx_zpaq_get_block_offset(const xx_zpaq *archive);
/** @brief True when the 13-byte locator tag precedes the first block. */
XXFC_API bool xx_zpaq_has_tag(const xx_zpaq *archive);
/** @brief Block level, 1 or 2. */
XXFC_API uint8_t xx_zpaq_get_level(const xx_zpaq *archive);
/** @brief True when the archive uses the journaling (jDC) layout. Walks the
 *  archive on first use. */
XXFC_API bool xx_zpaq_is_journaling(xx_zpaq *archive, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_zpaq_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_zpaq_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_zpaq_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_ZPAQ_H */
