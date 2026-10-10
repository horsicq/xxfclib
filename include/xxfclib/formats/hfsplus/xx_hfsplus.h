/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only raw HFS+ (H+ version4) and HFSX (HX version5).
 *
 * Lists catalog folders and extracts logical data/resource forks. Fragmented
 * ordinary and special-file forks can use extents overflow; the overflow
 * tree itself must fit its eight volume-header extents. Resource forks with
 * allocated storage appear as '<name>.rsrc', including empty preallocated
 * forks. Symbolic-link target bytes are exported as ordinary files.
 * Raw UTF-16 keys use TN1150 case folding or HFSX binary comparison selected
 * by the catalog header; output aliases use Unicode15.1 full case folding
 * for safe unique UTF-8 paths on case-insensitive hosts. Stored decomposition
 * is preserved. Host components are shortened to128 UTF-8
 * bytes with aliases; paths <=4095bytes, folder depth <=64 and retained paths
 * <=64MiB. No links or special devices are created on the host.
 *
 * Supported bounds: allocation sizes512bytes through16MiB (powers of two),
 * <=16777216 allocation blocks, <=400000 extents, <=100000 catalog objects,
 * <=200000 catalog/overflow records,512–32768-byte B-tree nodes,
 * <=131072 nodes/tree, tree depth <=8 and <=64million checked work steps.
 * Big-key and variable-index-key trees are supported; fixed-length index
 * keys also follow their declared maximum length. Complete node maps,
 * reachable indexes, ordered leaf chains, catalog threads/hierarchy, fork
 * lengths, allocation bitmap and global extent ownership are validated.
 * Volume size is the allocation-block-aligned size declared in the header.
 * Input cursors are preserved; short I/O/cancellation and exclusive sibling
 * staging with overwrite preservation are supported.
 * MAX_MEMBER_SIZE limits logical fork size; MEMORY_LIMIT covers retained
 * reader arrays/paths, iterator and bounded copy buffer. Initial parser
 * transient allocations and generic archive metadata are excluded.
 *
 * HFS wrappers, unclean or journaled volumes (no journal replay), hard links,
 * compression, content protection/encryption, nonempty attributes trees,
 * special device objects, damaged/deleted recovery and writing are refused.
 * Primary specification: Apple Technical Note TN1150:
 * https://developer.apple.com/library/archive/technotes/tn/tn1150.html
 * Catalog case-fold data: src/formats/hfsplus/LICENSE.NetBSD.
 * Host Unicode case-fold data: src/formats/hfsplus/LICENSE.Unicode.
 */
#ifndef XXFCLIB_FORMAT_HFSPLUS_H
#define XXFCLIB_FORMAT_HFSPLUS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hfsplus {
    Abstractformat format;
    uint64_t number_of_records, volume_size;
    uint32_t allocation_block_size, allocation_block_count, file_count, folder_count;
    uint16_t signature, version;
    bool case_sensitive;
    char volume_name[1024];
} xx_hfsplus;
typedef xx_hfsplus xx_hfsplus_t;
typedef xx_hfsplus XHFSPLUS;
XXFC_API void xx_hfsplus_init(xx_hfsplus *, xx_io_device *, int64_t);
XXFC_API xx_hfsplus *xx_hfsplus_create(xx_io_device *, int64_t);
XXFC_API void xx_hfsplus_destroy(xx_hfsplus *);
XXFC_API void xx_hfsplus_free(xx_hfsplus *);
XXFC_API bool xx_hfsplus_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hfsplus_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_hfsplus_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_hfsplus_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_hfsplus_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_hfsplus_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_hfsplus_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_hfsplus_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_hfsplus_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_hfsplus_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_hfsplus_to_format(xx_hfsplus *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
