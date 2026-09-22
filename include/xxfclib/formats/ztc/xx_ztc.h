/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ztc.h @brief ZTC distribution archive reader. */

#ifndef XXFCLIB_FORMAT_ZTC_H
#define XXFCLIB_FORMAT_ZTC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Zortech C / Symantec C++ distribution archive: a 10 byte header followed by a chain of 0x12 byte records, each carrying its own length, a name, a four byte check word and an LZHUF payload stored in checksummed pages.
 */
typedef struct xx_ztc {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ztc;

typedef xx_ztc xx_ztc_t;

XXFC_API void xx_ztc_init(xx_ztc *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ztc *xx_ztc_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ztc_destroy(xx_ztc *archive);
XXFC_API void xx_ztc_free(xx_ztc *archive);

XXFC_API bool xx_ztc_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ztc_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ztc_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ztc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ztc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ztc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ztc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ztc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ztc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZTC_H */
