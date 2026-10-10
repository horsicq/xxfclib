/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore 8050/8250 DOS on raw D80/D82 images.
 *
 * This reader exposes every closed PRG, SEQ or USR directory member and
 * extracts its exact logical byte stream. A file's final sector uses its
 * second link byte as the inclusive last-byte index. The directory's block
 * count is checked against the complete chain. BAM counts/bitmaps, directory
 * sectors, file ownership and all track/sector references are validated.
 * PETSCII names are escaped to safe ASCII, with .prg/.seq/.usr suffixes and
 * aliases for host-name collisions. This bounded variant supports the flat
 * root directory and rejects live partition/subdirectory entries.
 *
 * D80 has 77 tracks/2083 sectors; D82 has 154 tracks/4166 sectors. Track
 * zones have 29/27/25/23 sectors. Header 39/0 points through BAM sectors
 * 38/0,3 (plus 38/6,9 for D82) to the root directory at 39/1. Five-byte
 * BAM entries, links, free counts, ownership and sector bounds are checked.
 * A trailing sector-error table is reported as overlay; error codes are not
 * interpreted. REL, extended directories outside track39, open/splat files,
 * deleted recovery and disk writing are outside this bounded reader.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Primary format evidence: official VICE D80/D82 image specification,
 * https://vice-emu.sourceforge.io/vice_17.html
 * Independent producer: VICE c1541 v3.10, https://vice-emu.sourceforge.io/
 */
#ifndef XXFCLIB_FORMAT_CBM_D8X_H
#define XXFCLIB_FORMAT_CBM_D8X_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cbm_d8x_variant_e {
    XX_CBM_D8X_8050 = 80,
    XX_CBM_D8X_8250 = 82
} xx_cbm_d8x_variant;
typedef struct xx_cbm_d8x_s {
    Abstractformat format;
    xx_cbm_d8x_variant variant;
    uint32_t track_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d8x;
typedef xx_cbm_d8x xx_cbm_d8x_t;
typedef xx_cbm_d8x XCBM_D8X;
XXFC_API void xx_cbm_d8x_init(xx_cbm_d8x *, xx_io_device *, int64_t);
XXFC_API void xx_cbm_d8x_init_ex(xx_cbm_d8x *, xx_io_device *, int64_t, xx_cbm_d8x_variant);
XXFC_API xx_cbm_d8x *xx_cbm_d8x_create(xx_io_device *, int64_t);
XXFC_API void xx_cbm_d8x_destroy(xx_cbm_d8x *);
XXFC_API void xx_cbm_d8x_free(xx_cbm_d8x *);
XXFC_API bool xx_cbm_d8x_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d8x_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d8x_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d8x_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d8x_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d8x_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d8x_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d8x_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d8x_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d8x_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d8x_to_format(xx_cbm_d8x *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
