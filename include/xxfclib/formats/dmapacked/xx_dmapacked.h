/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_dmapacked.h @brief DMA Packed (.PK$) single-file reader. */

#ifndef XXFCLIB_FORMAT_DMAPACKED_H
#define XXFCLIB_FORMAT_DMAPACKED_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dmapacked {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint16_t dos_date;
    uint16_t dos_time;
    char file_name[15];
} xx_dmapacked;

typedef xx_dmapacked xx_dmapacked_t;
typedef xx_dmapacked XDMAPacked;

XXFC_API void xx_dmapacked_init(xx_dmapacked *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_dmapacked *xx_dmapacked_create(xx_io_device *device,
                                            int64_t base_address);
XXFC_API void xx_dmapacked_destroy(xx_dmapacked *archive);
XXFC_API void xx_dmapacked_free(xx_dmapacked *archive);

XXFC_API bool xx_dmapacked_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_dmapacked_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_dmapacked_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_dmapacked_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dmapacked_unpack_to_device(xx_dmapacked *archive,
                                            xx_io_device *destination,
                                            xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dmapacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dmapacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dmapacked_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dmapacked_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dmapacked_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dmapacked_get_uncompressed_size(
    const xx_dmapacked *archive);
XXFC_API int64_t xx_dmapacked_get_stream_end(const xx_dmapacked *archive);

static inline Abstractformat *xx_dmapacked_to_format(xx_dmapacked *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DMAPACKED_H */
