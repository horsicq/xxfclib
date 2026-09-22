/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gashuff.h @brief GAS static-Huffman single-file reader. */

#ifndef XXFCLIB_FORMAT_GASHUFF_H
#define XXFCLIB_FORMAT_GASHUFF_H

#include "xxfclib/algo/gashuff/xx_gashuff.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gashuff {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t table_end_offset;
} xx_gashuff;

typedef xx_gashuff xx_gashuff_t;
typedef xx_gashuff XGasHuff;

XXFC_API void xx_gashuff_init(xx_gashuff *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_gashuff *xx_gashuff_create(xx_io_device *device,
                                        int64_t base_address);
XXFC_API void xx_gashuff_destroy(xx_gashuff *archive);
XXFC_API void xx_gashuff_free(xx_gashuff *archive);

XXFC_API bool xx_gashuff_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_gashuff_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_gashuff_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_gashuff_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gashuff_unpack_to_device(xx_gashuff *archive,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gashuff_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gashuff_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gashuff_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gashuff_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gashuff_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_gashuff_get_uncompressed_size(
    const xx_gashuff *archive);
XXFC_API int64_t xx_gashuff_get_stream_end(const xx_gashuff *archive);
XXFC_API uint32_t xx_gashuff_get_table_end_offset(
    const xx_gashuff *archive);

static inline Abstractformat *xx_gashuff_to_format(xx_gashuff *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GASHUFF_H */
