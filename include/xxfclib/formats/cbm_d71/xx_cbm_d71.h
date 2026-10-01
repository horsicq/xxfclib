/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore 1571 DOS on raw double-sided D71 images.
 *
 * This reader exposes every closed PRG, SEQ or USR directory member and
 * extracts its exact logical byte stream. A file's final sector uses its
 * second link byte as the inclusive last-byte index. The directory's block
 * count is checked against the complete chain. BAM counts/bitmaps, directory
 * sectors, file ownership and all track/sector references are validated.
 * PETSCII names are escaped to safe ASCII, with .prg/.seq/.usr suffixes and
 * aliases for host-name collisions. There are no subdirectories in 1571 DOS.
 *
 * The 70-track image has 1366x256 byte sectors. Side 0's BAM and directory
 * are at track 18; track 53/0 holds side 1's bitmap, while 18/0 bytes
 * DD-FF hold its per-track free counts. The complete two-side BAM is checked.
 * A trailing 1366-byte error map is reported as overlay and not interpreted.
 * REL, GEOS semantics, open/splat files, D81, G71 flux and deleted-file
 * recovery are outside this variant. Images with a live unsupported entry
 * fail validation rather than silently losing it.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Primary format evidence: Commodore 1570/1571 User's Guide and official
 * VICE D71 format manual, https://vice-emu.sourceforge.io/vice_17.html
 * Independent producer: cc1541, https://acoustic-velocity.com/cc1541/
 */
#ifndef XXFCLIB_FORMAT_CBM_D71_H
#define XXFCLIB_FORMAT_CBM_D71_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cbm_d71_variant_e {
    XX_CBM_D71_1571 = 71
} xx_cbm_d71_variant;
typedef struct xx_cbm_d71_s {
    Abstractformat format;
    xx_cbm_d71_variant variant;
    uint32_t track_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d71;
typedef xx_cbm_d71 xx_cbm_d71_t;
typedef xx_cbm_d71 XCBM_D71;
XXFC_API void xx_cbm_d71_init(xx_cbm_d71 *, xx_io_device *, int64_t);
XXFC_API xx_cbm_d71 *xx_cbm_d71_create(xx_io_device *, int64_t);
XXFC_API void xx_cbm_d71_destroy(xx_cbm_d71 *);
XXFC_API void xx_cbm_d71_free(xx_cbm_d71 *);
XXFC_API bool xx_cbm_d71_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d71_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d71_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d71_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d71_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d71_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d71_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d71_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d71_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d71_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d71_to_format(xx_cbm_d71 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
