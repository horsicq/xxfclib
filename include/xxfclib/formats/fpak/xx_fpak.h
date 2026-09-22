/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_fpak.h @brief FoxPro Distribution Kit (.pak) archive reader. */

#ifndef XXFCLIB_FORMAT_FPAK_H
#define XXFCLIB_FORMAT_FPAK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_fpak xx_fpak;
typedef struct xx_fpak xx_fpak_t;
typedef struct xx_fpak XFpakArchive;

/**
 * @brief A FoxPro Distribution Kit archive volume (.pak, .pa1, .pa2, ...).
 *
 * A lead volume starts "FPAK" and carries a 16-byte global header plus an
 * ASCII description; a continuation volume starts "FPAC" and jumps straight
 * into segments.  Every segment is an "FPPF" record: a 30-byte fixed header, a
 * name, and a slice of one member's PKZIP-Implode stream.  A member larger
 * than the remaining space on a volume is split across the media set, so a
 * member is one or more consecutive segments whose data sizes add up to the
 * packed size the segments all declare.
 *
 * This reader covers ONE device.  Joining a lead .PAK with its .PA1 / .PA2
 * siblings needs the source path, which an xx_io_device does not expose, so a
 * member whose stream continues on another volume is published as a
 * "<name>.partNN" record and refuses to extract.  See the port report.
 */
struct xx_fpak {
    Abstractformat format;
    uint64_t number_of_records; /**< Members assembled from this volume. */
    uint64_t number_of_segments; /**< FPPF records walked. */
    uint16_t version;           /**< Lead global header +4; 0 on a continuation. */
    bool is_lead;               /**< True for "FPAK", false for "FPAC". */
    bool is_truncated;          /**< The chain stopped before the end of the volume. */
    int64_t archive_size;       /**< How far the chain reached. */
};

XXFC_API void xx_fpak_init(xx_fpak *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_fpak *xx_fpak_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_fpak_destroy(xx_fpak *archive);
XXFC_API void xx_fpak_free(xx_fpak *archive);

XXFC_API bool xx_fpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_fpak_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_fpak_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_fpak_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_fpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_fpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_fpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_fpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_fpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_fpak_get_number_of_records(const xx_fpak *archive);
XXFC_API int64_t xx_fpak_get_archive_size(const xx_fpak *archive);
XXFC_API uint16_t xx_fpak_get_version(const xx_fpak *archive);

static inline Abstractformat *xx_fpak_to_format(xx_fpak *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FPAK_H */
