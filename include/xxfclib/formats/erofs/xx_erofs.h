/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native, read-only EROFS filesystem reader.
 *
 * Supports single-device images with compact (32-byte) and extended (64-byte)
 * inodes, flat plain/inline data, simple 4-byte chunk maps, and LZ4,
 * DEFLATE, MicroLZMA, or Zstandard compressed regular files using compact or
 * full cluster indexes. One compression algorithm per volume is supported.
 * Bounded big pclusters and compressed inline tails are supported. Nested
 * directories and regular
 * files are listed and extracted byte-exactly; genuine mkfs.erofs images are
 * compared with independent fsck.erofs extraction. Mixed algorithms, HEAD2,
 * fragments, compressed directories/metadata, xattrs,
 * external devices, 48-bit block addresses, symlinks/special files, and
 * chunk-index records are refused. The reader does not write images.
 *
 * Bounds: block size 512 bytes..64 KiB, 100,000 members, 64 directory levels,
 * 64 MiB retained path bytes, one million chunks per file, and four million
 * units of parse/extraction work. For LZ4, encoded pclusters are limited to
 * 1 MiB and decoded extents to 12 MiB. DEFLATE config requires 15 window bits;
 * MicroLZMA uses dictionary sizes 4 KiB..12 MiB and lc+lp <= 4; Zstandard
 * requires format 0 and window-log delta <= 14. MAX_MEMBER_SIZE and
 * MEMORY_LIMIT resolve
 * from operation/state and format settings. The memory budget covers retained
 * parser view, paths, iterator, extraction buffers and heap codec workspaces,
 * but excludes transient parsing allocations and generic metadata storage.
 *
 * Primary on-disk specification:
 * https://erofs.docs.kernel.org/en/latest/ondisk/core_ondisk.html
 * https://erofs.docs.kernel.org/en/latest/ondisk/chunked_format.html
 * https://github.com/erofs/erofs-utils/blob/v1.9.1/include/erofs_fs.h
 */
#ifndef XXFCLIB_FORMAT_EROFS_H
#define XXFCLIB_FORMAT_EROFS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_erofs_s {
    Abstractformat format;
    uint32_t block_size;
    uint64_t number_of_records;
} xx_erofs;
typedef xx_erofs xx_erofs_t;

XXFC_API void xx_erofs_init(xx_erofs *, xx_io_device *, int64_t);
XXFC_API xx_erofs *xx_erofs_create(xx_io_device *, int64_t);
XXFC_API void xx_erofs_destroy(xx_erofs *);
XXFC_API void xx_erofs_free(xx_erofs *);
XXFC_API bool xx_erofs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_erofs_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_erofs_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_erofs_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_erofs_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_erofs_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_erofs_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_erofs_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API bool xx_erofs_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_erofs_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);

static inline Abstractformat *xx_erofs_to_format(xx_erofs *v)
{
    return v ? &v->format : NULL;
}

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_EROFS_H */
