/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfark_compressed_soundfont.h @brief sfArk v2 compressed SoundFont reader. */

#ifndef XXFCLIB_FORMAT_SFARK_COMPRESSED_SOUNDFONT_H
#define XXFCLIB_FORMAT_SFARK_COMPRESSED_SOUNDFONT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An sfArk v2 file: one SoundFont (.sf2) compressed by Melody
 * Machine's sfArk, plus an optional notes and licence text.
 *
 *   0x00  u32 flags        bit0 notes text present, bit1 licence present
 *   0x04  u32 original size of the SoundFont
 *   0x08  u32 compressed size (informational)
 *   0x0C  u32 file check   running check over the whole output
 *   0x10  u32 header check Adler-32 (seed 0) of these 42 bytes with this
 *                          field zeroed, continued over the file name + NUL
 *   0x14  u8  version needed (21 = 2.1)
 *   0x15  char[5] program version
 *   0x1A  char[5] "sfArk"
 *   0x1F  u8  method: 4 fast, 5 standard, 6 LPC order 8, 7 LPC order 128
 *   0x20  u16 file type
 *   0x22  u32 audio start      first byte of the 16-bit sample data
 *   0x26  u32 post-audio start first byte after it
 *   0x2A  file name, NUL terminated
 *   then  [u32 length + zlib stream] licence text   (flags bit1)
 *         [u32 length + zlib stream] notes text     (flags bit0)
 *   then  a bit stream (16-bit little-endian words, MSB first) carrying
 *         zlib blocks for the bytes before and after the sample data and
 *         Rice-coded, differenced / LPC-predicted sample chunks between.
 *
 * sfArk v1 files (a different, proprietary container) are not handled.
 */
typedef struct xx_sfark_compressed_soundfont {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;   /**< Original SoundFont size from the header. */
    uint32_t method;          /**< 4..7. */
} xx_sfark_compressed_soundfont;

typedef xx_sfark_compressed_soundfont xx_sfark_compressed_soundfont_t;

XXFC_API void xx_sfark_compressed_soundfont_init(
    xx_sfark_compressed_soundfont *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfark_compressed_soundfont *xx_sfark_compressed_soundfont_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfark_compressed_soundfont_destroy(
    xx_sfark_compressed_soundfont *archive);
XXFC_API void xx_sfark_compressed_soundfont_free(
    xx_sfark_compressed_soundfont *archive);

XXFC_API bool xx_sfark_compressed_soundfont_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfark_compressed_soundfont_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfark_compressed_soundfont_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sfark_compressed_soundfont_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfark_compressed_soundfont_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfark_compressed_soundfont_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfark_compressed_soundfont_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfark_compressed_soundfont_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfark_compressed_soundfont_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFARK_COMPRESSED_SOUNDFONT_H */
