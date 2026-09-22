/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_unixcompact.h @brief Unix compact(1) adaptive Huffman reader. */

#ifndef XXFCLIB_FORMAT_UNIXCOMPACT_H
#define XXFCLIB_FORMAT_UNIXCOMPACT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_unixcompact {
    Abstractformat format;
    int64_t stream_offset;
    int64_t stream_size;
} xx_unixcompact;

typedef xx_unixcompact xx_unixcompact_t;
typedef xx_unixcompact XUnixCompact;

XXFC_API void xx_unixcompact_init(xx_unixcompact *archive,
                                  xx_io_device *device,
                                  int64_t base_address);
XXFC_API xx_unixcompact *xx_unixcompact_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_unixcompact_destroy(xx_unixcompact *archive);
XXFC_API void xx_unixcompact_free(xx_unixcompact *archive);

XXFC_API bool xx_unixcompact_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_unixcompact_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_unixcompact_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_unixcompact_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_unixcompact_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_unixcompact_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_unixcompact_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_unixcompact_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_unixcompact_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Offset of the coded stream (just past the two magic bytes). */
XXFC_API int64_t xx_unixcompact_get_stream_offset(
    const xx_unixcompact *archive);
/** Size of the coded stream in bytes. */
XXFC_API int64_t xx_unixcompact_get_stream_size(const xx_unixcompact *archive);

static inline Abstractformat *xx_unixcompact_to_format(
    xx_unixcompact *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UNIXCOMPACT_H */
