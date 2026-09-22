/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rsvk.h @brief "RSVKDATA" / "DLIBDATA" container reader. */

#ifndef XXFCLIB_FORMAT_RSVK_H
#define XXFCLIB_FORMAT_RSVK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RSVKDATA archive: a "RSVK"/"DLIB" + "DATA" header, a run of
 * member block chains, a trailing directory of 28-byte entries each followed
 * by a NUL-terminated DOS path, and a 12-byte "ECDR"/"DEND" trailer whose
 * last field points back at the directory.
 */
typedef struct xx_rsvk {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t directory_offset; /**< absolute offset of the directory, -1 if unknown */
    int64_t directory_size;   /**< directory bytes, trailer excluded */
} xx_rsvk;

typedef xx_rsvk xx_rsvk_t;

XXFC_API void xx_rsvk_init(xx_rsvk *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_rsvk *xx_rsvk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rsvk_destroy(xx_rsvk *archive);
XXFC_API void xx_rsvk_free(xx_rsvk *archive);

XXFC_API bool xx_rsvk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rsvk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rsvk_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_rsvk_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rsvk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rsvk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rsvk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rsvk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rsvk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RSVK_H */
