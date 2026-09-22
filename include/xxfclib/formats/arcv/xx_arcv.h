/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_ARCV_H
#define XXFCLIB_FORMAT_ARCV_H

#include "xxfclib/formats/xx_format.h"

/* Eschalon Setup ARCV 1.10.  Unlike ARCV 2.00 (block chain) and ARCV 4.00
 * (0x79C directory header) a 1.10 archive holds exactly one member: a single
 * file descriptor followed by one "CHNK" segment.  The payload is a
 * Yoshizaki LZHUF stream in one of two sub-variants, selected by whether the
 * header JAMCRC covers the packed or the unpacked bytes. */
typedef struct xx_arcv {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    uint32_t raw_size;
    uint32_t packed_size;
    uint32_t jam_crc;
    bool wide; /* true = stock F=60 variant, false = compact F=32 variant */
} xx_arcv;

XXFC_API void xx_arcv_init(xx_arcv *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_arcv *xx_arcv_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_arcv_destroy(xx_arcv *archive);
XXFC_API void xx_arcv_free(xx_arcv *archive);
XXFC_API bool xx_arcv_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_arcv_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_arcv_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_arcv_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_arcv_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_arcv_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arcv_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arcv_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arcv_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#endif
