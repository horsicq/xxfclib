/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native, read-only Microware OS-9/6809 RBF volume reader.
 *
 * Supports clean raw 256-byte-sector Level One/Two RBF volumes with the
 * classic LSN0, allocation bitmap, 48-segment file descriptors, 32-byte
 * directory entries, nested folders, regular hard links, fragmented files,
 * and exact FD.SIZ extraction. Geometry is read from LSN0, and init_ex can
 * require one of the independently verified profiles below. Generic init is
 * explicit selection only: RBF has no strong magic for safe auto-detection.
 *
 * Bounded to 65536 sectors, 4096 listed members, depth 32, path bytes 1MiB,
 * and four million units of scan/extraction work. OS-9/68K CRUZ headers,
 * variable sector sizes, non-classic descriptors, deleted recovery, journal
 * replay and writing are outside this reader. Directory hard links are
 * refused. MAX_MEMBER_SIZE and MEMORY_LIMIT apply to resolved options;
 * memory includes retained view/maps/names, iterator and copy buffer but
 * excludes initial parsing transients and generic metadata allocations.
 *
 * Primary: Microware OS-9 System Programmer's Manual, "Random Block File
 * Manager", section 6.1, https://www.roug.org/retrocomputing/os/os9/os9sysprog.html
 * Microware OS-9 v2.4 Technical I/O Manual, "Device Driver Tables",
 * https://colorcomputerarchive.com/repo/Documents/Manuals/Operating%20Systems/
 * ToolShed is an independent fixture producer, not linked into this reader.
 */
#ifndef XXFCLIB_FORMAT_OS9_RBF_H
#define XXFCLIB_FORMAT_OS9_RBF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_os9_rbf_variant_e {
    XX_OS9_RBF_CLASSIC = 0,
    XX_OS9_RBF_35SS_C1 = 1,
    XX_OS9_RBF_40DS_C1 = 2,
    XX_OS9_RBF_40DS_C2 = 3,
    XX_OS9_RBF_80DS_C1 = 4,
    XX_OS9_RBF_HD4096_C4 = 5
} xx_os9_rbf_variant;
typedef struct xx_os9_rbf_s {
    Abstractformat format;
    xx_os9_rbf_variant variant;
    uint32_t total_sectors, sectors_per_cluster;
    uint64_t number_of_records;
    char volume_name[40];
} xx_os9_rbf;
typedef xx_os9_rbf xx_os9_rbf_t;
XXFC_API void xx_os9_rbf_init(xx_os9_rbf *, xx_io_device *, int64_t);
XXFC_API void xx_os9_rbf_init_ex(xx_os9_rbf *, xx_io_device *, int64_t, xx_os9_rbf_variant);
XXFC_API xx_os9_rbf *xx_os9_rbf_create(xx_io_device *, int64_t);
XXFC_API xx_os9_rbf *xx_os9_rbf_create_ex(xx_io_device *, int64_t, xx_os9_rbf_variant);
XXFC_API void xx_os9_rbf_destroy(xx_os9_rbf *);
XXFC_API void xx_os9_rbf_free(xx_os9_rbf *);
XXFC_API bool xx_os9_rbf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_os9_rbf_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_os9_rbf_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_os9_rbf_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_os9_rbf_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_os9_rbf_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_os9_rbf_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_os9_rbf_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API bool xx_os9_rbf_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_os9_rbf_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_os9_rbf_to_format(xx_os9_rbf *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
