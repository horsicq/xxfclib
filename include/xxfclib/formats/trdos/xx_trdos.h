/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_trdos.h @brief TR-DOS (.TRD) ZX Spectrum Beta Disk image reader. */

#ifndef XXFCLIB_FORMAT_TRDOS_H
#define XXFCLIB_FORMAT_TRDOS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A TR-DOS disk image: the raw sector dump of a ZX Spectrum Beta Disk
 * floppy.  Track 0 sectors 0..7 - file bytes 0x000..0x7FF - hold a catalogue of
 * 128 sixteen-byte records; the TR-DOS disk descriptor in sector 8 identifies
 * the image.  Members are stored as plain 256-byte sectors and are published
 * as Hobeta files: a synthesised 17-byte header in front of those sectors.
 */
typedef struct xx_trdos {
    Abstractformat format;
    uint64_t number_of_records;
    uint8_t disk_type;
} xx_trdos;

typedef xx_trdos xx_trdos_t;

XXFC_API void xx_trdos_init(xx_trdos *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_trdos *xx_trdos_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_trdos_destroy(xx_trdos *archive);
XXFC_API void xx_trdos_free(xx_trdos *archive);

XXFC_API bool xx_trdos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_trdos_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_trdos_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_trdos_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_trdos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_trdos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_trdos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_trdos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_trdos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TRDOS_H */
