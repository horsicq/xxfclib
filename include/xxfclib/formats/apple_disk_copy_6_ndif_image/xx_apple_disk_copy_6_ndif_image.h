/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apple_disk_copy_6_ndif_image.h
 *  @brief Apple Disk Copy 6 NDIF disk image reader (MacBinary-wrapped). */

#ifndef XXFCLIB_FORMAT_APPLE_DISK_COPY_6_NDIF_IMAGE_H
#define XXFCLIB_FORMAT_APPLE_DISK_COPY_6_NDIF_IMAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Disk Copy 6 "New Disk Image Format" (NDIF) image.
 *
 * NDIF keeps the (compressed) sectors in the data fork and the chunk table in
 * resource 'bcem' 128 of the resource fork, so off a Mac the image only
 * survives as a single file inside MacBinary.  The reader accepts MacBinary
 * I, II and III wrappers and surfaces ONE member: the decoded raw disk image.
 *
 * 'bcem' 128 (all fields big endian):
 *   0x00  u16   version (major), u16 version (minor)
 *   0x04  u8    image name length (0..63), 63 bytes image name
 *   0x44  u32   number of 512-byte sectors in the image
 *   0x48  u32   chunk size in sectors (informational)
 *   0x4C  u32   "bs zero offset" (informational)
 *   0x50  u32   checksum (informational)
 *   0x54  u32   1 when the image is segmented (.imgpart set, 'bcm#')
 *   0x58  8 bytes unknown, 28 bytes reserved
 *   0x7C  u32   number of 12-byte chunk entries that follow
 *   0x80  entries: u24 first sector, u8 type, u32 data-fork offset,
 *                  u32 data-fork length
 * Chunk types: 0x00 zero fill, 0x02 raw, 0x83 ADC, 0x80 KenCode (no known
 * decoder; refused on extraction), 0xFF terminator (last entry, whose sector
 * equals the sector count).  A chunk covers the sectors up to the next
 * entry's first sector.
 */
typedef struct xx_apple_disk_copy_6_ndif_image {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t image_size;      /**< Decoded disk size in bytes. */
    uint32_t chunk_count;     /**< Entries in 'bcem' 128, terminator included. */
    bool segmented;
    bool has_kencode;
} xx_apple_disk_copy_6_ndif_image;

typedef xx_apple_disk_copy_6_ndif_image xx_apple_disk_copy_6_ndif_image_t;

XXFC_API void xx_apple_disk_copy_6_ndif_image_init(
    xx_apple_disk_copy_6_ndif_image *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_apple_disk_copy_6_ndif_image *xx_apple_disk_copy_6_ndif_image_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_disk_copy_6_ndif_image_destroy(
    xx_apple_disk_copy_6_ndif_image *archive);
XXFC_API void xx_apple_disk_copy_6_ndif_image_free(
    xx_apple_disk_copy_6_ndif_image *archive);

XXFC_API bool xx_apple_disk_copy_6_ndif_image_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apple_disk_copy_6_ndif_image_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_apple_disk_copy_6_ndif_image_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_apple_disk_copy_6_ndif_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_apple_disk_copy_6_ndif_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_apple_disk_copy_6_ndif_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apple_disk_copy_6_ndif_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apple_disk_copy_6_ndif_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apple_disk_copy_6_ndif_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode the whole disk image into @p destination (NULL only
 * verifies every chunk).
 */
XXFC_API bool xx_apple_disk_copy_6_ndif_image_unpack_to_device(
    xx_apple_disk_copy_6_ndif_image *archive, xx_io_device *destination,
    xx_pd_struct *pd);

/**
 * @brief Decode one Apple Data Compression (ADC) stream held in memory.
 *
 * @return true when exactly @p output_size bytes were produced without a
 *         back-reference before the start of the output; trailing input
 *         bytes are allowed and reported through @p consumed.
 */
XXFC_API bool xx_apple_disk_copy_6_ndif_image_adc_decode_memory(
    const uint8_t *input, size_t input_size, uint8_t *output,
    size_t output_size, size_t *consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APPLE_DISK_COPY_6_NDIF_IMAGE_H */
