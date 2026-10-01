/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_mds.h @brief Alcohol 120% MDS v1.4 / companion MDF reader.
 *
 * Lists and extracts exact raw 2352-byte stored tracks from the companion
 * MDF, including any postgap physically stored there. Declared pregaps are
 * not synthesized. Handles up to 32 CD-ROM sessions and 99 tracks; ordinary
 * audio, Mode 1, Mode 2, XA Form 1/Form 2 track modes are accepted when the
 * MDS declares a single unencrypted, uncompressed, subchannel-free MDF.
 * MDS v2 (DAEMON Tools MDX), split MDF, DVD media and altered sector layouts
 * are outside this bounded reader. Attach a borrowed MDF device or open the
 * matching-basename .mdf next to the descriptor before creating an iterator.
 * MEMORY_LIMIT accounts for retained parse view, iterator and 64 KiB copy
 * buffer; initial bounded descriptor parse is outside that operation budget.
 */
#ifndef XXFCLIB_FORMAT_MDS_H
#define XXFCLIB_FORMAT_MDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mds {
    Abstractformat format;
    uint32_t number_of_tracks;
    uint32_t number_of_sessions;
    xx_io_device *data;
    bool data_owned;
    void *internal;
} xx_mds;
typedef xx_mds xx_mds_t;
XXFC_API void xx_mds_init(xx_mds *, xx_io_device *, int64_t);
XXFC_API xx_mds *xx_mds_create(xx_io_device *, int64_t);
XXFC_API void xx_mds_destroy(xx_mds *);
XXFC_API void xx_mds_free(xx_mds *);
XXFC_API bool xx_mds_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mds_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_mds_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_mds_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_mds_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_mds_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_mds_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_mds_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_mds_extract_record_to_device(
    Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API void xx_mds_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_mds_set_data_device(xx_mds *, xx_io_device *);
XXFC_API bool xx_mds_open_data_file(xx_mds *, const char *mds_path);
XXFC_API uint32_t xx_mds_get_number_of_tracks(xx_mds *);
static inline Abstractformat *xx_mds_to_format(xx_mds *v) { return v ? &v->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
