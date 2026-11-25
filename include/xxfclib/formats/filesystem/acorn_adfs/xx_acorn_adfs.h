/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only Acorn ADFS S/M/L old-map, old-directory volumes.
 *
 * S=160KiB, M=320KiB and L=640KiB, with 256-byte logical sectors and
 * 47-entry Hugo/Nick directories. Files occupy contiguous logical sectors;
 * exact byte lengths come from directory entries. L-sector image ordering is
 * explicit: track-interleaved side order is the init default and matches ADL
 * dumps, while init_ex selects side-sequential/linear order. AUTO order refuses
 * L because the first-side map alone cannot distinguish physical ordering.
 * The reader validates both free-map carry checksums, free-run ordering,
 * directory parent/sequence markers, file bounds, crosslinks and free-space
 * overlap. The 8-bit ADFS zero directory-checkbyte convention is accepted.
 * Nonzero RISC OS directory checkbytes are outside this bounded variant.
 *
 * Host names are sanitized 7-bit strings; aliases preserve collisions after
 * sanitization. Source cursors, short I/O, cancellation, resolved
 * MAX_MEMBER_SIZE/MEMORY_LIMIT and exclusive sibling output staging are
 * supported. Bounded to 8192 entries, depth64, path4095 and 2M work steps.
 * Excludes D/E/F and later new maps/77-entry/big directories, hard-disc boot
 * blocks, flux/sector tags, compressed images and damaged/deleted recovery.
 * Primary Acorn RISC OS PRM, FileCore old maps and directories:
 * https://www.riscos.com/support/developers/prm/filecore.html
 */
#ifndef XXFCLIB_FORMAT_ACORN_ADFS_H
#define XXFCLIB_FORMAT_ACORN_ADFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_acorn_adfs_variant_e {
    XX_ACORN_ADFS_AUTO = 0,
    XX_ACORN_ADFS_S = 640,
    XX_ACORN_ADFS_M = 1280,
    XX_ACORN_ADFS_L = 2560
} xx_acorn_adfs_variant;
typedef enum xx_acorn_adfs_order_e {
    XX_ACORN_ADFS_ORDER_AUTO = 0,
    XX_ACORN_ADFS_ORDER_LINEAR = 1,
    XX_ACORN_ADFS_ORDER_L_TRACK_INTERLEAVED = 2
} xx_acorn_adfs_order;
typedef struct xx_acorn_adfs_s {
    Abstractformat format;
    xx_acorn_adfs_variant variant;
    xx_acorn_adfs_order order;
    uint64_t number_of_records, volume_size;
    uint32_t logical_sectors;
    char volume_name[16];
} xx_acorn_adfs;
typedef xx_acorn_adfs xx_acorn_adfs_t;
typedef xx_acorn_adfs XACORN_ADFS;
XXFC_API void xx_acorn_adfs_init(xx_acorn_adfs *, xx_io_device *, int64_t);
XXFC_API void xx_acorn_adfs_init_ex(xx_acorn_adfs *, xx_io_device *, int64_t, xx_acorn_adfs_variant, xx_acorn_adfs_order);
XXFC_API xx_acorn_adfs *xx_acorn_adfs_create(xx_io_device *, int64_t);
XXFC_API void xx_acorn_adfs_destroy(xx_acorn_adfs *);
XXFC_API void xx_acorn_adfs_free(xx_acorn_adfs *);
XXFC_API bool xx_acorn_adfs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_acorn_adfs_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_acorn_adfs_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_acorn_adfs_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_acorn_adfs_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_acorn_adfs_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_acorn_adfs_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_acorn_adfs_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_acorn_adfs_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_acorn_adfs_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_acorn_adfs_to_format(xx_acorn_adfs *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
