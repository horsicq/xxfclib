/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_logitechcompress.h @brief Logitech Compress single-file reader. */

#ifndef XXFCLIB_FORMAT_LOGITECHCOMPRESS_H
#define XXFCLIB_FORMAT_LOGITECHCOMPRESS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_logitechcompress {
    Abstractformat format;
    uint64_t uncompressed_size;
    int64_t stream_end;
    uint8_t missing_name_character;
    char file_name[16];
} xx_logitechcompress;

typedef xx_logitechcompress xx_logitechcompress_t;
typedef xx_logitechcompress XLogitechCompress;

XXFC_API void xx_logitechcompress_init(xx_logitechcompress *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_logitechcompress *xx_logitechcompress_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_logitechcompress_destroy(xx_logitechcompress *archive);
XXFC_API void xx_logitechcompress_free(xx_logitechcompress *archive);

XXFC_API bool xx_logitechcompress_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_logitechcompress_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_logitechcompress_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_logitechcompress_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_logitechcompress_unpack_to_device(
    xx_logitechcompress *archive, xx_io_device *destination,
    xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_logitechcompress_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_logitechcompress_get_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state);
XXFC_API bool xx_logitechcompress_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_logitechcompress_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_logitechcompress_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_logitechcompress_get_uncompressed_size(
    const xx_logitechcompress *archive);
XXFC_API int64_t xx_logitechcompress_get_stream_end(
    const xx_logitechcompress *archive);

static inline Abstractformat *xx_logitechcompress_to_format(
    xx_logitechcompress *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LOGITECHCOMPRESS_H */
