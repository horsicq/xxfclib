/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ufs1.h @brief Original native, read-only BSD FFS/UFS1 reader.
 * Standard 8192-byte superblock, little/big endian, 4.4BSD 128-byte inodes,
 * cylinder-group inode/fragment allocation maps and rotational group offsets.
 * Direct and three indirect levels, fragment-sized final direct blocks,
 * sparse regular files, nested directories and regular-file hard links are
 * supported. Symbolic links and special files are validated but not exposed
 * as reconstructed regular files. Unix name bytes are escaped for host paths;
 * case-insensitive host collisions receive deterministic aliases.
 * Primary layouts: FreeBSD sys/ufs/{ffs/fs.h,ufs/dinode.h,ufs/dir.h} and
 * NetBSD sys/ufs/{ffs/fs.h,ufs/dinode.h}. No upstream source is incorporated.
 * Only clean, unjournaled, non-snapshot 4.4BSD images with zero filesystem
 * feature flags and zero inode flags
 * are accepted. Older 4.2BSD directory/inode layouts, alternate superblocks,
 * journal replay, extended attributes, recovery and writing are unsupported.
 * Limits: 128 million fragments, 4096 groups, 16 million inode slots,
 * 100000 reachable inodes/members or names per directory, 1 million runs,
 * 16 million pointer/fragment
 * steps, 64 directory levels, 4096-byte paths and 128MiB parser allocations.
 * Initial parsing uses these hard ceilings. Extraction MEMORY_LIMIT covers
 * retained native view, iterator and at most 64KiB transfer; generic metadata,
 * options and caller device storage are excluded. MAX_MEMBER_SIZE bounds
 * logical bytes including sparse holes. Iterator options override format
 * options; limits precede output or destination creation. Input positions
 * are preserved. Files use exclusive sibling staging and publish only after
 * success; overwrite defaults to false. Devices are borrowed and must remain
 * open and unchanged while the volume/iterators exist.
 */
#ifndef XXFCLIB_FORMAT_UFS1_H
#define XXFCLIB_FORMAT_UFS1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ufs1 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t volume_size;
    uint32_t block_size;
    uint32_t fragment_size;
    uint32_t cylinder_groups;
    bool big_endian;
    void *internal;
} xx_ufs1;
typedef xx_ufs1 xx_ufs1_t;
typedef xx_ufs1 XUfs1;
XXFC_API void xx_ufs1_init(xx_ufs1 *, xx_io_device *, int64_t);
XXFC_API xx_ufs1 *xx_ufs1_create(xx_io_device *, int64_t);
XXFC_API void xx_ufs1_destroy(xx_ufs1 *);
XXFC_API void xx_ufs1_free(xx_ufs1 *);
XXFC_API bool xx_ufs1_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ufs1_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_ufs1_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_ufs1_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_ufs1_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_ufs1_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_ufs1_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_ufs1_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** NULL destination verifies all stored bytes; sparse holes need no reads.
 * Folders produce no bytes. Destination advances and must differ from input. */
XXFC_API bool xx_ufs1_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_ufs1_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_ufs1_to_format(xx_ufs1 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
