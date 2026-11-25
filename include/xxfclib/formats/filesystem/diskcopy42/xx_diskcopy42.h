/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_diskcopy42.h @brief Apple DiskCopy 4.2 floppy image reader.
 *
 * Accepts exact 400/800 KiB GCR and 720/1440 KiB MFM images, with absent or
 * full 12-byte-per-block tag streams for GCR media. It checks both Apple
 * rolling checksums before publishing a view. A ProDOS volume in the data
 * fork is listed and extracted directly within that bounded fork. Other
 * images retain separate raw disk and tag members.
 * The 84-byte header and full data/tag scan are bounded by the standard
 * floppy geometry and outside the extraction MEMORY_LIMIT operation budget.
 */
#ifndef XXFCLIB_FORMAT_DISKCOPY42_H
#define XXFCLIB_FORMAT_DISKCOPY42_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_diskcopy42 {
    Abstractformat format;
    uint32_t number_of_members;
    void *internal;
} xx_diskcopy42;
typedef xx_diskcopy42 xx_diskcopy42_t;
XXFC_API void xx_diskcopy42_init(xx_diskcopy42 *, xx_io_device *, int64_t);
XXFC_API xx_diskcopy42 *xx_diskcopy42_create(xx_io_device *, int64_t);
XXFC_API void xx_diskcopy42_destroy(xx_diskcopy42 *);
XXFC_API void xx_diskcopy42_free(xx_diskcopy42 *);
XXFC_API bool xx_diskcopy42_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_diskcopy42_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_diskcopy42_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_diskcopy42_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_diskcopy42_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_diskcopy42_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_diskcopy42_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_diskcopy42_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_diskcopy42_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_diskcopy42_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_diskcopy42_test_magic(const uint8_t *, size_t);
static inline Abstractformat *xx_diskcopy42_to_format(xx_diskcopy42 *d)
{
    return d ? &d->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
