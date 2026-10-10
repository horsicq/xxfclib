/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_partclone_image.h @brief Partclone image (format 0002) reader. */

/* A partclone image is a block-level copy of one partition: a header, a
 * bitmap saying which filesystem blocks are in use, and then only the used
 * blocks, optionally interleaved with checksums.  The reader publishes ONE
 * member, the restored raw partition image, the way `partclone.restore -W`
 * writes it (unused blocks read back as zeros).
 *
 * Layout (image format "0002", 110-byte packed header).  Multi-byte header
 * fields are in the byte order of the machine that wrote the image; the
 * endian mark at +34 tells which (bytes DE C0 = little, C0 DE = big).
 *
 *   +0    char[16] "partclone-image\0"
 *   +16   char[14] partclone version text, e.g. "0.3.27"
 *   +30   char[4]  image version "0002"
 *   +34   u16      endian mark 0xC0DE
 *   +36   char[16] filesystem name ("EXTFS", "NTFS", "FAT32", "raw", ...)
 *   +52   u64      device size in bytes
 *   +60   u64      total blocks
 *   +68   u64      used blocks as the filesystem superblock counts them
 *   +76   u64      used blocks as the bitmap counts them (= data blocks)
 *   +84   u32      block size
 *   +88   u32      feature (options) size
 *   +92   u16      image version (2)
 *   +94   u16      CPU bits of the writer (32 / 64)
 *   +96   u16      checksum mode (0 none, 0x20 CRC32, 0x30 XXH64, ...)
 *   +98   u16      checksum size in bytes
 *   +100  u32      blocks per checksum
 *   +104  u8       reseed checksum flag
 *   +105  u8       bitmap mode (1 = one bit per block)
 *   +106  u32      CRC32 of bytes 0..105 (seed 0xFFFFFFFF, no final XOR)
 *
 *   bitmap: ceil(total_blocks / 8) bytes, bit n = block n (LSB first within
 *           each CPU word), then a u32 CRC32 of the bitmap (same convention)
 *   data:   the used blocks in order; after every `blocks per checksum`
 *           blocks, and after the last partial group, `checksum size` bytes.
 *
 * Every count is attacker controlled.  Nothing proportional to a declared
 * size is allocated: the bitmap is streamed through a fixed buffer, and the
 * declared output size is checked against an expansion ceiling.
 */

#ifndef XXFCLIB_FORMAT_PARTCLONE_IMAGE_H
#define XXFCLIB_FORMAT_PARTCLONE_IMAGE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_partclone_image xx_partclone_image;
typedef struct xx_partclone_image xx_partclone_image_t;
typedef struct xx_partclone_image XPartcloneImage;

struct xx_partclone_image {
    Abstractformat format;
    uint64_t number_of_records; /**< Always 1 for a valid image. */
    uint64_t total_blocks;
    uint64_t used_blocks;
    uint64_t device_size;
    uint32_t block_size;
    uint16_t checksum_mode;
    uint16_t checksum_size;
    uint32_t blocks_per_checksum;
    bool big_endian;
    char fs_name[17];      /**< Filesystem name, NUL terminated. */
    int64_t restored_size; /**< Size of the restored image, or -1. */
    int64_t archive_end;   /**< End of the image data, or -1. */
    void *internal;
};

XXFC_API void xx_partclone_image_init(xx_partclone_image *image, xx_io_device *dev, int64_t base_address);
XXFC_API xx_partclone_image *xx_partclone_image_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_partclone_image_destroy(xx_partclone_image *image);
XXFC_API void xx_partclone_image_free(xx_partclone_image *image);

XXFC_API bool xx_partclone_image_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_partclone_image_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_partclone_image_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_partclone_image_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

/** Restore the raw partition image into destination, from its current
 * position. */
XXFC_API bool xx_partclone_image_unpack_to_device(xx_partclone_image *image, xx_io_device *destination, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_partclone_image_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_partclone_image_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_partclone_image_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_partclone_image_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_partclone_image_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_partclone_image_get_restored_size(const xx_partclone_image *image);
XXFC_API int64_t xx_partclone_image_get_archive_end(const xx_partclone_image *image);

static inline Abstractformat *xx_partclone_image_to_format(xx_partclone_image *image)
{
    return image ? &image->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PARTCLONE_IMAGE_H */
