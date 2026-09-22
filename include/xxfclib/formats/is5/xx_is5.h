/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_IS5_H
#define XXFCLIB_FORMAT_IS5_H

#include "xxfclib/formats/xx_format.h"

/* InstallShield 5 and later cabinets ("ISc(").  A set is either one .cab that
 * carries both the descriptor and the payload, or a .hdr that carries only the
 * descriptor with the payload in a sibling .cab.  Both are parsed; a member
 * whose payload is outside the opened file is listed but cannot be
 * unpacked. */
typedef struct xx_is5 {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t major_version;
} xx_is5;

XXFC_API void xx_is5_init(xx_is5 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_is5 *xx_is5_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_is5_destroy(xx_is5 *archive);
XXFC_API void xx_is5_free(xx_is5 *archive);
XXFC_API bool xx_is5_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_is5_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_is5_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_is5_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_is5_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_is5_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_is5_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_is5_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_is5_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
