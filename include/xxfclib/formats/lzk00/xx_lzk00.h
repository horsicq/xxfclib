/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzk00.h @brief LZK00 single-stream reader. */

#ifndef XXFCLIB_FORMAT_LZK00_H
#define XXFCLIB_FORMAT_LZK00_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "LZK00" + four reserved zero bytes, then the byte aligned LZ77 stream. */
#define XX_LZK00_HEADER_SIZE 9U

typedef struct xx_lzk00 {
    Abstractformat format;
    int64_t stream_end;
    uint64_t unpacked_size;
} xx_lzk00;

typedef xx_lzk00 xx_lzk00_t;
typedef xx_lzk00 XLzk00;

XXFC_API void xx_lzk00_init(xx_lzk00 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_lzk00 *xx_lzk00_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_lzk00_destroy(xx_lzk00 *archive);
XXFC_API void xx_lzk00_free(xx_lzk00 *archive);

XXFC_API bool xx_lzk00_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzk00_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_lzk00_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lzk00_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzk00_unpack_to_device(xx_lzk00 *archive,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzk00_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzk00_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzk00_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzk00_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzk00_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_lzk00_get_stream_end(const xx_lzk00 *archive);
XXFC_API uint64_t xx_lzk00_get_unpacked_size(const xx_lzk00 *archive);

/** Decode a whole LZK00 file (header included) into a freshly allocated
 *  buffer.  The caller owns *output and releases it with xx_mem_free(). */
XXFC_API bool xx_lzk00_decompress_memory(const uint8_t *input,
                                         size_t input_size,
                                         uint8_t **output,
                                         size_t *output_size);

static inline Abstractformat *xx_lzk00_to_format(xx_lzk00 *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZK00_H */
