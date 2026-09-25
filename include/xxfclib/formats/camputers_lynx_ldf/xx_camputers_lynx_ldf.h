/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_camputers_lynx_ldf.h @brief Camputers Lynx .LDF floppy image. */

#ifndef XXFCLIB_FORMAT_CAMPUTERS_LYNX_LDF_H
#define XXFCLIB_FORMAT_CAMPUTERS_LYNX_LDF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Camputers Lynx disk image (.LDF, from the PALE emulator).
 *
 * The file has no header and no signature: it is the data of every sector,
 * 512 bytes each, sectors 1..10 of a track in ascending order, tracks in
 * cylinder order and, on a double-sided disk, head 0 then head 1 of the
 * same cylinder.  The geometry follows from the size alone:
 *
 *   204800 bytes   40 cylinders, 1 head   (200K, 48 tpi single sided)
 *   409600 bytes   80 cylinders, 1 head   (HxC only; MAME has no 400K form)
 *   819200 bytes   80 cylinders, 2 heads  (800K, 96 tpi double sided)
 *
 * Any other size is refused.  Lynx DOS keeps its catalogue as an ordinary
 * file in 256-byte blocks, but its allocation scheme is not documented, so
 * the reader stops at the sector level: one member per physical track,
 * named track<CC>_<H>.bin, holding that track's 10 sectors (5120 bytes).
 *
 * The size is the only evidence, and it is shared with many other raw
 * sector dumps (Kaypro, Opus DDOS, TIKI-100, Atari ST, 1581, ...), so the
 * reader is meant to be constructed by name, not auto-detected.
 */
typedef struct xx_camputers_lynx_ldf {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t cylinders;
    uint32_t heads;
} xx_camputers_lynx_ldf;

typedef xx_camputers_lynx_ldf xx_camputers_lynx_ldf_t;

#define XX_CAMPUTERS_LYNX_LDF_SECTOR_SIZE 512U
#define XX_CAMPUTERS_LYNX_LDF_SECTORS_PER_TRACK 10U
#define XX_CAMPUTERS_LYNX_LDF_TRACK_SIZE \
    (XX_CAMPUTERS_LYNX_LDF_SECTOR_SIZE * XX_CAMPUTERS_LYNX_LDF_SECTORS_PER_TRACK)

XXFC_API void xx_camputers_lynx_ldf_init(xx_camputers_lynx_ldf *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_camputers_lynx_ldf *xx_camputers_lynx_ldf_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_camputers_lynx_ldf_destroy(xx_camputers_lynx_ldf *archive);
XXFC_API void xx_camputers_lynx_ldf_free(xx_camputers_lynx_ldf *archive);

XXFC_API bool xx_camputers_lynx_ldf_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_camputers_lynx_ldf_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_camputers_lynx_ldf_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_camputers_lynx_ldf_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_camputers_lynx_ldf_create_archive_records_reading(Abstractformat *self,
                                                     const xx_list_s *options,
                                                     xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_camputers_lynx_ldf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_camputers_lynx_ldf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_camputers_lynx_ldf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_camputers_lynx_ldf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Geometry implied by the image size.
 *
 * @return false when the bytes from base_address to the end of the device
 *         are not exactly one of the three LDF sizes.
 */
XXFC_API bool xx_camputers_lynx_ldf_get_geometry(
    xx_camputers_lynx_ldf *archive, uint32_t *cylinders, uint32_t *heads);

/**
 * @brief Copy the 5120 bytes of one track into @p buffer.
 *
 * @param cylinder  0 .. cylinders - 1
 * @param head      0 .. heads - 1
 * @param buffer    receives XX_CAMPUTERS_LYNX_LDF_TRACK_SIZE bytes
 */
XXFC_API bool xx_camputers_lynx_ldf_read_track(xx_camputers_lynx_ldf *archive,
                                               uint32_t cylinder,
                                               uint32_t head, uint8_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CAMPUTERS_LYNX_LDF_H */
