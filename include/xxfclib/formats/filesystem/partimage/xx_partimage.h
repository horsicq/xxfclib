/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_partimage.h @brief Native single-volume Partimage partition reader.
 * Supports uncompressed, little-endian v0.6.1 images with volume number zero,
 * no MBR backup, extension, encryption or split flag. One `partition.img`
 * member reconstructs the complete partition; omitted bitmap blocks are zero,
 * matching Partimage's erase-free-blocks restore mode. Main, local and FS-info
 * additive checks, 64KiB data CRC32 checkpoints, block bitmap count, final
 * additive checksum and exact container length are validated. Original format
 * evidence: Partimage 0.6.9 client/{misc.cpp,imagefile.cpp,fs/fs_base.cpp}.
 * No upstream implementation is incorporated. Compressed streams, multiple
 * volumes, MBR records, nonzero extensions and other format versions are
 * refused. Initial parsing caps the bitmap at 64MiB and image at 1PiB.
 * Extraction MAX_MEMBER_SIZE includes zero holes; MEMORY_LIMIT covers retained
 * bitmap/view, iterator and 64KiB transfer workspace. Input cursor is
 * preserved; output is staged beside the destination and overwrite defaults
 * to false. Borrowed input must remain open and unchanged while in use.
 */
#ifndef XXFCLIB_FORMAT_PARTIMAGE_H
#define XXFCLIB_FORMAT_PARTIMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_partimage {
    Abstractformat format;
    uint64_t partition_size;
    uint64_t used_blocks;
    uint32_t block_size;
    void *internal;
} xx_partimage;
typedef xx_partimage xx_partimage_t;
typedef xx_partimage XPartimage;
XXFC_API void xx_partimage_init(xx_partimage *, xx_io_device *, int64_t);
XXFC_API xx_partimage *xx_partimage_create(xx_io_device *, int64_t);
XXFC_API void xx_partimage_destroy(xx_partimage *);
XXFC_API void xx_partimage_free(xx_partimage *);
XXFC_API bool xx_partimage_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_partimage_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_partimage_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_partimage_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_partimage_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_partimage_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_partimage_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_partimage_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** NULL destination verifies data and checksums without writing. */
XXFC_API bool xx_partimage_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_partimage_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_partimage_to_format(xx_partimage *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
