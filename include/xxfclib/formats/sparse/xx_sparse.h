/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sparse.h @brief Android sparse image reader. */

/* The Android sparse image is the transport form of a flashable partition:
 * the raw image with its long runs of zeros and repeated words replaced by
 * short descriptors.  Everything is LITTLE endian, unlike the other firmware
 * containers in this library.
 *
 *   sparse_header (file_hdr_sz bytes, at least 28)
 *     +0   u32  magic 0xED26FF3A
 *     +4   u16  major version, must be 1
 *     +6   u16  minor version
 *     +8   u16  file_hdr_sz, the size of this header
 *     +10  u16  chunk_hdr_sz, the size of every chunk header, at least 12
 *     +12  u32  blk_sz, the output block size, a non-zero multiple of 4
 *     +16  u32  total_blks, the output size in blocks
 *     +20  u32  total_chunks
 *     +24  u32  image_checksum, CRC32 of the expanded image (0 if not given)
 *
 *   chunk_header (chunk_hdr_sz bytes, at least 12)
 *     +0   u16  chunk_type
 *     +2   u16  reserved
 *     +4   u32  chunk_sz, the number of OUTPUT BLOCKS this chunk expands to
 *     +8   u32  total_sz, the number of INPUT BYTES the chunk occupies,
 *               its own header included
 *
 *   chunk types
 *     0xCAC1 RAW        chunk_sz blocks stored verbatim after the header
 *     0xCAC2 FILL       one 4-byte pattern repeated over chunk_sz blocks
 *     0xCAC3 DONT_CARE  chunk_sz blocks of nothing; expands to zeros
 *     0xCAC4 CRC32      no output; carries the running CRC32 of the image
 *                       produced so far, used to catch a truncated transfer
 *
 * The reader publishes ONE member: the reassembled raw image.  A sparse file
 * is not an archive of separable parts, and its only useful product is the
 * partition image that simg2img would write, so that is what is extracted.
 *
 * total_blks and every chunk's chunk_sz are attacker controlled and are
 * 32-bit, so an image may legitimately declare an output far larger than the
 * input.  Nothing proportional to the DECLARED sizes is ever allocated: the
 * chunk table grows only as real chunk headers are read, which the input size
 * already bounds, and expansion streams through a fixed staging buffer.
 */

#ifndef XXFCLIB_FORMAT_SPARSE_H
#define XXFCLIB_FORMAT_SPARSE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_sparse xx_sparse;
typedef struct xx_sparse xx_sparse_t;
typedef struct xx_sparse XSparse;

struct xx_sparse {
    Abstractformat format;
    uint64_t number_of_records;   /**< Always 1 for a valid image. */
    uint64_t number_of_members;   /**< Always 1 for a valid image. */
    uint32_t block_size;          /**< blk_sz from the header. */
    uint32_t total_blocks;        /**< total_blks from the header. */
    uint32_t total_chunks;        /**< total_chunks from the header. */
    uint32_t image_checksum;      /**< image_checksum from the header. */
    uint16_t major_version;
    uint16_t minor_version;
    int64_t expanded_size;        /**< total_blks * blk_sz, or -1. */
    int64_t archive_end;          /**< End of the last chunk, or -1. */
    void *internal;
};

XXFC_API void xx_sparse_init(xx_sparse *sparse, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_sparse *xx_sparse_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_sparse_destroy(xx_sparse *sparse);
XXFC_API void xx_sparse_free(xx_sparse *sparse);

XXFC_API bool xx_sparse_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sparse_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_sparse_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_sparse_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

/** Expand the whole image into destination.  The reassembled bytes, not the
 * sparse container, are written; the destination is filled from its current
 * position.  Any CRC32 chunk encountered is checked against the running
 * checksum and a mismatch fails the call. */
XXFC_API bool xx_sparse_unpack_to_device(xx_sparse *sparse,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sparse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sparse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sparse_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sparse_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sparse_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_sparse_get_number_of_records(const xx_sparse *sparse);
XXFC_API uint64_t xx_sparse_get_number_of_members(const xx_sparse *sparse);
XXFC_API uint32_t xx_sparse_get_block_size(const xx_sparse *sparse);
XXFC_API uint32_t xx_sparse_get_total_blocks(const xx_sparse *sparse);
XXFC_API uint32_t xx_sparse_get_total_chunks(const xx_sparse *sparse);
XXFC_API uint32_t xx_sparse_get_image_checksum(const xx_sparse *sparse);
XXFC_API int64_t xx_sparse_get_expanded_size(const xx_sparse *sparse);
XXFC_API int64_t xx_sparse_get_archive_end(const xx_sparse *sparse);

static inline Abstractformat *xx_sparse_to_format(xx_sparse *sparse) {
    return sparse ? &sparse->format : NULL;
}
static inline void XSparse_init(xx_sparse *sparse, xx_io_device *dev,
                                int64_t base_address) {
    xx_sparse_init(sparse, dev, base_address);
}
static inline xx_sparse *XSparse_create(xx_io_device *dev,
                                        int64_t base_address) {
    return xx_sparse_create(dev, base_address);
}
static inline void XSparse_free(xx_sparse *sparse) { xx_sparse_free(sparse); }
static inline bool XSparse_is_valid(xx_sparse *sparse, xx_pd_struct *pd) {
    return sparse ? xx_format_is_valid(&sparse->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SPARSE_H */
