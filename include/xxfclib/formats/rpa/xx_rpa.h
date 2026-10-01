/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rpa.h @brief Ren'Py RPA game archive reader. */

#ifndef XXFCLIB_FORMAT_RPA_H
#define XXFCLIB_FORMAT_RPA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Ren'Py archive (.rpa).
 *
 * One ASCII header line, the member data, and a zlib-compressed Python
 * pickle index at the offset the header names:
 *
 *   "RPA-2.0 <offset>\n"                 no key
 *   "RPA-3.0 <offset> <key>\n"           offsets and lengths XOR key
 *   "RPA-4.0 <offset> <key>\n"           as 3.0
 *   "RPA-3.2 <offset> <key> <extra>\n"   as 3.0; later fields are ignored
 *   "ALT-1.0 <key ^ 0xDABE8DF0> <offset>\n"
 *
 * Fields are hexadecimal.  The index unpickles to a dict
 * { name: [ (offset, length) | (offset, length, prefix), ... ] }; only the
 * first tuple of each list is used, exactly as Ren'Py's loader does.  A
 * member's bytes are the prefix followed by length - len(prefix) bytes at
 * offset.
 *
 * The pickle is decoded by a data-only interpreter: it builds dicts, lists,
 * tuples, ints and strings and knows exactly two callables,
 * builtins.bytes() (empty bytes) and _codecs.encode(text, "latin1"), which
 * is how Python 3 pickles bytes at protocol 2.  Nothing is ever executed.
 *
 * RPA-1.0 (a data file plus a separate .rpi index) and the ZiX-12A/12B
 * obfuscated variants (which need the game's own loader code) are not
 * handled.
 */
typedef struct xx_rpa {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t index_offset;
    uint64_t key;
    uint32_t version; /**< 20, 30, 32, 40, or 1 for ALT-1.0. */
} xx_rpa;

typedef xx_rpa xx_rpa_t;

XXFC_API void xx_rpa_init(xx_rpa *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_rpa *xx_rpa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rpa_destroy(xx_rpa *archive);
XXFC_API void xx_rpa_free(xx_rpa *archive);

XXFC_API bool xx_rpa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rpa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rpa_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_rpa_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rpa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rpa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rpa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rpa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rpa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RPA_H */
