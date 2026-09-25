/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sar_ns.h @brief NScripter SAR archive (arc.sar) reader. */

#ifndef XXFCLIB_FORMAT_SAR_NS_H
#define XXFCLIB_FORMAT_SAR_NS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An NScripter / ONScripter SAR resource archive.
 *
 * Every field is BIG endian:
 *   0x00  u16 number of entries (1..65535)
 *   0x02  u32 base: absolute offset of the data area, which is also the
 *         end of the index
 *   0x06  the index, one entry per member:
 *           char[] name, NUL terminated; Shift-JIS bytes with '\\' as the
 *                  directory separator
 *           u32    data offset, relative to base
 *           u32    data size
 *   base  the member data
 *
 * Members are stored; the format has no codec field (that is the NSA
 * variant's extra byte), no timestamps and no directory entries.
 *
 * There is no magic, so the index walk is also the detection probe (run
 * late, after every signature-gated format): the base must lie inside the
 * file and leave room for the index; every name is 1..1024 bytes with no
 * control byte; the entries must end exactly at base; the members must be
 * in ascending order without overlapping; and the last member must end
 * exactly at EOF.  Garbage fails on the six header bytes or on the first
 * entry.  The index is walked through a 64 KiB window and never loaded
 * whole, so a probe costs at most one window allocation.
 *
 * Member names are made safe and unique before anything is written:
 *   - '\\' and '/' become '/';
 *   - a Shift-JIS double-byte character becomes "%XX%XX" and any other byte
 *     >= 0x80 becomes "%XX" (upper-case hex), so a trail byte of 0x5C or
 *     0x7C is never taken for a separator or a '|';
 *   - a literal '%' becomes "%25", which keeps the escaping reversible;
 *   - a name that repeats an earlier one (compared case-insensitively) gets
 *     "%_<entry index>" inserted before its extension.  Since every '%' in a
 *     converted name is followed by two hex digits, "%_" occurs only in
 *     renamed members, which therefore never collide with anything;
 *   - a name with an empty, ".", ".." or trailing-dot/space component, a
 *     Windows device name, or one of : < > " | ? * is listed but refused on
 *     extraction.
 */
typedef struct xx_sar_ns {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t data_base; /**< The base field: the index ends and data starts. */
} xx_sar_ns;

typedef xx_sar_ns xx_sar_ns_t;

XXFC_API void xx_sar_ns_init(xx_sar_ns *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_sar_ns *xx_sar_ns_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_sar_ns_destroy(xx_sar_ns *archive);
XXFC_API void xx_sar_ns_free(xx_sar_ns *archive);

XXFC_API bool xx_sar_ns_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sar_ns_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sar_ns_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sar_ns_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sar_ns_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sar_ns_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sar_ns_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sar_ns_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sar_ns_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SAR_NS_H */
