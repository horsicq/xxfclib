/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_debugscr.h @brief DOS DEBUG.EXE script reader. */

#ifndef XXFCLIB_FORMAT_DEBUGSCR_H
#define XXFCLIB_FORMAT_DEBUGSCR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DOS DEBUG.EXE script: plain ASCII "E" commands carrying the bytes
 * of one embedded binary, an "N" command carrying its name, and an RCX/W/Q
 * tail that states how long the file DEBUG would write is. The reader
 * reconstructs the binary from the text; nothing is ever executed.
 */
typedef struct xx_debugscr {
    Abstractformat format;
    uint64_t number_of_records;
} xx_debugscr;

typedef xx_debugscr xx_debugscr_t;

XXFC_API void xx_debugscr_init(xx_debugscr *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_debugscr *xx_debugscr_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_debugscr_destroy(xx_debugscr *archive);
XXFC_API void xx_debugscr_free(xx_debugscr *archive);

XXFC_API bool xx_debugscr_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_debugscr_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_debugscr_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_debugscr_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_debugscr_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_debugscr_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_debugscr_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_debugscr_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_debugscr_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DEBUGSCR_H */
