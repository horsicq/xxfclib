/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cloop.h @brief cloop V2 / FreeBSD geom_uzip compressed image reader. */

#ifndef XXFCLIB_FORMAT_CLOOP_H
#define XXFCLIB_FORMAT_CLOOP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A block-compressed read-only disk image: the Linux cloop 2.x
 * "compressed loop" format (KNOPPIX) and the FreeBSD geom_uzip image that
 * mkuzip writes on the same layout.
 *
 *   0x00  char[128] shell preamble: "#!/bin/sh\n#" + a flavour letter and
 *                   version ("V2.0" zlib, "L3.0" xz, "Z4.0" zstd; lower
 *                   case when mkuzip de-duplicated blocks), NUL padded
 *   0x80  u32 BE    block size (uncompressed, a multiple of 512)
 *   0x84  u32 BE    number of blocks n
 *   0x88  u64 BE[n+1] table of contents: absolute offsets of the compressed
 *                   blocks, the last entry being the end of the data
 *
 * Every block decodes to exactly one block size of image, so the image is
 * n * block size bytes. It is published as one member, "disk.iso" when the
 * preamble mounts it as ISO 9660 and "disk.img" otherwise.
 */
typedef struct xx_cloop {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< n * block size. */
    uint32_t block_size;
    uint32_t block_count;
    uint32_t method; /**< 1 zlib, 2 xz, 3 zstd. */
} xx_cloop;

typedef xx_cloop xx_cloop_t;

XXFC_API void xx_cloop_init(xx_cloop *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_cloop *xx_cloop_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_cloop_destroy(xx_cloop *archive);
XXFC_API void xx_cloop_free(xx_cloop *archive);

XXFC_API bool xx_cloop_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cloop_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_cloop_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_cloop_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cloop_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cloop_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cloop_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cloop_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cloop_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CLOOP_H */
