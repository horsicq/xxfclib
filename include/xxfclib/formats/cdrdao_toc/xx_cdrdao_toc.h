/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_cdrdao_toc.h @brief CDRDAO TOC sidecar track reader.
 *
 * This bounded reader accepts plain CD_ROM/CD_DA TOCs with one DATAFILE or
 * raw AUDIOFILE per track, optionally carrying frame offsets and lengths.
 * MODE1_RAW (2352), MODE1 (2048), MODE2_RAW (2352) and AUDIO (2352) expose
 * exactly the referenced stored bytes. It does not construct unrecorded
 * pregaps, index runs, CD-Text or decoded WAV/MP3 audio. Those instructions
 * are rejected. Up to 99 tracks and 64 KiB of TOC text are accepted.
 * Attach borrowed sidecar devices by track index, or open safe literal
 * basenames next to the .toc path before creating an iterator. MEMORY_LIMIT
 * covers retained view, iterator and 64 KiB transfer space; the initial
 * bounded text parser is outside that operation budget.
 */
#ifndef XXFCLIB_FORMAT_CDRDAO_TOC_H
#define XXFCLIB_FORMAT_CDRDAO_TOC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XX_CDRDAO_TOC_MAX_TRACKS 99U
typedef struct xx_cdrdao_toc {
    Abstractformat format;
    uint32_t number_of_tracks;
    xx_io_device *data[XX_CDRDAO_TOC_MAX_TRACKS];
    bool data_owned[XX_CDRDAO_TOC_MAX_TRACKS];
    void *internal;
} xx_cdrdao_toc;
typedef xx_cdrdao_toc xx_cdrdao_toc_t;
XXFC_API void xx_cdrdao_toc_init(xx_cdrdao_toc *, xx_io_device *, int64_t);
XXFC_API xx_cdrdao_toc *xx_cdrdao_toc_create(xx_io_device *, int64_t);
XXFC_API void xx_cdrdao_toc_destroy(xx_cdrdao_toc *);
XXFC_API void xx_cdrdao_toc_free(xx_cdrdao_toc *);
XXFC_API bool xx_cdrdao_toc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cdrdao_toc_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_cdrdao_toc_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_cdrdao_toc_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_cdrdao_toc_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_cdrdao_toc_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cdrdao_toc_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cdrdao_toc_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_cdrdao_toc_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_cdrdao_toc_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_cdrdao_toc_set_data_device(xx_cdrdao_toc *, uint32_t, xx_io_device *);
XXFC_API uint32_t xx_cdrdao_toc_open_data_files(xx_cdrdao_toc *, const char *toc_path);
XXFC_API uint32_t xx_cdrdao_toc_get_number_of_tracks(xx_cdrdao_toc *);
XXFC_API char *xx_cdrdao_toc_get_file_name(xx_cdrdao_toc *, uint32_t);
XXFC_API bool xx_cdrdao_toc_test_magic(const uint8_t *, size_t);
static inline Abstractformat *xx_cdrdao_toc_to_format(xx_cdrdao_toc *v) {
    return v ? &v->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
