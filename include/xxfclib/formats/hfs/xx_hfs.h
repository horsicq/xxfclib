/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native, read-only classic Macintosh HFS (BD signature).
 *
 * Reads raw 512-byte logical-sector images and partition-relative devices.
 * Catalog folders, data forks, resource forks, fragmented allocations and
 * ordinary file/catalog extents overflow are supported. Resources with
 * allocated storage appear as '<name>.rsrc'; empty data forks are retained.
 * MacRoman names become unique safe UTF-8 relative paths. Logical file lengths
 * exclude allocation slack. Input cursors are preserved; short I/O,
 * cancellation and staged overwrite are supported.
 *
 * Limits: 65535 allocation blocks, allocation size <=16MiB, 512-byte B-tree
 * nodes, 131072 nodes per tree, tree depth <=8, 100000 catalog objects,
 * 200000 leaf records and
 * overflow records, directory depth <=64, paths <=4095 UTF-8 bytes, <=64MiB
 * of retained paths, and <=4 million checked parsing work steps. Complete
 * B-tree maps/indexes/leaf chains, folder threads, lengths, allocation bitmap
 * and extent ownership are validated. Orphan overflow records are refused.
 * The extents-overflow tree must fit its three MDB extents: resolving that
 * tree through its own overflow records is deliberately unsupported.
 * MAX_MEMBER_SIZE limits logical fork size. MEMORY_LIMIT covers retained
 * reader arrays/paths, the iterator and the bounded copy buffer; initial
 * parsing transient allocations and generic archive metadata are excluded.
 *
 * HFS+/HFSX (including embedded wrappers), journaled/inconsistent volumes,
 * deleted-file recovery, damaged-tree recovery, Sony sector tags, nibble/flux
 * decoding and disk writing are outside this reader.
 * Primary specification: Apple Inside Macintosh: Files, chapter 2:
 * https://developer.apple.com/library/archive/documentation/mac/Files/Files-102.html
 * https://developer.apple.com/library/archive/documentation/mac/Files/Files-103.html
 * https://developer.apple.com/library/archive/documentation/mac/Files/Files-104.html
 * https://developer.apple.com/library/archive/documentation/mac/Files/Files-105.html
 * https://developer.apple.com/library/archive/documentation/mac/Files/Files-106.html
 */
#ifndef XXFCLIB_FORMAT_HFS_H
#define XXFCLIB_FORMAT_HFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hfs {
    Abstractformat format;
    uint64_t number_of_records, volume_size;
    uint32_t allocation_block_size, file_count, folder_count;
    uint16_t allocation_block_count;
    char volume_name[82];
} xx_hfs;
typedef xx_hfs xx_hfs_t;
typedef xx_hfs XHFS;
XXFC_API void xx_hfs_init(xx_hfs *, xx_io_device *, int64_t);
XXFC_API xx_hfs *xx_hfs_create(xx_io_device *, int64_t);
XXFC_API void xx_hfs_destroy(xx_hfs *);
XXFC_API void xx_hfs_free(xx_hfs *);
XXFC_API bool xx_hfs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hfs_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_hfs_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_hfs_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_hfs_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_hfs_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_hfs_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_hfs_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_hfs_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_hfs_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_hfs_to_format(xx_hfs *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
