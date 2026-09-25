/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_microfox_put.h @brief MicroFox PUT archive reader. */

#ifndef XXFCLIB_FORMAT_MICROFOX_PUT_H
#define XXFCLIB_FORMAT_MICROFOX_PUT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A MicroFox PUT archive (PUT.EXE / GET.EXE / the MicroFox Install
 * Program, Jim Hass, 1990-1993; usual extensions .PUT and .INS).
 *
 * The container is the LHA level-0 / level-1 member chain with PUT's own
 * method tags, so ordinary LHA tools do not recognise it:
 *
 *   -lZ0-  stored
 *   -lZ1-  the -lh1- bitstream (4 KiB LZSS, adaptive Huffman)
 *   -lZ5-  the -lh5- bitstream (8 KiB LZSS, static Huffman blocks)
 *
 * Levels 0 and 1 may alternate inside one archive. The chain ends at a single
 * 0x00 byte (or at EOF). See xx_microfox_put.c for the field table.
 */
typedef struct xx_microfox_put {
    Abstractformat format;
    uint64_t number_of_records;
} xx_microfox_put;

typedef xx_microfox_put xx_microfox_put_t;

XXFC_API void xx_microfox_put_init(xx_microfox_put *archive,
                                   xx_io_device *device, int64_t base_address);
XXFC_API xx_microfox_put *xx_microfox_put_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_microfox_put_destroy(xx_microfox_put *archive);
XXFC_API void xx_microfox_put_free(xx_microfox_put *archive);

XXFC_API bool xx_microfox_put_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_microfox_put_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_microfox_put_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_microfox_put_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_microfox_put_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_microfox_put_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_microfox_put_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_microfox_put_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_microfox_put_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MICROFOX_PUT_H */
