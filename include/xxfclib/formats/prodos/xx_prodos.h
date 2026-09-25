/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_prodos.h @brief Apple ProDOS / SOS volume (block image) reader. */

#ifndef XXFCLIB_FORMAT_PRODOS_H
#define XXFCLIB_FORMAT_PRODOS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A ProDOS (ProDOS 8, ProDOS 16 / GS/OS) or Apple /// SOS volume
 *        stored as a raw image of 512-byte blocks.
 *
 * The image has no header of its own.  Blocks 0 and 1 hold the boot loader
 * (usually 01 38 B0 03 ...), block 2 is the key block of the volume
 * directory:
 *
 *   +0x000  u16 LE  previous directory block (0 in a key block)
 *   +0x002  u16 LE  next directory block (0 = last)
 *   +0x004  thirteen 0x27-byte entries, the first being the header:
 *     +0x00  u8     storage type (high nibble, 0xF) | name length (1..15)
 *     +0x01  15     volume name, A-Z 0-9 '.', first character a letter
 *     +0x16  u16 LE GS/OS lower-case flags (bit 15 = valid, bit 14 = char 1)
 *     +0x18  u32    creation date / time
 *     +0x1F  u8     entry length, 0x27
 *     +0x20  u8     entries per block, 0x0D
 *     +0x21  u16 LE active entries in this directory
 *     +0x23  u16 LE first volume bitmap block
 *     +0x25  u16 LE total blocks on the volume
 *
 * File entries carry the storage type (1 seedling, 2 sapling, 3 tree,
 * 5 GS/OS extended file with a resource fork, 0xD subdirectory), the key
 * block at +0x11, the blocks used at +0x13, a 24-bit EOF at +0x15, the file
 * type at +0x10, the aux type at +0x1F, the access byte at +0x1E, the
 * modification date / time at +0x21 and the GS/OS lower-case flags in the
 * version / min_version word at +0x1C.  Subdirectories are chains of the
 * same blocks whose first entry is a 0xE header.
 *
 * A 140 KiB image (143360 bytes) may also be stored in DOS 3.3 sector order
 * (.do / .dsk); it is recognised when the ProDOS-order probe fails and the
 * DOS-order block 2 holds a volume header for 280 blocks.
 *
 * Members are the files with their directory path (folders are listed as
 * folder records); an extended file's resource fork is a second member
 * named "<name>.rsrc", as the MacBinary and StuffIt readers name it.
 * Sparse blocks read as zeros.  Pascal areas (storage type 4) and DOS
 * volumes embedded by DOS MASTER are not listed.
 */
typedef struct xx_prodos {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t total_blocks;  /**< Volume size from the header, in blocks. */
    uint32_t file_count;    /**< The root directory's active-entry count. */
    bool dos_order;         /**< 140 KiB image in DOS 3.3 sector order. */
    bool truncated;         /**< The image holds fewer blocks than declared. */
    bool damaged;           /**< A directory chain or entry was unusable. */
    char volume_name[16];   /**< Volume name with GS/OS case applied. */
} xx_prodos;

typedef xx_prodos xx_prodos_t;

XXFC_API void xx_prodos_init(xx_prodos *volume, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_prodos *xx_prodos_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_prodos_destroy(xx_prodos *volume);
XXFC_API void xx_prodos_free(xx_prodos *volume);

XXFC_API bool xx_prodos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_prodos_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_prodos_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_prodos_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_prodos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_prodos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_prodos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_prodos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_prodos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PRODOS_H */
