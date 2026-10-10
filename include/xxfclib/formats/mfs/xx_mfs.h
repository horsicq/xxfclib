/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only Macintosh File System (MFS), not HFS/HFS+.
 *
 * Reads raw 512-byte logical-sector images and partition-relative devices.
 * Validates the standard 1024-byte MDB and up to640 allocation blocks,
 * word-aligned per-sector directory entries and complete 12-bit fork chains.
 * Data forks are listed under their safe UTF-8 names; resource forks with
 * allocated storage are separate '<name>.rsrc' records. Empty data forks are
 * retained. Logical lengths exclude allocated tail slack and preallocation.
 * Names use MacRoman, with unsafe host characters replaced and long names
 * shortened to240 UTF-8 bytes before suffixes; aliases are made unique.
 *
 * Source cursors are preserved. Supports short I/O and cancellation.
 * MAX_MEMBER_SIZE limits logical fork length. MEMORY_LIMIT covers
 * retained maps/chains/member capacities/names, iterator and copy buffer;
 * initial parser transient allocations and generic metadata are excluded.
 * There is no disk writing, Sony sector-tag/nibble/flux decoding, deleted-file recovery,
 * Finder-folder reconstruction, damaged-primary MDB recovery or HFS support.
 * Extended MDB allocation maps beyond the standard two sectors are refused.
 *
 * Primary layout: Apple Inside Macintosh, Volume II, II-119 through II-123;
 * Volume IV, IV-160 through IV-164. Additional primary implementation evidence:
 * https://developer.apple.com/library/archive/samplecode/MFSLives/Introduction/Intro.html
 * https://ciderpress2.com/formatdoc/MFS-notes.html
 */
#ifndef XXFCLIB_FORMAT_MFS_H
#define XXFCLIB_FORMAT_MFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mfs {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t volume_size;
    uint32_t allocation_block_size;
    uint16_t allocation_block_count;
    uint16_t file_count;
    char volume_name[82];
} xx_mfs;
typedef xx_mfs xx_mfs_t;
typedef xx_mfs XMFS;
XXFC_API void xx_mfs_init(xx_mfs *volume, xx_io_device *device, int64_t base_address);
XXFC_API xx_mfs *xx_mfs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mfs_destroy(xx_mfs *volume);
XXFC_API void xx_mfs_free(xx_mfs *volume);
XXFC_API bool xx_mfs_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mfs_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mfs_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_mfs_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_mfs_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mfs_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mfs_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mfs_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mfs_extract_record_to_device(Abstractformat *self, xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd);
XXFC_API void xx_mfs_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);
static inline Abstractformat *xx_mfs_to_format(xx_mfs *volume)
{
    return volume ? &volume->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
