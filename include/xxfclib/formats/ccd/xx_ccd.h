/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ccd.h @brief CloneCD CCD/IMG/SUB track reader.
 *
 * Supports single-session CloneCD Version 3 descriptors with sequential
 * unscrambled raw 2352-byte sectors and Mode 1, Mode 2 or audio tracks.
 * Each stored IMG track is listed and extracted byte-for-byte; when a SUB
 * sidecar is attached, each track's 96-byte-per-sector subchannel range is
 * listed separately. Pregaps absent from IMG/SUB are never synthesized.
 * Multi-session descriptors, INDEX 0 gaps, CD-Text and sector scrambling are
 * rejected. Attach borrowed sidecars or open same-basename .img/.sub files
 * next to the descriptor before creating an iterator. An IMG is required for
 * extraction, SUB is optional. Initial parser cap: 128 KiB / 99 tracks.
 * Operation MEMORY_LIMIT includes retained view, cursor, 64 KiB copy buffer;
 * MAX_MEMBER_SIZE limits each listed member.
 */
#ifndef XXFCLIB_FORMAT_CCD_H
#define XXFCLIB_FORMAT_CCD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ccd {
    Abstractformat format;
    uint32_t number_of_tracks;
    xx_io_device *img;
    xx_io_device *sub;
    bool img_owned;
    bool sub_owned;
    void *internal;
} xx_ccd;
typedef xx_ccd xx_ccd_t;
XXFC_API void xx_ccd_init(xx_ccd *, xx_io_device *, int64_t);
XXFC_API xx_ccd *xx_ccd_create(xx_io_device *, int64_t);
XXFC_API void xx_ccd_destroy(xx_ccd *);
XXFC_API void xx_ccd_free(xx_ccd *);
XXFC_API bool xx_ccd_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ccd_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_ccd_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_ccd_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_ccd_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_ccd_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_ccd_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_ccd_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_ccd_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_ccd_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_ccd_set_img_device(xx_ccd *, xx_io_device *);
XXFC_API bool xx_ccd_set_sub_device(xx_ccd *, xx_io_device *);
/** Returns 0, 1 or 2 newly or previously attached sidecar devices. */
XXFC_API uint32_t xx_ccd_open_data_files(xx_ccd *, const char *ccd_path);
XXFC_API uint32_t xx_ccd_get_number_of_tracks(xx_ccd *);
XXFC_API bool xx_ccd_test_magic(const uint8_t *, size_t);
static inline Abstractformat *xx_ccd_to_format(xx_ccd *v)
{
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
