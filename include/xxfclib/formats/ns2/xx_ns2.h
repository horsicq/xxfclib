/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ns2.h @brief NScripter NS2 archive (*.ns2) reader. */

#ifndef XXFCLIB_FORMAT_NS2_H
#define XXFCLIB_FORMAT_NS2_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An NScripter2 (NS2) resource archive, unencrypted.
 *
 * Every field is LITTLE endian:
 *   0x00  u32 base: absolute offset of the data area, which is also the end
 *         of the index
 *   0x04  the index, one entry per member:
 *           '"'   opening quote
 *           char[] name, Shift-JIS bytes with '\\' as the directory separator
 *           '"'   closing quote
 *           u32   data size
 *         then a single 'e' byte, the last byte before base
 *   base  the member data, back to back in index order, up to EOF
 *
 * Members are stored; the container has no codec field, no offsets (each
 * member starts where the previous one ended), no timestamps and no
 * directory entries.  Password-encrypted NS2 archives (the index is
 * encrypted too) are not recognised.
 *
 * There is no magic, so the index walk is also the detection probe: base
 * lies inside the file, leaves room for one entry and the terminator and is
 * at most 16 MiB + 4; every name is 1..1024 bytes of no control byte; there
 * are at most 65535 entries; the index ends with 'e' exactly at base - 1;
 * and the sizes add up exactly to EOF - base.  Garbage fails on the first
 * five bytes or on the first entry.  The index is walked through a bounded
 * window and never loaded whole.
 *
 * Member names are made safe and unique before anything is written (same
 * rules as the SAR and NSA readers):
 *   - '\\' and '/' become '/';
 *   - a Shift-JIS double-byte character becomes "%XX%XX" and any other byte
 *     >= 0x80 becomes "%XX" (upper-case hex);
 *   - a literal '%' becomes "%25";
 *   - a name that repeats an earlier one (compared case-insensitively) gets
 *     "%_<entry index>" inserted before its extension;
 *   - a name with an empty, ".", ".." or trailing-dot/space component, a
 *     Windows device name, or one of : < > | ? * is listed but refused on
 *     extraction.
 */
typedef struct xx_ns2 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t data_base; /**< The base field: the index ends and data starts. */
} xx_ns2;

typedef xx_ns2 xx_ns2_t;

XXFC_API void xx_ns2_init(xx_ns2 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_ns2 *xx_ns2_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ns2_destroy(xx_ns2 *archive);
XXFC_API void xx_ns2_free(xx_ns2 *archive);

XXFC_API bool xx_ns2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ns2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ns2_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ns2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ns2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ns2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ns2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ns2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ns2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ns2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ns2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ns2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ns2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ns2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_NS2_H */
