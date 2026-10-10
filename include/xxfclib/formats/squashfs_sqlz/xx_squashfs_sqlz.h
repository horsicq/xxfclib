/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_squashfs_sqlz.h
 * @brief SquashFS v2/v3 images carrying the vendor magic 'sqlz' / 'zlqs'.
 *
 * Some firmware (Linksys WAG120N and other Broadcom/Sercomm based devices)
 * ships a SquashFS 2.x/3.x image whose magic was changed from 'sqsh' to
 * 'sqlz' ("SQuashfs LZma") and whose blocks are LZMA compressed.  Apart from
 * the magic the superblock, inode, directory and fragment layouts are the
 * standard v2/v3 ones:
 *
 *   'sqlz' (73 71 6C 7A)  big-endian image,    major = BE u16 at 0x1C
 *   'zlqs' (7A 6C 71 73)  little-endian image, major = LE u16 at 0x1C
 *
 * Only majors 2 and 3 are accepted.  Superblock (packed):
 *
 *   0x00 magic     0x04 inodes u32     0x08 bytes_used u32 (v2)
 *   0x14 inode_table u32 (v2)          0x18 directory_table u32 (v2)
 *   0x1C major u16 0x1E minor u16      0x22 block_log u16   0x24 flags u8
 *   0x2B root_inode u64                0x33 block_size u32  0x37 fragments u32
 *   0x3B fragment_table u32 (v2)       -- v2 ends at 0x3F --
 *   0x3F bytes_used u64  0x57 inode_table u64  0x5F directory_table u64
 *   0x67 fragment_table u64            -- v3 ends at 0x77 --
 *
 * A block is stored raw, zlib, or LZMA in one of two framings: five props
 * bytes followed by the range-coded data with no size field and usually no
 * end marker (the Broadcom 7z-based tools), or the 13-byte "alone" header
 * (props plus a u64 size).  The reader carries its own LZMA decoder because a
 * stream without an end marker has to stop at the block's known size or at
 * the clean end of its input, which the library's LZMA entry points do not
 * offer.
 *
 * Members are the regular files of the tree; symlinks, devices, fifos and
 * sockets are skipped.  Names are relative paths joined with '/'.
 */

#ifndef XXFCLIB_FORMAT_SQUASHFS_SQLZ_H
#define XXFCLIB_FORMAT_SQUASHFS_SQLZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_squashfs_sqlz {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t block_size;
    uint64_t inode_count;
    uint64_t fragment_count;
    int64_t bytes_used;
    bool big_endian;
    void *internal; /**< Private parse result. */
} xx_squashfs_sqlz;

typedef xx_squashfs_sqlz xx_squashfs_sqlz_t;

XXFC_API void xx_squashfs_sqlz_init(xx_squashfs_sqlz *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_squashfs_sqlz *xx_squashfs_sqlz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_squashfs_sqlz_destroy(xx_squashfs_sqlz *archive);
XXFC_API void xx_squashfs_sqlz_free(xx_squashfs_sqlz *archive);

XXFC_API bool xx_squashfs_sqlz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_squashfs_sqlz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_squashfs_sqlz_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_squashfs_sqlz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_squashfs_sqlz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_squashfs_sqlz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_squashfs_sqlz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_squashfs_sqlz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_squashfs_sqlz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/**
 * Decode one LZMA1 stream (no header) into `output`.  `exact` true: produce
 * exactly `output_size` bytes.  `exact` false: stop at an end marker, at the
 * clean end of the input, or when `output_size` bytes are produced.
 */
XXFC_API bool xx_squashfs_sqlz_lzma_decode(const uint8_t *input, size_t input_size, uint8_t props_byte, uint8_t *output, size_t output_size, bool exact, size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQUASHFS_SQLZ_H */
