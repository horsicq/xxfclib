/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_softronics.h @brief Softronics Compressed File v2.00 reader. */

#ifndef XXFCLIB_FORMAT_SOFTRONICS_H
#define XXFCLIB_FORMAT_SOFTRONICS_H

#include "xxfclib/algo/softronics/xx_softronics.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_softronics {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint32_t header_size;
    uint32_t compressed_size;
    uint32_t dos_datetime;
    char file_name[13];
} xx_softronics;

typedef xx_softronics xx_softronics_t;
typedef xx_softronics XSoftronics;

XXFC_API void xx_softronics_init(xx_softronics *archive,
                                 xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_softronics *xx_softronics_create(xx_io_device *device,
                                              int64_t base_address);
XXFC_API void xx_softronics_destroy(xx_softronics *archive);
XXFC_API void xx_softronics_free(xx_softronics *archive);

XXFC_API bool xx_softronics_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_softronics_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_softronics_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_softronics_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_softronics_unpack_to_device(xx_softronics *archive,
                                              xx_io_device *destination,
                                              xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_softronics_create_archive_records_reading(Abstractformat *self,
                                             const xx_list_s *options,
                                             xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_softronics_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_softronics_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_softronics_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_softronics_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_softronics_get_uncompressed_size(
    const xx_softronics *archive);
XXFC_API int64_t xx_softronics_get_stream_end(const xx_softronics *archive);

static inline Abstractformat *xx_softronics_to_format(xx_softronics *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SOFTRONICS_H */
