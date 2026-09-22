/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sco.h @brief SCO UNIX "compress -H" (LZH) stream reader. */

#ifndef XXFCLIB_FORMAT_SCO_H
#define XXFCLIB_FORMAT_SCO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sco {
    Abstractformat format;
    /* The 16-bit symbol count of the stream's first -lh5- block, which sits
     * immediately behind the magic.  It belongs to the coded stream rather
     * than to a header, and is kept only because it is cheap to expose. */
    uint32_t header_word;
    int64_t stream_offset;
    int64_t stream_size;
} xx_sco;

typedef xx_sco xx_sco_t;
typedef xx_sco XSco;

XXFC_API void xx_sco_init(xx_sco *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_sco *xx_sco_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sco_destroy(xx_sco *archive);
XXFC_API void xx_sco_free(xx_sco *archive);

XXFC_API bool xx_sco_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sco_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sco_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_sco_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sco_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sco_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sco_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sco_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sco_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Symbol count of the first coded block, the 16-bit word at offset 2. */
XXFC_API uint32_t xx_sco_get_header_word(const xx_sco *archive);
/** Offset of the coded stream (just past the two magic bytes). */
XXFC_API int64_t xx_sco_get_stream_offset(const xx_sco *archive);
/** Size of the coded stream in bytes. */
XXFC_API int64_t xx_sco_get_stream_size(const xx_sco *archive);

static inline Abstractformat *xx_sco_to_format(xx_sco *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SCO_H */
