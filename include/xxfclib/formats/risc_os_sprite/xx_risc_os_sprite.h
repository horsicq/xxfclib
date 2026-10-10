/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_risc_os_sprite.h @brief RISC OS (Acorn) sprite file reader. */

#ifndef XXFCLIB_FORMAT_RISC_OS_SPRITE_H
#define XXFCLIB_FORMAT_RISC_OS_SPRITE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A RISC OS sprite file (filetype &FF9): a saved sprite area.
 *
 * The file is the in-memory sprite area without its leading size word, so
 * every offset stored in it is relative to a point four bytes BEFORE the
 * file start.  All fields are little-endian u32.
 *
 *   0x00  number of sprites
 *   0x04  offset of the first sprite (+4; normally 16, 32 with extension
 *         words)
 *   0x08  offset of the first free word (+4) = file size + 4
 *
 * Each sprite is a 44-byte header followed by its optional palette, image
 * and mask:
 *
 *   +0x00  offset to the next sprite (= this sprite's size)
 *   +0x04  char[12] name, NUL padded
 *   +0x10  width in 32-bit words - 1
 *   +0x14  height in rows - 1
 *   +0x18  first bit used in each row (left padding bits)
 *   +0x1C  last bit used in the last word of each row
 *   +0x20  image offset, from the sprite start
 *   +0x24  mask offset, from the sprite start (= image offset: no mask)
 *   +0x28  mode: an old screen mode number (< 256), or a "new format"
 *          mode word whose bits 27..30 give the pixel type (1..6 = 1, 2, 4,
 *          8, 16, 32 bpp), bits 14..26 / 1..13 the dpi and bit 31 an 8-bit
 *          alpha mask
 *   +0x2C  palette: pairs of colour words 0xBBGGRR00, up to the image
 *
 * Each sprite becomes one member.  A sprite whose pixel type this reader
 * decodes becomes "<name>.bmp" (24-bit, or 32-bit BITMAPV4 with the mask as
 * alpha); any other sprite is published verbatim as a one-sprite sprite file
 * "<name>,ff9".
 */
typedef struct xx_risc_os_sprite {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t declared_count; /**< The sprite count at 0x00. */
} xx_risc_os_sprite;

typedef xx_risc_os_sprite xx_risc_os_sprite_t;

/** Largest sprite count accepted (the field is a u32). */
#define XX_RISC_OS_SPRITE_MAX_SPRITES 10000U

XXFC_API void xx_risc_os_sprite_init(xx_risc_os_sprite *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_risc_os_sprite *xx_risc_os_sprite_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_risc_os_sprite_destroy(xx_risc_os_sprite *archive);
XXFC_API void xx_risc_os_sprite_free(xx_risc_os_sprite *archive);

XXFC_API bool xx_risc_os_sprite_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_risc_os_sprite_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_risc_os_sprite_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_risc_os_sprite_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_risc_os_sprite_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_risc_os_sprite_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_risc_os_sprite_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_risc_os_sprite_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_risc_os_sprite_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Cheap test of the first bytes of a candidate (the detector's
 *        prefilter): the area header and the first sprite header.
 *
 * @param magic       the first @p magic_size bytes of the candidate
 * @param magic_size  bytes available (at most 64 are looked at)
 * @param total_size  size of the candidate
 */
XXFC_API bool xx_risc_os_sprite_prefilter(const uint8_t *magic, size_t magic_size, int64_t total_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RISC_OS_SPRITE_H */
