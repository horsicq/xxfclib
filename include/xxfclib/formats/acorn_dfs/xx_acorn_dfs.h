/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Acorn BBC Micro Disc Filing System images.
 *
 * Four bounded variants: 40/80-track single-sided SSD and track-interleaved
 * double-sided DSD, with 10x256-byte sectors per track. Each DSD side owns an
 * independent 31-file catalogue in its logical sectors 0 and 1. All catalogue
 * files are exposed as <directory>.<filename>, prefixed 0/ or 1/ on DSD.
 * Names are escaped to safe host paths and aliased on case-insensitive
 * collisions. Exact 18-bit byte lengths, contiguous extents, catalogue
 * ranges, crosslinks and duplicate raw names are validated. An AUTO probe
 * requires at least one file because empty DFS has no magic signature; an
 * explicit variant can read a blank formatted image.
 *
 * Watford/HDFS extended catalogues, unusual densities, sequential DSD,
 * copy-protection sector maps, and data interpretation are outside scope.
 * Source cursor preservation, short I/O, cancellation, resolved limits and
 * exclusive sibling staging are supported. MEMORY_LIMIT includes the retained
 * parser view, member names, record state and copy buffer; parser temporaries
 * and generic record metadata are excluded.
 *
 * Primary layout: Acorn Disc Filing System User Guide, issue 2, Technical
 * Information pp. 86-87, https://www.retroisle.com/others/acornbbc/OriginalDocs/Acorn_DiscSystemUGI2.pdf
 * Independent producer: beebtools, https://github.com/acscpt/beebtools
 */
#ifndef XXFCLIB_FORMAT_ACORN_DFS_H
#define XXFCLIB_FORMAT_ACORN_DFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_acorn_dfs_variant_e {
    XX_ACORN_DFS_AUTO = 0,
    XX_ACORN_DFS_SSD40 = 1,
    XX_ACORN_DFS_SSD80 = 2,
    XX_ACORN_DFS_DSD40 = 3,
    XX_ACORN_DFS_DSD80 = 4
} xx_acorn_dfs_variant;
typedef struct xx_acorn_dfs_s {
    Abstractformat format;
    xx_acorn_dfs_variant variant;
    uint32_t track_count;
    uint32_t side_count;
    uint64_t number_of_records;
    char disk_name[2][13];
} xx_acorn_dfs;
typedef xx_acorn_dfs xx_acorn_dfs_t;
typedef xx_acorn_dfs XACORN_DFS;
XXFC_API void xx_acorn_dfs_init(xx_acorn_dfs *, xx_io_device *, int64_t);
XXFC_API void xx_acorn_dfs_init_ex(xx_acorn_dfs *, xx_io_device *, int64_t, xx_acorn_dfs_variant);
XXFC_API xx_acorn_dfs *xx_acorn_dfs_create(xx_io_device *, int64_t);
XXFC_API void xx_acorn_dfs_destroy(xx_acorn_dfs *);
XXFC_API void xx_acorn_dfs_free(xx_acorn_dfs *);
XXFC_API bool xx_acorn_dfs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_acorn_dfs_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_acorn_dfs_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_acorn_dfs_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_acorn_dfs_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_acorn_dfs_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_acorn_dfs_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_acorn_dfs_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_acorn_dfs_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_acorn_dfs_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_acorn_dfs_to_format(xx_acorn_dfs *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
