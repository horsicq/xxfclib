/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Commodore D9060/D9090 DOS 3 on stock raw D90 hard-disk images.
 *
 * This reader exposes every closed PRG, SEQ or USR directory member and
 * extracts its exact logical byte stream. A file's final sector uses its
 * second link byte as the inclusive last-byte index. The directory's block
 * count is checked against the complete chain. BAM counts/bitmaps, directory
 * sectors, file ownership and all track/sector references are validated.
 * PETSCII names are escaped to safe ASCII, with .prg/.seq/.usr suffixes and
 * aliases for host-name collisions. DOS 3 has no subdirectories here.
 *
 * Stock D9060/D9090 images contain 153 cylinders, four or six heads and
 * 32 256-byte sectors per head (5,013,504 or 7,520,256 bytes). The default
 * factory selects D9090; init_ex selects D9060 explicitly. Configuration is
 * at track 0/0,
 * the empty bad-block list at 0/1, DOS 3 header at 76/20, directory starts
 * at 76/10 and 13 or 20 linked BAM sectors cover all cylinders. Five-byte BAM
 * entries per head, free counts, metadata/file sector ownership, directory
 * links and file chains are validated. Directory sectors may cross tracks.
 * The reader is bounded to 8192 files and 1024 directory sectors. Nonempty
 * bad-block lists, non-stock geometry, REL, open/splat entries,
 * deleted recovery and disk writes are excluded; live unsupported entries
 * fail validation. Trailing bytes are reported as overlay.
 * Source cursors, short I/O, cancellation, resolved size/memory budgets and
 * exclusive sibling staging on unpack are supported.
 *
 * Primary format evidence: VICE D90 image specification, section 16.11,
 * https://vice-emu.sourceforge.io/manual/vice.pdf
 * Independent producer: VICE c1541 v3.10,
 * https://vice-emu.sourceforge.io/manual/vice.pdf
 */
#ifndef XXFCLIB_FORMAT_CBM_D90_H
#define XXFCLIB_FORMAT_CBM_D90_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_cbm_d90_variant_e {
    XX_CBM_D90_9060 = 60,
    XX_CBM_D90_9090 = 90
} xx_cbm_d90_variant;
typedef struct xx_cbm_d90_s {
    Abstractformat format;
    xx_cbm_d90_variant variant;
    uint32_t track_count;
    uint32_t head_count;
    uint64_t number_of_records;
    char disk_name[64];
    char disk_id[8];
} xx_cbm_d90;
typedef xx_cbm_d90 xx_cbm_d90_t;
typedef xx_cbm_d90 XCBM_D90;
XXFC_API void xx_cbm_d90_init(xx_cbm_d90 *, xx_io_device *, int64_t);
XXFC_API void xx_cbm_d90_init_ex(xx_cbm_d90 *, xx_io_device *, int64_t, xx_cbm_d90_variant);
XXFC_API xx_cbm_d90 *xx_cbm_d90_create(xx_io_device *, int64_t);
XXFC_API xx_cbm_d90 *xx_cbm_d90_create_ex(xx_io_device *, int64_t, xx_cbm_d90_variant);
XXFC_API void xx_cbm_d90_destroy(xx_cbm_d90 *);
XXFC_API void xx_cbm_d90_free(xx_cbm_d90 *);
XXFC_API bool xx_cbm_d90_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cbm_d90_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cbm_d90_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cbm_d90_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cbm_d90_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cbm_d90_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cbm_d90_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d90_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cbm_d90_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cbm_d90_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_cbm_d90_to_format(xx_cbm_d90 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
