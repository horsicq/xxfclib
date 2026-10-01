/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_htc_nbh_rom_image.h @brief HTC NBH (signed ROM update) reader. */

#ifndef XXFCLIB_FORMAT_HTC_NBH_ROM_IMAGE_H
#define XXFCLIB_FORMAT_HTC_NBH_ROM_IMAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * An HTC .nbh is the ROM image that HTC's ROM Update Utility flashes onto
 * Windows Mobile (and early Android) phones.  It is two layers:
 *
 * Outer layer - the signed block stream (all integers little endian):
 *
 *   +0x00  char[7]  "R000FF\n"
 *   +0x07  u8[16]   initial signature
 *   then blocks, until one whose flag is 2:
 *     +0  u32  data length
 *     +4  u32  signature length   (128 in the tools that write it)
 *     +8  u8   flag               1 = more blocks follow, 2 = last block
 *     +9  data[data length], then signature[signature length]
 *
 *   The concatenated block data is the "DBH" image; the writers cut it into
 *   64 KiB or 1 MiB blocks and close with an empty flag-2 block.
 *
 * Inner layer - the DBH image, a 0x200-byte header then the sections:
 *
 *   +0x000  u32[8]   'H','T','C','I','M','A','G','E', one character per
 *                    32-bit word (bytes 48 00 00 00 54 00 00 00 ...)
 *   +0x020  char[32] device (model id)
 *   +0x040  u32[32]  section types (0 = unused slot)
 *   +0x0C0  u32[32]  section offsets, from the start of the DBH image
 *   +0x140  u32[32]  section lengths
 *   +0x1C0  char[32] CID
 *   +0x1E0  char[16] version
 *   +0x1F0  char[16] language
 *
 * Each used slot i becomes one member named "%02d_%s.nb" after its type
 * (0x100 IPL, 0x101 G3IPL, 0x102 G4IPL, 0x200 SPL, 0x201 G3SPL, 0x202 G4SPL,
 * 0x300 GSM, 0x400 OS, 0x600 MainSplash, 0x601 SubSplash, 0x700/0x900
 * ExtROM, anything else Unknown), which is what NBHextract 1.0 writes.  A
 * section's bytes may straddle several signed blocks; they are reassembled
 * without the interleaved block headers and signatures.  The signatures are
 * not verified (no key is public).
 */

/** Longest block chain accepted (64 KiB blocks: 16 GiB of image). */
#define XX_HTC_NBH_ROM_IMAGE_MAX_BLOCKS 262144U
/** Slots in the DBH section table. */
#define XX_HTC_NBH_ROM_IMAGE_MAX_SECTIONS 32U

typedef struct xx_htc_nbh_rom_image {
    Abstractformat format;
    void *internal; /**< Parsed block map and section table. */
    uint64_t number_of_records;
    uint64_t number_of_blocks;
    int64_t image_size;  /**< Bytes of reassembled DBH image. */
    int64_t archive_end; /**< Absolute end of the last parsed block. */
    bool truncated;      /**< No flag-2 block before the end of the input. */
    char device[33];
    char cid[33];
    char version[17];
    char language[17];
} xx_htc_nbh_rom_image;

typedef xx_htc_nbh_rom_image xx_htc_nbh_rom_image_t;

XXFC_API void xx_htc_nbh_rom_image_init(xx_htc_nbh_rom_image *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_htc_nbh_rom_image *xx_htc_nbh_rom_image_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_htc_nbh_rom_image_destroy(xx_htc_nbh_rom_image *archive);
XXFC_API void xx_htc_nbh_rom_image_free(xx_htc_nbh_rom_image *archive);

XXFC_API bool xx_htc_nbh_rom_image_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_htc_nbh_rom_image_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_htc_nbh_rom_image_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_htc_nbh_rom_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_htc_nbh_rom_image_create_archive_records_reading(Abstractformat *self,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_htc_nbh_rom_image_get_current_archive_record(Abstractformat *self,
                                                xx_archive_record_state *state);
XXFC_API bool xx_htc_nbh_rom_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_htc_nbh_rom_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_htc_nbh_rom_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HTC_NBH_ROM_IMAGE_H */
