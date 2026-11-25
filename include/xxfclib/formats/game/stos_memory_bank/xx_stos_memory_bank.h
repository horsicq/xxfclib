/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_stos_memory_bank.h @brief STOS Basic memory bank (.MBK) reader. */

#ifndef XXFCLIB_FORMAT_STOS_MEMORY_BANK_H
#define XXFCLIB_FORMAT_STOS_MEMORY_BANK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A memory bank saved by STOS Basic (Atari ST), usually "*.MBK".
 *
 * All fields are big-endian.  The MBK file is an 18-byte header and the
 * bank's contents:
 *
 *   0x00  "Lionpoubnk"
 *   0x0A  u32 bank number (1..15; 0 marks an .MBS "all banks" file, which
 *         this reader does not take)
 *   0x0E  u8  bank type (0x01 work, 0x02 screen, 0x81 data, 0x82 data
 *         screen, 0x84 set, 0x85 packed files)
 *   0x0F  u24 bank size.  STOS writes it rounded up to 256 and truncated
 *         to 16 bits, so it is not used to delimit the bank.
 *   0x12  bank contents
 *
 * A data bank starts with a u32 bank id.  Two are decoded:
 *
 *   0x19861987  sprite bank:
 *     +0x04  u32[3] offsets of the low/med/high resolution parameter
 *            blocks, relative to +0x04
 *     +0x10  u16[3] sprite counts for the three resolutions
 *     +0x16  one 8-byte parameter block per sprite, then "PALT" and 16
 *            u16 Atari ST palette words
 *     parameter block: u32 data offset (relative to its resolution's
 *     parameter-block start), u8 width in 16-pixel words, u8 height,
 *     u8 hot-spot x, u8 hot-spot y.  The data is a 1-plane mask
 *     (bit set = transparent) followed by the 4-, 2- or 1-plane image,
 *     both in the ST's word-interleaved plane layout.
 *   0x28091960  icon bank: u16 icon count at +0x04, then 84-byte icons
 *     from +0x06 (16x16, one word of mask and one of image per row from
 *     +0x0A).
 *
 * A sprite bank is also accepted on its own, without the MBK header (the
 * same layout from offset 0), when its palette block is present.
 *
 * Members: every sprite becomes "sprite_<low|med|high>_<n>.bmp" and every
 * icon "icon_<n>.bmp" (32-bit BITMAPV4 with the mask as alpha).  Any other
 * bank, or a sprite/icon bank none of whose images can be placed inside the
 * file, is published verbatim as "bank<nn>_<kind>.bin".
 */
typedef struct xx_stos_memory_bank {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t bank_number; /**< 0x0A of the MBK header (0 = bare sprite bank). */
    uint32_t bank_type;   /**< 0x0E of the MBK header (0 = bare sprite bank). */
    uint32_t bank_id;     /**< First u32 of a data bank, else 0. */
} xx_stos_memory_bank;

typedef xx_stos_memory_bank xx_stos_memory_bank_t;

/** Largest number of members accepted (three u16 sprite counts). */
#define XX_STOS_MEMORY_BANK_MAX_RECORDS 65535U

XXFC_API void xx_stos_memory_bank_init(xx_stos_memory_bank *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_stos_memory_bank *xx_stos_memory_bank_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_stos_memory_bank_destroy(xx_stos_memory_bank *archive);
XXFC_API void xx_stos_memory_bank_free(xx_stos_memory_bank *archive);

XXFC_API bool xx_stos_memory_bank_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_stos_memory_bank_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_stos_memory_bank_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_stos_memory_bank_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_stos_memory_bank_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_stos_memory_bank_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_stos_memory_bank_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_stos_memory_bank_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_stos_memory_bank_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_STOS_MEMORY_BANK_H */
