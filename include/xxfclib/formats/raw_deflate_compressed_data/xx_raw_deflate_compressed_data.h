/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_raw_deflate_compressed_data.h
 * @brief Raw Deflate (RFC 1951) compressed data: a bare Deflate stream with
 * no zlib, gzip or ZIP wrapper around it.
 */

#ifndef XXFCLIB_FORMAT_RAW_DEFLATE_COMPRESSED_DATA_H
#define XXFCLIB_FORMAT_RAW_DEFLATE_COMPRESSED_DATA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A file that is one raw Deflate stream and nothing else, as written
 * by zlib with a negative window (`wbits = -15`), by .NET DeflateStream, or
 * by cutting the member data out of a ZIP / gzip file.
 *
 * The stream is a run of blocks, each opening with a 3-bit header
 * (BFINAL, then BTYPE: 0 stored, 1 fixed Huffman, 2 dynamic Huffman,
 * 3 reserved); the block with BFINAL set is the last.  There is no magic,
 * no stored length and no checksum, so validity is decided by walking the
 * whole stream: every block must be well formed under the rules zlib's
 * inflate enforces, no match may reach before the start of the output, and
 * the final block must end in the last byte of the file.  The payload's
 * size is only known after that walk.
 *
 * check_is_valid is the detector's (late) probe and is stricter than the
 * grammar: it also wants at least 64 bytes of input (shorter chance streams
 * are too common), a non-empty payload, at most 128 MiB of input and at most
 * 4 GiB of plaintext.  handle_base_info applies the grammar alone, for 2
 * bytes up to 4 GiB of input and 16 GiB of plaintext, so a caller that has
 * chosen this reader can open short streams too.
 *
 * One record, named "payload".  Deflate64 is not handled.
 */
typedef struct xx_raw_deflate_compressed_data {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;   /**< Bytes the stream decodes to. */
    int64_t stream_size;      /**< Bytes the stream occupies. */
    uint64_t block_count;     /**< Deflate blocks in the stream. */
} xx_raw_deflate_compressed_data;

typedef xx_raw_deflate_compressed_data xx_raw_deflate_compressed_data_t;

XXFC_API void xx_raw_deflate_compressed_data_init(
    xx_raw_deflate_compressed_data *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_raw_deflate_compressed_data *xx_raw_deflate_compressed_data_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_raw_deflate_compressed_data_destroy(
    xx_raw_deflate_compressed_data *archive);
XXFC_API void xx_raw_deflate_compressed_data_free(
    xx_raw_deflate_compressed_data *archive);

XXFC_API bool xx_raw_deflate_compressed_data_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_raw_deflate_compressed_data_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_raw_deflate_compressed_data_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_raw_deflate_compressed_data_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_raw_deflate_compressed_data_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_raw_deflate_compressed_data_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_raw_deflate_compressed_data_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_raw_deflate_compressed_data_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_raw_deflate_compressed_data_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode the stream into @p destination (after a successful
 * handle_base_info or check_is_valid).  Fails unless exactly the measured
 * number of bytes is produced.
 */
XXFC_API bool xx_raw_deflate_compressed_data_unpack_to_device(
    xx_raw_deflate_compressed_data *archive, xx_io_device *destination,
    xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RAW_DEFLATE_COMPRESSED_DATA_H */
