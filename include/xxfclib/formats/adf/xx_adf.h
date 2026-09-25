/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_adf.h @brief Amiga ADF / AmigaDOS OFS and FFS volume reader. */

#ifndef XXFCLIB_FORMAT_ADF_H
#define XXFCLIB_FORMAT_ADF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An AmigaDOS volume image: an Amiga floppy (.adf) or a plain hardfile.
 *
 * The image is an array of 512-byte blocks.  All fields are big-endian.
 *
 * Boot block (block 0):
 *   0x00  "DOS" + one flags byte 0..7: bit 0 FFS (else OFS), bit 1 INTL,
 *         bit 2 DIRCACHE; 6 and 7 are the long-name variants (LNFS)
 *   A standard DD or HD floppy (exactly 901120 or 1802240 bytes) whose two
 *   boot blocks are all zero is accepted too, as DMS decoders produce them;
 *   OFS or FFS is then decided from the first file's first data block and
 *   names are read in the classic (non-LNFS) layout.
 *
 * Root block, at (blocks + 1) / 2 of the filesystem: block 880 on a DD
 * floppy (901120 bytes), 1760 on an HD floppy (1802240 bytes):
 *   0x000 u32 type 2           0x004 header key 0     0x008 high seq 0
 *   0x00C u32 hash table size 72                      0x014 checksum
 *   0x018 u32[72] hash table   0x1B0 BSTR volume name (at most 30)
 *   0x1FC u32 secondary type 1
 *
 * Every header block (root, directory, file, link) sums to zero over its
 * 128 longs.  Entry headers (type 2) carry their own block number at 0x004,
 * the next entry on the same hash chain at 0x1F0, the parent directory at
 * 0x1F4 and a secondary type at 0x1FC: 2 directory, -3 file, 3 soft link,
 * -4 hard link to a file, 4 hard link to a directory.  Names are ISO-8859-1
 * BSTRs at 0x1B0 (at most 30 bytes); on the long-name variants the name and
 * the comment share one 112-byte field at 0x148 (names of up to 107 bytes)
 * and the modification date moves from 0x1A4 to 0x1C4.
 *
 * A file header lists its data blocks backwards from 0x134 (72 slots) and
 * continues in extension blocks (type 16) chained through 0x1F8; the byte
 * size is at 0x144.  FFS data blocks are 512 raw bytes; OFS data blocks are
 * type 8 with a 24-byte header {type, file header, sequence number, data
 * size, next block, checksum} and up to 488 payload bytes.
 *
 * Records are the directories (folders) and files reached from the root's
 * hash table, depth first, with '/'-separated paths.  Names are converted to
 * UTF-8 and made safe for a host file system (reserved characters, trailing
 * dots and spaces and device names are replaced; case-insensitive duplicates
 * get a "~N" suffix).  A hard link to a file extracts the linked file's
 * data; a hard link to a directory is an empty folder; a soft link is listed
 * with XX_META_ID_LINK_TARGET and cannot be extracted.  Every header block is
 * visited once (looping chains and directory cycles end), directories are
 * descended to 64 levels, and a file whose OFS blocks do not name it with the
 * right sequence numbers, or whose block chain leaves the volume or loops,
 * fails to unpack instead of yielding foreign data.
 */
typedef struct xx_adf {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t image_size;     /**< Bytes of the volume (format size). */
    uint32_t root_block;    /**< Block number of the root block. */
    uint32_t block_count;   /**< Blocks the filesystem spans. */
    uint8_t dos_type;       /**< The boot block's flags byte, 0..7 (for a
                                 blank boot block: 0 or 1 as inferred). */
    uint8_t blank_boot;     /**< 1 when the boot blocks were all zero. */
    char volume_name[96];   /**< UTF-8 volume name from the root block. */
} xx_adf;

typedef xx_adf xx_adf_t;

/** Block size of every image this reader accepts. */
#define XX_ADF_BLOCK_SIZE 512

XXFC_API void xx_adf_init(xx_adf *image, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_adf *xx_adf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_adf_destroy(xx_adf *image);
XXFC_API void xx_adf_free(xx_adf *image);

XXFC_API bool xx_adf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_adf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_adf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_adf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_adf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_adf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_adf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_adf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_adf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ADF_H */
