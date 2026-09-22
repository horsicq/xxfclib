/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rnc.h @brief Rob Northen Compression (RNC ProPack) reader. */

#ifndef XXFCLIB_FORMAT_RNC_H
#define XXFCLIB_FORMAT_RNC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RNC ProPack is a single packed stream with an 18 byte big-endian header.
 * The stream carries no member name, so the archive API exposes the decoded
 * data as a single record called "payload". */
typedef struct xx_rnc {
    Abstractformat format;      /**< Base format structure (must be first). */
    uint64_t unpacked_size;     /**< Declared size of the decoded stream. */
    uint64_t packed_size;       /**< Declared size of the packed stream. */
    int64_t stream_end;         /**< Absolute end offset of the RNC stream. */
    uint16_t unpacked_crc;      /**< Declared CRC16 of the decoded stream. */
    uint16_t packed_crc;        /**< Declared CRC16 of the packed stream. */
    uint8_t method;             /**< 1 = LZ77 + Huffman, 2 = bitwise LZ77. */
    uint8_t leeway;             /**< In place unpacking leeway in bytes. */
    uint8_t chunk_count;        /**< Number of packed chunks. */
} xx_rnc;

typedef xx_rnc xx_rnc_t;
typedef xx_rnc XRnc;

XXFC_API void xx_rnc_init(xx_rnc *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_rnc *xx_rnc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rnc_destroy(xx_rnc *archive);
XXFC_API void xx_rnc_free(xx_rnc *archive);

XXFC_API bool xx_rnc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rnc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rnc_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_rnc_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_rnc_unpack_to_device(xx_rnc *archive,
                                      xx_io_device *destination,
                                      xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rnc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rnc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rnc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rnc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rnc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_rnc_get_unpacked_size(const xx_rnc *archive);
XXFC_API int64_t xx_rnc_get_stream_end(const xx_rnc *archive);
XXFC_API uint8_t xx_rnc_get_method(const xx_rnc *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RNC_H */
