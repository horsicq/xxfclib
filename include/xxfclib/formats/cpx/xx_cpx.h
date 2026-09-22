/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CPX_H
#define XXFCLIB_FORMAT_CPX_H

#include "xxfclib/formats/xx_format.h"

/* The ".CPX" distribution container, both of its shapes: the flat v1 directory
 * (stamp byte 0x28 / 0x2a) and the sectioned v4 one (stamp byte 0x2c).  Both
 * carry the same payload codec - MSB-first LZW, 9..14 bits. */
typedef struct xx_cpx {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint8_t container_version; /* the stamp byte: 0x28, 0x2a or 0x2c */
} xx_cpx;

XXFC_API void xx_cpx_init(xx_cpx *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_cpx *xx_cpx_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_cpx_destroy(xx_cpx *archive);
XXFC_API void xx_cpx_free(xx_cpx *archive);
XXFC_API bool xx_cpx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cpx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cpx_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_cpx_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_cpx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cpx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cpx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cpx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
