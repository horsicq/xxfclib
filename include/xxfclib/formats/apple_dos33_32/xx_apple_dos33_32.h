/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native read-only Apple DOS 3.3 embedded 32-sector filesystem (.do).
 * Standard 50 tracks, 32 sectors/track, 256 bytes/sector in DOS order.
 * VTOC/catalog/T-S lists, sparse sectors, cycles, crosslinks and allocation
 * maps are validated. LOGICAL mode (default) trims T/A/I/B headers and
 * sequential text NUL padding; RAW_SECTORS preserves file sectors and holes.
 * No nibble/WOZ decoder, 13/16-sector DOS, deleted recovery, disk writing,
 * or text high-ASCII conversion. The distinct DOS3.3 reader handles16-sector.
 * Source cursor, short I/O, cancellation, resolved size/memory limits and
 * exclusive sibling staging are supported.
 * Primary: Beneath Apple DOS; CiderPress II DOS filesystem notes:
 * https://ciderpress2.com/formatdoc/DOS-notes.html
 */
#ifndef XXFCLIB_FORMAT_APPLE_DOS33_32_H
#define XXFCLIB_FORMAT_APPLE_DOS33_32_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_apple_dos33_32_mode_e {
    XX_APPLE_DOS33_32_MODE_LOGICAL = 0,
    XX_APPLE_DOS33_32_MODE_RAW_SECTORS = 1
} xx_apple_dos33_32_mode;
typedef struct xx_apple_dos33_32 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t track_count;
    uint8_t volume_number;
    xx_apple_dos33_32_mode mode;
} xx_apple_dos33_32;
typedef xx_apple_dos33_32 xx_apple_dos33_32_t;
typedef xx_apple_dos33_32 XAppleDOS33_32;
XXFC_API void xx_apple_dos33_32_init(xx_apple_dos33_32 *, xx_io_device *, int64_t);
XXFC_API void xx_apple_dos33_32_init_ex(xx_apple_dos33_32 *, xx_io_device *, int64_t, xx_apple_dos33_32_mode);
XXFC_API xx_apple_dos33_32 *xx_apple_dos33_32_create(xx_io_device *, int64_t);
XXFC_API void xx_apple_dos33_32_destroy(xx_apple_dos33_32 *);
XXFC_API void xx_apple_dos33_32_free(xx_apple_dos33_32 *);
XXFC_API bool xx_apple_dos33_32_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apple_dos33_32_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_apple_dos33_32_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_apple_dos33_32_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_apple_dos33_32_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_apple_dos33_32_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_apple_dos33_32_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_apple_dos33_32_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_apple_dos33_32_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_apple_dos33_32_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_apple_dos33_32_to_format(xx_apple_dos33_32 *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
