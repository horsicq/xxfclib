/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sfx_abbyy_fine_objects.h
 * @brief ABBYY "Fine Objects" setup / patch executables (Lingvo 5, 1999).
 *
 * A Win32 PE stub (the ABBYY Fine Objects updater) with the payload
 * appended at the exact end of the last section's raw data.  Offsets below
 * are relative to that overlay; integers are little-endian and every string
 * is a Pascal string (one length byte, no terminator, Windows-1251).
 *
 *   +0      u32   n, member count
 *           n     member names (non-empty, no byte below 0x20)
 *           u32   n again
 *           n     u32 stream sizes
 *           u32   m, count of window classes the updater looks for
 *           m     window-class names (e.g. "FineReaderMainWindowClass")
 *           3     strings: patch title, target product, program name
 *           12    two u32 dates (Unix time, local midnight) and a u32 0
 *   S       n     member streams back to back, each a complete FINEAR
 *                 stream of the listed size:
 *                   "FINEAR" dd 88 dd, u32 CRC-16/ARC of the plaintext,
 *                   u32 plaintext size, LHA -lh1- (LZHUF) body
 *   S+sum   19    trailer: u32 overlay offset (the stub size), then
 *                 "ArcUpdateABBYY" and a NUL
 *
 * The trailer normally ends the file.  Members are listed under their
 * stored names ('\' becomes '/') and extracted decoded; a member is written
 * only when its body decodes to exactly the stored size with the stored
 * CRC-16/ARC.
 */

#ifndef XXFCLIB_FORMAT_SFX_ABBYY_FINE_OBJECTS_H
#define XXFCLIB_FORMAT_SFX_ABBYY_FINE_OBJECTS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sfx_abbyy_fine_objects {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t overlay_offset; /**< Payload start (member count), from the base. */
    int64_t streams_offset; /**< First FINEAR stream, from the base. */
    int64_t trailer_offset; /**< The 19-byte trailer, from the base. */
    uint32_t class_count;   /**< Window-class names in the header. */
} xx_sfx_abbyy_fine_objects;

typedef xx_sfx_abbyy_fine_objects xx_sfx_abbyy_fine_objects_t;

XXFC_API void xx_sfx_abbyy_fine_objects_init(
    xx_sfx_abbyy_fine_objects *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_abbyy_fine_objects *xx_sfx_abbyy_fine_objects_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_abbyy_fine_objects_destroy(
    xx_sfx_abbyy_fine_objects *archive);
XXFC_API void xx_sfx_abbyy_fine_objects_free(
    xx_sfx_abbyy_fine_objects *archive);

XXFC_API bool xx_sfx_abbyy_fine_objects_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_sfx_abbyy_fine_objects_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_abbyy_fine_objects_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_abbyy_fine_objects_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_abbyy_fine_objects_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_abbyy_fine_objects_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_abbyy_fine_objects_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_abbyy_fine_objects_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_abbyy_fine_objects_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_ABBYY_FINE_OBJECTS_H */
