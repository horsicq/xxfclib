/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_apridisk.h @brief ApriDisk sector-record floppy image reader.
 *
 * Reconstructs a complete raw 512-byte-sector CHS image from raw and RLE
 * sector records, allowing creator/comment and obsolete deleted records.
 * Missing/duplicate active sectors, unknown encodings and nonuniform
 * geometry are rejected; missing sectors are never invented as zeros.
 * Supports cylinders 0..79, heads 0..1 and sector IDs 1..36, with an 8 MiB
 * source ceiling. MEMORY_LIMIT covers retained sector index and extraction
 * workspace; the initial bounded parser is outside that operation budget.
 */
#ifndef XXFCLIB_FORMAT_APRIDISK_H
#define XXFCLIB_FORMAT_APRIDISK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apridisk {
    Abstractformat format;
    uint32_t number_of_sectors;
    void *internal;
} xx_apridisk;
typedef xx_apridisk xx_apridisk_t;
XXFC_API void xx_apridisk_init(xx_apridisk *, xx_io_device *, int64_t);
XXFC_API xx_apridisk *xx_apridisk_create(xx_io_device *, int64_t);
XXFC_API void xx_apridisk_destroy(xx_apridisk *);
XXFC_API void xx_apridisk_free(xx_apridisk *);
XXFC_API bool xx_apridisk_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apridisk_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_apridisk_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_apridisk_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_apridisk_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_apridisk_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_apridisk_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_apridisk_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_apridisk_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_apridisk_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_apridisk_test_magic(const uint8_t *, size_t);
static inline Abstractformat *xx_apridisk_to_format(xx_apridisk *a) {
    return a ? &a->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
