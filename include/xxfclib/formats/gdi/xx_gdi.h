/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gdi.h @brief Dreamcast GDI track descriptor reader.
 *
 * A .gdi descriptor lists one track per sidecar file (track number, disc LBA,
 * control, sector size, filename and byte offset). The descriptor has no
 * payload. Attach borrowed sidecar devices, or call xx_gdi_open_data_files
 * with the descriptor's path before creating an iterator. Members are the
 * exact stored track byte streams; LBA gaps are not fabricated. Data tracks
 * with 2048 or 2352 byte sectors and 2352 byte audio tracks are supported.
 * Other control/sector combinations are rejected. Initial descriptor parsing
 * is capped at 64 KiB and 99 tracks. The operation MEMORY_LIMIT accounts for
 * the retained track view, iterator and 64 KiB transfer buffer; it does not
 * govern the transient descriptor parser. MAX_MEMBER_SIZE limits each track.
 * Inputs remain owned by the caller unless opened via xx_gdi_open_data_files.
 */
#ifndef XXFCLIB_FORMAT_GDI_H
#define XXFCLIB_FORMAT_GDI_H

#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

#define XX_GDI_MAX_TRACKS 99U
typedef struct xx_gdi {
    Abstractformat format;
    uint32_t number_of_tracks;
    xx_io_device *data[XX_GDI_MAX_TRACKS];
    bool data_owned[XX_GDI_MAX_TRACKS];
    void *internal;
} xx_gdi;
typedef xx_gdi xx_gdi_t;

XXFC_API void xx_gdi_init(xx_gdi *, xx_io_device *, int64_t);
XXFC_API xx_gdi *xx_gdi_create(xx_io_device *, int64_t);
XXFC_API void xx_gdi_destroy(xx_gdi *);
XXFC_API void xx_gdi_free(xx_gdi *);
XXFC_API bool xx_gdi_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_gdi_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_gdi_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_gdi_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_gdi_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_gdi_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_gdi_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_gdi_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** Copy an exact track to a device; NULL destination verifies/read-checks it. */
XXFC_API bool xx_gdi_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_gdi_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
/** Attach a borrowed device to a zero-based track. The reader never closes it. */
XXFC_API bool xx_gdi_set_data_device(xx_gdi *, uint32_t, xx_io_device *);
/** Open safe single-component filenames next to the descriptor. */
XXFC_API uint32_t xx_gdi_open_data_files(xx_gdi *, const char *gdi_path);
XXFC_API uint32_t xx_gdi_get_number_of_tracks(xx_gdi *);
/** Filename as written, newly allocated; free with xx_str_free. */
XXFC_API char *xx_gdi_get_track_file_name(xx_gdi *, uint32_t);
/** Cheap detector prefilter; full validation requires check_is_valid. */
XXFC_API bool xx_gdi_test_magic(const uint8_t *, size_t);
static inline Abstractformat *xx_gdi_to_format(xx_gdi *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
