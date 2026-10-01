/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore 2040/3040 DOS 1 on raw 35-track D67 images.
 *
 * This reader exposes every closed PRG, SEQ or USR directory member and
 * extracts its exact logical byte stream. A file's final sector uses its
 * second link byte as the inclusive last-byte index. The directory's block
 * count is checked against the complete chain. BAM counts/bitmaps, directory
 * sectors, file ownership and all track/sector references are validated.
 * PETSCII names are escaped to safe ASCII, with .prg/.seq/.usr suffixes and
 * aliases for host-name collisions. DOS 1 has no subdirectories.
 *
 * D67 has 690 256-byte sectors: tracks 1-17 contain 21, 18-24 contain 20,
 * 25-30 contain 18 and 31-35 contain 17. The DOS 1 BAM at 18/0 has version
 * byte 1, 35 four-byte allocation entries and points to the directory at
 * 18/1. Total raw size is 176,640 bytes. A trailing error-byte map is
 * reported as overlay and not interpreted. REL, GEOS semantics, open/splat
 * files, other DOS variants, flux and deleted-file recovery are excluded.
 * Images with a live unsupported entry fail validation.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Primary format evidence: Commodore CBM 2040/3040 user manual, Table 7,
 * https://www.manualslib.com/manual/1517372/Commodore-Cbm-2040.html?page=61
 * Independent producer: VICE c1541 v3.10,
 * https://vice-emu.sourceforge.io/manual/vice.pdf
 */
#ifndef XXFCLIB_FORMAT_CBM_D67_H
#define XXFCLIB_FORMAT_CBM_D67_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cbm_d67_s {
    Abstractformat format;
    uint32_t track_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d67;
typedef xx_cbm_d67 xx_cbm_d67_t;
typedef xx_cbm_d67 XCBM_D67;
XXFC_API void xx_cbm_d67_init(xx_cbm_d67 *, xx_io_device *, int64_t);
XXFC_API xx_cbm_d67 *xx_cbm_d67_create(xx_io_device *, int64_t);
XXFC_API void xx_cbm_d67_destroy(xx_cbm_d67 *);
XXFC_API void xx_cbm_d67_free(xx_cbm_d67 *);
XXFC_API bool xx_cbm_d67_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d67_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d67_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d67_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d67_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d67_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d67_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d67_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d67_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d67_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d67_to_format(xx_cbm_d67 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
