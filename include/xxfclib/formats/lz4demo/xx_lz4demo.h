/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lz4demo.h @brief Legacy LZ4 ("lz4demo") frame reader. */

#ifndef XXFCLIB_FORMAT_LZ4DEMO_H
#define XXFCLIB_FORMAT_LZ4DEMO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The legacy LZ4 frame, as written by the original lz4demo tool and
 * still produced by `lz4 -l`.
 *
 *   0x00  u32 LE magic 0x184c2102
 *   then a chain of blocks, each
 *     u32 LE compressed size
 *     that many bytes of an LZ4 block-compressed stream
 *
 * There is no end marker, no stored content size and no checksum: the chain
 * simply runs to the end of the file.  Each block decodes to at most 8 MiB,
 * which is the only bound the format gives.  A legacy magic or a skippable
 * frame magic (0x184d2a5x) where a block size would be starts a new frame.
 *
 * This is NOT the modern LZ4 frame format (magic 0x184d2204); that one has
 * its own reader.  Deriving the member's unpacked size means decoding it,
 * because the container never stores it.
 */
typedef struct xx_lz4demo {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< 0 when the size pass was skipped. */
    uint64_t block_count;
} xx_lz4demo;

typedef xx_lz4demo xx_lz4demo_t;

XXFC_API void xx_lz4demo_init(xx_lz4demo *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_lz4demo *xx_lz4demo_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_lz4demo_destroy(xx_lz4demo *archive);
XXFC_API void xx_lz4demo_free(xx_lz4demo *archive);

XXFC_API bool xx_lz4demo_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_lz4demo_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_lz4demo_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_lz4demo_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lz4demo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lz4demo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lz4demo_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lz4demo_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lz4demo_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZ4DEMO_H */
