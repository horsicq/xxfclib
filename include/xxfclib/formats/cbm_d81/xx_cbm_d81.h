/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore 1581 DOS on raw D81 images.
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
 * The 80-track image has 3200x256-byte logical sectors. The header is at
 * 40/0, both BAMs at 40/1 and 40/2, and the root directory starts at 40/3.
 * Six-byte BAM entries, file-sector links, free counts, crosslinks and all
 * track/sector bounds are checked. A trailing 3200-byte error map is reported
 * as overlay, without interpreting sector error codes. REL, partitions,
 * subdirectories, GEOS semantics, open/splat files, G81 flux and deleted-file
 * recovery are outside this variant; live unsupported entries fail validation.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Primary format evidence: official VICE D81 image specification,
 * https://vice-emu.sourceforge.io/vice_17.html
 * Independent producer: cc1541, https://acoustic-velocity.com/cc1541/
 */
#ifndef XXFCLIB_FORMAT_CBM_D81_H
#define XXFCLIB_FORMAT_CBM_D81_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cbm_d81_variant_e {
    XX_CBM_D81_1581 = 81
} xx_cbm_d81_variant;
typedef struct xx_cbm_d81_s {
    Abstractformat format;
    xx_cbm_d81_variant variant;
    uint32_t track_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d81;
typedef xx_cbm_d81 xx_cbm_d81_t;
typedef xx_cbm_d81 XCBM_D81;
XXFC_API void xx_cbm_d81_init(xx_cbm_d81 *, xx_io_device *, int64_t);
XXFC_API xx_cbm_d81 *xx_cbm_d81_create(xx_io_device *, int64_t);
XXFC_API void xx_cbm_d81_destroy(xx_cbm_d81 *);
XXFC_API void xx_cbm_d81_free(xx_cbm_d81 *);
XXFC_API bool xx_cbm_d81_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d81_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d81_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d81_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d81_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d81_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d81_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d81_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d81_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d81_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d81_to_format(xx_cbm_d81 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
