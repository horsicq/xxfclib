/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_frontpagetheme.h @brief FrontPage theme package reader. */

#ifndef XXFCLIB_FORMAT_FRONTPAGETHEME_H
#define XXFCLIB_FORMAT_FRONTPAGETHEME_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Microsoft FrontPage theme package.\n *\n * A text directory naming every member and its size, followed by the payloads in the same order, each introduced by a fourteen-byte marker. Members are stored verbatim; there is no compression and no trailer.
 */
typedef struct xx_frontpagetheme {
    Abstractformat format;
    uint64_t number_of_records;
} xx_frontpagetheme;

typedef xx_frontpagetheme xx_frontpagetheme_t;

XXFC_API void xx_frontpagetheme_init(xx_frontpagetheme *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_frontpagetheme *xx_frontpagetheme_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_frontpagetheme_destroy(xx_frontpagetheme *archive);
XXFC_API void xx_frontpagetheme_free(xx_frontpagetheme *archive);

XXFC_API bool xx_frontpagetheme_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_frontpagetheme_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_frontpagetheme_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_frontpagetheme_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_frontpagetheme_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_frontpagetheme_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_frontpagetheme_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_frontpagetheme_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_frontpagetheme_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FRONTPAGETHEME_H */
