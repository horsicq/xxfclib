/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore 1541 DOS on raw 35- and 40-track D64 images.
 *
 * This reader exposes every closed PRG, SEQ or USR directory member and
 * extracts its exact logical byte stream. A file's final sector uses its
 * second link byte as the inclusive last-byte index. The directory's block
 * count is checked against the complete chain. BAM counts/bitmaps, directory
 * sectors, file ownership and all track/sector references are validated.
 * PETSCII names are escaped to safe ASCII, with .prg/.seq/.usr suffixes and
 * aliases for host-name collisions. There are no subdirectories in 1541 DOS.
 *
 * Explicit 40-track modes use the SpeedDOS C0-D3 or DolphinDOS AC-BF BAM
 * extension and have 768 sectors; the default is ordinary 683-sector D64.
 * The sector payload is 174,848 or 196,608 bytes respectively; a trailing
 * error-byte map is reported as overlay and not interpreted. REL, GEOS
 * semantics, open/splat files, D71/D81, G64 flux and deleted-file recovery
 * are outside these variants. Images with
 * a live unsupported entry fail validation rather than silently losing it.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Format evidence: Inside Commodore DOS, section 4.6 and directory/BAM
 * chapters, https://www.lyonlabs.org/commodore/onrequest/Inside_Commodore_Dos.pdf
 * Independent producer: cc1541 (VICE-compatible 1541 DOS writer),
 * https://manpages.debian.org/testing/cc1541/cc1541.1.en.html
 */
#ifndef XXFCLIB_FORMAT_CBM_D64_H
#define XXFCLIB_FORMAT_CBM_D64_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cbm_d64_variant_e {
    XX_CBM_D64_35 = 35,
    XX_CBM_D64_SPEED40 = 40,
    XX_CBM_D64_DOLPHIN40 = 41
} xx_cbm_d64_variant;
typedef struct xx_cbm_d64_s {
    Abstractformat format;
    xx_cbm_d64_variant variant;
    uint32_t track_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d64;
typedef xx_cbm_d64 xx_cbm_d64_t;
typedef xx_cbm_d64 XCBM_D64;
XXFC_API void xx_cbm_d64_init(xx_cbm_d64 *, xx_io_device *, int64_t);
XXFC_API void xx_cbm_d64_init_ex(xx_cbm_d64 *, xx_io_device *, int64_t, xx_cbm_d64_variant);
XXFC_API xx_cbm_d64 *xx_cbm_d64_create(xx_io_device *, int64_t);
XXFC_API void xx_cbm_d64_destroy(xx_cbm_d64 *);
XXFC_API void xx_cbm_d64_free(xx_cbm_d64 *);
XXFC_API bool xx_cbm_d64_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d64_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d64_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d64_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d64_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d64_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d64_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d64_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d64_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d64_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d64_to_format(xx_cbm_d64 *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
