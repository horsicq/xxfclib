/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_oraclesqueeze.h @brief Oracle/R:BASE Squeeze reader. */

#ifndef XXFCLIB_FORMAT_ORACLESQUEEZE_H
#define XXFCLIB_FORMAT_ORACLESQUEEZE_H

#include "xxfclib/algo/oraclesqueeze/xx_oraclesqueeze.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_oraclesqueeze {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t tree_offset;
    uint16_t checksum;
    char file_name[256];
} xx_oraclesqueeze;

typedef xx_oraclesqueeze xx_oraclesqueeze_t;
typedef xx_oraclesqueeze XOracleSqueeze;

XXFC_API void xx_oraclesqueeze_init(xx_oraclesqueeze *archive,
                                    xx_io_device *device,
                                    int64_t base_address);
XXFC_API xx_oraclesqueeze *xx_oraclesqueeze_create(xx_io_device *device,
                                                    int64_t base_address);
XXFC_API void xx_oraclesqueeze_destroy(xx_oraclesqueeze *archive);
XXFC_API void xx_oraclesqueeze_free(xx_oraclesqueeze *archive);

XXFC_API bool xx_oraclesqueeze_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_oraclesqueeze_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_oraclesqueeze_get_format_size(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API uint64_t xx_oraclesqueeze_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_oraclesqueeze_unpack_to_device(xx_oraclesqueeze *archive,
                                                xx_io_device *destination,
                                                xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_oraclesqueeze_create_archive_records_reading(Abstractformat *self,
                                                const xx_list_s *options,
                                                xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_oraclesqueeze_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_oraclesqueeze_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_oraclesqueeze_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_oraclesqueeze_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_oraclesqueeze_get_uncompressed_size(
    const xx_oraclesqueeze *archive);
XXFC_API int64_t xx_oraclesqueeze_get_stream_end(
    const xx_oraclesqueeze *archive);
XXFC_API uint16_t xx_oraclesqueeze_get_checksum(
    const xx_oraclesqueeze *archive);

static inline Abstractformat *xx_oraclesqueeze_to_format(
    xx_oraclesqueeze *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ORACLESQUEEZE_H */
