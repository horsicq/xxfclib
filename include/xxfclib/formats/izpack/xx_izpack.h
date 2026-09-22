/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_izpack.h @brief IzPack installer pack file ("packN") reader. */

#ifndef XXFCLIB_FORMAT_IZPACK_H
#define XXFCLIB_FORMAT_IZPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The "packN" members of an IzPack installer JAR.  Despite living inside a
 * JAR, a pack is not itself a ZIP: it is a Java object-serialization stream
 * of com.izforge.izpack.PackFile records, each followed by its file's bytes
 * in ObjectOutputStream block framing. */
typedef struct xx_izpack {
    Abstractformat format;
    uint64_t number_of_records;
    int32_t pack_version;   /**< 1..7, selected by the class descriptor. */
    int32_t declared_count; /**< Record count from the stream header. */
} xx_izpack;

typedef xx_izpack xx_izpack_t;
typedef xx_izpack XIzPack;

XXFC_API void xx_izpack_init(xx_izpack *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_izpack *xx_izpack_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_izpack_destroy(xx_izpack *archive);
XXFC_API void xx_izpack_free(xx_izpack *archive);

XXFC_API bool xx_izpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_izpack_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_izpack_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_izpack_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_izpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_izpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_izpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_izpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_izpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_izpack_to_format(xx_izpack *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IZPACK_H */
