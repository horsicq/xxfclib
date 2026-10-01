/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_oberon.h @brief Oberon installer .Z archive.
 * A sequence of fixed 136-byte file headers, each followed by a bounded
 * Microsoft SZDD stream or no bytes for an empty member. Filenames occupy
 * the first 64 header bytes and are converted from DOS separators.
 * Extraction verifies that the SZDD decoder consumed the complete declared
 * payload. Limits: 1 GiB archive, 10000 members, 64 MiB per decoded file.
 */
#ifndef XXFCLIB_FORMAT_OBERON_H
#define XXFCLIB_FORMAT_OBERON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_oberon {
    Abstractformat format;
    uint64_t number_of_records;
} xx_oberon;
typedef xx_oberon xx_oberon_t;
XXFC_API void xx_oberon_init(xx_oberon *, xx_io_device *, int64_t);
XXFC_API xx_oberon *xx_oberon_create(xx_io_device *, int64_t);
XXFC_API void xx_oberon_destroy(xx_oberon *);
XXFC_API void xx_oberon_free(xx_oberon *);
XXFC_API bool xx_oberon_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_oberon_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_oberon_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_oberon_get_number_of_archive_records(Abstractformat *,
                                                            xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_oberon_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_oberon_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_oberon_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_oberon_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_oberon_free_archive_records_reading(
    Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_oberon_to_format(xx_oberon *archive) {
    return archive ? &archive->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
