/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_winlink.h @brief WinLink packed-file ("02 00 00") reader. */

#ifndef XXFCLIB_FORMAT_WINLINK_H
#define XXFCLIB_FORMAT_WINLINK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WinLink packed file.
 *
 * One container holds exactly one member.  The 24-byte header carries a
 * three-byte constant, an MS-DOS time/date pair, a fixed 13-byte 8.3 name
 * field and a 0xFFFFFFFF sentinel; the payload runs from offset 24 to the
 * end of the file and is LZW - MSB-first, 9 to 14 bits, early width change,
 * CLEAR 0x100, END 0x101, first assignable code 0x102.
 *
 * No plaintext length is recorded anywhere in the container, so the member's
 * uncompressed size is measured by running the stream once when the records
 * are created.
 */
typedef struct xx_winlink {
    Abstractformat format;
    uint64_t number_of_records;
    /** MS-DOS packed date/time, date in the high word. */
    uint32_t timestamp;
} xx_winlink;

typedef xx_winlink xx_winlink_t;

XXFC_API void xx_winlink_init(xx_winlink *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_winlink *xx_winlink_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_winlink_destroy(xx_winlink *archive);
XXFC_API void xx_winlink_free(xx_winlink *archive);

XXFC_API bool xx_winlink_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_winlink_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_winlink_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_winlink_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_winlink_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_winlink_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_winlink_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_winlink_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_winlink_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WINLINK_H */
